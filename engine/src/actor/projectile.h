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
#include <functional>
#include <vector>

namespace omk {

inline constexpr int kProjectileSlots  = 256;   // the pool
inline constexpr int kProjectileStride = 60;    // bytes, and it divides exactly
inline constexpr int kWeaponSlots      = 4;     // per actor

struct Projectile {
    int   node   = 0;        // +0   0 = free. The allocator tests ONLY this
    float speed  = 0.0f;     // +8   property34.hi * 3.9
    float travelled = 0.0f;  // +12  `+= speed * dt` a frame; past 1968.5 it is retired
    float vel[3] = {0, 0, 0};// +16  the direction, rotated and scaled by speed
    float scale[3] = {1, 1, 1};   // +28 set to 1.0 by the aim
    float grow   = 0.0f;     // +40  frames the bolt still grows (sprite +20)
    int   owner  = -1;       // +44  the firing actor
    int   sprite = 0;        // +48  from the weapon's node name
    float windUp = 0.0f;     // +52  frames it waits before it flies (sprite +24)
    int   kind   = 0;        // +56  property34.lo
    float pos[3] = {0, 0, 0};// the node's position: the muzzle, then the flight
    // The node's own matrix (`sub_437160(node, v57)`), kept because the
    // flight reads its axis back: `Matrix3x3_RotateVector(-1, 0, 0, node+56)`
    // is the heading the segment is laid along, and the bolt is drawn in it.
    float rot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    float growStep[3] = {0, 0, 0};   // the sprite's +28..+36
};

// What one frame of `Projectiles_Tick` did to an entry that it retired.
struct FlightEvent {
    enum class Why { World, Range };
    int   entry = -1;
    Why   why = Why::Range;
    float at[3] = {0, 0, 0};   // the world hit, or where the range ran out
    float travelled = 0.0f;
};
// The world ray `sub_4449E0` casts: the segment a..b against every set mesh
// but those flagged 0x800000 (`sub_444460`'s own filter). true and the hit
// point when it meets one.
using WorldRay = std::function<bool(const float a[3], const float b[3], float hit[3])>;

// The range, `if (v3[3] > 1968.5039 || v0)`: 50 metres in the engine's inch.
inline constexpr float kProjectileRange = 1968.5039f;

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

struct ShootWeaponRow;

// `Actor_TickProjectiles`' OTHER path - the one an actor holding a weapon
// takes, and so the one shoot mode takes (`todo/shoot-mode.md` 7h). When
// actor `+164` (the object in the hand, `sub_41C490`) carries a node at its
// `+12`, the four-slot walk above is skipped entirely and the shot comes from
// the SHOOT RECORD's weapon row:
//
//     free = the pool's first free entry; if (free < 0) return 0;
//     if (row.index) { count = property 35[row.index - 1];
//                      if (count <= 0 && player) event 48;  // change weapon
//                      HUD = count - 1; property 35 = count - 1; }
//     entry +8  = row.f1    - the SPEED, as it stands: no x3.9 on this path
//     entry +56 = row.i2    - what the hit deals
//     node at the held object's `+12` node - the MUZZLE
//     matrix = sub_442160(0, yaw + 90, -pitch); velocity = (-1,0,0) through
//     it, times the speed (`sub_44D7F0`)
//
// So `tables/shoot_weapons.json`'s "f1 and i2 have NO reader" was a scan of
// `05_sys.c` alone: both are read here, in `17_script.c`.
//
// Note what the magazine does NOT do on this path: gate the shot. A count at
// or below zero asks for a weapon change and the round is taken anyway -
// transcribed, and asserted, rather than tidied.
//
// NOT modelled: event 48 (the change), the player's point-blank ray from his
// shoulder to the muzzle (`sub_4449E0`, which hits at once when the gun is
// through a wall), the node clone, the sprite, and a gunman's aim at the
// player - this is the player's shot.
struct RecordShot {
    float muzzle[3] = {0, 0, 0};
    float yawDeg = 0.0f;     // actor `+420`
    float pitchDeg = 0.0f;   // `dword_657A10`
    int*  ammo = nullptr;    // property 35's count for `row.ammoIndex - 1`
    // The weapon's SHOT SPRITE, `shoot2.sfx` section A by the gun's root mesh
    // name (`FxShotSprite`). With one, `sub_44D7F0` sets +40, +52 and the
    // scales from it; without one the engine leaves them as the entry's last
    // user did, which this does not reproduce - they are zeroed.
    bool  sprite = false;
    float windUp = 0.0f, grow = 0.0f;
    float growStep[3] = {0, 0, 0};
};
struct RecordShotOut {
    int  entry = -1;         // -1: the pool was full and nothing was spent
    bool ammoSpent = false;
    int  hudAmmo = -1;       // `dword_90E11C`
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

    // The record path, above.
    RecordShotOut fireFromRecord(int actor, const ShootWeaponRow& row,
                                 const RecordShot& in);

    // `Projectiles_Tick` (0x0044D930), the flight - which the frame loop runs
    // BEFORE `Actors_TickAll`, so a shot fired this frame first moves on the
    // next. Per live entry:
    //
    //   +52 > 0   it waits at the muzzle: `+52 -= dt`, and nothing else
    //   else      a = pos - 0.5 * heading;  pos += vel * dt
    //             while +40 > 0 (and it has a sprite): +40 -= dt and the
    //               three scales grow by the sprite's step * dt
    //             b = pos + 0.5 * heading;  +12 += speed * dt
    //             [the ACTOR sweep, `sub_45E9C0` - step 2b, not here]
    //             the WORLD ray a..b (`sub_4449E0`)
    //             retired when +12 passes 1968.5 or the ray met something
    //
    // where the heading is the node's own -X axis read back through its
    // matrix, not the velocity. Returns how many it retired this frame.
    int fly(float dt, const WorldRay& world, std::vector<FlightEvent>* out = nullptr);

    const std::array<Projectile, kProjectileSlots>& entries() const { return pool_; }
    void clear() { pool_ = {}; }

private:
    std::array<Projectile, kProjectileSlots> pool_{};
};

}  // namespace omk

#endif
