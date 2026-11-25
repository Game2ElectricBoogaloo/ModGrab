#include "app.hpp"

#include "compat.hpp"
#include "config.hpp"
#include "installer.hpp"
#include "layers.hpp"
#include "net.hpp"
#include "theme.hpp"
#include "updater.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <memory>
#include <set>

namespace {

using ui::Tone;

constexpr int64_t ProgressStep = 256 * 1024;
constexpr size_t MaxListedProblems = 12;
constexpr int ReleaseNoteLines = 6;

bool internetUp() {
    NifmInternetConnectionType type;
    u32 strength = 0;
    NifmInternetConnectionStatus status;
    return R_SUCCEEDED(nifmGetInternetConnectionStatus(&type, &strength, &status)) && status == NifmInternetConnectionStatus_Connected;
}

std::string safeFileName(const std::string& name) {
    std::string out;
    for (char c : name)
        out += (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_') ? c : '_';
    return out.empty() ? "download" : out;
}

bool containsPath(const std::vector<std::string>& paths, const std::string& path) {
    return std::any_of(paths.begin(), paths.end(), [&](const std::string& owned) { return samePath(owned, path); });
}

std::string downloadPath(const ModSource& source, const char* prefix) {
    return std::string(cfg::DownloadDir) + "/" + prefix + std::to_string(source.fileId) + "_" + safeFileName(source.fileName);
}

}

ModSource sourceFrom(const gb::Mod& mod, const gb::ModFile& file) {
    return {mod.id, mod.name, mod.previewUrl, mod.version, file.id, file.name, file.dateAdded};
}

net::ProgressFn App::progressReporter(const std::string& title, const std::string& detail, int64_t expectedSize) {
    auto lastDrawn = std::make_shared<int64_t>(-ProgressStep);
    return [this, title, detail, expectedSize, lastDrawn](int64_t now, int64_t total) {
        if (!input.update(false) || (input.pressed() & HidNpadButton_B))
            return false;
        if (now - *lastDrawn >= ProgressStep || (total && now == total)) {
            *lastDrawn = now;
            ui::progress(title, detail, now, total ? total : expectedSize, "Cancel");
        }
        return true;
    };
}

bool App::requireOnline() {
    if (internetUp())
        return true;
    ui::notice(input, "No internet", {"The Switch isn't connected to the internet.", "Connect in System Settings and try again."});
    return false;
}

std::string App::persist() {
    std::string error = store.save();
    if (!error.empty())
        ui::notice(input, "Couldn't save install list", {error, "Installed files are on the SD card but ModGrab may forget them."});
    return error;
}

void App::reportErrors(const std::string& title, const std::string& success, const std::vector<std::string>& errors) {
    if (errors.empty()) {
        ui::notice(input, title, {success});
        return;
    }
    std::vector<std::string> lines = {std::to_string(errors.size()) + " problems:"};
    for (size_t i = 0; i < errors.size() && i < MaxListedProblems; i++)
        lines.push_back(errors[i]);
    ui::notice(input, title + " with problems", lines);
}

bool App::confirmFile(const Title& title, const gb::Mod& mod, const gb::ModFile& file, const std::string& action) {
    const InstalledMod* previous = store.findMod(mod.id);
    std::vector<std::string> question = {file.name + "  (" + ui::formatSize(file.size) + ")", "for " + title.name};
    if (previous)
        question.push_back("Replaces installed " + previous->fileName + " (" + versionLabel(previous->version) + ")");
    Compatibility compatibility = checkCompatibility(title.version, mod, file);
    question.push_back("");
    question.push_back("Game version check: " + compatibility.detail);
    bool mismatch = compatibility.fit == Fit::Mismatch;
    return ui::confirm(input, mismatch ? "Version mismatch, " + action + " anyway?" : action + " " + mod.name + "?", question, mismatch ? Tone::Warn : Tone::Normal);
}

void App::installFile(const Title& title, const gb::Mod& mod, const gb::ModFile& file) {
    if (!confirmFile(title, mod, file, "Install") || !requireOnline())
        return;
    std::string dirError = makeDirs(cfg::DownloadDir);
    if (!dirError.empty()) {
        ui::notice(input, "Install failed", {dirError});
        return;
    }
    ModSource source = sourceFrom(mod, file);
    std::string archivePath = downloadPath(source, "");
    std::string error = net::download(file.downloadUrl, archivePath, file.size, progressReporter("Downloading " + mod.name, file.name, file.size));
    if (!error.empty()) {
        ui::notice(input, "Download failed", {error});
        return;
    }
    installArchive(title, source, archivePath, true);
}

void App::queueFile(const Title& title, const gb::Mod& mod, const gb::ModFile& file) {
    if (!confirmFile(title, mod, file, "Queue"))
        return;
    std::string dirError = makeDirs(cfg::DownloadDir);
    if (!dirError.empty()) {
        ui::notice(input, "Can't queue", {dirError});
        return;
    }
    std::vector<QueuedDownload> queued = downloads.snapshot();
    if (std::any_of(queued.begin(), queued.end(), [&](const QueuedDownload& job) { return job.source.fileId == file.id; })) {
        ui::notice(input, "Already queued", {file.name + " is already in Downloads."});
        return;
    }
    QueuedDownload job;
    job.titleId = title.id;
    job.source = sourceFrom(mod, file);
    job.url = file.downloadUrl;
    job.size = file.size;
    job.path = downloadPath(job.source, "queued_");
    downloads.add(std::move(job));
    ui::notice(input, "Queued", {file.name + " is downloading in the background.", "Install it from Downloads (press - on the games screen) when it's ready."});
}

bool App::installArchive(const Title& title, const ModSource& source, const std::string& archivePath, bool deleteArchive) {
    auto finish = [&](bool installed) {
        if (deleteArchive)
            std::remove(archivePath.c_str());
        return installed;
    };

    ui::busy("Installing " + source.modName, "Reading the archive...");
    ArchiveScan scan = scanArchive(archivePath, title.id);
    if (!scan.error.empty()) {
        ui::notice(input, "Can't read archive", {scan.error});
        return finish(false);
    }
    if (scan.placements.empty()) {
        std::vector<std::string> lines = {"Couldn't find romfs, exefs, exefs_patches or contents folders in this archive.",
                                          "It was kept at " + archivePath + " so you can install it by hand."};
        if (scan.blocked)
            lines.push_back(std::to_string(scan.blocked) + " files would have replaced Atmosphere or system files, ModGrab won't touch those.");
        ui::notice(input, "Nothing ModGrab can install", lines);
        return false;
    }

    std::string variant = scan.variants.front();
    if (scan.variants.size() > 1) {
        std::vector<std::string> labels;
        for (const std::string& root : scan.variants)
            labels.push_back((root.empty() ? "(archive root)" : root) + "   " + std::to_string(scan.countIn(root)) + " files");
        auto choice = ui::choose(input, "This mod has options, pick one", labels);
        if (!choice)
            return finish(false);
        variant = scan.variants[*choice];
    }

    const InstalledMod* previous = store.findMod(source.modId);
    std::vector<std::string> previousFiles = previous && previous->enabled ? previous->files : std::vector<std::string>{};
    std::set<std::string> overridden;
    int existing = 0;
    for (const Placement& placement : scan.placements) {
        if (placement.root != variant || containsPath(previousFiles, placement.dest))
            continue;
        int owner = store.topOwner(placement.dest);
        if (owner && owner != source.modId)
            overridden.insert(store.findMod(owner)->modName);
        else if (!owner && fileExists(placement.dest))
            existing++;
    }
    if (!overridden.empty() || existing) {
        std::vector<std::string> lines;
        if (!overridden.empty()) {
            std::string names;
            for (const std::string& name : overridden)
                names += (names.empty() ? "" : ", ") + name;
            lines.push_back("This mod replaces files from: " + names + ".");
        }
        if (existing)
            lines.push_back(std::to_string(existing) + " files already on the SD card will be replaced.");
        lines.push_back("");
        lines.push_back("Everything replaced is backed up and comes back when you uninstall or disable this mod.");
        if (!ui::confirm(input, "Mods overlap", lines, Tone::Warn))
            return finish(false);
    }

    bool replacingDisabled = previous && !previous->enabled;

    std::vector<FileBackup> created;
    std::vector<std::string> touched;
    ExtractOutcome outcome = extractArchive(
        archivePath, title.id, variant, scan.countIn(variant),
        [&](const std::string& dest, std::string& writeTo) {
            if (containsPath(touched, dest))
                return std::string();
            touched.push_back(dest);
            if (containsPath(previousFiles, dest)) {
                if (const InstalledMod* above = store.coveringMod(dest, source.modId))
                    writeTo = layers::backupLocation(above->modId, dest);
                return std::string();
            }
            return layers::protect(store, source.modId, dest, created);
        },
        [&](int done, int total) {
            if (done % 8 == 0 || done == total)
                ui::progress("Installing " + source.modName, "Extracting files", done, total, "");
        });

    if (!outcome.error.empty()) {
        std::vector<std::string> fresh;
        for (const std::string& dest : outcome.written)
            if (!containsPath(previousFiles, dest))
                fresh.push_back(dest);
        std::vector<std::string> leftovers = removeFiles(fresh);
        for (const FileBackup& backup : created) {
            std::string error = moveFile(layers::backupLocation(source.modId, backup.path), backup.path);
            if (!error.empty())
                leftovers.push_back(error);
        }
        std::vector<std::string> lines = {outcome.error};
        lines.push_back(previousFiles.empty() ? "Files from this attempt were removed and anything they replaced was put back."
                                              : "The installed version may be partly overwritten, reinstall it if the game acts up.");
        for (size_t i = 0; i < leftovers.size() && i < MaxListedProblems; i++)
            lines.push_back("Couldn't clean up: " + leftovers[i]);
        ui::notice(input, "Install failed", lines);
        return finish(false);
    }

    std::vector<std::string> problems;
    std::vector<FileBackup> keptBackups;
    if (replacingDisabled) {
        layers::appendErrors(problems, layers::discardStash(*store.findMod(source.modId)));
    } else if (previous) {
        for (const std::string& path : previousFiles)
            if (!containsPath(outcome.written, path))
                layers::appendErrors(problems, layers::detach(store, source.modId, path, ""));
        keptBackups = store.findMod(source.modId)->backups;
    }

    InstalledMod record;
    record.titleId = title.id;
    record.gameName = title.name;
    record.gameVersion = title.version;
    record.modId = source.modId;
    record.modName = source.modName;
    record.imageUrl = source.imageUrl;
    record.version = source.version;
    record.fileId = source.fileId;
    record.fileName = source.fileName;
    record.fileDate = source.fileDate;
    record.installedAt = static_cast<int64_t>(time(nullptr));
    record.files = outcome.written;
    record.backups = std::move(keptBackups);
    record.backups.insert(record.backups.end(), created.begin(), created.end());
    store.putMod(std::move(record));
    updateStatus.erase(source.modId);
    persist();

    std::vector<std::string> summary = {std::to_string(outcome.written.size()) + " files installed for " + title.name + "."};
    if (!created.empty())
        summary.push_back(std::to_string(created.size()) + " replaced files were backed up.");
    if (outcome.skipped)
        summary.push_back(std::to_string(outcome.skipped) + " extra files skipped (readmes, previews, other options).");
    if (scan.blocked)
        summary.push_back(std::to_string(scan.blocked) + " files left out because they'd replace Atmosphere or system files.");
    for (const std::string& problem : problems)
        summary.push_back(problem);
    summary.push_back("Restart the game for the mod to load.");
    ui::notice(input, "Installed " + source.modName, summary);
    return finish(true);
}

void App::uninstall(int modId) {
    const InstalledMod* mod = store.findMod(modId);
    if (!mod || !ui::confirm(input, "Uninstall " + mod->modName + "?",
                             {std::to_string(mod->files.size()) + " files will be removed from the SD card.", "Files this mod replaced are put back."}, Tone::Warn))
        return;
    std::string name = mod->modName;
    ui::busy("Uninstalling " + name, "Removing files...");
    std::vector<std::string> errors = layers::removeMod(store, modId);
    updateStatus.erase(modId);
    persist();
    reportErrors("Uninstalled", name + " was removed.", errors);
}

void App::toggle(int modId) {
    const InstalledMod* mod = store.findMod(modId);
    if (!mod)
        return;
    std::string name = mod->modName;
    bool enabling = !mod->enabled;
    ui::busy(enabling ? "Enabling " + name : "Disabling " + name, "Moving files...");
    std::vector<std::string> errors = enabling ? layers::enableMod(store, modId) : layers::disableMod(store, modId);
    persist();
    reportErrors(enabling ? "Enabled" : "Disabled", enabling ? name + " is back on." : name + " is off. Its files are parked until you enable it again.", errors);
}

void App::checkUpdates() {
    if (!requireOnline())
        return;
    std::vector<InstalledMod> mods = store.mods();
    for (size_t i = 0; i < mods.size(); i++) {
        if (mods[i].isLocal()) {
            updateStatus[mods[i].modId] = "local file";
            continue;
        }
        ui::progress("Checking for updates", mods[i].modName, static_cast<int64_t>(i), static_cast<int64_t>(mods.size()), "");
        std::string error;
        auto latest = gb::fetchMod(mods[i].modId, error);
        if (!latest)
            updateStatus[mods[i].modId] = "check failed: " + error;
        else if (hasUpdate(mods[i], *latest))
            updateStatus[mods[i].modId] = "update: " + versionLabel(latest->version);
        else
            updateStatus[mods[i].modId] = "up to date";
    }
}

void App::presetsMenu(uint64_t titleId) {
    const Title* owner = findTitle(titleId);
    std::string gameName = owner ? owner->name : formatTitleId(titleId);
    std::vector<Preset>& presets = store.presetsFor(titleId);
    std::vector<std::string> options = {"Save the current setup as a preset"};
    for (const Preset& preset : presets)
        options.push_back(preset.name + "   " + std::to_string(preset.modIds.size()) + " mods on");
    auto choice = ui::choose(input, "Presets for " + gameName, options);
    if (!choice)
        return;

    if (*choice == 0) {
        std::string name = "Preset " + std::to_string(presets.size() + 1);
        if (appletGetAppletType() == AppletType_Application)
            if (auto entered = ui::keyboard("Preset name", name); entered && !entered->empty())
                name = *entered;
        Preset preset {name, {}};
        for (const InstalledMod& mod : store.mods())
            if (mod.titleId == titleId && mod.enabled)
                preset.modIds.push_back(mod.modId);
        presets.push_back(std::move(preset));
        persist();
        ui::notice(input, "Preset saved", {"\"" + name + "\" remembers which mods are on right now."});
        return;
    }

    size_t index = static_cast<size_t>(*choice - 1);
    auto action = ui::choose(input, presets[index].name, {"Apply this preset", "Delete this preset"});
    if (!action)
        return;
    if (*action == 1) {
        presets.erase(presets.begin() + static_cast<long>(index));
        persist();
        return;
    }

    std::vector<int> wanted = presets[index].modIds;
    std::vector<std::string> errors;
    ui::busy("Applying " + presets[index].name, "Turning mods on and off...");
    std::vector<int> ids;
    for (const InstalledMod& mod : store.mods())
        if (mod.titleId == titleId)
            ids.push_back(mod.modId);
    for (int id : ids) {
        bool shouldRun = std::find(wanted.begin(), wanted.end(), id) != wanted.end();
        const InstalledMod* mod = store.findMod(id);
        if (mod->enabled && !shouldRun)
            layers::appendErrors(errors, layers::disableMod(store, id));
        else if (!mod->enabled && shouldRun)
            layers::appendErrors(errors, layers::enableMod(store, id));
    }
    persist();
    reportErrors("Preset applied", presets[index].name + " is active.", errors);
}

void App::offerSelfUpdate() {
    if (!*cfg::UpdateRepo || !internetUp())
        return;
    std::string error;
    std::optional<Release> release = findUpdate(error);
    if (!release)
        return;
    std::vector<std::string> lines = {"Version " + release->version + " is out, you have " + cfg::AppVersion + "."};
    for (const std::string& line : gfx::wrap(release->notes, theme::BodySize, 700, ReleaseNoteLines))
        lines.push_back(line);
    if (!ui::confirm(input, "Update ModGrab?", lines))
        return;
    error = installUpdate(*release, selfPath, progressReporter("Updating ModGrab", "Version " + release->version, release->size));
    if (error.empty())
        ui::notice(input, "Updated", {"ModGrab " + release->version + " is installed. Close and reopen ModGrab to use it."});
    else
        ui::notice(input, "Update failed", {error});
}
