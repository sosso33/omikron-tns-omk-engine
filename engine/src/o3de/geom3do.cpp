// SPDX-License-Identifier: GPL-3.0-or-later
#include "o3de/geom3do.h"

#include <cmath>

#include <algorithm>
#include <map>
#include <optional>

namespace omk {
namespace {

// A vertex index is per-MESH, so each mesh's block starts at the running sum
// of the counts before it. Same for faces.
struct Bases {
    std::vector<std::size_t> vertex, triangle, quad;
};

Bases runningBases(const std::vector<Mesh>& ms) {
    Bases b;
    b.vertex.reserve(ms.size());
    b.triangle.reserve(ms.size());
    b.quad.reserve(ms.size());
    std::size_t av = 0, at = 0, aq = 0;
    for (const auto& m : ms) {
        b.vertex.push_back(av);
        b.triangle.push_back(at);
        b.quad.push_back(aq);
        av += static_cast<std::size_t>(std::max(0, m.vertices));
        at += static_cast<std::size_t>(std::max(0, m.triangles));
        aq += static_cast<std::size_t>(std::max(0, m.quads));
    }
    return b;
}

bool drawable(std::uint32_t flags, DrawFilter f) {
    return f == DrawFilter::Engine ? (flags & kDrawableMask) == 0
                                   : (flags & kCollisionOnly) == 0;
}

Blend blendOf(std::uint32_t f) {
    if ((f & kTransparent) && (f & kAdditive)) return Blend::Add;
    if ((f & kTransparent) && (f & kMultiply)) return Blend::Mul;
    return Blend::Opaque;
}

// A resolved corner source: which global vertex, and whose offset to add.
struct Resolved {
    std::size_t  gi;
    const float* off;
    // WHICH MESH's transform applies to this corner, which is not always the
    // mesh that declared the face: a negative index is SKINNED to an ancestor,
    // and the corner is then built from that ancestor's vertex and its offset.
    // Posing it by the declaring mesh instead tears the model apart - visibly,
    // at the shoulders, where the skinned corners are.
    int          owner;
};

}  // namespace

Geometry buildGeometry(std::span<const std::byte> d, DrawFilter filter) {
    Geometry out;
    const auto header = readHeader(d);
    if (!header) return out;

    const auto ms  = readMeshes(d, *header);
    const auto vs  = readVertices(d, *header);
    const auto tris = readTriangles(d, *header);
    const auto quads = readQuads(d, *header);
    if (ms.empty()) return out;

    const auto base = runningBases(ms);

    // parent/child/next are mesh IDs, not indices
    std::map<std::int32_t, std::size_t> byId;
    for (std::size_t i = 0; i < ms.size(); ++i) byId.emplace(ms[i].id, i);

    // A negative index names a vertex of an ANCESTOR: walk up the parent chain
    // for the first mesh with more than three vertices that actually has this
    // one. The guard is not decoration - a malformed parent chain would loop.
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

    const auto resolve = [&](const Mesh& m, std::int16_t i,
                             const float* off) -> std::optional<Resolved> {
        if (i < 0) {
            const auto k = static_cast<std::size_t>(i & 0x7FFF);
            const Mesh* anc = ancestorFor(m, k);
            if (!anc) return std::nullopt;
            return Resolved{base.vertex[static_cast<std::size_t>(anc->index)] + k,
                            anc->pos, anc->index};
        }
        const std::size_t gi =
            base.vertex[static_cast<std::size_t>(m.index)] + static_cast<std::size_t>(i);
        if (gi >= vs.size()) return std::nullopt;
        return Resolved{gi, off, m.index};
    };

    // group key: (blend, material, cutout) - the engine's draw order
    using Key = std::tuple<int, std::int32_t, bool>;
    std::map<Key, std::vector<Corner>> groups;
    std::map<Key, std::vector<std::uint8_t>> mirrorOf;
    std::map<Key, std::vector<std::int32_t>> meshOf;
    std::map<Key, std::vector<std::int32_t>> vertOf;
    std::map<Key, std::vector<std::int32_t>> declOf;

    for (const auto& m : ms) {
        const auto f = static_cast<std::uint32_t>(m.flags);
        if (!drawable(f, filter)) continue;
        const bool  cut = (f & kCutout) != 0;
        const Blend bl  = blendOf(f);
        const bool  shimmer = (f & kFlicker) != 0;
        const std::uint8_t isMirror = (f & kMirror) != 0 ? 1u : 0u;

        const auto corner = [&](const Resolved& r, std::uint8_t tu, std::uint8_t tv) {
            const Vertex& v = vs[r.gi];
            Corner c;
            c.x = v.p[0] + r.off[0];
            c.y = v.p[1] + r.off[1];
            c.z = v.p[2] + r.off[2];
            c.u = static_cast<float>(tu);
            c.v = static_cast<float>(tv);
            c.r = static_cast<float>(v.r) / 255.0f;
            c.g = static_cast<float>(v.g) / 255.0f;
            c.b = static_cast<float>(v.b) / 255.0f;
            c.phase = shimmer ? static_cast<float>((2u * r.gi) % 32u) : -1.0f;
            return c;
        };

        const auto mi = static_cast<std::size_t>(m.index);
        for (std::size_t t = base.triangle[mi];
             t < base.triangle[mi] + static_cast<std::size_t>(std::max(0, m.triangles))
             && t < tris.size(); ++t) {
            const Triangle& tr = tris[t];
            if (tr.material < 0) continue;
            Corner c3[3];
            std::int32_t tv[3] = {-1, -1, -1};
            std::int32_t to[3] = {-1, -1, -1};
            bool ok = true;
            for (int k = 0; k < 3 && ok; ++k) {
                const auto r = resolve(m, tr.idx[k], m.pos);
                if (!r) { ok = false; break; }
                c3[k] = corner(*r, tr.uv[k * 2], tr.uv[k * 2 + 1]);
                tv[k] = static_cast<std::int32_t>(r->gi);
                to[k] = r->owner;
            }
            if (!ok) continue;
            auto& g = groups[Key{static_cast<int>(bl), tr.material, cut}];
            g.insert(g.end(), {c3[0], c3[1], c3[2]});
            auto& gm = mirrorOf[Key{static_cast<int>(bl), tr.material, cut}];
            auto& gx = meshOf[Key{static_cast<int>(bl), tr.material, cut}];
            gx.insert(gx.end(), {to[0], to[1], to[2]});
            auto& gv = vertOf[Key{static_cast<int>(bl), tr.material, cut}];
            gv.insert(gv.end(), {tv[0], tv[1], tv[2]});
            auto& gd = declOf[Key{static_cast<int>(bl), tr.material, cut}];
            gd.insert(gd.end(), 3, m.index);
            gm.insert(gm.end(), 3, isMirror);
        }

        for (std::size_t q = base.quad[mi];
             q < base.quad[mi] + static_cast<std::size_t>(std::max(0, m.quads))
             && q < quads.size(); ++q) {
            const Quad& qd = quads[q];
            if (qd.material < 0) continue;
            Corner c4[4];
            std::int32_t qv[4] = {-1, -1, -1, -1};
            std::int32_t qo[4] = {-1, -1, -1, -1};
            bool ok = true;
            for (int k = 0; k < 4 && ok; ++k) {
                const auto r = resolve(m, qd.idx[k], m.pos);
                if (!r) { ok = false; break; }
                c4[k] = corner(*r, qd.uv[k * 2], qd.uv[k * 2 + 1]);
                qv[k] = static_cast<std::int32_t>(r->gi);
                qo[k] = r->owner;
            }
            if (!ok) continue;
            // the four indices are in perimeter order, so the split is fixed
            auto& g = groups[Key{static_cast<int>(bl), qd.material, cut}];
            g.insert(g.end(), {c4[0], c4[1], c4[2], c4[0], c4[2], c4[3]});
            auto& gm = mirrorOf[Key{static_cast<int>(bl), qd.material, cut}];
            gm.insert(gm.end(), 6, isMirror);
            auto& gx = meshOf[Key{static_cast<int>(bl), qd.material, cut}];
            gx.insert(gx.end(), {qo[0], qo[1], qo[2], qo[0], qo[2], qo[3]});
            auto& gv = vertOf[Key{static_cast<int>(bl), qd.material, cut}];
            gv.insert(gv.end(), {qv[0], qv[1], qv[2], qv[0], qv[2], qv[3]});
            auto& gd = declOf[Key{static_cast<int>(bl), qd.material, cut}];
            gd.insert(gd.end(), 6, m.index);
        }
    }

    for (auto& [key, corners] : groups) {
        Batch b;
        b.blend    = static_cast<Blend>(std::get<0>(key));
        b.material = std::get<1>(key);
        b.cutout   = std::get<2>(key);
        b.start    = out.corners.size();
        b.count    = corners.size();
        out.batches.push_back(b);
        out.corners.insert(out.corners.end(), corners.begin(), corners.end());
        const auto& mf = mirrorOf[key];
        out.cornerMirror.insert(out.cornerMirror.end(), mf.begin(), mf.end());
        const auto& mx = meshOf[key];
        out.cornerMesh.insert(out.cornerMesh.end(), mx.begin(), mx.end());
        const auto& vx = vertOf[key];
        out.cornerVertex.insert(out.cornerVertex.end(), vx.begin(), vx.end());
        const auto& dx = declOf[key];
        out.cornerDeclared.insert(out.cornerDeclared.end(), dx.begin(), dx.end());
    }
    return out;
}

// The mirror's plane. See `MirrorPlane` in the header for what is traced and
// what is reconstructed - in short, the POINT is the engine's own field and
// the NORMAL is the face's cross product because the engine's is a runtime
// value that was not traced.
MirrorPlane mirrorPlane(std::span<const std::byte> d) {
    MirrorPlane out;
    const auto header = readHeader(d);
    if (!header) return out;
    const auto ms = readMeshes(d, *header);
    const auto vs = readVertices(d, *header);
    const auto tris = readTriangles(d, *header);
    const auto quads = readQuads(d, *header);
    if (ms.empty()) return out;
    const auto base = runningBases(ms);

    for (const auto& m : ms) {
        if ((static_cast<std::uint32_t>(m.flags) & kMirror) == 0) continue;
        const auto mi = static_cast<std::size_t>(m.index);

        // The engine branches on mesh +68 - the TRIANGLE COUNT - taking the
        // normal from a triangle when there is one and from a quad otherwise.
        // The same branch, so the same face is used.
        std::int16_t idx[3];
        bool have = false;
        if (m.triangles > 0 && base.triangle[mi] < tris.size()) {
            const Triangle& t = tris[base.triangle[mi]];
            idx[0] = t.idx[0]; idx[1] = t.idx[1]; idx[2] = t.idx[2];
            have = true;
        } else if (m.quads > 0 && base.quad[mi] < quads.size()) {
            const Quad& q = quads[base.quad[mi]];
            idx[0] = q.idx[0]; idx[1] = q.idx[1]; idx[2] = q.idx[2];
            have = true;
        }
        if (!have) continue;

        float p[3][3];
        bool ok = true;
        for (int k = 0; k < 3 && ok; ++k) {
            if (idx[k] < 0) { ok = false; break; }   // an ancestor's vertex
            const std::size_t gi = base.vertex[mi] + static_cast<std::size_t>(idx[k]);
            if (gi >= vs.size()) { ok = false; break; }
            for (int c = 0; c < 3; ++c) p[k][c] = vs[gi].p[c] + m.pos[c];
        }
        if (!ok) continue;

        const float a1[3] = {p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]};
        const float b1[3] = {p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2]};
        float n[3] = {a1[1] * b1[2] - a1[2] * b1[1],
                      a1[2] * b1[0] - a1[0] * b1[2],
                      a1[0] * b1[1] - a1[1] * b1[0]};
        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len <= 0) continue;
        for (int c = 0; c < 3; ++c) n[c] /= len;

        out.found = true;
        out.mesh = m.index;
        // The plane's point is the mesh's own position, which is what
        // `sub_440D90` adds the instance position to. A vertex of the face
        // would name the same plane; this names the engine's field.
        for (int c = 0; c < 3; ++c) { out.point[c] = m.pos[c]; out.normal[c] = n[c]; }
        return out;   // one global in the engine: the first is the mirror
    }
    return out;
}

}  // namespace omk
