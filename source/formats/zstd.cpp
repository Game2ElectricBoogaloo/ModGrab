#include "formats/zstd.hpp"

#include <zstd.h>

#include <cstring>

namespace zstd {

namespace {

constexpr int Level = 3;
constexpr unsigned char Magic[] = {0x28, 0xB5, 0x2F, 0xFD};

bool endsWith(const std::string& text, const char* suffix) {
    size_t length = std::strlen(suffix);
    return text.size() >= length && text.compare(text.size() - length, length, suffix) == 0;
}

std::optional<Bytes> decodeFrame(const Bytes& frame, const ZSTD_DDict* dictionary) {
    unsigned long long size = ZSTD_getFrameContentSize(frame.data(), frame.size());
    if (size == ZSTD_CONTENTSIZE_ERROR || size == ZSTD_CONTENTSIZE_UNKNOWN)
        return std::nullopt;
    Bytes out(static_cast<size_t>(size));
    ZSTD_DCtx* context = ZSTD_createDCtx();
    size_t written = dictionary ? ZSTD_decompress_usingDDict(context, out.data(), out.size(), frame.data(), frame.size(), dictionary)
                                : ZSTD_decompressDCtx(context, out.data(), out.size(), frame.data(), frame.size());
    ZSTD_freeDCtx(context);
    if (ZSTD_isError(written) || written != out.size())
        return std::nullopt;
    return out;
}

}

bool looksLikeZstd(const Bytes& data) {
    return data.size() >= 4 && std::memcmp(data.data(), Magic, 4) == 0;
}

Dictionaries::~Dictionaries() {
    for (Dictionary& dictionary : entries) {
        ZSTD_freeDDict(dictionary.decoder);
        ZSTD_freeCDict(dictionary.encoder);
    }
}

std::string Dictionaries::load(const Bytes& zsDicPack) {
    std::optional<Bytes> unpacked = decodeFrame(zsDicPack, nullptr);
    if (!unpacked)
        return "ZsDic.pack.zs isn't a readable zstd file";
    std::optional<sarc::Archive> archive = sarc::parse(*unpacked);
    if (!archive)
        return "ZsDic.pack.zs doesn't contain a SARC archive";
    for (const sarc::Entry& entry : archive->entries) {
        if (!endsWith(entry.name, ".zsdic"))
            continue;
        Dictionary dictionary;
        dictionary.name = entry.name;
        dictionary.decoder = ZSTD_createDDict(entry.data.data(), entry.data.size());
        dictionary.encoder = ZSTD_createCDict(entry.data.data(), entry.data.size(), Level);
        dictionary.id = ZSTD_getDictID_fromDDict(dictionary.decoder);
        entries.push_back(dictionary);
    }
    return entries.empty() ? "ZsDic.pack.zs has no dictionaries in it" : std::string();
}

std::optional<Bytes> Dictionaries::decompress(const Bytes& frame) const {
    unsigned id = ZSTD_getDictID_fromFrame(frame.data(), frame.size());
    if (id == 0)
        return decodeFrame(frame, nullptr);
    for (const Dictionary& dictionary : entries)
        if (dictionary.id == id)
            return decodeFrame(frame, dictionary.decoder);
    return std::nullopt;
}

std::optional<Bytes> Dictionaries::compress(const Bytes& data, const std::string& fileName) const {
    const char* wanted = endsWith(fileName, ".pack.zs") ? "pack.zsdic" : endsWith(fileName, ".bcett.byml.zs") ? "bcett.byml.zsdic" : "zs.zsdic";
    const Dictionary* chosen = nullptr;
    for (const Dictionary& dictionary : entries)
        if (dictionary.name == wanted)
            chosen = &dictionary;
    Bytes out(ZSTD_compressBound(data.size()));
    ZSTD_CCtx* context = ZSTD_createCCtx();
    size_t written = chosen ? ZSTD_compress_usingCDict(context, out.data(), out.size(), data.data(), data.size(), chosen->encoder)
                            : ZSTD_compressCCtx(context, out.data(), out.size(), data.data(), data.size(), Level);
    ZSTD_freeCCtx(context);
    if (ZSTD_isError(written))
        return std::nullopt;
    out.resize(written);
    return out;
}

}
