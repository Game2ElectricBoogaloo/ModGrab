#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct FileBackup {
    std::string path;
    int origin = 0;
};

struct InstalledMod {
    uint64_t titleId = 0;
    std::string gameName;
    std::string gameVersion;
    int modId = 0;
    std::string modName;
    std::string imageUrl;
    std::string version;
    int fileId = 0;
    std::string fileName;
    int64_t fileDate = 0;
    int64_t installedAt = 0;
    bool enabled = true;
    std::vector<std::string> files;
    std::vector<FileBackup> backups;

    bool isLocal() const { return modId < 0; }
    const FileBackup* backupFor(const std::string& path) const;
};

struct Preset {
    std::string name;
    std::vector<int> modIds;
};

bool samePath(const std::string& a, const std::string& b);

class Store {
public:
    std::string load();
    std::string save() const;

    int gameFor(uint64_t titleId) const;
    void linkGame(uint64_t titleId, int gameId);

    const std::vector<InstalledMod>& mods() const { return installed; }
    const InstalledMod* findMod(int modId) const;
    InstalledMod* editMod(int modId);
    void putMod(InstalledMod mod);
    void dropMod(int modId);
    int nextLocalId() const;

    int topOwner(const std::string& path) const;
    bool listedByOther(const std::string& path, int modId) const;
    InstalledMod* coveringMod(const std::string& path, int modId);

    std::vector<Preset>& presetsFor(uint64_t titleId) { return presets[titleId]; }

private:
    std::map<uint64_t, int> gameLinks;
    std::map<uint64_t, std::vector<Preset>> presets;
    std::vector<InstalledMod> installed;
};
