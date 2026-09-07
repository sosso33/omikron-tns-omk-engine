// SPDX-License-Identifier: GPL-3.0-or-later
// NEAR-DEGENERATE TRIANGLES in the collision soups, and what they do to a sweep.
//
//     sliver_probe <gamedata> [set ...]
//
// `collisionSoup` keeps a face when its cross product is non-zero
// (`if (n2 <= 0) return;`), which is a test against exact zero and not against
// degeneracy. A triangle whose three vertices are collinear to within float
// noise survives it with a normal that is pure noise - and a sweep that hits
// one gets a meaningless normal, which `Walk_ClampNormal` can then collapse,
// zeroing the move: the actor stops dead with nothing in front of him.
//
// This counts them per set, by the area the cross product implies.
#include "formats/mesh3do.h"
#include "o3de/collision.h"
#include "platform/datafs.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: sliver_probe <gamedata> [set ...]\n"); return 2; }
    const std::string fr = argv[1];
    std::vector<std::string> sets;
    for (int i = 2; i < argc; ++i) sets.push_back(argv[i]);
    if (sets.empty()) sets = {"Anekbah","Abank","AToit","AIMPASSE","AAPKAYL","Lahoreh",
                              "Sconcert","Sprison","ARESTO14","Qalisar"};
    long allTris = 0, allSlivers = 0;
    for (const auto& s : sets) {
        const auto d = omk::DataFs::readPath(fr + "/MESHES/DECORS/" + s + ".3DO");
        if (d.empty()) continue;
        for (const auto kind : {omk::SoupKind::Walkable, omk::SoupKind::Steep}) {
            const auto soup = omk::collisionSoup(d, kind);
            long n = 0, sliver = 0;
            double worst = 1e30;
            for (std::size_t i = 0; i + 8 < soup.size(); i += 9) {
                ++n;
                const double ux = soup[i+3]-soup[i],   uy = soup[i+4]-soup[i+1], uz = soup[i+5]-soup[i+2];
                const double vx = soup[i+6]-soup[i],   vy = soup[i+7]-soup[i+1], vz = soup[i+8]-soup[i+2];
                const double nx = uy*vz-uz*vy, ny = uz*vx-ux*vz, nz = ux*vy-uy*vx;
                const double area = std::sqrt(nx*nx+ny*ny+nz*nz) / 2.0;
                if (area < 0.5) { ++sliver; if (area < worst) worst = area; }
            }
            allTris += n; allSlivers += sliver;
            std::printf("%-10s %-9s %6ld tris, %4ld with area < 0.5%s\n",
                        s.c_str(), kind == omk::SoupKind::Walkable ? "walkable" : "steep",
                        n, sliver, sliver ? "" : "");
        }
    }
    std::printf("\n%ld of %ld collision triangles are near-degenerate\n", allSlivers, allTris);
    return 0;
}
