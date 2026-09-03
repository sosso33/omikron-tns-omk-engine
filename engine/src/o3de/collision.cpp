// SPDX-License-Identifier: GPL-3.0-or-later
#include "o3de/collision.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace omk {

TriangleSoup collisionSoup(std::span<const std::byte> d, SoupKind kind) {
    TriangleSoup out;
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
        if (n2 <= 0) return;
        const double slope = std::fabs(ny) / std::sqrt(n2);
        if (kind == SoupKind::Walkable && slope < kSlopeCos30) return;
        if (kind == SoupKind::Steep    && slope >= kSlopeCos30) return;
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
        const auto mi = static_cast<std::size_t>(m.index);
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

}  // namespace omk
