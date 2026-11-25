#include "merge.hpp"

#include "formats/ips.hpp"
#include "formats/yaz0.hpp"

#include <algorithm>
#include <cstring>
#include <strings.h>

namespace merge {

namespace {

enum class Wrapper { Plain, Yaz0, Zstd };

struct Unwrapped {
    Wrapper wrapper = Wrapper::Plain;
    Bytes inner;
};

const char* const ArchiveExtensions[] = {".sarc", ".pack", ".szs", ".ssarc", ".sblarc", ".sbactorpack", ".sbeventpack", ".pack.zs", ".sarc.zs"};

bool endsWithNoCase(const std::string& text, const char* suffix) {
    size_t length = std::strlen(suffix);
    return text.size() >= length && strcasecmp(text.c_str() + text.size() - length, suffix) == 0;
}

std::string baseName(const std::string& path) {
    size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::optional<Unwrapped> unwrap(const Bytes& data, const zstd::Dictionaries& dictionaries) {
    if (yaz0::looksLikeYaz0(data)) {
        std::optional<Bytes> inner = yaz0::decompress(data);
        return inner ? std::optional<Unwrapped>(Unwrapped{Wrapper::Yaz0, std::move(*inner)}) : std::nullopt;
    }
    if (zstd::looksLikeZstd(data)) {
        std::optional<Bytes> inner = dictionaries.decompress(data);
        return inner ? std::optional<Unwrapped>(Unwrapped{Wrapper::Zstd, std::move(*inner)}) : std::nullopt;
    }
    return Unwrapped{Wrapper::Plain, data};
}

std::optional<Bytes> rewrap(const Bytes& inner, Wrapper wrapper, const std::string& name, const zstd::Dictionaries& dictionaries) {
    switch (wrapper) {
    case Wrapper::Yaz0: return yaz0::compress(inner);
    case Wrapper::Zstd: return dictionaries.compress(inner, name);
    case Wrapper::Plain: break;
    }
    return inner;
}

std::optional<Bytes> mergeContainer(const std::string& label, const Bytes& original, const std::vector<Bytes>& versions, const zstd::Dictionaries& dictionaries,
                                    std::vector<std::string>& clashes, std::string& error);

sarc::Archive mergeArchive(const std::string& label, const sarc::Archive& original, const std::vector<sarc::Archive>& layers, const zstd::Dictionaries& dictionaries,
                           std::vector<std::string>& clashes) {
    sarc::Archive merged = original;
    std::vector<std::pair<std::string, std::vector<const sarc::Entry*>>> changes;

    auto changesFor = [&](const std::string& name) -> std::vector<const sarc::Entry*>& {
        for (auto& [entryName, versions] : changes)
            if (entryName == name)
                return versions;
        changes.push_back({name, {}});
        return changes.back().second;
    };

    for (const sarc::Archive& layer : layers) {
        for (const sarc::Entry& entry : layer.entries) {
            const sarc::Entry* untouched = original.find(entry.name);
            if (untouched && untouched->data == entry.data)
                continue;
            changesFor(entry.name).push_back(&entry);
        }
    }

    for (auto& [name, versions] : changes) {
        const sarc::Entry* untouched = original.find(name);
        const sarc::Entry* newest = versions.back();
        bool allSame = std::all_of(versions.begin(), versions.end(), [&](const sarc::Entry* version) { return version->data == newest->data; });
        Bytes result = newest->data;
        if (versions.size() > 1 && !allSame) {
            std::vector<Bytes> contents;
            for (const sarc::Entry* version : versions)
                contents.push_back(version->data);
            std::optional<Bytes> nested;
            if (untouched) {
                std::string nestedError;
                nested = mergeContainer(label + "/" + name, untouched->data, contents, dictionaries, clashes, nestedError);
            }
            if (nested)
                result = std::move(*nested);
            else
                clashes.push_back(label + "/" + name);
        }
        auto target = std::find_if(merged.entries.begin(), merged.entries.end(), [&](const sarc::Entry& entry) { return entry.name == name; });
        if (target != merged.entries.end()) {
            target->data = std::move(result);
        } else {
            sarc::Entry fresh = *newest;
            fresh.data = std::move(result);
            merged.entries.push_back(std::move(fresh));
        }
    }
    return merged;
}

std::optional<Bytes> mergeContainer(const std::string& label, const Bytes& original, const std::vector<Bytes>& versions, const zstd::Dictionaries& dictionaries,
                                    std::vector<std::string>& clashes, std::string& error) {
    std::optional<Unwrapped> base = unwrap(original, dictionaries);
    if (!base || !sarc::looksLikeSarc(base->inner)) {
        error = "the original " + label + " isn't an archive ModGrab can read";
        return std::nullopt;
    }
    std::optional<sarc::Archive> baseArchive = sarc::parse(base->inner);
    if (!baseArchive) {
        error = "the original " + label + " is damaged";
        return std::nullopt;
    }
    std::vector<sarc::Archive> layers;
    for (const Bytes& version : versions) {
        std::optional<Unwrapped> opened = unwrap(version, dictionaries);
        std::optional<sarc::Archive> archive = opened ? sarc::parse(opened->inner) : std::nullopt;
        if (!archive) {
            error = "a mod's copy of " + label + " isn't an archive ModGrab can read";
            return std::nullopt;
        }
        layers.push_back(std::move(*archive));
    }
    sarc::Archive merged = mergeArchive(label, *baseArchive, layers, dictionaries, clashes);
    std::optional<Bytes> packed = rewrap(sarc::build(merged), base->wrapper, baseName(label), dictionaries);
    if (!packed)
        error = "couldn't recompress " + label;
    return packed;
}

}

Kind kindOf(const std::string& path) {
    if (endsWithNoCase(path, ".ips"))
        return Kind::Patch;
    for (const char* extension : ArchiveExtensions)
        if (endsWithNoCase(path, extension))
            return Kind::Archive;
    return Kind::None;
}

Outcome patches(const std::vector<Bytes>& layers) {
    Outcome outcome;
    std::optional<ips::Patch> combined;
    int clashes = 0;
    for (const Bytes& layer : layers) {
        std::optional<ips::Patch> patch = ips::parse(layer);
        if (!patch) {
            outcome.error = "one of the patches isn't a valid IPS file";
            return outcome;
        }
        combined = combined ? ips::combine(*combined, *patch, clashes) : *patch;
    }
    if (clashes)
        outcome.clashes.push_back(std::to_string(clashes) + " patched bytes");
    outcome.data = ips::write(*combined);
    return outcome;
}

Outcome archives(const std::string& path, const Bytes& original, const std::vector<Bytes>& layers, const zstd::Dictionaries& dictionaries) {
    Outcome outcome;
    outcome.data = mergeContainer(baseName(path), original, layers, dictionaries, outcome.clashes, outcome.error);
    return outcome;
}

}
