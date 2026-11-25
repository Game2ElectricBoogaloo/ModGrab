#pragma once

#include "formats/sarc.hpp"

#include <optional>
#include <string>
#include <vector>

struct ZSTD_CDict_s;
struct ZSTD_DDict_s;

namespace zstd {

bool looksLikeZstd(const Bytes& data);

class Dictionaries {
public:
    Dictionaries() = default;
    Dictionaries(const Dictionaries&) = delete;
    Dictionaries& operator=(const Dictionaries&) = delete;
    ~Dictionaries();

    std::string load(const Bytes& zsDicPack);
    bool loaded() const { return !entries.empty(); }
    std::optional<Bytes> decompress(const Bytes& frame) const;
    std::optional<Bytes> compress(const Bytes& data, const std::string& fileName) const;

private:
    struct Dictionary {
        std::string name;
        unsigned id = 0;
        ZSTD_DDict_s* decoder = nullptr;
        ZSTD_CDict_s* encoder = nullptr;
    };

    std::vector<Dictionary> entries;
};

}
