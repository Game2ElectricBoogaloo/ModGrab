#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gb {

struct Game {
    int id = 0;
    std::string name;
};

struct ModSummary {
    int id = 0;
    std::string name;
    std::string submitter;
    std::string thumbnailUrl;
    int64_t dateUpdated = 0;
};

struct ModFile {
    int id = 0;
    std::string name;
    std::string description;
    std::string downloadUrl;
    int64_t size = 0;
    int64_t dateAdded = 0;
};

struct Mod {
    int id = 0;
    std::string name;
    std::string version;
    std::string submitter;
    std::string description;
    std::string previewUrl;
    std::vector<std::string> gallery;
    int64_t dateUpdated = 0;
    std::vector<ModFile> files;
};

struct ModPage {
    std::vector<ModSummary> mods;
    int total = 0;
    bool complete = true;
};

std::vector<Game> searchGames(const std::string& title, std::string& error);
std::optional<ModPage> listMods(int gameId, int page, std::string& error);
std::optional<ModPage> searchMods(int gameId, const std::string& query, int page, std::string& error);
std::optional<Mod> fetchMod(int modId, std::string& error);

}
