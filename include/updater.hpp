#pragma once

#include "net.hpp"

#include <cstdint>
#include <optional>
#include <string>

struct Release {
    std::string version;
    std::string notes;
    std::string downloadUrl;
    int64_t size = 0;
    std::string sha256;
};

bool isNewerVersion(const std::string& candidate, const std::string& current);
std::optional<Release> findUpdate(std::string& error);
std::string installUpdate(const Release& release, const std::string& selfPath, const net::ProgressFn& progress);
