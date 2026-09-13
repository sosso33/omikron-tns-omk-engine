// SPDX-License-Identifier: GPL-3.0-or-later
// THE GROUND PROBE'S GRID GIVES THE LINEAR SCAN'S ANSWERS - bit for bit.
//
//     probe_grid <set.3DO>...
//
// `SoupGrid` (o3de/collision.h) exists to make the probes cheap and is held to
// changing no answer. This asks every question the viewer asks of a soup -
// `floorUnder`, `surfaceUnder`, `soupInBox` - of both the linear scan and the
// grid, over the walkable soup and the steep one, and counts every probe whose
// two answers differ in any bit: presence, height, normal, or the gathered box.
//
// The probes are chosen to find a wrong grid, not to be typical:
//   * the centroid of every sampled triangle, from above;
//   * its three VERTICES and three EDGE MIDPOINTS - points on a boundary
//     shared by two faces, where the first-hit rule decides;
//   * random points over (and a little past) the soup's extent;
//   * points EXACTLY on cell boundaries, where an off-by-one cell loses faces;
//   * random boxes for `soupInBox`, some straddling the extent.
// It also times both over the same probes, which is the point of the grid.
//
// One line a set a soup, then `mismatches <total>`. Prints only; writes nothing.
#include "o3de/collision.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct Lcg {
    std::uint64_t s = 0x2545F4914F6CDD1DULL;
    double next() {   // [0, 1)
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>(s >> 11) / 9007199254740992.0;
    }
};

bool sameD(double a, double b) { return std::memcmp(&a, &b, sizeof a) == 0; }

