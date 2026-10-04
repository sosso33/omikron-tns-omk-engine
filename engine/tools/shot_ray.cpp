// SPDX-License-Identifier: GPL-3.0-or-later
// WHAT STOPPED A BOLT (`todo/shoot-mode.md` 7i).
//
//     shot_ray <set.3DO> x0 y0 z0 x1 y1 z1
//     shot_ray --kinds <set.3DO>
//     shot_ray --mask <set.3DO>...
//
// `--mask` proves the SIGHT ray can run over the SHOT soup (`cutoutMask`,
// todo/ram-vs-original.md tier B): per set, the shot soup's unmasked
// triangles are the sight soup's, bitwise and in order, and 64 segments
// between the set's own faces hit the same face at the same t either way.
//
// `--kinds` measures the two world rays' soups against the render soup
// (`todo/shoot-sight.md` step 6): the triangles each keeps, the meshes `Shot`
// drops (0x41, `sub_444460`'s no-triangle-test bits) and `Sight` drops too
// (0x800, skipped by the engage's `sub_4449E0` only), and for every dropped
// mesh a 100-unit segment straight through its first triangle, cast in each
// soup - naming the mesh it meets, or `miss`.
//
// Casts the segment the way the viewer's world ray does - the RENDER soup
// (every mesh but those flagged 0x800000, `sub_444460`'s own skip) at radius
// 0 - and names the MESH it meets: its index, name and flags. Built for the
// question a play log cannot answer: every shot toward the supermarket's
// gunman stopped at the same z, and "the world" is not a reason.
#include "formats/mesh3do.h"
#include "o3de/collision.h"
#include "platform/datafs.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static int nearestMesh(const omk::TriangleSoup& soup, const std::vector<int>& meshOf,
                       const double a[3], const double dv[3]) {
    double bestT = 2.0;
    int best = -2;                                  // -2: nothing met
    for (std::size_t i = 0; i + 9 <= soup.size(); i += 9) {
        const omk::TriangleSoup one(soup.begin() + static_cast<long>(i),
                                    soup.begin() + static_cast<long>(i + 9));
        const auto hit = omk::sweepSphere(one, a, dv, 0.0);
        if (hit && hit->t < bestT) {
            bestT = hit->t;
            best = i / 9 < meshOf.size() ? meshOf[i / 9] : -1;
        }
    }
    return best;
}

static int kinds(const char* path) {
    const auto d = omk::DataFs::readPath(path);
    const auto h = omk::readHeader(d);
    const auto ms = h ? omk::readMeshes(d, *h) : std::vector<omk::Mesh>{};
    std::vector<int> mR, mS, mG;
    const auto render = omk::collisionSoup(d, omk::SoupKind::Render, &mR);
    const auto shot   = omk::collisionSoup(d, omk::SoupKind::Shot, &mS);
    const auto sight  = omk::collisionSoup(d, omk::SoupKind::Sight, &mG);
    std::printf("kinds: render %zu, shot %zu, sight %zu triangles\n",
                render.size() / 9, shot.size() / 9, sight.size() / 9);
    const auto name = [&](int mi) -> std::string {
        if (mi == -2) return "miss";
        if (mi < 0 || static_cast<std::size_t>(mi) >= ms.size()) return "?";
        return ms[static_cast<std::size_t>(mi)].name;
    };
    // every mesh the render soup has and one of the ray soups drops
    std::vector<int> seen;
    for (std::size_t t = 0; t < mR.size(); ++t) {
        const int mi = mR[t];
        bool had = false;
        for (int x : seen) had |= x == mi;
        if (had) continue;
        seen.push_back(mi);
        const auto fl = static_cast<std::uint32_t>(ms[static_cast<std::size_t>(mi)].flags);
        const bool inShot = (fl & 0x41u) == 0, inSight = inShot && (fl & 0x800u) == 0;
        if (inShot && inSight) continue;
        long tris = 0;
        for (int x : mR) tris += x == mi;
        // straight through its first triangle, 50 units each side
        const float* p = &render[t * 9];
        double e1[3], e2[3], n[3], c[3];
        for (int k = 0; k < 3; ++k) {
            e1[k] = double(p[3 + k]) - p[k];
            e2[k] = double(p[6 + k]) - p[k];
            c[k] = (double(p[k]) + p[3 + k] + p[6 + k]) / 3.0;
        }
        n[0] = e1[1] * e2[2] - e1[2] * e2[1];
        n[1] = e1[2] * e2[0] - e1[0] * e2[2];
        n[2] = e1[0] * e2[1] - e1[1] * e2[0];
        const double len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len <= 0.0) continue;
        double a[3], dv[3];
        for (int k = 0; k < 3; ++k) { a[k] = c[k] + 50.0 * n[k] / len; dv[k] = -100.0 * n[k] / len; }
        std::printf("dropped: mesh %d '%s' flags 0x%08x, %ld triangles, from %s - through it: "
                    "render %s, shot %s, sight %s\n", mi, name(mi).c_str(),
                    static_cast<unsigned>(fl), tris, inShot ? "sight" : "shot and sight",
                    name(nearestMesh(render, mR, a, dv)).c_str(),
                    name(nearestMesh(shot, mS, a, dv)).c_str(),
                    name(nearestMesh(sight, mG, a, dv)).c_str());
    }
    return 0;
}

