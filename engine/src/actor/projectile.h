// SPDX-License-Identifier: GPL-3.0-or-later
//
// THE PROJECTILE POOL - what a shot IS (`todo/shoot-mode.md` 4c, 7f).
//
// `Actor_TickProjectiles` (0x0044D110) is the whole of firing, and it is a
// pool allocator with a gate in front of it. Four weapon SLOTS per actor,
// each with its own weapon object at `actor+84 + 4*slot`, its own countdown
// at `actor+148 + 4*slot`, and its own ammunition.
//
// THE POOL'S GEOMETRY was mis-measured in this tree once and the span is what
// settles it: `0x534F48 - 0x531348 = 15360`, which is **256 entries of 60
// bytes** exactly, where the 52 first recorded gives 295.38. A stride that
// does not divide the pool is not a stride.
//
// THE 60 BYTES ACCOUNT FOR EXACTLY, once `sub_44D7F0` (the aim) is read
// beside the allocator - which is the walk-lands-on-the-size test the ground
// rules ask for, applied to a struct rather than a file:
//
//     +0   the NODE, a `sub_437850` clone - and NON-ZERO IS WHAT OCCUPIED
//          MEANS: the allocator's free scan tests this and nothing else
//     +4   zeroed by the aim
//     +8   the speed, `property34.hi * 3.9`
//     +12  zeroed by the aim
//     +16  the VELOCITY - the direction rotated into world and scaled by the
//     +20  speed. `Matrix3x3_RotateVector(dir, node+56, &e[4], &e[5], &e[6])`
//     +24  then `e[4..6] *= speed`
//     +28  a scale, set to 1.0
//     +32  1.0
//     +36  1.0
//     +40  `sub_44F180(sprite)`
//     +44  the firing actor
//     +48  the sprite handle, from the weapon's own node name (`sub_44EEB0`)
//     +52  `sub_44F1A0(sprite)`
//     +56  `property34.lo`
//                                                              = 60 exactly
//
// The node is placed at the WEAPON's `+44/+48/+52` - the muzzle - and the
// DIRECTION comes from `actor + 12*slot + 100`, a per-slot float[3]. Note
// where that lands: four slots of twelve bytes fill `+100..+148`, and `+148`
// is where the four per-slot fire timers begin. The two arrays butt exactly.
//
// `sub_44D7F0` MAY REFUSE, and its refusals are object-graph ones: no node,
// or `o3de_UnlinkObject` / `o3de_LinkObjectToParent` failing.
//
// A SHOT WITH THE POOL FULL IS SIMPLY NOT TAKEN: the scan returns -1 and the
// function returns without firing, without spending the round's timer.
#ifndef OMK_ACTOR_PROJECTILE_H
#define OMK_ACTOR_PROJECTILE_H

#include <array>
#include <cstdint>

namespace omk {

inline constexpr int kProjectileSlots  = 256;   // the pool
inline constexpr int kProjectileStride = 60;    // bytes, and it divides exactly
inline constexpr int kWeaponSlots      = 4;     // per actor

struct Projectile {
    int   node   = 0;        // +0   0 = free. The allocator tests ONLY this
    float speed  = 0.0f;     // +8   property34.hi * 3.9
    float vel[3] = {0, 0, 0};// +16  the direction, rotated and scaled by speed
    float scale[3] = {1, 1, 1};   // +28 set to 1.0 by the aim
    int   owner  = -1;       // +44  the firing actor
    int   sprite = 0;        // +48  from the weapon's node name
    int   kind   = 0;        // +56  property34.lo
    float pos[3] = {0, 0, 0};// where the node was placed: the weapon's muzzle
};

// One actor's four weapon slots, as `Actor_TickProjectiles` walks them.
struct WeaponSlot {
    bool  present = false;   // `actor+84 + 4*slot` - 0 ends the walk
    float timer   = 0.0f;    // `actor+148 + 4*slot`, in frames
    int   ammo    = 0;       // property 35 for this slot
    float muzzle[3] = {0, 0, 0};   // the weapon object's +44/+48/+52
    float dir[3]   = {0, 0, 1};    // `actor + 12*slot + 100`, already world
};

// What one shot needs that this port does not compute. `speedHi` and `kindLo`
// are property 34's two halves; `reload` is what the countdown is set to;
// `aimAccepts` stands for `sub_44D7F0`, which is unread and may REFUSE.
struct FireIn {
    int   speedHi = 0;
    int   kindLo  = 0;
    float reload  = 0.0f;
    bool  aimAccepts = true;
};

class ProjectilePool {
public:
    // `Actor_TickProjectiles`'s free scan: the first entry whose node is 0,
    // or -1 when the pool is full.
    int freeSlot() const;
    int live() const;

    // One actor's tick. Walks his four slots in order and stops at the first
    // ABSENT weapon, exactly as the engine's `if (!weapon) return` does - so
    // a hole in the slots hides everything after it. Returns how many shots
    // were actually taken.
    int tick(int actor, std::array<WeaponSlot, kWeaponSlots>& slots,
             const FireIn& in, float dt);

    const std::array<Projectile, kProjectileSlots>& entries() const { return pool_; }
    void clear() { pool_ = {}; }

private:
    std::array<Projectile, kProjectileSlots> pool_{};
};

}  // namespace omk

#endif
