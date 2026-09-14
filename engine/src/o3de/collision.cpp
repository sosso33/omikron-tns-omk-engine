// SPDX-License-Identifier: GPL-3.0-or-later
#include "o3de/collision.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace omk {

TriangleSoup collisionSoup(std::span<const std::byte> d, SoupKind kind,
                           std::vector<int>* meshOf) {
    TriangleSoup out;
    if (meshOf) meshOf->clear();
    int curMesh = -1;
    const auto header = readHeader(d);
    if (!header) return out;
    const auto ms = readMeshes(d, *header);
    const auto vs = readVertices(d, *header);
    const auto tris = readTriangles(d, *header);
    const auto quads = readQuads(d, *header);
    if (ms.empty()) return out;

    std::vector<std::size_t> basev(ms.size()), baset(ms.size()), baseq(ms.size());
    std::size_t av = 0, at = 0, aq = 0;
    for (std::size_t i = 0; i < ms.size(); ++i) {
        basev[i] = av; baset[i] = at; baseq[i] = aq;
        av += static_cast<std::size_t>(std::max(0, ms[i].vertices));
        at += static_cast<std::size_t>(std::max(0, ms[i].triangles));
        aq += static_cast<std::size_t>(std::max(0, ms[i].quads));
    }
    std::map<std::int32_t, std::size_t> byId;
    for (std::size_t i = 0; i < ms.size(); ++i) byId.emplace(ms[i].id, i);

    const auto ancestorFor = [&](const Mesh& m, std::size_t k) -> const Mesh* {
        auto it = byId.find(m.parent);
        for (int guard = 0; it != byId.end() && guard < 16; ++guard) {
            const Mesh& cur = ms[it->second];
            if (cur.vertices > 3 && k < static_cast<std::size_t>(cur.vertices))
                return &cur;
            it = byId.find(cur.parent);
        }
        return nullptr;
    };

    struct P { double x, y, z; bool ok; };
    const auto resolve = [&](const Mesh& m, std::int16_t i) -> P {
        std::size_t gi;
        const float* off;
        if (i < 0) {
            const auto k = static_cast<std::size_t>(i & 0x7FFF);
            const Mesh* anc = ancestorFor(m, k);
            if (!anc) return {0, 0, 0, false};
            gi = basev[static_cast<std::size_t>(anc->index)] + k;
            off = anc->pos;
        } else {
            gi = basev[static_cast<std::size_t>(m.index)] + static_cast<std::size_t>(i);
            off = m.pos;
        }
        if (gi >= vs.size()) return {0, 0, 0, false};
        return {vs[gi].p[0] + off[0], vs[gi].p[1] + off[1], vs[gi].p[2] + off[2], true};
    };

    const auto emit = [&](const P& a, const P& b, const P& c) {
        const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
        const double vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
        const double nx = uy * vz - uz * vy;
        const double ny = uz * vx - ux * vz;
        const double nz = ux * vy - uy * vx;
        const double n2 = nx * nx + ny * ny + nz * nz;
        // DEGENERATE FACES ARE DROPPED, and the test is against an area rather
        // than against exact zero. `n2 <= 0` only catches a cross product that
        // cancels bit for bit; a triangle whose three vertices are collinear
        // to within float noise survives it carrying a normal that IS that
        // noise. Nothing can legitimately hit a face of no area - but this
        // port's own sweep can, and then `Walk_ClampNormal` collapses the
        // meaningless normal and `slide` zeroes the move: the actor stops dead
        // with nothing in front of him.
        //
        // That is what blocked the player on the LAST step of the bank's
        // entrance stairs in Anekbah - a sliver at x 4566.9 whose three
        // vertices print identically to a decimal (`todo/next-tasks.md` 20).
        // 102 of the 143325 collision triangles over ten sets are like it.
        //
        // **AND 102 IS THE MOTIVATING CASE, NOT THIS TEST'S REACH** - measured
        // 2026-09-09, because the two were read as the same number. Those 102
        // are the EXACTLY degenerate ones; the threshold below reaches every
        // face under half a square inch, which in `ARESTO14` alone is **60 of
        // 2875** render triangles and **40 of 1107** walkable ones, 2% and 4%.
        // The walk is unchanged by it - the walker still moves 287 and reverts
        // 113, the same as `tools/sim` - so the faces being dropped carry no
        // verdict; but the reach is thirty times what the sentence above
        // suggests, and a threshold nobody has measured is a threshold nobody
        // can defend. `engine: walk` and `engine: walker falls` pin both soup
        // sizes, and putting this back to `n2 <= 0` restores 2875 and 1107.
        //
        // This is the port's own numerical hygiene and NOT a ported decision:
        // `Sweep_PolygonKernel` is 930 lines of x87 and whether it rejects a
        // degenerate face is unread. |n| is twice the area, so this drops
        // anything under half a square inch.
        if (n2 <= 1.0) return;
        const double slope = std::fabs(ny) / std::sqrt(n2);
        if (kind == SoupKind::Walkable && slope < kSlopeCos30) return;
        if (kind == SoupKind::Steep    && slope >= kSlopeCos30) return;
        if (meshOf) meshOf->push_back(curMesh);
        for (const P* p : {&a, &b, &c}) {
            out.push_back(static_cast<float>(p->x));
            out.push_back(static_cast<float>(p->y));
            out.push_back(static_cast<float>(p->z));
        }
    };

    for (const auto& m : ms) {
        // the render soup drops CollisionOnly, the two collision soups keep it
        if (kind == SoupKind::Render &&
            (static_cast<std::uint32_t>(m.flags) & 0x800000u)) continue;
        // the two world rays' soups (`sub_444460`, see `SoupKind`)
        if ((kind == SoupKind::Shot || kind == SoupKind::Sight) &&
            (static_cast<std::uint32_t>(m.flags) & (0x800000u | 0x41u))) continue;
        if (kind == SoupKind::Sight && (static_cast<std::uint32_t>(m.flags) & 0x800u)) continue;
        // ...AND THE ENGINE'S OWN MESH EXCLUSIONS, which are two different
        // tests read out of two different functions (2026-09-07):
        //
        //   the SWEEP   `Sweep_MeshTest` (0x004AD460) opens
        //               `if ((flags & 0x20000000) == 0 && (flags & 0x41) == 0)`
        //               - so a mesh carrying either bit is never swept against.
        //   the STEP    the mover's refusal (21_d3d.c:2644) is
        //               `rise > 11.811 || cos(30 deg) > -n || (flags & 0x20000000)`
        //               - a THIRD arm this port does not have. It is NOT
        //               applied here, and the reason is measured: that arm
        //               sits in a PUSH-BACK branch, not a "you may not stand
        //               here" one, so dropping those meshes from the walkable
        //               soup is a different rule - it takes **3606 of
        //               Lahoreh's 17658** floor triangles away, a fifth of the
        //               city's floor. Porting it properly means carrying the
        //               flag per triangle into the walker's step test, which
        //               is the open half of `todo/next-tasks.md` 20.
        //
        // Both are exclusions, which is the opposite of how the note below
        // first read: a filter that ADMITTED only those bits would keep 0-4%
        // of a set's meshes and let the player walk through the world.
        const auto mf = static_cast<std::uint32_t>(m.flags);
        if (kind == SoupKind::Steep && (mf & (0x20000000u | 0x41u))) continue;
        const auto mi = static_cast<std::size_t>(m.index);
        curMesh = m.index;
        for (std::size_t t = baset[mi];
             t < baset[mi] + static_cast<std::size_t>(std::max(0, m.triangles))
             && t < tris.size(); ++t) {
            const P a = resolve(m, tris[t].idx[0]);
            const P b = resolve(m, tris[t].idx[1]);
            const P c = resolve(m, tris[t].idx[2]);
            if (a.ok && b.ok && c.ok) emit(a, b, c);
        }
        for (std::size_t q = baseq[mi];
             q < baseq[mi] + static_cast<std::size_t>(std::max(0, m.quads))
             && q < quads.size(); ++q) {
            const P p[4] = {resolve(m, quads[q].idx[0]), resolve(m, quads[q].idx[1]),
                            resolve(m, quads[q].idx[2]), resolve(m, quads[q].idx[3])};
            if (p[0].ok && p[1].ok && p[2].ok && p[3].ok) {
                emit(p[0], p[1], p[2]);
                emit(p[0], p[2], p[3]);
            }
        }
    }
    return out;
}

