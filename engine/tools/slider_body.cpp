// SPDX-License-Identifier: GPL-3.0-or-later
// WHERE THE SLIDER'S ORIGIN IS ON ITS BODY - `SLI_FN.3DO`'s four root
// sub-objects measured.
//
//     slider_body <gamedata>
//
// The port draws a vehicle re-centred on its root mesh's own `pos`; the
// engine places the NODE and the file's geometry hangs off the file origin.
// `sub_457F50` sets the slider node 196.85 units (5.00 m) behind the rider,
// so where the origin sits on the body decides whether a man placed from
// the clips' `SlBassin`-relative numbers lands in the seat of a body drawn
// about `SlBasB`. This prints, per root subtree: the root's pos, the bounding
// box, and the box's centre against the root pos and the file origin.
#include "formats/mesh3do.h"
#include "o3de/geom3do.h"
#include "platform/datafs.h"

#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: slider_body <gamedata>\n"); return 2; }
    const omk::DataFs fs(argv[1]);
    const auto d = fs.read("MESHES/MISC/SLI_FN.3DO");
    const auto h = omk::readHeader(d);
    if (!h) { std::printf("SLI_FN.3DO MISSING\n"); return 1; }
    const auto ms = omk::readMeshes(d, *h);
    const auto g = omk::buildGeometry(d, omk::DrawFilter{});
    if (g.cornerMesh.size() != g.corners.size()) { std::printf("no cornerMesh\n"); return 1; }
    // each mesh's root: follow parents (ids) until none
    auto rootOf = [&](int i) {
        int cur = i;
        for (int guard = 0; guard < 64; ++guard) {
            int p = -1;
            for (std::size_t k = 0; k < ms.size(); ++k)
                if (static_cast<int>(ms[k].id) == ms[(std::size_t)cur].parent && (int)k != cur) { p = (int)k; break; }
            if (p < 0) return cur;
            cur = p;
        }
        return cur;
    };
    std::printf("%zu meshes, %zu corners\n", ms.size(), g.corners.size());
    for (std::size_t r = 0; r < ms.size(); ++r) {
        if (rootOf((int)r) != (int)r) continue;
        float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
        std::size_t n = 0;
        for (std::size_t c = 0; c < g.corners.size(); ++c) {
            if (rootOf(g.cornerMesh[c]) != (int)r) continue;
            const float v[3] = {g.corners[c].x, g.corners[c].y, g.corners[c].z};
            for (int k = 0; k < 3; ++k) { if (v[k] < lo[k]) lo[k] = v[k]; if (v[k] > hi[k]) hi[k] = v[k]; }
            ++n;
        }
        if (!n) { std::printf("root %zu %-10s no corners\n", r, ms[r].name); continue; }
        const float* p = ms[r].pos;
        std::printf("root %zu %-10s pos %7.1f %7.1f %7.1f  box x %7.1f..%7.1f y %7.1f..%7.1f z %7.1f..%7.1f  (%zu corners)\n",
                    r, ms[r].name, p[0], p[1], p[2], lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], n);
        std::printf("       centre - pos = %7.1f %7.1f %7.1f   size %6.1f x %6.1f x %6.1f   centre - origin = %7.1f %7.1f %7.1f\n",
                    (lo[0] + hi[0]) / 2 - p[0], (lo[1] + hi[1]) / 2 - p[1], (lo[2] + hi[2]) / 2 - p[2],
                    hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2],
                    (lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2);
    }
    // ...and every mesh of the COCKPIT's subtree on its own, because the door
    // is two meshes (a hinge piece and a panel) and which of them the clip
    // must turn is decided by which one covers the seat.
    std::printf("-- per mesh:\n");
    for (std::size_t r = 0; r < ms.size(); ++r) {
        float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
        std::size_t n = 0;
        for (std::size_t c = 0; c < g.corners.size(); ++c) {
            if (g.cornerMesh[c] != (int)r) continue;
            const float v[3] = {g.corners[c].x, g.corners[c].y, g.corners[c].z};
            for (int k = 0; k < 3; ++k) { if (v[k] < lo[k]) lo[k] = v[k]; if (v[k] > hi[k]) hi[k] = v[k]; }
            ++n;
        }
        if (!n) continue;
        std::printf("   %zu %-10s id %2d parent %2d  box x %7.1f..%7.1f y %7.1f..%7.1f z %7.1f..%7.1f  size %5.1f x %5.1f x %5.1f  (%zu corners)\n",
                    r, ms[r].name, (int)ms[r].id, (int)ms[r].parent, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2],
                    hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], n);
    }
    return 0;
}
