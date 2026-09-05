// SPDX-License-Identifier: GPL-3.0-or-later
//
// List a set's collision triangles (walkable or steep) near a point, with the
// mesh each came from - for looking at a floor's edge where a walker stops.
//
//     soup_dump <model.3DO> walkable|steep <x> <z> <radius> [y]
//
// With `y`, also reports `floorUnder(x, y - kStepUp - 1, z)` - the walker's own
// ground probe - at that point.
#include "o3de/collision.h"
#include "actor/walk.h"
#include "formats/mesh3do.h"
#include "platform/datafs.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    if (argc < 6) { std::fprintf(stderr, "usage: soup_dump <model.3DO> walkable|steep <x> <z> <radius>\n"); return 2; }
    const auto d = omk::DataFs::readPath(argv[1]);
    const auto kind = !std::strcmp(argv[2], "steep") ? omk::SoupKind::Steep : omk::SoupKind::Walkable;
    const double x = std::atof(argv[3]), z = std::atof(argv[4]), r = std::atof(argv[5]);
    std::vector<int> meshOf;
    const auto soup = omk::collisionSoup(d, kind, &meshOf);
    const auto h = omk::readHeader(d);
    const auto ms = h ? omk::readMeshes(d, *h) : std::vector<omk::Mesh>{};
    int n = 0;
    for (std::size_t t = 0; t + 9 <= soup.size(); t += 9) {
        bool near = false;
        for (int v = 0; v < 3; ++v) {
            const double dx = soup[t + 3 * v] - x, dz = soup[t + 3 * v + 2] - z;
            if (std::sqrt(dx * dx + dz * dz) <= r) near = true;
        }
        if (!near) continue;
        const int mi = t / 9 < meshOf.size() ? meshOf[t / 9] : -1;
        const char* name = (mi >= 0 && static_cast<std::size_t>(mi) < ms.size()) ? ms[static_cast<std::size_t>(mi)].name : "?";
        std::printf("tri %3zu mesh %3d %-16s (%7.1f %7.1f %7.1f) (%7.1f %7.1f %7.1f) (%7.1f %7.1f %7.1f)\n",
                    t / 9, mi, name, soup[t], soup[t + 1], soup[t + 2], soup[t + 3], soup[t + 4], soup[t + 5],
                    soup[t + 6], soup[t + 7], soup[t + 8]);
        ++n;
    }
    if (argc > 6) {
        const double y = std::atof(argv[6]);
        const auto g = omk::floorUnder(soup, x, y - omk::kStepUp - 1.0, z);
        if (g) std::printf("floorUnder(%.1f, %.1f - %.1f - 1, %.1f) = %.3f\n", x, y, omk::kStepUp, z, *g);
        else   std::printf("floorUnder(%.1f, %.1f - %.1f - 1, %.1f) = none\n", x, y, omk::kStepUp, z);
    }
    std::printf("%d triangles within %.0f of (%.0f, %.0f) in the %s soup (%zu in all)\n", n, r, x, z, argv[2], soup.size() / 9);
    return 0;
}
