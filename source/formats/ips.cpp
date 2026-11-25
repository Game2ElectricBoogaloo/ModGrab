#include "formats/ips.hpp"

#include <cstring>

namespace ips {

namespace {

constexpr uint32_t NarrowLimit = 0xFFFFFF;
constexpr uint32_t NarrowEof = 0x454F46;
constexpr size_t MaxRun = 0xFFFF;

uint32_t readBig(const Bytes& data, size_t at, int width) {
    uint32_t value = 0;
    for (int i = 0; i < width; i++)
        value = value << 8 | data[at + i];
    return value;
}

void putBig(Bytes& out, uint32_t value, int width) {
    for (int i = width - 1; i >= 0; i--)
        out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

}

bool looksLikeIps(const Bytes& data) {
    return (data.size() >= 5 && std::memcmp(data.data(), "PATCH", 5) == 0) || (data.size() >= 5 && std::memcmp(data.data(), "IPS32", 5) == 0);
}

std::optional<Patch> parse(const Bytes& data) {
    if (!looksLikeIps(data))
        return std::nullopt;
    Patch patch;
    patch.wide = std::memcmp(data.data(), "IPS32", 5) == 0;
    int width = patch.wide ? 4 : 3;
    const char* end = patch.wide ? "EEOF" : "EOF";
    size_t at = 5;
    while (true) {
        if (at + width > data.size())
            return std::nullopt;
        if (std::memcmp(data.data() + at, end, width) == 0)
            return patch;
        uint32_t offset = readBig(data, at, width);
        at += width;
        if (at + 2 > data.size())
            return std::nullopt;
        uint32_t size = readBig(data, at, 2);
        at += 2;
        if (size == 0) {
            if (at + 3 > data.size())
                return std::nullopt;
            uint32_t repeat = readBig(data, at, 2);
            uint8_t value = data[at + 2];
            at += 3;
            for (uint32_t i = 0; i < repeat; i++)
                patch.bytes[offset + i] = value;
            continue;
        }
        if (at + size > data.size())
            return std::nullopt;
        for (uint32_t i = 0; i < size; i++)
            patch.bytes[offset + i] = data[at + i];
        at += size;
    }
}

Patch combine(const Patch& lower, const Patch& upper, int& clashes) {
    Patch merged = lower;
    merged.wide = lower.wide || upper.wide;
    for (const auto& [offset, value] : upper.bytes) {
        auto existing = merged.bytes.find(offset);
        if (existing != merged.bytes.end() && existing->second != value)
            clashes++;
        merged.bytes[offset] = value;
    }
    return merged;
}

Bytes write(const Patch& patch) {
    bool wide = patch.wide;
    for (const auto& [offset, value] : patch.bytes)
        if (offset > NarrowLimit || offset == NarrowEof)
            wide = true;
    int width = wide ? 4 : 3;

    Bytes out;
    const char* header = wide ? "IPS32" : "PATCH";
    out.insert(out.end(), header, header + 5);
    auto it = patch.bytes.begin();
    while (it != patch.bytes.end()) {
        uint32_t start = it->first;
        Bytes run;
        while (it != patch.bytes.end() && it->first == start + run.size() && run.size() < MaxRun) {
            run.push_back(it->second);
            ++it;
        }
        putBig(out, start, width);
        putBig(out, static_cast<uint32_t>(run.size()), 2);
        out.insert(out.end(), run.begin(), run.end());
    }
    const char* footer = wide ? "EEOF" : "EOF";
    out.insert(out.end(), footer, footer + width);
    return out;
}

}
