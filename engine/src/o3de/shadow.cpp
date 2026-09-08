// SPDX-License-Identifier: GPL-3.0-or-later
#include "o3de/shadow.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace omk {

ShadowModel loadShadowModel(const DataFs& fs) {
    ShadowModel m;
    const auto o = fs.resolve("MESHES/MISC/shadows.3DO");
    if (!o) return m;
    const auto d = DataFs::readPath(*o);
    const auto h = readHeader(d);
    if (!h) return m;
    const auto vs = readVertices(d, *h);
    const auto qs = readQuads(d, *h);
    // One quad of four vertices is the whole model. Anything else is not the
    // file this reader was written against, so refuse rather than guess.
    if (qs.size() != 1 || vs.size() < 4) return m;
    const Quad& q = qs[0];
    for (int k = 0; k < 4; ++k) {
        const int i = q.idx[k];
        if (i < 0 || static_cast<std::size_t>(i) >= vs.size()) return m;
        for (int a = 0; a < 3; ++a) m.corner[k][a] = vs[static_cast<std::size_t>(i)].p[a];
        m.uv[k][0] = q.uv[2 * k];
        m.uv[k][1] = q.uv[2 * k + 1];
    }
    m.r = static_cast<float>(vs[static_cast<std::size_t>(q.idx[0])].r) / 255.0f;
    m.g = static_cast<float>(vs[static_cast<std::size_t>(q.idx[0])].g) / 255.0f;
    m.b = static_cast<float>(vs[static_cast<std::size_t>(q.idx[0])].b) / 255.0f;
    const auto t = fs.resolve("MESHES/MISC/shadows.3DT");
    if (t) m.tex = textures(d, DataFs::readPath(*t));
    m.loaded = !m.tex.empty();
    return m;
}

std::vector<int> shadowBonesFor(int detail) {
    std::vector<int> out;
    // `Actor_DrawShadow`'s switch: -1 and 0 fall to the chest, 1 adds the head
    // and the legs, 2 adds the arms, and 3 or more returns without drawing.
    if (detail > 2) return out;
    for (int i = 0; i < kShadowBoneCount; ++i)
        if (detail >= kShadowBones[i].minLevel) out.push_back(i);
    return out;
}

int findMeshContaining(const std::vector<Mesh>& meshes, const char* wanted) {
    int found = -1;
    for (std::size_t i = 0; i < meshes.size(); ++i)
        if (std::string_view(meshes[i].name).find(wanted) != std::string_view::npos)
            found = static_cast<int>(i);   // the LAST match, as the traverse leaves it
    return found;
}

namespace {

// Append the five shaded corners as the four-triangle fan `Shadow_EmitBoneBlob`
// emits: (0,1,C) (1,2,C) (2,3,C) (3,0,C).
void fan(Geometry& g, const Corner (&c)[5]) {
    const int e[4][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}};
    for (const auto& t : e) {
        g.corners.push_back(c[t[0]]);
        g.corners.push_back(c[t[1]]);
        g.corners.push_back(c[4]);
    }
}

void widen(Geometry& g) {
    g.cornerMirror.resize(g.corners.size(), 0);
    g.cornerMesh.resize(g.corners.size(), -1);
    g.cornerVertex.resize(g.corners.size(), -1);
    g.cornerDeclared.resize(g.corners.size(), -1);
}

}  // namespace

