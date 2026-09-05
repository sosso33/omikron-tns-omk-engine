// SPDX-License-Identifier: GPL-3.0-or-later
// THE .3DO LIGHT RECORD, decoded and checked over a corpus.
//
//     light_probe <model.3DO>...
//
// Prints one summary line:
//   lights magic named scratchZero centreInBox distinctColours distinctFlags nan
//
// and, with one model named, a line per light. `verify.py: light record`.
// See `engine/src/formats/light3do.h` for the layout and `todo/mesh-lights.md`.
#include "platform/datafs.h"
#include "formats/light3do.h"
#include "formats/mesh3do.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>

int main(int argc, char** argv) {
    long total = 0, magic = 0, named = 0, scratch = 0, inBox = 0, nan = 0;
    std::set<std::uint32_t> colours, flags;
    const bool one = (argc == 2);
    for (int i = 1; i < argc; ++i) {
        const auto d = omk::DataFs::readPath(argv[i]);
        if (d.empty()) continue;
        const auto h = omk::readHeader(d);
        if (!h) continue;
        const auto ls = omk::readLights(d, *h);
        const auto base = static_cast<std::size_t>(h->lightOff);
        for (std::size_t k = 0; k < ls.size(); ++k) {
            const omk::Light3do& l = ls[k];
            ++total;
            const std::size_t o = base + omk::kLightRecord * k;
            // the tag the record opens its name with
            if (!std::memcmp(d.data() + o + 4, "LIGH", 4)) ++magic;
            if (l.name.rfind("LIGHT", 0) == 0) ++named;
            // every runtime slot must be zero on disk: the transformed
            // position (+60), the transformed direction (+124) and the two
            // squared radii (+240) are all written by the loader
            bool z = true;
            for (std::size_t b : {60u, 64u, 68u, 124u, 128u, 132u, 240u, 244u}) {
                float v = 0;
                std::memcpy(&v, d.data() + o + b, 4);
                if (v != 0.0f) z = false;
            }
            if (z) ++scratch;
            // THE GEOMETRIC INVARIANT: the authored centre lies inside the
            // four corners' bounding box
            float lo[3], hi[3];
            for (int a = 0; a < 3; ++a) { lo[a] = hi[a] = l.corner[0][a]; }
            for (int c = 1; c < 4; ++c)
                for (int a = 0; a < 3; ++a) {
                    lo[a] = std::min(lo[a], l.corner[c][a]);
                    hi[a] = std::max(hi[a], l.corner[c][a]);
                }
            //
            // NaN FIRST, and it is not pedantry: `MTrone.3DO`'s LIGHT15 has
            // NaN corners, and every comparison against a NaN is false - so a
            // containment test written the obvious way PASSES it and reports
            // 4179 of 4179 where the truth is 4178 and one corrupt record.
            // That is the shape of error a check exists to catch, and this one
            // caught it only because a second implementation disagreed.
            bool finite = std::isfinite(l.centre[0]) && std::isfinite(l.centre[1]) &&
                          std::isfinite(l.centre[2]);
            for (int c = 0; c < 4 && finite; ++c)
                for (int a = 0; a < 3; ++a)
                    if (!std::isfinite(l.corner[c][a])) finite = false;
            if (!finite) { ++nan; continue; }
            bool in = true;
            for (int a = 0; a < 3; ++a)
                if (l.centre[a] < lo[a] - 0.5f || l.centre[a] > hi[a] + 0.5f) in = false;
            if (in) ++inBox;
            colours.insert(l.colour);
            flags.insert(l.flags);
            if (one)
                std::printf("  %-12s flags 0x%08X  colour %06X  rA %8.2f  rB %8.2f"
                            "  pos %9.1f %8.1f %9.1f\n",
                            l.name.c_str(), l.flags, l.colour & 0xFFFFFFu,
                            l.radiusA, l.radiusB, l.pos[0], l.pos[1], l.pos[2]);
        }
    }
    std::printf("%ld %ld %ld %ld %ld %zu %zu %ld\n", total, magic, named, scratch, inBox,
                colours.size(), flags.size(), nan);
    return 0;
}
