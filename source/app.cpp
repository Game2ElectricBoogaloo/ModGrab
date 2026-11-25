#include "app.hpp"

#include "compat.hpp"
#include "config.hpp"
#include "layers.hpp"
#include "originals.hpp"
#include "theme.hpp"
#include "widgets.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <strings.h>

namespace {

using ui::Tone;
using widgets::RowPitch;
using widgets::RowWidth;

constexpr u64 PageBack = HidNpadButton_L | HidNpadButton_ZL;
constexpr u64 PageNext = HidNpadButton_R | HidNpadButton_ZR;
constexpr int FilesLeft = theme::Margin + theme::PreviewWidth + 32;
constexpr int FilesWidth = gfx::Width - theme::Margin - FilesLeft - 20;
constexpr int GalleryWidth = 640;
constexpr int GalleryHeight = 360;
constexpr int MaxDescriptionLines = 400;

time_t buildTime() {
    static const char* const Months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char month[4] = {};
    int day = 1;
    int year = 1970;
    std::sscanf(__DATE__, "%3s %d %d", month, &day, &year);
    std::tm built {};
    built.tm_year = year - 1900;
    built.tm_mon = static_cast<int>((std::strstr(Months, month) - Months) / 3);
    built.tm_mday = day;
    return std::mktime(&built);
}

std::optional<std::string> clockProblem() {
    time_t now = time(nullptr);
    time_t built = buildTime();
    if (now >= built)
        return std::nullopt;
    return "The Switch thinks today is " + ui::formatDate(now) + ", before this version of ModGrab was even built (" + ui::formatDate(built) +
           "). Downloads will fail with certificate errors until the date is right.";
}

std::string stripPlatformTag(std::string name) {
    for (const char* tag : {" (Switch)", " (NX)"}) {
        size_t length = std::strlen(tag);
        if (name.size() > length && strcasecmp(name.c_str() + name.size() - length, tag) == 0)
            name.erase(name.size() - length);
    }
    return name;
}

Tone fitTone(Fit fit) {
    switch (fit) {
    case Fit::Match: return Tone::Good;
    case Fit::Mismatch: return Tone::Warn;
    case Fit::Unknown: break;
    }
    return Tone::Dim;
}

}

bool hasUpdate(const InstalledMod& installed, const gb::Mod& mod) {
    if (!installed.version.empty() && !mod.version.empty() && installed.version != mod.version)
        return true;
    return std::any_of(mod.files.begin(), mod.files.end(), [&](const gb::ModFile& file) {
        return file.id != installed.fileId && file.dateAdded > installed.fileDate && file.dateAdded > installed.installedAt;
    });
}

std::string versionLabel(const std::string& version) {
    return version.empty() ? "no version" : "v" + version;
}

App::App(std::string startupWarning, std::string selfPath) : warning(std::move(startupWarning)), selfPath(std::move(selfPath)) {}

void App::run() {
    std::string error = store.load();
    if (!error.empty())
        ui::notice(input, "Saved data problem", {error});

    ui::busy("ModGrab", "Reading your installed games...");
    titles = listInstalledTitles(error);
    if (!error.empty())
        ui::notice(input, "Couldn't list games", {error});
    if (!warning.empty())
        ui::notice(input, "Heads up", {warning});
    remergeWaiting();
    if (auto problem = clockProblem())
        ui::notice(input, "Check the date and time", {*problem, "", "Fix it in System Settings > System > Date and Time, then restart ModGrab."});
    else
        offerSelfUpdate();

    gamesScreen();
}

void App::remergeWaiting() {
    for (const Title& title : titles)
        originals::useVersion(title.id, title.version);
    if (std::none_of(store.mergedFiles().begin(), store.mergedFiles().end(), layers::retriable))
        return;
    ui::busy("ModGrab", "Merging mod files...");
    size_t merged = layers::remergePending(store);
    std::vector<std::string> notes = layers::takeNotes();
    persist();
    std::vector<std::string> lines = {std::to_string(merged) + " shared files were merged."};
    lines.insert(lines.end(), notes.begin(), notes.end());
    ui::notice(input, "Merged mods", lines);
}

const Title* App::findTitle(uint64_t titleId) const {
    auto owner = std::find_if(titles.begin(), titles.end(), [&](const Title& title) { return title.id == titleId; });
    return owner == titles.end() ? nullptr : &*owner;
}

Title App::titleFor(const InstalledMod& mod) const {
    const Title* owner = findTitle(mod.titleId);
    return owner ? *owner : Title{mod.titleId, mod.gameName, "", {}};
}

