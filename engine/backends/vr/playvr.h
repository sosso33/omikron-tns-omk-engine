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

    // ---- this frame
    omk::vr::HeadPose pose;
    bool haveEyes = false;      // the eyes below are this frame's
    omk::RCamera authored;      // the camera the GAME chose, before the head
    omk::RCamera eye[2];        // the cameras the eyes draw with
    int eyeW = 0, eyeH = 0;     // each eye's picture
    long told = -1;             // the frame the setup line was printed on
};

#endif  // OMK_VR
