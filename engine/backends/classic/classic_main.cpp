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
// The program's own `main` is compiled as `omk_tool_main` (-Dmain=...), as
// the Vita build does with `play.cpp`.
#include <Files.h>

#include <cstdio>
#include <string>
#include <vector>

int omk_tool_main(int argc, char** argv);

int main() {
    std::freopen("omk-out.txt", "w", stdout);
    std::freopen("omk-out.txt", "a", stderr);
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
    const int rc = omk_tool_main(static_cast<int>(args.size()), argv.data());
    std::printf("exit %d\n", rc);
    std::fflush(stdout);
    std::fflush(stderr);
    FlushVol(nullptr, 0);
    return rc;
}
