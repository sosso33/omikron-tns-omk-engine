// SPDX-License-Identifier: GPL-3.0-or-later
// THE DEPTH TIE - two faces on the same four vertices, opposite winding.
//
//     tie_probe
//
// Anekbah's 18 two-sided shop signs are each two quads on ONE set of four
// vertices in opposite winding, one material a side (`Abank03`: (2,3,1,0)
// mat 1 and (3,2,0,1) mat 9). The engine draws both - CULLMODE = NONE - and
// its strict `ZFUNC = GREATER` on a quantised z-buffer keeps the FIRST drawn
// on every pixel. A float compare of each triangle's own interpolated depth
// does not: the two windings split on different diagonals, their `1/izp`
// differ in the last bits, and the later face wins wherever the noise fell
// its way - dots of the other advert, re-rolled as the camera creeps. That
// was the Anekbah panel FLICKER (todo/standing-unknowns 4).
//
// This draws the same shape with no textures - the first face red, the
// second green - from an oblique camera, and counts. The tie rule in
// `drawGeometry` makes green 0.
#include "o3de/geom3do.h"
#include "o3de/raster.h"
#include "ui/surface.h"

#include <cmath>
#include <cstdio>
#include <vector>

int main() {
    omk::Geometry g;
    // a 200 x 60 sign standing at z = 1000, seen from the side so the
    // depth runs across it
    const float P[4][3] = {{-100, -30, 900}, {100, -30, 1100}, {100, 30, 1100}, {-100, 30, 900}};
    auto corner = [&](int k, float r, float gg, float b) {
        omk::Corner c{};
        c.x = P[k][0]; c.y = P[k][1]; c.z = P[k][2];
        c.u = 0; c.v = 0; c.r = r; c.g = gg; c.b = b;
        c.nx = 0; c.ny = 0; c.nz = -1; c.phase = -1.0f;
        return c;
    };
    auto quad = [&](const int idx[4], float r, float gg, float b, int material) {
        omk::Batch bt; bt.material = material; bt.start = g.corners.size();
        // the same split `buildGeometry` makes: (0,1,2) and (0,2,3) of the quad's order
        for (int t : {idx[0], idx[1], idx[2], idx[0], idx[2], idx[3]})
            g.corners.push_back(corner(t, r, gg, b));
        bt.count = 6; g.batches.push_back(bt);
    };
    // `Abooks02`'s pair: (0,1,2,3) and (1,0,3,2) - the same cycle walked the
    // other way, so the two split on different diagonals. (The probe's
    // first draft used `Abank03`'s (2,3,1,0)/(3,2,0,1) on ITS OWN vertex
    // layout, which made a bow-tie whose two triangulations cover different
    // pixels - 212 one way, 420 the other - and read as the tie failing.
    // Index order is only a cycle over the mesh's own vertices.)
    const int front[4] = {0, 1, 2, 3};
    const int back[4]  = {1, 0, 3, 2};
    quad(front, 255, 0, 0, 0);
    quad(back, 0, 255, 0, 1);
    g.cornerMirror.assign(g.corners.size(), 0);
    g.cornerMesh.assign(g.corners.size(), 0);

    omk::RCamera cam;
    cam.eye[0] = -200; cam.eye[1] = 0; cam.eye[2] = 300;
    cam.at[0] = 0; cam.at[1] = 0; cam.at[2] = 1000;
    cam.hfovDeg = 60; cam.w = 320; cam.h = 240;
    omk::Surface fb(320, 240, 0);
    std::vector<float> depth;
    omk::clearDepth(depth, 320, 240);
    const auto st = omk::drawGeometry(fb, depth, cam, g, {});
    long red = 0, green = 0;
    for (int y = 0; y < 240; ++y)
        for (int x = 0; x < 320; ++x) {
            const auto p = fb.at(x, y);
            const int r = (p >> 11) & 31, gg = (p >> 5) & 63;
            if (r > 16 && gg < 8) ++red;
            else if (gg > 32 && r < 8) ++green;
        }
    // ...and the two faces ALONE, into their own depth buffers, to measure
    // how far their depths actually differ where both cover a pixel.
    {
        omk::Geometry a, b;
        a.corners.assign(g.corners.begin(), g.corners.begin() + 6);
        b.corners.assign(g.corners.begin() + 6, g.corners.end());
        a.batches = {g.batches[0]}; b.batches = {g.batches[1]}; b.batches[0].start = 0;
        a.cornerMirror.assign(6, 0); b.cornerMirror.assign(6, 0);
        a.cornerMesh.assign(6, 0); b.cornerMesh.assign(6, 0);
        omk::Surface fa(320, 240, 0), fbb(320, 240, 0);
        std::vector<float> da, db;
        omk::clearDepth(da, 320, 240); omk::clearDepth(db, 320, 240);
        omk::drawGeometry(fa, da, cam, a, {});
        omk::drawGeometry(fbb, db, cam, b, {});
        long both = 0, onlyA = 0, onlyB = 0; double worst = 0, sum = 0;
        for (std::size_t i = 0; i < da.size(); ++i) {
            const bool ia = fa.px[i] != 0, ib = fbb.px[i] != 0;
            if (ia && ib) {
                ++both;
                const double rel = std::fabs(da[i] - db[i]) / da[i];
                sum += rel; if (rel > worst) worst = rel;
            } else if (ia) ++onlyA; else if (ib) ++onlyB;
        }
        std::printf("alone: both %ld onlyA %ld onlyB %ld  relative depth difference mean %.3g worst %.3g\n",
                    both, onlyA, onlyB, both ? sum / both : 0.0, worst);
    }
    std::printf("tie: triangles %ld drawn %ld behind %ld offscreen %ld first-face %ld "
                "second-face %ld rejects %ld\n",
                static_cast<long>(st.triangles), static_cast<long>(st.drawn),
                static_cast<long>(st.behind), static_cast<long>(st.offscreen), red, green,
                static_cast<long>(st.depthRejects));
    return 0;
}
