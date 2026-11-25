#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct ModSource {
    int modId = 0;
    std::string modName;
    std::string imageUrl;
    std::string version;
    int fileId = 0;
    std::string fileName;
    int64_t fileDate = 0;
};

enum class DownloadState { Waiting, Downloading, Ready, Failed };

struct QueuedDownload {
    int id = 0;
    uint64_t titleId = 0;
    ModSource source;
    std::string url;
    int64_t size = 0;
    std::string path;
    DownloadState state = DownloadState::Waiting;
    int64_t done = 0;
    std::string error;
};

class DownloadQueue {
public:
    DownloadQueue();
    ~DownloadQueue();

    void add(QueuedDownload job);
    void remove(int id);
    void release(int id);
    std::vector<QueuedDownload> snapshot();
    int readyCount();
    bool takeChanged();

private:
    void work();
    QueuedDownload* findLocked(int id);
    void eraseLocked(int id);

    std::deque<QueuedDownload> jobs;
    std::mutex lock;
    std::condition_variable wake;
    std::atomic<int> cancelId {0};
    std::atomic<bool> changed {false};
    int nextId = 1;
    bool stopping = false;
    std::thread worker;
};
