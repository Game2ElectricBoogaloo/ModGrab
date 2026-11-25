#pragma once

#include "gfx.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

class ImageLoader {
public:
    ImageLoader();
    ~ImageLoader();

    const gfx::Image* get(const std::string& url, int width, int height);
    bool collect();

private:
    struct Job {
        std::string key;
        std::string url;
        int width;
        int height;
    };

    void work();

    std::unordered_map<std::string, gfx::Image> cache;
    std::deque<Job> queue;
    std::vector<std::pair<std::string, gfx::Image>> finished;
    std::mutex lock;
    std::condition_variable wake;
    bool stopping = false;
    std::thread worker;
};
