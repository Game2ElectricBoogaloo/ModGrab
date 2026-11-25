#include "downloads.hpp"

#include "net.hpp"

#include <algorithm>
#include <cstdio>

DownloadQueue::DownloadQueue() : worker(&DownloadQueue::work, this) {}

DownloadQueue::~DownloadQueue() {
    {
        std::lock_guard<std::mutex> guard(lock);
        stopping = true;
    }
    cancelId = -1;
    wake.notify_all();
    worker.join();
}

void DownloadQueue::add(QueuedDownload job) {
    {
        std::lock_guard<std::mutex> guard(lock);
        job.id = nextId++;
        job.state = DownloadState::Waiting;
        jobs.push_back(std::move(job));
    }
    changed = true;
    wake.notify_one();
}

void DownloadQueue::remove(int id) {
    std::lock_guard<std::mutex> guard(lock);
    QueuedDownload* job = findLocked(id);
    if (!job)
        return;
    if (job->state == DownloadState::Downloading) {
        cancelId = id;
        return;
    }
    if (job->state == DownloadState::Ready)
        std::remove(job->path.c_str());
    eraseLocked(id);
    changed = true;
}

void DownloadQueue::release(int id) {
    std::lock_guard<std::mutex> guard(lock);
    QueuedDownload* job = findLocked(id);
    if (!job || job->state == DownloadState::Downloading)
        return;
    eraseLocked(id);
    changed = true;
}

std::vector<QueuedDownload> DownloadQueue::snapshot() {
    std::lock_guard<std::mutex> guard(lock);
    return {jobs.begin(), jobs.end()};
}

int DownloadQueue::readyCount() {
    std::lock_guard<std::mutex> guard(lock);
    return static_cast<int>(std::count_if(jobs.begin(), jobs.end(), [](const QueuedDownload& job) { return job.state == DownloadState::Ready; }));
}

bool DownloadQueue::takeChanged() {
    return changed.exchange(false);
}

QueuedDownload* DownloadQueue::findLocked(int id) {
    auto found = std::find_if(jobs.begin(), jobs.end(), [id](const QueuedDownload& job) { return job.id == id; });
    return found == jobs.end() ? nullptr : &*found;
}

void DownloadQueue::eraseLocked(int id) {
    jobs.erase(std::remove_if(jobs.begin(), jobs.end(), [id](const QueuedDownload& job) { return job.id == id; }), jobs.end());
}

void DownloadQueue::work() {
    while (true) {
        int id = 0;
        std::string url;
        std::string path;
        int64_t size = 0;
        {
            std::unique_lock<std::mutex> guard(lock);
            wake.wait(guard, [this] {
                return stopping || std::any_of(jobs.begin(), jobs.end(), [](const QueuedDownload& job) { return job.state == DownloadState::Waiting; });
            });
            if (stopping)
                return;
            QueuedDownload& job = *std::find_if(jobs.begin(), jobs.end(), [](const QueuedDownload& entry) { return entry.state == DownloadState::Waiting; });
            job.state = DownloadState::Downloading;
            id = job.id;
            url = job.url;
            path = job.path;
            size = job.size;
        }
        changed = true;

        std::string error = net::download(url, path, size, [&](int64_t now, int64_t) {
            {
                std::lock_guard<std::mutex> guard(lock);
                if (QueuedDownload* job = findLocked(id))
                    job->done = now;
            }
            changed = true;
            return cancelId != id && cancelId != -1;
        });

        std::lock_guard<std::mutex> guard(lock);
        QueuedDownload* job = findLocked(id);
        if (cancelId == id) {
            cancelId = 0;
            std::remove(path.c_str());
            eraseLocked(id);
        } else if (job) {
            job->state = error.empty() ? DownloadState::Ready : DownloadState::Failed;
            job->error = error;
        }
        changed = true;
    }
}
