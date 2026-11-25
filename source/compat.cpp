#include "compat.hpp"

#include <algorithm>
#include <cctype>

namespace {

constexpr size_t KeywordReach = 48;
constexpr int MaxShown = 3;

const char* const VersionKeywords[] = {"version", "ver.", "update", "patch", "works on", "compatible", "game v", "for v", "requires"};

std::vector<int> components(const std::string& version) {
    std::vector<int> parts;
    size_t pos = 0;
    while (pos < version.size()) {
        if (!std::isdigit(static_cast<unsigned char>(version[pos]))) {
            pos++;
            continue;
        }
        size_t end = pos;
        while (end < version.size() && std::isdigit(static_cast<unsigned char>(version[end])))
            end++;
        parts.push_back(std::stoi(version.substr(pos, std::min<size_t>(end - pos, 6))));
        pos = end;
    }
    return parts;
}

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

bool nearKeyword(const std::string& lowered, size_t pos) {
    size_t from = pos > KeywordReach ? pos - KeywordReach : 0;
    std::string before = lowered.substr(from, pos - from);
    return std::any_of(std::begin(VersionKeywords), std::end(VersionKeywords), [&](const char* keyword) { return before.find(keyword) != std::string::npos; });
}

bool isModVersion(const std::string& version, const std::string& modVersion) {
    std::vector<int> mentioned = components(version);
    std::vector<int> own = components(modVersion);
    if (own.empty())
        return false;
    own.resize(std::max(own.size(), mentioned.size()), 0);
    mentioned.resize(own.size(), 0);
    return mentioned == own;
}

}

std::vector<std::string> findVersions(const std::string& text, bool needsKeyword) {
    std::vector<std::string> found;
    std::string lowered = lowercase(text);
    size_t pos = 0;
    while (pos < text.size()) {
        bool boundary = pos == 0 || !std::isalnum(static_cast<unsigned char>(text[pos - 1])) || text[pos - 1] == 'v' || text[pos - 1] == 'V';
        if (!std::isdigit(static_cast<unsigned char>(text[pos])) || !boundary) {
            pos++;
            continue;
        }
        size_t end = pos;
        int dots = 0;
        while (end < text.size()) {
            if (std::isdigit(static_cast<unsigned char>(text[end]))) {
                end++;
            } else if (text[end] == '.' && end + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[end + 1])) && dots < 3) {
                dots++;
                end++;
            } else {
                break;
            }
        }
        bool unitFollows = end < text.size() && std::isalpha(static_cast<unsigned char>(text[end]));
        if (dots >= 1 && !unitFollows && (!needsKeyword || nearKeyword(lowered, pos))) {
            std::string version = text.substr(pos, end - pos);
            if (std::find(found.begin(), found.end(), version) == found.end())
                found.push_back(version);
        }
        pos = end;
    }
    return found;
}

bool sameVersion(const std::string& mentioned, const std::string& installed) {
    std::vector<int> wanted = components(mentioned);
    std::vector<int> have = components(installed);
    if (wanted.empty() || have.empty())
        return false;
    if (wanted.size() <= have.size() && std::equal(wanted.begin(), wanted.end(), have.begin()))
        return true;
    have.resize(std::max(wanted.size(), have.size()), 0);
    wanted.resize(have.size(), 0);
    return wanted == have;
}

Compatibility checkCompatibility(const std::string& gameVersion, const gb::Mod& mod, const gb::ModFile& file) {
    if (gameVersion.empty())
        return {Fit::Unknown, "game version unknown"};

    std::vector<std::string> mentions;
    auto collect = [&](const std::vector<std::string>& versions) {
        for (const std::string& version : versions)
            if (std::find(mentions.begin(), mentions.end(), version) == mentions.end() && !isModVersion(version, mod.version))
                mentions.push_back(version);
    };
    collect(findVersions(file.name, false));
    collect(findVersions(file.description, false));
    collect(findVersions(mod.name, false));
    collect(findVersions(mod.description, true));

    int gameMajor = components(gameVersion).front();
    mentions.erase(std::remove_if(mentions.begin(), mentions.end(), [&](const std::string& version) { return components(version).front() > gameMajor + 2; }),
                   mentions.end());

    if (mentions.empty())
        return {Fit::Unknown, "no game version listed, you have " + gameVersion};
    for (const std::string& version : mentions)
        if (sameVersion(version, gameVersion))
            return {Fit::Match, "made for " + version + ", matches your game"};

    std::string listed;
    for (int i = 0; i < static_cast<int>(mentions.size()) && i < MaxShown; i++)
        listed += (i ? ", " : "") + mentions[i];
    return {Fit::Mismatch, "made for " + listed + " but you have " + gameVersion + ", may not work"};
}
