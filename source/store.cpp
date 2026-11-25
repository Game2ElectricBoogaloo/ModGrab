#include "store.hpp"

#include "config.hpp"
#include "titles.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <strings.h>

using nlohmann::json;

namespace {

const std::string TempStatePath = std::string(cfg::StatePath) + ".tmp";

std::string setAside(const std::string& reason) {
    std::string backup = std::string(cfg::StatePath) + ".bad";
    std::remove(backup.c_str());
    if (std::rename(cfg::StatePath, backup.c_str()) != 0)
        return reason + " and couldn't move it aside, ModGrab will overwrite it on the next save";
    return reason + ", moved state.json to " + backup + " and started fresh";
}

}

std::string Store::load() {
    if (std::ifstream(TempStatePath) && !std::ifstream(cfg::StatePath))
        std::rename(TempStatePath.c_str(), cfg::StatePath);

    std::ifstream in(cfg::StatePath);
    if (!in)
        return {};
    std::stringstream raw;
    raw << in.rdbuf();
    json state = json::parse(raw.str(), nullptr, false);
    in.close();
    if (state.is_discarded() || !state.is_object())
        return setAside("state.json is corrupt");

    try {
        json games = state.value("games", json::object());
        for (const auto& [titleHex, gameId] : games.items())
            gameLinks[std::stoull(titleHex, nullptr, 16)] = gameId.get<int>();

        json mods = state.value("mods", json::array());
        for (const json& entry : mods) {
            InstalledMod mod;
            mod.titleId = std::stoull(entry.at("title").get<std::string>(), nullptr, 16);
            mod.gameName = entry.value("game", "");
            mod.gameVersion = entry.value("gameVersion", "");
            mod.modId = entry.at("mod").get<int>();
            mod.modName = entry.value("name", "");
            mod.imageUrl = entry.value("image", "");
            mod.version = entry.value("version", "");
            mod.fileId = entry.at("file").get<int>();
            mod.fileName = entry.value("fileName", "");
            mod.fileDate = entry.value("fileDate", int64_t{0});
            mod.installedAt = entry.value("installedAt", int64_t{0});
            mod.enabled = entry.value("enabled", true);
            mod.files = entry.value("files", std::vector<std::string>{});
            for (const json& backup : entry.value("backups", json::array()))
                mod.backups.push_back({backup.at("path").get<std::string>(), backup.value("origin", 0)});
            installed.push_back(std::move(mod));
        }

        for (const auto& [titleHex, list] : state.value("presets", json::object()).items()) {
            std::vector<Preset>& target = presets[std::stoull(titleHex, nullptr, 16)];
            for (const json& preset : list)
                target.push_back({preset.at("name").get<std::string>(), preset.value("mods", std::vector<int>{})});
        }
    } catch (const json::exception& e) {
        gameLinks.clear();
        presets.clear();
        installed.clear();
        return setAside(std::string("bad entry in state.json (") + e.what() + ")");
    } catch (const std::exception&) {
        gameLinks.clear();
        presets.clear();
        installed.clear();
        return setAside("bad title id in state.json");
    }
    return {};
}

std::string Store::save() const {
    json games = json::object();
    for (const auto& [titleId, gameId] : gameLinks)
        games[formatTitleId(titleId)] = gameId;

    json mods = json::array();
    for (const InstalledMod& mod : installed) {
        json backups = json::array();
        for (const FileBackup& backup : mod.backups)
            backups.push_back({{"path", backup.path}, {"origin", backup.origin}});
        mods.push_back({
            {"title", formatTitleId(mod.titleId)},
            {"game", mod.gameName},
            {"gameVersion", mod.gameVersion},
            {"mod", mod.modId},
            {"name", mod.modName},
            {"image", mod.imageUrl},
            {"version", mod.version},
            {"file", mod.fileId},
            {"fileName", mod.fileName},
            {"fileDate", mod.fileDate},
            {"installedAt", mod.installedAt},
            {"enabled", mod.enabled},
            {"files", mod.files},
            {"backups", backups},
        });
    }

    json presetsByGame = json::object();
    for (const auto& [titleId, list] : presets) {
        if (list.empty())
            continue;
        json entries = json::array();
        for (const Preset& preset : list)
            entries.push_back({{"name", preset.name}, {"mods", preset.modIds}});
        presetsByGame[formatTitleId(titleId)] = entries;
    }

    {
        std::ofstream out(TempStatePath, std::ios::trunc);
        if (!out)
            return "can't write " + TempStatePath;
        out << json{{"games", games}, {"mods", mods}, {"presets", presetsByGame}}.dump(2);
        if (!out.flush())
            return "write to " + TempStatePath + " failed";
    }
    std::remove(cfg::StatePath);
    if (std::rename(TempStatePath.c_str(), cfg::StatePath) != 0)
        return std::string("can't replace ") + cfg::StatePath;
    return {};
}

int Store::gameFor(uint64_t titleId) const {
    auto it = gameLinks.find(titleId);
    return it == gameLinks.end() ? 0 : it->second;
}

void Store::linkGame(uint64_t titleId, int gameId) {
    gameLinks[titleId] = gameId;
}

const InstalledMod* Store::findMod(int modId) const {
    for (const InstalledMod& mod : installed)
        if (mod.modId == modId)
            return &mod;
    return nullptr;
}

void Store::putMod(InstalledMod mod) {
    dropMod(mod.modId);
    installed.push_back(std::move(mod));
}

void Store::dropMod(int modId) {
    installed.erase(std::remove_if(installed.begin(), installed.end(), [modId](const InstalledMod& mod) { return mod.modId == modId; }),
                    installed.end());
}

int Store::nextLocalId() const {
    int lowest = 0;
    for (const InstalledMod& mod : installed)
        lowest = std::min(lowest, mod.modId);
    return lowest - 1;
}

InstalledMod* Store::editMod(int modId) {
    for (InstalledMod& mod : installed)
        if (mod.modId == modId)
            return &mod;
    return nullptr;
}

InstalledMod* Store::coveringMod(const std::string& path, int modId) {
    for (InstalledMod& mod : installed)
        for (const FileBackup& backup : mod.backups)
            if (backup.origin == modId && samePath(backup.path, path))
                return &mod;
    return nullptr;
}

int Store::topOwner(const std::string& path) const {
    for (const InstalledMod& mod : installed) {
        if (!mod.enabled || std::none_of(mod.files.begin(), mod.files.end(), [&](const std::string& file) { return samePath(file, path); }))
            continue;
        bool covered = std::any_of(installed.begin(), installed.end(), [&](const InstalledMod& other) {
            return std::any_of(other.backups.begin(), other.backups.end(), [&](const FileBackup& backup) { return backup.origin == mod.modId && samePath(backup.path, path); });
        });
        if (!covered)
            return mod.modId;
    }
    return 0;
}

bool Store::listedByOther(const std::string& path, int modId) const {
    return std::any_of(installed.begin(), installed.end(), [&](const InstalledMod& mod) {
        return mod.modId != modId && mod.enabled && std::any_of(mod.files.begin(), mod.files.end(), [&](const std::string& file) { return samePath(file, path); });
    });
}

bool samePath(const std::string& a, const std::string& b) {
    return strcasecmp(a.c_str(), b.c_str()) == 0;
}

const FileBackup* InstalledMod::backupFor(const std::string& path) const {
    for (const FileBackup& backup : backups)
        if (samePath(backup.path, path))
            return &backup;
    return nullptr;
}