void App::gamesScreen() {
    ui::ListCursor cursor;
    int count = static_cast<int>(titles.size());
    bool dirty = true;
    while (input.update()) {
        u64 keys = input.pressed();
        if (auto at = input.tap()) {
            int row = widgets::slotAt(*at, widgets::GridLeft, gfx::Width - 2 * widgets::GridLeft, theme::ContentTop, theme::TileRowHeight, theme::TileRows);
            int column = (at->x - widgets::GridLeft) / (theme::TileSize + theme::TileGap);
            int item = cursor.top + row * theme::TileColumns + column;
            if (row >= 0 && column < theme::TileColumns && item < count) {
                if (item == cursor.index)
                    keys |= HidNpadButton_A;
                cursor.index = item;
                dirty = true;
            }
        }
        if (keys & HidNpadButton_Plus)
            return;
        if (keys & HidNpadButton_Y) {
            installedScreen();
            dirty = true;
        } else if (keys & HidNpadButton_X) {
            localScreen();
            dirty = true;
        } else if (keys & HidNpadButton_Minus) {
            queueScreen();
            dirty = true;
        } else if ((keys & HidNpadButton_A) && count) {
            const Title& title = titles[cursor.index];
            if (auto game = resolveGame(title, false))
                modsScreen(title, *game);
            dirty = true;
        }
        dirty |= cursor.move(input.navigation(), count, theme::TileColumns, theme::TileRows);
        dirty |= downloads.takeChanged();
        if (!dirty)
            continue;

        int ready = downloads.readyCount();
        std::string corner = std::to_string(store.mods().size()) + " mods installed";
        if (ready)
            corner = std::to_string(ready) + " ready to install   " + corner;
        ui::beginFrame(std::string(cfg::AppName) + " " + cfg::AppVersion, corner,
                       {{HidNpadButton_Plus, "Exit"}, {HidNpadButton_Minus, "Downloads"}, {HidNpadButton_X, "Local files"}, {HidNpadButton_Y, "Installed"}, {HidNpadButton_A, "Browse"}});
        if (!count)
            ui::emptyMessage("No games found on this Switch.");
        for (int slot = 0; slot < theme::TileColumns * theme::TileRows && cursor.top + slot < count; slot++) {
            int item = cursor.top + slot;
            const Title& title = titles[item];
            int x = widgets::GridLeft + (slot % theme::TileColumns) * (theme::TileSize + theme::TileGap);
            int y = theme::ContentTop + (slot / theme::TileColumns) * theme::TileRowHeight;
            bool selected = item == cursor.index;
            if (selected)
                widgets::selectionOutline(x, y, theme::TileSize, theme::TileSize, theme::Radius);
            if (title.icon.empty())
                widgets::placeholder(x, y, theme::TileSize, theme::TileSize, title.name);
            else
                gfx::drawImage(title.icon, x, y, theme::Radius);
            int installed = static_cast<int>(std::count_if(store.mods().begin(), store.mods().end(), [&](const InstalledMod& mod) { return mod.titleId == title.id; }));
            if (installed)
                widgets::badge(x + theme::TileSize - 8, y + 8, std::to_string(installed), Tone::Good);
            gfx::drawText(x, y + theme::TileSize + 14, gfx::ellipsize(title.name, theme::SmallSize, theme::TileSize), theme::SmallSize, selected ? theme::Text : theme::TextDim);
        }
        int rows = (count + theme::TileColumns - 1) / theme::TileColumns;
        ui::scrollBar(cursor.top / theme::TileColumns, theme::TileRows, rows, gfx::Width - 24, theme::ContentTop, theme::TileRows * theme::TileRowHeight - 40);
        if (count) {
            const Title& selected = titles[cursor.index];
            ui::status(selected.name + "   " + versionLabel(selected.version) + "   " + formatTitleId(selected.id), Tone::Dim);
        }
        ui::endFrame();
        dirty = false;
    }
}

std::optional<gb::Game> App::resolveGame(const Title& title, bool forcePick) {
    int linked = store.gameFor(title.id);
    if (linked && !forcePick)
        return gb::Game{linked, title.name};
    if (!requireOnline())
        return std::nullopt;

    ui::busy("Finding " + title.name, "Looking it up on GameBanana...");
    std::string error;
    std::vector<gb::Game> matches = gb::searchGames(title.name, error);
    if (!error.empty()) {
        ui::notice(input, "Couldn't reach GameBanana", {error});
        return std::nullopt;
    }
    if (matches.empty()) {
        ui::notice(input, "Not on GameBanana", {"No GameBanana game matched \"" + title.name + "\"."});
        return std::nullopt;
    }

    std::optional<gb::Game> picked;
    if (!forcePick) {
        for (const gb::Game& match : matches) {
            if (strcasecmp(stripPlatformTag(match.name).c_str(), stripPlatformTag(title.name).c_str()) == 0) {
                picked = match;
                break;
            }
        }
    }
    if (!picked) {
        std::vector<std::string> names;
        for (const gb::Game& match : matches)
            names.push_back(match.name);
        auto choice = ui::choose(input, "Which GameBanana page is " + title.name + "?", names);
        if (!choice)
            return std::nullopt;
        picked = matches[*choice];
    }

    store.linkGame(title.id, picked->id);
    persist();
    return picked;
}

