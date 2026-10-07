// SPDX-License-Identifier: GPL-3.0-or-later
// THE HEAD ON THE AUTHORED CAMERA, MODE BY MODE (`todo/quest-port.md` §5
// steps 1-2).
//
// Called once, where `PlayState::worldCamera()` ends - after every writer of
// the frame's camera, the instrument override and `lastEye` included, so what
// the GAME remembers of its camera is the authored one and the head never
// reaches it. Here the frame's camera KIND is named, the headset's ORIGIN is
// chosen for it (the authored camera, his head in first person, the calmed
// fight camera), the head is RECENTRED at a change of kind and at a cut, and
// composed. `view.cam` then becomes what the frame CULLS with: the one eye in
// mono, or a camera whose frustum holds both eyes. The eyes draw in
// `playvr_draw.cpp`.
//
// Every choice here is a second path beside the authored one, selected by its
// flag (`--vr-help`); the authored camera is computed exactly as without VR,
// and `--vr-adventure=authored --vr-fight=authored` puts the head on it alone.
#if OMK_VR

#include "../sdl/playframe.h"
#if OMK_OPENXR
#include "../openxr/xrhost.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>

const char* vrKindName(VrKind k) {
    switch (k) {
        case VrKind::World:       return "world";
        case VrKind::FirstPerson: return "first-person";
        case VrKind::Dialogue:    return "dialogue";
        case VrKind::Editing:     return "editing";
        case VrKind::Fight:       return "fight";
        case VrKind::Ride:        return "ride";
        case VrKind::Shoot:       return "shoot";
    }
    return "?";
}

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;

float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
bool normH(float v[3]) {   // horizontal unit vector: the game's Y is the vertical
    v[1] = 0.0f;
    const float l = std::sqrt(v[0] * v[0] + v[2] * v[2]);
    if (l < 1e-6f) return false;
    v[0] /= l; v[2] /= l;
    return true;
}
// the RIGHT of a horizontal heading, as `RCamera`'s basis takes it: f x (0,-1,0)
// (the screen's right for a camera looking along `f`)
void rightOf(const float f[3], float r[3]) {
    r[0] = f[2]; r[1] = 0.0f; r[2] = -f[0];
    // f x (0,-1,0) = (f.y*0 - f.z*(-1), f.z*0 - f.x*0, f.x*(-1) - f.y*0) = (f.z, 0, -f.x)
}

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
    // the fake RIGHT CONTROLLER: held below and to the right of where the
    // head started, aimed by its own yaw and pitch in the headset's space -
    // NOT turned with the head, as a real controller is not
    p.hand[1].pos[0] = 0.18f; p.hand[1].pos[1] = -0.30f; p.hand[1].pos[2] = -0.30f;
    omk::vr::quatFromYawPitchRoll(-v.ctlYaw, v.ctlPitch, 0.0f, p.hand[1].quat);
    p.handValid[1] = true;
}

// The head's YAW in OpenXR's sense (about +y, + turns left), from its forward.
float yawOf(const float q[4]) {
    const float fwd[3] = {0.0f, 0.0f, -1.0f};
    float f[3];
    omk::vr::rotate(q, fwd, f);
    return std::atan2(-f[0], -f[2]);
}

// A pose with the recentre taken off: turned by -zeroYaw about +y, moved by
// -zeroPos first.
omk::vr::Pose recentred(const omk::vr::Pose& raw, float zeroYaw, const float zeroPos[3]) {
    omk::vr::Pose p;
    const float h = -0.5f * zeroYaw;
    const float qz[4] = {0.0f, std::sin(h), 0.0f, std::cos(h)};
    const float d[3] = {raw.pos[0] - zeroPos[0], raw.pos[1] - zeroPos[1], raw.pos[2] - zeroPos[2]};
    omk::vr::rotate(qz, d, p.pos);
    // q = qz * raw.quat
    const float* b = raw.quat;
    p.quat[0] = qz[3] * b[0] + qz[0] * b[3] + qz[1] * b[2] - qz[2] * b[1];
    p.quat[1] = qz[3] * b[1] - qz[0] * b[2] + qz[1] * b[3] + qz[2] * b[0];
    p.quat[2] = qz[3] * b[2] + qz[0] * b[1] - qz[1] * b[0] + qz[2] * b[3];
    p.quat[3] = qz[3] * b[3] - qz[0] * b[0] - qz[1] * b[1] - qz[2] * b[2];
    if (zeroYaw == 0.0f && zeroPos[0] == 0.0f && zeroPos[1] == 0.0f && zeroPos[2] == 0.0f)
        return raw;   // exact: no recentre is no change
    return p;
}

