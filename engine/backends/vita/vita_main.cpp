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
//   ux0:data/omk/omk-play-YYYYMMDD-HHMMSS.log / .err   each run's log, dated
//   ux0:data/omk/omk.ini        the game's own config file, when present
//   ux0:data/omk/args.txt       EXTRA arguments, one per line, when present -
//                               how a device run is given `--area`, `--save`,
//                               `--fps` or `--nofmv` without a rebuild
//
// The frame is the Vita's own 960x544, drawn 1:1 (the reader's choice,
// 2026-09-18). The 3D keeps its proportions - the camera's field of view is
// horizontal and the vertical follows the frame - but the INTERFACE is
// authored at 640x480 and the engine's own I2D scaling (`v * w / 640`,
// `v * h / 480`) stretches it to 16:9. `--res 640x480` in args.txt gives the
// 4:3 frame back, pillarboxed into the screen.
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>
#include <psp2/rtc.h>

#include <cstdio>
#include <fstream>
#include <malloc.h>
#include <new>
#include <string>
#include <vector>

// The newlib heap. The SDK's start-up reserves this whole block with
// `sceKernelAllocMemBlock` BEFORE `main` - and if the system refuses it, there
// is no heap at all: every `new` throws, starting with the first static
// initialiser. 300 MB did exactly that on a real Vita (2026-09-18: a crash
// dump, `std::map` built before `main` -> `operator new` -> `bad_alloc` ->
// `abort`), while Vita3K, which hands out more, ran it. 192 MB is what the
// bench runs with; the M1 measured a 137 MB live heap on a busy street with
// 64-bit pointers, and vitaGL takes its own memory OUTSIDE this block.
extern "C" {
int _newlib_heap_size_user = 192 * 1024 * 1024;
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

// Copy every file under `from` that `to` has not got; -> how many were copied.
// `app0:` is read-only and vitaGL writes its cache, so the cache cannot simply
// be pointed at the package.
int seedTree(const std::string& from, const std::string& to) {
    const SceUID d = sceIoDopen(from.c_str());
    if (d < 0) return 0;
    sceIoMkdir(to.c_str(), 0777);
    int copied = 0;
    SceIoDirent e{};
    while (sceIoDread(d, &e) > 0) {
        const std::string src = from + "/" + e.d_name, dst = to + "/" + e.d_name;
        if (SCE_S_ISDIR(e.d_stat.st_mode)) { copied += seedTree(src, dst); continue; }
        if (exists(dst.c_str())) continue;
        std::ifstream in(src, std::ios::binary);
        std::ofstream out(dst, std::ios::binary);
        if (in && out) { out << in.rdbuf(); ++copied; }
    }
    sceIoDclose(d);
    return copied;
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
    //
    // ONE PAIR A RUN, DATED: `omk-play-YYYYMMDD-HHMMSS.log` / `.err`, so the
    // runs sort by name and a report never overwrites the one before it
    // (the reader's request, 2026-09-18). The console's local clock.
    char stamp[32] = "undated";
    {
        SceDateTime t{};
        if (sceRtcGetCurrentClockLocalTime(&t) >= 0)
            std::snprintf(stamp, sizeof stamp, "%04u%02u%02u-%02u%02u%02u",
                          static_cast<unsigned>(t.year), static_cast<unsigned>(t.month),
                          static_cast<unsigned>(t.day), static_cast<unsigned>(t.hour),
                          static_cast<unsigned>(t.minute), static_cast<unsigned>(t.second));
    }
    const std::string logPath = std::string("ux0:data/omk/omk-play-") + stamp + ".log";
    const std::string errPath = std::string("ux0:data/omk/omk-play-") + stamp + ".err";
    omk_vita_redirect(std::fopen(logPath.c_str(), "w"), std::fopen(errPath.c_str(), "w"));

    std::vector<std::string> args = {"omk-play", kRoot, kTables,
                                     "--saves", kSaves, "--res", "960x544"};
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
    std::printf("run %s\n", stamp);
    for (const auto& a : args) std::printf("%s ", a.c_str());
    std::printf("\n");
    std::fflush(stdout);

    // THE SHADERS, PRECOMPILED (2026-09-21). vitaGL compiles GLSL at run time
    // with Sony's `libshacccg.suprx`, which only comes extracted from a
    // console. Our vitaGL is built with its shader CACHE and one patch
    // (`scripts/vita-vitagl-patch.py`): a shader found in the cache as
    // `<hash of its source>.gxp` needs no compiler. The VPK carries the cache
    // (`app0:shader_cache`, made by `scripts/vita-shader-cache.sh` in Vita3K)
    // and this copies what is missing into vitaGL's own folder, so a player
    // never needs the compiler; a developer who changed a shader does, once -
    // the miss is compiled and cached as before.
    const int seeded = seedTree("app0:shader_cache", "ux0:data/shader_cache");
    const bool compiler = exists("ur0:data/libshacccg.suprx") ||
                          exists("ur0:data/external/libshacccg.suprx");
    std::printf("shaders: %d precompiled file(s) copied into ux0:data/shader_cache; "
                "the runtime compiler (libshacccg.suprx) is %s\n",
                seeded, compiler ? "present" : "ABSENT - a shader that is not in the cache "
                                               "will fail to link, and the log will say so");

    // what the heap actually holds, first - the line a memory report starts
    // from (`arena` is the newlib block reached so far, not its capacity)
    std::printf("heap: %d MB reserved for newlib, %d bytes in use at main\n",
                _newlib_heap_size_user / (1024 * 1024), mallinfo().uordblks);
    int rc = 1;
    try {
        rc = omk_play_main(static_cast<int>(args.size()), argv.data());
    } catch (const std::bad_alloc&) {
        // out of memory is SAID, not a silent abort: this log is the only
        // report a console run leaves
        std::printf("FATAL: out of memory (std::bad_alloc) - %d bytes in use of the %d MB heap\n",
                    mallinfo().uordblks, _newlib_heap_size_user / (1024 * 1024));
        std::fprintf(stderr, "FATAL: out of memory\n");
    }
    std::fflush(stdout);
    std::fflush(stderr);
    sceKernelExitProcess(rc);
    return rc;
}


// A REFUSED ALLOCATION SAYS ITS SIZE. The city's load died of `bad_alloc` with
// 86 MB in use of 192 (console log 2026-09-18) - so one request, not the sum,
// and nothing said which. Global `operator new` on the Vita build only.
#include <new>
void* operator new(std::size_t n) {
    if (void* p = std::malloc(n ? n : 1)) return p;
    const struct mallinfo mi = mallinfo();
    std::printf("new: %u bytes REFUSED - %d in use, %d free in the arena of %d\n",
                static_cast<unsigned>(n), mi.uordblks, mi.fordblks, mi.arena);
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