std::optional<double> floorUnder(const TriangleSoup& tris, double x, double y,
                                 double z) {
    std::optional<double> best;
    for (std::size_t t = 0; t + 9 <= tris.size(); t += 9) {
        const double ax = tris[t],     ay = tris[t + 1], az = tris[t + 2];
        const double bx = tris[t + 3], by = tris[t + 4], bz = tris[t + 5];
        const double cx = tris[t + 6], cy = tris[t + 7], cz = tris[t + 8];
        const double d = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
        if (std::fabs(d) < 1e-9) continue;
        const double w0 = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / d;
        const double w1 = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / d;
        const double w2 = 1.0 - w0 - w1;
        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
        const double hit = w0 * ay + w1 * by + w2 * cy;
        // "below" is a LARGER y, and strictly below the origin
        if (hit > y + 1.0 && (!best || hit < *best)) best = hit;
    }
    return best;
}

TriangleSoup soupInBox(const TriangleSoup& tris, double minX, double maxX,
                       double minZ, double maxZ) {
    TriangleSoup out;
    for (std::size_t t = 0; t + 9 <= tris.size(); t += 9) {
        double lo[2] = {tris[t], tris[t + 2]}, hi[2] = {tris[t], tris[t + 2]};
        for (int k = 1; k < 3; ++k) {
            const double px = tris[t + 3 * k], pz = tris[t + 3 * k + 2];
            lo[0] = std::min(lo[0], px); hi[0] = std::max(hi[0], px);
            lo[1] = std::min(lo[1], pz); hi[1] = std::max(hi[1], pz);
        }
        if (hi[0] < minX || lo[0] > maxX || hi[1] < minZ || lo[1] > maxZ) continue;
        out.insert(out.end(), tris.begin() + static_cast<long>(t),
                   tris.begin() + static_cast<long>(t) + 9);
    }
    return out;
}