// A CUT, read off the authored camera alone: the eye jumps more than a metre,
// or the gaze turns more than 20 degrees, between two frames. A travel moves
// far less than that in a 30th of a second; a cut does not travel at all.
bool isCut(const omk::RCamera& a, const omk::RCamera& b) {
    float d[3], fa[3], fb[3];
    for (int k = 0; k < 3; ++k) {
        d[k] = b.eye[k] - a.eye[k];
        fa[k] = a.at[k] - a.eye[k];
        fb[k] = b.at[k] - b.eye[k];
    }
    const float jump = std::sqrt(dot3(d, d));
    const float la = std::sqrt(dot3(fa, fa)), lb = std::sqrt(dot3(fb, fb));
    if (la < 1e-6f || lb < 1e-6f) return jump > 39.37f;
    const float c = dot3(fa, fb) / (la * lb);
    return jump > 39.37f || c < std::cos(20.0f * kDegToRad);
}

}  // namespace

void PlayState::vrAfterWorldCamera() {
    vr.haveEyes = false;
    vr.hidePlayer = false;
    if (!vr.on) return;
    // A PANEL WITH A 3D VIEWPORT ITEM - the videophone's caller - renders the
    // world into its rectangle through the GAME's camera, read back and
    // composed into the panel (`playframe_world_draw.cpp`). The eyes took that
    // frame over and the rectangle stayed black (the Quest, Telis's call in
    // the restaurant, 2026-10-07). So the game's view is KEPT here, before
    // the eyes rewrite it, and `vrWorldDraw` draws it once more after them -
    // a flat MONITOR, as a videophone is ("this is a visio so it is correct
    // if the image is flat. But I'd rather keep the shaded world").
    vr.monitor = vpItem != nullptr;
    if (vr.monitor) vr.monitorView = view;
    omk::vr::HeadPose raw;
    bool simRecentre = false;
    if (!front.headPose(raw)) {
        if (!vr.sim) return;
        // the numpad turns the fake head (4/6 yaw, 8/2 pitch, 7/9 roll, 5
        // recentres; 1/3 snap-turn in first person)
        // ...from the keyboard, or from a `--hold` stream (the device state),
        // which is how a check turns the head
        const auto held = [&](int dik) {
            if (host.held.count(dik)) return true;
            for (int k : st.keyboard) if (k == dik) return true;
            return false;
        };
        constexpr float kStep = 1.5f;
        if (held(0x4B)) vr.yaw -= kStep;
        if (held(0x4D)) vr.yaw += kStep;
        if (held(0x48)) vr.pitch += kStep;
        if (held(0x50)) vr.pitch -= kStep;
        if (held(0x47)) vr.roll -= kStep;
        if (held(0x49)) vr.roll += kStep;
        if (held(0x4C)) { vr.yaw = vr.pitch = vr.roll = 0.0f; simRecentre = true; }
        if (vr.headAfterFrame >= 0 && n >= vr.headAfterFrame) {
            vr.headAfterFrame = -1;
            vr.yaw = vr.headAfter[0]; vr.pitch = vr.headAfter[1]; vr.roll = vr.headAfter[2];
            std::printf("frame %ld: vr - the fake head turned to yaw %.1f pitch %.1f roll %.1f "
                        "(--vr-head-after)\n", n, static_cast<double>(vr.yaw),
                        static_cast<double>(vr.pitch), static_cast<double>(vr.roll));
        }
        simPose(vr, raw);
    }
    if (!raw.valid) return;
#if OMK_OPENXR
    // THE PANEL PLACED WHERE THE HEAD LOOKS (step 6b): when a screen opens
    // and when a conversation starts - fixed in the world until the next
    {
        const bool dlg = session_->dialogOpen();
        if ((openScreen >= 0 && openScreen != vr.lastScreen) || (dlg && !vr.lastDialog))
            omk::xr::anchorQuad();
        vr.lastScreen = openScreen;
        vr.lastDialog = dlg;
    }
#endif

    // ---- THE KIND, from what the frame's camera code chose
    VrKind kind = VrKind::World;
    if (fightRun.active) kind = VrKind::Fight;
    else if (haveDlgCam) kind = VrKind::Dialogue;
    else if (haveEdit || holdEditCam) kind = VrKind::Editing;
    else if (ride.has_value()) kind = VrKind::Ride;
    // shoot mode while ITS first-person camera holds; a script's camera
    // taking the view in shoot mode (`shootCameraLive` false) is authored
    else if (shootMode && shootCameraLive) kind = VrKind::Shoot;
    else if (shootMode) kind = VrKind::World;
    else if (vr.adventureFirst && adventure && player && playerReady && !playerProgram &&
             !boarded && !leaving && !session_->dialogOpen())
        kind = VrKind::FirstPerson;
    // UNDER A SCREEN the kind is the one it opened over (step 6b): the world
    // drawn behind the sneak must not switch first person to the authored
    // camera, and recentre, because the screen took `adventure` away
    if (openScreen >= 0 && vr.haveKind) kind = vr.kind;
    const bool kindChanged = !vr.haveKind || kind != vr.kind;

    // ---- THE ORIGIN for the kind
    const omk::RCamera authored = view.cam;
    omk::RCamera originSrc = authored;
    if (kind == VrKind::FirstPerson || kind == VrKind::Shoot) {
        // his head, as shoot mode's first-person eye is placed (`worldCamera`'s
        // shoot branch): the pelvis lifted by `headLift`, yaw only. In shoot
        // mode too: there the CONTROLLER turns his body (`vrAdventureInput`),
        // so the view cannot ride his facing, or it would turn twice
        const float lift = player->headLift();
        const float eyeOff[3] = {0.0f, lift, 0.0f}, atOff[3] = {0.0f, lift, 100.0f};
        const omk::FollowCamera fc = player->resolveOffsetsYaw(eyeOff, atOff, authored.hfovDeg);
        if (!vr.fpInit || kindChanged) {
            // the mode begins looking where his body faces
            for (int k = 0; k < 3; ++k) vr.fpFwd[k] = fc.at[k] - fc.eye[k];
            if (!normH(vr.fpFwd)) { vr.fpFwd[0] = 0.0f; vr.fpFwd[2] = 1.0f; }
            vr.fpInit = true;
        }
        // the SNAP TURN, 30 degrees an edge (numpad 1 / 3; a stick on a headset)
        const auto down = [&](int dik) {
            if (host.held.count(dik)) return true;
            for (int k : st.keyboard) if (k == dik) return true;
            return false;
        };
        // ...and the RIGHT STICK pushed sideways past 600 (the Quest, 2026-10-07)
        const bool l = down(0x4F) || host.pad.rx < -600, r = down(0x51) || host.pad.rx > 600;
        if ((l || r) && !vr.snapHeld) {
            const float a = (r ? 30.0f : -30.0f) * kDegToRad;
            float rt[3];
            rightOf(vr.fpFwd, rt);
            for (int k = 0; k < 3; ++k) vr.fpFwd[k] = vr.fpFwd[k] * std::cos(a) + rt[k] * std::sin(a);
            normH(vr.fpFwd);
        }
        vr.snapHeld = l || r;
        for (int k = 0; k < 3; ++k) {
            originSrc.eye[k] = fc.eye[k];
            originSrc.at[k] = fc.eye[k] + vr.fpFwd[k] * 100.0f;
        }
        originSrc.rollDeg = 0.0f;
        // shoot mode hides him itself (`Shoot_Enter`'s `sub_436CE0`) and
        // draws the arm; first person hides the whole body here
        vr.hidePlayer = kind == VrKind::FirstPerson;
    } else {
        vr.fpInit = false;
    }
    if (kind == VrKind::Fight && vr.fightCalm) {
        // THE CALM FIGHT CAMERA, over `Fight_TickCamera`'s own output: the
        // target as it chose it, the eye at the distance of the fight's first
        // frame (no zoom in or out), its direction from the target turned
        // toward the authored one by at most `fightTurnDeg` a frame - which
        // also slows the throw swing and the steady orbit
        float d[3];
        for (int k = 0; k < 3; ++k) d[k] = authored.eye[k] - authored.at[k];
        const float l = std::sqrt(dot3(d, d));
        if (l > 1e-3f) {
            for (float& x : d) x /= l;
            if (!vr.fightInit || kindChanged) {
                vr.fightDist = l;
                for (int k = 0; k < 3; ++k) vr.fightDir[k] = d[k];
                vr.fightInit = true;
            } else {
                const float c = std::max(-1.0f, std::min(1.0f, dot3(vr.fightDir, d)));
                const float ang = std::acos(c);
                const float maxA = vr.fightTurnDeg * kDegToRad * static_cast<float>(frameSec * 30.0);
                if (ang > 1e-5f) {
                    const float t = ang <= maxA ? 1.0f : maxA / ang;
                    // slerp from fightDir toward d by t
                    const float s = std::sin(ang);
                    const float wa = std::sin((1.0f - t) * ang) / s, wb = std::sin(t * ang) / s;
                    for (int k = 0; k < 3; ++k) vr.fightDir[k] = wa * vr.fightDir[k] + wb * d[k];
                }
            }
            for (int k = 0; k < 3; ++k) originSrc.eye[k] = authored.at[k] + vr.fightDir[k] * vr.fightDist;
        }
    } else {
        vr.fightInit = false;
    }

    // ---- THE RECENTRE: a new kind, and (by default) a cut of the authored camera
    bool recentre = kindChanged || simRecentre;
    if (!recentre && vr.recentreEachCut && vr.havePrev && kind != VrKind::FirstPerson &&
        kind != VrKind::Fight && isCut(vr.prevAuthored, authored))
        recentre = true;
    if (recentre) {
        vr.zeroYaw = yawOf(raw.head.quat);
        for (int k = 0; k < 3; ++k) vr.zeroPos[k] = raw.head.pos[k];
        ++vr.recentres;
        std::printf("frame %ld: vr - camera %s%s, the head recentred (%ld)\n", n, vrKindName(kind),
                    kindChanged ? "" : " - a CUT", vr.recentres);
    }
    vr.kind = kind;
    vr.haveKind = true;
    vr.prevAuthored = authored;
    vr.havePrev = true;
    vr.pose = raw;
    vr.pose.head = recentred(raw.head, vr.zeroYaw, vr.zeroPos);
    for (int e = 0; e < 2; ++e) vr.pose.eye[e] = recentred(raw.eye[e], vr.zeroYaw, vr.zeroPos);
    for (int e = 0; e < 2; ++e)
        if (raw.handValid[e]) vr.pose.hand[e] = recentred(raw.hand[e], vr.zeroYaw, vr.zeroPos);

    // ---- COMPOSED
    vr.authored = authored;
    const omk::RCamera origin = omk::vr::originCamera(originSrc, vr.orient);
    vr.origin = origin;
    vr.haveOrigin = true;
    if (vr.mono) {
        // ONE eye, the frame's own size and letterbox: everything flat but
        // the camera, so an unturned head on the authored camera draws the
        // flat frame to the bit
        vr.eyeW = view.cam.w;
        vr.eyeH = view.cam.h;
        const omk::vr::EyeFov* fov = vr.pose.fov[0].set() ? &vr.pose.fov[0] : nullptr;
        vr.eye[0] = vr.eye[1] = omk::vr::composeEye(origin, vr.pose.head, fov, vr.eyeW, vr.eyeH);
        view.cam = vr.eye[0];
    } else {
        // both eyes, side by side over the whole frame: no letterbox - or, on
        // a headset, at the size its swapchain asks for (`HeadPose::eyeW`)
        vr.eyeW = raw.eyeW > 0 ? raw.eyeW : fb.w / 2;
        vr.eyeH = raw.eyeH > 0 ? raw.eyeH : fb.h;
        for (int e = 0; e < 2; ++e) {
            const omk::vr::EyeFov* fov = vr.pose.fov[e].set() ? &vr.pose.fov[e] : nullptr;
            vr.eye[e] = omk::vr::composeEye(origin, vr.pose.eye[e], fov, vr.eyeW, vr.eyeH);
        }
        view.vx = view.vy = view.vw = view.vh = 0;
        view.cam = omk::vr::cullCamera(vr.eye[0], vr.eye[1], 2 * vr.eyeW, vr.eyeH);
    }
    // where the head looks, for the look-relative walk (`vrAdventureInput`)
    for (int k = 0; k < 3; ++k) vr.headFwd[k] = vr.eye[0].at[k] - vr.eye[0].eye[k];
    vr.haveHeadFwd = normH(vr.headFwd);
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
    static const bool vrLog = std::getenv("OMK_VRLOG") && *std::getenv("OMK_VRLOG") == '1';
    if (vrLog)
        std::printf("[vr] frame %ld kind %s target %.2f %.2f %.2f authored %.2f %.2f %.2f drawn %.2f %.2f %.2f "
                    "look %.3f %.3f %.3f origin %.2f %.2f %.2f him %.2f %.2f %.2f facing %.2f pitch %.2f\n", n, vrKindName(kind),
                    static_cast<double>(authored.at[0]), static_cast<double>(authored.at[1]),
                    static_cast<double>(authored.at[2]),
                    static_cast<double>(authored.eye[0]), static_cast<double>(authored.eye[1]),
                    static_cast<double>(authored.eye[2]), static_cast<double>(vr.eye[0].eye[0]),
                    static_cast<double>(vr.eye[0].eye[1]), static_cast<double>(vr.eye[0].eye[2]),
                    static_cast<double>(vr.headFwd[0]), static_cast<double>(vr.headFwd[1]),
                    static_cast<double>(vr.headFwd[2]),
                    static_cast<double>(origin.eye[0]), static_cast<double>(origin.eye[1]),
                    static_cast<double>(origin.eye[2]),
                    player ? static_cast<double>(player->pos()[0]) : 0.0,
                    player ? static_cast<double>(player->pos()[1]) : 0.0,
                    player ? static_cast<double>(player->pos()[2]) : 0.0,
                    player ? static_cast<double>(player->facing()) : 0.0,
                    static_cast<double>(shootPitch));
}

