// SPDX-License-Identifier: GPL-3.0-or-later
// THE ENTRY POINT ON THE NINTENDO 3DS - `todo/3ds-port.md` steps 1 and 2.
//
// A 3DS program has no command line, and the programs it runs are written for
// one. As on the Vita and the classic Mac, the program's own `main` is
// compiled under another name (`-Dmain=...`, `OMK_3DS_ENTRY` names it:
// `omk_tool_main` for `omk_boot`, `omk_play_main` for the game) and this file
// is the real `main`: it sets the console up and hands it the arguments a
// desktop user would have typed.
//
//   sdmc:/omk/gamedata/      the game's data tree, copied from the disc
//                            (MESHES/, IAM/, MORPH/, ... directly inside) -
//                            IN BINARY MODE: the Vita's IAM copied by an FTP
//                            client in ASCII mode was a black screen
//   romfs:                   the tables (`tables/*.json`), in the .3dsx - named
//                            WITHOUT the slash, because the engine joins
//                            "<tables>/vm_opcodes.json" and libctru's romfs
//                            refuses the "romfs://..." a trailing slash makes
//                            ("no VM opcode table", Azahar, 2026-10-06)
//   sdmc:/omk/saves/GAMES    where saves go (never into the data tree -
//                            CLAUDE.md 1)
//   sdmc:/omk/omk.ini        the game's own config file, when present
//   sdmc:/omk/args.txt       EXTRA arguments, one per line, when present
//                            (`#` starts a comment) - how a run is given
//                            `--area`, `--save` or `--nofmv` without a rebuild
//   sdmc:/omk/<program>-YYYYMMDD-HHMMSS.log / .err   each run's log, dated,
//                            so the runs sort by name and none overwrites
//                            the one before (the Vita's rule)
//
// Every line written to stdout or stderr goes to the file and to the LOG
// RING (`n3dshost.h`) the instrument panel shows (`tee` below). `omk_boot`
// also shows it live on the bottom screen through libctru's console;
// `omk_play`'s bottom screen is the panel (step 2b), and the console comes
// back only when the game has returned, with the log's last lines.
#include "n3dshost.h"

#include <3ds.h>
#include <sys/iosupport.h>
#include <sys/stat.h>

#include <malloc.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <exception>
#include <new>
#include <string>
#include <vector>

#if !defined(OMK_3DS_ENTRY)
#  define OMK_3DS_ENTRY omk_tool_main
#endif
int OMK_3DS_ENTRY(int argc, char** argv);

// The main thread's stack. libctru's default is 32 KB, which the engine's
// load paths would overrun at once; `PlayState` itself is on the heap
// (`play.cpp`), so 1 MB is a first figure, to be measured (step 4) rather
// than trusted.
extern "C" {
u32 __stacksize__ = 1024 * 1024;
// what libctru's start-up gave the two heaps (the newlib heap `new` draws
// from, and the linear heap the GPU and the DSP read)
extern u32 __ctru_heap_size;
extern u32 __ctru_linear_heap_size;
}