std::optional<GroundHit> surfaceUnder(const TriangleSoup& tris, double x,
                                      double y, double z) {
    std::optional<GroundHit> best;
    for (std::size_t t = 0; t + 9 <= tris.size(); t += 9) {
        const double ax = tris[t],     ay = tris[t + 1], az = tris[t + 2];
        const double bx = tris[t + 3], by = tris[t + 4], bz = tris[t + 5];
        const double cx = tris[t + 6], cy = tris[t + 7], cz = tris[t + 8];
        const double d = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
        if (std::fabs(d) < 1e-9) continue;
        const double w0 = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / d;
        const double w1 = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / d;
        const double w2 = 1.0 - w0 - w1;
        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
        const double hit = w0 * ay + w1 * by + w2 * cy;
        if (!(hit > y + 1.0) || (best && hit >= best->y)) continue;
        const double ux = bx - ax, uy = by - ay, uz = bz - az;
        const double vx = cx - ax, vy = cy - ay, vz = cz - az;
        double nx = uy * vz - uz * vy;
        double ny = uz * vx - ux * vz;
        double nz = ux * vy - uy * vx;
        const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len <= 0) continue;
        nx /= len; ny /= len; nz /= len;
        // Point it UP, which with Y growing downward means a negative y - the
        // orientation `Walk_GroundResponse`'s `cos(30) > -normal.y` reads.
        if (ny > 0) { nx = -nx; ny = -ny; nz = -nz; }
        best = GroundHit{hit, {nx, ny, nz}};
    }
    return best;
}

// ---------------------------------------------------- THE PROBE'S BROAD PHASE
//
// See `SoupGrid` in collision.h. The two per-triangle kernels below are the
// loop bodies of `floorUnder` and `surfaceUnder` above, copied statement for
// statement rather than shared, so the linear scans stay byte-identical to what
// they were and `probe_grid` compares the grid against the code as it stood.

