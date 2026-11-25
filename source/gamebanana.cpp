#include "gamebanana.hpp"

#include "config.hpp"
#include "net.hpp"

#include <nlohmann/json.hpp>

#include <cstring>

using nlohmann::json;

namespace gb {

namespace {

std::optional<json> fetchJson(const std::string& path, std::string& error) {
    net::Response response = net::get(std::string(cfg::ApiBase) + path);
    if (!response.ok) {
        error = "GameBanana: " + response.error;
        if (response.status)
            error += " (HTTP " + std::to_string(response.status) + ")";
        return std::nullopt;
    }
    json parsed = json::parse(response.body, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        error = "GameBanana sent something that isn't JSON";
        return std::nullopt;
    }
    if (parsed.contains("_sErrorCode") || parsed.contains("_sErrorMessage")) {
        error = "GameBanana: " + parsed.value("_sErrorMessage", parsed.value("_sErrorCode", std::string("unknown error")));
        return std::nullopt;
    }
    return parsed;
}

std::string stringField(const json& node, const char* key) {
    auto it = node.find(key);
    return it != node.end() && it->is_string() ? it->get<std::string>() : std::string();
}

int64_t numberField(const json& node, const char* key) {
    auto it = node.find(key);
    return it != node.end() && it->is_number() ? it->get<int64_t>() : 0;
}

std::string submitterName(const json& record) {
    auto it = record.find("_aSubmitter");
    return it != record.end() && it->is_object() ? stringField(*it, "_sName") : std::string();
}

std::string plainText(const std::string& html) {
    std::string out;
    bool inTag = false;
    for (size_t i = 0; i < html.size(); i++) {
        char c = html[i];
        if (c == '<') {
            inTag = true;
            if (html.compare(i, 3, "<br") == 0 || html.compare(i, 3, "<p>") == 0 || html.compare(i, 4, "</p>") == 0 || html.compare(i, 4, "<li>") == 0)
                out += '\n';
        } else if (c == '>') {
            inTag = false;
        } else if (!inTag) {
            out += c;
        }
    }
    const std::pair<const char*, const char*> entities[] = {{"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&#039;", "'"}, {"&nbsp;", " "}};
    for (const auto& [entity, replacement] : entities)
        for (size_t pos = out.find(entity); pos != std::string::npos; pos = out.find(entity, pos))
            out.replace(pos, std::strlen(entity), replacement);
    std::string tidy;
    for (char c : out) {
        if (c == '\n' && (tidy.empty() || tidy.back() == '\n'))
            continue;
        tidy += c;
    }
    while (!tidy.empty() && (tidy.back() == '\n' || tidy.back() == ' '))
        tidy.pop_back();
    return tidy;
}

std::string imageUrl(const json& record, const char* sizeKey) {
    auto media = record.find("_aPreviewMedia");
    if (media == record.end() || !media->is_object())
        return {};
    auto images = media->find("_aImages");
    if (images == media->end() || !images->is_array() || images->empty())
        return {};
    const json& first = images->front();
    std::string base = stringField(first, "_sBaseUrl");
    std::string file = stringField(first, sizeKey);
    if (file.empty())
        file = stringField(first, "_sFile");
    return base.empty() || file.empty() ? std::string() : base + "/" + file;
}

std::optional<ModPage> readModPage(const json& feed, std::string& error) {
    auto records = feed.find("_aRecords");
    if (records == feed.end() || !records->is_array()) {
        error = "GameBanana mod list has no records";
        return std::nullopt;
    }
    ModPage page;
    for (const json& record : *records) {
        std::string model = stringField(record, "_sModelName");
        if (!model.empty() && model != "Mod")
            continue;
        ModSummary mod;
        mod.id = static_cast<int>(numberField(record, "_idRow"));
        mod.name = stringField(record, "_sName");
        mod.submitter = submitterName(record);
        mod.thumbnailUrl = imageUrl(record, "_sFile220");
        mod.dateUpdated = numberField(record, "_tsDateUpdated");
        if (!mod.dateUpdated)
            mod.dateUpdated = numberField(record, "_tsDateAdded");
        page.mods.push_back(std::move(mod));
    }
    auto meta = feed.find("_aMetadata");
    if (meta != feed.end() && meta->is_object()) {
        page.total = static_cast<int>(numberField(*meta, "_nRecordCount"));
        auto complete = meta->find("_bIsComplete");
        page.complete = complete == meta->end() || !complete->is_boolean() || complete->get<bool>();
    }
    return page;
}

}

std::vector<Game> searchGames(const std::string& title, std::string& error) {
    auto found = fetchJson("/Util/Search/Results?_sModelName=Game&_sOrder=best_match&_sSearchString=" + net::escape(title), error);
    std::vector<Game> games;
    if (!found)
        return games;
    auto records = found->find("_aRecords");
    if (records == found->end() || !records->is_array())
        return games;
    for (const json& record : *records) {
        Game game;
        game.id = static_cast<int>(numberField(record, "_idRow"));
        game.name = stringField(record, "_sName");
        if (game.id)
            games.push_back(std::move(game));
    }
    return games;
}

std::optional<ModPage> listMods(int gameId, int page, std::string& error) {
    auto feed = fetchJson("/Game/" + std::to_string(gameId) + "/Subfeed?_csvModelInclusions=Mod&_nPage=" + std::to_string(page), error);
    if (!feed)
        return std::nullopt;
    return readModPage(*feed, error);
}

std::optional<ModPage> searchMods(int gameId, const std::string& query, int page, std::string& error) {
    auto found = fetchJson("/Util/Search/Results?_sModelName=Mod&_sOrder=best_match&_idGameRow=" + std::to_string(gameId) +
                                 "&_nPage=" + std::to_string(page) + "&_sSearchString=" + net::escape(query),
                             error);
    if (!found)
        return std::nullopt;
    return readModPage(*found, error);
}

std::optional<Mod> fetchMod(int modId, std::string& error) {
    auto profile = fetchJson("/Mod/" + std::to_string(modId) + "/ProfilePage", error);
    if (!profile)
        return std::nullopt;

    Mod mod;
    mod.id = modId;
    mod.name = stringField(*profile, "_sName");
    mod.version = stringField(*profile, "_sVersion");
    mod.submitter = submitterName(*profile);
    mod.description = plainText(stringField(*profile, "_sText"));
    mod.previewUrl = imageUrl(*profile, "_sFile530");
    auto media = profile->find("_aPreviewMedia");
    if (media != profile->end() && media->is_object() && media->contains("_aImages") && (*media)["_aImages"].is_array()) {
        for (const json& image : (*media)["_aImages"]) {
            std::string base = stringField(image, "_sBaseUrl");
            std::string file = stringField(image, "_sFile");
            if (!base.empty() && !file.empty())
                mod.gallery.push_back(base + "/" + file);
        }
    }
    mod.dateUpdated = numberField(*profile, "_tsDateUpdated");
    if (!mod.dateUpdated)
        mod.dateUpdated = numberField(*profile, "_tsDateAdded");

    auto files = profile->find("_aFiles");
    if (files != profile->end() && files->is_array()) {
        for (const json& entry : *files) {
            ModFile file;
            file.id = static_cast<int>(numberField(entry, "_idRow"));
            file.name = stringField(entry, "_sFile");
            file.description = stringField(entry, "_sDescription");
            file.downloadUrl = stringField(entry, "_sDownloadUrl");
            file.size = numberField(entry, "_nFilesize");
            file.dateAdded = numberField(entry, "_tsDateAdded");
            if (file.id && !file.downloadUrl.empty())
                mod.files.push_back(std::move(file));
        }
    }
    return mod;
}

}
