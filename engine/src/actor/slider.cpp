// SPDX-License-Identifier: GPL-3.0-or-later
#include "actor/slider.h"

#include <cmath>

namespace omk {

namespace {
constexpr double kDeg = 0.0174532925199433;
double wrap360(double a) {
    while (a >= 360.0) a -= 360.0;
    while (a < 0.0)    a += 360.0;
    return a;
}
}  // namespace

// `sub_4573E0` (0x004573E0), 387 lines, transcribed arm by arm.
void SliderRide::fly(std::uint32_t input, double dt, const Probe& probe) {
    double slide = 0.0;                       // v0: the skid flag

    // ---- THE BRAKE ---------------------------------------------------
    //
    //     if ((input & 0x20) && fabs(speed) < 10) { speed = 0;
    //                                               sub_4570F0(); return; }
    //
    // `sub_4570F0` is what ENDS a ride, so this is the only way out of the
    // model that is not a dismount.
    if ((input & kStop) != 0 && std::fabs(speed) < 10.0) {
        speed = 0.0;
        stopped = true;
        return;
    }

    // ---- THE THRUST LADDER -------------------------------------------
    //
    // Six constants, and they are a ladder: 0.615, three times it (1.845)
    // and six times it (3.690). Which one is chosen by the SIGN OF THE
    // SPEED as well as the key, so "up" accelerates going forward and brakes
    // hard going backward.
    //
    //     if ((input & 0xC) && settle < 0x50) {
    //         if (speed < 0)  down -> -1.845, up -> +3.690, else +0.615
    //         else            up   -> +1.845, down -> -3.690, else -0.615
    //     } else thrust = 0;
    //
    // The `else` arm of each pair cannot be reached while `input & 0xC` is
    // non-zero; it is reproduced because the engine writes it.
    bool driving = false;
    if ((input & (kThrustUp | kThrustDown)) != 0 &&
        static_cast<unsigned>(settle) < 0x50u) {
        driving = true;
        if (speed < 0.0) {
            if      (input & kThrustDown) thrust = -1.845;
            else if (input & kThrustUp)   thrust =  3.690;
            else                          thrust =  0.615;
        } else {
            if      (input & kThrustUp)   thrust =  1.845;
            else if (input & kThrustDown) thrust = -3.690;
            else                          thrust = -0.615;
        }
    } else {
        thrust = 0.0;
    }
    if (noThrust) thrust = 0.0;

    // ---- THE TURN SIGN, which is not the steer ------------------------
    //
    //     if (!(input & 0xC))                             sign = 0
    //     else if ((input & 4) && (speed < 0 || sign))    sign = +1
    //     else if ((input & 8) && (speed > 0 || sign))    sign = -1
    //     else                                            sign = 0
    //
    // It latches: once non-zero it keeps itself alive while the key is held,
    // which is what lets a reversing slider be brought back through zero.
    int sign = 0;
    if ((input & (kThrustUp | kThrustDown)) != 0) {
        if ((input & kThrustUp) != 0 && (speed < 0.0 || turnSign))       sign = 1;
        else if ((input & kThrustDown) != 0 && (speed > 0.0 || turnSign)) sign = -1;
    }
    turnSign = sign;

    // ---- THE STEER AND THE BANK ---------------------------------------
    //
    // `0x1` and `0x2` steer at +-5 degrees a frame, and the bank ramps at
    // 1.75 a frame toward +-11 (349 being -11 in the wrapped angle). The
    // steer is NEGATED when the slider is reversing, so the stick still
    // turns the nose the way it points.
    double steer = 0.0;
    if ((input & (kSteerLeft | kSteerRight)) != 0)
        steer = (input & kSteerLeft) ? -kSteerRate : kSteerRate;

    if (steer != 0.0) {
        if (speed != 0.0) {
            if (steer < 0.0) {
                if (roll < 180.0) roll -= dt * kBankRate * 2.0;
                roll -= dt * kBankRate;
                if (roll < 0.0) roll += 360.0;
                if (roll < 349.0 && roll > 180.0) roll = 349.0;
            } else {
                if (roll > 180.0) roll += dt * kBankRate * 2.0;
                roll += dt * kBankRate;
                if (roll > 360.0) roll -= 360.0;
                if (roll > kBankLimit && roll < 180.0) roll = kBankLimit;
            }
        } else {
            // standing still: the bank unwinds toward 0 (or 360) first
            if (roll <= 180.0) { if (roll > 0.0) { roll -= dt * kBankRate;
                                                   if (roll < 0.0) roll = 0.0; } }
            else               { roll += dt * kBankRate;
                                 if (roll > 360.0) roll = 0.0; }
        }
        if (speed < 0.0) steer = -steer;
        yaw = wrap360(yaw + steer * dt);
    } else if (roll != 0.0) {
        // no steer: the bank unwinds at the same rate, toward whichever of
        // 0 or 360 it is nearer
        roll += (roll > 180.0) ? dt * kBankRate : -dt * kBankRate;
        if (roll < 0.0 || roll >= 360.0) roll = 0.0;
    }

    // `dword_8F5DD0 = steer * speed / 42` - the term the integration below
    // tests against 0.769 to decide whether the model is in its simple arm.
    {
        double s = steer;
        if (s > 180.0) s -= 360.0;
        steerTerm = s * speed * 0.023809524;
    }

    // ---- THE SKID TEST -------------------------------------------------
    //
    // The angle between where the nose points and where the velocity goes.
    // Inside 37 degrees the slider grips; past that and inside 170 it is
    // SLIDING, and the arm below snaps the velocity back toward the nose.
    if (speed != 0.0) {
        double heading;
        if (vz == 0.0) heading = (vx <= 0.0) ? -1.5707964 : 1.5707964;
        else {
            heading = std::atan2(vx / vz, 1.0);
            if (vz < 0.0) heading += (heading < 0.0) ? 3.1415927 : -3.1415927;
        }
        yaw = wrap360(yaw);
        double d = std::fabs(yaw * kDeg - heading);
        if (d > 3.1415927) d = 6.2831855 - d;
        slide = (d <= 0.6457718) ? 0.0 : (d < 2.9670596 ? 1.0 : 0.0);
    }

    // ---- THE SKID RECOVERY --------------------------------------------
    if (slide != 0.0 && std::fabs(speed) > 16.0 && speed > 0.0) {
        const double r = yaw * kDeg;
        vx += (std::sin(r) * lastSpeed - vx) * 0.2;
        vz += (std::cos(r) * lastSpeed - vz) * 0.2;
        speed = std::sqrt(vx * vx + vz * vz);
        if (speed != 0.0) { vx = lastSpeed * vx / speed;
                            vz = lastSpeed * vz / speed; }
        speed = std::sqrt(vx * vx + vz * vz);
    } else if (std::fabs(steerTerm) <= 0.76899999 && slide == 0.0 && !noThrust) {
        // ---- THE SIMPLE ARM: one speed along the nose ------------------
        //
        // Quadratic drag off the thrust, the thrust onto the speed, and a
        // dead band that snaps a coasting slider to a stop.
        lastSpeed = speed;
        const double drag = speed * speed * 0.1538 * 0.0078125;
        thrust -= (speed < 0.0) ? -drag : drag;
        speed += thrust;
        if (lastSpeed * speed <= 8.0 &&
            (input & (kThrustUp | kThrustDown)) == 0 && !settle) speed = 0.0;
        if (turnSign && lastSpeed * speed <= 0.0 && !settle) speed = 0.0;
        const double r = yaw * kDeg;
        vx = std::sin(r) * speed;
        vz = std::cos(r) * speed;
    } else {
        // ---- THE HARD ARM: the two components drift apart --------------
        //
        // Each component takes the thrust along the nose and its own
        // quadratic drag, and the YAW is then pulled by the cross product of
        // the velocity with the nose - which is what makes a hard turn slew.
        const double r = yaw * kDeg;
        lastSpeed = speed;
        const double sn = std::sin(r), cs = std::cos(r);
        const double ax = thrust * sn, az = thrust * cs;
        const double dx = vx * vx * 0.30759999 * 0.00390625;
        const double dz = vz * vz * 0.30759999 * 0.00390625;
        vx += ax - ((vx < 0.0) ? -dx : dx);
        vz += az - ((vz < 0.0) ? -dz : dz);
        const double cross = vz * sn - vx * cs;
        const double t = speed * thrust;
        if (t > 0.0)      yaw -= cross * -0.0625;
        else if (t < 0.0) yaw -= steer * dt * 2.0;
        double sp = std::sqrt(vx * vx + vz * vz);
        if (speed < 0.0) sp = -sp;
        speed = sp;
    }

    if (roll < 0.0 || roll >= 360.0) roll = 0.0;

    // THE POSITION, and the sign is the engine's: `x -= vx * dt`.
    x -= vx * dt;
    z -= vz * dt;
    yaw = wrap360(yaw);

    // ---- THE PITCH, from two probes a METRE fore and aft ---------------
    //
    // 39.370079 units is exactly 1.00 m, so the span is 2 m, and 0.0127 is
    // 1/78.74 - the reciprocal of that span. The engine takes the sine of
    // the height difference over it, which is the slope of the ground the
    // slider is following.
    if (probe) {
        const double r = yaw;               // NOTE: the engine takes sin/cos
        const double sn = std::sin(r);      // of the DEGREES here, without
        const double cs = std::cos(r);      // converting - reproduced as read
        double hf = 0.0, hb = 0.0; bool bf = false, bb = false;
        const bool okF = probe(x + sn * kProbeSpan, y - kProbeSpan,
                               z + cs * kProbeSpan, hf, bf);
        const bool okB = probe(x - sn * kProbeSpan, y - kProbeSpan,
                               z - cs * kProbeSpan, hb, bb);
        if (okF && okB) pitch = std::asin((hf - hb) * 0.0127);
    }
}

// `sub_458600` (0x00458600) - the hover, the bob, and the "OP" brake.
//
// WHICH OF ITS TWO ARMS RUNS is a comparison the decompilation lost (`v7`,
// flagged "possibly undefined" at 458775). The listing has it:
//
//     fld   dword_8F5DBC        ; the SPEED
//     fcomp flt_4BC5E0          ; ...against 0.0
//     test  ah, 40h             ; C3 - equal?
//     jz    the EASE arm
//
// so **a stationary slider BOBS and a moving one EASES** toward its hover
// height at a third of the gap a frame. Parked it idles up and down; under
// way it simply follows the ground.
void SliderRide::hover(double dt, const Probe& probe) {
    if (noThrust) --noThrust;
    if (!probe) return;

    double drop = 0.0; bool braking = false;
    if (probe(x, y, z, drop, braking)) {
        // A surface whose mesh name begins "OP" damps the ride by a quarter
        // a frame and pushes it back along its own velocity - the engine's
        // `strncmp(aOp, name, 1)` arm.
        if (braking) {
            const double px = vx * dt;
            vx -= vx * 0.25;
            const double pz = vz * dt;
            vz -= vz * 0.25;
            speed -= speed * 0.25;
            x += px - vx * dt;
            z += pz - vz * dt;
        }
        const double gap = drop - kHover;
        if (speed == 0.0) {
            bobPhase += dt * kBobRate;
            if (bobPhase >= 360.0) bobPhase -= 360.0;
            y += gap - std::sin(bobPhase * kDeg) * kBobAmp;
        } else {
            y += gap * dt * 0.33333334;
        }
    } else {
        // no surface at all: look 39 units lower, and fall to it
        double d2 = 0.0; bool b2 = false;
        if (probe(x, y - 39.0, z, d2, b2)) y -= 39.0;
    }
}

}  // namespace omk