namespace {

// `floorUnder`'s loop body: the XZ barycentric test and the interpolated height.
inline bool underKernel(const TriangleSoup& tris, std::size_t t, double x, double z,
                        double& hit) {
    const double ax = tris[t],     ay = tris[t + 1], az = tris[t + 2];
    const double bx = tris[t + 3], by = tris[t + 4], bz = tris[t + 5];
    const double cx = tris[t + 6], cy = tris[t + 7], cz = tris[t + 8];
    const double d = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
    if (std::fabs(d) < 1e-9) return false;
    const double w0 = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / d;
    const double w1 = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / d;
    const double w2 = 1.0 - w0 - w1;
    if (w0 < 0 || w1 < 0 || w2 < 0) return false;
    hit = w0 * ay + w1 * by + w2 * cy;
    return true;
}

// `surfaceUnder`'s normal, pointed up.
inline bool upNormal(const TriangleSoup& tris, std::size_t t, double n[3]) {
    const double ax = tris[t],     ay = tris[t + 1], az = tris[t + 2];
    const double bx = tris[t + 3], by = tris[t + 4], bz = tris[t + 5];
    const double cx = tris[t + 6], cy = tris[t + 7], cz = tris[t + 8];
    const double ux = bx - ax, uy = by - ay, uz = bz - az;
    const double vx = cx - ax, vy = cy - ay, vz = cz - az;
    double nx = uy * vz - uz * vy;
    double ny = uz * vx - ux * vz;
    double nz = ux * vy - uy * vx;
    const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len <= 0) return false;
    nx /= len; ny /= len; nz /= len;
    if (ny > 0) { nx = -nx; ny = -ny; nz = -nz; }
    n[0] = nx; n[1] = ny; n[2] = nz;
    return true;
}

// The cell column/row a coordinate falls in, clamped to the grid; -1 when the
// coordinate is outside the grid's inflated extent (or NaN), where no triangle
// can contain it.
inline int cellAxis(double v, double lo, double hi, double cell, int n) {
    if (!(v >= lo - kGridEps && v <= hi + kGridEps)) return -1;
    int i = static_cast<int>(std::floor((v - lo) / cell));
    return std::clamp(i, 0, n - 1);
}

}  // namespace

namespace {

// THE ONE BUILD BODY. `forEach(f)` calls `f(t)` for every triangle to include,
// in ASCENDING order - the mask builder walks the whole soup testing its mask,
// the id builder walks its list - so a grid over the same triangles comes out
// the same from either.
template <class ForEach>
SoupGrid buildOver(const TriangleSoup& tris, double cell, ForEach forEach) {
    SoupGrid g;
    g.data = tris.data();
    g.size = tris.size();
    g.built = true;
    bool any = false;
    double lo[2] = {1e300, 1e300}, hi[2] = {-1e300, -1e300};
    forEach([&](std::size_t t) {
        any = true;
        for (int k = 0; k < 3; ++k) {
            const double x = tris[9 * t + 3 * k], z = tris[9 * t + 3 * k + 2];
            lo[0] = std::min(lo[0], x); hi[0] = std::max(hi[0], x);
            lo[1] = std::min(lo[1], z); hi[1] = std::max(hi[1], z);
        }
    });
    if (!any) return g;                       // built, and over nothing: nx == 0
    g.minX = lo[0]; g.maxX = hi[0]; g.minZ = lo[1]; g.maxZ = hi[1];
    const double span = std::max(hi[0] - lo[0], hi[1] - lo[1]);
    g.cell = std::max({cell, span / 512.0, 1.0});
    g.nx = static_cast<int>((hi[0] - lo[0]) / g.cell) + 1;
    g.nz = static_cast<int>((hi[1] - lo[1]) / g.cell) + 1;
    const auto range = [&](std::size_t t, int& i0, int& i1, int& k0, int& k1) {
        double a[2] = {tris[9 * t], tris[9 * t + 2]}, b[2] = {a[0], a[1]};
        for (int k = 1; k < 3; ++k) {
            a[0] = std::min<double>(a[0], tris[9 * t + 3 * k]);
            b[0] = std::max<double>(b[0], tris[9 * t + 3 * k]);
            a[1] = std::min<double>(a[1], tris[9 * t + 3 * k + 2]);
            b[1] = std::max<double>(b[1], tris[9 * t + 3 * k + 2]);
        }
        const auto axis = [&](double v, double base, int cells) {
            return std::clamp(static_cast<int>(std::floor((v - base) / g.cell)), 0, cells - 1);
        };
        i0 = axis(a[0] - kGridEps, g.minX, g.nx); i1 = axis(b[0] + kGridEps, g.minX, g.nx);
        k0 = axis(a[1] - kGridEps, g.minZ, g.nz); k1 = axis(b[1] + kGridEps, g.minZ, g.nz);
    };
    const std::size_t cells = static_cast<std::size_t>(g.nx) * static_cast<std::size_t>(g.nz);
    g.start.assign(cells + 1, 0);
    forEach([&](std::size_t t) {
        int i0, i1, k0, k1;
        range(t, i0, i1, k0, k1);
        for (int k = k0; k <= k1; ++k)
            for (int i = i0; i <= i1; ++i)
                ++g.start[static_cast<std::size_t>(k) * g.nx + i + 1];
    });
    for (std::size_t c = 0; c < cells; ++c) g.start[c + 1] += g.start[c];
    g.index.resize(g.start[cells]);
    std::vector<std::uint32_t> cursor(g.start.begin(), g.start.end() - 1);
    forEach([&](std::size_t t) {   // ascending, so each cell's list is too
        int i0, i1, k0, k1;
        range(t, i0, i1, k0, k1);
        for (int k = k0; k <= k1; ++k)
            for (int i = i0; i <= i1; ++i)
                g.index[cursor[static_cast<std::size_t>(k) * g.nx + i]++] =
                    static_cast<std::uint32_t>(t);
    });
    return g;
}

}  // namespace

