#include "net.hpp"

#include "config.hpp"

#include <curl/curl.h>
#include <switch.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <vector>
#include <sys/stat.h>

namespace net {

namespace {

struct Proxy {
    std::string address;
    std::string login;
};

Proxy systemProxy;
std::vector<std::string> bundledRoots;

void readBundledRoots() {
    std::ifstream in(cfg::RootCertsPath);
    std::stringstream raw;
    raw << in.rdbuf();
    std::string text = raw.str();
    const std::string begin = "-----BEGIN CERTIFICATE-----";
    const std::string end = "-----END CERTIFICATE-----";
    for (size_t start = text.find(begin); start != std::string::npos; start = text.find(begin, start)) {
        size_t stop = text.find(end, start);
        if (stop == std::string::npos)
            break;
        stop += end.size();
        bundledRoots.push_back(text.substr(start, stop - start) + "\n");
        start = stop;
    }
}

std::atomic<int> rootsAccepted {0};
std::atomic<u32> lastRejection {0};

CURLcode trustBundledRoots(CURL*, void* context, void*) {
    int accepted = 0;
    for (const std::string& pem : bundledRoots) {
        Result rc = sslContextImportServerPki(static_cast<SslContext*>(context), pem.data(), static_cast<u32>(pem.size()), SslCertificateFormat_Pem, nullptr);
        if (R_SUCCEEDED(rc))
            accepted++;
        else
            lastRejection = rc;
    }
    rootsAccepted = accepted;
    return CURLE_OK;
}

CURL* openHandle(const std::string& url);

std::string peerIssuers(const std::string& url) {
    CURL* curl = openHandle(url);
    if (!curl)
        return {};
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_CERTINFO, 1L);
    curl_easy_perform(curl);
    curl_certinfo* info = nullptr;
    curl_easy_getinfo(curl, CURLINFO_CERTINFO, &info);
    std::string chain;
    for (int i = 0; info && i < info->num_of_certs; i++) {
        for (curl_slist* field = info->certinfo[i]; field; field = field->next) {
            if (std::strncmp(field->data, "Issuer:", 7) == 0)
                chain += (chain.empty() ? "" : " > ") + std::string(field->data + 7);
        }
    }
    curl_easy_cleanup(curl);
    return chain;
}

std::string verificationReport(CURL* curl, const std::string& where) {
    char* url = nullptr;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &url);
    time_t now = time(nullptr);
    char date[16];
    std::strftime(date, sizeof(date), "%Y-%m-%d", std::gmtime(&now));
    std::string report = "couldn't verify the secure connection" + where + ". Switch date: " + date + ". Extra certificates accepted: " +
                         std::to_string(rootsAccepted.load()) + " of " + std::to_string(bundledRoots.size());
    if (lastRejection)
        report += " (rejected with error " + std::to_string(lastRejection.load()) + ")";
    std::string issuers = url ? peerIssuers(url) : std::string();
    report += issuers.empty() ? ". Couldn't read the site's certificate." : ". Site certificate issued by: " + issuers;
    return report;
}

void readSystemProxy() {
    NifmNetworkProfileData profile {};
    if (R_FAILED(nifmGetCurrentNetworkProfile(&profile)))
        return;
    const NifmProxySetting& setting = profile.ip_setting_data.proxy_setting;
    if (!setting.enabled || !setting.server[0])
        return;
    systemProxy.address = std::string(setting.server) + ":" + std::to_string(setting.port);
    if (setting.auto_auth_enabled && setting.user[0])
        systemProxy.login = std::string(setting.user) + ":" + setting.password;
}

