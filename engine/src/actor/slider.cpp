// SPDX-License-Identifier: GPL-3.0-or-later
#include "actor/slider.h"

#include <algorithm>
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
    //
    // ONLY WITH NO THRUST KEY DRIVING (`if (v58 == 0)`, drift audit M6) -
    // holding thrust through a hard turn keeps the slide and takes the hard
    // arm - and inside it two things this skipped: under |speed| 16 the
    // skid is simply CANCELLED (`v0 = 0`, the steer term zeroed) and the
    // simple arm runs; over 16 the steer term is zeroed as the velocity is
    // pulled back toward the nose:
    //
    //     if (!v58) {
    //         if (|speed| < 16 && v0 && speed > 0) { v0 = 0; DD0 = 0; }
    //         if (v0 && |speed| > 16 && speed > 0) { DD0 = 0; ...0.2...;
    //                                                 goto LABEL_135; }
    //     }
    bool recover = false;
    if (!driving) {
        if (std::fabs(speed) < 16.0 && slide != 0.0 && speed > 0.0) { slide = 0.0; steerTerm = 0.0; }
        if (slide != 0.0 && std::fabs(speed) > 16.0 && speed > 0.0) { steerTerm = 0.0; recover = true; }
    }
    if (recover) {
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
// `sub_458880` (0x00458880), transcribed. Its overlap test is `sub_4583D0`:
// the box `max(|dx|, |dz|) <= 1.5 r` and then `sqrt(dx^2 + 2 dz^2) < 1.5 r`
// (the -2.0 is `flt_4BC634`, read from the image), r the OTHER vehicle's
// model radius; a distance of exactly 0 returns 0.0, which is "no".
bool SliderRide::collideVehicles(const RideWorld& w) {
    if (noThrust) { pushX = pushZ = 0.0; return false; }
    const RideVehicle* hit = nullptr;
    for (const auto& v : w.vehicles) {
        const double dx = x - v.x, dz = z - v.z;
        const double R = v.radius * 1.5;
        if (R < std::max(std::fabs(dx), std::fabs(dz))) continue;
        const double d = std::sqrt(dx * dx - dz * dz * -2.0);
        if (R <= d || d == 0.0) continue;
        hit = &v;
        break;
    }
    if (!hit) return false;
    double vs = hit->speed;                       // `sub_4382A0`: +52 / 256
    if (vs == 0.0) vs = 7.0;
    const double A = hit->radius * 1.5;
    const double A2 = A * A, B2 = (A * 0.5) * (A * 0.5);
    const double rvx = -(hit->dirx * vs) - vx;    // the closing velocity
    const double rvz = -(hit->dirz * vs) - vz;
    double ox = x - hit->x, oz = z - hit->z;
    double d = std::sqrt(ox * ox + oz * oz);
    if (d == 0.0) return false;
    double c = (hit->dirx * ox + hit->dirz * oz) / d;
    double e = c * c * (A2 - B2) + B2;            // its ellipse along the offset
    c = (std::cos(yaw * kDeg) * -oz + std::sin(yaw * kDeg) * -ox) / d;
    e += c * c * (A2 - B2) + B2;                  // ...and the ride's, same axes
    if (e < d * d) return false;
    const double rv = std::sqrt(rvx * rvx + rvz * rvz);
    const double s = std::sqrt(e);
    pushX = rv * ox / d * -0.25;
    pushZ = rv * oz / d * -0.25;
    const double cx = hit->x + s * ox / d, cz = hit->z + s * oz / d;
    double drop = 0.0; char nm[2] = {0, 0};
    if (!w.surface || !w.surface(cx, y, cz, drop, nm)) return false;
    // `strncmp("X", name, 1)` or `strncmp("OP", name, 2)`: only onto ROAD
    if (!(nm[0] == 'X' || (nm[0] == 'O' && nm[1] == 'P'))) return false;
    x = cx; z = cz;
    ox = x - hit->x; oz = z - hit->z;
    d = std::sqrt(ox * ox + oz * oz);
    vx -= rv * ox / d * 0.66666669;
    vz -= rv * oz / d * 0.66666669;
    double sp = std::sqrt(vx * vx + vz * vz);
    if (std::sin(yaw * kDeg) * vx + std::cos(yaw * kDeg) * vz < 0.0) sp = -sp;
    speed = sp;
    settle = 40;
    if (w.setSpeed) w.setSpeed(hit->slot, vs);
    ++vehicleHits;
    return true;
}

namespace {
bool isRoad(const char nm[2]) { return nm[0] == 'X' || (nm[0] == 'O' && nm[1] == 'P'); }

// `sub_459810(a, b, out, 2)`: from the middle of a..b, four halving steps
// along it, each probing 39.370079 under the ride's height - back toward `a`
// (the OFF-road corner) while the probe is on road, forward while it is not
// - so `out` lands on the boundary.
void edgeCrossing(const SliderRide& r, const RideWorld& w, const double a[2],
                  const double b[2], double out[2]) {
    double hx = (b[0] - a[0]) * 0.5, hz = (b[1] - a[1]) * 0.5;
    double mx = a[0] + hx, mz = a[1] + hz;
    for (int k = 0; k < 4; ++k) {
        double drop = 0.0; char nm[2] = {0, 0};
        const bool road = w.surface && w.surface(mx, r.y - 39.370079, mz, drop, nm) && isRoad(nm);
        hx = road ? -(hx * 0.5) : hx * 0.5;
        hz = road ? -(hz * 0.5) : hz * 0.5;
        mx += hx; mz += hz;
    }
    out[0] = mx; out[1] = mz;
}
}  // namespace

// `sub_458C70` (0x00458C70), 466 lines, transcribed - its edge selection
// branch for branch, the two flags the decompiler lost read from the image
// (0x4594D0: the shove is x-5 under |speed| 30 and x-10 above; 0x459580:
// the boundary's angle is atan(e.x / e.z), or 90 / 270 when e.z is 0).
void SliderRide::roadEdges(const RideWorld& w) {
    if (!w.surface) return;
    const double as = std::fabs(speed);
    double lx = 0.0, lz = 0.0;                         // v69 / v77, the lead
    if (as > 32.0) { lx = vx * -2.0; lz = vz * -2.0; }
    else if (as > 18.0) { lx = -vx; lz = -vz; }
    const double r = yaw * kDeg;
    const double c = std::cos(r), s = std::sin(r);
    const double v50 = s * 60.369999, v51 = c * 60.369999, v49 = s * -60.369999;
    const double v52 = v50 * -2.0, v68x = v51 * -2.0;
    const double A[2] = {v51 + v50 + x, v49 + v51 + z};
    const double B[2] = {A[0] + v52, A[1] + v68x};
    const double C[2] = {v50 - v51 + x, v51 - v49 + z};
    const double D[2] = {C[0] + v52, C[1] + v68x};
    // each corner: no surface at all -> `sub_459BD0` and out (LABEL_121);
    // otherwise 1 when it is OFF the road
    auto off = [&](const double p[2], int& o) {
        double drop = 0.0; char nm[2] = {0, 0};
        if (!w.surface(p[0] + lx, y - 20.0, p[1] + lz, drop, nm)) return false;
        o = isRoad(nm) ? 0 : 1;
        return true;
    };
    int oA = 0, oB = 0, oC = 0, oD = 0;               // v56, v57, v7, v6
    if (!off(A, oA) || !off(B, oB)) { wallPass(w); return; }   // LABEL_121
    double P[2][2] = {{0, 0}, {0, 0}};                // v64/v65 and v66[0..1]
    int npt = 0;                                      // v1
    bool abCrossed = false;                           // v59
    if (oA != oB) {
        if (oA) edgeCrossing(*this, w, A, B, P[0]); else edgeCrossing(*this, w, B, A, P[0]);
        npt = 1; abCrossed = true;
    }
    if (!off(C, oC) || !off(D, oD)) { wallPass(w); return; }
    if (!(oA | oB | oC | oD)) {                       // all four on the road
        edgeX -= edgeX * 0.125;
        edgeZ -= edgeZ * 0.125;
        yawRate -= yawRate * 0.0625;
        yaw += yawRate;
        x += edgeX;
        z += edgeZ;
        return;
    }
    const bool allOff = (oB & oC & oD & oA) != 0;
    int centreOff = 0;
    if (allOff) {
        x -= vx * -2.0;                                // back along the motion
        z -= vz * -2.0;
        const double q = std::fabs(speed) * 0.25;
        edgeX = edgeNX * q;
        edgeZ = edgeNZ * q;
        speed *= 0.25;
    } else {
        double drop = 0.0; char nm[2] = {0, 0};
        centreOff = (!w.surface(x + lx, y - 20.0, z + lz, drop, nm) || !isRoad(nm)) ? 1 : 0;
    }
    noThrust = 8;                                     // dword_8F5DF0
    ++edgeHits;
    if (allOff) { vx = edgeX; vz = edgeZ; return; }
    bool cdCrossed = false;                           // v10
    if (oC != oD) {
        double* pt = P[npt < 2 ? npt : 1];
        if (oC) edgeCrossing(*this, w, C, D, pt); else edgeCrossing(*this, w, D, C, pt);
        ++npt; cdCrossed = true;
    }
    // ...the END edges (A-C, B-D) for the point(s) still owed: the
    // decompiled selection, kept as it branches
    auto at = [&](int k) -> double* { return P[k < 2 ? k : 1]; };
    const int k = npt;
    if (!(oB && oA)) {
        if (oD && oC) {
            if (oA) edgeCrossing(*this, w, D, B, at(k));
            else if (oB) edgeCrossing(*this, w, C, A, at(k));
            else { edgeCrossing(*this, w, C, A, at(k)); edgeCrossing(*this, w, D, B, at(k + 1)); }
        } else if (!abCrossed) {
            if (oC) edgeCrossing(*this, w, C, A, at(k));
            else if (oD) edgeCrossing(*this, w, D, B, at(k));
        } else if (!cdCrossed) {
            if (oA) edgeCrossing(*this, w, A, C, at(k));
            else if (oB) edgeCrossing(*this, w, B, D, at(k));
        }
    } else {                                          // A and B both off
        if (oC) edgeCrossing(*this, w, B, D, at(k));
        else if (oD) edgeCrossing(*this, w, A, C, at(k));
        else { edgeCrossing(*this, w, A, C, at(k)); edgeCrossing(*this, w, B, D, at(k + 1)); }
    }
    // LABEL_65: the boundary through the two points, its normal toward the ride
    const double ex = P[1][0] - P[0][0], ez = P[1][1] - P[0][1];
    const double len = std::sqrt(ex * ex + ez * ez);
    if (!(len > 0.0)) return;
    double nx = ez / len, nz = -ex / len;
    if ((x - P[0][0]) * nx + (z - P[0][1]) * nz < 0.0) { nx = -nx; nz = -nz; }
    const double ux = ex / len, uz = ez / len;
    double base;
    if (centreOff == 0) {
        vx -= nx; vz -= nz;                            // the motion turned off the edge
        base = edgeX;
    } else {
        x += vx; z += vz;                              // a step back
        const double k5 = std::fabs(speed) < 30.0 ? -5.0 : -10.0;
        vx = nx * k5; vz = nz * k5;
        base = 0.0; edgeZ = 0.0;
    }
    edgeNX = nx; edgeNZ = nz;
    edgeX = nx * 2.5 + base;
    edgeZ = nz * 2.5 + edgeZ;
    x += edgeX;
    z += edgeZ;
    // ...and turned along it, by a rate clamped to +-2.5
    double want = (uz == 0.0) ? (ux < 0.0 ? 270.0 : 90.0)
                              : std::atan2(ux / uz, 1.0) * 57.29577951308232;
    double cur = yaw > 180.0 ? yaw - 360.0 : yaw;
    if (std::fabs(cur - want) > 90.0) want = want <= 0.0 ? want + 180.0 : want - 180.0;
    const double dyaw = want - cur;
    const bool backSide  = (oB && !oA) || (oD && !oC);    // `v57 && !v56 || v6 && !v7`
    const bool frontSide = (oA && !oB) || (oC && !oD);    // `v56 && !v57 || v7 && !v6`
    auto clamp = [&] { if (yawRate < -2.5) yawRate = -2.5; else if (yawRate > 2.5) yawRate = 2.5; };
    if (std::fabs(dyaw) >= 180.0) {
        if (speed < 0.0) { if (!backSide) clamp(); }
        else if (!frontSide) clamp();
    } else if (speed < 0.0) {
        if (!backSide) clamp();
    } else if (!frontSide) {
        yawRate += dyaw * 0.020833334 * speed * 0.028571429;
        clamp();
    }
    yaw += yawRate;
    if (yaw > 360.0) yaw -= 360.0;
    if (yaw < 0.0) yaw += 360.0;
    settle = 0;                                       // dword_8F5E08
}

// `sub_459BD0` (0x00459BD0), 312 lines, transcribed. The corners are the
// node matrix's own rows - `sub_442160(0, -yaw, -(360 - roll))`, so its Z is
// (sin yaw, cos yaw) and its X (cos roll cos yaw, -sin yaw cos roll), both
// x60. The flags the decompiler lost (0x45A365) are the yaw wrap.
//
// ONE EDGE ALONE IS UNDEFINED IN THE ORIGINAL: that path (`LABEL_38`,
// loc_45A042) loads `esi` - the pair code 1..4 - from `[ebp-4Ch]`, a slot
// nothing in the function has written yet, and leaves the second hit point's
// x (`[ebp-28h]`) unwritten too, so the wall direction it then uses is stack
// left over from an earlier call. This port gives that case NO response
// (counted in `wallSingles`) - with a code outside 1..4 only a corner over no
// ground could have moved it, and in a direction nobody can reproduce.
void SliderRide::wallPass(const RideWorld& w) {
    if (!w.ray || !w.surface) return;
    const double yr = yaw * kDeg, rr = roll * kDeg;
    const double Zx = std::sin(yr) * 60.0, Zz = std::cos(yr) * 60.0;
    const double Xx = std::cos(rr) * std::cos(yr) * 60.0, Xz = -std::sin(yr) * std::cos(rr) * 60.0;
    const double P[4][2] = {{x - Xx - Zx, z - Xz - Zz}, {x + Xx - Zx, z + Xz - Zz},
                            {x + Xx + Zx, z + Xz + Zz}, {x - Xx + Zx, z - Xz + Zz}};
    bool e[4] = {false, false, false, false};
    double h[4][3] = {};
    for (int i = 0; i < 4; ++i) {                 // P1->P2, P2->P3, P3->P4, P4->P1
        const int j = (i + 1) & 3;
        if (w.ray(P[i][0], P[i][1], P[j][0], P[j][1], y, h[i])) e[i] = true;
    }
    for (int i = 0; i < 4; ++i) {                 // ...and back, the later hit kept
        const int j = (i + 1) & 3;
        double t[3];
        if (w.ray(P[j][0], P[j][1], P[i][0], P[i][1], y, t)) {
            e[i] = true; h[i][0] = t[0]; h[i][1] = t[1]; h[i][2] = t[2];
        }
    }
    auto farFromP1 = [&](const double q[3]) {
        const double dx = q[0] - P[0][0], dz = q[2] - P[0][1];
        return std::sqrt(dx * dx + dz * dz) > 60.0;
    };
    int code = 0;                                 // esi, v6
    const double* Ha = nullptr; const double* Hb = nullptr;
    bool single = false;
    if (e[1]) {
        Ha = h[1];
        if (e[0])      { Hb = h[0]; code = 2; }
        else if (e[2]) { Hb = h[2]; code = 3; }
        else if (e[3]) { Hb = h[3]; code = farFromP1(h[3]) ? 3 : 2; }
        else single = true;
    } else if (!e[2]) {
        if (e[3]) { Ha = h[3]; if (e[0]) { Hb = h[0]; code = 1; } else single = true; }
        else { if (!e[0]) return; Ha = h[0]; single = true; }
    } else {
        Ha = h[2];
        if (e[3])      { Hb = h[3]; code = 4; }
        else if (e[0]) { Hb = h[0]; code = farFromP1(h[0]) ? 2 : 1; }
        else single = true;
    }
    if (single) { ++wallSingles; return; }
    // the corners' ground, 39.370079 under the ride (any surface)
    bool g[4];
    for (int i = 0; i < 4; ++i) {
        double drop = 0.0; char nm[2] = {0, 0};
        g[i] = w.surface(P[i][0], y - 39.370079, P[i][1], drop, nm);
    }
    int q = -1;
    for (int i = 0; i < 4 && q < 0; ++i)
        if (!g[i] || code == i + 1) q = i;
    if (q < 0) return;
    const double rx = P[q][0] - Ha[0], rz = P[q][1] - Ha[2];
    if (speed == 0.0) speed = 0.0099999998;       // `dword_8F5DBC = 0.01f`
    const double mx = -vx / std::fabs(speed), mz = -vz / std::fabs(speed);
    double ux = Hb[0] - Ha[0], uz = Hb[2] - Ha[2];
    double L = std::sqrt(ux * ux + uz * uz);
    if (L == 0.0) L = 0.0099999998;
    ux /= L; uz /= L;
    const double t = uz * rz + ux * rx;
    x -= (ux * t + Ha[0] - P[q][0]) * -0.5;
    z -= (uz * t + Ha[2] - P[q][1]) * -0.5;
    const double pm = uz * mz + ux * mx;
    const double px = ux * pm, pz = uz * pm;
    if ((g[0] || !g[1]) && (g[2] || !g[3])) {
        if ((g[0] && !g[1]) || (g[2] && !g[3])) { yawRate = 2.0; yaw += 2.0; }
    } else {
        yawRate = -2.0; yaw -= 2.0;
    }
    if (yaw > 360.0) yaw -= 360.0;
    else if (yaw < 0.0) yaw += 360.0;
    const double as = std::fabs(speed);
    vx = -(px * as) * 95.0 * 0.0099999998;
    vz = -(pz * as) * 95.0 * 0.0099999998;
    ++wallHits;
}

namespace {
// `sub_458490`: the first walker (record order) within 300 of the ride and
// AHEAD of its motion (`x -= v`, so ahead is `-cos > 0.5`) whose own surface
// is a crossing - a mesh name starting `O`. -1 for none.
int crossingWalker(const SliderRide& r, const RideWorld& w) {
    for (std::size_t i = 0; i < w.walkers.size(); ++i) {
        const RideWalker& p = w.walkers[i];
        const double ox = p.x - r.x, oy = p.y - r.y, oz = p.z - r.z;
        const double d = std::sqrt(ox * ox + oy * oy + oz * oz);
        if (!(d < 300.0)) continue;
        double c = oz * r.vz + ox * r.vx;
        if (d != 0.0) c /= d;
        if (r.speed != 0.0) c /= std::fabs(r.speed);
        if (!(-c > 0.5)) continue;
        double drop = 0.0; char nm[2] = {0, 0};
        if (w.surface && w.surface(p.x, p.y, p.z, drop, nm) && nm[0] == 'O')
            return static_cast<int>(i);
    }
    return -1;
}
}  // namespace

void SliderRide::hover(double dt, const RideWorld& w) {
    collideVehicles(w);                           // `sub_458880`
    if (noThrust) --noThrust;
    if (!w.surface) return;
    double drop = 0.0; char nm[2] = {0, 0};
    const bool have = w.surface(x, y, z, drop, nm);
    roadEdges(w);                                 // `sub_458C70`
    if (have) {
        // the ride on a crossing (`strncmp("OP", name, 1)` - the byte `O`),
        // and a walker ahead on one too
        if (nm[0] == 'O') {
            const int wi = crossingWalker(*this, w);
            if (wi >= 0) {
                // `sub_459970`: if the walker's path at the ride's distance
                // falls inside the ride's +-2r band, step a fifteenth of 2r
                // to the side, across the offset
                const RideWalker& p = w.walkers[static_cast<std::size_t>(wi)];
                const double r2 = w.radius + w.radius;
                const double ox = x - p.x, oz = z - p.z;
                const double d = std::sqrt(ox * ox + oz * oz);
                if (d != 0.0) {
                    const double px = oz / d * r2, pz = -ox / d * r2;
                    const double wx = p.dirx * d, wz = p.dirz * d;
                    if ((ox - px - wx) * (px + ox - wx) + (oz - pz - wz) * (pz + oz - wz) <= 0.0) {
                        x -= px * -0.06666667;
                        z -= pz * -0.06666667;
                        ++walkerSteps;
                    }
                }
                const double qx = vx * dt;
                vx -= vx * 0.25;
                const double qz = vz * dt;
                vz -= vz * 0.25;
                z += qz;
                speed -= speed * 0.25;
                x = qx + x - vx * dt;
                z -= vz * dt;
            }
        }
        const double gap = drop - kHover;
        if (speed == 0.0) {
            bobPhase += dt * kBobRate;
            if (bobPhase >= 360.0) bobPhase -= 360.0;
            y += gap - std::sin(bobPhase * kDeg) * kBobAmp;
        } else {
            y += gap * dt * 0.33333334;
        }
        wallPass(w);                              // `sub_459BD0`, both arms
    } else {
        double d2 = 0.0; char n2[2] = {0, 0};
        if (w.surface(x, y - 39.0, z, d2, n2)) y -= 39.0;
        wallPass(w);
    }
}

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