SoupGrid buildSoupGrid(const TriangleSoup& tris, double cell,
                       const std::vector<std::uint8_t>* include) {
    const std::size_t n = tris.size() / 9;
    return buildOver(tris, cell, [&](auto&& f) {
        for (std::size_t t = 0; t < n; ++t)
            if (!include || (t < include->size() && (*include)[t])) f(t);
    });
}

SoupGrid buildSoupGrid(const TriangleSoup& tris, double cell,
                       std::span<const std::uint32_t> ids) {
    const std::size_t n = tris.size() / 9;
    return buildOver(tris, cell, [&](auto&& f) {
        for (const std::uint32_t t : ids)
            if (t < n) f(t);
    });
}

std::optional<double> floorUnder(const TriangleSoup& tris, const SoupGrid& g,
                                 double x, double y, double z) {
    if (!g.matches(tris)) return floorUnder(tris, x, y, z);
    std::optional<double> best;
    if (g.nx <= 0) return best;
    const int i = cellAxis(x, g.minX, g.maxX, g.cell, g.nx);
    const int k = cellAxis(z, g.minZ, g.maxZ, g.cell, g.nz);
    if (i < 0 || k < 0) return best;
    const std::size_t c = static_cast<std::size_t>(k) * g.nx + i;
    for (std::uint32_t e = g.start[c]; e < g.start[c + 1]; ++e) {
        double hit;
        if (!underKernel(tris, 9 * static_cast<std::size_t>(g.index[e]), x, z, hit)) continue;
        // "below" is a LARGER y, and strictly below the origin
        if (hit > y + 1.0 && (!best || hit < *best)) best = hit;
    }
    return best;
}

std::optional<GroundHit> surfaceUnder(const TriangleSoup& tris, const SoupGrid& g,
                                      double x, double y, double z) {
    if (!g.matches(tris)) return surfaceUnder(tris, x, y, z);
    std::optional<GroundHit> best;
    if (g.nx <= 0) return best;
    const int i = cellAxis(x, g.minX, g.maxX, g.cell, g.nx);
    const int k = cellAxis(z, g.minZ, g.maxZ, g.cell, g.nz);
    if (i < 0 || k < 0) return best;
    const std::size_t c = static_cast<std::size_t>(k) * g.nx + i;
    for (std::uint32_t e = g.start[c]; e < g.start[c + 1]; ++e) {
        const std::size_t t = 9 * static_cast<std::size_t>(g.index[e]);
        double hit;
        if (!underKernel(tris, t, x, z, hit)) continue;
        if (!(hit > y + 1.0) || (best && hit >= best->y)) continue;
        double n[3];
        if (!upNormal(tris, t, n)) continue;
        best = GroundHit{hit, {n[0], n[1], n[2]}};
    }
    return best;
}

