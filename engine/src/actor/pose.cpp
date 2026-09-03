// SPDX-License-Identifier: GPL-3.0-or-later
// Posing a character. See `pose.h` for where each rule comes from.
#include "actor/pose.h"

#include "formats/morph.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstring>
#include <string>

namespace omk {
namespace {

std::int32_t i32at(std::span<const std::byte> b, std::size_t o) {
    return static_cast<std::int32_t>(
        static_cast<std::uint32_t>(b[o]) |
        (static_cast<std::uint32_t>(b[o + 1]) << 8) |
        (static_cast<std::uint32_t>(b[o + 2]) << 16) |
        (static_cast<std::uint32_t>(b[o + 3]) << 24));
}

float f32at(std::span<const std::byte> b, std::size_t o) {
    std::uint32_t v = static_cast<std::uint32_t>(b[o]) |
                      (static_cast<std::uint32_t>(b[o + 1]) << 8) |
                      (static_cast<std::uint32_t>(b[o + 2]) << 16) |
                      (static_cast<std::uint32_t>(b[o + 3]) << 24);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
std::uint32_t u32at(std::span<const std::byte> b, std::size_t o) {
    return static_cast<std::uint32_t>(b[o]) |
           (static_cast<std::uint32_t>(b[o + 1]) << 8) |
           (static_cast<std::uint32_t>(b[o + 2]) << 16) |
           (static_cast<std::uint32_t>(b[o + 3]) << 24);
}

}  // namespace

Quatf qslerp(const Quatf& a, const Quatf& b, float t) {
    // `sub_4721F0(a1, a2, out, k)`: `v38 = k / 256`, `v37 = 1 - v38`, the
    // shortest arc taken by negating one side when the dot is negative, and
    // the two sines over sin(theta) - a slerp, with the weight QUANTISED to
    // 256 steps by the caller (`f32(a4, 8) * 255.0`, truncated).
    const int k = static_cast<int>(t * 255.0f);
    const float w = static_cast<float>(k < 0 ? 0 : (k > 255 ? 255 : k)) * 0.00390625f;
    Quatf bb = b;
    float dot = a.w * bb.w + a.x * bb.x + a.y * bb.y + a.z * bb.z;
    if (dot < 0.0f) { bb.w = -bb.w; bb.x = -bb.x; bb.y = -bb.y; bb.z = -bb.z; dot = -dot; }
    float wa, wb;
    if (dot > 0.9995f) {
        wa = 1.0f - w; wb = w;                       // `v43 <= 0.1` arm: linear
    } else {
        const float th = std::acos(dot > 1.0f ? 1.0f : dot);
        const float s = std::sin(th);
        wa = std::sin((1.0f - w) * th) / s;
        wb = std::sin(w * th) / s;
    }
    Quatf q{a.w * wa + bb.w * wb, a.x * wa + bb.x * wb,
            a.y * wa + bb.y * wb, a.z * wa + bb.z * wb};
    const float n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (n > 1e-6f) { q.w /= n; q.x /= n; q.y /= n; q.z /= n; }
    return q;
}

NodeTracks blendTracks(const NodeTracks& a, int frameA, bool cancelRootA,
                       const NodeTracks& b, int frameB, bool cancelRootB,
                       float t) {
    NodeTracks out;
    if (!a.valid() && !b.valid()) return out;
    const auto clampF = [](const NodeTracks& tr, int f) {
        return f < 0 ? 0 : (f >= tr.frames ? tr.frames - 1 : f);
    };
    // The union of the two sides' mesh ids, in a's order then b's.
    for (const auto& id : a.ids) out.ids.push_back(id);
    for (const auto& id : b.ids)
        if (std::find(out.ids.begin(), out.ids.end(), id) == out.ids.end())
            out.ids.push_back(id);
    out.count = static_cast<int>(out.ids.size());
    out.frames = 1;
    out.rootTrack = a.valid() ? a.rootTrack : b.rootTrack;
    std::vector<Quatf> row(out.ids.size());
    const std::vector<Quatf>* ra = a.valid() ? &a.quats[static_cast<std::size_t>(clampF(a, frameA))] : nullptr;
    const std::vector<Quatf>* rb = b.valid() ? &b.quats[static_cast<std::size_t>(clampF(b, frameB))] : nullptr;
    for (std::size_t i = 0; i < out.ids.size(); ++i) {
        Quatf qa, qb;   // identity where a side has no track for this mesh
        if (ra) for (std::size_t k = 0; k < a.ids.size() && k < ra->size(); ++k)
            if (a.ids[k] == out.ids[i]) {
                qa = (*ra)[k];
                if (cancelRootA && static_cast<int>(k) == a.rootTrack) qa = Quatf{};
            }
        if (rb) for (std::size_t k = 0; k < b.ids.size() && k < rb->size(); ++k)
            if (b.ids[k] == out.ids[i]) {
                qb = (*rb)[k];
                if (cancelRootB && static_cast<int>(k) == b.rootTrack) qb = Quatf{};
            }
        row[i] = qslerp(qa, qb, t);
    }
    out.quats.push_back(std::move(row));
    // The root translation lerps the same way (`v46 = morph * (1 - f) + f *
    // start` in sub_42D120); a side with no keys contributes zero.
    std::array<float, 3> tr{0, 0, 0};
    for (int c = 0; c < 3; ++c) {
        const float ta = (a.valid() && !a.trans.empty())
            ? a.trans[static_cast<std::size_t>(clampF(a, frameA) < static_cast<int>(a.trans.size()) ? clampF(a, frameA) : static_cast<int>(a.trans.size()) - 1)][static_cast<std::size_t>(c)] : 0.0f;
        const float tb = (b.valid() && !b.trans.empty())
            ? b.trans[static_cast<std::size_t>(clampF(b, frameB) < static_cast<int>(b.trans.size()) ? clampF(b, frameB) : static_cast<int>(b.trans.size()) - 1)][static_cast<std::size_t>(c)] : 0.0f;
        tr[static_cast<std::size_t>(c)] = ta * (1.0f - t) + tb * t;
    }
    out.trans.push_back(tr);
    return out;
}

Quatf qmul(const Quatf& a, const Quatf& b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}

void qrot(const Quatf& q, const float v[3], float out[3]) {
    // q * (0,v) * conj(q), written out.
    const float tx = 2.0f * (q.y * v[2] - q.z * v[1]);
    const float ty = 2.0f * (q.z * v[0] - q.x * v[2]);
    const float tz = 2.0f * (q.x * v[1] - q.y * v[0]);
    out[0] = v[0] + q.w * tx + (q.y * tz - q.z * ty);
    out[1] = v[1] + q.w * ty + (q.z * tx - q.x * tz);
    out[2] = v[2] + q.w * tz + (q.x * ty - q.y * tx);
}

NodeTracks clipTracks(std::span<const std::byte> d) {
    NodeTracks t;
    if (d.size() < 8) return t;
    const auto i32 = [&](std::size_t o) {
        return static_cast<std::int32_t>(u32at(d, o));
    };
    const std::int32_t frames = i32(0);
    const std::int32_t n = i32(4);
    if (frames < 0 || frames >= 20000 || n <= 0 || n >= 512) return t;
    if (8u + 40u * static_cast<std::size_t>(n) > d.size()) return t;

    struct Track { std::int32_t node, rotKeys, rotOffset, posKeys, posOffset; };
    std::vector<Track> tr;
    tr.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const std::size_t o = 8u + 40u * static_cast<std::size_t>(i);
        Track k{i32(o), i32(o + 32), i32(o + 36), i32(o + 24), i32(o + 28)};
        if (k.rotOffset &&
            (static_cast<std::size_t>(k.rotOffset) + 16u *
             static_cast<std::size_t>(k.rotKeys) > d.size())) return t;
        if (k.posOffset &&
            (static_cast<std::size_t>(k.posOffset) + 12u *
             static_cast<std::size_t>(k.posKeys) > d.size())) return t;
        tr.push_back(k);
    }