namespace {

using omk::n3ds::kHome;
using omk::n3ds::kRoot;
using omk::n3ds::kTables;
constexpr const char* kSaves  = "sdmc:/omk/saves/GAMES";
constexpr const char* kIni    = "sdmc:/omk/omk.ini";
constexpr const char* kExtra  = "sdmc:/omk/args.txt";

bool exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

// ---- the tee: stdout and stderr to their files AND to the console ----------
// libctru's console is a newlib device (`devoptab_list[STD_OUT]`), installed
// by `consoleInit`; it is copied here with its write replaced by one that
// writes the file first and then hands the same bytes to the console. The
// file is on another device (`sdmc:`), so the file's own write never comes
// back through this one. Flushed on every write: a crash otherwise takes the
// last lines with it, and they are the ones that say where it happened.
std::FILE* g_log = nullptr;
std::FILE* g_err = nullptr;
const devoptab_t* g_console = nullptr;
devoptab_t g_teeOut, g_teeErr;

bool g_showConsole = false;      // libctru's console is on the bottom screen

ssize_t teeTo(std::FILE* f, struct _reent* r, void* fd, const char* p, size_t n) {
    if (f) {
        std::fwrite(p, 1, n, f);
        std::fflush(f);
    }
    omk::n3ds::logAppend(p, n);
    if (g_showConsole && g_console && g_console->write_r) g_console->write_r(r, fd, p, n);
    return static_cast<ssize_t>(n);
}
ssize_t teeOut(struct _reent* r, void* fd, const char* p, size_t n) { return teeTo(g_log, r, fd, p, n); }
ssize_t teeErr(struct _reent* r, void* fd, const char* p, size_t n) { return teeTo(g_err, r, fd, p, n); }

// The tee over whatever device stdout is now - the console's once
// `consoleInit` has run, libctru's null device before. Called again after a
// late `consoleInit`, which installs its own device over this one.
void hookStdout() {
    g_console = devoptab_list[STD_OUT];
    if (!g_console || g_console == &g_teeOut) return;
    g_teeOut = *g_console;
    g_teeOut.write_r = teeOut;
    g_teeErr = *g_console;
    g_teeErr.write_r = teeErr;
    devoptab_list[STD_OUT] = &g_teeOut;
    devoptab_list[STD_ERR] = &g_teeErr;
}

void installTee(const std::string& logPath, const std::string& errPath) {
    g_log = std::fopen(logPath.c_str(), "w");
    g_err = std::fopen(errPath.c_str(), "w");
    hookStdout();
    // line-buffered: each line reaches the tee (and the card) as it is ended
    std::setvbuf(stdout, nullptr, _IOLBF, 1024);
    std::setvbuf(stderr, nullptr, _IOLBF, 1024);
}

std::string stamp() {
    char s[32] = "undated";
    const std::time_t t = std::time(nullptr);
    if (const std::tm* lt = std::localtime(&t))
        std::strftime(s, sizeof s, "%Y%m%d-%H%M%S", lt);
    return s;
}

// What the console has to say about itself, first - the line a report starts
// from: which model, at which clock, with how much memory.
void reportConsole() {
    bool n3ds = false;
    APT_CheckNew3DS(&n3ds);
    // NOT `osGetMemRegionFree`: libctru's start-up takes the whole region
    // for its two heaps, and the call read 4 GB in Azahar - a wrapped value
    std::printf("console: %s 3DS, the CPU %s; application memory %u KB: the heap %u KB "
                "(%u KB in use at main), the linear heap %u KB (%u KB free)\n",
                n3ds ? "NEW" : "OLD", n3ds ? "at 804 MHz with the L2 (osSetSpeedupEnable)" : "at 268 MHz",
                static_cast<unsigned>(osGetMemRegionSize(MEMREGION_APPLICATION) / 1024),
                static_cast<unsigned>(__ctru_heap_size / 1024),
                static_cast<unsigned>(mallinfo().uordblks / 1024),
                static_cast<unsigned>(__ctru_linear_heap_size / 1024),
                static_cast<unsigned>(linearSpaceFree() / 1024));
}

// The program has returned; keep its last lines on the bottom screen until
// the player has read them (START), or the system asks the program to close.
void waitForStart(int rc) {
#if !defined(OMK_3DS_TOOL)
    // the bottom screen was the panel: the console back, with the log's tail
    consoleInit(GFX_BOTTOM, nullptr);
    hookStdout();
    g_showConsole = true;
    for (const std::string& l : omk::n3ds::logTail(24)) std::printf("%s\n", l.c_str());
#endif
    std::printf("\nexit %d - press START to leave\n", rc);
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gspWaitForVBlank();
    }
}

}  // namespace

