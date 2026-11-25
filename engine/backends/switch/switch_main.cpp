// SPDX-License-Identifier: GPL-3.0-or-later
// THE GAME'S ENTRY POINT ON THE NINTENDO SWITCH - `todo/switch-port.md` S1.
//
// The Vita's arrangement (`backends/vita/vita_main.cpp`): `play.cpp` is
// compiled with `-Dmain=omk_play_main` and this file is the real `main`,
// supplying the arguments a desktop user would type. hbmenu launches the NRO
// from `sdmc:/switch/omk/`, and everything else lives beside it:
//
//   sdmc:/switch/omk/omk.nro         the program
//   sdmc:/switch/omk/gamedata/       the game's data tree, copied from the
//                                    disc in BINARY (MESHES/, IAM/, ... inside)
//   romfs:/tables/                   the tables lifted out of the exe, inside
//                                    the NRO (CMakeLists.txt stages them)
//   sdmc:/switch/omk/saves/GAMES     where saves go (never into the data tree -
//                                    CLAUDE.md §1)
//   sdmc:/switch/omk/omk.ini         the config, read when present; OMK's menu writes it
//   sdmc:/switch/omk/args.txt        EXTRA arguments, one per line, AFTER the
//                                    defaults ('#' starts a comment) - `--area`,
//                                    `--res`, `--fps` without a rebuild
//   sdmc:/switch/omk/omk-play-YYYYMMDD-HHMMSS.log / .err   each run's output
//
// Launched with `nxlink -s omk.nro` (the dev loop over Wi-Fi) the output goes
// to the Mac's terminal instead of the files.
//
// The frame is the mode's own: 1280x720 handheld, 1920x1080 docked, taken at
// launch (a dock change mid-run is step S4). The 3D keeps its horizontal FOV;
// the 640x480 interface is stretched by the engine's own I2D scaling, as on
// the Vita; `--res 960x720` in args.txt gives the 4:3 frame back.
#include <switch.h>

#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>

#include <cstdio>
#include <ctime>
#include <fstream>
#include <new>
#include <string>
#include <vector>

int omk_play_main(int argc, char** argv);

namespace {
constexpr const char* kHome   = "sdmc:/switch/omk";
constexpr const char* kRoot   = "sdmc:/switch/omk/gamedata";
constexpr const char* kTables = "romfs:/tables";
constexpr const char* kSaves  = "sdmc:/switch/omk/saves/GAMES";
constexpr const char* kIni    = "sdmc:/switch/omk/omk.ini";
constexpr const char* kExtra  = "sdmc:/switch/omk/args.txt";

// `play.cpp`'s `main` is one function with ~470 locals in a single frame; the
// Vita needed 8 MiB for it and the recursion below it. An NRO's main stack is
// whatever the loader gave it, so the game runs on a thread whose stack this
// file sets.
constexpr std::size_t kGameStackBytes = 8u * 1024 * 1024;

struct GameArgs {
    std::vector<std::string> args;
    int rc = 1;
};

void* runGame(void* p) {
    auto* g = static_cast<GameArgs*>(p);
    std::vector<char*> argv;
    for (auto& a : g->args) argv.push_back(a.data());
    argv.push_back(nullptr);
    try {
        g->rc = omk_play_main(static_cast<int>(g->args.size()), argv.data());
    } catch (const std::bad_alloc&) {
        // out of memory is SAID, not a silent abort: the log is the only
        // report a console run leaves
        std::printf("FATAL: out of memory (std::bad_alloc)\n");
        std::fprintf(stderr, "FATAL: out of memory\n");
    }
    return nullptr;
}

// the process's memory as the kernel accounts it - applet mode (launched from
// the album) gets a fraction of what title takeover (R held over a game) gets
void logMemory(const char* when) {
    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    std::printf("memory %s: %llu MB used of %llu MB\n", when,
                static_cast<unsigned long long>(used >> 20),
                static_cast<unsigned long long>(total >> 20));
}

const char* appletTypeName(AppletType t) {
    switch (t) {
    case AppletType_Application: return "application (title takeover: full memory)";
    case AppletType_SystemApplication: return "system application";
    case AppletType_LibraryApplet: return "library applet (launched from the album: LITTLE memory)";
    case AppletType_OverlayApplet: return "overlay applet";
    case AppletType_SystemApplet: return "system applet";
    default: return "unknown";
    }
}
}  // namespace

int main(int, char**) {
    romfsInit();
    mkdir(kHome, 0777);
    mkdir("sdmc:/switch/omk/saves", 0777);

    const bool nxlink = __nxlink_host.s_addr != 0;
    if (nxlink) {
        socketInitializeDefault();
        nxlinkStdio();
    } else {
        // ONE PAIR A RUN, DATED, so the runs sort by name and a report never
        // overwrites the one before it. The console's local clock.
        char stamp[32] = "undated";
        const std::time_t now = std::time(nullptr);
        if (const std::tm* t = std::localtime(&now))
            std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", t);
        const std::string base = std::string(kHome) + "/omk-play-" + stamp;
        std::freopen((base + ".log").c_str(), "w", stdout);
        std::freopen((base + ".err").c_str(), "w", stderr);
        std::printf("run %s\n", stamp);
    }
    // a line at a time, so a crash keeps its tail
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    const bool docked = appletGetOperationMode() == AppletOperationMode_Console;
    GameArgs game;
    game.args = {"omk-play", kRoot, kTables, "--saves", kSaves,
                 "--res", docked ? "1920x1080" : "1280x720",
                 // always named: read when it is there, and where OMK's options menu writes
                 "--config", kIni};
    if (std::ifstream extra{kExtra}) {
        std::string line;
        while (std::getline(extra, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (!line.empty() && line[0] != '#') game.args.push_back(line);
        }
    }
    std::printf("switch: %s, %s, output to %s\n", appletTypeName(appletGetAppletType()),
                docked ? "docked" : "handheld", nxlink ? "nxlink" : "the SD card");
    logMemory("at main");
    for (const auto& a : game.args) std::printf("%s ", a.c_str());
    std::printf("\n");

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, kGameStackBytes);
    pthread_t thread;
    if (pthread_create(&thread, &attr, runGame, &game) == 0) {
        pthread_join(thread, nullptr);
    } else {
        std::printf("FATAL: no thread with a %zu MB stack could be made\n", kGameStackBytes >> 20);
    }
    pthread_attr_destroy(&attr);

    logMemory("at exit");
    std::fflush(stdout);
    std::fflush(stderr);
    if (nxlink) socketExit();
    romfsExit();
    return game.rc;
}
