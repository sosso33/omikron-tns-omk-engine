// SPDX-License-Identifier: GPL-3.0-or-later
// The WORLD UNIT, measured off a thing whose real size everyone knows.
//
// The engine states the ratio itself, in both directions - `State_Save`
// quantises the player's position as `round(w * 0.0254 * 256)` and
// `Area_LoadSet` turns the clip-distance option into world units with
// `* 39.37007874015748` - but a reader is entitled to ask why a French
// studio's engine would store inches. This settles it without appealing to
// either constant: it prints a model's extent, and a human body is either
// 1.8 m or 71 m.
//
//     unit_probe <model.3DO>...
//
// `verify.py: world unit` drives it.
#include "platform/datafs.h"
#include "o3de/geom3do.h"

#include <cstdio>

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const auto d = omk::DataFs::readPath(argv[i]);
        if (d.empty()) { std::printf("%s unreadable\n", argv[i]); continue; }
        const auto g = omk::buildGeometry(d, omk::DrawFilter::Engine);
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (const auto& c : g.corners) {
            const float p[3] = {c.x, c.y, c.z};
            for (int k = 0; k < 3; ++k) {
                if (p[k] < lo[k]) lo[k] = p[k];
                if (p[k] > hi[k]) hi[k] = p[k];
            }
        }
        // Y is the height: the game is right-handed with Y DOWN, so the span
        // is what matters and its sign does not.
        std::printf("%s %zu %.3f %.3f %.3f\n", argv[i], g.corners.size(),
                    hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]);
    }
    return 0;
}
