// SPDX-License-Identifier: GPL-3.0-or-later
// THE CAMERA'S OBSTRUCTION PASS - `sub_417070` (04_sys.c 3370), the arm
// camera flag 8 selects (todo/camera-obstruction.md 5 has it line by line).
//
// Two cameras carry flag 8, and `sub_414520` is what arms it:
//
//   `sub_413C00`  mode 0 (and 12/20 with eye subject 0) - the follow camera:
//                 +312 = +316 = -0.7 x the subject's pelvis height
//   `sub_4141F0`  mode 8 (eye subject 5, the SLIDER, target subject not 9):
//                 +312 = -78.74 (2 m), +316 = -39.37 (1 m). Mode 10 takes the
//                 same function's other arm, which CLEARS flag 4; mode 9 none
//
// and both load +300 = 1.2, +320 = 8, +324 = 4. One transcription serves
// both: this was `PlayerController::cameraCollide` until 2026-10-07, moved
// here unchanged so the slider's mode 8 can run it (drift audit B12).
#pragma once

#include <functional>

namespace omk {

// The camera block's fields this pass keeps between frames.
struct CamObstruct {
    float dist = 0.0f;           // +328, the distance it keeps
    int   block = 0;             // +208: 0 clear, 1 blocked, 2 recovering
    float prevEyeY = 0.0f;       // +24, LAST FRAME'S FINAL eye y  } written back by
    float prevAtY  = 0.0f;       // +36, ...and target y           } the CALLER at the
    bool  prevValid = false;     //                                } end of its tick
    float subjY = 0.0f;          // +340 = +156, the eye subject's y this frame
    float prevSubjY = 0.0f;      // +344, last frame's
};

// `cast(from, ray)`: the nearest hit along `from + t * ray` against the
// camera's solids, as the fraction t (anything > 1 for none).
// `unlagged(eye, at)`: the second ray's ends - the camera resolved on the
// subject with NO lag in it (`+100..+120` / `+152..+172`); false skips it.
// `lift312` / `lift316` are the two height pushes WITHOUT the subject's y
// (the pass adds +156). `justChanged` is flag 1: both easings snap.
void obstructCamera(CamObstruct& st, float eye[3], float at[3],
                    double lift312, double lift316, float dt, bool justChanged,
                    const std::function<double(const double from[3], const double ray[3])>& cast,
                    const std::function<bool(float eye[3], float at[3])>& unlagged);

}  // namespace omk
