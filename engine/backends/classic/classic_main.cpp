// SPDX-License-Identifier: GPL-3.0-or-later
// THE CLASSIC MAC OS ENTRY for a command-line program
// (`todo/classic-mac-port-1999.md` step 5). A Mac OS 9 application is started
// from the Finder with no command line and no terminal, so:
//
//   * the arguments come from `omk.args` beside the application, ONE PER
//     LINE - an HFS path has spaces in it ("Macintosh HD:omk:fr") - with
//     `omk` as argv[0];
//   * stdout and stderr go to `omk-out.txt` beside it;
//   * the last line is the exit code, and the volume is FLUSHED: OS 9 caches
//     writes, and an emulator stopped rather than shut down loses them.
//
//   * and NO C++ stream anywhere - here, in the engine (`platform/datafs.h`,
//     `readTextFile`) or in the tool: a stream initialises libstdc++'s locale,
//     and Retro68's linker lays `num_get<char>::id` over `timepunct_cache_w`,
//     so the locale's own set-up corrupts it and the first stream crashes
//     (found 2026-10-03 on Tiger; it showed only once the program was large).
//
// The program's own `main` is compiled under another name (-Dmain=...), as
// the Vita build does with `play.cpp`, and `OMK_CLASSIC_ENTRY` names it:
// `omk_tool_main` for a tool, `omk_play_main` for the viewer.
#include <Files.h>

#include "platform/profile.h"
#if OMK_PROFILE
#  include "heapcount.h"
#endif

#include <cstdio>
#include <exception>
#include <string>
#include <vector>

#if !defined(OMK_CLASSIC_ENTRY)
#  define OMK_CLASSIC_ENTRY omk_tool_main
#endif
int OMK_CLASSIC_ENTRY(int argc, char** argv);

int main() {
    std::freopen("omk-out.txt", "w", stdout);
    std::freopen("omk-out.txt", "a", stderr);
    // LINE-buffered: a crash otherwise takes the last buffer of output with
    // it, and that is the part that says where it happened
    std::setvbuf(stdout, nullptr, _IOLBF, 4096);
    std::setvbuf(stderr, nullptr, _IOLBF, 4096);
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
    std::printf("exit %d\n", rc);
    std::fflush(stdout);
    std::fflush(stderr);
    FlushVol(nullptr, 0);
    return rc;
}
