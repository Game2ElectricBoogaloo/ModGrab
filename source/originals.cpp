#include "originals.hpp"

#include "config.hpp"
#include "installer.hpp"
#include "titles.hpp"

#include <switch.h>

#include <cstdio>
#include <map>
#include <memory>
#include <strings.h>

namespace originals {

namespace {

constexpr const char* MountName = "game";
constexpr const char* DictionaryPack = "Pack/ZsDic.pack.zs";

bool mounted = false;
std::map<uint64_t, std::unique_ptr<zstd::Dictionaries>> loadedDictionaries;
std::map<uint64_t, std::string> versions;

std::string cachedCopy(uint64_t titleId, const std::string& romfsRelative) {
    return std::string(cfg::OriginalsDir) + "/" + formatTitleId(titleId) + "/" + versionOf(titleId) + "/" + romfsRelative;
}

}

std::string romfsPath(uint64_t titleId, const std::string& livePath) {
    std::string prefix = std::string(cfg::AtmosphereDir) + "contents/" + formatTitleId(titleId) + "/romfs/";
    if (livePath.size() <= prefix.size() || strncasecmp(livePath.c_str(), prefix.c_str(), prefix.size()) != 0)
        return {};
    return livePath.substr(prefix.size());
}

void useVersion(uint64_t titleId, const std::string& version) {
    versions[titleId] = version;
}

std::string versionOf(uint64_t titleId) {
    auto known = versions.find(titleId);
    return known == versions.end() || known->second.empty() ? "unknown" : known->second;
}

bool runningAs(uint64_t titleId) {
    u64 programId = 0;
    return appletGetAppletType() == AppletType_Application && R_SUCCEEDED(svcGetInfo(&programId, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0)) && programId == titleId;
}

std::optional<Bytes> read(uint64_t titleId, const std::string& romfsRelative, std::string& problem) {
    std::string cached = cachedCopy(titleId, romfsRelative);
    if (std::optional<Bytes> saved = readFile(cached))
        return saved;
    if (!runningAs(titleId)) {
        problem = "open ModGrab by holding R while starting the game, so it can read the game's original files";
        return std::nullopt;
    }
    if (!mounted) {
        Result rc = romfsMountFromCurrentProcess(MountName);
        if (R_FAILED(rc)) {
            problem = "couldn't open the game's files (error " + std::to_string(rc) + ")";
            return std::nullopt;
        }
        mounted = true;
    }
    std::optional<Bytes> original = readFile(std::string(MountName) + ":/" + romfsRelative);
    if (!original) {
        problem = "the game has no original " + romfsRelative;
        return std::nullopt;
    }
    std::string saveError = writeFile(cached, *original);
    if (!saveError.empty())
        problem = "couldn't keep a copy of the original: " + saveError;
    return original;
}

void forget(uint64_t titleId, const std::string& romfsRelative) {
    std::string cached = cachedCopy(titleId, romfsRelative);
    if (std::remove(cached.c_str()) == 0)
        pruneParents(cached);
}

const zstd::Dictionaries& dictionaries(uint64_t titleId, std::string& problem) {
    std::unique_ptr<zstd::Dictionaries>& slot = loadedDictionaries[titleId];
    if (!slot || !slot->loaded()) {
        slot = std::make_unique<zstd::Dictionaries>();
        if (std::optional<Bytes> pack = read(titleId, DictionaryPack, problem))
            if (std::string error = slot->load(*pack); !error.empty())
                problem = "couldn't load the game's zstd dictionaries: " + error;
    }
    return *slot;
}

void close() {
    loadedDictionaries.clear();
    if (mounted)
        romfsUnmount(MountName);
    mounted = false;
}

}
