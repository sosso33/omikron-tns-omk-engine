// SPDX-License-Identifier: GPL-3.0-or-later
// Every object of one `.SCX`, with its handle, loop count and function ids -
// the listing that finds an ENVIRONMENT animation. An object with loopCount
// -1 runs for ever, and in `Impasse.SCX` there is exactly one: `Ventilo`,
// handle 20, the fan, which AREA 222's startup script starts and no cutscene
// beat ever names.
//
//     scene_objects <gamedata> Impasse.SCX
#include "formats/scx.h"
#include "platform/datafs.h"
#include <cstdio>
#include <string>
int main(int argc, char** argv) {
    const omk::DataFs fs(argv[1]);
    const auto p = fs.resolve(std::string("SCPTDATA/") + argv[2]);
    if (!p) { std::fprintf(stderr, "no such scx\n"); return 1; }
    const auto d = omk::DataFs::readPath(*p);
    const omk::ScxScene s = omk::readScx(d);
    std::printf("%s: %zu objects\n", argv[2], s.objects.size());
    std::printf("%-4s %-22s %6s %5s %5s  functions\n", "idx", "name", "handle", "loop", "nfn");
    for (const auto& o : s.objects) {
        std::printf("%-4d %-22s %6u %5d %5d  ", o.index, o.name.c_str(),
                    o.handle >> 16, o.loop, o.nfn);
        for (const auto& f : o.functions) std::printf("%08X ", f.id);
        std::printf("\n");
    }
}
