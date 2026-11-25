#pragma once

#include "gamebanana.hpp"

#include <string>
#include <vector>

enum class Fit { Unknown, Match, Mismatch };

struct Compatibility {
    Fit fit = Fit::Unknown;
    std::string detail;
};

std::vector<std::string> findVersions(const std::string& text, bool needsKeyword);
bool sameVersion(const std::string& mentioned, const std::string& installed);
Compatibility checkCompatibility(const std::string& gameVersion, const gb::Mod& mod, const gb::ModFile& file);
