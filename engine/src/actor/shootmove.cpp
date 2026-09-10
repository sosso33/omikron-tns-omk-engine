// SPDX-License-Identifier: GPL-3.0-or-later
#include "actor/shootmove.h"

#include <cmath>

namespace omk {

int shootSpeedRow(int speed) {
    int r = 0;
    while (r < 29 && speed > kShootSpeedTable[r].upTo) ++r;
    return r;
}

void shootMoveInit(ShootMover& m, int speed, float periodFrames) {
    m = ShootMover{};                                 // `memset(&dword_6579B0, 0, 0x6C)`
    m.active = true;
    m.period = periodFrames;
    m.row = shootSpeedRow(speed);
    const ShootSpeedRow& r = kShootSpeedTable[m.row];
    // `(double)(30 * dword_4CF7D4[v4] / 100 + 5) * 1.3` - the int division first
    m.top = static_cast<float>(static_cast<double>(30 * r.top / 100 + 5) * 1.3);
    m.brake = static_cast<float>(static_cast<double>(m.top) * 0.2);
    m.accel = static_cast<float>(static_cast<double>(r.accel) * 0.043333333);
}

void shootMoveLeave(ShootMover& m) { m.active = false; }

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

ShootMoveStep shootMoveTick(ShootMover& m, float facingDeg, float dt) {
    ShootMoveStep s;
    if (!m.active) return s;
    const std::uint32_t f = m.flags;
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
    // (flag 0x400, the shove, would move +416/+424 here - `sub_47D1F0`)
    const double t = static_cast<double>(turnBy) * 0.017453292519943295;
    const double c = std::cos(t), sn = std::sin(t);
    s.dx = static_cast<float>((c * m.side - sn * m.fwd) * dt);
    s.dz = static_cast<float>((c * m.fwd + sn * m.side) * dt);
    s.dy = static_cast<float>(static_cast<double>(m.vertical) * dt);
    // (the head bob and the footsteps are here in the engine - not modelled)
    m.flags = f & 0x17F0u;
    return s;
}

}  // namespace omk
