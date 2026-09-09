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
//     +0   the NODE, a `sub_437850` clone - and NON-ZERO IS WHAT OCCUPIED
//          MEANS: the allocator's free scan tests this and nothing else
//     +8   the speed, `property34.hi * 3.9`
//     +44  the firing actor
//     +48  a value derived from the weapon's own node name (`sub_44EEB0`)
//     +56  `property34.lo`
//
// The node is placed at the WEAPON's `+44/+48/+52` - the muzzle - and
// `sub_44D7F0` then sets its direction and MAY REFUSE, which drops the shot.
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
    int   owner  = -1;       // +44  the firing actor
    int   kind   = 0;        // +56  property34.lo
    float pos[3] = {0, 0, 0};// where the node was placed: the weapon's muzzle
};

// One actor's four weapon slots, as `Actor_TickProjectiles` walks them.
struct WeaponSlot {
    bool  present = false;   // `actor+84 + 4*slot` - 0 ends the walk
    float timer   = 0.0f;    // `actor+148 + 4*slot`, in frames
    int   ammo    = 0;       // property 35 for this slot
    float muzzle[3] = {0, 0, 0};   // the weapon object's +44/+48/+52
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
