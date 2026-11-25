#include "formats/yaz0.hpp"

#include <cstring>

namespace yaz0 {

namespace {

constexpr size_t HeaderSize = 16;
constexpr size_t Window = 0x1000;
constexpr size_t MinMatch = 3;
constexpr size_t MaxMatch = 0x111;
constexpr int ChainDepth = 32;
constexpr size_t HashSize = 1 << 16;

uint32_t readBig32(const Bytes& data, size_t at) {
    return static_cast<uint32_t>(data[at]) << 24 | data[at + 1] << 16 | data[at + 2] << 8 | data[at + 3];
}

uint32_t hashAt(const Bytes& data, size_t at) {
    uint32_t key = static_cast<uint32_t>(data[at]) << 16 | data[at + 1] << 8 | data[at + 2];
    return (key * 2654435761u) >> 16;
}

}

bool looksLikeYaz0(const Bytes& data) {
    return data.size() >= HeaderSize && std::memcmp(data.data(), "Yaz0", 4) == 0;
}

std::optional<Bytes> decompress(const Bytes& data) {
    if (!looksLikeYaz0(data))
        return std::nullopt;
    size_t size = readBig32(data, 4);
    Bytes out;
    out.reserve(size);
    size_t in = HeaderSize;
    while (out.size() < size) {
        if (in >= data.size())
            return std::nullopt;
        uint8_t code = data[in++];
        for (int bit = 7; bit >= 0 && out.size() < size; bit--) {
            if (code & (1 << bit)) {
                if (in >= data.size())
                    return std::nullopt;
                out.push_back(data[in++]);
                continue;
            }
            if (in + 1 >= data.size())
                return std::nullopt;
            uint8_t first = data[in++];
            uint8_t second = data[in++];
            size_t distance = ((first & 0xF) << 8 | second) + 1;
            size_t length = first >> 4;
            if (length == 0) {
                if (in >= data.size())
                    return std::nullopt;
                length = data[in++] + 0x12;
            } else {
                length += 2;
            }
            if (distance > out.size())
                return std::nullopt;
            size_t from = out.size() - distance;
            for (size_t i = 0; i < length && out.size() < size; i++)
                out.push_back(out[from + i]);
        }
    }
    return out;
}

Bytes compress(const Bytes& data) {
    Bytes out(HeaderSize, 0);
    std::memcpy(out.data(), "Yaz0", 4);
    for (int i = 0; i < 4; i++)
        out[4 + i] = (data.size() >> (24 - 8 * i)) & 0xFF;

    std::vector<int64_t> head(HashSize, -1);
    std::vector<int64_t> previous(Window, -1);
    auto remember = [&](size_t at) {
        if (at + MinMatch > data.size())
            return;
        uint32_t slot = hashAt(data, at);
        previous[at % Window] = head[slot];
        head[slot] = static_cast<int64_t>(at);
    };

    size_t pos = 0;
    while (pos < data.size()) {
        size_t codeAt = out.size();
        out.push_back(0);
        for (int bit = 7; bit >= 0 && pos < data.size(); bit--) {
            size_t bestLength = 0;
            size_t bestDistance = 0;
            if (pos + MinMatch <= data.size()) {
                int64_t candidate = head[hashAt(data, pos)];
                for (int checked = 0; candidate >= 0 && checked < ChainDepth && pos - static_cast<size_t>(candidate) <= Window; checked++) {
                    size_t from = static_cast<size_t>(candidate);
                    size_t length = 0;
                    while (length < MaxMatch && pos + length < data.size() && data[from + length] == data[pos + length])
                        length++;
                    if (length > bestLength) {
                        bestLength = length;
                        bestDistance = pos - from;
                    }
                    int64_t next = previous[from % Window];
                    if (next >= candidate)
                        break;
                    candidate = next;
                }
            }

            size_t advance = 1;
            if (bestLength >= MinMatch) {
                size_t distance = bestDistance - 1;
                if (bestLength >= 0x12) {
                    out.push_back(static_cast<uint8_t>(distance >> 8));
                    out.push_back(static_cast<uint8_t>(distance & 0xFF));
                    out.push_back(static_cast<uint8_t>(bestLength - 0x12));
                } else {
                    out.push_back(static_cast<uint8_t>((bestLength - 2) << 4 | distance >> 8));
                    out.push_back(static_cast<uint8_t>(distance & 0xFF));
                }
                advance = bestLength;
            } else {
                out[codeAt] |= 1 << bit;
                out.push_back(data[pos]);
            }
            for (size_t i = 0; i < advance; i++, pos++)
                remember(pos);
        }
    }
    return out;
}

}
