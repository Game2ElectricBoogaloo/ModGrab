#pragma once

#include "downloads.hpp"
#include "gamebanana.hpp"
#include "images.hpp"
#include "net.hpp"
#include "store.hpp"
#include "titles.hpp"
#include "ui.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

class App {
public:
    App(std::string startupWarning, std::string selfPath);
    void run();

private:
    void gamesScreen();
    void modsScreen(const Title& title, const gb::Game& game);
    void modScreen(const Title& title, int modId);
    void detailsScreen(const gb::Mod& mod);
    void installedScreen();
    void localScreen();
    void queueScreen();
    void presetsMenu(uint64_t titleId);

    std::optional<gb::Game> resolveGame(const Title& title, bool forcePick);
    bool confirmFile(const Title& title, const gb::Mod& mod, const gb::ModFile& file, const std::string& action);
    void installFile(const Title& title, const gb::Mod& mod, const gb::ModFile& file);
    void queueFile(const Title& title, const gb::Mod& mod, const gb::ModFile& file);
    bool installArchive(const Title& title, const ModSource& source, const std::string& archivePath, bool deleteArchive);
    void uninstall(int modId);
    void toggle(int modId);
    void checkUpdates();
    void offerSelfUpdate();
    net::ProgressFn progressReporter(const std::string& title, const std::string& detail, int64_t expectedSize);
    bool requireOnline();
    std::string persist();
    void reportErrors(const std::string& title, const std::string& success, const std::vector<std::string>& errors);
    const Title* findTitle(uint64_t titleId) const;
    Title titleFor(const InstalledMod& mod) const;

    ui::Input input;
    Store store;
    ImageLoader images;
    DownloadQueue downloads;
    std::vector<Title> titles;
    std::map<int, std::string> updateStatus;
    std::string warning;
    std::string selfPath;
};

ModSource sourceFrom(const gb::Mod& mod, const gb::ModFile& file);
std::string versionLabel(const std::string& version);
bool hasUpdate(const InstalledMod& installed, const gb::Mod& mod);
