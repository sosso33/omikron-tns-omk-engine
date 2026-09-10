// SPDX-License-Identifier: GPL-3.0-or-later
#include "actor/shootaim.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace omk {

void shootAimSlew(ShootAim& a, float yawTarget, float pitchTarget, float dt) {
    // `v29 = flt_4C30D8 * 0.52359879;` then each angle steps toward its
    // target by v29 and is clamped onto it
    const double step = double(dt) * 0.52359879;
    const auto slew = [step](float& v, float target) {
        if (v < target) {
            const double n = step + v;
            v = static_cast<float>(n);
            if (n > target) v = target;
        } else if (v > target) {
            const double n = double(v) - step;
            v = static_cast<float>(n);
            if (n < target) v = target;
        }
    };
    slew(a.yaw, yawTarget);
    slew(a.pitch, pitchTarget);
}

Quatf shootAimBone(const std::vector<Quatf>& keys, float yawRad, float pitchRad) {
    if (keys.size() < 15) return Quatf{};
    // key k of the track, 1-based, as `v26[4k .. 4k+3]`
    const auto K = [&keys](int k) { return keys[static_cast<std::size_t>(k - 1)]; };
    // `v25 = sin(v70)`: the YAW's band, as (v28, v29)
    const double sy = std::sin(double(yawRad));
    int b28, b29;
    if (sy <= 0.0) {
        if (sy >= -0.70700002) { b28 = 2; b29 = 3; }
        else                   { b28 = 3; b29 = 4; }
    } else if (sy <= 0.70700002) { b28 = 2; b29 = 1; }
    else                         { b28 = 1; b29 = 0; }
    // `v76 = sin(v24)`: the PITCH's sign picks the half of the grid
    const double sp = std::sin(double(pitchRad));
    Quatf A = K(1), B, C, D;
    if (sp <= 0.0) {
        if (b28 == 1)      { A = K(11); B = K(4);  C = K(6); D = K(5);  }
        else if (b28 == 2) { B = b29 == 1 ? K(11) : K(12); C = K(7);
                             D = b29 == 1 ? K(6) : K(8); }
        else               { A = K(12); B = K(9);  C = K(8); D = K(15); }
    } else {
        if (b28 == 1)      { A = K(11); B = K(4);  C = K(3); D = K(13); }
        else if (b28 == 2) { B = b29 == 1 ? K(11) : K(12); C = K(2);
                             D = b29 == 1 ? K(3) : K(10); }
        else               { A = K(12); B = K(9);  C = K(10); D = K(14); }
    }
    // `v47 = abs32((int64_t)(v24 * 488.92401))`, the PITCH weight; the yaw's
    // `v48 = abs32((int64_t)(v70 * 366.69299)); if (v48 > 0x100) v48 -= 256;`
    const auto w47 = static_cast<unsigned>(
        std::llabs(static_cast<long long>(double(pitchRad) * 488.92401)));
    unsigned w48 = static_cast<unsigned>(
        std::llabs(static_cast<long long>(double(yawRad) * 366.69299)));
    if (w48 > 0x100u) w48 -= 256u;
    const Quatf v77 = qslerpK(A, C, w47);
    const Quatf v79 = qslerpK(B, D, w47);
    return qslerpK(v77, v79, w48);
}

Quatf shootAimLower(const Quatf& aim, const Quatf& stanceKey1, float lowered) {
    // `sub_4721F0(v80, key1, v78, (int64_t)(as_f32(v71) * 256.0))`
    const long long k = static_cast<long long>(double(lowered) * 256.0);
    return qslerpK(aim, stanceKey1, static_cast<unsigned>(k < 0 ? 0 : k));
}

}  // namespace omk
