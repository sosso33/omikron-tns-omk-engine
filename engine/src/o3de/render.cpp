// SPDX-License-Identifier: GPL-3.0-or-later
#include "o3de/render.h"

#include <cmath>

namespace omk {
namespace {

void sub3(const float a[3], const float b[3], float o[3]) {
    for (int k = 0; k < 3; ++k) o[k] = a[k] - b[k];
}
float dot3(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
void cross3(const float a[3], const float b[3], float o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}
void norm3(float v[3]) {
    const float l = std::sqrt(dot3(v, v));
    if (l > 0.0f) for (int k = 0; k < 3; ++k) v[k] /= l;
}

}  // namespace

Plane4 planeThroughPoints(const float p1[3], const float p2[3],
                          const float p3[3]) {
    // n = (p2 - p1) x (p3 - p1), d = -n.p1. `sub_442FB0` reaches the same
    // normal through the expanded cross-product terms (p2.z*p1.y - p2.y*p1.z
    // and its siblings) rather than by subtracting first; the two are equal.
    float a[3], b[3];
    sub3(p2, p1, a);
    sub3(p3, p1, b);
    Plane4 pl;
    cross3(a, b, pl.n);
    pl.d = -dot3(pl.n, p1);
    return pl;
}

Frustum frustumFromCamera(const float eye[3], const float at[3],
                          float projX, float projY,
                          int width, int height, float farDist) {
    Frustum f;
    for (int k = 0; k < 3; ++k) f.eye[k] = eye[k];
    f.farDist = farDist;

    float fwd[3];
    sub3(at, eye, fwd);
    norm3(fwd);
    // The game is right-handed with Y pointing DOWN (CLAUDE.md 5), so the
    // world up is -Y. Using +Y mirrors the two horizontal planes, and the cull
    // then keeps exactly the meshes it should reject - invisible in a count,
    // obvious in a picture.
    const float worldUp[3] = {0.0f, -1.0f, 0.0f};
    float right[3], up[3];
    cross3(fwd, worldUp, right);
    norm3(right);
    cross3(right, fwd, up);
    norm3(up);

    // the view rectangle AT THE FAR PLANE, the two axes scaled independently
    const float hw = projX != 0.0f ? farDist * static_cast<float>(width)  / (2.0f * projX) : 0.0f;
    const float hh = projY != 0.0f ? farDist * static_cast<float>(height) / (2.0f * projY) : 0.0f;
    float centre[3], corner[4][3];
    for (int k = 0; k < 3; ++k) centre[k] = eye[k] + fwd[k] * farDist;
    const float sx[4] = {-1, +1, +1, -1};      // going round the rectangle
    const float sy[4] = {-1, -1, +1, +1};
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 3; ++k)
            corner[i][k] = centre[k] + right[k] * (hw * sx[i]) + up[k] * (hh * sy[i]);

    // Four planes, each through the eye and one EDGE of that rectangle - which
    // is what the four `sub_442FB0` calls are handed. The normal's sign is
    // fixed by requiring the view axis to be inside, because a plane through
    // three points has no inherent orientation and the engine's four calls
    // each pick their own winding.
    float axis[3];
    for (int k = 0; k < 3; ++k) axis[k] = eye[k] + fwd[k] * (farDist * 0.5f);
    for (int i = 0; i < 4; ++i) {
        const auto pl = planeThroughPoints(eye, corner[i], corner[(i + 1) % 4]);
        float n[3] = {pl.n[0], pl.n[1], pl.n[2]};
        float d = pl.d;
        norm3(n);
        d /= std::sqrt(dot3(pl.n, pl.n)) > 0 ? std::sqrt(dot3(pl.n, pl.n)) : 1.0f;
        if (dot3(n, axis) + d > 0.0f) {        // the axis must be INSIDE
            for (int k = 0; k < 3; ++k) n[k] = -n[k];
            d = -d;
        }
        for (int k = 0; k < 3; ++k) f.side[i].n[k] = n[k];
        f.side[i].d = d;
    }
    return f;
}

Frustum frustumFromFov(const float eye[3], const float at[3],
                       float fovDegrees, int width, int height, float farDist) {
    // tan(halfH) = width / (2 * projX), so projX = width / (2 * tan(halfH)).
    // The vertical scale is taken so square pixels give the same angle per
    // pixel on both axes - which is what the engine's two independent scales
    // do when the viewport is not stretched.
    const float halfH = fovDegrees * 0.5f * 3.14159265358979f / 180.0f;
    const float projX = static_cast<float>(width) / (2.0f * std::tan(halfH));
    return frustumFromCamera(eye, at, projX, projX, width, height, farDist);
}

CullResult cullMesh(const Mesh& m, const Frustum& f) {
    if (static_cast<std::uint32_t>(m.flags) & 0x40u) return CullResult::Hidden;

    float dv[3];
    sub3(m.pos, f.eye, dv);
    const float d2 = dot3(dv, dv);
    const float reach = m.radius + f.farDist;
    if (!(reach * reach > d2)) return CullResult::TooFar;

    for (const auto& p : f.side)
        if (dot3(p.n, m.pos) + p.d > m.radius) return CullResult::OffSide;
    return CullResult::Visible;
}

}  // namespace omk
