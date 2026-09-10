// SPDX-License-Identifier: GPL-3.0-or-later
// WHAT STOPPED A BOLT (`todo/shoot-mode.md` 7i).
//
//     shot_ray <set.3DO> x0 y0 z0 x1 y1 z1
//
// Casts the segment the way the viewer's world ray does - the RENDER soup
// (every mesh but those flagged 0x800000, `sub_444460`'s own skip) at radius
// 0 - and names the MESH it meets: its index, name and flags. Built for the
// question a play log cannot answer: every shot toward the supermarket's
// gunman stopped at the same z, and "the world" is not a reason.
#include "formats/mesh3do.h"
#include "o3de/collision.h"
#include "platform/datafs.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 8) {
        std::fprintf(stderr, "usage: shot_ray <set.3DO> x0 y0 z0 x1 y1 z1\n");
        return 2;
    }
    const auto d = omk::DataFs::readPath(argv[1]);
    std::vector<int> meshOf;
    const omk::TriangleSoup soup = omk::collisionSoup(d, omk::SoupKind::Render, &meshOf);
    const auto h = omk::readHeader(d);
    const auto ms = h ? omk::readMeshes(d, *h) : std::vector<omk::Mesh>{};
    const double a[3] = {std::atof(argv[2]), std::atof(argv[3]), std::atof(argv[4])};
    const double b[3] = {std::atof(argv[5]), std::atof(argv[6]), std::atof(argv[7])};
    const double dv[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    std::printf("soup: %zu triangles, %zu meshes\n", soup.size() / 9, ms.size());
    // the nearest hit, and which triangle gave it
    double bestT = 2.0;
    std::size_t bestTri = 0;
    for (std::size_t i = 0; i + 9 <= soup.size(); i += 9) {
        const omk::TriangleSoup one(soup.begin() + static_cast<long>(i),
                                    soup.begin() + static_cast<long>(i + 9));
        const auto hit = omk::sweepSphere(one, a, dv, 0.0);
        if (hit && hit->t < bestT) { bestT = hit->t; bestTri = i / 9; }
    }
    if (bestT > 1.0) { std::printf("miss\n"); return 0; }
    const int mi = bestTri < meshOf.size() ? meshOf[bestTri] : -1;
    std::printf("hit at t %.4f: %.1f %.1f %.1f - triangle %zu, mesh %d", bestT,
                a[0] + bestT * dv[0], a[1] + bestT * dv[1], a[2] + bestT * dv[2], bestTri, mi);
    if (mi >= 0 && static_cast<std::size_t>(mi) < ms.size()) {
        const auto& m = ms[static_cast<std::size_t>(mi)];
        std::printf(" '%s' flags 0x%08x at %.1f %.1f %.1f", m.name,
                    static_cast<unsigned>(m.flags), double(m.pos[0]), double(m.pos[1]),
                    double(m.pos[2]));
    }
    std::printf("\n");
    return 0;
}
