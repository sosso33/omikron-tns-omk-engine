// SPDX-License-Identifier: GPL-3.0-or-later
// THE MAPPED SHADOW, measured where a window cannot be opened.
//
//     shadow_probe <dirX,dirY,dirZ> [strength]
//
// A synthetic scene: one big ground quad and one small caster floating above
// it. No game data, no window, no surface - the Vulkan backend renders
// offscreen exactly as `run_vulkan` does - so the whole of
// `todo/enhancements.md` row 6 can be exercised deterministically.
//
// Prints the fraction of ground pixels the shadow darkened and the CENTROID of
// those pixels, which is what the check needs: a shadow that is real MOVES
// when the light moves, and a value verified in one still frame is not
// verified (CLAUDE.md 1).
//
// The game's Y grows DOWN, so the ground is at y = 0 and the caster above it
// is at a NEGATIVE y.
#include "o3de/renderer.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>

namespace omk {
Renderer* makeVulkanRenderer();
const char* vulkanDeviceName(Renderer*);
}

namespace {

void quad(omk::Geometry& g, float cx, float cy, float cz, float half, bool flat) {
    const float p[4][3] = {{cx - half, cy, cz - half}, {cx + half, cy, cz - half},
                           {cx + half, cy, cz + half}, {cx - half, cy, cz + half}};
    const int fan[2][3] = {{0, 1, 2}, {0, 2, 3}};
    for (const auto& t : fan)
        for (int k = 0; k < 3; ++k) {
            omk::Corner c{};
            c.x = p[t[k]][0]; c.y = flat ? p[t[k]][1] : p[t[k]][1]; c.z = p[t[k]][2];
            c.u = 0.5f; c.v = 0.5f;
            c.r = c.g = c.b = 1.0f;
            c.nx = 0; c.ny = -1; c.nz = 0; c.phase = -1;
            g.corners.push_back(c);
        }
    g.cornerMirror.resize(g.corners.size(), 0);
    g.cornerMesh.resize(g.corners.size(), -1);
    g.cornerVertex.resize(g.corners.size(), -1);
    g.cornerDeclared.resize(g.corners.size(), -1);
}

}  // namespace

int main(int argc, char** argv) {
    float dir[3] = {0.0f, 1.0f, 0.0f};      // straight down, in a Y-down world
    if (argc > 1) std::sscanf(argv[1], "%f,%f,%f", &dir[0], &dir[1], &dir[2]);
    const float strength = argc > 2 ? static_cast<float>(std::atof(argv[2])) : 0.6f;

    omk::Renderer* r = omk::makeVulkanRenderer();
    if (!r) { std::printf("no vulkan\n"); return 0; }
    const int W = 320, H = 320;
    if (!r->init(W, H)) { std::printf("no vulkan device\n"); return 0; }
    r->setTextures({});                      // every draw takes the white fallback

    omk::Geometry ground, caster;
    quad(ground, 0, 0, 0, 400.0f, true);     // the floor
    quad(caster, 0, -120.0f, 0, 40.0f, true);  // a slab 120 units above it

    omk::View v;
    // looking down at the floor from above and behind
    v.cam.eye[0] = 0; v.cam.eye[1] = -420; v.cam.eye[2] = -420;
    v.cam.at[0] = 0;  v.cam.at[1] = 0;   v.cam.at[2] = 0;
    v.cam.hfovDeg = 60.0f;
    v.shadow.on = strength > 0.0f;
    for (int k = 0; k < 3; ++k) { v.shadow.dir[k] = dir[k]; v.shadow.centre[k] = 0.0f; }
    v.shadow.centre[1] = -60.0f;
    v.shadow.radius = 260.0f;
    v.shadow.strength = strength;

    std::vector<omk::Draw> draws;
    // {key, geo, start, count, blend, cutout, LIT, castsShadow} - `lit` was
    // added between the last two by row 7, and a positional initialiser that
    // still had seven fields silently put `castsShadow` in `lit` and left
    // nothing casting. The check went red and said so.
    draws.push_back({0, &ground, 0, ground.corners.size(), omk::Blend::Opaque, false, 0, false});
    draws.push_back({0, &caster, 0, caster.corners.size(), omk::Blend::Opaque, false, 0, true});

    omk::drawWithMirror(*r, draws, v, omk::MirrorPlane{});
    const omk::Surface& s = r->readback();

    // The ground is white where lit. Count what is not, ignoring the caster
    // itself (the top third of the frame, where it sits between the eye and
    // the floor) and the black background.
    long dark = 0, lit = 0;
    double sx = 0, sy = 0;
    for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < s.w; ++x) {
            const std::uint16_t px = s.px[static_cast<std::size_t>(y) * s.w + x];
            const int rr = (px >> 11) & 31, gg = (px >> 5) & 63, bb = px & 31;
            if (rr < 2 && gg < 4 && bb < 2) continue;          // background
            if (rr >= 28 && gg >= 56 && bb >= 28) { ++lit; continue; }
            ++dark; sx += x; sy += y;
        }
    std::printf("device %s\n", omk::vulkanDeviceName(r));
    std::printf("dir %.2f,%.2f,%.2f  strength %.2f  lit %ld  shadowed %ld  centroid %.1f %.1f\n",
                static_cast<double>(dir[0]), static_cast<double>(dir[1]),
                static_cast<double>(dir[2]), static_cast<double>(strength), lit, dark,
                dark ? sx / static_cast<double>(dark) : -1.0,
                dark ? sy / static_cast<double>(dark) : -1.0);
    delete r;
    return 0;
}
