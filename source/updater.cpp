#include "updater.hpp"

#include "config.hpp"

#include <nlohmann/json.hpp>
#include <switch.h>

#include <cctype>
#include <cstdio>
#include <vector>

using nlohmann::json;

namespace {

constexpr size_t HashChunk = 64 * 1024;

std::vector<int> versionParts(const std::string& version) {
    std::vector<int> parts;
    int current = -1;
    for (char c : version) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            current = (current < 0 ? 0 : current * 10) + (c - '0');
        } else if (current >= 0) {
            parts.push_back(current);
            current = -1;
        }
    }
    if (current >= 0)
        parts.push_back(current);
    return parts;
}

std::string sha256Hex(const std::string& path) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file)
        return {};
    Sha256Context context;
    sha256ContextCreate(&context);
    std::vector<unsigned char> buffer(HashChunk);
    size_t read = 0;
    while ((read = std::fread(buffer.data(), 1, buffer.size(), file)) > 0)
        sha256ContextUpdate(&context, buffer.data(), read);
    std::fclose(file);
    unsigned char digest[SHA256_HASH_SIZE];
    sha256ContextGetHash(&context, digest);
    std::string hex;
    char pair[3];
    for (unsigned char byte : digest) {
        std::snprintf(pair, sizeof(pair), "%02x", byte);
        hex += pair;
    }
    return hex;
}

}

bool isNewerVersion(const std::string& candidate, const std::string& current) {
    std::vector<int> offered = versionParts(candidate);
    std::vector<int> running = versionParts(current);
    size_t length = std::max(offered.size(), running.size());
    offered.resize(length, 0);
    running.resize(length, 0);
    return offered > running;
}

std::optional<Release> findUpdate(std::string& error) {
    net::Response response = net::get(std::string(cfg::GitHubApiBase) + "/repos/" + cfg::UpdateRepo + "/releases/latest");
    if (!response.ok) {
        error = "GitHub: " + response.error;
        return std::nullopt;
    }
    json latest = json::parse(response.body, nullptr, false);
    if (latest.is_discarded() || !latest.is_object()) {
        error = "GitHub sent something that isn't JSON";
        return std::nullopt;
    }

    Release release;
    release.version = latest.value("tag_name", "");
    release.notes = latest.value("body", "");
    if (!isNewerVersion(release.version, cfg::AppVersion))
        return std::nullopt;

    for (const json& asset : latest.value("assets", json::array())) {
        std::string name = asset.value("name", "");
        if (name.size() < 4 || name.compare(name.size() - 4, 4, ".nro") != 0)
            continue;
        release.downloadUrl = asset.value("browser_download_url", "");
        release.size = asset.value("size", int64_t{0});
        std::string digest = asset.is_object() && asset.contains("digest") && asset["digest"].is_string() ? asset["digest"].get<std::string>() : "";
        if (digest.rfind("sha256:", 0) == 0)
            release.sha256 = digest.substr(7);
        break;
    }
    if (release.downloadUrl.empty()) {
        error = "release " + release.version + " has no .nro file attached";
        return std::nullopt;
    }
    if (release.sha256.empty()) {
        error = "release " + release.version + " has no SHA-256 checksum, so it can't be verified";
        return std::nullopt;
    }
    return release;
}

std::string installUpdate(const Release& release, const std::string& selfPath, const net::ProgressFn& progress) {
    std::string incoming = selfPath + ".new";
    std::string error = net::download(release.downloadUrl, incoming, release.size, progress);
    if (!error.empty())
        return error;
    if (sha256Hex(incoming) != release.sha256) {
        std::remove(incoming.c_str());
        return "the download doesn't match GitHub's checksum, nothing was changed";
    }
    std::string previous = selfPath + ".old";
    std::remove(previous.c_str());
    if (std::rename(selfPath.c_str(), previous.c_str()) != 0) {
        std::remove(incoming.c_str());
        return "can't move the current " + selfPath + " aside";
    }
    if (std::rename(incoming.c_str(), selfPath.c_str()) != 0) {
        if (std::rename(previous.c_str(), selfPath.c_str()) != 0)
            return "couldn't swap the files. The working version is at " + previous + " and the new one at " + incoming + ", rename one of them to " + selfPath;
        return "can't put the new version in place, kept the current one";
    }
    std::remove(previous.c_str());
    return {};
}