std::string describe(CURLcode code, CURL* curl) {
    char* ip = nullptr;
    curl_easy_getinfo(curl, CURLINFO_PRIMARY_IP, &ip);
    std::string where = ip && ip[0] ? std::string(" (tried ") + ip + ")" : std::string();
    switch (code) {
    case CURLE_COULDNT_RESOLVE_HOST:
        return "couldn't look up the server address, check the Switch's DNS settings";
    case CURLE_COULDNT_CONNECT:
        if (ip && (std::string(ip) == "127.0.0.1" || std::string(ip) == "0.0.0.0"))
            return "the Switch's DNS is blocking this site" + where + ", check atmosphere/hosts or your DNS settings";
        return "couldn't reach the server" + where + ". The network may need a proxy (set it in the Switch's wifi settings) or is blocking the Switch";
    case CURLE_COULDNT_RESOLVE_PROXY:
        return "couldn't find the proxy " + systemProxy.address + " set in the Switch's wifi settings";
    case CURLE_PEER_FAILED_VERIFICATION:
        return verificationReport(curl, where);
    default:
        return std::string(curl_easy_strerror(code)) + where;
    }
}

size_t appendBody(char* data, size_t size, size_t count, void* user) {
    static_cast<std::string*>(user)->append(data, size * count);
    return size * count;
}

size_t appendFile(char* data, size_t size, size_t count, void* user) {
    return std::fwrite(data, 1, size * count, static_cast<FILE*>(user));
}

int reportProgress(void* user, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
    const auto& progress = *static_cast<const ProgressFn*>(user);
    return progress(now, total) ? 0 : 1;
}

CURL* openHandle(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl)
        return nullptr;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, cfg::UserAgent);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, cfg::ConnectTimeoutSec);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, cfg::StallTimeoutSec);
    curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, trustBundledRoots);
    if (!systemProxy.address.empty()) {
        curl_easy_setopt(curl, CURLOPT_PROXY, systemProxy.address.c_str());
        if (!systemProxy.login.empty())
            curl_easy_setopt(curl, CURLOPT_PROXYUSERPWD, systemProxy.login.c_str());
    }
    return curl;
}

}

bool init() {
    readSystemProxy();
    readBundledRoots();
    return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
}

void shutdown() {
    curl_global_cleanup();
}

std::string escape(const std::string& text) {
    char* escaped = curl_easy_escape(nullptr, text.c_str(), static_cast<int>(text.size()));
    if (!escaped)
        return {};
    std::string out(escaped);
    curl_free(escaped);
    return out;
}

Response get(const std::string& url) {
    Response response;
    CURL* curl = openHandle(url);
    if (!curl) {
        response.error = "curl_easy_init failed";
        return response;
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    response.ok = code == CURLE_OK;
    if (!response.ok)
        response.error = describe(code, curl);
    curl_easy_cleanup(curl);
    return response;
}

std::string download(const std::string& url, const std::string& path, int64_t expectedSize, const ProgressFn& progress) {
    std::string partPath = path + ".part";
    FILE* file = std::fopen(partPath.c_str(), "wb");
    if (!file)
        return "can't write " + partPath;

    CURL* curl = openHandle(url);
    if (!curl) {
        std::fclose(file);
        std::remove(partPath.c_str());
        return "curl_easy_init failed";
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, reportProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 128L * 1024L);

    CURLcode code = curl_easy_perform(curl);
    std::string error;
    if (code == CURLE_ABORTED_BY_CALLBACK)
        error = "cancelled";
    else if (code != CURLE_OK)
        error = describe(code, curl);
    curl_easy_cleanup(curl);
    bool flushed = std::fclose(file) == 0;
    if (error.empty() && !flushed)
        error = "write failed, SD card full?";

    struct stat info {};
    if (error.empty() && expectedSize > 0 && (stat(partPath.c_str(), &info) != 0 || info.st_size != expectedSize))
        error = "size mismatch, got " + std::to_string(info.st_size) + " of " + std::to_string(expectedSize) + " bytes";

    if (error.empty()) {
        std::remove(path.c_str());
        if (std::rename(partPath.c_str(), path.c_str()) != 0)
            error = "can't move download into place";
    }
    if (!error.empty())
        std::remove(partPath.c_str());
    return error;
}

}
