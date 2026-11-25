#include "installer.hpp"

#include "config.hpp"
#include "titles.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <optional>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr size_t ReadBlock = 64 * 1024;

std::string normalize(const char* raw) {
    std::string path(raw ? raw : "");
    std::replace(path.begin(), path.end(), '\\', '/');
    while (path.rfind("./", 0) == 0)
        path.erase(0, 2);
    while (!path.empty() && path.front() == '/')
        path.erase(0, 1);
    return path;
}

bool escapesRoot(const std::string& path) {
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos)
            end = path.size();
        if (path.compare(start, end - start, "..") == 0)
            return true;
        start = end + 1;
    }
    return path.find(':') != std::string::npos;
}

size_t findSegment(const std::string& path, const char* name) {
    size_t length = std::strlen(name);
    for (size_t pos = 0; pos + length < path.size(); pos++) {
        if ((pos == 0 || path[pos - 1] == '/') && path[pos + length] == '/' && strncasecmp(path.c_str() + pos, name, length) == 0)
            return pos;
    }
    return std::string::npos;
}

bool isTitleFolder(const std::string& path, size_t pos) {
    if (pos + 17 > path.size() || path[pos + 16] != '/')
        return false;
    return std::all_of(path.begin() + pos, path.begin() + pos + 16, [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); });
}

std::string after(const std::string& path, size_t pos, const char* name) {
    return path.substr(pos + std::strlen(name));
}

bool isSystemTitle(const std::string& path, size_t pos) {
    return strncasecmp(path.c_str() + pos, "010000000000", 12) == 0;
}

std::optional<Placement> placeInContents(const std::string& root, const std::string& rest, bool& blocked) {
    if (!isTitleFolder(rest, 0) || isSystemTitle(rest, 0)) {
        blocked = true;
        return std::nullopt;
    }
    return Placement{root, std::string(cfg::AtmosphereDir) + "contents/" + rest};
}

std::optional<Placement> place(const std::string& path, uint64_t titleId, bool& blocked) {
    blocked = false;
    if (path.empty() || path.back() == '/' || escapesRoot(path))
        return std::nullopt;

    if (size_t pos = findSegment(path, "atmosphere"); pos != std::string::npos) {
        std::string inside = after(path, pos, "atmosphere/");
        if (findSegment(inside, "contents") == 0)
            return placeInContents(path.substr(0, pos), after(inside, 0, "contents/"), blocked);
        if (findSegment(inside, "exefs_patches") == 0)
            return Placement{path.substr(0, pos), cfg::AtmosphereDir + inside};
        blocked = true;
        return std::nullopt;
    }

    if (size_t pos = findSegment(path, "contents"); pos != std::string::npos && isTitleFolder(path, pos + 9))
        return placeInContents(path.substr(0, pos), after(path, pos, "contents/"), blocked);

    if (size_t pos = findSegment(path, "exefs_patches"); pos != std::string::npos)
        return Placement{path.substr(0, pos), std::string(cfg::AtmosphereDir) + "exefs_patches/" + after(path, pos, "exefs_patches/")};

    size_t romfs = findSegment(path, "romfs");
    size_t exefs = findSegment(path, "exefs");
    if (romfs != std::string::npos || exefs != std::string::npos) {
        bool useRomfs = exefs == std::string::npos || (romfs != std::string::npos && romfs < exefs);
        size_t pos = useRomfs ? romfs : exefs;
        const char* segment = useRomfs ? "romfs/" : "exefs/";
        return Placement{path.substr(0, pos), std::string(cfg::AtmosphereDir) + "contents/" + formatTitleId(titleId) + "/" + segment + after(path, pos, segment)};
    }

    if (titleId == cfg::SmashUltimateId) {
        if (size_t pos = findSegment(path, "mods"); pos != std::string::npos)
            return Placement{path.substr(0, pos), cfg::UltimateModsDir + after(path, pos, "mods/")};
        size_t slash = path.find('/');
        if (slash != std::string::npos)
            return Placement{path.substr(0, slash + 1), cfg::UltimateModsDir + path};
    }

    return std::nullopt;
}

bool headerReadable(int status) {
    return status == ARCHIVE_OK || status == ARCHIVE_WARN;
}

std::string damagedMessage(archive* reader) {
    const char* reason = archive_error_string(reader);
    return std::string("archive is damaged: ") + (reason ? reason : "unknown reason");
}

archive* openArchive(const std::string& path, std::string& error) {
    archive* reader = archive_read_new();
    archive_read_support_format_all(reader);
    archive_read_support_filter_all(reader);
    if (archive_read_open_filename(reader, path.c_str(), ReadBlock) != ARCHIVE_OK) {
        const char* reason = archive_error_string(reader);
        error = std::string("can't open archive: ") + (reason ? reason : "unknown format");
        archive_read_free(reader);
        return nullptr;
    }
    return reader;
}

std::string writeEntry(archive* reader, const std::string& dest) {
    size_t slash = dest.rfind('/');
    if (slash != std::string::npos) {
        std::string dirError = makeDirs(dest.substr(0, slash));
        if (!dirError.empty())
            return dirError;
    }
    FILE* out = std::fopen(dest.c_str(), "wb");
    if (!out)
        return "can't create " + dest + ": " + std::strerror(errno);

    const void* block = nullptr;
    size_t size = 0;
    la_int64_t offset = 0;
    std::string error;
    while (true) {
        int status = archive_read_data_block(reader, &block, &size, &offset);
        if (status == ARCHIVE_EOF)
            break;
        if (status < ARCHIVE_WARN) {
            error = damagedMessage(reader);
            break;
        }
        if (std::fseek(out, offset, SEEK_SET) != 0 || std::fwrite(block, 1, size, out) != size) {
            error = "write failed for " + dest + ", SD card full?";
            break;
        }
    }
    if (std::fclose(out) != 0 && error.empty())
        error = "write failed for " + dest;
    if (!error.empty())
        std::remove(dest.c_str());
    return error;
}