bool PlayState::vrHidesPlayer() const { return vr.on && vr.hidePlayer; }

// THE WORLD BEHIND A SCREEN THAT HIDES IT (step 6b, the reader's request,
// 2026-10-07: "a small shade filter on the background when a ui is open").
// The original turns the 3D view off behind every screen but three
// (`UI_LoadScreen`'s `sub_466B30`, `playframe_world.cpp`); in a headset
// that leaves the window in a void. A HEADSET build - the frontend pacing,
// not the desktop's fake one - draws it on, darkened to `--vr-shade`. Only
// the DRAWING: Kay'l stays held and the sound suspended as the screen set
// them. A screen that keeps the world (or a panel that dims it itself)
// changes nothing here and gets no extra shade.
// ...and the one draw list must hold what BOTH cameras see: the side planes
// are the eyes', so on a monitor frame nothing is side-culled (the distance
// cull is from an eye a few metres from the game's, against 150-200 m)
bool PlayState::vrNoSideCull() const { return vr.on && vr.monitor; }

bool PlayState::vrDrawsBehindScreen(bool screenKeepsWorld) {
    vr.shadeWorld = false;
    if (!vr.on || vr.sim || !front.paces() || openScreen < 0 || screenKeepsWorld) return false;
    vr.shadeWorld = true;
    return true;
}

