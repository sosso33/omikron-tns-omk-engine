// SPDX-License-Identifier: GPL-3.0-or-later
// THE HEAD ON THE AUTHORED CAMERA, MEASURED - `vr/xrspace.*`
// (`todo/quest-port.md` §5 step 1, `verify.py: engine: vr camera rule`).
//
//     vr_probe
//
// No data: the composition is arithmetic, and each line below is one thing a
// headset must get right that a still frame cannot show - the SENSE of a turn
// (a mirrored yaw looks like a plausible picture), the units (inches against
// metres), the off-axis eye, and the culling camera holding both eyes. Prints
// `name value` lines for the check to assert; a build without OMK_VR says so.
#include "o3de/raster.h"
#include "vr/xrspace.h"

#include <cmath>
#include <cstdio>

#if OMK_VR

namespace {

constexpr float kDeg = 3.14159265358979323846f / 180.0f;

omk::RCamera authored() {
    omk::RCamera c;
    c.eye[0] = 100.0f; c.eye[1] = -50.0f; c.eye[2] = 200.0f;
    c.at[0] = 100.0f;  c.at[1] = -50.0f;  c.at[2] = 600.0f;   // level, along +z
    c.hfovDeg = 75.0f;
    c.w = 800; c.h = 600;
    return c;
}

void dir(const omk::RCamera& c, float d[3]) {
    float l = 0.0f;
    for (int k = 0; k < 3; ++k) { d[k] = c.at[k] - c.eye[k]; l += d[k] * d[k]; }
    l = std::sqrt(l);
    for (int k = 0; k < 3; ++k) d[k] /= l;
}
float dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// a head turned by the desktop's convention (yaw right, pitch up, roll to the
// right shoulder), as `playvr_camera.cpp`'s fake headset turns it
omk::vr::Pose head(float yawRight, float pitchUp, float rollRight) {
    omk::vr::Pose p;
    omk::vr::quatFromYawPitchRoll(-yawRight, pitchUp, -rollRight, p.quat);
    return p;
}

}  // namespace

