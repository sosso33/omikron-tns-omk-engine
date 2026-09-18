// SPDX-License-Identifier: GPL-3.0-or-later
// THE GAME'S ENTRY POINT ON THE PS VITA - `todo/vita-port.md` F4.
//
// A Vita title has no command line, and `play.cpp`'s `main` is written for
// one. Rather than thread `#if __vita__` through its argument parsing, the
// Vita build compiles `play.cpp` with `-Dmain=omk_play_main` and this file is
// the real `main`: it sets the platform up and hands `omk_play_main` the
// arguments a desktop user would have typed.
//
//   ux0:data/omk/gamedata/      the game's data tree, copied from the disc
//                               (MESHES/, IAM/, MORPH/, ... directly inside)
//   app0:tables/                the tables compiled into the exe, lifted to
//                               JSON - packaged in the VPK, read-only
//   ux0:data/omk/saves/GAMES    where saves go (the port never writes into
//                               the data tree - CLAUDE.md §1)
//   ux0:data/omk/omk.ini        the game's own config file, when present
//   ux0:data/omk/args.txt       EXTRA arguments, one per line, when present -
//                               how a device run is given `--area`, `--save`,
//                               `--fps` or `--nofmv` without a rebuild
//
// The frame is 640x480, the resolution the interface is authored at; the
// GLES backend scales it into the 960x544 screen at its own aspect.
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

// The newlib heap: the M1 measured a 137 MB live heap on a street
// (`handoff-vita.md` §1), and vitaGL keeps its own copies on top. 300 MB is
// what a homebrew title can ask for without the extended-memory mode.
extern "C" {
int _newlib_heap_size_user = 300 * 1024 * 1024;
// `play.cpp`'s `main` is one function of ~18800 lines with ~470 locals in a
// single frame; the Vita's default main-thread stack is 256 KiB. 8 MiB leaves
// room for that frame and the recursion below it.
unsigned int sceUserMainThreadStackSize = 8 * 1024 * 1024;
}

int omk_play_main(int argc, char** argv);
extern "C" void omk_vita_redirect(std::FILE* out, std::FILE* err);   // printf_c99.cpp

namespace {
constexpr const char* kRoot   = "ux0:data/omk/gamedata";
constexpr const char* kTables = "app0:tables";
constexpr const char* kSaves  = "ux0:data/omk/saves/GAMES";
constexpr const char* kIni    = "ux0:data/omk/omk.ini";
constexpr const char* kExtra  = "ux0:data/omk/args.txt";

bool exists(const char* path) {
    SceIoStat st;
    return sceIoGetstat(path, &st) >= 0;
}
}  // namespace

int main(int, char**) {
    // The CPU at its best case, as the bench asks for it: 444 MHz is what the
    // handoff's decision assumed, and the default is 333.
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    sceIoMkdir("ux0:data/omk", 0777);
    sceIoMkdir("ux0:data/omk/saves", 0777);

    // stdout goes nowhere on a device; the log is where a play report starts.
    // NOT freopen: on the Vita it opens the file and newlib keeps writing the
    // standard streams to the TTY regardless (`printf_c99.cpp`), so the
    // redirect is done where every printf already passes. Unbuffered, so a
    // crash cannot take the last lines with it.
    omk_vita_redirect(std::fopen("ux0:data/omk/omk-play.log", "w"),
                      std::fopen("ux0:data/omk/omk-play.err", "w"));

    std::vector<std::string> args = {"omk-play", kRoot, kTables,
                                     "--saves", kSaves, "--res", "640x480"};
    if (exists(kIni)) { args.push_back("--config"); args.push_back(kIni); }
    if (std::ifstream extra{kExtra}) {
        std::string line;
        while (std::getline(extra, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (!line.empty() && line[0] != '#') args.push_back(line);
        }
    }
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    for (const auto& a : args) std::printf("%s ", a.c_str());
    std::printf("\n");
    std::fflush(stdout);

    const int rc = omk_play_main(static_cast<int>(args.size()), argv.data());
    std::fflush(stdout);
    std::fflush(stderr);
    sceKernelExitProcess(rc);
    return rc;
}
