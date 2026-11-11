// SPDX-License-Identifier: GPL-3.0-or-later
#include "actor/shootmove.h"
#include "o3de/vecmath.h"

#include <cmath>
#include <cstring>

namespace omk {

int shootSpeedRow(int speed) {
    int r = 0;
    while (r < 29 && speed > kShootSpeedTable[r].upTo) ++r;
    return r;
}

void shootMoveInit(ShootMover& m, int speed, float periodFrames, int charType, int jumpSlot3) {
    m = ShootMover{};                                 // `memset(&dword_6579B0, 0, 0x6C)`
    m.active = true;
    m.period = periodFrames;
    // `if (v11 == 5) { 69, 70, 68; flags |= 0x10 } else { 161, 162, 191; &= ~0x10 }`
    if (charType == 5) {
        m.hurtSound = 68; m.stepLeft = 69; m.stepRight = 70;
        m.flags |= kShootMoveMeca;                    // `BYTE1 |= 0x10` is bit 12
    }
    m.row = shootSpeedRow(speed);
    const ShootSpeedRow& r = kShootSpeedTable[m.row];
    // `(double)(30 * dword_4CF7D4[v4] / 100 + 5) * 1.3` - the int division first
    m.top = static_cast<float>(static_cast<double>(30 * r.top / 100 + 5) * 1.3);
    m.brake = static_cast<float>(static_cast<double>(m.top) * 0.2);
    m.accel = static_cast<float>(static_cast<double>(r.accel) * 0.043333333);
    m.impulse = impulseFromProperty36(jumpSlot3);    // `flt_6579D4`, 0x0047CD66
}

void shootMoveLeave(ShootMover& m) { m.active = false; }

bool shootHurt(ShootMover& m, int band, float& eulerPitch, float& eulerRoll) {
    if (!m.active) return false;                      // `if (dword_6579CC)`
    m.shoveRoll = m.shovePitch = 0.0f;
    switch (band) {                                   // 1.0f / -1.0f, the four cases
        case 0: m.shoveRoll  =  1.0f; break;
        case 1: m.shoveRoll  = -1.0f; break;
        case 2: m.shovePitch =  1.0f; break;
        case 3: m.shovePitch = -1.0f; break;
        default: break;                               // the engine's `default: break`
    }
    m.shoveTimer = 4.0f;                              // `dword_6579F4 = 1082130432`
    eulerPitch = eulerRoll = 0.0f;                    // `u32(+416) = 0; u32(+424) = 0`
    m.flags |= kShootMoveShove;                       // `BYTE1 |= 4`
    return true;
}

bool shootMoveForward(ShootMover& m, bool forward, bool falling, float dt, float& pitchDeg) {
    if (!m.active || (m.flags & kShootMoveBlock) || falling) return false;
    if (m.flags & kShootMoveHead) {
        // `v2 = v1 + v1 + dword_657A10` / `v3 = v1 * -2.0 + dword_657A10`
        double v = static_cast<double>(dt) * (forward ? 2.0 : -2.0) + pitchDeg;
        if (v > 45.0) v = 45.0;
        if (v < -45.0) v = -45.0;
        pitchDeg = static_cast<float>(v);
        return true;
    }
    if (forward) {
        if (m.fwd > 0.0f) m.fwd = 0.0f;
        m.fwdIntent = -m.accel;
    } else {
        if (m.fwd < 0.0f) m.fwd = 0.0f;
        m.fwdIntent = m.accel;
    }
    m.flags |= kShootMoveFwd;
    return true;
}

bool shootMoveStrafe(ShootMover& m, bool right, bool falling) {
    if (!m.active || (m.flags & kShootMoveBlock) || falling) return false;
    m.sideIntent = right ? -m.accel : m.accel;
    m.flags |= kShootMoveSide;
    return true;
}

void shootMoveCrouch(ShootMover& m, bool down) {
    if (down) m.flags = (m.flags & ~kShootMoveTr) | kShootMoveCrouch;   // `and al, 0BFh ; or al, 20h`
    else      m.flags &= ~(kShootMoveCrouch | kShootMoveTr);            // `and al, 9Fh`
}

float shootTurnDegrees(int a1, int sensitivity) {
    return -static_cast<float>(static_cast<double>(sensitivity) * 0.0099999998 *
                               static_cast<double>(a1));
}

float shootPitchStep(float pitchDeg, int dy, int sensitivity, bool inverted, float delta) {
    // `fild sens; fmul 0.01; fild dy (fchs unless inverted); fmulp; fmul
    // delta; fadd pitch`, then the two clamps
    const double d = static_cast<double>(sensitivity) * 0.0099999998 *
                     static_cast<double>(inverted ? dy : -dy) * static_cast<double>(delta);
    float p = static_cast<float>(d + static_cast<double>(pitchDeg));
    if (p > 45.0f) p = 45.0f;
    if (p < -45.0f) p = -45.0f;
    return p;
}

bool shootJump(ShootMover& m, bool falling) {
    // `sub_47D2E0`: `fld 6579F8; fcomp 0.0; test ah, 40h; jz ret` - only
    // when the cool-down is exactly 0
    if (!m.active || m.jumpCool != 0.0f || (m.flags & kShootMoveBlock) || falling) return false;
    m.jumpCool = 1.0f;
    m.vertical = m.impulse;
    m.flags |= kShootMoveHold | kShootMoveBlock;
    return true;
}

ShootMoveStep shootMoveTick(ShootMover& m, float facingDeg, float dt,
                            float* eulerPitch, float* eulerRoll, bool falling,
                            float clipFrame) {
    ShootMoveStep s;
    if (!m.active) return s;
    std::uint32_t f = m.flags;                        // the engine's `v2`, and the
                                                      // shove arm CLEARS 0x400 in it
    const float accelStep = m.accel * dt;                        // v43
    const float top = (f & kShootMoveCrouch) ? m.top * 0.5f : m.top;   // v45
    const float turnBy = m.lastFacing;                           // v44, LAST frame's
    // (the turn momentum `dword_657A04` would add to +420 here; it is zeroed
    // whenever flag 8 is clear, and nothing sets flag 8)
    if (m.vertical > 0.0f) {
        const float v = m.vertical - accelStep;
        m.vertical = v < 0.0f ? 0.0f : v;
    }
    if (!(f & kShootMoveHold)) {
        const float in = m.fwdIntent * dt;                       // var_10
        m.lastFacing = facingDeg;                                // `dword_6579FC = +420`
        float v = m.fwd;
        if (f & kShootMoveFwd) {
            if (f & kShootMoveRun) {
                // over twice the top speed it is SET to twice the top speed,
                // POSITIVE: the sign test before the `jmp` is dead code, so a
                // run backward past the limit would turn forward. MDCO is
                // queued by no H1Avnt entry, so the player never meets it.
                v = in + m.fwd;
                const float lim = top + top;
                if (std::fabs(v) > lim) v = lim;
            } else if (std::fabs(m.fwd) <= top) {
                v = in + m.fwd;
                if (std::fabs(v) > top) v = v > 0.0f ? top : -top;
            } else {
                // above the top speed (he crouched at speed): down by twice the
                // acceleration a frame, toward 0
                v = m.fwd > 0.0f ? m.fwd - (accelStep + accelStep)
                                 : m.fwd - accelStep * -2.0f;
            }
        } else {
            if (m.fwd > 0.0f) { v = m.fwd - m.brake; if (v < 0.0f) v = 0.0f; }
            if (v < 0.0f)     { v = v + m.brake;     if (v > 0.0f) v = 0.0f; }
        }
        m.fwd = v;
        const float sin_ = m.sideIntent * dt;
        float w = m.side;
        if (f & kShootMoveSide) {
            w = m.side - sin_ * -2.0f;                           // twice the forward rate
            if (std::fabs(w) > top) w = w > 0.0f ? top : -top;
        } else {
            const float b2 = m.brake + m.brake;
            if (m.side > 0.0f) { w = m.side - b2; if (w < 0.0f) w = 0.0f; }
            if (w < 0.0f)      { w = w + b2;      if (w > 0.0f) w = 0.0f; }
        }
        m.side = w;
    }
    // ---- THE SHOVE, `sub_47D1F0`'s four frames (0x0047D80F..0x0047D8B5) ----
    // `fld 6579F4; fsub dt; fst 6579F4; fcomp 0.0` - at or below zero the flag
    // goes down and both angles are zeroed; otherwise `fcomp 2.0` picks the
    // sign, `jz` (C0 clear, i.e. timer >= 2.0) taking the SUBTRACT arm.
    if ((f & kShootMoveShove) && eulerPitch && eulerRoll) {
        m.shoveTimer -= dt;
        if (m.shoveTimer <= 0.0f) {
            f &= ~kShootMoveShove;
            *eulerPitch = *eulerRoll = 0.0f;
        } else if (m.shoveTimer < 2.0f) {
            *eulerPitch += m.shovePitch * dt;
            *eulerRoll  += m.shoveRoll * dt;
        } else {
            *eulerPitch -= m.shovePitch * dt;
            *eulerRoll  -= m.shoveRoll * dt;
        }
    }
    const double t = static_cast<double>(turnBy) * kDegToRad;
    const double c = std::cos(t), sn = std::sin(t);
    s.dx = static_cast<float>((c * m.side - sn * m.fwd) * dt);
    s.dz = static_cast<float>((c * m.fwd + sn * m.side) * dt);
    s.dy = static_cast<float>(static_cast<double>(m.vertical) * dt);
    // WHETHER HE STEPS (`edi`, 0x0047D8B5..): under MDTR the z step's raw
    // bits (`mov edi, [ebp+var_10]` - a -0.0 counts), else both velocities
    // read AFTER this frame's update, and flag 0x10 as the frame began
    bool stepping;
    if (f & kShootMoveTr) {
        std::uint32_t bits;
        std::memcpy(&bits, &s.dz, sizeof bits);
        stepping = bits != 0;
    } else {
        stepping = !((m.side == 0.0f && m.fwd == 0.0f) || (f & kShootMoveHold));
    }
    // THE HEAD BOB (0x0047D955..0x0047D9A9): `3 cos(720 +188 / P) - 3 cos(720
    // +192 / P)` (flt_4BCB2C, flt_4BCB68, dbl_4BCB60), only when he steps and
    // not under MDTR, whose arm moves y by the fall alone (0x0047D90A)
    if (stepping && !(f & kShootMoveTr) && m.period > 0.0f && m.prevFrame >= 0.0f) {
        // degrees stored as floats (`fstp [ebp+var_8]`), then x pi/180
        const float a = clipFrame * 720.0f / m.period;
        const float b = m.prevFrame * 720.0f / m.period;
        s.bob = static_cast<float>(std::cos(double(a) * 0.0174532925199433) * 3.0 -
                                   std::cos(double(b) * 0.0174532925199433) * 3.0);
    }
    m.prevFrame = clipFrame;
    // THE JUMP'S HOLD AND COOL-DOWN (0x0047D95C..): flag 0x10 comes down
    // once 0x800 is gone and the fall byte is clear; without it the
    // cool-down runs down by the delta to 0
    if (f & kShootMoveHold) {
        if (!(f & kShootMoveBlock) && !falling) f &= ~kShootMoveHold;
    } else {
        m.jumpCool -= dt;
        if (m.jumpCool < 0.0f) m.jumpCool = 0.0f;
    }
    // THE FOOTSTEPS (0x0047DA41..0x0047DB43, the header has the table)
    if (stepping) {
        const float q = m.period * 0.25f;                       // flt_4BCB6C
        const float ph = clipFrame;                             // +188
        bool left = false, right = false;
        if (ph < q)                  left  = !(f & kShootMoveStepL);
        else if (ph < q + q)         right = !(f & kShootMoveStepR);
        else if (ph < q * 3.0f)      left  = !(f & kShootMoveStepL);   // flt_4BCB2C
        else if (ph < q * 4.0f)      right = !(f & kShootMoveStepR);   // flt_4BCB4C
        if (left)  { f = (f & ~kShootMoveStepR) | kShootMoveStepL; s.sound = m.stepLeft; }
        if (right) { f = (f & ~kShootMoveStepL) | kShootMoveStepR; s.sound = m.stepRight; }
    }
    m.flags = f & 0x17F0u;
    return s;
}

}  // namespace omk
