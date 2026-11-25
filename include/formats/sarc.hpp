#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using Bytes = std::vector<uint8_t>;

namespace sarc {

struct Entry {
    std::string name;
    Bytes data;
    uint32_t alignment = 4;
};

struct Archive {
    std::vector<Entry> entries;
    uint32_t hashKey = 0x65;

    const Entry* find(const std::string& name) const;
};

bool looksLikeSarc(const Bytes& data);
std::optional<Archive> parse(const Bytes& data);
Bytes build(const Archive& archive);

}
