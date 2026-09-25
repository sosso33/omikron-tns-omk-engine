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

namespace {
// One light's reach on a body: false when it does not reach, else its
// direction scaled by its strength. `applyLights` and `lightReach` both go
// through here, so a GPU-lit body is lit by exactly the CPU's lights.
bool reachOf(const Light3do& l, const float bodyPos[3], float out[3]) {
    // the reach test is the SQUARED radius, so a light is not `sqrt`ed
    // until it is known to reach
    const float dx = bodyPos[0] - l.pos[0];
    const float dy = bodyPos[1] - l.pos[1];
    const float dz = bodyPos[2] - l.pos[2];
    const float d2 = dx * dx + dy * dy + dz * dz;
    if (!(d2 <= l.radiusA * l.radiusA)) return false;
    if (!(l.radiusA > l.radiusB)) return false;   // a degenerate pair lights nothing

    float k = l.f32 * 256.0f;
    const float d = std::sqrt(d2);
    // the linear falloff from the inner radius to the outer, clamped
    float fall = 1.0f - (d - l.radiusB) / (l.radiusA - l.radiusB);
    if (fall > 1.0f) fall = 1.0f;
    k *= fall;
    if (!(k > 0.0f)) return false;
    out[0] = l.dir[0] * k; out[1] = l.dir[1] * k; out[2] = l.dir[2] * k;
    return true;
}
}  // namespace

int lightReach(const float bodyPos[3], std::span<const Light3do> lights,
               std::vector<float>& out) {
    int n = 0;
    for (const Light3do& l : lights) {
        float v[3];
        if (!reachOf(l, bodyPos, v)) continue;
        out.insert(out.end(), {v[0], v[1], v[2], 0.0f,
                               static_cast<float>((l.colour >> 16) & 0xFF),
                               static_cast<float>((l.colour >> 8) & 0xFF),
                               static_cast<float>(l.colour & 0xFF), 0.0f});
        ++n;
    }
    return n;
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
        const float* ramp;          // 256 x rgb, `lightRamp` tabulated
    };
    // THE RAMP, TABULATED PER LIGHT. `(t * c) >> 8 / 255` depends only on the
    // light's three colour bytes and the 0..255 index; the table is built for
    // each light that REACHES this body, in this call. It was a per-colour
    // cache kept `thread_local` - and on the Vita a `thread_local` is not per
    // thread for the pool's kernel threads (see `composePose`), so the
    // crowd's workers shared and raced it. Same values, the call's own.
    std::vector<float> ramps;
    std::vector<Reach> reach;
    std::vector<std::uint32_t> colours;
    for (const Light3do& l : lights) {
        float v[3];
        if (!reachOf(l, bodyPos, v)) continue;
        reach.push_back(Reach{v[0], v[1], v[2], nullptr});
        colours.push_back(l.colour);
    }
    ramps.resize(reach.size() * 256 * 3);
    for (std::size_t r = 0; r < reach.size(); ++r) {
        const std::uint32_t colour = colours[r];
        const auto cr = static_cast<std::uint8_t>((colour >> 16) & 0xFF);
        const auto cg = static_cast<std::uint8_t>((colour >> 8) & 0xFF);
        const auto cb = static_cast<std::uint8_t>(colour & 0xFF);
        float* t3 = ramps.data() + r * 256 * 3;
        for (int t = 0; t < 256; ++t) {
            t3[3 * t]     = static_cast<float>(lightRamp(cr, t)) / 255.0f;
            t3[3 * t + 1] = static_cast<float>(lightRamp(cg, t)) / 255.0f;
            t3[3 * t + 2] = static_cast<float>(lightRamp(cb, t)) / 255.0f;
        }
        reach[r].ramp = t3;
    }
    if (reach.empty()) return 0;
    for (std::size_t i = first; i < first + count; ++i) {
        Corner& c = g.corners[i];
        const float nx = c.nx, ny = c.ny, nz = c.nz;
        float cr = c.r, cg = c.g, cb = c.b;
        for (const Reach& e : reach) {
            const float t = -(nx * e.lx + ny * e.ly + nz * e.lz);
            // NO BRANCH ON THE SIGN, and it is the same answer: `lightRamp`
            // clamps a `t` at or below zero to zero and so returns zero, and
            // adding zero to a colour already in [0, 1] leaves it and its clamp
            // alone. A corner faces away from about half the lights that reach
            // it, so the test this replaces mispredicted about half the time.
            int ti = static_cast<int>(t);
            ti = ti < 0 ? 0 : (ti > 255 ? 255 : ti);      // `lightRamp`'s own clamp
            const float* rp = e.ramp + 3 * static_cast<std::size_t>(ti);
            const float ar = rp[0], ag = rp[1], ab = rp[2];
            cr = cr + ar > 1.0f ? 1.0f : cr + ar;
            cg = cg + ag > 1.0f ? 1.0f : cg + ag;
            cb = cb + ab > 1.0f ? 1.0f : cb + ab;
        }
        c.r = cr; c.g = cg; c.b = cb;
    }
    return static_cast<int>(reach.size());
}

}  // namespace omk