static int mask(int argc, char** argv) {
    long sets = 0, tris = 0, masked = 0, rays = 0, hits = 0, badSoup = 0, badRay = 0;
    for (int k = 2; k < argc; ++k) {
        const auto d = omk::DataFs::readPath(argv[k]);
        std::vector<int> meshOf;
        const auto shot = omk::collisionSoup(d, omk::SoupKind::Shot, &meshOf);
        const auto sight = omk::collisionSoup(d, omk::SoupKind::Sight);
        const auto skip = omk::cutoutMask(d, meshOf);
        ++sets;
        tris += static_cast<long>(shot.size() / 9);
        // the unmasked shot triangles, in order, ARE the sight soup
        omk::TriangleSoup kept;
        for (std::size_t t = 0; t < skip.size(); ++t)
            if (!skip[t]) kept.insert(kept.end(), shot.begin() + static_cast<long>(9 * t),
                                      shot.begin() + static_cast<long>(9 * t + 9));
            else ++masked;
        if (skip.size() * 9 != shot.size() || kept != sight) { ++badSoup; continue; }
        if (shot.size() < 18) continue;
        // segments from one face's centre to another's, a fixed sequence
        std::uint32_t r = 12345u;
        const std::size_t n = shot.size() / 9;
        for (int i = 0; i < 64; ++i) {
            double a[3], b[3];
            for (double* p : {a, b}) {
                r = r * 1664525u + 1013904223u;
                const std::size_t t = (r >> 8) % n;
                for (int c = 0; c < 3; ++c)
                    p[c] = (shot[9 * t + c] + shot[9 * t + 3 + c] + shot[9 * t + 6 + c]) / 3.0;
            }
            const double dv[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
            const auto h1 = omk::sweepSphere(sight, a, dv, 0.0);
            const auto h2 = omk::sweepSphere(shot, skip, a, dv, 0.0);
            ++rays;
            if (h1) ++hits;
            if (bool(h1) != bool(h2) ||
                (h1 && (h1->t != h2->t || h1->n[0] != h2->n[0] || h1->n[1] != h2->n[1] ||
                        h1->n[2] != h2->n[2]))) ++badRay;
        }
    }
    std::printf("mask: %ld sets, %ld shot triangles, %ld masked as cutouts, %ld soups "
                "differ; %ld rays, %ld hit, %ld differ\n",
                sets, tris, masked, badSoup, rays, hits, badRay);
    return badSoup || badRay ? 1 : 0;
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--kinds") return kinds(argv[2]);
    if (argc >= 3 && std::string(argv[1]) == "--mask") return mask(argc, argv);
    if (argc < 8) {
        std::fprintf(stderr, "usage: shot_ray <set.3DO> x0 y0 z0 x1 y1 z1 | --kinds <set.3DO>\n");
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
