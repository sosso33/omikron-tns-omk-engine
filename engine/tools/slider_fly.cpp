// SPDX-License-Identifier: GPL-3.0-or-later
// THE SLIDER'S FLIGHT MODEL, RUN - `sub_4573E0` and `sub_458600`.
//
//     slider_fly
//
// No game data: the model is the engine's arithmetic and nothing else, so
// this drives it over a FLAT floor and reports the numbers a reader can hold
// against the listing. What it exercises is the shape of the model, which is
// the part a transcription can get wrong silently: the thrust ladder's six
// values, the bank's rate and its 11-degree limit, the steer, the dead band
// that stops a coasting slider, and the two hover arms - the bob when it is
// parked and the ease when it is under way.
//
// The frame delta is 1.0, the engine's own unit (`Game_Frame` sets
// `flt_4C30D8 = 30.0 / fps`), HALVED to 0.5 the way `Slider_TickRide` halves
// it for all three helpers.
//
// One line per fact:
//   thrust ...      the six-value ladder, by input and by the sign of speed
//   accel ...       twenty frames of UP: the speed and the distance
//   bank ...        twenty frames of steering: the roll, and where it clamps
//   steer ...       the yaw after those frames, and after the mirror
//   coast ...       how many frames a released slider takes to stop
//   hover ...       the height a parked slider settles to, and its bob
//   moving ...      ...and that a moving one does NOT bob
#include "actor/slider.h"

#include <cmath>
#include <cstdio>

namespace {

// A flat floor at y = 0: the DROP from the probe point is just its height.
omk::SliderRide::Probe flat(double floorY = 0.0, bool braking = false) {
    return [floorY, braking](double, double y, double, double& drop, bool& br) {
        drop = floorY - y;
        br = braking;
        return true;
    };
}

double runThrust(std::uint32_t input, double speed) {
    omk::SliderRide s;
    s.speed = speed;
    s.fly(input, 0.0, nullptr);        // dt 0: nothing integrates, the ladder shows
    return s.thrust;
}

}  // namespace

int main() {
    const double dt = 0.5;                     // one frame, halved

    std::printf("thrust up %.3f down %.3f rev_up %.3f rev_down %.3f none %.3f\n",
                runThrust(omk::SliderRide::kThrustUp, 10.0),
                runThrust(omk::SliderRide::kThrustDown, 10.0),
                runThrust(omk::SliderRide::kThrustUp, -10.0),
                runThrust(omk::SliderRide::kThrustDown, -10.0),
                runThrust(0, 10.0));

    {   // twenty frames of UP over a flat floor
        omk::SliderRide s;
        const auto p = flat();
        for (int k = 0; k < 20; ++k) {
            s.fly(omk::SliderRide::kThrustUp, dt, p);
            s.hover(dt, p);
        }
        std::printf("accel speed %.2f z %.1f y %.2f\n", s.speed, s.z, s.y);
    }

    {   // twenty frames of steering right, at speed
        omk::SliderRide s;
        s.speed = 40.0; s.vz = 40.0;
        const auto p = flat();
        for (int k = 0; k < 20; ++k) {
            s.fly(omk::SliderRide::kSteerRight, dt, p);
            s.hover(dt, p);
        }
        std::printf("bank roll %.2f limit %.2f yaw %.2f\n",
                    s.roll, omk::SliderRide::kBankLimit, s.yaw);
        omk::SliderRide t;
        t.speed = 40.0; t.vz = 40.0;
        for (int k = 0; k < 20; ++k) {
            t.fly(omk::SliderRide::kSteerLeft, dt, p);
            t.hover(dt, p);
        }
        std::printf("steer right %.2f left %.2f\n", s.yaw, t.yaw);
    }

    {   // released at speed: the dead band stops it
        omk::SliderRide s;
        s.speed = 40.0; s.vz = 40.0;
        const auto p = flat();
        int frames = 0;
        for (; frames < 400 && s.speed != 0.0; ++frames) {
            s.fly(0, dt, p);
            s.hover(dt, p);
        }
        std::printf("coast frames %d z %.1f\n", frames, s.z);
    }

    {   // PARKED over a floor 200 below: it settles to the hover height and bobs
        omk::SliderRide s;
        s.y = 200.0;
        const auto p = flat();
        double lo = 1e9, hi = -1e9;
        for (int k = 0; k < 200; ++k) { s.fly(0, dt, p); s.hover(dt, p); }
        for (int k = 0; k < 200; ++k) {
            s.fly(0, dt, p); s.hover(dt, p);
            if (s.y < lo) lo = s.y;
            if (s.y > hi) hi = s.y;
        }
        std::printf("hover y %.2f bob %.2f amp %.3f\n",
                    (lo + hi) / 2.0, hi - lo, omk::SliderRide::kBobAmp);
    }

    {   // MOVING: the ease arm, and no bob at all
        omk::SliderRide s;
        s.y = 200.0; s.speed = 40.0; s.vz = 40.0;
        const auto p = flat();
        for (int k = 0; k < 200; ++k) { s.fly(omk::SliderRide::kThrustUp, dt, p);
                                        s.hover(dt, p); }
        const double a = s.y;
        for (int k = 0; k < 20; ++k) { s.fly(omk::SliderRide::kThrustUp, dt, p);
                                       s.hover(dt, p); }
        std::printf("moving y %.2f drift %.4f phase %.2f\n",
                    s.y, std::fabs(s.y - a), s.bobPhase);
    }
    return 0;
}
