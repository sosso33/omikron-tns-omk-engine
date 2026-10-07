// SPDX-License-Identifier: GPL-3.0-or-later
// The headset's space and the game's - see `xrspace.h`. Empty unless OMK_VR.
#include "vr/xrspace.h"

#if OMK_VR

#include <algorithm>
#include <cmath>

namespace omk::vr {

namespace {

constexpr float kPi = 3.14159265358979323846f;

float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
void cross3(const float a[3], const float b[3], float o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}
float len3(const float a[3]) { return std::sqrt(dot3(a, a)); }

bool isIdentity(const float q[4]) {
    return q[0] == 0.0f && q[1] == 0.0f && q[2] == 0.0f && (q[3] == 1.0f || q[3] == -1.0f);
}

void quatMul(const float a[4], const float b[4], float o[4]) {
    o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

}  // namespace

void rotate(const float q[4], const float v[3], float out[3]) {
    // v' = v + 2w (q x v) + 2 q x (q x v)
    const float qv[3] = {q[0], q[1], q[2]};
    float t[3], tt[3];
    cross3(qv, v, t);
    for (float& x : t) x *= 2.0f;
    cross3(qv, t, tt);
    for (int k = 0; k < 3; ++k) out[k] = v[k] + q[3] * t[k] + tt[k];
}

void quatFromYawPitchRoll(float yawDeg, float pitchDeg, float rollDeg, float q[4]) {
    const float y = yawDeg * kPi / 360.0f, p = pitchDeg * kPi / 360.0f, r = rollDeg * kPi / 360.0f;
    const float qy[4] = {0.0f, std::sin(y), 0.0f, std::cos(y)};
    const float qp[4] = {std::sin(p), 0.0f, 0.0f, std::cos(p)};
    const float qr[4] = {0.0f, 0.0f, std::sin(r), std::cos(r)};
    float t[4];
    quatMul(qy, qp, t);
    quatMul(t, qr, q);
}

void headToWorld(const RCamera& c, const float v[3], float out[3]) {
    float s[3], u[3], f[3], th = 0.0f, tv = 0.0f;
    cameraBasis(c, s, u, f, th, tv);
    for (int k = 0; k < 3; ++k) out[k] = v[0] * s[k] + v[1] * u[k] - v[2] * f[k];
}

RCamera originCamera(const RCamera& authored, CameraOrientation o) {
    if (o == CameraOrientation::Full) return authored;
    RCamera c = authored;
    float d[3] = {authored.at[0] - authored.eye[0], 0.0f, authored.at[2] - authored.eye[2]};
    const float horiz = len3(d);
    if (horiz < 1e-4f) return authored;   // straight up or down: no heading to keep
    // the same distance to `at`, levelled: the game's Y points down, so a level
    // gaze keeps y and moves only in x and z
    float full[3] = {authored.at[0] - authored.eye[0], authored.at[1] - authored.eye[1],
                     authored.at[2] - authored.eye[2]};
    const float dist = len3(full);
    for (int k = 0; k < 3; ++k) c.at[k] = authored.eye[k] + d[k] / horiz * dist;
    c.rollDeg = 0.0f;
    return c;
}

RCamera composeEye(const RCamera& origin, const Pose& local, const EyeFov* fov, int w, int h) {
    RCamera c = origin;
    c.w = w;
    c.h = h;
    float off[3];
    headToWorld(origin, local.pos, off);
    for (float& x : off) x *= kInchesPerMetre;
    for (int k = 0; k < 3; ++k) c.eye[k] = origin.eye[k] + off[k];
    if (isIdentity(local.quat)) {
        // exact: the authored gaze and roll, only moved
        for (int k = 0; k < 3; ++k) c.at[k] = origin.at[k] + off[k];
    } else {
        const float fwdH[3] = {0.0f, 0.0f, -1.0f}, upH[3] = {0.0f, 1.0f, 0.0f};
        float fh[3], uh[3], fw[3], uw[3];
        rotate(local.quat, fwdH, fh);
        rotate(local.quat, upH, uh);
        headToWorld(origin, fh, fw);
        headToWorld(origin, uh, uw);
        const float dv[3] = {origin.at[0] - origin.eye[0], origin.at[1] - origin.eye[1],
                             origin.at[2] - origin.eye[2]};
        const float dist = std::max(len3(dv), 1.0f);
        for (int k = 0; k < 3; ++k) c.at[k] = c.eye[k] + fw[k] * dist;
        // THE ROLL, recovered in RCamera's own sense: `basisOf` takes s0, u0
        // from (eye, at) with the game's down-pointing up, then turns
        // u' = u0 cos r + s0 sin r - so r = atan2(up . s0, up . u0).
        RCamera unrolled = c;
        unrolled.rollDeg = 0.0f;
        unrolled.mirror = false;
        unrolled.flipX = false;
        float s0[3], u0[3], f0[3], th = 0.0f, tv = 0.0f;
        cameraBasis(unrolled, s0, u0, f0, th, tv);
        c.rollDeg = std::atan2(dot3(uw, s0), dot3(uw, u0)) * 180.0f / kPi;
    }
    if (fov && fov->set()) {
        const float tl = std::tan(fov->left), tr = std::tan(fov->right);
        const float tu = std::tan(fov->up), td = std::tan(fov->down);
        const float th = 0.5f * (tr - tl);
        c.hfovDeg = 2.0f * std::atan(th) * 180.0f / kPi;
        c.lensX = 0.5f * (tr + tl);
        c.tanHalfV = 0.5f * (tu - td);
        c.lensY = 0.5f * (tu + td);
    }
    return c;
}

RCamera cullCamera(const RCamera& left, const RCamera& right, int w, int h) {
    RCamera c = left;
    c.w = w;
    c.h = h;
    c.lensX = c.lensY = 0.0f;
    c.tanHalfV = 0.0f;
    // the widest tangent either eye reaches, in any direction: the radius of
    // the circle that holds both frusta at any roll of the head
    float r = 0.0f;
    for (const RCamera* e : {&left, &right}) {
        float s[3], u[3], f[3], th = 0.0f, tv = 0.0f;
        cameraBasis(*e, s, u, f, th, tv);
        const float hx = th + std::fabs(e->lensX), vy = tv + std::fabs(e->lensY);
        r = std::max(r, std::sqrt(hx * hx + vy * vy));
    }
    // ...as a horizontal half-tangent whose derived vertical (w/h) holds it too
    const float aspect = static_cast<float>(w) / static_cast<float>(h);
    const float tanH = r * std::max(1.0f, aspect);
    c.hfovDeg = 2.0f * std::atan(tanH) * 180.0f / kPi;
    // stand back from the eyes' midpoint so the cone also holds both apexes
    float mid[3], half[3];
    for (int k = 0; k < 3; ++k) {
        mid[k] = 0.5f * (left.eye[k] + right.eye[k]);
        half[k] = 0.5f * (right.eye[k] - left.eye[k]);
    }
    const float sep = len3(half);
    float g[3] = {left.at[0] - left.eye[0], left.at[1] - left.eye[1], left.at[2] - left.eye[2]};
    const float gl = std::max(len3(g), 1e-6f);
    const float back = r > 1e-6f ? sep / r : 0.0f;
    for (int k = 0; k < 3; ++k) {
        c.eye[k] = mid[k] - g[k] / gl * back;
        c.at[k] = c.eye[k] + g[k];
    }
    return c;
}

}  // namespace omk::vr

#endif  // OMK_VR
