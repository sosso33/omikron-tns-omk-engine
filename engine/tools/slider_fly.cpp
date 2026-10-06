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

#include <algorithm>
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

    // THE SKID RECOVERY'S GATE (`if (v58 == 0)`, drift audit M6): a slider
    // skidding sideways at 30 - the nose on +Z, the velocity on X - pulled
    // back toward its nose (vx shrinking by a fifth) only when NO thrust key
    // drives; with UP held it keeps the slide and takes the hard arm
    {
        auto skid = [&](std::uint32_t in) {
            omk::SliderRide s;
            s.speed = 30.0; s.lastSpeed = 30.0; s.vx = 30.0; s.vz = 0.0; s.yaw = 0.0;
            s.fly(in, dt, nullptr);
            return s.vz;                     // the velocity turning onto the nose
        };
        std::printf("skid recover free_vz %.2f held_vz %.2f\n", skid(0u),
                    skid(omk::SliderRide::kThrustUp));
    }
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
    // ---- WHAT THE RIDE MEETS (drift audit M3) ---------------------------
    // A flat ROAD (`X...`) with one slider (r 82.3, the shipped sli_fn's)
    // parked 400 ahead across the lane, and UP held into it: `sub_458880`
    // must bounce the ride off the two ellipses - never through - set
    // `settle` to 40 and hand the parked one a speed (0 -> 7). Then a walker
    // on a crossing (`O...`) 200 ahead, walking across: `sub_458490` finds
    // him, `sub_459970` steps round him and the 0.25 damping runs.
    {
        omk::RideWorld w;
        w.surface = [](double, double y, double, double& drop, char nm[2]) {
            drop = 0.0 - y; nm[0] = 'X'; nm[1] = 0; return true;
        };
        w.radius = 82.3;
        w.vehicles.push_back({7, 0.0, 0.0, -400.0, 1.0, 0.0, 0.0, 82.3});
        double handed = -1.0;
        w.setSpeed = [&](int slot, double sp) { if (slot == 7) handed = sp; };
        omk::SliderRide s;
        const auto p = flat();
        double closest = 1e9; int settleSeen = 0;
        for (int k = 0; k < 400; ++k) {
            s.fly(omk::SliderRide::kThrustUp, dt, p);
            s.hover(dt, w);
            if (s.settle) { settleSeen = std::max(settleSeen, s.settle); --s.settle; }
            const double dx = s.x - 0.0, dz = s.z + 400.0;
            closest = std::min(closest, std::sqrt(dx * dx + dz * dz));
        }
        std::printf("collide hits %d closest %.1f settle %d handed %.1f final z %.1f\n",
                    s.vehicleHits, closest, settleSeen, handed, s.z);
        // off the road the push is refused: the same run on a pavement (`T`)
        omk::RideWorld w2 = w;
        w2.surface = [](double, double y, double, double& drop, char nm[2]) {
            drop = 0.0 - y; nm[0] = 'T'; nm[1] = 0; return true;
        };
        omk::SliderRide t;
        for (int k = 0; k < 400; ++k) { t.fly(omk::SliderRide::kThrustUp, dt, p); t.hover(dt, w2); }
        std::printf("collide offroad hits %d\n", t.vehicleHits);

        omk::RideWorld c;
        c.surface = [](double, double y, double, double& drop, char nm[2]) {
            drop = 0.0 - y; nm[0] = 'O'; nm[1] = 'P'; return true;
        };
        c.radius = 82.3;
        c.walkers.push_back({0.0, 0.0, -200.0, 1.0, 0.0});
        omk::SliderRide u;
        u.speed = 20.0; u.vz = 20.0;
        double sMin = 1e9;
        for (int k = 0; k < 30; ++k) { u.fly(omk::SliderRide::kThrustUp, dt, p); u.hover(dt, c); sMin = std::min(sMin, std::fabs(u.speed)); }
        std::printf("crossing steps %d x %.2f speed %.2f\n", u.walkerSteps, u.x, u.speed);
    }

    // ---- `sub_458C70`, THE ROAD EDGES (drift audit M3 step 2) -----------
    // A road strip |x| < 150 (`X`), pavement (`T`) beyond, the ride on its
    // centreline heading 30 degrees toward +x with UP held: the corners that
    // leave the road shove it back off the boundary and turn it along it.
    // The furthest any part of the hull's centre gets past the edge, the
    // final heading (0 / 180 is along the road), and how many pushes.
    {
        omk::RideWorld w;
        w.surface = [](double x, double y, double, double& drop, char nm[2]) {
            drop = 0.0 - y; nm[0] = std::fabs(x) < 150.0 ? 'X' : 'T'; nm[1] = 0; return true;
        };
        w.radius = 82.3;
        omk::SliderRide s;
        s.yaw = 330.0;                    // x -= sin(yaw) v: toward +x
        const auto p = flat();
        double maxX = -1e9;
        for (int k = 0; k < 600; ++k) {
            s.fly(omk::SliderRide::kThrustUp, dt, p);
            s.hover(dt, w);
            if (s.settle) --s.settle;
            maxX = std::max(maxX, s.x);
        }
        double yw = s.yaw > 180.0 ? s.yaw - 360.0 : s.yaw;
        std::printf("road edges %d max_x %.1f yaw %.1f z %.0f\n", s.edgeHits, maxX, yw, s.z);
    }

    // ---- `sub_459BD0`, THE WALL PASS (drift audit M3 step 3) -------------
    // A wall along z = -400 (all road, so only the walls act), the ride
    // heading 20 degrees off straight into it with UP held: once two hull
    // edges cross the wall it is drawn back toward the wall line and its
    // motion laid along it. The deepest the centre gets past the wall, the
    // slides, the single-edge hits (the engine's undefined case, left
    // alone), and how far it slid along x.
    {
        omk::RideWorld w;
        w.surface = [](double, double y, double, double& drop, char nm[2]) {
            drop = 0.0 - y; nm[0] = 'X'; nm[1] = 0; return true;
        };
        w.radius = 82.3;
        w.ray = [](double ax, double az, double bx, double bz, double y, double hit[3]) {
            const double wz = -400.0;
            if ((az - wz) * (bz - wz) > 0.0 || az == bz) return false;
            const double t = (wz - az) / (bz - az);
            hit[0] = ax + t * (bx - ax); hit[1] = y; hit[2] = wz;
            return true;
        };
        omk::SliderRide s;
        s.yaw = 340.0;                    // toward -z, a little toward +x
        const auto p = flat();
        double minZ = 1e9;
        for (int k = 0; k < 300; ++k) {
            s.fly(omk::SliderRide::kThrustUp, dt, p);
            s.hover(dt, w);
            if (s.settle) --s.settle;
            minZ = std::min(minZ, s.z);
        }
        std::printf("wall slides %d singles %d min_z %.1f x %.0f speed %.2f\n",
                    s.wallHits, s.wallSingles, minZ, s.x, s.speed);
    }
    return 0;
}
