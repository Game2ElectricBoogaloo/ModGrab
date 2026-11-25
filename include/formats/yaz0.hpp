#pragma once

#include "formats/sarc.hpp"

#include <optional>

namespace yaz0 {

bool looksLikeYaz0(const Bytes& data);
std::optional<Bytes> decompress(const Bytes& data);
Bytes compress(const Bytes& data);

}
