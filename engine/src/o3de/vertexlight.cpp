// SPDX-License-Identifier: GPL-3.0-or-later
#include "o3de/vertexlight.h"

#include <cmath>

namespace omk {

int applyLights(Geometry& g, std::size_t first, std::size_t count,
                const float bodyPos[3], std::span<const Light3do> lights) {
    if (count == 0 || first + count > g.corners.size()) return 0;
    int reached = 0;
    for (const Light3do& l : lights) {
        // the reach test the engine makes with the SQUARED radius it cached
        // at load (`+240`), so no square root is taken unless it passes
        const float dx = bodyPos[0] - l.pos[0];
        const float dy = bodyPos[1] - l.pos[1];
        const float dz = bodyPos[2] - l.pos[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (!(d2 <= l.radiusA * l.radiusA)) continue;
        if (!(l.radiusA > l.radiusB)) continue;   // a degenerate pair lights nothing

        float k = l.f32 * 256.0f;
        const float d = std::sqrt(d2);
        // The falloff is LINEAR from the inner radius to the outer. The engine
        // guards it - `if (!(v11 | v12))` on a comparison this reading did not
        // decode - and the guard's only sensible sense is "not inside the
        // inner radius", since the expression exceeds 1 there. Clamped rather
        // than left to over-brighten; recorded as a reading, not a finding.
        float fall = 1.0f - (d - l.radiusB) / (l.radiusA - l.radiusB);
        if (fall > 1.0f) fall = 1.0f;
        k *= fall;
        if (!(k > 0.0f)) continue;
        ++reached;

        // The direction `readLights` computed the way `sub_493C30` does -
        // NOT the on-disk corner 0, which is a world-space point. Scaled by
        // the falloff exactly as the engine folds `v46` in before the dot.
        const float lx = l.dir[0] * k;
        const float ly = l.dir[1] * k;
        const float lz = l.dir[2] * k;

        // The engine indexes three ramps by the record's BYTES at +44, +45
        // and +46 and adds them into three consecutive bytes of a BGRA vertex
        // colour - so +46 is red, +45 green, +44 blue, which is the same
        // 0x00RRGGBB the dword reads as.
        const auto lightR = static_cast<std::uint8_t>((l.colour >> 16) & 0xFF);
        const auto lightG = static_cast<std::uint8_t>((l.colour >> 8) & 0xFF);
        const auto lightB = static_cast<std::uint8_t>(l.colour & 0xFF);

        for (std::size_t i = first; i < first + count; ++i) {
            Corner& c = g.corners[i];
            const float t = -(c.nx * lx + c.ny * ly + c.nz * lz);
            if (!(t > 0.0f)) continue;
            const int ti = static_cast<int>(t);
            // the saturating add, on the port's 0..1 floats rather than the
            // engine's bytes - the clamp is the same clamp
            const float ar = static_cast<float>(lightRamp(lightR, ti)) / 255.0f;
            const float ag = static_cast<float>(lightRamp(lightG, ti)) / 255.0f;
            const float ab = static_cast<float>(lightRamp(lightB, ti)) / 255.0f;
            c.r = c.r + ar > 1.0f ? 1.0f : c.r + ar;
            c.g = c.g + ag > 1.0f ? 1.0f : c.g + ag;
            c.b = c.b + ab > 1.0f ? 1.0f : c.b + ab;
        }
    }
    return reached;
}

}  // namespace omk