// THE LOOK-RELATIVE WALK (§3b 7): in first person, the four direction bits
// the bindings produced (`Tourner a gauche/droite`, `Avancer`, `Reculer` -
// bits 1, 2, 4, 8 of the Aventure group) are read as a direction RELATIVE TO
// WHERE THE HEAD LOOKS, and turned back into the same four bits for the body:
// turn toward that direction, walk while it is within 70 degrees. Nothing new
// moves him - the `.CTL` channel and the walker take the word they always take.
void PlayState::vrAdventureInput(std::uint32_t& word) {
    // NOT WHILE A SCREEN IS UP: `vr.kind` is the last WORLD frame's, and the
    // world camera does not run under a screen, so the sneak opened from first
    // person still read as first person - and every stick push reached its
    // rows as `Avancer`, "up", while turning him (the Quest, 2026-10-07: "stick
    // input was not recognized" in the sneak, confirm was)
    if (openScreen >= 0) return;
    if (vr.on && vr.kind == VrKind::Shoot && player) { vrShootAim(word); return; }
    if (!vr.on || !adventure || vr.kind != VrKind::FirstPerson || !vr.haveHeadFwd || !player) return;
    if (word & 0x40000000u) return;   // the idle word: nothing held
    // THE RIGHT STICK IS THE SNAP TURN in first person (`vrAfterWorldCamera`),
    // and the head does the looking: its three button slots - 8 sideways, 9
    // up, 12 down (`input/pad.h`) - are taken out of the word, or pushing it
    // to turn pressed `Regarder` and the shoot group's Action as well
    word &= ~(0x100u | 0x200u | 0x1000u);
    const std::uint32_t dirBits = word & 0xFu;
    word &= ~0xFu;
    // THE DIRECTION: the stick's own, analog, when a pad is pushed past the
    // dead zone - else the keys' eight. x right, y forward, both relative to
    // where the head looks.
    float x = 0.0f, y = 0.0f;
    const float px = static_cast<float>(host.pad.lx) / 1000.0f, py = -static_cast<float>(host.pad.ly) / 1000.0f;
    if (std::sqrt(px * px + py * py) * 1000.0f > static_cast<float>(omk::pad::kDeadZone)) {
        x = px;
        y = py;
    } else {
        x = ((dirBits & 2u) ? 1.0f : 0.0f) - ((dirBits & 1u) ? 1.0f : 0.0f);
        y = ((dirBits & 4u) ? 1.0f : 0.0f) - ((dirBits & 8u) ? 1.0f : 0.0f);
    }
    if (x == 0.0f && y == 0.0f) {
        if (!(word & 0x3FFFu)) word = 0x40000000u;
        return;
    }
    // HE FACES WHERE THE STICK POINTS AND WALKS: the facing set outright, as
    // the controller sets it in shoot mode (`vrShootAim`), and `Avancer`. It
    // replaced turning his body toward the direction with the tank controls'
    // own `Tourner` bits and walking only within 70 degrees of it (step 2),
    // which on the Quest (2026-10-07) walked forward, turned on the spot
    // before any sideways step, and never went backward: at 180 degrees the
    // turn's sign flipped from frame to frame.
    float hr[3];
    rightOf(vr.headFwd, hr);
    float want[3] = {vr.headFwd[0] * y + hr[0] * x, 0.0f, vr.headFwd[2] * y + hr[2] * x};
    if (!normH(want)) return;
    const float eyeOff[3] = {0.0f, 0.0f, 0.0f}, atOff[3] = {0.0f, 0.0f, 100.0f};
    const omk::FollowCamera fc = player->resolveOffsetsYaw(eyeOff, atOff, 75.0f);
    float far[3];
    for (int k = 0; k < 3; ++k) far[k] = fc.eye[k] + want[k] * 10000.0f;
    const int k0[3] = {0, 0, 0};
    player->setFacing(omk::shootGunmanAim(far, fc.eye, 0.0f, k0).yawDeg);
    word |= 4u;   // Avancer
}

