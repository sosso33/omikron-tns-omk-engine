// SPDX-License-Identifier: GPL-3.0-or-later
// A .SFX's section C effects and its section D bindings, side by side: which
// tag fires which effect, with the sprite id, blend mode, colour ramp and
// scale. What tells a street light from a fire when they share a sprite.
//
//     sfx_effects <gamedata> <name.sfx>
#include "formats/sfx.h"
#include "platform/datafs.h"
#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    const omk::DataFs fs(argv[1]);
    const auto p = fs.resolve(std::string("SCPTDATA/") + argv[2]);
    if (!p) { std::fprintf(stderr, "no such .sfx\n"); return 1; }
    const omk::SfxFile sx = omk::readSfx(omk::DataFs::readPath(*p));
    std::printf("%s: %zu effects, %zu bindings\n\n", argv[2],
                sx.effects.size(), sx.bindings.size());
    std::printf("%-4s %-4s %-12s %6s %5s  %8s %8s %7s\n",
                "idx", "id", "name", "sprite", "mode", "colour0", "colour1", "life");
    for (std::size_t i = 0; i < sx.effects.size(); ++i) {
        const auto& e = sx.effects[i];
        std::printf("%-4zu %-4d %-12s %6u %5u  %06X   %06X   %7.1f\n",
                    i, e.id, e.name.c_str(), e.sprite, e.mode,
                    e.colour0 & 0xFFFFFFu, e.colour1 & 0xFFFFFFu, e.life);
    }
    std::printf("\nbindings (tag -> effect, period):\n");
    for (const auto& b : sx.bindings)
        std::printf("  '%-4s' -> effect %d  period %.1f\n", b.tag, b.effect, b.period);
    return 0;
}