void App::modsScreen(const Title& title, const gb::Game& linkedGame) {
    gb::Game game = linkedGame;
    std::string query;
    int page = 1;
    gb::ModPage listing;
    ui::ListCursor cursor;
    std::string loadError;

    auto load = [&]() {
        if (!requireOnline()) {
            loadError = "No internet connection.";
            return;
        }
        ui::busy(title.name, "Loading page " + std::to_string(page) + "...");
        std::string error;
        auto fetched = query.empty() ? gb::listMods(game.id, page, error) : gb::searchMods(game.id, query, page, error);
        loadError = error;
        listing = fetched ? std::move(*fetched) : gb::ModPage{};
        cursor = {};
    };

    load();
    bool dirty = true;
    while (input.update()) {
        int count = static_cast<int>(listing.mods.size());
        u64 keys = input.pressed();
        dirty |= widgets::tappedRow(input, cursor, count, keys) >= 0;
        if (keys & HidNpadButton_B)
            return;
        if ((keys & HidNpadButton_A) && count) {
            modScreen(title, listing.mods[cursor.index].id);
            dirty = true;
        } else if ((keys & PageNext) && !listing.complete) {
            page++;
            load();
            dirty = true;
        } else if ((keys & PageBack) && page > 1) {
            page--;
            load();
            dirty = true;
        } else if (keys & HidNpadButton_Y) {
            if (appletGetAppletType() != AppletType_Application) {
                ui::notice(input, "Keyboard unavailable", {"Search needs the keyboard, which only works in full app mode.",
                                                           "Launch ModGrab by holding R while opening a game."});
            } else if (auto entered = ui::keyboard("Search mods", query)) {
                query = *entered;
                page = 1;
                load();
            }
            dirty = true;
        } else if (keys & HidNpadButton_X) {
            if (auto relinked = resolveGame(title, true)) {
                game = *relinked;
                query.clear();
                page = 1;
                load();
            }
            dirty = true;
        }
        dirty |= cursor.move(input.navigation(), count, 1, theme::VisibleRows);
        dirty |= images.collect();
        if (!dirty)
            continue;

        std::vector<ui::Hint> hints = {{HidNpadButton_B, "Back"}, {HidNpadButton_X, "Change game"}, {HidNpadButton_Y, "Search"}};
        if (page > 1)
            hints.push_back({HidNpadButton_L, "Prev"});
        if (!listing.complete)
            hints.push_back({HidNpadButton_R, "Next"});
        hints.push_back({HidNpadButton_A, "Open"});
        ui::beginFrame(title.name, "Page " + std::to_string(page), hints);
        if (!loadError.empty())
            ui::emptyMessage(loadError);
        else if (!count)
            ui::emptyMessage(query.empty() ? "No mods for this game yet." : "Nothing matched \"" + query + "\".");
        for (int slot = 0; slot < theme::VisibleRows && cursor.top + slot < count; slot++) {
            const gb::ModSummary& mod = listing.mods[cursor.top + slot];
            const InstalledMod* installed = store.findMod(mod.id);
            std::string tag = !installed ? "" : installed->enabled ? "Installed" : "Disabled";
            widgets::drawRow(slot, cursor.top + slot == cursor.index, images.get(mod.thumbnailUrl, theme::ThumbWidth, theme::ThumbHeight), mod.name,
                             "by " + mod.submitter + "   updated " + ui::formatDate(mod.dateUpdated), tag, installed && installed->enabled ? Tone::Good : Tone::Dim);
        }
        widgets::rowScrollBar(cursor, count);
        std::string scope = query.empty() ? "All mods" : "Search: " + query;
        if (listing.total)
            scope += "   " + std::to_string(listing.total) + " total";
        ui::status(scope, Tone::Dim);
        ui::endFrame();
        dirty = false;
    }
}