TriangleSoup soupInBox(const TriangleSoup& tris, const SoupGrid& g, double minX,
                       double maxX, double minZ, double maxZ) {
    // A NaN bound makes every rejection test in the linear version false, so it
    // returns the whole soup; that and a mismatched grid go to the linear scan.
    if (!g.matches(tris) || std::isnan(minX) || std::isnan(maxX) ||
        std::isnan(minZ) || std::isnan(maxZ))
        return soupInBox(tris, minX, maxX, minZ, maxZ);
    TriangleSoup out;
    if (g.nx <= 0) return out;
    if (maxX < g.minX - kGridEps || minX > g.maxX + kGridEps ||
        maxZ < g.minZ - kGridEps || minZ > g.maxZ + kGridEps || minX > maxX || minZ > maxZ)
        return out;
    const auto axis = [&](double v, double base, int cells) {
        const double f = std::floor((v - base) / g.cell);
        if (f < 0) return 0;
        if (f >= cells - 1) return cells - 1;
        return static_cast<int>(f);
    };
    const int i0 = axis(minX, g.minX, g.nx), i1 = axis(maxX, g.minX, g.nx);
    const int k0 = axis(minZ, g.minZ, g.nz), k1 = axis(maxZ, g.minZ, g.nz);
    std::vector<std::uint32_t> cand;
    for (int k = k0; k <= k1; ++k)
        for (int i = i0; i <= i1; ++i) {
            const std::size_t c = static_cast<std::size_t>(k) * g.nx + i;
            cand.insert(cand.end(), g.index.begin() + g.start[c], g.index.begin() + g.start[c + 1]);
        }
    std::sort(cand.begin(), cand.end());
    cand.erase(std::unique(cand.begin(), cand.end()), cand.end());
    for (std::uint32_t tri : cand) {
        const std::size_t t = 9 * static_cast<std::size_t>(tri);
        double lo[2] = {tris[t], tris[t + 2]}, hi[2] = {tris[t], tris[t + 2]};
        for (int k = 1; k < 3; ++k) {
            const double px = tris[t + 3 * k], pz = tris[t + 3 * k + 2];
            lo[0] = std::min(lo[0], px); hi[0] = std::max(hi[0], px);
            lo[1] = std::min(lo[1], pz); hi[1] = std::max(hi[1], pz);
        }
        if (hi[0] < minX || lo[0] > maxX || hi[1] < minZ || lo[1] > maxZ) continue;
        out.insert(out.end(), tris.begin() + static_cast<long>(t),
                   tris.begin() + static_cast<long>(t) + 9);
    }
    return out;
}

// ---------------------------------------------------------- THE TWO-LAYER GRID

namespace {

// The candidate list of one layer's cell under (x, z): [begin, end) into its
// index, empty when the point is outside that layer's extent or it holds none.
inline std::pair<const std::uint32_t*, const std::uint32_t*> cellList(const SoupGrid& g,
                                                                     double x, double z) {
    if (g.nx <= 0) return {nullptr, nullptr};
    const int i = cellAxis(x, g.minX, g.maxX, g.cell, g.nx);
    const int k = cellAxis(z, g.minZ, g.maxZ, g.cell, g.nz);
    if (i < 0 || k < 0) return {nullptr, nullptr};
    const std::size_t c = static_cast<std::size_t>(k) * g.nx + i;
    return {g.index.data() + g.start[c], g.index.data() + g.start[c + 1]};
}

// Visit the two ascending lists as one ascending sequence. A triangle is in one
// layer only, so there is nothing to de-duplicate.
template <class F>
inline void mergedWalk(std::pair<const std::uint32_t*, const std::uint32_t*> a,
                       std::pair<const std::uint32_t*, const std::uint32_t*> b, F&& visit) {
    const std::uint32_t* pa = a.first, *ea = a.second, *pb = b.first, *eb = b.second;
    while (pa != ea || pb != eb) {
        if (pb == eb || (pa != ea && *pa < *pb)) visit(*pa++);
        else visit(*pb++);
    }
}

}  // namespace

std::optional<double> floorUnder(const TriangleSoup& tris, const SplitSoupGrid& g,
                                 double x, double y, double z) {
    if (!g.matches(tris)) return floorUnder(tris, x, y, z);
    std::optional<double> best;
    mergedWalk(cellList(g.fixed, x, z), cellList(g.moving, x, z), [&](std::uint32_t tri) {
        double hit;
        if (!underKernel(tris, 9 * static_cast<std::size_t>(tri), x, z, hit)) return;
        // "below" is a LARGER y, and strictly below the origin
        if (hit > y + 1.0 && (!best || hit < *best)) best = hit;
    });
    return best;
}