#endif  // OMK_VR

#if OMK_VR
// SHOOT MODE AIMS WITH THE CONTROLLER (§3b 8, step 3). The engine aims the
// player's shot along his FACING and the look PITCH (`rs.yawDeg = facing`,
// `rs.pitchDeg = shootPitch` where `Actor_TickProjectiles` fires), and the arm
// follows the same two (`shootAimSlew`: yaw toward 0 - the body turns with the
// look - pitch toward the look's). So the controller does what the mouse did:
// its ray, taken into the world through this frame's origin, sets his facing
// and the look pitch through `shootGunmanAim` - the solver `--aim-at` uses -
// toward a point far along the ray, and the shot, the arm and his body follow
// as they always do. The keyboard's turn and look bits (`Tourner`, `Regarder
// En-Haut/En-Bas`: 0x1, 0x2, 0x200, 0x1000 in the Tirer group) are taken out
// of the word, since the controller owns both; the view keeps its own heading
// (the snap turn) and the head looks freely. Pitch is held to the mouse's own
// +-45 (`actor/shootmove.h`), the range the arm's keys are authored over.
void PlayState::vrShootAim(std::uint32_t& word) {
    // THE STICK'S SIDEWAYS PUSH STRAFES (the reader, 2026-10-07: "the stick
    // should also make lateral steps, not just going forward"). `Input_Poll`
    // hardwires the stick's x to *Tirer*'s `Tourner` bits 1 / 2, and the
    // controller already aims, so they were simply dropped; they become the
    // group's own `Glisser a gauche / droite`, bits 0x400 / 0x800, which the
    // `.CTL` channel queues as MDDG / MDDD - the side steps the arrows give a
    // keyboard player (actor/shootmove.h)
    if (word & 0x1u) word |= 0x400u;
    if (word & 0x2u) word |= 0x800u;
    word &= ~(0x1u | 0x2u | 0x200u | 0x1000u);
    if (!(word & 0x3FFFu)) word = 0x40000000u;
    if (vr.sim) {
        // the mouse moves the fake controller, and nothing else: zeroed here,
        // so `adventureAim` does not turn his body with it as well
        vr.ctlYaw += host.mouseDX * 0.15f;
        vr.ctlPitch = std::max(-70.0f, std::min(70.0f, vr.ctlPitch - host.mouseDY * 0.15f));
        host.mouseDX = host.mouseDY = 0.0f;
    }
    if (!vr.haveOrigin || !vr.pose.handValid[1]) return;
    const omk::vr::Pose& h = vr.pose.hand[1];
    float off[3], dirH[3], dir[3];
    omk::vr::headToWorld(vr.origin, h.pos, off);
    const float fwd[3] = {0.0f, 0.0f, -1.0f};
    omk::vr::rotate(h.quat, fwd, dirH);
    omk::vr::headToWorld(vr.origin, dirH, dir);
    float from[3], far[3];
    for (int k = 0; k < 3; ++k) {
        from[k] = vr.origin.eye[k] + off[k] * omk::vr::kInchesPerMetre;
        far[k] = from[k] + dir[k] * 10000.0f;
    }
    const int k0[3] = {0, 0, 0};
    const omk::GunmanAim a = omk::shootGunmanAim(far, from, 0.0f, k0);
    player->setFacing(a.yawDeg);
    shootPitch = std::max(-45.0f, std::min(45.0f, a.pitchDeg));
    if (!vr.aimed || std::fabs(a.yawDeg - vr.aimYaw) > 5.0f || std::fabs(shootPitch - vr.aimPitch) > 5.0f)
        std::printf("frame %ld: vr - the controller aims: facing %.1f, look pitch %.1f\n", n,
                    static_cast<double>(a.yawDeg), static_cast<double>(shootPitch));
    vr.aimed = true;
    vr.aimYaw = a.yawDeg;
    vr.aimPitch = shootPitch;
}
#endif