void App::modScreen(const Title& title, int modId) {
    if (!requireOnline())
        return;
    ui::busy(title.name, "Loading mod...");
    std::string error;
    std::optional<gb::Mod> mod = gb::fetchMod(modId, error);
    if (!mod) {
        ui::notice(input, "Couldn't load mod", {error});
        return;
    }

    ui::ListCursor cursor;
    int count = static_cast<int>(mod->files.size());
    bool dirty = true;
    while (input.update()) {
        const InstalledMod* installed = store.findMod(modId);
        u64 keys = input.pressed();
        if (auto at = input.tap()) {
            int slot = widgets::slotAt(*at, FilesLeft, FilesWidth, theme::ContentTop + 48, theme::FileRowHeight, theme::VisibleFileRows);
            if (slot >= 0 && cursor.top + slot < count) {
                if (cursor.top + slot == cursor.index)
                    keys |= HidNpadButton_A;
                cursor.index = cursor.top + slot;
                dirty = true;
            }
        }
        if (keys & HidNpadButton_B)
            return;
        if ((keys & HidNpadButton_A) && count) {
            installFile(title, *mod, mod->files[cursor.index]);
            dirty = true;
        } else if ((keys & HidNpadButton_Y) && count) {
            queueFile(title, *mod, mod->files[cursor.index]);
            dirty = true;
        } else if ((keys & HidNpadButton_X) && installed) {
            uninstall(modId);
            dirty = true;
        } else if (keys & HidNpadButton_Minus) {
            detailsScreen(*mod);
            dirty = true;
        }
        dirty |= cursor.move(input.navigation(), count, 1, theme::VisibleFileRows);
        dirty |= images.collect();
        if (!dirty)
            continue;

        installed = store.findMod(modId);
        std::vector<ui::Hint> hints = {{HidNpadButton_B, "Back"}, {HidNpadButton_Minus, "Details"}};
        if (installed)
            hints.push_back({HidNpadButton_X, "Uninstall"});
        if (count) {
            hints.push_back({HidNpadButton_Y, "Queue"});
            hints.push_back({HidNpadButton_A, installed && installed->fileId == mod->files[cursor.index].id ? "Reinstall" : "Install"});
        }
        ui::beginFrame(mod->name, versionLabel(mod->version), hints);

        int x = theme::Margin;
        int y = theme::ContentTop;
        if (const gfx::Image* preview = images.get(mod->previewUrl, theme::PreviewWidth, theme::PreviewHeight))
            gfx::drawImage(*preview, x, y, theme::Radius);
        else
            widgets::placeholder(x, y, theme::PreviewWidth, theme::PreviewHeight, mod->name);
        y += theme::PreviewHeight + 18;
        gfx::drawText(x, y, gfx::ellipsize("by " + mod->submitter + "   updated " + ui::formatDate(mod->dateUpdated), theme::SmallSize, theme::PreviewWidth), theme::SmallSize, theme::TextDim);
        y += 32;
        if (count) {
            Compatibility compatibility = checkCompatibility(title.version, *mod, mod->files[cursor.index]);
            for (const std::string& line : gfx::wrap(compatibility.detail, theme::SmallSize, theme::PreviewWidth, 2)) {
                gfx::drawText(x, y, line, theme::SmallSize, ui::toneColor(fitTone(compatibility.fit)));
                y += 28;
            }
        }
        if (installed) {
            bool update = hasUpdate(*installed, *mod);
            std::string line = !installed->enabled ? "Installed but disabled" : update ? "Update available, installed " + versionLabel(installed->version) : "Installed and up to date";
            Tone tone = !installed->enabled ? Tone::Dim : update ? Tone::Warn : Tone::Good;
            gfx::drawText(x, y, gfx::ellipsize(line, theme::SmallSize, theme::PreviewWidth), theme::SmallSize, ui::toneColor(tone));
            y += 28;
        }
        for (const std::string& line : gfx::wrap(mod->description, theme::SmallSize, theme::PreviewWidth, std::max(0, (theme::FooterTop - 20 - y) / 28))) {
            gfx::drawText(x, y, line, theme::SmallSize, theme::TextDim);
            y += 28;
        }

        gfx::drawText(FilesLeft, theme::ContentTop, "Files", theme::HeadingSize, theme::Text);
        if (!count)
            gfx::drawText(FilesLeft, theme::ContentTop + 60, "This mod has no downloadable files.", theme::BodySize, theme::TextDim);
        for (int slot = 0; slot < theme::VisibleFileRows && cursor.top + slot < count; slot++) {
            const gb::ModFile& file = mod->files[cursor.top + slot];
            int rowY = theme::ContentTop + 48 + slot * theme::FileRowHeight;
            bool selected = cursor.top + slot == cursor.index;
            int height = theme::FileRowHeight - 8;
            if (selected)
                widgets::selectionOutline(FilesLeft, rowY, FilesWidth, height, theme::Radius);
            gfx::roundRect(FilesLeft, rowY, FilesWidth, height, theme::Radius, selected ? theme::SurfaceRaised : theme::Surface);
            bool current = installed && installed->fileId == file.id;
            std::string meta = ui::formatSize(file.size) + "   " + ui::formatDate(file.dateAdded);
            int metaWidth = gfx::textWidth(meta, theme::SmallSize);
            gfx::drawText(FilesLeft + 16, rowY + 15, gfx::ellipsize(file.name, theme::BodySize, FilesWidth - metaWidth - 56), theme::BodySize, current ? theme::Good : theme::Text);
            gfx::drawText(FilesLeft + FilesWidth - 16 - metaWidth, rowY + 18, meta, theme::SmallSize, theme::TextDim);
        }
        ui::scrollBar(cursor.top, theme::VisibleFileRows, count, gfx::Width - 24, theme::ContentTop + 48, theme::VisibleFileRows * theme::FileRowHeight - 8);
        if (count) {
            int noteY = theme::ContentTop + 56 + std::min(count, theme::VisibleFileRows) * theme::FileRowHeight;
            for (const std::string& line : gfx::wrap(mod->files[cursor.index].description, theme::SmallSize, FilesWidth, std::max(0, (theme::FooterTop - 20 - noteY) / 28))) {
                gfx::drawText(FilesLeft, noteY, line, theme::SmallSize, theme::TextDim);
                noteY += 28;
            }
        }
        ui::endFrame();
        dirty = false;
    }
}

