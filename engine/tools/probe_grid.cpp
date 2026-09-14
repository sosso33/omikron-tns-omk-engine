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
#include "actor/walk.h"
#include "o3de/collision.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
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

    // THE SWEEPS (collision.h, step 11): `sweepSphere` through the two-layer
    // grids below against the linear scan. Random segments over and past the
    // extent - some vertical, some horizontal, some of zero length - at radius
    // 0, 12 and 40, and segments aimed from above each sampled triangle's centre
    // down and across it, so a good share of them HIT and the earliest-hit and
    // tie rules are exercised rather than a stream of misses.
    struct Sw { double p0[3], d[3], radius; };
    std::vector<Sw> sweeps;
    const double radii[3] = {0.0, 12.0, 40.0};
    for (int i = 0; i < 1500; ++i) {
        Sw s{};
        s.p0[0] = grid.minX - 0.05 * wx + 1.1 * wx * r.next();
        s.p0[1] = ylo - 300.0 + (yhi - ylo + 350.0) * r.next();
        s.p0[2] = grid.minZ - 0.05 * wz + 1.1 * wz * r.next();
        const int kind = i % 4;
        const double len = 600.0 * r.next();
        if (kind == 0) { s.d[1] = len; }                                   // straight down
        else if (kind == 1) { s.d[0] = len * (r.next() - 0.5); s.d[2] = len * (r.next() - 0.5); }
        else if (kind == 2) { /* zero length */ }
        else { s.d[0] = len * (r.next() - 0.5); s.d[1] = len * (r.next() - 0.5); s.d[2] = len * (r.next() - 0.5); }
        s.radius = radii[i % 3];
        sweeps.push_back(s);
    }
    for (std::size_t t = 0; t < n; t += step) {
        const float* v = &soup[9 * t];
        Sw s{};
        s.p0[0] = (v[0] + v[3] + v[6]) / 3.0 - 30.0;
        s.p0[1] = (v[1] + v[4] + v[7]) / 3.0 - 80.0;
        s.p0[2] = (v[2] + v[5] + v[8]) / 3.0 - 30.0;
        s.d[0] = 60.0; s.d[1] = 160.0; s.d[2] = 60.0;
        s.radius = radii[t % 3];
        sweeps.push_back(s);
    }
    std::vector<std::optional<omk::SweepHit>> sweepLin(sweeps.size());
    long sweepHits = 0, sweepMis = 0;
    double sweepLinMs = 0.0, sweepGridMs = 0.0;
    {
        const auto s0 = clk::now();
        for (std::size_t i = 0; i < sweeps.size(); ++i) {
            sweepLin[i] = omk::sweepSphere(soup, sweeps[i].p0, sweeps[i].d, sweeps[i].radius);
            sweepHits += sweepLin[i].has_value();
        }
        sweepLinMs = ms(s0, clk::now());
    }

    // THE TWO-LAYER GRID (collision.h, step 7c): the same probes and boxes
    // through a split into a `fixed` and a `moving` layer by a pseudo-random
    // mask (~1 in 16 moving), then through its complement, against the linear
    // answers above.
    long spMis = 0, spMoving = 0;
    for (int flip = 0; flip < 2; ++flip) {
        std::vector<std::uint8_t> moving(n), fixed(n);
        for (std::size_t t = 0; t < n; ++t) {
            const bool mv = (((static_cast<std::uint64_t>(t) + 17) * 2654435761ULL) >> 12) % 16 == 0;
            moving[t] = static_cast<std::uint8_t>(mv != (flip == 1));
            fixed[t] = static_cast<std::uint8_t>(!moving[t]);
            if (flip == 0) spMoving += moving[t];
        }
        omk::SplitSoupGrid split;
        split.fixed = omk::buildSoupGrid(soup, 256.0, &fixed);
        split.moving = omk::buildSoupGrid(soup, 256.0, &moving);
        // the id-list builder over the same triangles must give the same grid
        std::vector<std::uint32_t> ids;
        for (std::size_t t = 0; t < n; ++t) if (moving[t]) ids.push_back(static_cast<std::uint32_t>(t));
        const omk::SoupGrid byIds = omk::buildSoupGrid(soup, 256.0, std::span<const std::uint32_t>(ids));
        if (!(byIds.minX == split.moving.minX && byIds.maxX == split.moving.maxX &&
              byIds.minZ == split.moving.minZ && byIds.maxZ == split.moving.maxZ &&
              byIds.cell == split.moving.cell && byIds.nx == split.moving.nx &&
              byIds.nz == split.moving.nz && byIds.start == split.moving.start &&
              byIds.index == split.moving.index)) ++spMis;
        for (std::size_t i = 0; i < probes.size(); ++i) {
            const auto f = omk::floorUnder(soup, split, probes[i].x, probes[i].y, probes[i].z);
            if (f.has_value() != fl[i].has_value() || (f && !sameD(*f, *fl[i]))) ++spMis;
            const auto h = omk::surfaceUnder(soup, split, probes[i].x, probes[i].y, probes[i].z);
            if (h.has_value() != sl[i].has_value() ||
                (h && !(sameD(h->y, sl[i]->y) && sameD(h->n[0], sl[i]->n[0]) &&
                        sameD(h->n[1], sl[i]->n[1]) && sameD(h->n[2], sl[i]->n[2])))) ++spMis;
        }
        for (const auto& b : boxes) {
            const auto lin = omk::soupInBox(soup, b.x0, b.x1, b.z0, b.z1);
            const auto grd = omk::soupInBox(soup, split, b.x0, b.x1, b.z0, b.z1);
            if (lin.size() != grd.size() ||
                (!lin.empty() && std::memcmp(lin.data(), grd.data(), lin.size() * sizeof(float)) != 0))
                ++spMis;
        }
        const auto g0 = clk::now();
        for (std::size_t i = 0; i < sweeps.size(); ++i) {
            const auto h = omk::sweepSphere(soup, split, sweeps[i].p0, sweeps[i].d, sweeps[i].radius);
            if (h.has_value() != sweepLin[i].has_value() ||
                (h && std::memcmp(&*h, &*sweepLin[i], sizeof(omk::SweepHit)) != 0))
                ++sweepMis;
        }
        if (flip == 0) sweepGridMs = ms(g0, clk::now());
    }
    std::printf("split %s %s moving %ld | probes %zu boxes %zu (each twice) mismatches %ld\n",
                stem.c_str(), kind, spMoving, probes.size(), boxes.size(), spMis);
    std::printf("sweep %s %s | sweeps %zu hits %ld (each twice) mismatches %ld | lin_ms %.1f grid_ms %.2f\n",
                stem.c_str(), kind, sweeps.size(), sweepHits, sweepMis, sweepLinMs, sweepGridMs);
    totalMismatch += spMis + sweepMis;

    std::printf("%s %s tris %zu grid %dx%d cell %.0f entries %zu build_ms %.2f | "
                "floor probes %zu hits %ld mismatches %ld lin_ms %.1f grid_ms %.2f | "
                "surface mismatches %ld lin_ms %.1f grid_ms %.2f | "
                "box boxes %zu mismatches %ld lin_ms %.1f grid_ms %.2f\n",
                stem.c_str(), kind, n, grid.nx, grid.nz, grid.cell, grid.index.size(), ms(b0, b1),
                probes.size(), fHits, fMis, fLin, fGrid, sMis, sLin, sGrid,
                boxes.size(), bMis, bLin, bGrid);
    totalMismatch += fMis + sMis + bMis;
}

