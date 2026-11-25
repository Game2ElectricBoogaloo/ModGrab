#include "app.hpp"

#include "config.hpp"
#include "installer.hpp"
#include "theme.hpp"
#include "widgets.hpp"

#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

namespace {

using ui::Tone;

struct LocalArchive {
    std::string name;
    std::string path;
    int64_t size = 0;
    int64_t modified = 0;
};

bool isArchiveName(const std::string& name) {
    for (const char* extension : {".zip", ".7z", ".rar"}) {
        size_t length = std::strlen(extension);
        if (name.size() > length && strcasecmp(name.c_str() + name.size() - length, extension) == 0)
            return true;
    }
    return false;
}

std::vector<LocalArchive> listLocalArchives() {
    std::vector<LocalArchive> archives;
    DIR* dir = opendir(cfg::LocalDir);
    if (!dir)
        return archives;
    while (dirent* entry = readdir(dir)) {
        std::string name = entry->d_name;
        if (!isArchiveName(name))
            continue;
        LocalArchive archive {name, std::string(cfg::LocalDir) + "/" + name};
        struct stat info {};
        if (stat(archive.path.c_str(), &info) == 0) {
            archive.size = info.st_size;
            archive.modified = info.st_mtime;
        }
        archives.push_back(std::move(archive));
    }
    closedir(dir);
    std::sort(archives.begin(), archives.end(), [](const LocalArchive& a, const LocalArchive& b) { return strcasecmp(a.name.c_str(), b.name.c_str()) < 0; });
    return archives;
}

std::string stem(const std::string& name) {
    size_t dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

std::string stateLabel(const QueuedDownload& job) {
    switch (job.state) {
    case DownloadState::Waiting: return "Waiting";
    case DownloadState::Downloading: return job.size > 0 ? std::to_string(job.done * 100 / job.size) + "%" : ui::formatSize(job.done);
    case DownloadState::Ready: return "Ready";
    case DownloadState::Failed: return "Failed";
    }
    return {};
}

Tone stateTone(DownloadState state) {
    switch (state) {
    case DownloadState::Ready: return Tone::Good;
    case DownloadState::Failed: return Tone::Bad;
    case DownloadState::Downloading: return Tone::Accent;
    case DownloadState::Waiting: break;
    }
    return Tone::Dim;
}

}

void App::installedScreen() {
    ui::ListCursor cursor;
    bool dirty = true;
    while (input.update()) {
        int count = static_cast<int>(store.mods().size());
        cursor.clamp(count, 1, theme::VisibleRows);
        u64 keys = input.pressed();
        dirty |= widgets::tappedRow(input, cursor, count, keys) >= 0;
        if (keys & HidNpadButton_B)
            return;
        if (keys & HidNpadButton_Y) {
            checkUpdates();
            dirty = true;
        } else if (count && (keys & HidNpadButton_X)) {
            uninstall(store.mods()[cursor.index].modId);
            dirty = true;
        } else if (count && (keys & HidNpadButton_Minus)) {
            toggle(store.mods()[cursor.index].modId);
            dirty = true;
        } else if (count && (keys & HidNpadButton_Plus)) {
            presetsMenu(store.mods()[cursor.index].titleId);
            dirty = true;
        } else if (count && (keys & HidNpadButton_A)) {
            const InstalledMod& picked = store.mods()[cursor.index];
            if (picked.isLocal())
                ui::notice(input, picked.modName, {"Installed from " + picked.fileName + " in the local files folder.", std::to_string(picked.files.size()) + " files."});
            else
                modScreen(titleFor(picked), picked.modId);
            dirty = true;
        }
        dirty |= cursor.move(input.navigation(), static_cast<int>(store.mods().size()), 1, theme::VisibleRows);
        dirty |= images.collect();
        if (!dirty)
            continue;

        const std::vector<InstalledMod>& shown = store.mods();
        count = static_cast<int>(shown.size());
        cursor.clamp(count, 1, theme::VisibleRows);
        ui::beginFrame("Installed mods", std::to_string(count) + " total",
                       {{HidNpadButton_B, "Back"}, {HidNpadButton_Plus, "Presets"}, {HidNpadButton_Minus, "On/off"}, {HidNpadButton_X, "Uninstall"},
                        {HidNpadButton_Y, "Check updates"}, {HidNpadButton_A, "Open"}});
        if (!count)
            ui::emptyMessage("Nothing installed through ModGrab yet.");
        for (int slot = 0; slot < theme::VisibleRows && cursor.top + slot < count; slot++) {
            const InstalledMod& mod = shown[cursor.top + slot];
            const Title* owner = findTitle(mod.titleId);
            bool gameChanged = owner && !mod.gameVersion.empty() && !owner->version.empty() && owner->version != mod.gameVersion;
            auto state = updateStatus.find(mod.modId);
            std::string tag;
            Tone tone = Tone::Good;
            if (!mod.enabled) {
                tag = "Off";
                tone = Tone::Dim;
            } else if (gameChanged) {
                tag = "Game updated";
                tone = Tone::Warn;
            } else if (state != updateStatus.end()) {
                bool update = state->second.rfind("update", 0) == 0;
                bool fine = state->second == "up to date" || state->second == "local file";
                tag = update ? "Update available" : state->second == "local file" ? "Local" : fine ? "Up to date" : "Check failed";
                tone = update ? Tone::Warn : fine ? Tone::Good : Tone::Bad;
            }
            std::string detail = mod.gameName + "   " + (mod.isLocal() ? "local file" : versionLabel(mod.version)) + "   installed " + ui::formatDate(mod.installedAt);
            widgets::drawRow(slot, cursor.top + slot == cursor.index, images.get(mod.imageUrl, theme::ThumbWidth, theme::ThumbHeight), mod.modName, detail, tag, tone);
        }
        widgets::rowScrollBar(cursor, count);
        if (count) {
            const InstalledMod& selected = shown[cursor.index];
            const Title* owner = findTitle(selected.titleId);
            auto state = updateStatus.find(selected.modId);
            if (owner && !selected.gameVersion.empty() && !owner->version.empty() && owner->version != selected.gameVersion)
                ui::status("Installed on game " + selected.gameVersion + ", the game is now " + owner->version + ". The mod may break, check for a newer file.", Tone::Warn);
            else if (state != updateStatus.end() && state->second.rfind("check failed", 0) == 0)
                ui::status(state->second, Tone::Bad);
            else
                ui::status(selected.fileName + "   " + std::to_string(selected.files.size()) + " files" +
                               (selected.backups.empty() ? "" : ", " + std::to_string(selected.backups.size()) + " backed up"),
                           Tone::Dim);
        }
        ui::endFrame();
        dirty = false;
    }
}

void App::localScreen() {
    std::string dirError = makeDirs(cfg::LocalDir);
    if (!dirError.empty()) {
        ui::notice(input, "Local files unavailable", {dirError});
        return;
    }
    std::vector<LocalArchive> archives = listLocalArchives();
    ui::ListCursor cursor;
    bool dirty = true;
    while (input.update()) {
        int count = static_cast<int>(archives.size());
        u64 keys = input.pressed();
        dirty |= widgets::tappedRow(input, cursor, count, keys) >= 0;
        if (keys & HidNpadButton_B)
            return;
        if (keys & HidNpadButton_Y) {
            archives = listLocalArchives();
            cursor.clamp(static_cast<int>(archives.size()), 1, theme::VisibleRows);
            dirty = true;
        } else if (count && (keys & HidNpadButton_A) && !titles.empty()) {
            const LocalArchive& archive = archives[cursor.index];
            std::vector<std::string> names;
            for (const Title& title : titles)
                names.push_back(title.name);
            if (auto choice = ui::choose(input, "Which game is " + archive.name + " for?", names)) {
                const Title& title = titles[*choice];
                auto existing = std::find_if(store.mods().begin(), store.mods().end(), [&](const InstalledMod& mod) {
                    return mod.isLocal() && mod.titleId == title.id && mod.fileName == archive.name;
                });
                ModSource source;
                source.modId = existing != store.mods().end() ? existing->modId : store.nextLocalId();
                source.modName = stem(archive.name);
                source.fileName = archive.name;
                source.fileDate = archive.modified;
                installArchive(title, source, archive.path, false);
            }
            dirty = true;
        }
        dirty |= cursor.move(input.navigation(), count, 1, theme::VisibleRows);
        if (!dirty)
            continue;

        ui::beginFrame("Local files", std::to_string(count) + " archives", {{HidNpadButton_B, "Back"}, {HidNpadButton_Y, "Refresh"}, {HidNpadButton_A, "Install"}});
        if (!count)
            ui::emptyMessage("Put .zip, .7z or .rar mods in switch/ModGrab/local, then press Y.");
        for (int slot = 0; slot < theme::VisibleRows && cursor.top + slot < count; slot++) {
            const LocalArchive& archive = archives[cursor.top + slot];
            bool installed = std::any_of(store.mods().begin(), store.mods().end(), [&](const InstalledMod& mod) { return mod.isLocal() && mod.fileName == archive.name; });
            widgets::drawRow(slot, cursor.top + slot == cursor.index, nullptr, archive.name, ui::formatSize(archive.size) + "   " + ui::formatDate(archive.modified),
                             installed ? "Installed" : "", Tone::Good);
        }
        widgets::rowScrollBar(cursor, count);
        ui::status("Copy mod archives into switch/ModGrab/local with DBI or ftpd. ModGrab sorts the files the same way it does for downloads.", Tone::Dim);
        ui::endFrame();
        dirty = false;
    }
}

void App::queueScreen() {
    ui::ListCursor cursor;
    std::vector<QueuedDownload> jobs = downloads.snapshot();
    bool dirty = true;
    while (input.update()) {
        if (downloads.takeChanged()) {
            jobs = downloads.snapshot();
            dirty = true;
        }
        int count = static_cast<int>(jobs.size());
        cursor.clamp(count, 1, theme::VisibleRows);
        u64 keys = input.pressed();
        dirty |= widgets::tappedRow(input, cursor, count, keys) >= 0;
        if (keys & HidNpadButton_B)
            return;
        if (count && (keys & HidNpadButton_X)) {
            downloads.remove(jobs[cursor.index].id);
            dirty = true;
        } else if (count && (keys & HidNpadButton_A)) {
            QueuedDownload job = jobs[cursor.index];
            if (job.state == DownloadState::Ready) {
                const Title* owner = findTitle(job.titleId);
                Title title = owner ? *owner : Title{job.titleId, formatTitleId(job.titleId), "", {}};
                installArchive(title, job.source, job.path, true);
                downloads.release(job.id);
            } else if (job.state == DownloadState::Failed) {
                downloads.remove(job.id);
                downloads.add(job);
            }
            dirty = true;
        }
        dirty |= cursor.move(input.navigation(), count, 1, theme::VisibleRows);
        dirty |= images.collect();
        if (!dirty)
            continue;

        ui::beginFrame("Downloads", std::to_string(count) + " queued", {{HidNpadButton_B, "Back"}, {HidNpadButton_X, "Remove"}, {HidNpadButton_A, "Install / retry"}});
        if (!count)
            ui::emptyMessage("Nothing queued. Press Y on a mod's file to queue it.");
        for (int slot = 0; slot < theme::VisibleRows && cursor.top + slot < count; slot++) {
            const QueuedDownload& job = jobs[cursor.top + slot];
            const Title* owner = findTitle(job.titleId);
            widgets::drawRow(slot, cursor.top + slot == cursor.index, images.get(job.source.imageUrl, theme::ThumbWidth, theme::ThumbHeight), job.source.modName,
                             (owner ? owner->name + "   " : "") + job.source.fileName + "   " + ui::formatSize(job.size), stateLabel(job), stateTone(job.state));
        }
        widgets::rowScrollBar(cursor, count);
        if (count && jobs[cursor.index].state == DownloadState::Failed)
            ui::status(jobs[cursor.index].error, Tone::Bad);
        ui::endFrame();
        dirty = false;
    }
}
