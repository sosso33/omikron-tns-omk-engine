// SPDX-License-Identifier: GPL-3.0-or-later
//
// THE RAISE - the first-person arm swinging up to aim (`todo/shoot-mode.md`
// 8.0, part 2). A reader's frames of the original show it: the gun low at
// the right at rest, raised to the centre while firing, the bolt leaving it
// there.
//
// It is a LAYER over the channel's clip, laid by `sub_47C2A0` every tick of
// the player in shoot mode:
//
//   * `sub_471070(node, clip, 1, rec+84)` binds every bone the table at
//     0x4C3798 marks - one word per bone SLOT (mesh +12), 1 = the upper body:
//     both arms, the shoulders, the torso, the neck and the head - to the
//     track of `.CTL` group 202's default clip, `S_AUTOLK` in H1Avnt;
//   * `sub_434C30` hands `sub_471950` the two aim angles `dword_6A4720` (yaw)
//     and `dword_6A4724` (pitch) and the weapon's LOWERED amount
//     `dword_6A4728` (the shoot record's +176), and names the base clip
//     `dword_6A472C`, group 200's default - the stance;
//   * `sub_471950`, per marked bone: four of the `S_AUTOLK` track's keys chosen
//     by the bands of sin(yaw) (+-0.707) and the sign of sin(pitch), slerped
//     by `|pitch| * 488.924` (256 at 30 degrees) and `|yaw| * 366.693` (256 at
//     40), then slerped TOWARD the stance clip's key 1 by `lowered * 256`, and
//     set as the bone's LOCAL rotation.
//
// So a lowered weapon holds the stance's arm, and pulling the trigger raises
// it into the aim pose at 0.2 a frame - which is also when the aim angles
// move: `sub_47C2A0` slews them toward their targets at 30 degrees a frame
// only in its PULLED arm, and holds them while released.
//
// For the player the yaw target is `dword_6A4714`, which that arm sets to 0 -
// the body turns with the look - and the pitch target is `flt_6A4718`, which
// the mouse look writes (`sub_47C260`: the look pitch in radians). NOT
// modelled, labelled: the AUTO-LOCK (`sub_47CEE0` / `sub_47CA50` /
// `sub_47CB30`), which points the arm's pitch at a target found in the cone
// instead - the clip's own name, S_AUTOLK, is that.
#pragma once

#include "actor/pose.h"

#include <vector>

namespace omk {

// The bone table at 0x4C3798, forty words by bone SLOT.
inline constexpr int kShootAimMask[40] = {
    1, 1, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0,
    1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 1, 1, 0, 0};
inline bool shootAimMarked(int slot) {
    return slot >= 0 && slot < 40 && kShootAimMask[slot] != 0;
}

// `dword_6579A0` / `dword_6579A4`, the arm's aim angles in RADIANS.
struct ShootAim {
    float yaw = 0.0f;
    float pitch = 0.0f;
};

// `sub_47C2A0`'s player arm, the part that moves the angles - PULLED ticks
// only: each approaches its target by `dt * 0.52359879` (30 degrees a frame)
// and stops on it.
void shootAimSlew(ShootAim& a, float yawTarget, float pitchTarget, float dt);

// `sub_471950`'s key choice and its two slerps, for one bone. `keys[k - 1]`
// is the `S_AUTOLK` track's key k (key 0, the rest sentinel, is not in it);
// fifteen are needed. Returns identity when the track is short.
Quatf shootAimBone(const std::vector<Quatf>& keys, float yawRad, float pitchRad);

// ...and the lowering: `sub_4721F0(aim, stanceKey1, out, lowered * 256)`.
Quatf shootAimLower(const Quatf& aim, const Quatf& stanceKey1, float lowered);

}  // namespace omk