// THE DECOR UNDER THE FEET THROUGH A MERGED SOUP (actor/walk.h, step 9): two
// decors' walkable soups concatenated in order, as the viewer builds the
// player's, asked `decorUnder` through the merged two-layer grid and by the
// per-decor loop, over every sampled triangle's centre of both from above
// (with its vertices and edge midpoints) and random points over the union.
// Three cases: two different sets (the decor must be NAMED right), the same set
// twice (every floor TIES, and the earliest decor must win), and a decor with
// no area (the fallback). One `decor` row each.
void runDecors(const std::string& name, const omk::TriangleSoup& a, int areaA,
               const omk::TriangleSoup& b, int areaB, long& totalMismatch) {
    omk::TriangleSoup merged(a);
    merged.insert(merged.end(), b.begin(), b.end());
    const std::size_t n = merged.size() / 9;
    if (n == 0) { std::printf("decor %s empty\n", name.c_str()); return; }
    const std::vector<omk::DecorSoup> decors = {{areaA, &a}, {areaB, &b}};
    std::vector<std::uint8_t> moving(n), fixed(n);
    for (std::size_t t = 0; t < n; ++t) {
        moving[t] = (((static_cast<std::uint64_t>(t) + 17) * 2654435761ULL) >> 12) % 16 == 0;
        fixed[t] = !moving[t];
    }
    omk::SplitSoupGrid grid;
    grid.fixed = omk::buildSoupGrid(merged, 256.0, &fixed);
    grid.moving = omk::buildSoupGrid(merged, 256.0, &moving);

    struct P { double x, y, z; };
    std::vector<P> probes;
    const std::size_t step = std::max<std::size_t>(1, n / 3000);
    for (std::size_t t = 0; t < n; t += step) {
        const float* v = &merged[9 * t];
        probes.push_back({(v[0] + v[3] + v[6]) / 3.0, (v[1] + v[4] + v[7]) / 3.0 - 100.0,
                          (v[2] + v[5] + v[8]) / 3.0});
        for (int k = 0; k < 3; ++k) {
            const float* p = &v[3 * k];
            const float* q = &v[3 * ((k + 1) % 3)];
            probes.push_back({p[0], p[1] - 60.0, p[2]});
            probes.push_back({(double(p[0]) + q[0]) * 0.5, (double(p[1]) + q[1]) * 0.5 - 60.0,
                              (double(p[2]) + q[2]) * 0.5});
        }
    }
    const omk::SoupGrid& fx = grid.fixed;
    Lcg r;
    for (int i = 0; i < 4000; ++i)
        probes.push_back({fx.minX + (fx.maxX - fx.minX) * r.next(), -3000.0 + 6000.0 * r.next(),
                          fx.minZ + (fx.maxZ - fx.minZ) * r.next()});
    long first = 0, second = 0, none = 0, mis = 0;
    for (const auto& p : probes) {
        const int lin = omk::decorUnder(decors, p.x, p.y, p.z);
        const int grd = omk::decorUnder(decors, merged, grid, p.x, p.y, p.z);
        if (lin != grd) ++mis;
        if (lin < 0) ++none;
        else if (lin == areaA) ++first;
        else ++second;
    }
    std::printf("decor %s | probes %zu first %ld second %ld none %ld mismatches %ld\n",
                name.c_str(), probes.size(), first, second, none, mis);
    totalMismatch += mis;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: probe_grid [--decor] <set.3DO>...\n");
        return 2;
    }
    // `--decor`: only the decor rows, over the first two sets
    const bool decorOnly = std::strcmp(argv[1], "--decor") == 0;
    long total = 0;
    std::vector<omk::TriangleSoup> walkable;
    std::vector<std::string> stems;
    for (int a = decorOnly ? 2 : 1; a < argc; ++a) {
        std::ifstream f(argv[a], std::ios::binary);
        const std::vector<char> raw((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
        if (raw.empty()) { std::fprintf(stderr, "cannot read %s\n", argv[a]); return 1; }
        const std::span<const std::byte> d(reinterpret_cast<const std::byte*>(raw.data()), raw.size());
        std::string stem = argv[a];
        if (const auto s = stem.find_last_of("/\\"); s != std::string::npos) stem = stem.substr(s + 1);
        if (const auto s = stem.find('.'); s != std::string::npos) stem = stem.substr(0, s);
        walkable.push_back(omk::collisionSoup(d, omk::SoupKind::Walkable));
        stems.push_back(stem);
        if (decorOnly) continue;
        runSoup(stem + ".3DO", "walkable", walkable.back(), total);
        runSoup(stem + ".3DO", "steep", omk::collisionSoup(d, omk::SoupKind::Steep), total);
    }
    if (walkable.size() >= 2) {
        runDecors(stems[0] + "+" + stems[1], walkable[0], 1, walkable[1], 2, total);
        runDecors(stems[0] + "+" + stems[0], walkable[0], 1, walkable[0], 2, total);
        runDecors(stems[0] + "+" + stems[1] + "-noarea", walkable[0], -1, walkable[1], 2, total);
    }
    std::printf("mismatches %ld\n", total);
    return total == 0 ? 0 : 3;
}
