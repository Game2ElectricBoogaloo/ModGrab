#pragma once

#include "formats/zstd.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace originals {

std::string romfsPath(uint64_t titleId, const std::string& livePath);
void useVersion(uint64_t titleId, const std::string& version);
std::string versionOf(uint64_t titleId);
bool runningAs(uint64_t titleId);
std::optional<Bytes> read(uint64_t titleId, const std::string& romfsRelative, std::string& problem);
void forget(uint64_t titleId, const std::string& romfsRelative);
const zstd::Dictionaries& dictionaries(uint64_t titleId, std::string& problem);
void close();

}
