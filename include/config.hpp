#pragma once

namespace cfg {

inline constexpr const char* AppName = "ModGrab";
inline constexpr const char* AppVersion = APP_VERSION;
inline constexpr const char* UserAgent = "ModGrab/" APP_VERSION " (Nintendo Switch)";

inline constexpr const char* DataDir = "sdmc:/switch/ModGrab";
inline constexpr const char* StatePath = "sdmc:/switch/ModGrab/state.json";
inline constexpr const char* DownloadDir = "sdmc:/switch/ModGrab/downloads";
inline constexpr const char* BackupDir = "sdmc:/switch/ModGrab/backups";
inline constexpr const char* StashDir = "sdmc:/switch/ModGrab/disabled";
inline constexpr const char* LocalDir = "sdmc:/switch/ModGrab/local";
inline constexpr const char* SdRoot = "sdmc:/";
inline constexpr const char* AtmosphereDir = "sdmc:/atmosphere/";
inline constexpr const char* UltimateModsDir = "sdmc:/ultimate/mods/";

inline constexpr unsigned long long SmashUltimateId = 0x01006A800016E000ULL;

inline constexpr const char* RootCertsPath = "romfs:/certs.pem";

inline constexpr const char* UpdateRepo = "Game2ElectricBoogaloo/ModGrab";
inline constexpr const char* GitHubApiBase = "https://api.github.com";
inline constexpr const char* SelfPath = "sdmc:/switch/ModGrab/ModGrab.nro";

inline constexpr const char* ApiBase = "https://gamebanana.com/apiv11";

inline constexpr long ConnectTimeoutSec = 15;
inline constexpr long StallTimeoutSec = 30;

}
