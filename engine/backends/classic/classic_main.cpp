// SPDX-License-Identifier: GPL-3.0-or-later
// THE CLASSIC MAC OS ENTRY for a command-line program
// (`todo/classic-mac-port-1999.md` step 5). A Mac OS 9 application is started
// from the Finder with no command line and no terminal, so:
//
//   * the arguments come from `omk.args` beside the application, ONE PER
//     LINE - an HFS path has spaces in it ("Macintosh HD:omk:fr") - with
//     `omk` as argv[0];
//   * stdout and stderr go to `omk-out.txt` beside it, EVERY LINE STAMPED
//     with the seconds since `main` began (`[12.34s] `) - the reader's iMac
//     G3 took more than a minute to launch (2026-10-30) and its log could not
//     say where; the first line also says how long the system took to load
//     the application before `main` (its Process Manager launch date);
//   * the last line is the exit code, and the volume is FLUSHED: OS 9 caches
//     writes, and an emulator stopped rather than shut down loses them.
//
//   * and NO C++ stream anywhere - here, in the engine (`platform/datafs.h`,
//     `readTextFile`) or in the tool: a stream initialises libstdc++'s locale,
//     and Retro68's linker lays `num_get<char>::id` over `timepunct_cache_w`,
//     so the locale's own set-up corrupts it and the first stream crashes
//     (seen on Tiger; it shows only once the program is large).
//
// The program's own `main` is compiled under another name (-Dmain=...), as
// the Vita build does with `play.cpp`, and `OMK_CLASSIC_ENTRY` names it:
// `omk_tool_main` for a tool, `omk_play_main` for the viewer.
#include <Files.h>
#include <Processes.h>
#include <Timer.h>

#include "platform/profile.h"
#if OMK_PROFILE
#  include "heapcount.h"
#endif
#if defined(OMK_LAUNCH_GUARD)
#  include "launchguard.h"   // the viewer: a step that froze OS 9 is not taken twice
#endif

#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

#if !defined(OMK_CLASSIC_ENTRY)
#  define OMK_CLASSIC_ENTRY omk_tool_main
#endif
int OMK_CLASSIC_ENTRY(int argc, char** argv);

namespace {

std::uint64_t microsNow() {
    UnsignedWide us;
    Microseconds(&us);
    return (static_cast<std::uint64_t>(us.hi) << 32) | us.lo;
}

// THE STAMPED LOG: stdout and stderr are one `funopen` stream whose writer
// puts `[seconds since main] ` before each line and hands it to the file,
// unbuffered there (the stream itself is line-buffered, below).
struct StampedLog {
    std::FILE* file = nullptr;
    std::uint64_t t0 = 0;
    bool atLineStart = true;
};
StampedLog g_log;

int stampedWrite(void* cookie, const char* buf, int n) {
    auto* L = static_cast<StampedLog*>(cookie);
    for (int i = 0; i < n;) {
        if (L->atLineStart) {
            char stamp[24];
            const double s = static_cast<double>(microsNow() - L->t0) / 1e6;
            const int k = std::snprintf(stamp, sizeof stamp, "[%.2fs] ", s);
            std::fwrite(stamp, 1, static_cast<std::size_t>(k), L->file);
            L->atLineStart = false;
        }
        int j = i;
        while (j < n && buf[j] != '\n') ++j;
        const bool eol = j < n;
        std::fwrite(buf + i, 1, static_cast<std::size_t>(j - i + (eol ? 1 : 0)), L->file);
        if (eol) L->atLineStart = true;
        i = j + (eol ? 1 : 0);
    }
    return n;
}

}  // namespace

int main() {
    g_log.t0 = microsNow();
    // the time the system took between launching the application and its
    // `main`: the code fragment read off the disk and prepared (CFM)
    long loadTicks = -1;
    {
        ProcessSerialNumber psn = {0, kCurrentProcess};
        ProcessInfoRec info = {};
        info.processInfoLength = sizeof info;
        if (GetProcessInformation(&psn, &info) == noErr)
            loadTicks = static_cast<long>(TickCount() - info.processLaunchDate);
    }
#if defined(OMK_LAUNCH_GUARD)
    classic_guard_launch();   // before the log is opened: the last run's is kept
#endif
    g_log.file = std::fopen("omk-out.txt", "w");
    if (g_log.file) {
        std::setvbuf(g_log.file, nullptr, _IONBF, 0);
        if (std::FILE* f = funopen(&g_log, nullptr, stampedWrite, nullptr, nullptr)) stdout = stderr = f;
    }
    // LINE-buffered: a crash otherwise takes the last buffer of output with
    // it, and that is the part that says where it happened
    std::setvbuf(stdout, nullptr, _IOLBF, 4096);
    if (stderr != stdout) std::setvbuf(stderr, nullptr, _IOLBF, 4096);
    if (loadTicks >= 0)
        std::printf("launch: the system loaded the application in %.1f s before its main "
                    "(the Process Manager's launch date; every line is stamped from here)\n",
                    static_cast<double>(loadTicks) / 60.0);
    // ...and std::terminate (an exception through a `noexcept`, say) ends the
    // same silent way; say that it happened, and what was in flight
    std::set_terminate([] {
        std::printf("std::terminate");
        if (const std::exception_ptr ep = std::current_exception()) {
            try { std::rethrow_exception(ep); }
            catch (const std::exception& e) { std::printf(" - %s", e.what()); }
            catch (...) { std::printf(" - not a std::exception"); }
        }
        std::printf("\n");
        std::fflush(stdout);
        FlushVol(nullptr, 0);
        // a FAULT, not abort(): on Mac OS X the crash reporter then writes the
        // whole backtrace (~/Library/Logs/CrashReporter/<app>.crash.log),
        // which is the only way to see where the exception was in flight
        *static_cast<volatile int*>(nullptr) = 0;
        std::abort();
    });
#if OMK_PROFILE
    classic_heap_launch();
#endif
    std::vector<std::string> args{"omk"};
    if (std::FILE* f = std::fopen("omk.args", "r")) {
        char line[1024];
        while (std::fgets(line, sizeof line, f)) {
            std::string s(line);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            if (!s.empty()) args.push_back(s);
        }
        std::fclose(f);
    }
#if defined(OMK_LAUNCH_GUARD)
    classic_guard_apply(args);
    // the setup before the window (the session, area 118, the options) reads
    // the data with no event taken: a stop there is named too
    classic_guard_arm("the launch - the session and area 118, before the window");
    FlushVol(nullptr, 0);
#endif
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    // An exception that escapes would reach `std::terminate`, whose `abort`
    // on Retro68 returns to the shell with status 0 and nothing flushed - a
    // run that looks clean and printed nothing. Say what it was instead.
    int rc = 1;
    try {
        rc = OMK_CLASSIC_ENTRY(static_cast<int>(args.size()), argv.data());
    } catch (const std::exception& e) {
        std::printf("uncaught exception: %s\n", e.what());
    } catch (...) {
        std::printf("uncaught exception (not a std::exception)\n");
    }
#if OMK_PROFILE
    classic_heap_report();
#endif
#if defined(OMK_LAUNCH_GUARD)
    classic_guard_clear();
#endif
    std::printf("exit %d\n", rc);
    std::fflush(stdout);
    std::fflush(stderr);
    FlushVol(nullptr, 0);
    return rc;
}
