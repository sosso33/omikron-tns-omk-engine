// SPDX-License-Identifier: GPL-3.0-or-later
// The launch guard - see launchguard.h.
#include "launchguard.h"

#include <Files.h>
#include <Gestalt.h>
#include <Timer.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr const char* kGuard = "omk.guard";     // the step in flight
constexpr const char* kSafe = "omk.safe";       // the steps skipped, one a line
constexpr const char* kLog = "omk-out.txt";
constexpr const char* kFrozenLog = "omk-freeze.txt";
// how long a run must go on past its last checkpoint before the step is
// taken to have worked: the first frames of the menu or a film come well
// inside it on the iBook (one to a few seconds)
constexpr std::uint64_t kClearAfterUs = 15000000;

bool onMacOS9() {
    static int v = -1;
    if (v < 0) {
        long sv = 0;
        v = Gestalt(gestaltSystemVersion, &sv) == noErr && sv < 0x1000 ? 1 : 0;
    }
    return v == 1;
}

std::uint64_t microseconds() {
    UnsignedWide us;
    Microseconds(&us);
    return (static_cast<std::uint64_t>(us.hi) << 32) | us.lo;
}

// A file beside the application (the default directory at launch), by the
// File Manager: Retro68's C library has no remove() or rename().
bool specFor(const char* name, FSSpec& out) {
    Str255 p;
    const std::size_t n = std::strlen(name);
    p[0] = static_cast<unsigned char>(n);
    std::memcpy(p + 1, name, n);
    return FSMakeFSSpec(0, 0, p, &out) == noErr;
}
void removeFile(const char* name) {
    FSSpec s;
    if (specFor(name, s)) FSpDelete(&s);
}

// Which step a checkpoint's words name. Their text is `carbonfront.cpp`'s.
const char* skipFor(const char* stage) {
    if (std::strstr(stage, "OpenGL") || std::strstr(stage, "agl") || std::strstr(stage, "upload probe"))
        return "opengl";
    if (std::strstr(stage, "QuickTime") || std::strstr(stage, "NewMovieFromFile")) return "quicktime";
    if (std::strstr(stage, "SndNewChannel")) return "sound";
    return nullptr;
}

std::string g_stopped;                  // the stage the last run stopped in
bool g_armed = false;
std::uint64_t g_armedAt = 0;

bool safeHas(const char* what) {
    std::FILE* f = std::fopen(kSafe, "r");
    if (!f) return false;
    char line[256];
    bool found = false;
    while (!found && std::fgets(line, sizeof line, f))
        found = std::strncmp(line, what, std::strlen(what)) == 0;
    std::fclose(f);
    return found;
}

}  // namespace

void classic_guard_launch() {
    std::FILE* g = std::fopen(kGuard, "r");
    if (!g) return;
    char stage[256] = {};
    if (!std::fgets(stage, sizeof stage, g)) stage[0] = 0;
    std::fclose(g);
    for (std::size_t n = std::strlen(stage); n && (stage[n - 1] == '\n' || stage[n - 1] == '\r');) stage[--n] = 0;
    g_stopped = stage[0] ? stage : "an unnamed step";
    // the stopped run's log, kept before this run's replaces it
    removeFile(kFrozenLog);
    FSSpec s;
    if (specFor(kLog, s)) {
        Str255 to;
        to[0] = static_cast<unsigned char>(std::strlen(kFrozenLog));
        std::memcpy(to + 1, kFrozenLog, to[0]);
        FSpRename(&s, to);
    }
    if (const char* skip = skipFor(g_stopped.c_str()); skip && !safeHas(skip)) {
        if (std::FILE* f = std::fopen(kSafe, "a")) {
            std::fprintf(f, "%s - the run stopped at: %s\n", skip, g_stopped.c_str());
            std::fclose(f);
        }
    }
    removeFile(kGuard);
    FlushVol(nullptr, 0);
}

void classic_guard_apply(std::vector<std::string>& args) {
    if (!g_stopped.empty())
        std::printf("guard: THE LAST RUN STOPPED at the checkpoint \"%s\" - its log is kept as %s%s\n",
                    g_stopped.c_str(), kFrozenLog,
                    skipFor(g_stopped.c_str()) ? "; that step is skipped from now on (omk.safe)"
                                               : "; no step to skip for it");
    bool any = false;
    for (const char* w : {"opengl", "quicktime", "sound"}) {
        if (!safeHas(w)) continue;
        any = true;
        std::printf("guard: omk.safe skips %s - %s\n", w,
                    std::strcmp(w, "opengl") == 0 ? "the software renderer (--software)"
                    : std::strcmp(w, "quicktime") == 0 ? "the films through the software decoder"
                                                       : "no audio device");
        if (std::strcmp(w, "opengl") == 0) args.push_back("--software");
    }
    if (any) std::printf("guard: delete omk.safe beside the application to try every step again\n");
    std::fflush(stdout);
}

bool classic_guard_skips(const char* what) { return safeHas(what); }

void classic_guard_arm(const char* stage) {
    if (!onMacOS9()) return;
    if (std::FILE* f = std::fopen(kGuard, "w")) {
        std::fprintf(f, "%s\n", stage);
        std::fclose(f);
    }
    g_armed = true;
    g_armedAt = microseconds();
    // the caller flushes the volume with its log line
}

void classic_guard_tick() {
    if (g_armed && microseconds() - g_armedAt > kClearAfterUs) classic_guard_clear();
}

void classic_guard_clear() {
    if (!g_armed) return;
    g_armed = false;
    removeFile(kGuard);
    FlushVol(nullptr, 0);
}
