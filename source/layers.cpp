#include "layers.hpp"

#include "config.hpp"
#include "installer.hpp"
#include "merge.hpp"
#include "originals.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <optional>

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

std::vector<std::string> pendingNotes;

std::string fileName(const std::string& path) {
    return path.substr(path.rfind('/') + 1);
}

void addError(std::vector<std::string>& errors, std::string error) {
    if (!error.empty())
        errors.push_back(std::move(error));
}

std::string attach(Store& store, int modId, const std::string& path, std::vector<FileBackup>& created) {
    std::string stashed = stashLocation(modId, path);
    if (!fileExists(stashed))
        return "missing from the disabled folder: " + path + ", reinstall the mod to fix it";
    if (canMerge(store, modId, path)) {
        std::string layer = layerLocation(modId, path);
        std::string error = moveFile(stashed, layer);
        if (error.empty())
            error = joinOrReplace(store, modId, store.findMod(modId)->titleId, path, created);
        if (!error.empty() && fileExists(layer))
            addError(pendingNotes, moveFile(layer, stashed));
        return error;
    }
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

std::string layerLocation(int modId, const std::string& path) {
    return underRoot(cfg::LayerDir, modId, path);
}

bool canMerge(Store& store, int modId, const std::string& path) {
    merge::Kind kind = merge::kindOf(path);
    if (kind == merge::Kind::None)
        return false;
    if (kind == merge::Kind::Archive && path.find("/romfs/") == std::string::npos)
        return false;
    if (store.mergedAt(path))
        return true;
    int owner = store.topOwner(path);
    const InstalledMod* below = owner && owner != modId ? store.findMod(owner) : nullptr;
    if (!below || !fileExists(path))
        return false;
    const FileBackup* backup = below->backupFor(path);
    return !backup || backup->origin == 0;
}

std::string join(Store& store, int modId, uint64_t titleId, const std::string& path) {
    if (!store.mergedAt(path)) {
        int owner = store.topOwner(path);
        std::string error = moveFile(path, layerLocation(owner, path));
        if (!error.empty())
            return error;
        store.mergedFiles().push_back({path, titleId, {owner}, false, originals::versionOf(titleId)});
    }
    store.mergedAt(path)->layers.push_back(modId);
    rebuild(store, path);
    return {};
}

std::string joinOrReplace(Store& store, int modId, uint64_t titleId, const std::string& path, std::vector<FileBackup>& created) {
    std::string error = join(store, modId, titleId, path);
    if (error.empty())
        return {};
    pendingNotes.push_back("Couldn't merge " + fileName(path) + " (" + error + "), this mod's copy replaces the other one instead.");
    error = protect(store, modId, path, created);
    if (!error.empty())
        return error;
    return moveFile(layerLocation(modId, path), path);
}

void rebuild(Store& store, const std::string& path) {
    MergedFile* file = store.mergedAt(path);
    std::vector<Bytes> raws;
    for (int layer : file->layers) {
        if (std::optional<Bytes> raw = readFile(layerLocation(layer, path)))
            raws.push_back(std::move(*raw));
        else
            pendingNotes.push_back("missing a mod's copy of " + fileName(path));
    }
    if (raws.empty())
        return;

    merge::Outcome outcome;
    if (merge::kindOf(path) == merge::Kind::Patch) {
        outcome = merge::patches(raws);
    } else {
        std::string relative = originals::romfsPath(file->titleId, path);
        std::string problem;
        std::optional<Bytes> original = relative.empty() ? std::nullopt : originals::read(file->titleId, relative, problem);
        if (relative.empty()) {
            outcome.error = "it isn't inside a game's romfs folder";
        } else if (!original) {
            outcome.error = problem;
        } else if (original == readFile(path)) {
            originals::forget(file->titleId, relative);
            outcome.error = "the game handed back the modded copy instead of its original";
        } else {
            if (!problem.empty())
                pendingNotes.push_back(problem);
            std::string dictionaryProblem;
            const zstd::Dictionaries& dictionaries = originals::dictionaries(file->titleId, dictionaryProblem);
            outcome = merge::archives(path, *original, raws, dictionaries);
            if (!outcome.data && !dictionaryProblem.empty())
                outcome.error += " (" + dictionaryProblem + ")";
        }
    }

    std::string staged = path + ".merging";
    std::string error = writeFile(staged, outcome.data ? *outcome.data : raws.back());
    if (error.empty())
        error = moveFile(staged, path);
    store.mergedAt(path)->combined = error.empty() && outcome.data;
    if (!error.empty()) {
        pendingNotes.push_back("Couldn't write the merged " + fileName(path) + ": " + error);
        return;
    }
    if (!outcome.data)
        pendingNotes.push_back("Couldn't merge " + fileName(path) + ": " + outcome.error + ". The newest mod's copy is used until it can be merged.");
    for (const std::string& clash : outcome.clashes)
        pendingNotes.push_back("Both mods change " + clash + ", the newer one's version is used.");
}

bool retriable(const MergedFile& file) {
    bool outdated = !file.combined || file.gameVersion != originals::versionOf(file.titleId);
    return outdated && (merge::kindOf(file.path) == merge::Kind::Patch || originals::runningAs(file.titleId));
}

size_t remergePending(Store& store) {
    std::vector<std::string> waiting;
    for (MergedFile& file : store.mergedFiles()) {
        if (!retriable(file))
            continue;
        file.gameVersion = originals::versionOf(file.titleId);
        waiting.push_back(file.path);
    }
    size_t merged = 0;
    for (const std::string& path : waiting) {
        rebuild(store, path);
        merged += store.mergedAt(path)->combined;
    }
    return merged;
}

std::vector<std::string> takeNotes() {
    std::vector<std::string> notes;
    notes.swap(pendingNotes);
    return notes;
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

std::vector<std::string> detachLayer(Store& store, int modId, const std::string& path, const std::string& keepAt) {
    std::vector<std::string> errors;
    std::vector<int> remaining = store.mergedAt(path)->layers;
    remaining.erase(std::remove(remaining.begin(), remaining.end(), modId), remaining.end());

    InstalledMod* mod = store.editMod(modId);
    const FileBackup* own = mod->backupFor(path);
    std::optional<FileBackup> handed = own && !remaining.empty() ? std::optional<FileBackup>(*own) : std::nullopt;
    if (handed) {
        std::string error = moveFile(backupLocation(modId, path), backupLocation(remaining.front(), path));
        if (!error.empty()) {
            errors.push_back(std::move(error));
            return errors;
        }
    }

    std::string raw = layerLocation(modId, path);
    std::string error = keepAt.empty() ? discard(raw) : moveFile(raw, keepAt);
    if (!error.empty()) {
        if (handed)
            addError(errors, moveFile(backupLocation(remaining.front(), path), backupLocation(modId, path)));
        errors.push_back(std::move(error));
        return errors;
    }

    store.mergedAt(path)->layers = remaining;
    if (handed) {
        mod->dropBackup(path);
        store.editMod(remaining.front())->backups.push_back(*handed);
    } else if (own) {
        mod->dropBackup(path);
        addError(errors, moveFile(backupLocation(modId, path), path));
    }

    if (remaining.size() > 1) {
        rebuild(store, path);
        return errors;
    }
    store.dropMerged(path);
    if (remaining.size() == 1)
        addError(errors, moveFile(layerLocation(remaining.front(), path), path));
    return errors;
}

std::vector<std::string> detach(Store& store, int modId, const std::string& path, const std::string& keepAt) {
    std::vector<std::string> errors;
    if (const MergedFile* file = store.mergedAt(path); file && std::find(file->layers.begin(), file->layers.end(), modId) != file->layers.end())
        return detachLayer(store, modId, path, keepAt);
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

    mod->dropBackup(path);
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
