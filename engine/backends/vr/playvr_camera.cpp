// SPDX-License-Identifier: GPL-3.0-or-later
// THE HEAD ON THE AUTHORED CAMERA (`todo/quest-port.md` §5 steps 1-2).
//
// Called once, where `PlayState::worldCamera()` ends - after every writer of
// the frame's camera, the instrument override and `lastEye` included, so what
// the GAME remembers of its camera is the authored one and the head never
// reaches it. Here the authored camera is kept, the head is composed on it,
// and `view.cam` becomes what the frame CULLS with: the one eye in mono, or a
// camera whose frustum holds both eyes. The eyes draw in `playvr_draw.cpp`.
#if OMK_VR

#include "../sdl/playframe.h"

#include <cmath>

namespace {

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

// THE FAKE HEADSET's eyes: the head turned by yaw / pitch / roll, each eye half
// the inter-eye distance along the head's own x. Its sense (yaw right, pitch
// up, roll to the right shoulder) is turned into OpenXR's here, once.
void simPose(const VrState& v, omk::vr::HeadPose& p) {
    p = omk::vr::HeadPose{};
    p.valid = true;
    omk::vr::quatFromYawPitchRoll(-v.yaw, v.pitch, -v.roll, p.head.quat);
    for (int k = 0; k < 3; ++k) p.head.pos[k] = v.headPos[k];
    const float half = v.mono ? 0.0f : 0.5f * v.ipdMm / 1000.0f;
    for (int e = 0; e < 2; ++e) {
        const float side[3] = {e == 0 ? -half : half, 0.0f, 0.0f};
        float off[3];
        omk::vr::rotate(p.head.quat, side, off);
        for (int k = 0; k < 3; ++k) p.eye[e].pos[k] = p.head.pos[k] + off[k];
        for (int k = 0; k < 4; ++k) p.eye[e].quat[k] = p.head.quat[k];
        if (v.questFov) {
            // NOMINAL, not measured: an eye wider on its outer side, the shape
            // a Quest 2 reports. The left eye's outer side is its left.
            const float outer = 52.0f * kDegToRad, inner = 44.0f * kDegToRad;
            p.fov[e].left = -(e == 0 ? outer : inner);
            p.fov[e].right = e == 0 ? inner : outer;
            p.fov[e].up = 48.0f * kDegToRad;
            p.fov[e].down = -52.0f * kDegToRad;
        }
    }
}

}  // namespace

void PlayState::vrAfterWorldCamera() {
    vr.haveEyes = false;
    if (!vr.on) return;
    if (!front.headPose(vr.pose)) {
        if (!vr.sim) return;
        // the numpad turns the fake head (4/6 yaw, 8/2 pitch, 7/9 roll, 5 recentres)
        const auto held = [&](int dik) { return host.held.count(dik) != 0; };
        constexpr float kStep = 1.5f;
        if (held(0x4B)) vr.yaw -= kStep;
        if (held(0x4D)) vr.yaw += kStep;
        if (held(0x48)) vr.pitch += kStep;
        if (held(0x50)) vr.pitch -= kStep;
        if (held(0x47)) vr.roll -= kStep;
        if (held(0x49)) vr.roll += kStep;
        if (held(0x4C)) vr.yaw = vr.pitch = vr.roll = 0.0f;
        simPose(vr, vr.pose);
    }
    if (!vr.pose.valid) return;
    vr.authored = view.cam;
    const omk::RCamera origin = omk::vr::originCamera(view.cam, vr.orient);
    if (vr.mono) {
        // ONE eye, the frame's own size and letterbox: everything flat but
        // the camera, so an unturned head draws the flat frame to the bit
        vr.eyeW = view.cam.w;
        vr.eyeH = view.cam.h;
        const omk::vr::EyeFov* fov = vr.pose.fov[0].set() ? &vr.pose.fov[0] : nullptr;
        vr.eye[0] = vr.eye[1] = omk::vr::composeEye(origin, vr.pose.head, fov, vr.eyeW, vr.eyeH);
        view.cam = vr.eye[0];
    } else {
        // both eyes, side by side over the whole frame: no letterbox
        vr.eyeW = fb.w / 2;
        vr.eyeH = fb.h;
        for (int e = 0; e < 2; ++e) {
            const omk::vr::EyeFov* fov = vr.pose.fov[e].set() ? &vr.pose.fov[e] : nullptr;
            vr.eye[e] = omk::vr::composeEye(origin, vr.pose.eye[e], fov, vr.eyeW, vr.eyeH);
        }
        view.vx = view.vy = view.vw = view.vh = 0;
        view.cam = omk::vr::cullCamera(vr.eye[0], vr.eye[1], fb.w, fb.h);
    }
    vr.haveEyes = true;
    if (vr.told < 0) {
        vr.told = n;
        std::printf("frame %ld: vr - the head on the authored camera; eye 0 at %.1f %.1f %.1f "
                    "fov %.1f roll %.2f lens %.3f %.3f\n", n,
                    static_cast<double>(vr.eye[0].eye[0]), static_cast<double>(vr.eye[0].eye[1]),
                    static_cast<double>(vr.eye[0].eye[2]), static_cast<double>(vr.eye[0].hfovDeg),
                    static_cast<double>(vr.eye[0].rollDeg), static_cast<double>(vr.eye[0].lensX),
                    static_cast<double>(vr.eye[0].lensY));
    }
}

#endif  // OMK_VR
