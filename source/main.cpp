#include "app.hpp"
#include "config.hpp"
#include "gfx.hpp"
#include "installer.hpp"
#include "net.hpp"

#include <switch.h>

#include <cstdio>

namespace {

void showFatal(const std::string& message) {
    consoleInit(nullptr);
    std::printf("ModGrab can't start:\n%s\n\nPress + to exit.\n", message.c_str());
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr);
}

}

int main(int argc, char** argv) {
    std::string gfxError = gfx::init();
    if (!gfxError.empty()) {
        showFatal(gfxError);
        return 0;
    }

    std::string warning;
    bool romfsUp = R_SUCCEEDED(romfsInit());
    if (!romfsUp)
        warning = "Couldn't open ModGrab's built-in files, secure connections may fail.";
    bool socketsUp = R_SUCCEEDED(socketInitializeDefault());
    bool nifmUp = R_SUCCEEDED(nifmInitialize(NifmServiceType_User));
    bool curlUp = socketsUp && nifmUp && net::init();
    if (!curlUp)
        warning += (warning.empty() ? "" : " ") + std::string("Networking failed to start, only uninstalling will work this session.");

    Result nsResult = nsInitialize();
    if (R_SUCCEEDED(nsResult)) {
        std::string dirError = makeDirs(cfg::DataDir);
        if (!dirError.empty())
            warning = warning.empty() ? dirError : warning + " " + dirError;
        std::string selfPath = argc > 0 && argv[0] && std::string(argv[0]).rfind(cfg::SdRoot, 0) == 0 ? argv[0] : cfg::SelfPath;
        App(warning, selfPath).run();
        nsExit();
    }

    if (curlUp)
        net::shutdown();
    if (nifmUp)
        nifmExit();
    if (socketsUp)
        socketExit();
    if (romfsUp)
        romfsExit();
    gfx::shutdown();

    if (R_FAILED(nsResult))
        showFatal("couldn't open the ns service (error " + std::to_string(nsResult) + ")");
    return 0;
}
