// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// THE HEADSET'S SPACE AND THE GAME'S (`todo/quest-port.md` §5 step 1).
//
// Compiled only into a VR build (`OMK_VR`, the main Makefile's `VR=1`); every
// other target - the Vita, the classic Mac, the 3DS, the PowerPC Mac - builds
// this file empty. It is the platform-free half of the prototype: no OpenXR
// header, no SDL, nothing but the arithmetic, so the probe and verify.py can
// test it headless.
//
// THE ONE RULE IT EXISTS FOR: a head pose is composed with the AUTHORED camera
// in that camera's own frame, and never converted through the world's axes.
// OpenXR's view space is x right, y up, z BACK, in metres. The authored
// camera's basis (`cameraBasis`, `o3de/raster.h`) is s right, u up, f forward
// - and the map  (x, y, z) -> x s + y u - z f  is a proper ROTATION
// (det[s, u, -f] = +1, since u = s x f), so no rotation changes its sense on
// the way in. The reflection CLAUDE.md §5 warns of (the game's Y points down)
// only bites a conversion done through world axes; this one never does.
//
// Units: the game's world unit is the INCH, OpenXR's the metre.
#if OMK_VR

#include "o3de/raster.h"

namespace omk::vr {

inline constexpr float kInchesPerMetre = 39.37007874f;

// A pose in the headset's LOCAL space: where the head (or an eye) is relative
// to the origin the authored camera stands at. OpenXR's conventions.
struct Pose {
    float pos[3]{0.0f, 0.0f, 0.0f};          // metres: x right, y up, z back
    float quat[4]{0.0f, 0.0f, 0.0f, 1.0f};   // x, y, z, w - identity is no turn
};

// An eye's field of view, OpenXR's `XrFovf`: four angles in RADIANS, left and
// down NEGATIVE.
struct EyeFov {
    float left = 0.0f, right = 0.0f, up = 0.0f, down = 0.0f;
    bool  set() const { return right > left && up > down; }
};

// What a frontend reports each frame: the head, and each eye's pose and field
// of view (OpenXR's `xrLocateViews`). `valid` is false when there is no
// headset, or it has lost tracking.
struct HeadPose {
    bool   valid = false;
    Pose   head;
    Pose   eye[2];
    EyeFov fov[2];
    // the CONTROLLERS (step 3): their aim poses in the same local space as
    // the head - 0 left, 1 right - each with its own validity
    Pose   hand[2];
    bool   handValid[2]{false, false};
    // the size each eye's picture is drawn at, when the frontend has one (a
    // headset's swapchain, step 5b); 0 = the frame's halves, as the fake one
    int    eyeW = 0, eyeH = 0;
};

// How the authored camera's orientation is taken (§3: a flag).
enum class CameraOrientation {
    Level,   // only its position and HEADING: pitch and roll dropped, the horizon level
    Full,    // all of it - faithful, and the one that makes people sick
};

// The authored camera's basis as the headset sees it: the camera itself
// (Full), or the same eye and heading with pitch and roll removed (Level). A
// camera looking straight up or down has no heading and is kept as it is.
RCamera originCamera(const RCamera& authored, CameraOrientation o);

// THE COMPOSITION: `origin` is where the headset's origin stands (the result
// of `originCamera`), `local` an eye's (or the head's) pose relative to it,
// `fov` that eye's field of view or nullptr to keep the origin's. Returns the
// camera to draw that eye with. `w`/`h` are the eye's picture size.
//
// With an identity rotation the result keeps the origin's `at` and roll
// exactly - so a head that has not turned draws the authored frame to the
// bit, which is what the frame check asserts.
RCamera composeEye(const RCamera& origin, const Pose& local, const EyeFov* fov, int w, int h);

// ONE camera whose frustum contains both eyes', for the CPU culling (the
// frame culls once, the eyes draw twice). Symmetric, so it stands back from
// the eyes' midpoint far enough for the inter-eye distance, and wide enough
// for either eye at any roll of the head.
RCamera cullCamera(const RCamera& left, const RCamera& right, int w, int h);

// The world vector of a head-space vector (x right, y up, z back) under the
// camera `c`'s basis: x s + y u - z f.
void headToWorld(const RCamera& c, const float v[3], float out[3]);

// Rotate `v` by the unit quaternion `q` (x, y, z, w).
void rotate(const float q[4], const float v[3], float out[3]);

// A quaternion from yaw (about +y, positive turns LEFT as OpenXR's is
// counter-clockwise seen from above), pitch (about +x, positive looks UP) and
// roll (about +z, positive tilts the head's top to the LEFT), in degrees,
// applied yaw then pitch then roll - for the desktop's fake headset.
void quatFromYawPitchRoll(float yawDeg, float pitchDeg, float rollDeg, float q[4]);

}  // namespace omk::vr

#endif  // OMK_VR