double ms(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

void runSoup(const std::string& stem, const char* kind, const omk::TriangleSoup& soup,
             long& totalMismatch) {
    using clk = std::chrono::steady_clock;
    const std::size_t n = soup.size() / 9;
    if (n == 0) { std::printf("%s %s empty\n", stem.c_str(), kind); return; }

    const auto b0 = clk::now();
    const omk::SoupGrid grid = omk::buildSoupGrid(soup);
    const auto b1 = clk::now();

    struct P { double x, y, z; };
    std::vector<P> probes;
    double ylo = 1e300, yhi = -1e300;
    for (std::size_t t = 0; t < n; ++t)
        for (int k = 0; k < 3; ++k) {
            ylo = std::min<double>(ylo, soup[9 * t + 3 * k + 1]);
            yhi = std::max<double>(yhi, soup[9 * t + 3 * k + 1]);
        }
    const std::size_t step = std::max<std::size_t>(1, n / 1500);
    for (std::size_t t = 0; t < n; t += step) {
        const float* v = &soup[9 * t];
        const double cy = (v[1] + v[4] + v[7]) / 3.0;
        probes.push_back({(v[0] + v[3] + v[6]) / 3.0, cy - 200.0, (v[2] + v[5] + v[8]) / 3.0});
        for (int k = 0; k < 3; ++k) {
            const float* a = &v[3 * k];
            const float* b = &v[3 * ((k + 1) % 3)];
            probes.push_back({a[0], a[1] - 50.0, a[2]});
            probes.push_back({(double(a[0]) + b[0]) * 0.5, (double(a[1]) + b[1]) * 0.5 - 50.0,
                              (double(a[2]) + b[2]) * 0.5});
        }
    }
    Lcg r;
    const double wx = grid.maxX - grid.minX, wz = grid.maxZ - grid.minZ;
    for (int i = 0; i < 3000; ++i)
        probes.push_back({grid.minX - 0.05 * wx + 1.1 * wx * r.next(),
                          ylo - 300.0 + (yhi - ylo + 350.0) * r.next(),
                          grid.minZ - 0.05 * wz + 1.1 * wz * r.next()});
    for (int i = 0; i < 1500; ++i) {   // exactly on cell boundaries
        const double bx = grid.minX + grid.cell * std::floor(r.next() * (grid.nx + 1));
        const double bz = grid.minZ + grid.cell * std::floor(r.next() * (grid.nz + 1));
        const double y = ylo - 300.0 + (yhi - ylo + 350.0) * r.next();
        probes.push_back({bx, y, grid.minZ + wz * r.next()});
        probes.push_back({grid.minX + wx * r.next(), y, bz});
        probes.push_back({bx, y, bz});
    }

    // floorUnder
    std::vector<std::optional<double>> fl(probes.size()), fg(probes.size());
    auto t0 = clk::now();
    for (std::size_t i = 0; i < probes.size(); ++i)
        fl[i] = omk::floorUnder(soup, probes[i].x, probes[i].y, probes[i].z);
    auto t1 = clk::now();
    for (std::size_t i = 0; i < probes.size(); ++i)
        fg[i] = omk::floorUnder(soup, grid, probes[i].x, probes[i].y, probes[i].z);
    auto t2 = clk::now();
    long fMis = 0, fHits = 0;
    for (std::size_t i = 0; i < probes.size(); ++i) {
        fHits += fl[i].has_value();
        if (fl[i].has_value() != fg[i].has_value() || (fl[i] && !sameD(*fl[i], *fg[i]))) ++fMis;
    }
    const double fLin = ms(t0, t1), fGrid = ms(t1, t2);

    // surfaceUnder
    std::vector<std::optional<omk::GroundHit>> sl(probes.size()), sg(probes.size());
    t0 = clk::now();
    for (std::size_t i = 0; i < probes.size(); ++i)
        sl[i] = omk::surfaceUnder(soup, probes[i].x, probes[i].y, probes[i].z);
    t1 = clk::now();
    for (std::size_t i = 0; i < probes.size(); ++i)
        sg[i] = omk::surfaceUnder(soup, grid, probes[i].x, probes[i].y, probes[i].z);
    t2 = clk::now();
    long sMis = 0;
    for (std::size_t i = 0; i < probes.size(); ++i) {
        if (sl[i].has_value() != sg[i].has_value()) { ++sMis; continue; }
        if (sl[i] && !(sameD(sl[i]->y, sg[i]->y) && sameD(sl[i]->n[0], sg[i]->n[0]) &&
                       sameD(sl[i]->n[1], sg[i]->n[1]) && sameD(sl[i]->n[2], sg[i]->n[2])))
            ++sMis;
    }
    const double sLin = ms(t0, t1), sGrid = ms(t1, t2);

    // soupInBox
    struct B { double x0, x1, z0, z1; };
    std::vector<B> boxes;
    for (int i = 0; i < 400; ++i) {
        const double cx = grid.minX - 0.05 * wx + 1.1 * wx * r.next();
        const double cz = grid.minZ - 0.05 * wz + 1.1 * wz * r.next();
        const double hx = 5.0 + 600.0 * r.next(), hz = 5.0 + 600.0 * r.next();
        boxes.push_back({cx - hx, cx + hx, cz - hz, cz + hz});
    }
    for (int i = 0; i < 100; ++i) {   // edges exactly on cell boundaries
        const double bx = grid.minX + grid.cell * std::floor(r.next() * (grid.nx + 1));
        const double bz = grid.minZ + grid.cell * std::floor(r.next() * (grid.nz + 1));
        boxes.push_back({bx, bx + grid.cell, bz, bz + grid.cell});
    }
    long bMis = 0;
    double bLin = 0, bGrid = 0;
    for (const auto& b : boxes) {
        const auto a0 = clk::now();
        const auto lin = omk::soupInBox(soup, b.x0, b.x1, b.z0, b.z1);
        const auto a1 = clk::now();
        const auto grd = omk::soupInBox(soup, grid, b.x0, b.x1, b.z0, b.z1);
        const auto a2 = clk::now();
        bLin += ms(a0, a1); bGrid += ms(a1, a2);
        if (lin.size() != grd.size() ||
            (!lin.empty() && std::memcmp(lin.data(), grd.data(), lin.size() * sizeof(float)) != 0))
            ++bMis;
    }

    std::printf("%s %s tris %zu grid %dx%d cell %.0f entries %zu build_ms %.2f | "
                "floor probes %zu hits %ld mismatches %ld lin_ms %.1f grid_ms %.2f | "
                "surface mismatches %ld lin_ms %.1f grid_ms %.2f | "
                "box boxes %zu mismatches %ld lin_ms %.1f grid_ms %.2f\n",
                stem.c_str(), kind, n, grid.nx, grid.nz, grid.cell, grid.index.size(), ms(b0, b1),
                probes.size(), fHits, fMis, fLin, fGrid, sMis, sLin, sGrid,
                boxes.size(), bMis, bLin, bGrid);
    totalMismatch += fMis + sMis + bMis;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: probe_grid <set.3DO>...\n");
        return 2;
    }
    long total = 0;
    for (int a = 1; a < argc; ++a) {
        std::ifstream f(argv[a], std::ios::binary);
        const std::vector<char> raw((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
        if (raw.empty()) { std::fprintf(stderr, "cannot read %s\n", argv[a]); return 1; }
        const std::span<const std::byte> d(reinterpret_cast<const std::byte*>(raw.data()), raw.size());
        std::string stem = argv[a];
        if (const auto s = stem.find_last_of("/\\"); s != std::string::npos) stem = stem.substr(s + 1);
        runSoup(stem, "walkable", omk::collisionSoup(d, omk::SoupKind::Walkable), total);
        runSoup(stem, "steep", omk::collisionSoup(d, omk::SoupKind::Steep), total);
    }
    std::printf("mismatches %ld\n", total);
    return total == 0 ? 0 : 3;
}