void App::detailsScreen(const gb::Mod& mod) {
    std::vector<std::string> gallery = mod.gallery.empty() && !mod.previewUrl.empty() ? std::vector<std::string>{mod.previewUrl} : mod.gallery;
    std::vector<std::string> lines = gfx::wrap(mod.description.empty() ? "No description." : mod.description, theme::SmallSize, gfx::Width - 2 * theme::Margin, MaxDescriptionLines);
    int shown = 0;
    int scroll = 0;
    int textTop = theme::ContentTop + GalleryHeight + 24;
    int visibleLines = (theme::FooterTop - 16 - textTop) / 28;
    int maxScroll = std::max(0, static_cast<int>(lines.size()) - visibleLines);
    bool dirty = true;
    while (input.update()) {
        u64 keys = input.navigation();
        if (input.pressed() & HidNpadButton_B)
            return;
        int count = static_cast<int>(gallery.size());
        if (count && (keys & (HidNpadButton_AnyRight | HidNpadButton_R))) {
            shown = (shown + 1) % count;
            dirty = true;
        } else if (count && (keys & (HidNpadButton_AnyLeft | HidNpadButton_L))) {
            shown = (shown + count - 1) % count;
            dirty = true;
        } else if ((keys & HidNpadButton_AnyDown) && scroll < maxScroll) {
            scroll++;
            dirty = true;
        } else if ((keys & HidNpadButton_AnyUp) && scroll > 0) {
            scroll--;
            dirty = true;
        }
        dirty |= images.collect();
        if (!dirty)
            continue;

        ui::beginFrame(mod.name, count ? "Image " + std::to_string(shown + 1) + " of " + std::to_string(count) : "", {{HidNpadButton_B, "Back"}});
        int x = (gfx::Width - GalleryWidth) / 2;
        const gfx::Image* image = count ? images.get(gallery[shown], GalleryWidth, GalleryHeight) : nullptr;
        if (image)
            gfx::drawImage(*image, x, theme::ContentTop, theme::Radius);
        else
            widgets::placeholder(x, theme::ContentTop, GalleryWidth, GalleryHeight, mod.name);
        if (count > 1) {
            gfx::drawText(x - 40, theme::ContentTop + GalleryHeight / 2 - 20, "<", theme::TitleSize, theme::TextDim);
            gfx::drawText(x + GalleryWidth + 18, theme::ContentTop + GalleryHeight / 2 - 20, ">", theme::TitleSize, theme::TextDim);
        }
        for (int line = 0; line < visibleLines && scroll + line < static_cast<int>(lines.size()); line++)
            gfx::drawText(theme::Margin, textTop + line * 28, lines[scroll + line], theme::SmallSize, theme::Text);
        ui::scrollBar(scroll, visibleLines, static_cast<int>(lines.size()), gfx::Width - 24, textTop, visibleLines * 28);
        ui::endFrame();
        dirty = false;
    }
}
