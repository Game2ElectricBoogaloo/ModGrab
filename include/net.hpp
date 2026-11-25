#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace net {

struct Response {
    bool ok = false;
    long status = 0;
    std::string body;
    std::string error;
};

using ProgressFn = std::function<bool(int64_t now, int64_t total)>;

bool init();
void shutdown();

std::string escape(const std::string& text);
Response get(const std::string& url);
std::string download(const std::string& url, const std::string& path, int64_t expectedSize, const ProgressFn& progress);

}
