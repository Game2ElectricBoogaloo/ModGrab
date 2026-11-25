#include "images.hpp"

#include "net.hpp"

namespace {

constexpr size_t MaxQueued = 32;

}

ImageLoader::ImageLoader() : worker(&ImageLoader::work, this) {}

ImageLoader::~ImageLoader() {
    {
        std::lock_guard<std::mutex> guard(lock);
        stopping = true;
        queue.clear();
    }
    wake.notify_all();
    worker.join();
}

const gfx::Image* ImageLoader::get(const std::string& url, int width, int height) {
    if (url.empty())
        return nullptr;
    std::string key = url + "@" + std::to_string(width) + "x" + std::to_string(height);
    auto found = cache.find(key);
    if (found != cache.end())
        return found->second.empty() ? nullptr : &found->second;

    cache.emplace(key, gfx::Image{});
    {
        std::lock_guard<std::mutex> guard(lock);
        queue.push_front({key, url, width, height});
        if (queue.size() > MaxQueued) {
            cache.erase(queue.back().key);
            queue.pop_back();
        }
    }
    wake.notify_one();
    return nullptr;
}

bool ImageLoader::collect() {
    std::vector<std::pair<std::string, gfx::Image>> done;
    {
        std::lock_guard<std::mutex> guard(lock);
        done.swap(finished);
    }
    for (auto& [key, image] : done)
        cache[key] = std::move(image);
    return !done.empty();
}

void ImageLoader::work() {
    while (true) {
        Job job;
        {
            std::unique_lock<std::mutex> guard(lock);
            wake.wait(guard, [this] { return stopping || !queue.empty(); });
            if (stopping)
                return;
            job = std::move(queue.front());
            queue.pop_front();
        }
        net::Response response = net::get(job.url);
        gfx::Image image;
        if (response.ok)
            image = gfx::decodeImage(response.body.data(), response.body.size(), job.width, job.height);
        std::lock_guard<std::mutex> guard(lock);
        finished.emplace_back(std::move(job.key), std::move(image));
    }
}
