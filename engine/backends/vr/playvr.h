// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// THE VIEWER'S VR HALF - its state (`todo/quest-port.md` §5). One struct, so
// `PlayState` carries one member for all of it; the methods that use it are
// `PlayState::vr*` in `backends/vr/playvr_<part>.cpp`. Compiled only with
// OMK_VR (the main Makefile's `VR=1`); a build without it links
// `backends/sdl/playvr_off.cpp`'s empty methods and never sees this file.
#if OMK_VR

#include "o3de/raster.h"
#include "vr/xrspace.h"

// WHICH CAMERA THE FRAME HAS, as the headset sees it (step 2): what the
// headset's origin is, and when it recentres.
enum class VrKind {
    World,         // an authored camera: a script's world camera, the follow camera
    FirstPerson,   // adventure with the player at the keys: the origin is his head
    Dialogue,      // a conversation's camera
    Editing,       // a cutscene's camera editing (mode 13)
    Fight,         // melee's camera (mode 14), calmed
    Ride,          // a slider ride's camera (mode 8)
    Shoot,         // shoot mode's first-person camera (step 3 aims it)
};
const char* vrKindName(VrKind k);

struct VrState {
    // ---- the command line (`--vr-help`)
    bool on = false;            // a headset this run: the fake one, or a frontend's
    bool sim = false;           // --vr-sim: the desktop's FAKE headset
    bool mono = false;          // --vr-sim=mono: one eye, the whole frame
    omk::vr::CameraOrientation orient = omk::vr::CameraOrientation::Level;
    float ipdMm = 64.0f;        // --vr-ipd=MM
    bool questFov = false;      // --vr-fov=quest2: a nominal asymmetric eye
    // the fake head: degrees, yaw RIGHT, pitch UP, roll toward the RIGHT
    // shoulder (--vr-head=Y,P,R; the numpad moves it), and metres (--vr-headpos)
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    float headPos[3]{0.0f, 0.0f, 0.0f};
    // the fake CONTROLLER (right hand): degrees in the headset's local space,
    // yaw right and pitch up - independent of the head, as a held controller
    // is (--vr-aim=Y,P; the mouse moves it in shoot mode)
    float ctlYaw = 0.0f, ctlPitch = 0.0f;
    // --vr-head-after=FRAME:Y,P,R: the fake head turned at a frame, AFTER the
    // recentre a new camera kind makes (which zeroes a head turned from the start)
    long headAfterFrame = -1;
    float headAfter[3]{0.0f, 0.0f, 0.0f};
    // ---- step 2's choices, each a second path beside the authored one
    bool adventureFirst = true;   // --vr-adventure=first|authored
    bool fightCalm = true;        // --vr-fight=calm|authored
    float fightTurnDeg = 1.0f;    // --vr-fight-turn=DEG: the calm camera's turn a frame, at 30 fps
    bool recentreEachCut = true;  // --vr-recentre=cut|scene
    // THE REFERENCE SPACE - the one seam a standing mode will change: where
    // the head's local offsets are measured from. Seated: around the origin
    // as they come, the origin being the authored eye (or his head in first
    // person). Standing would subtract a floor height here instead.
    enum class Space { Seated } space = Space::Seated;

    // ---- this frame
    omk::vr::HeadPose pose;
    bool haveEyes = false;      // the eyes below are this frame's
    omk::RCamera authored;      // the camera the GAME chose, before the head
    omk::RCamera eye[2];        // the cameras the eyes draw with
    int eyeW = 0, eyeH = 0;     // each eye's picture
    long told = -1;             // the frame the setup line was printed on
    VrKind kind = VrKind::World;
    bool haveKind = false;
    // THE RECENTRE: the raw head's yaw and position taken as zero, at a change
    // of camera kind and (--vr-recentre=cut) at each cut of an authored camera
    float zeroYaw = 0.0f;       // radians, OpenXR's sense (+ turns left)
    float zeroPos[3]{0.0f, 0.0f, 0.0f};
    long recentres = 0;
    omk::RCamera prevAuthored;
    bool havePrev = false;
    // first person: the view's heading in the world (horizontal, unit), set
    // from his body when the mode begins and turned by the snap turn only
    float fpFwd[3]{0.0f, 0.0f, 1.0f};
    bool fpInit = false;
    float headFwd[3]{0.0f, 0.0f, 1.0f};   // where the head looked last frame (world, horizontal)
    bool haveHeadFwd = false;
    bool hidePlayer = false;    // first person: his body is not drawn
    // the calm fight camera: the distance frozen at the fight's start, and
    // the direction from the target turned at most fightTurnDeg a frame
    float fightDist = 0.0f;
    float fightDir[3]{0.0f, 0.0f, 0.0f};
    bool fightInit = false;
    bool snapHeld = false;      // the snap turn's key, for its edge
    omk::RCamera origin;        // this frame's origin, for the controller's ray
    bool haveOrigin = false;
    float aimYaw = 0.0f, aimPitch = 0.0f;   // shoot mode: what the controller last aimed
    bool aimed = false;
};

#endif  // OMK_VR