bool shadowFootBlob(Geometry& g, const ShadowModel& m, const float left[3],
                    const float right[3], float floorY, const float normal[3]) {
    if (!m.loaded) return false;
    const float cx = (left[0] + right[0]) * 0.5f;
    const float cz = (left[2] + right[2]) * 0.5f;
    const float y  = floorY - kShadowFootLift;
    // The minimal rotation taking straight down (0, +1, 0 in this Y-down
    // world) onto the floor's normal. `normal` arrives the way `GroundHit`
    // orients it - pointing UP, so with a negative y.
    float n[3] = {normal[0], normal[1], normal[2]};
    const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (len > 1e-6f) { n[0] /= len; n[1] /= len; n[2] /= len; }
    else { n[0] = 0; n[1] = -1; n[2] = 0; }
    // up = -n, and the quad's own up is -Y, so rotate -Y onto -n: axis is
    // (-Y) x (-n) = Y x n, angle from their dot.
    const float axis[3] = {-n[2], 0.0f, n[0]};     // (0,1,0) x n
    const float alen = std::sqrt(axis[0] * axis[0] + axis[2] * axis[2]);
    const float cosA = -n[1];                       // dot((0,-1,0), n) = -n.y
    Corner c[5];
    const auto place = [&](int i, float lx, float ly, float lz) {
        float p[3] = {lx, ly, lz};
        if (alen > 1e-6f && cosA < 0.999999f) {
            const float ux = axis[0] / alen, uz = axis[2] / alen;
            const float s = std::sqrt(std::max(0.0f, 1.0f - cosA * cosA));
            const float t = 1.0f - cosA;
            // Rodrigues about the unit axis (ux, 0, uz).
            const float R[9] = {
                t * ux * ux + cosA,  -s * uz,           t * ux * uz,
                s * uz,               cosA,            -s * ux,
                t * ux * uz,          s * ux,           t * uz * uz + cosA};
            const float q[3] = {R[0] * p[0] + R[1] * p[1] + R[2] * p[2],
                                R[3] * p[0] + R[4] * p[1] + R[5] * p[2],
                                R[6] * p[0] + R[7] * p[1] + R[8] * p[2]};
            p[0] = q[0]; p[1] = q[1]; p[2] = q[2];
        }
        c[i].x = cx + p[0]; c[i].y = y + p[1]; c[i].z = cz + p[2];
    };
    for (int i = 0; i < 4; ++i) {
        place(i, m.corner[i][0] * kShadowScale, m.corner[i][1] * kShadowScale,
              m.corner[i][2] * kShadowScale);
        c[i].u = static_cast<float>(m.uv[i][0]);
        c[i].v = static_cast<float>(m.uv[i][1]);
    }
    place(4, 0.0f, 0.0f, 0.0f);
    c[4].u = static_cast<float>((m.uv[0][0] + m.uv[1][0] + m.uv[2][0] + m.uv[3][0]) / 4);
    c[4].v = static_cast<float>((m.uv[0][1] + m.uv[1][1] + m.uv[2][1] + m.uv[3][1]) / 4);
    for (auto& v : c) {
        // Drawn as an ordinary mesh, so it carries the model's OWN vertex
        // colour and nothing fades it.
        v.r = m.r; v.g = m.g; v.b = m.b;
        v.nx = n[0]; v.ny = n[1]; v.nz = n[2];
        v.phase = -1.0f;
    }
    fan(g, c);
    widen(g);
    return true;
}

bool shadowBlob(Geometry& g, const ShadowModel& m, const float bone[3],
                float radius, float divisor, float reach, float floorY) {
    if (!m.loaded || reach <= 0.0f || divisor <= 0.0f) return false;
    // `v5 = mesh[+88] / g_ShadowBoneDivisor`, clamped at 1.5 - a thicker bone
    // casts a wider blob, up to a ceiling.
    float f = radius / divisor;
    if (f > 1.5f) f = 1.5f;
    // Y grows DOWN, so the surface below the bone has the larger y and the
    // engine's `dist` is that difference.
    const float dist = floorY - bone[1];
    if (dist < 0.0f || dist > reach) return false;
    // `LOBYTE(v53) = -1 - (int)(dist * 255.0 / reach)` - 255 in contact, 0 at
    // the limit, the same byte in all three channels.
    const int shade = 255 - static_cast<int>(dist * 255.0f / reach);
    const float s = static_cast<float>(std::clamp(shade, 0, 255)) / 255.0f;
    const float y = floorY - kShadowLift;
    const float k = kShadowScale * f;

    Corner c[5];
    for (int i = 0; i < 4; ++i) {
        c[i].x = bone[0] + m.corner[i][0] * k;
        // The model's own y is 0 in all four, so this is the floor plane; kept
        // as the engine writes it rather than assumed away.
        c[i].y = y + m.corner[i][1] * k;
        c[i].z = bone[2] + m.corner[i][2] * k;
        c[i].u = static_cast<float>(m.uv[i][0]);
        c[i].v = static_cast<float>(m.uv[i][1]);
    }
    // The CENTRE vertex - the bone's own x and z, and the average of the four
    // UV pairs, in the engine's integer arithmetic.
    c[4].x = bone[0];
    c[4].y = y;
    c[4].z = bone[2];
    c[4].u = static_cast<float>((m.uv[0][0] + m.uv[1][0] + m.uv[2][0] + m.uv[3][0]) / 4);
    c[4].v = static_cast<float>((m.uv[0][1] + m.uv[1][1] + m.uv[2][1] + m.uv[3][1]) / 4);
    for (auto& v : c) {
        v.r = m.r * s; v.g = m.g * s; v.b = m.b * s;
        v.nx = 0.0f; v.ny = -1.0f; v.nz = 0.0f;   // flat on the floor, Y down
        v.phase = -1.0f;                          // this mesh does not shimmer
    }
    fan(g, c);
    widen(g);
    return true;
}

}  // namespace omk