    t.count  = n;
    t.frames = frames;
    t.rootTrack = -1;              // a clip's root is the model's, not a track
    t.ids.reserve(tr.size());
    for (const auto& k : tr) t.ids.push_back(k.node);
    t.quats.assign(static_cast<std::size_t>(frames), {});
    t.trans.assign(static_cast<std::size_t>(frames), {0.0f, 0.0f, 0.0f});

    // THE ROOT MOTION, which this reader used to drop on the floor.
    //
    // A `.3DA` track carries POSITION keys at `+24`/`+28` beside its rotation
    // keys at `+32`/`+36`, and exactly one track in each of GRID's three clips
    // has any: track 2, **`UBassin`** - the pelvis, which is the hierarchy root
    // in all 181 character models. `Anim_RootDelta` (0x004711D0) reads that
    // array as 12 bytes a key and SUMS it between the previous frame and the
    // current one, fractional ends included, starting at key 1:
    //
    //     v15 = anim[+28];                       // the position keys
    //     ...ceil(prev) .. floor(cur), summing f32[3] at 12 * k...
    //
    // and `Script_SelectRelativeBodyAnimation` hands the result to
    // `Actor_MoveBy`. So key 0 is a sentinel exactly as it is for rotations,
    // and the accumulated offset at frame f is `sum(keys[1 .. f + 1])` - the
    // same `f -> f + 1` mapping the quaternions take, which is what keeps the
    // two in step.
    //
    // Dropping this pinned every animated character at its placement for the
    // whole clip: Kay'l rose off the floor instead of standing up, and his
    // 159-unit +Z jump out of `INTRO3` did not happen at all.
    t.trans = clipRootMotion(d);
    for (int f = 0; f < frames; ++f) {
        auto& row = t.quats[static_cast<std::size_t>(f)];
        row.resize(tr.size());
        for (std::size_t i = 0; i < tr.size(); ++i) {
            const Track& k = tr[i];
            if (!k.rotOffset || k.rotKeys <= 0) continue;
            // key 0 is the REST SENTINEL, so frame f is key f + 1
            const int key = std::min(f + 1, k.rotKeys - 1);
            const std::size_t o = static_cast<std::size_t>(k.rotOffset) +
                                  16u * static_cast<std::size_t>(key);
            row[i] = {f32at(d, o), f32at(d, o + 4),
                      f32at(d, o + 8), f32at(d, o + 12)};
        }
    }
    return t;
}

std::vector<std::array<float, 3>> clipRootMotion(std::span<const std::byte> d) {
    std::vector<std::array<float, 3>> out;
    if (d.size() < 8) return out;
    const int frames = i32at(d, 0), n = i32at(d, 4);
    if (frames <= 0 || n <= 0 || 8u + 40u * static_cast<std::size_t>(n) > d.size())
        return out;
    out.assign(static_cast<std::size_t>(frames), {0.0f, 0.0f, 0.0f});
    for (int i = 0; i < n; ++i) {
        const std::size_t o = 8u + 40u * static_cast<std::size_t>(i);
        const int pk = i32at(d, o + 24), po = i32at(d, o + 28);
        if (!po || pk <= 1) continue;
        if (static_cast<std::size_t>(po) + 12u * static_cast<std::size_t>(pk) > d.size())
            continue;
        std::array<float, 3> acc{0.0f, 0.0f, 0.0f};
        for (int f = 0; f < frames; ++f) {
            const int key = f + 1;
            if (key < pk) {
                const std::size_t k = static_cast<std::size_t>(po) +
                                      12u * static_cast<std::size_t>(key);
                for (int c = 0; c < 3; ++c)
                    acc[static_cast<std::size_t>(c)] +=
                        f32at(d, k + 4u * static_cast<std::size_t>(c));
            }
            for (int c = 0; c < 3; ++c)
                out[static_cast<std::size_t>(f)][static_cast<std::size_t>(c)] +=
                    acc[static_cast<std::size_t>(c)];
        }
    }
    return out;
}

bool clipRootStart(std::span<const std::byte> d, float out[3]) {
    if (d.size() < 8) return false;
    const int n = i32at(d, 4);
    if (n <= 0 || 8u + 40u * static_cast<std::size_t>(n) > d.size()) return false;
    for (int i = 0; i < n; ++i) {
        const std::size_t o = 8u + 40u * static_cast<std::size_t>(i);
        const int pk = i32at(d, o + 24), po = i32at(d, o + 28);
        if (!po || pk <= 0) continue;
        if (static_cast<std::size_t>(po) + 12u > d.size()) continue;
        for (int c = 0; c < 3; ++c)
            out[c] = f32at(d, static_cast<std::size_t>(po) +
                              4u * static_cast<std::size_t>(c));
        return true;
    }
    return false;
}

int rootTrackOf(const std::vector<Mesh>& meshes) {
    for (const auto& m : meshes) {
        bool hasParent = false;
        for (const auto& p : meshes) if (p.id == m.parent) { hasParent = true; break; }
        if (!hasParent) return m.index;
    }
    return 0;
}

NodeTracks nodeTracks(std::span<const std::byte> d, int rootTrack) {
    NodeTracks t;
    const auto L = morphLayout(d);
    if (!L.valid || !L.nodes || !L.frames) return t;
    t.count     = static_cast<int>(L.nodes);
    t.frames    = static_cast<int>(L.frames);
    t.rootTrack = rootTrack;

    // The preamble is the track table: `nodeCount` uint32 mesh indices.
    if (16u + 4u * L.nodes > d.size()) return t;
    t.ids.reserve(L.nodes);
    for (std::uint32_t i = 0; i < L.nodes; ++i)
        t.ids.push_back(static_cast<std::int32_t>(u32at(d, 16u + 4u * i)));

    // The per-track offsets inside one record - the root's 12 bytes sit
    // BEFORE its own quaternion, wherever that track falls.
    std::vector<std::size_t> offs(L.nodes);
    std::size_t o = 0;
    for (std::uint32_t i = 0; i < L.nodes; ++i) {
        if (static_cast<int>(i) == rootTrack) o += 12;
        offs[i] = o;
        o += 16;
    }

    t.quats.resize(L.frames);
    t.trans.resize(L.frames);
    float acc[3] = {0, 0, 0};
    for (std::size_t f = 0; f < L.frames; ++f) {
        const std::size_t base = L.preamble + f * L.record;
        auto& row = t.quats[f];
        row.resize(L.nodes);
        for (std::uint32_t i = 0; i < L.nodes; ++i) {
            const std::size_t at = base + offs[i];
            if (at + 16 > d.size()) return t;
            row[i] = {f32at(d, at), f32at(d, at + 4),
                      f32at(d, at + 8), f32at(d, at + 12)};
        }
        if (rootTrack >= 0 && rootTrack < static_cast<int>(L.nodes)) {
            const std::size_t at = base + offs[static_cast<std::size_t>(rootTrack)] - 12;
            if (at + 12 <= d.size())
                for (int k = 0; k < 3; ++k)
                    acc[k] += f32at(d, at + 4u * static_cast<std::size_t>(k));
        }
        t.trans[f] = {acc[0], acc[1], acc[2]};
    }
    return t;
}

std::vector<MeshPose> composePose(const std::vector<Mesh>& meshes,
                                  const NodeTracks& t, int frame,
                                  bool upright) {
    std::vector<MeshPose> out(meshes.size());
    std::vector<char> done(meshes.size(), 0);
    if (meshes.empty()) return out;

    // The track's quaternion for each mesh, CONJUGATED.
    std::vector<Quatf> qof(meshes.size());
    std::vector<char> hasQ(meshes.size(), 0);
    if (t.valid()) {
        const int f = frame < 0 ? 0
                    : (frame >= t.frames ? t.frames - 1 : frame);
        const auto& row = t.quats[static_cast<std::size_t>(f)];
        for (std::size_t i = 0; i < t.ids.size() && i < row.size(); ++i) {
            const std::int32_t mi = t.ids[i];
            if (mi < 0 || mi >= static_cast<std::int32_t>(meshes.size())) continue;
            qof[static_cast<std::size_t>(mi)] =
                {row[i].w, -row[i].x, -row[i].y, -row[i].z};
            hasQ[static_cast<std::size_t>(mi)] = 1;
        }
    }

    const auto byId = [&](std::int32_t id) -> const Mesh* {
        for (const auto& m : meshes) if (m.id == id) return &m;
        return nullptr;
    };

    // rot[m] = rot[parent] * q[m]; pos[m] = pos[parent] + rot[parent]*(rest offset)
    std::vector<int> stack;
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        if (done[i]) continue;
        stack.clear();
        std::size_t cur = i;
        int guard = 0;
        while (!done[cur] && guard++ <= 24) {
            stack.push_back(static_cast<int>(cur));
            const Mesh* par = byId(meshes[cur].parent);
            if (!par) break;
            const auto pi = static_cast<std::size_t>(par->index);
            if (pi >= meshes.size() || pi == cur) break;
            cur = pi;
        }
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
            const auto k = static_cast<std::size_t>(*it);
            if (done[k]) continue;
            const Mesh& m = meshes[k];
            const Quatf q = hasQ[k] ? qof[k] : Quatf{};
            const Mesh* par = byId(m.parent);
            if (!par || static_cast<std::size_t>(par->index) >= meshes.size() ||
                !done[static_cast<std::size_t>(par->index)]) {
                out[k].q = q;
                for (int c = 0; c < 3; ++c) out[k].pos[c] = m.pos[c];
            } else {
                const MeshPose& pp = out[static_cast<std::size_t>(par->index)];
                const float off[3] = {m.pos[0] - par->pos[0],
                                      m.pos[1] - par->pos[1],
                                      m.pos[2] - par->pos[2]};
                float rot[3];
                qrot(pp.q, off, rot);
                out[k].q = qmul(pp.q, q);
                for (int c = 0; c < 3; ++c) out[k].pos[c] = pp.pos[c] + rot[c];
            }
            done[k] = 1;
        }
    }

    if (upright) {
        // Cancel the ROOT's own rotation about its own origin.
        const Mesh* root = nullptr;
        for (const auto& m : meshes) if (!byId(m.parent)) { root = &m; break; }
        if (root) {
            const auto ri = static_cast<std::size_t>(root->index);
            if (ri < out.size()) {
                const Quatf rq = out[ri].q;
                const Quatf inv{rq.w, -rq.x, -rq.y, -rq.z};
                const float origin[3] = {out[ri].pos[0], out[ri].pos[1], out[ri].pos[2]};
                for (auto& mp : out) {
                    const float rel[3] = {mp.pos[0] - origin[0],
                                          mp.pos[1] - origin[1],
                                          mp.pos[2] - origin[2]};
                    float r[3];
                    qrot(inv, rel, r);
                    mp.q = qmul(inv, mp.q);
                    for (int c = 0; c < 3; ++c) mp.pos[c] = origin[c] + r[c];
                }
            }
        }
    }
    return out;
}

