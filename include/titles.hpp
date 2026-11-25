#pragma once

#include "gfx.hpp"

#include <switch.h>

#include <string>
#include <vector>

struct Title {
    u64 id = 0;
    std::string name;
    std::string version;
    gfx::Image icon;
};

std::vector<Title> listInstalledTitles(std::string& error);
std::string formatTitleId(u64 id);
