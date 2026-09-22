// SPDX-License-Identifier: GPL-3.0-or-later
#include "o3de/vertexlight.h"

#include <cmath>
#include <vector>

namespace omk {

const Light3do* strongestLightAt(const float p[3], std::span<const Light3do> lights,
                                 float* kOut) {
    const Light3do* best = nullptr;
    float bestK = 0.0f;
    for (const Light3do& l : lights) {
        const float dx = p[0] - l.pos[0], dy = p[1] - l.pos[1], dz = p[2] - l.pos[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (!(d2 <= l.radiusA * l.radiusA)) continue;
        if (!(l.radiusA > l.radiusB)) continue;
        float fall = 1.0f - (std::sqrt(d2) - l.radiusB) / (l.radiusA - l.radiusB);
        if (fall > 1.0f) fall = 1.0f;
        const float k = l.f32 * 256.0f * fall;
        if (k > bestK) { bestK = k; best = &l; }
    }
    if (kOut) *kOut = bestK;
    return best;
}

int applyLights(Geometry& g, std::size_t first, std::size_t count,
                const float bodyPos[3], std::span<const Light3do> lights) {
    if (count == 0 || first + count > g.corners.size()) return 0;
    // THE CORNERS ARE WALKED ONCE, NOT ONCE PER LIGHT (2026-09-22). This used
    // to be a loop over lights each running the whole corner range - so a
    // body's 21 KB of corners was read five times a frame on a street, which
    // on a Vita is the cost rather than the arithmetic. The reaching lights
    // are gathered first, in their own order, and each corner then takes them
    // in that same order: every corner's colour is the SAME SEQUENCE of
    // additions and clamps on the same values, so the result is bit-identical
    // (`todo/vita-port.md`; shown by a byte-identical street render).
    struct Reach {
        float lx, ly, lz;
        std::uint8_t r, g, b;
    };
    static thread_local std::vector<Reach> reach;
    reach.clear();
    for (const Light3do& l : lights) {
        // the reach test is the SQUARED radius, so a light is not `sqrt`ed
        // until it is known to reach
        const float dx = bodyPos[0] - l.pos[0];
        const float dy = bodyPos[1] - l.pos[1];
        const float dz = bodyPos[2] - l.pos[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (!(d2 <= l.radiusA * l.radiusA)) continue;
        if (!(l.radiusA > l.radiusB)) continue;   // a degenerate pair lights nothing

        float k = l.f32 * 256.0f;
        const float d = std::sqrt(d2);
        // the linear falloff from the inner radius to the outer, clamped
        float fall = 1.0f - (d - l.radiusB) / (l.radiusA - l.radiusB);
        if (fall > 1.0f) fall = 1.0f;
        k *= fall;
        if (!(k > 0.0f)) continue;

        reach.push_back(Reach{l.dir[0] * k, l.dir[1] * k, l.dir[2] * k,
                              static_cast<std::uint8_t>((l.colour >> 16) & 0xFF),
                              static_cast<std::uint8_t>((l.colour >> 8) & 0xFF),
                              static_cast<std::uint8_t>(l.colour & 0xFF)});
    }
    if (reach.empty()) return 0;
    for (std::size_t i = first; i < first + count; ++i) {
        Corner& c = g.corners[i];
        const float nx = c.nx, ny = c.ny, nz = c.nz;
        float cr = c.r, cg = c.g, cb = c.b;
        for (const Reach& e : reach) {
            const float t = -(nx * e.lx + ny * e.ly + nz * e.lz);
            if (!(t > 0.0f)) continue;
            const int ti = static_cast<int>(t);
            const float ar = static_cast<float>(lightRamp(e.r, ti)) / 255.0f;
            const float ag = static_cast<float>(lightRamp(e.g, ti)) / 255.0f;
            const float ab = static_cast<float>(lightRamp(e.b, ti)) / 255.0f;
            cr = cr + ar > 1.0f ? 1.0f : cr + ar;
            cg = cg + ag > 1.0f ? 1.0f : cg + ag;
            cb = cb + ab > 1.0f ? 1.0f : cb + ab;
        }
        c.r = cr; c.g = cg; c.b = cb;
    }
    return static_cast<int>(reach.size());
}

}  // namespace omk
