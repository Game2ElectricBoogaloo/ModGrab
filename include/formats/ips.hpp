#pragma once

#include "formats/sarc.hpp"

#include <map>
#include <optional>

namespace ips {

struct Patch {
    bool wide = false;
    std::map<uint32_t, uint8_t> bytes;
};

bool looksLikeIps(const Bytes& data);
std::optional<Patch> parse(const Bytes& data);
Patch combine(const Patch& lower, const Patch& upper, int& clashes);
Bytes write(const Patch& patch);

}
