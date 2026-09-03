// SPDX-License-Identifier: GPL-3.0-or-later
// A prop model's own coordinate range. `buildGeometry` bakes a mesh's `pos`
// into its corners for a decor set, and if it does the same here then the
// placement must not be added on top of it.
//
//     prop_extent <gamedata> <STEM>
#include "formats/mesh3do.h"
#include "o3de/geom3do.h"
#include "platform/datafs.h"
#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    const omk::DataFs fs(argv[1]);
    const auto p = fs.resolve(std::string("MESHES/OBJETS/") + argv[2] + ".3DO");
    if (!p) { std::fprintf(stderr, "no model\n"); return 1; }
    const auto d = omk::DataFs::readPath(*p);
    const auto g = omk::buildGeometry(d, omk::DrawFilter::Engine);
    float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
    for (const auto& c : g.corners) {
        const float v[3] = {c.x, c.y, c.z};
        for (int k = 0; k < 3; ++k) { if (v[k] < lo[k]) lo[k] = v[k]; if (v[k] > hi[k]) hi[k] = v[k]; }
    }
    std::printf("%s: %zu corners  x %.1f..%.1f  y %.1f..%.1f  z %.1f..%.1f\n",
                argv[2], g.corners.size(), lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);
    if (const auto hd = omk::readHeader(d)) {
        const auto ms = omk::readMeshes(d, *hd);
        for (const auto& m : ms)
            std::printf("  mesh '%s' pos %.1f %.1f %.1f  flags %08X\n",
                        m.name, m.pos[0], m.pos[1], m.pos[2],
                        static_cast<unsigned>(m.flags));
    }
    return 0;
}