int main() {
    const omk::RCamera a = authored();
    float s[3], u[3], f[3], th = 0.0f, tv = 0.0f;
    omk::cameraBasis(a, s, u, f, th, tv);

    // 1. THE SENSE OF EACH TURN, against the authored camera's own axes
    {
        const omk::RCamera c = omk::vr::composeEye(a, head(30.0f, 0.0f, 0.0f), nullptr, a.w, a.h);
        float d[3]; dir(c, d);
        std::printf("yaw30_right %.4f\n", dot(d, s));     // sin 30 = 0.5: turned RIGHT
        std::printf("yaw30_ahead %.4f\n", dot(d, f));     // cos 30
    }
    {
        const omk::RCamera c = omk::vr::composeEye(a, head(0.0f, 20.0f, 0.0f), nullptr, a.w, a.h);
        float d[3]; dir(c, d);
        std::printf("pitch20_up %.4f\n", dot(d, u));      // sin 20 = 0.342: looking UP
    }
    {
        const omk::RCamera c = omk::vr::composeEye(a, head(0.0f, 0.0f, 15.0f), nullptr, a.w, a.h);
        float cs[3], cu[3], cf[3];
        omk::cameraBasis(c, cs, cu, cf, th, tv);
        std::printf("roll15_up_toward_right %.4f\n", dot(cu, s));   // sin 15: the head's top to the right
        std::printf("roll15_deg %.3f\n", c.rollDeg);
    }

    // 2. THE UNITS: 64 mm between the eyes is 2.52 inches, along the camera's right
    {
        omk::vr::Pose l, r;
        l.pos[0] = -0.032f; r.pos[0] = 0.032f;
        const omk::RCamera cl = omk::vr::composeEye(a, l, nullptr, a.w / 2, a.h);
        const omk::RCamera cr = omk::vr::composeEye(a, r, nullptr, a.w / 2, a.h);
        const float d[3] = {cr.eye[0] - cl.eye[0], cr.eye[1] - cl.eye[1], cr.eye[2] - cl.eye[2]};
        std::printf("ipd64_inches %.4f\n", std::sqrt(dot(d, d)));
        std::printf("ipd64_along_right %.4f\n", dot(d, s));
    }

    // 3. AN UNTURNED HEAD IS THE AUTHORED CAMERA TO THE BIT
    {
        const omk::vr::Pose id;
        const omk::RCamera c = omk::vr::composeEye(a, id, nullptr, a.w, a.h);
        bool same = c.rollDeg == a.rollDeg && c.hfovDeg == a.hfovDeg;
        for (int k = 0; k < 3; ++k) same = same && c.eye[k] == a.eye[k] && c.at[k] == a.at[k];
        std::printf("identity_exact %d\n", same ? 1 : 0);
    }

    // 4. LEVEL drops the authored pitch and roll; FULL keeps them
    {
        omk::RCamera p = a;
        p.at[1] = -250.0f;      // looking up (Y points down)
        p.rollDeg = 10.0f;
        const omk::RCamera lv = omk::vr::originCamera(p, omk::vr::CameraOrientation::Level);
        const omk::RCamera fu = omk::vr::originCamera(p, omk::vr::CameraOrientation::Full);
        std::printf("level_gaze_dy %.4f\n", lv.at[1] - lv.eye[1]);
        std::printf("level_roll %.3f\n", lv.rollDeg);
        std::printf("full_roll %.3f\n", fu.rollDeg);
    }

    // 5. THE OFF-AXIS EYE: a point on each edge of the eye's own field of view
    // lands on that edge of its picture
    omk::vr::EyeFov fov;
    fov.left = -52.0f * kDeg; fov.right = 44.0f * kDeg;
    fov.up = 48.0f * kDeg;    fov.down = -52.0f * kDeg;
    {
        const omk::vr::Pose id;
        const omk::RCamera c = omk::vr::composeEye(a, id, &fov, 400, 600);
        float p[3];
        const auto at = [&](float tx, float ty) {
            for (int k = 0; k < 3; ++k) p[k] = c.eye[k] + 100.0f * (f[k] + tx * s[k] + ty * u[k]);
            return omk::project(c, p);
        };
        std::printf("lens_right_edge_x %.2f\n", at(std::tan(fov.right), 0.0f).x);   // 400
        std::printf("lens_left_edge_x %.2f\n", at(std::tan(fov.left), 0.0f).x);     // 0
        std::printf("lens_top_edge_y %.2f\n", at(0.0f, std::tan(fov.up)).y);        // 0
        std::printf("lens_bottom_edge_y %.2f\n", at(0.0f, std::tan(fov.down)).y);   // 600
    }

    // 6. THE CULLING CAMERA HOLDS BOTH EYES: every eye-frustum corner, near
    // and far, at several head turns, projects inside its frame
    {
        long inside = 0, total = 0;
        for (float yaw : {0.0f, 35.0f, -70.0f})
            for (float roll : {0.0f, 25.0f}) {
                omk::vr::Pose hp = head(yaw, 10.0f, roll);
                omk::vr::Pose el = hp, er = hp;
                const float half[3] = {0.032f, 0.0f, 0.0f};
                float off[3];
                omk::vr::rotate(hp.quat, half, off);
                for (int k = 0; k < 3; ++k) { el.pos[k] -= off[k]; er.pos[k] += off[k]; }
                omk::vr::EyeFov fr = fov;   // the right eye mirrors the left
                fr.left = -fov.right; fr.right = -fov.left;
                const omk::RCamera cl = omk::vr::composeEye(a, el, &fov, 400, 600);
                const omk::RCamera cr = omk::vr::composeEye(a, er, &fr, 400, 600);
                const omk::RCamera cc = omk::vr::cullCamera(cl, cr, 800, 600);
                for (const omk::RCamera* e : {&cl, &cr}) {
                    const omk::vr::EyeFov& ef = e == &cl ? fov : fr;
                    float es[3], eu[3], ef3[3], t1 = 0.0f, t2 = 0.0f;
                    omk::RCamera flat = *e;
                    flat.lensX = flat.lensY = 0.0f;
                    flat.tanHalfV = 0.0f;
                    omk::cameraBasis(flat, es, eu, ef3, t1, t2);
                    for (float depth : {3.0f, 50.0f, 5000.0f})
                        for (float tx : {std::tan(ef.left), std::tan(ef.right)})
                            for (float ty : {std::tan(ef.down), std::tan(ef.up)}) {
                                float p[3];
                                for (int k = 0; k < 3; ++k)
                                    p[k] = e->eye[k] + depth * (ef3[k] + tx * es[k] + ty * eu[k]);
                                const omk::Projected q = omk::project(cc, p);
                                ++total;
                                if (q.ahead && q.x >= -0.5f && q.x <= 800.5f && q.y >= -0.5f && q.y <= 600.5f)
                                    ++inside;
                            }
                }
            }
        std::printf("cull_holds_eyes %ld %ld\n", inside, total);
    }
    return 0;
}

#else

int main() {
    std::printf("vr_probe: not built with OMK_VR\n");
    return 0;
}

#endif
