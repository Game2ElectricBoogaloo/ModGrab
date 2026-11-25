#include "layers.hpp"

#include "config.hpp"
#include "installer.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace layers {

namespace {

std::string underRoot(const char* root, int modId, const std::string& path) {
    std::string relative = path.rfind(cfg::SdRoot, 0) == 0 ? path.substr(std::strlen(cfg::SdRoot)) : path;
    return std::string(root) + "/" + std::to_string(modId) + "/" + relative;
}

std::string discard(const std::string& path) {
    if (std::remove(path.c_str()) != 0 && errno != ENOENT)
        return "can't delete " + path + ": " + std::strerror(errno);
    pruneParents(path);
    return {};
}

void addError(std::vector<std::string>& errors, std::string error) {
    if (!error.empty())
        errors.push_back(std::move(error));
}

std::string attach(Store& store, int modId, const std::string& path, std::vector<FileBackup>& created) {
    std::string stashed = stashLocation(modId, path);
    if (!fileExists(stashed))
        return "missing from the disabled folder: " + path + ", reinstall the mod to fix it";
    size_t before = created.size();
    std::string error = protect(store, modId, path, created);
    if (!error.empty())
        return error;
    error = moveFile(stashed, path);
    if (!error.empty() && created.size() > before) {
        std::string restore = moveFile(backupLocation(modId, path), path);
        created.pop_back();
        if (!restore.empty())
            error += "; " + restore;
    }
    return error;
}

}

std::string backupLocation(int modId, const std::string& path) {
    return underRoot(cfg::BackupDir, modId, path);
}

std::string stashLocation(int modId, const std::string& path) {
    return underRoot(cfg::StashDir, modId, path);
}

std::string protect(Store& store, int modId, const std::string& dest, std::vector<FileBackup>& created) {
    if (!fileExists(dest))
        return {};
    int origin = store.topOwner(dest);
    if (origin == modId)
        return {};
    std::string error = moveFile(dest, backupLocation(modId, dest));
    if (error.empty())
        created.push_back({dest, origin});
    return error;
}

std::vector<std::string> detach(Store& store, int modId, const std::string& path, const std::string& keepAt) {
    std::vector<std::string> errors;
    InstalledMod* mod = store.editMod(modId);
    if (!mod)
        return errors;
    InstalledMod* above = store.coveringMod(path, modId);
    const FileBackup* own = mod->backupFor(path);

    std::string content;
    if (above)
        content = backupLocation(above->modId, path);
    else if (own || !store.listedByOther(path, modId))
        content = path;

    if (!content.empty()) {
        std::string error = keepAt.empty() ? discard(content) : moveFile(content, keepAt);
        if (!error.empty()) {
            errors.push_back(std::move(error));
            return errors;
        }
    }

    if (above) {
        auto entry = std::find_if(above->backups.begin(), above->backups.end(), [&](const FileBackup& backup) { return backup.origin == modId && samePath(backup.path, path); });
        if (own) {
            addError(errors, moveFile(backupLocation(modId, path), content));
            entry->origin = own->origin;
        } else {
            above->backups.erase(entry);
        }
    } else if (own) {
        addError(errors, moveFile(backupLocation(modId, path), path));
    }

    mod->backups.erase(std::remove_if(mod->backups.begin(), mod->backups.end(), [&](const FileBackup& backup) { return samePath(backup.path, path); }), mod->backups.end());
    return errors;
}

void appendErrors(std::vector<std::string>& into, std::vector<std::string> from) {
    for (std::string& error : from)
        into.push_back(std::move(error));
}

std::vector<std::string> removeMod(Store& store, int modId) {
    std::vector<std::string> errors;
    const InstalledMod* mod = store.findMod(modId);
    if (!mod)
        return errors;
    if (!mod->enabled) {
        errors = discardStash(*mod);
    } else {
        std::vector<std::string> files = mod->files;
        for (const std::string& path : files)
            appendErrors(errors, detach(store, modId, path, ""));
    }
    store.dropMod(modId);
    return errors;
}

std::vector<std::string> disableMod(Store& store, int modId) {
    std::vector<std::string> errors;
    const InstalledMod* mod = store.findMod(modId);
    if (!mod || !mod->enabled)
        return errors;
    std::vector<std::string> files = mod->files;
    std::vector<std::string> parked;
    for (const std::string& path : files) {
        std::vector<std::string> problems = detach(store, modId, path, stashLocation(modId, path));
        if (problems.empty())
            parked.push_back(path);
        appendErrors(errors, std::move(problems));
    }
    if (errors.empty()) {
        store.editMod(modId)->enabled = false;
        return errors;
    }
    std::vector<FileBackup> created;
    for (const std::string& path : parked)
        addError(errors, attach(store, modId, path, created));
    InstalledMod* restored = store.editMod(modId);
    restored->backups.insert(restored->backups.end(), created.begin(), created.end());
    errors.push_back("the mod was left on");
    return errors;
}

std::vector<std::string> enableMod(Store& store, int modId) {
    std::vector<std::string> errors;
    const InstalledMod* mod = store.findMod(modId);
    if (!mod || mod->enabled)
        return errors;
    std::vector<std::string> files = mod->files;
    std::vector<FileBackup> created;
    std::vector<std::string> placed;
    for (const std::string& path : files) {
        std::string error = attach(store, modId, path, created);
        if (error.empty())
            placed.push_back(path);
        addError(errors, std::move(error));
    }
    InstalledMod* enabled = store.editMod(modId);
    enabled->backups = std::move(created);
    enabled->enabled = true;
    if (errors.empty())
        return errors;
    for (const std::string& path : placed)
        appendErrors(errors, detach(store, modId, path, stashLocation(modId, path)));
    store.editMod(modId)->enabled = false;
    errors.push_back("the mod was left off");
    return errors;
}

std::vector<std::string> discardStash(const InstalledMod& mod) {
    std::vector<std::string> errors;
    for (const std::string& path : mod.files)
        addError(errors, discard(stashLocation(mod.modId, path)));
    return errors;
}

}
