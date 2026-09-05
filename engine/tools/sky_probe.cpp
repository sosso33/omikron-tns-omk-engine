// SPDX-License-Identifier: GPL-3.0-or-later
// The SKY model: its geometry, its one texture, and the mesh's own name.
//
//     sky_probe <MESHES/MISC/xsky.3DO>...
//
// Prints, per model:  <path> corners batches meshes ylo yhi xlo xhi zlo zhi
//                     meshname flags texname texw texh
//
// `Area_LoadMiscModel` loads one of these out of the AREA chunk's +133 and
// makes it the sky.  `verify.py: the sky` drives this.
#include "platform/datafs.h"
#include "o3de/geom3do.h"
#include "formats/mesh3do.h"
#include "formats/tex3dt.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string path = argv[i];
        const auto d = omk::DataFs::readPath(path);
        if (d.empty()) { std::printf("%s unreadable\n", path.c_str()); continue; }
        const auto g = omk::buildGeometry(d, omk::DrawFilter::Engine);
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (const auto& c : g.corners) {
            const float p[3] = {c.x, c.y, c.z};
            for (int k = 0; k < 3; ++k) {
                if (p[k] < lo[k]) lo[k] = p[k];
                if (p[k] > hi[k]) hi[k] = p[k];
            }
        }
        const auto hdr = omk::readHeader(d);
        std::string mesh = "?";
        long flags = 0;
        std::size_t nmesh = 0;
        if (hdr) {
            const auto ms = omk::readMeshes(d, *hdr);
            nmesh = ms.size();
            if (!ms.empty()) { mesh = ms[0].name; flags = ms[0].flags; }
        }
        // its texture, out of the .3DT beside it
        std::string tname = "-";
        int tw = 0, th = 0;
        const auto dot = path.rfind('.');
        if (dot != std::string::npos) {
            const auto td = omk::DataFs::readPath(path.substr(0, dot) + ".3DT");
            if (!td.empty()) {
                const auto tx = omk::textures(d, td);
                if (!tx.empty()) {
                    tname = tx[0].name;
                    tw = tx[0].width;
                    th = tx[0].height;
                }
            }
        }
        std::printf("%s %zu %zu %zu %.2f %.2f %.2f %.2f %.2f %.2f %s 0x%08lX %s %d %d\n",
                    path.c_str(), g.corners.size(), g.batches.size(), nmesh,
                    lo[1], hi[1], lo[0], hi[0], lo[2], hi[2],
                    mesh.c_str(), flags, tname.c_str(), tw, th);
    }
    return 0;
}