FaceMesh faceMeshOf(const std::vector<Mesh>& meshes) {
    FaceMesh f;
    std::size_t base = 0;
    for (const auto& m : meshes) {
        std::string n(m.name);
        for (auto& c : n) c = static_cast<char>(std::tolower(
            static_cast<unsigned char>(c)));
        if (n.find("visage") != std::string::npos) {
            f.mesh  = m.index;
            f.base  = base;
            f.count = m.vertices > 0 ? m.vertices : 0;
            return f;
        }
        base += static_cast<std::size_t>(m.vertices > 0 ? m.vertices : 0);
    }
    return f;
}

std::vector<float> faceFrame(std::span<const std::byte> d, int frame) {
    std::vector<float> out;
    const auto L = morphLayout(d);
    if (!L.valid || !L.vertices || !L.frames) return out;
    const int f = frame < 0 ? 0
                : (frame >= static_cast<int>(L.frames)
                       ? static_cast<int>(L.frames) - 1 : frame);
    // The vertex block sits after the root's 12 bytes and every quaternion.
    const std::size_t vb0 = 12u + 16u * L.nodes;
    const std::size_t base = L.preamble + static_cast<std::size_t>(f) * L.record + vb0;
    if (base + 24u * L.vertices > d.size()) return out;
    out.reserve(3u * L.vertices);
    for (std::uint32_t i = 0; i < L.vertices; ++i)
        for (int k = 0; k < 3; ++k)
            out.push_back(f32at(d, base + 24u * i + 4u * static_cast<std::size_t>(k)));
    return out;
}