std::optional<double> floorUnder(const TriangleSoup& tris, const SplitSoupGrid& g,
                                 double x, double y, double z, std::uint32_t& tri) {
    std::optional<double> best;
    const auto visit = [&](std::uint32_t t) {
        double hit;
        if (!underKernel(tris, 9 * static_cast<std::size_t>(t), x, z, hit)) return;
        if (hit > y + 1.0 && (!best || hit < *best)) { best = hit; tri = t; }
    };
    if (!g.matches(tris)) {
        for (std::size_t t = 0; t + 9 <= tris.size(); t += 9) visit(static_cast<std::uint32_t>(t / 9));
        return best;
    }
    mergedWalk(cellList(g.fixed, x, z), cellList(g.moving, x, z), visit);
    return best;
}

std::optional<GroundHit> surfaceUnder(const TriangleSoup& tris, const SplitSoupGrid& g,
                                      double x, double y, double z) {
    if (!g.matches(tris)) return surfaceUnder(tris, x, y, z);
    std::optional<GroundHit> best;
    mergedWalk(cellList(g.fixed, x, z), cellList(g.moving, x, z), [&](std::uint32_t tri) {
        const std::size_t t = 9 * static_cast<std::size_t>(tri);
        double hit;
        if (!underKernel(tris, t, x, z, hit)) return;
        if (!(hit > y + 1.0) || (best && hit >= best->y)) return;
        double n[3];
        if (!upNormal(tris, t, n)) return;
        best = GroundHit{hit, {n[0], n[1], n[2]}};
    });
    return best;
}

TriangleSoup soupInBox(const TriangleSoup& tris, const SplitSoupGrid& g, double minX,
                       double maxX, double minZ, double maxZ) {
    if (!g.matches(tris) || std::isnan(minX) || std::isnan(maxX) ||
        std::isnan(minZ) || std::isnan(maxZ))
        return soupInBox(tris, minX, maxX, minZ, maxZ);
    // Both layers' candidates under the box, as triangle numbers, sorted into
    // ascending order - the order a single grid over all of them visits - and
    // then the linear test on each, exactly as the single-grid gather does.
    std::vector<std::uint32_t> ids;
    const auto gather = [&](const SoupGrid& layer) {
        if (layer.nx <= 0) return;
        if (maxX < layer.minX - kGridEps || minX > layer.maxX + kGridEps ||
            maxZ < layer.minZ - kGridEps || minZ > layer.maxZ + kGridEps || minX > maxX || minZ > maxZ)
            return;
        const auto axis = [&](double v, double base, int cells) {
            const double f = std::floor((v - base) / layer.cell);
            if (f < 0) return 0;
            if (f >= cells - 1) return cells - 1;
            return static_cast<int>(f);
        };
        const int i0 = axis(minX, layer.minX, layer.nx), i1 = axis(maxX, layer.minX, layer.nx);
        const int k0 = axis(minZ, layer.minZ, layer.nz), k1 = axis(maxZ, layer.minZ, layer.nz);
        for (int k = k0; k <= k1; ++k)
            for (int i = i0; i <= i1; ++i) {
                const std::size_t c = static_cast<std::size_t>(k) * layer.nx + i;
                ids.insert(ids.end(), layer.index.begin() + layer.start[c],
                           layer.index.begin() + layer.start[c + 1]);
            }
    };
    gather(g.fixed);
    gather(g.moving);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    TriangleSoup out;
    for (std::uint32_t tri : ids) {
        const std::size_t t = 9 * static_cast<std::size_t>(tri);
        double lo[2] = {tris[t], tris[t + 2]}, hi[2] = {tris[t], tris[t + 2]};
        for (int k = 1; k < 3; ++k) {
            const double px = tris[t + 3 * k], pz = tris[t + 3 * k + 2];
            lo[0] = std::min(lo[0], px); hi[0] = std::max(hi[0], px);
            lo[1] = std::min(lo[1], pz); hi[1] = std::max(hi[1], pz);
        }
        if (hi[0] < minX || lo[0] > maxX || hi[1] < minZ || lo[1] > maxZ) continue;
        out.insert(out.end(), tris.begin() + static_cast<long>(t),
                   tris.begin() + static_cast<long>(t) + 9);
    }
    return out;
}

}  // namespace omk

