// SPDX-License-Identifier: GPL-3.0-or-later
// THE FOG, measured at known depths through the renderer boundary.
//
//     fog_probe
//
// A white quad, drawn face-on at a series of distances through
// `SoftwareRenderer` with the engine's own fog parameters, and the centre
// pixel read back. That isolates the ported decisions from any set's geometry:
//
//   * the ramp is LINEAR from `fogStart` to `fogEnd` - `FOGTABLEMODE` 3 at
//     density 1.0, the only mode `20_ddraw.c` sets;
//   * it ENDS at `fogEnd`, which is the clip distance itself (the scene's
//     `+340`) and NOT the 0.95 bucket split;
//   * bucket key bits `0x2080` - the near bucket and the transparent state -
//     are not fogged at all;
//   * bucket key bit `0x800` DOUBLES both ends.
//
// Prints one line per case: <label> <key> <depth> <r> <g> <b>
// `verify.py: engine fog` drives it.
#include "o3de/renderer.h"

#include <cstdio>
#include <vector>

int main() {
    constexpr int W = 64, H = 64;
    omk::SoftwareRenderer ren;
    if (!ren.init(W, H)) { std::fprintf(stderr, "no software renderer\n"); return 1; }

    // One white, unlit quad on the +Z axis at `d`, big enough to cover the
    // centre pixel at every distance tested.
    const auto quadAt = [](float d) {
        omk::Geometry g;
        const float s = d * 0.5f;
        const float xs[6] = {-s,  s,  s, -s,  s, -s};
        const float ys[6] = {-s, -s,  s, -s,  s,  s};
        for (int i = 0; i < 6; ++i) {
            omk::Corner c;
            c.x = xs[i]; c.y = ys[i]; c.z = d;
            c.u = 0; c.v = 0; c.phase = -1.0f;
            c.r = 1.0f; c.g = 1.0f; c.b = 1.0f;   // white, so the fog is all that acts
            g.corners.push_back(c);
        }
        omk::Batch b;
        b.material = 0; b.start = 0; b.count = 6;
        b.blend = omk::Blend::Opaque; b.cutout = false;
        g.batches.push_back(b);
        return g;
    };

    // Looking down +Z from the origin, the game's own conventions.
    omk::View v;
    v.cam.eye[0] = 0; v.cam.eye[1] = 0; v.cam.eye[2] = 0;
    v.cam.at[0] = 0;  v.cam.at[1] = 0;  v.cam.at[2] = 1;
    v.cam.hfovDeg = 90.0f;
    v.cam.w = W; v.cam.h = H;
    v.fog = true;
    v.fogStart = 100.0f;
    v.fogEnd   = 500.0f;
    v.fogColour[0] = 0; v.fogColour[1] = 0; v.fogColour[2] = 0;   // the shipped black

    struct Case { const char* label; std::uint32_t key; float depth; };
    const Case cases[] = {
        // the linear ramp, at, inside and past the range
        {"before",  0x0000u,  50.0f},
        {"start",   0x0000u, 100.0f},
        {"quarter", 0x0000u, 200.0f},
        {"half",    0x0000u, 300.0f},
        {"end",     0x0000u, 500.0f},
        {"past",    0x0000u, 800.0f},
        // the exclusions
        {"near80",  0x0080u, 300.0f},   // the near bucket: no fog
        {"trans",   0x2000u, 300.0f},   // the transparent state: no fog
        {"cut800",  0x0800u, 300.0f},   // doubled: 200..1000, so LESS fogged
        {"cut800f", 0x0800u, 900.0f},   // still inside the doubled range
    };

    // one white texel, so the texture path does not change the colour
    omk::Texture t;
    t.name = "white"; t.width = 1; t.height = 1; t.bpp = 24; t.exact = true;
    t.rgb = {255, 255, 255};
    const std::vector<omk::Texture> pool = {t};
    ren.setTextures(pool);

    for (const auto& c : cases) {
        ren.begin(v);
        const omk::Geometry g = quadAt(c.depth);
        omk::Draw d;
        d.bucketKey = c.key;   // slot 0 = the white texel
        d.geo = &g; d.start = 0; d.count = 6;
        d.blend = omk::Blend::Opaque; d.cutout = false;
        ren.submit(d);
        ren.end();
        const omk::Surface& s = ren.readback();
        const std::uint16_t px = s.at(W / 2, H / 2);
        std::printf("%s 0x%04X %.0f %d %d %d\n", c.label, c.key, c.depth,
                    ((px >> 11) & 0x1F) * 255 / 31,
                    ((px >> 5) & 0x3F) * 255 / 63,
                    (px & 0x1F) * 255 / 31);
    }
    return 0;
}