void applyPose(Geometry& g, const Geometry& rest,
               const std::vector<Mesh>& meshes,
               const std::vector<MeshPose>& pose,
               const FaceMesh* face, const std::vector<float>* faceVerts) {
    if (rest.cornerMesh.size() != rest.corners.size()) return;
    const std::uint64_t was = g.revision;
    g = rest;
    // These corners are new even though the object is not - see
    // `Geometry::revision`.
    g.revision = was + 1;
    // The face stream only fits when it supplies exactly the vertices the
    // model's face mesh has - the count check that agrees on 150 of 153
    // conversations. A mismatch draws the bind pose rather than a scrambled
    // head, which is what the viewer does and says.
    const bool morphFace =
        face && face->valid() && faceVerts &&
        faceVerts->size() == 3u * static_cast<std::size_t>(face->count) &&
        rest.cornerVertex.size() == rest.corners.size();

    for (std::size_t i = 0; i < g.corners.size(); ++i) {
        const std::int32_t mi = rest.cornerMesh[i];
        if (mi < 0 || static_cast<std::size_t>(mi) >= meshes.size() ||
            static_cast<std::size_t>(mi) >= pose.size()) continue;
        const Mesh& m = meshes[static_cast<std::size_t>(mi)];
        const MeshPose& mp = pose[static_cast<std::size_t>(mi)];

        // The corner is already at `vertex + mesh.pos`; take it back to the
        // mesh's own frame, rotate, and put it at the posed position. A FACE
        // corner takes its local position from the morph frame instead - the
        // vertex is replaced, not rotated, which is the whole of what a morph
        // file is.
        float local[3];
        bool done = false;
        if (morphFace && mi == face->mesh) {
            const std::int32_t gv = rest.cornerVertex[i];
            const auto k = static_cast<std::size_t>(gv) - face->base;
            if (gv >= 0 && static_cast<std::size_t>(gv) >= face->base &&
                k < static_cast<std::size_t>(face->count)) {
                local[0] = (*faceVerts)[3 * k];
                local[1] = (*faceVerts)[3 * k + 1];
                local[2] = (*faceVerts)[3 * k + 2];
                done = true;
            }
        }
        if (!done) {
            local[0] = rest.corners[i].x - m.pos[0];
            local[1] = rest.corners[i].y - m.pos[1];
            local[2] = rest.corners[i].z - m.pos[2];
        }
        float r[3];
        qrot(mp.q, local, r);
        g.corners[i].x = mp.pos[0] + r[0];
        g.corners[i].y = mp.pos[1] + r[1];
        g.corners[i].z = mp.pos[2] + r[2];
    }
}


