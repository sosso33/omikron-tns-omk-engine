// SPDX-License-Identifier: GPL-3.0-or-later
// THE PITCH AND ROLL a scene program authors on a body - todo/drift-audit.md
// M7, counted over the whole shipped corpus rather than remembered.
//
//     scx_euler_probe <gamedata>
//
// Both body-animation calls carry an Euler that the engine writes to the
// node every tick (`Actor_SetEuler(node, p4, p5, p6)`): 0x02000004
// `Script_SelectBodyAnimation` and 0x0200002A
// `Script_SelectRelativeBodyAnimation` (`script/scenerunner.cpp`
// `placementOfFn`). The port applies p5, the yaw, only. Every call in every
// `SCPTDATA/*.SCX` (main and sync lists alike) is read; p4 (pitch) and p6
// (roll) are reported when either is not zero.
//
// Printed: the totals - files, calls, calls with a pitch, with a roll - then
// one line per such call (file, object, kind, p4 p5 p6, the clip p1 and
// the relative call's path file/index p7/p8).
#include "formats/scx.h"
#include "platform/datafs.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

float asFloat(std::int32_t v) {
    float f;
    std::memcpy(&f, &v, sizeof f);
    return f;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: scx_euler_probe <gamedata>\n");
        return 2;
    }
    const omk::DataFs fs(argv[1]);
    int files = 0, calls = 0, pitched = 0, rolled = 0, objects = 0;
    std::string lines;
    for (const std::string& path : fs.list("SCPTDATA", ".scx")) {
        const auto d = omk::DataFs::readPath(path);              // `list` hands full paths
        const std::string name = path.substr(path.find_last_of("/\\") + 1);
        const omk::ScxScene sc = omk::readScx(d);
        if (!sc.valid) continue;
        ++files;
        for (const auto& o : sc.objects) {
            bool any = false;
            for (const auto& f : o.functions) {
                const bool rel = f.id == 0x0200002Au, abs = f.id == 0x02000004u;
                if (!rel && !abs) continue;
                if (f.params.size() < 7) continue;
                ++calls;
                const float p4 = asFloat(f.params[4]), p5 = asFloat(f.params[5]),
                            p6 = asFloat(f.params[6]);
                const bool hasP = std::fabs(p4) > 0.001f, hasR = std::fabs(p6) > 0.001f;
                if (hasP) ++pitched;
                if (hasR) ++rolled;
                if (hasP || hasR) {
                    any = true;
                    char buf[200];
                    // ...and where to find him: the clip (p1) and, for the
                    // relative call, the path file and index (p7, p8)
                    std::snprintf(buf, sizeof buf, "euler %s %s %s %.2f %.2f %.2f clip %d path %d/%d\n",
                                  name.c_str(), o.name.c_str(), rel ? "relative" : "absolute", p4, p5,
                                  p6, f.params.size() > 1 ? f.params[1] : -1,
                                  rel && f.params.size() > 8 ? f.params[7] : -1,
                                  rel && f.params.size() > 8 ? f.params[8] : -1);
                    lines += buf;
                }
            }
            if (any) ++objects;
        }
    }
    std::printf("scx euler files %d calls %d pitched %d rolled %d objects %d\n", files, calls,
                pitched, rolled, objects);
    std::fputs(lines.c_str(), stdout);
    return 0;
}
