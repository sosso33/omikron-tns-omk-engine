// SPDX-License-Identifier: GPL-3.0-or-later
//
// List a scene's .3DP paths: index, name, duration, first and last key - so
// a walk can be stood where a scripted object ENDS UP rather than where it
// was authored.
//
//     scx_paths <file.SCX>
#include "script/program.h"
#include "platform/datafs.h"

#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: scx_paths <file.SCX>\n"); return 2; }
    const auto d = omk::DataFs::readPath(argv[1]);
    omk::ScxRuntime rt(d);
    if (!rt.valid()) { std::fprintf(stderr, "not a scene\n"); return 1; }
    int i = 0;
    for (const auto& p : rt.paths()) {
        if (p.keys.empty()) { ++i; continue; }
        const auto& a = p.keys.front(); const auto& b = p.keys.back();
        std::printf("%3d  %-20s dur %6.1f  keys %3zu  first %8.1f %8.1f %8.1f  last %8.1f %8.1f %8.1f\n",
                    i++, p.name.c_str(), static_cast<double>(p.duration), p.keys.size(),
                    a.pos[0], a.pos[1], a.pos[2], b.pos[0], b.pos[1], b.pos[2]);
    }
    return 0;
}