int headMeshOf(const std::vector<Mesh>& meshes) {
    int root = -1;
    for (std::size_t i = 0; i < meshes.size() && root < 0; ++i) {
        bool hasParent = false;
        for (const auto& p : meshes) if (p.id == meshes[i].parent) { hasParent = true; break; }
        if (!hasParent) root = static_cast<int>(i);
    }
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        std::string n = meshes[i].name;
        for (auto& c : n) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (n.size() < 4 || n.compare(n.size() - 4, 4, "tete") != 0) continue;
        int m = static_cast<int>(i);
        for (int guard = 0; guard < 64 && m >= 0; ++guard) {
            if (m == root) return static_cast<int>(i);
            const std::int32_t pid = meshes[static_cast<std::size_t>(m)].parent;
            int next = -1;
            for (std::size_t j = 0; j < meshes.size(); ++j) if (meshes[j].id == pid) { next = static_cast<int>(j); break; }
            m = next;
        }
    }
    return -1;
}

void aimHead(std::vector<MeshPose>& pose, const std::vector<Mesh>& meshes, int head,
             const float target[3], HeadLook& look, float dt, bool snap,
             float* wantedPitch, float* wantedYaw) {
    if (head < 0 || static_cast<std::size_t>(head) >= pose.size()) return;
    const float kDeg = 57.29577951308232f;
    const MeshPose& h = pose[static_cast<std::size_t>(head)];
    const float local[3] = {0.0f, 0.0f, -1.0f};
    float f[3];
    qrot(h.q, local, f);
    const float to[3] = {target[0] - h.pos[0], target[1] - h.pos[1], target[2] - h.pos[2]};
    const float fh = std::sqrt(f[0] * f[0] + f[2] * f[2]);
    const float th = std::sqrt(to[0] * to[0] + to[2] * to[2]);
    // yaw: the signed angle in the ground plane from the forward to the
    // target, positive turning -Z toward +X (rotateYaw's sense)
    float yaw = 0.0f;
    if (fh > 1e-6f && th > 1e-6f)
        yaw = std::atan2(f[0] * to[2] - f[2] * to[0], f[0] * to[0] + f[2] * to[2]) * kDeg;
    // pitch: the target's elevation less the forward's (Y down: up is -y)
    const float pitch = (std::atan2(-to[1], th) - std::atan2(-f[1], fh)) * kDeg;
    if (wantedPitch) *wantedPitch = pitch;
    if (wantedYaw) *wantedYaw = yaw;
    float p = pitch, y = yaw;
    if (p < -40.0f) p = -40.0f;
    if (p > 40.0f) p = 40.0f;
    if (y > 70.0f) y = 70.0f;
    if (y < -70.0f) y = -70.0f;
    if (snap) { look.pitch = p; look.yaw = y; }
    else {
        look.pitch += (p - look.pitch) * 0.125f * dt;
        look.yaw   += (y - look.yaw) * 0.125f * dt;
    }
    // the rotation: yaw about the world's Y, then pitch about the head's
    // right axis, applied to the head and everything under it about the
    // head's origin
    const float hy = look.yaw / kDeg * 0.5f, hp = look.pitch / kDeg * 0.5f;
    // yaw about -Y turns -Z toward +X in this handedness (Y points down)
    const Quatf qy{std::cos(hy), 0.0f, -std::sin(hy), 0.0f};
    float right[3];
    const float lx[3] = {1.0f, 0.0f, 0.0f};
    qrot(h.q, lx, right);
    float rr[3];
    qrot(qy, right, rr);
    // a nose-up pitch is a rotation about the right axis that lifts -Z,
    // which with Y down is the negative sense
    const Quatf qp{std::cos(hp), -rr[0] * std::sin(hp), -rr[1] * std::sin(hp), -rr[2] * std::sin(hp)};
    const Quatf d = qmul(qp, qy);
    const float origin[3] = {h.pos[0], h.pos[1], h.pos[2]};
    for (std::size_t i = 0; i < pose.size() && i < meshes.size(); ++i) {
        int m = static_cast<int>(i);
        bool under = false;
        for (int guard = 0; guard < 64 && m >= 0; ++guard) {
            if (m == head) { under = true; break; }
            const std::int32_t pid = meshes[static_cast<std::size_t>(m)].parent;
            int next = -1;
            for (std::size_t j = 0; j < meshes.size(); ++j) if (meshes[j].id == pid) { next = static_cast<int>(j); break; }
            m = next;
        }
        if (!under) continue;
        MeshPose& mp = pose[i];
        mp.q = qmul(d, mp.q);
        const float rel[3] = {mp.pos[0] - origin[0], mp.pos[1] - origin[1], mp.pos[2] - origin[2]};
        float rot[3];
        qrot(d, rel, rot);
        for (int k = 0; k < 3; ++k) mp.pos[k] = origin[k] + rot[k];
    }
}

}  // namespace omk
