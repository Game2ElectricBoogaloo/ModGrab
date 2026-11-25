#include "formats/sarc.hpp"

#include <algorithm>
#include <cstring>

namespace sarc {

namespace {

constexpr size_t HeaderSize = 0x14;
constexpr size_t SfatHeaderSize = 0xC;
constexpr size_t NodeSize = 0x10;
constexpr size_t SfntHeaderSize = 0x8;
constexpr uint32_t MaxAlignment = 0x2000;
constexpr uint32_t HasName = 1u << 24;

uint16_t read16(const Bytes& data, size_t at) {
    return static_cast<uint16_t>(data[at] | data[at + 1] << 8);
}

uint32_t read32(const Bytes& data, size_t at) {
    return static_cast<uint32_t>(data[at] | data[at + 1] << 8 | data[at + 2] << 16 | static_cast<uint32_t>(data[at + 3]) << 24);
}

void put16(Bytes& data, size_t at, uint16_t value) {
    data[at] = value & 0xFF;
    data[at + 1] = value >> 8;
}

void put32(Bytes& data, size_t at, uint32_t value) {
    for (int i = 0; i < 4; i++)
        data[at + i] = (value >> (8 * i)) & 0xFF;
}

uint32_t nameHash(const std::string& name, uint32_t key) {
    uint32_t hash = 0;
    for (char c : name)
        hash = hash * key + static_cast<uint32_t>(static_cast<int32_t>(static_cast<signed char>(c)));
    return hash;
}

uint32_t alignmentAt(size_t offset) {
    if (offset == 0)
        return MaxAlignment;
    uint32_t alignment = 4;
    while (alignment < MaxAlignment && offset % (alignment * 2) == 0)
        alignment *= 2;
    return offset % alignment == 0 ? alignment : 4;
}

size_t alignUp(size_t value, size_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

}

const Entry* Archive::find(const std::string& name) const {
    for (const Entry& entry : entries)
        if (entry.name == name)
            return &entry;
    return nullptr;
}

bool looksLikeSarc(const Bytes& data) {
    return data.size() >= HeaderSize && std::memcmp(data.data(), "SARC", 4) == 0 && data[6] == 0xFF && data[7] == 0xFE;
}

std::optional<Archive> parse(const Bytes& data) {
    if (!looksLikeSarc(data))
        return std::nullopt;
    uint32_t dataOffset = read32(data, 0xC);
    size_t sfat = read16(data, 4);
    if (sfat + SfatHeaderSize > data.size() || std::memcmp(data.data() + sfat, "SFAT", 4) != 0)
        return std::nullopt;
    uint16_t count = read16(data, sfat + 6);
    Archive archive;
    archive.hashKey = read32(data, sfat + 8);
    size_t nodes = sfat + SfatHeaderSize;
    size_t sfnt = nodes + count * NodeSize;
    if (sfnt + SfntHeaderSize > data.size() || std::memcmp(data.data() + sfnt, "SFNT", 4) != 0)
        return std::nullopt;
    size_t names = sfnt + SfntHeaderSize;

    for (uint16_t i = 0; i < count; i++) {
        size_t node = nodes + i * NodeSize;
        uint32_t attributes = read32(data, node + 4);
        uint32_t start = read32(data, node + 8);
        uint32_t end = read32(data, node + 12);
        if (!(attributes & HasName))
            return std::nullopt;
        size_t nameAt = names + (attributes & 0xFFFFFF) * 4;
        size_t absoluteStart = static_cast<size_t>(dataOffset) + start;
        size_t absoluteEnd = static_cast<size_t>(dataOffset) + end;
        if (nameAt >= data.size() || end < start || absoluteEnd > data.size())
            return std::nullopt;
        const char* name = reinterpret_cast<const char*>(data.data() + nameAt);
        size_t nameLength = strnlen(name, data.size() - nameAt);
        Entry entry;
        entry.name.assign(name, nameLength);
        entry.data.assign(data.begin() + static_cast<long>(absoluteStart), data.begin() + static_cast<long>(absoluteEnd));
        entry.alignment = alignmentAt(absoluteStart);
        archive.entries.push_back(std::move(entry));
    }
    return archive;
}

Bytes build(const Archive& archive) {
    std::vector<const Entry*> sorted;
    for (const Entry& entry : archive.entries)
        sorted.push_back(&entry);
    std::stable_sort(sorted.begin(), sorted.end(), [&](const Entry* a, const Entry* b) { return nameHash(a->name, archive.hashKey) < nameHash(b->name, archive.hashKey); });

    size_t sfat = HeaderSize;
    size_t nodes = sfat + SfatHeaderSize;
    size_t sfnt = nodes + sorted.size() * NodeSize;
    size_t names = sfnt + SfntHeaderSize;
    std::vector<size_t> nameOffsets;
    size_t namesEnd = names;
    for (const Entry* entry : sorted) {
        nameOffsets.push_back(namesEnd - names);
        namesEnd += alignUp(entry->name.size() + 1, 4);
    }

    uint32_t largest = 4;
    for (const Entry* entry : sorted)
        largest = std::max(largest, entry->alignment);
    size_t dataOffset = alignUp(namesEnd, largest);

    std::vector<size_t> starts;
    size_t cursor = dataOffset;
    for (const Entry* entry : sorted) {
        cursor = alignUp(cursor, entry->alignment);
        starts.push_back(cursor);
        cursor += entry->data.size();
    }

    Bytes out(cursor, 0);
    std::memcpy(out.data(), "SARC", 4);
    put16(out, 4, HeaderSize);
    out[6] = 0xFF;
    out[7] = 0xFE;
    put32(out, 8, static_cast<uint32_t>(out.size()));
    put32(out, 0xC, static_cast<uint32_t>(dataOffset));
    put16(out, 0x10, 0x0100);

    std::memcpy(out.data() + sfat, "SFAT", 4);
    put16(out, sfat + 4, SfatHeaderSize);
    put16(out, sfat + 6, static_cast<uint16_t>(sorted.size()));
    put32(out, sfat + 8, archive.hashKey);

    for (size_t i = 0; i < sorted.size(); i++) {
        size_t node = nodes + i * NodeSize;
        put32(out, node, nameHash(sorted[i]->name, archive.hashKey));
        put32(out, node + 4, HasName | static_cast<uint32_t>(nameOffsets[i] / 4));
        put32(out, node + 8, static_cast<uint32_t>(starts[i] - dataOffset));
        put32(out, node + 12, static_cast<uint32_t>(starts[i] - dataOffset + sorted[i]->data.size()));
        std::memcpy(out.data() + names + nameOffsets[i], sorted[i]->name.c_str(), sorted[i]->name.size());
        std::memcpy(out.data() + starts[i], sorted[i]->data.data(), sorted[i]->data.size());
    }

    std::memcpy(out.data() + sfnt, "SFNT", 4);
    put16(out, sfnt + 4, SfntHeaderSize);
    return out;
}

}
