// SPDX-License-Identifier: GPL-3.0-or-later
// PER-PIXEL vs PER-VERTEX lighting, measured - `todo/enhancements.md` row 7.
//
//     perpixel_probe
//
// The enhancement's claim is "the same law, sampled finer", and this is what
// makes that testable rather than rhetorical. One flat quad under one light,
// rendered twice through the Vulkan backend offscreen:
//
//   * per PIXEL - the light handed to the shader, which runs the same reach
//     test, the same linear falloff and the same `-(N.L)` per fragment;
//   * per VERTEX - `o3de/vertexlight.cpp` applied to the corners on the CPU,
//     exactly as the crowd is lit today, then interpolated by the rasteriser.
//
// Two things are reported. AT THE CENTRE - the point directly under the light,
// where a pixel maps back to the plane exactly - the shader is held to the law
// computed here from the same formula, so the comparison is against the
// engine's arithmetic rather than against the port's other copy of it.
// Everywhere else a pixel would have to be mapped back through the
// projection, and a mapping I got wrong would look exactly like a shader I got
// wrong; so the rest is measured as a PROFILE instead - how much the value
// varies along a scanline. Per pixel it follows the falloff and must vary;
// per vertex it is four corner values interpolated over two triangles and is
// near-flat. That is the whole claim, with no mapping needed.
#include "o3de/renderer.h"
#include "o3de/vertexlight.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <span>
#include <vector>

namespace omk {
Renderer* makeVulkanRenderer();
const char* vulkanDeviceName(Renderer*);
}

namespace {

// One quad in the y = 0 plane facing UP - which, with the game's Y growing
// downward, is a normal of (0, -1, 0).
omk::Geometry quad(float half) {
    omk::Geometry g;
    const float p[4][3] = {{-half, 0, -half}, {half, 0, -half},
                           {half, 0, half},   {-half, 0, half}};
    const int fan[2][3] = {{0, 1, 2}, {0, 2, 3}};
    for (const auto& t : fan)
        for (int k = 0; k < 3; ++k) {
            omk::Corner c{};
            c.x = p[t[k]][0]; c.y = p[t[k]][1]; c.z = p[t[k]][2];
            c.u = 0.5f; c.v = 0.5f;
            c.r = c.g = c.b = 0.0f;      // a LIT body starts from black
            c.nx = 0; c.ny = -1; c.nz = 0;
            c.phase = -1;
            g.corners.push_back(c);
        }
    g.cornerMirror.resize(g.corners.size(), 0);
    g.cornerMesh.resize(g.corners.size(), -1);
    g.cornerVertex.resize(g.corners.size(), -1);
    g.cornerDeclared.resize(g.corners.size(), -1);
    return g;
}

int green(const omk::Surface& s, int x, int y) {
    return (s.px[static_cast<std::size_t>(y) * s.w + x] >> 5) & 63;
}

}  // namespace

int main() {
    omk::Renderer* r = omk::makeVulkanRenderer();
    if (!r) { std::printf("no vulkan\n"); return 0; }
    const int W = 256, H = 256;
    if (!r->init(W, H)) { std::printf("no vulkan device\n"); return 0; }
    r->setTextures({});

    const float half = 200.0f;
    // Wide enough that the CORNERS are lit too, so the per-vertex path has a
    // real value to interpolate rather than four zeroes.
    omk::Light3do L{};
    L.pos[0] = 0; L.pos[1] = -150; L.pos[2] = 0;
    L.dir[0] = 0; L.dir[1] = 1; L.dir[2] = 0;        // travelling downward
    L.radiusA = 600.0f; L.radiusB = 40.0f;
    L.colour = 0x00FFFFFF; L.f32 = 1.0f;

    omk::View v;
    v.cam.eye[0] = 0; v.cam.eye[1] = -520; v.cam.eye[2] = 0.1f;   // straight down
    v.cam.at[0] = 0;  v.cam.at[1] = 0;  v.cam.at[2] = 0;
    v.cam.hfovDeg = 60.0f;

    // NO DITHER. This probe holds the shader to an analytic value, and the
    // 888->565 dither perturbs the green channel by up to one 6-bit step - so
    // with it on the centre pixel misses the law by 1 and says nothing about
    // the light. Dithering is measured by its own check; here it is noise.
    v.dither = false;

    omk::Surface pix, vert;
    {   // ---- per PIXEL: the light goes to the shader
        omk::Geometry g = quad(half);
        omk::View::GpuLight gl{};
        for (int k = 0; k < 3; ++k) { gl.pos[k] = L.pos[k]; gl.dir[k] = L.dir[k]; }
        gl.radiusA = L.radiusA; gl.radiusB = L.radiusB;
        gl.colour[0] = gl.colour[1] = gl.colour[2] = 1.0f;
        gl.intensity = L.f32;
        v.lights.assign(1, gl);
        std::vector<omk::Draw> d;
        d.push_back({0, &g, 0, g.corners.size(), omk::Blend::Opaque, false, 1, false});
        omk::drawWithMirror(*r, d, v, omk::MirrorPlane{});
        pix = r->readback();
    }
    {   // ---- per VERTEX: the SAME law on the CPU, as the crowd is lit today
        omk::Geometry g = quad(half);
        const float at[3] = {0, 0, 0};
        omk::applyLights(g, 0, g.corners.size(), at, std::span<const omk::Light3do>(&L, 1));
        v.lights.clear();
        std::vector<omk::Draw> d;
        d.push_back({0, &g, 0, g.corners.size(), omk::Blend::Opaque, false, 0, false});
        omk::drawWithMirror(*r, d, v, omk::MirrorPlane{});
        vert = r->readback();
    }

    // THE LAW, from the same formula, at the point under the light.
    const auto expect = [&](float x, float z) {
        const float dx = x - L.pos[0], dy = 0.0f - L.pos[1], dz = z - L.pos[2];
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d > L.radiusA) return 0.0f;
        float fall = 1.0f - (d - L.radiusB) / (L.radiusA - L.radiusB);
        if (fall > 1.0f) fall = 1.0f;
        const float k = L.f32 * 256.0f * fall;   // `-(N.L)` is k, N facing the light
        return std::min(k / 256.0f, 1.0f);
    };
    const int cx = W / 2, cy = H / 2;
    const int wantCentre = static_cast<int>(expect(0.0f, 0.0f) * 63.0f + 0.5f);

    const auto spread = [&](const omk::Surface& s) {
        int lo = 64, hi = -1;
        for (int x = 24; x < W - 24; ++x) {
            const int g = green(s, x, cy);
            lo = std::min(lo, g); hi = std::max(hi, g);
        }
        return hi - lo;
    };
    std::printf("device %s\n", omk::vulkanDeviceName(r));
    std::printf("perpixel at the centre %d, the law says %d, delta %d\n",
                green(pix, cx, cy), wantCentre,
                std::abs(green(pix, cx, cy) - wantCentre));
    std::printf("spread across the middle scanline: perpixel %d, pervertex %d\n",
                spread(pix), spread(vert));
    delete r;
    return 0;
}
