#include "titles.hpp"

#include "theme.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <strings.h>

namespace {

constexpr s32 RecordBatch = 64;

std::string readName(NacpStruct& nacp) {
    NacpLanguageEntry* entry = nullptr;
    if (R_SUCCEEDED(nacpGetLanguageEntry(&nacp, &entry)) && entry)
        return entry->name;
    return {};
}

}

std::string formatTitleId(u64 id) {
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016lX", id);
    return buffer;
}

std::vector<Title> listInstalledTitles(std::string& error) {
    std::vector<Title> titles;
    auto control = std::make_unique<NsApplicationControlData>();
    NsApplicationRecord records[RecordBatch];

    s32 offset = 0;
    while (true) {
        s32 count = 0;
        Result rc = nsListApplicationRecord(records, RecordBatch, offset, &count);
        if (R_FAILED(rc)) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "nsListApplicationRecord failed (0x%X)", rc);
            error = buffer;
            break;
        }
        for (s32 i = 0; i < count; i++) {
            u64 size = 0;
            if (R_FAILED(nsGetApplicationControlData(NsApplicationControlSource_Storage, records[i].application_id, control.get(), sizeof(NsApplicationControlData), &size)))
                continue;
            Title title;
            title.id = records[i].application_id;
            title.name = readName(control->nacp);
            title.version = control->nacp.display_version;
            if (title.name.empty())
                title.name = formatTitleId(title.id);
            if (size > sizeof(NacpStruct))
                title.icon = gfx::decodeImage(control->icon, size - sizeof(NacpStruct), theme::TileSize, theme::TileSize);
            titles.push_back(std::move(title));
        }
        if (count < RecordBatch)
            break;
        offset += count;
    }

    std::sort(titles.begin(), titles.end(), [](const Title& a, const Title& b) {
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return titles;
}