// ---------------------------------------------------------- THE NARROW PHASE
namespace omk {

namespace {
constexpr double kSweepEps = 1e-4;

bool triNormal(const float* a, const float* b, const float* c, double n[3]) {
    const double ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
    const double vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
    n[0] = uy * vz - uz * vy; n[1] = uz * vx - ux * vz; n[2] = ux * vy - uy * vx;
    const double L = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (L <= 0.0) return false;
    n[0] /= L; n[1] /= L; n[2] /= L;
    return true;
}

// Is p, already on the triangle's plane, inside it? (the sim's `_inside`)
bool insideTri(const float* a, const float* b, const float* c, const double n[3],
               const double p[3]) {
    const float* e[3][2] = {{a, b}, {b, c}, {c, a}};
    for (const auto& uv : e) {
        const float* u = uv[0]; const float* v = uv[1];
        const double ex = v[0] - u[0], ey = v[1] - u[1], ez = v[2] - u[2];
        const double wx = p[0] - u[0], wy = p[1] - u[1], wz = p[2] - u[2];
        const double cx = ey * wz - ez * wy, cy = ez * wx - ex * wz, cz = ex * wy - ey * wx;
        if (cx * n[0] + cy * n[1] + cz * n[2] < 0.0) return false;
    }
    return true;
}
}  // namespace

std::optional<SweepHit> sweepSphere(const TriangleSoup& tris, const double p0[3],
                                    const double d[3], double radius) {
    std::optional<SweepHit> best;
    double lo[3], hi[3];
    for (int k = 0; k < 3; ++k) {
        lo[k] = std::min(p0[k], p0[k] + d[k]) - radius;
        hi[k] = std::max(p0[k], p0[k] + d[k]) + radius;
    }
    for (std::size_t i = 0; i + 9 <= tris.size(); i += 9) {
        const float* a = &tris[i]; const float* b = &tris[i + 3]; const float* c = &tris[i + 6];
        // the broad phase: the face's own box against the swept box
        bool out = false;
        for (int k = 0; k < 3 && !out; ++k) {
            const double mn = std::min({a[k], b[k], c[k]}), mx = std::max({a[k], b[k], c[k]});
            if (mx < lo[k] || mn > hi[k]) out = true;
        }
        if (out) continue;
        double n[3];
        if (!triNormal(a, b, c, n)) continue;
        double s0 = n[0] * (p0[0] - a[0]) + n[1] * (p0[1] - a[1]) + n[2] * (p0[2] - a[2]);
        if (s0 < 0.0) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; s0 = -s0; }   // two-sided
        const double dn = n[0] * d[0] + n[1] * d[1] + n[2] * d[2];
        if (dn >= -kSweepEps) continue;                 // not closing on this face
        double t = (s0 - radius) / -dn;
        if (t > 1.0) continue;
        if (t < 0.0) {
            if (s0 > radius) continue;                  // behind, not penetrating
            t = 0.0;
        }
        const double hit[3] = {p0[0] + t * d[0] - n[0] * radius,
                               p0[1] + t * d[1] - n[1] * radius,
                               p0[2] + t * d[2] - n[2] * radius};
        if (!insideTri(a, b, c, n, hit)) continue;
        if (!best || t < best->t) {
            SweepHit h; h.t = t;
            for (int k = 0; k < 3; ++k) h.n[k] = n[k];
            best = h;
        }
    }
    return best;
}

bool clampNormal(unsigned mask, bool high, const double n[3], double out[3]) {
    for (int k = 0; k < 3; ++k) out[k] = n[k];
    const unsigned b = high ? 16u : 0u;
    for (int k = 0; k < 3; ++k) {
        if (((mask >> (b + 2 * static_cast<unsigned>(k))) & 1u) && n[k] > 0.0) out[k] = 0.0;
        if (((mask >> (b + 2 * static_cast<unsigned>(k) + 1)) & 1u) && out[k] < 0.0) out[k] = 0.0;
    }
    const double L = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    if (L <= 0.00999999987) { out[0] = out[1] = out[2] = 0.0; return false; }
    out[0] /= L; out[1] /= L; out[2] /= L;
    return true;
}

}  // namespace omk
