#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct Placement {
    std::string root;
    std::string dest;
};

struct ArchiveScan {
    std::vector<std::string> variants;
    std::vector<Placement> placements;
    int blocked = 0;
    std::string error;

    int countIn(const std::string& variant) const;
};

struct ExtractOutcome {
    std::vector<std::string> written;
    int skipped = 0;
    std::string error;
};

using ExtractProgress = std::function<void(int done, int total)>;
using PrepareTarget = std::function<std::string(const std::string& dest, std::string& writeTo)>;

ArchiveScan scanArchive(const std::string& archivePath, uint64_t titleId);
ExtractOutcome extractArchive(const std::string& archivePath, uint64_t titleId, const std::string& variant, int total, const PrepareTarget& prepare,
                              const ExtractProgress& progress);
std::vector<std::string> removeFiles(const std::vector<std::string>& files);
void pruneParents(const std::string& file);
std::string moveFile(const std::string& from, const std::string& to);
std::string makeDirs(const std::string& path);
bool fileExists(const std::string& path);