int main(int, char**) {
    gfxInitDefault();
#if defined(OMK_3DS_TOOL)
    consoleInit(GFX_BOTTOM, nullptr);
    g_showConsole = true;
#endif
    osSetSpeedupEnable(true);           // the New 3DS's 804 MHz and L2; a no-op on the old one
    const Result romfs = romfsInit();
    mkdir(kHome, 0777);
    mkdir("sdmc:/omk/saves", 0777);

#if defined(OMK_3DS_TOOL)
    const char* program = "omk-boot";
#else
    const char* program = "omk-play";
#endif
    const std::string when = stamp();
    const std::string base = std::string(kHome) + "/" + program + "-" + when;
    installTee(base + ".log", base + ".err");

#if defined(OMK_3DS_TOOL)
    std::vector<std::string> args = {"omk", kRoot, "--tables", kTables};
#else
    // THE FRAME IS 16:9 BY DEFAULT - the reader's decision of 2026-10-06
    // ("use 16:9 resolution by default on 3ds since it is already
    // supported"). 800x448 rather than 800x450: the PICA tiles its sides
    // exactly (multiples of 8), and 800x450 asked a 135 MB allocation of the
    // 32-bit build at the first world frame (Azahar, unexplained - the
    // desktop draws it; `todo/3ds-port.md`). Halved it is 400x224 on the
    // 400x240 top screen, 93% of it where 4:3 used 80%. The engine scales the 640x480 interface to the
    // frame (`I2D_ScaleX/Y`), as the Vita's 16:9 does, and keeps the 3D's
    // horizontal fov. The frontend fits whatever the frame is to the top
    // screen. Added only when args.txt names no `--res` of its own: the
    // viewer takes the FIRST `--res` it is given (measured in Azahar,
    // 2026-10-06 - an args.txt `--res 800x448` after this default was
    // ignored), so a default put first would always win.
    std::vector<std::string> args = {"omk-play", kRoot, kTables, "--saves", kSaves};
    if (exists(kIni)) { args.push_back("--config"); args.push_back(kIni); }
#endif
    // args.txt: one argument a line as on the Vita, and a line may also hold
    // several separated by spaces - `--profile sdmc:/omk/run.prof` is how a
    // person types it (the reader's third run put `--profile` alone, which
    // then took the next argument, the default `--res`, for its path). `#`
    // starts a comment.
    if (std::FILE* f = std::fopen(kExtra, "r")) {
        char line[1024];
        while (std::fgets(line, sizeof line, f)) {
            std::string s(line);
            if (const auto h = s.find('#'); h != std::string::npos) s.erase(h);
            std::size_t i = 0;
            while (i < s.size()) {
                while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
                std::size_t j = i;
                while (j < s.size() && !std::isspace(static_cast<unsigned char>(s[j]))) ++j;
                if (j > i) args.push_back(s.substr(i, j - i));
                i = j;
            }
        }
        std::fclose(f);
    }
    // `--profile` with no path after it: the card's default capture, rather
    // than the next argument
    for (std::size_t i = 0; i < args.size(); ++i)
        if (args[i] == "--profile" && (i + 1 >= args.size() || args[i + 1].rfind("--", 0) == 0))
            args.insert(args.begin() + static_cast<long>(i) + 1, std::string(kHome) + "/run.prof");
#if !defined(OMK_3DS_TOOL)
    if (std::find(args.begin(), args.end(), std::string("--res")) == args.end()) {
        args.push_back("--res");
        args.push_back("800x448");                      // 16:9, the 3DS default (above)
    }
#endif
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    // the profiler capture, for the panel's buttons (`n3dspanel.h`)
    for (std::size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i] == "--profile") omk::n3ds::setCapturePath(args[i + 1]);

    std::printf("run %s\n", when.c_str());
    for (const auto& a : args) std::printf("%s ", a.c_str());
    std::printf("\n");
    reportConsole();
    if (R_FAILED(romfs))
        std::printf("romfs: NOT mounted (0x%08lX) - the tables cannot be read\n",
                    static_cast<unsigned long>(romfs));
    if (!exists(kRoot))
        std::printf("data: %s is MISSING - copy the game's data tree there\n", kRoot);

    // An exception that escapes would reach `std::terminate` and end the run
    // with nothing said; this log is the only report a console run leaves.
    int rc = 1;
    try {
        rc = OMK_3DS_ENTRY(static_cast<int>(args.size()), argv.data());
    } catch (const std::bad_alloc&) {
        std::printf("FATAL: out of memory (std::bad_alloc) - %u KB in use of the %u KB heap\n",
                    static_cast<unsigned>(mallinfo().uordblks / 1024),
                    static_cast<unsigned>(__ctru_heap_size / 1024));
    } catch (const std::exception& e) {
        std::printf("uncaught exception: %s\n", e.what());
    } catch (...) {
        std::printf("uncaught exception (not a std::exception)\n");
    }
    std::fflush(stdout);
    std::fflush(stderr);
    waitForStart(rc);
    // the tee forgets a file before it is closed, so nothing written after
    // (an exit handler's line) reaches a closed FILE
    if (std::FILE* f = g_log) { g_log = nullptr; std::fclose(f); }
    if (std::FILE* f = g_err) { g_err = nullptr; std::fclose(f); }
    romfsExit();
    gfxExit();
    return rc;
}
