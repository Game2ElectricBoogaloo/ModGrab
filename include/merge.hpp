#pragma once

#include "formats/sarc.hpp"
#include "formats/zstd.hpp"

#include <optional>
#include <string>
#include <vector>

namespace merge {

enum class Kind { None, Patch, Archive };

struct Outcome {
    std::optional<Bytes> data;
    std::vector<std::string> clashes;
    std::string error;
};

Kind kindOf(const std::string& path);
Outcome patches(const std::vector<Bytes>& layers);
Outcome archives(const std::string& path, const Bytes& original, const std::vector<Bytes>& layers, const zstd::Dictionaries& dictionaries);

}