bool isStopDir(const std::string& dir) {
    for (const char* stop : {cfg::DataDir, cfg::BackupDir, cfg::StashDir, cfg::LocalDir, cfg::DownloadDir, cfg::LayerDir, cfg::OriginalsDir})
        if (dir == stop)
            return true;
    return dir + "/" == cfg::AtmosphereDir || dir + "/" == std::string(cfg::AtmosphereDir) + "contents/" || dir + "/" == cfg::UltimateModsDir ||
           dir + "/" == cfg::SdRoot || dir.size() <= std::strlen(cfg::SdRoot);
}

}

std::string makeDirs(const std::string& path) {
    for (size_t pos = std::strlen(cfg::SdRoot); pos <= path.size(); pos++) {
        if (pos != path.size() && path[pos] != '/')
            continue;
        std::string partial = path.substr(0, pos);
        if (mkdir(partial.c_str(), 0777) != 0 && errno != EEXIST)
            return "can't create folder " + partial + ": " + std::strerror(errno);
    }
    return {};
}

int ArchiveScan::countIn(const std::string& variant) const {
    return static_cast<int>(std::count_if(placements.begin(), placements.end(), [&](const Placement& placement) { return placement.root == variant; }));
}

ArchiveScan scanArchive(const std::string& archivePath, uint64_t titleId) {
    ArchiveScan scan;
    archive* reader = openArchive(archivePath, scan.error);
    if (!reader)
        return scan;

    archive_entry* entry = nullptr;
    int status = ARCHIVE_OK;
    while (headerReadable(status = archive_read_next_header(reader, &entry))) {
        if (archive_entry_filetype(entry) != AE_IFREG)
            continue;
        bool blocked = false;
        auto placement = place(normalize(archive_entry_pathname(entry)), titleId, blocked);
        scan.blocked += blocked;
        if (!placement)
            continue;
        if (std::find(scan.variants.begin(), scan.variants.end(), placement->root) == scan.variants.end())
            scan.variants.push_back(placement->root);
        scan.placements.push_back(std::move(*placement));
    }
    if (status != ARCHIVE_EOF)
        scan.error = damagedMessage(reader);
    archive_read_free(reader);
    std::sort(scan.variants.begin(), scan.variants.end());
    return scan;
}

ExtractOutcome extractArchive(const std::string& archivePath, uint64_t titleId, const std::string& variant, int total, const PrepareTarget& prepare,
                              const ExtractProgress& progress) {
    ExtractOutcome outcome;
    archive* reader = openArchive(archivePath, outcome.error);
    if (!reader)
        return outcome;

    archive_entry* entry = nullptr;
    int status = ARCHIVE_OK;
    while (headerReadable(status = archive_read_next_header(reader, &entry))) {
        if (archive_entry_filetype(entry) != AE_IFREG)
            continue;
        bool blocked = false;
        auto placement = place(normalize(archive_entry_pathname(entry)), titleId, blocked);
        if (!placement || placement->root != variant) {
            outcome.skipped++;
            continue;
        }
        std::string writeTo = placement->dest;
        std::string error = prepare(placement->dest, writeTo);
        if (error.empty())
            error = writeEntry(reader, writeTo);
        if (!error.empty()) {
            outcome.error = error;
            break;
        }
        outcome.written.push_back(placement->dest);
        progress(static_cast<int>(outcome.written.size()), total);
    }
    if (outcome.error.empty() && status != ARCHIVE_EOF)
        outcome.error = damagedMessage(reader);
    archive_read_free(reader);
    return outcome;
}

std::optional<std::vector<uint8_t>> readFile(const std::string& path) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file)
        return std::nullopt;
    std::vector<uint8_t> bytes;
    uint8_t chunk[ReadBlock];
    size_t read = 0;
    while ((read = std::fread(chunk, 1, sizeof(chunk), file)) > 0)
        bytes.insert(bytes.end(), chunk, chunk + read);
    bool failed = std::ferror(file);
    std::fclose(file);
    if (failed)
        return std::nullopt;
    return bytes;
}

std::string writeFile(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::string dirError = makeDirs(path.substr(0, path.rfind('/')));
    if (!dirError.empty())
        return dirError;
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file)
        return "can't create " + path + ": " + std::strerror(errno);
    bool written = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    bool closed = std::fclose(file) == 0;
    if (!written || !closed) {
        std::remove(path.c_str());
        return "write failed for " + path + ", SD card full?";
    }
    return {};
}

bool fileExists(const std::string& path) {
    struct stat info {};
    return stat(path.c_str(), &info) == 0;
}

void pruneParents(const std::string& file) {
    std::string dir = file.substr(0, file.rfind('/'));
    while (!isStopDir(dir) && rmdir(dir.c_str()) == 0)
        dir.erase(dir.rfind('/'));
}

std::string moveFile(const std::string& from, const std::string& to) {
    std::string dirError = makeDirs(to.substr(0, to.rfind('/')));
    if (!dirError.empty())
        return dirError;
    std::remove(to.c_str());
    if (std::rename(from.c_str(), to.c_str()) != 0)
        return "can't move " + from + " to " + to + ": " + std::strerror(errno);
    pruneParents(from);
    return {};
}

std::vector<std::string> removeFiles(const std::vector<std::string>& files) {
    std::vector<std::string> failed;
    for (const std::string& file : files) {
        if (std::remove(file.c_str()) != 0 && errno != ENOENT)
            failed.push_back(file);
        else
            pruneParents(file);
    }
    return failed;
}
