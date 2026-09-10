// SPDX-License-Identifier: GPL-3.0-or-later
//
// THE HIT - what a bolt meets, and what it does to him (`todo/shoot-mode.md`
// 7j, step 2b).
//
// `Projectiles_Tick` sweeps each move's segment against the BODIES first and
// casts it against the world only if no body stopped it. The bodies are the
// two lists `sub_45DF50(1, 320)` sizes - every attached actor's node
// (`Actor_Attach` -> `sub_45DFF0`) and the player's (`Player_SetActor` ->
// `sub_45E140`) - less the shooter's own. Per body, `sub_45E9C0`:
//
//   1  the NODE's sphere: its world position and the ROOT mesh's radius -
//      which on a character is the WHOLE body's (the pelvis `Bassin` carries
//      44.4 on VIR_FN and 42.5 on HO1_FN, where a forearm carries 7);
//   2  `o3de_Traverse` with `sub_45ECA0` on every mesh of it: the mesh's own
//      sphere (its record's +76..84 centre, turned by the mesh's world
//      matrix, radius +88), then the segment carried into the mesh's frame
//      and tested against its local BOX, +92..+112 (`sub_4986B0`);
//   3  the nearest box hit along the segment wins.
//
// Boxes, not triangles - a body is nineteen oriented boxes.
//
// What a hit does, `sub_4240E0(victim, damage, entry, segment)`:
//
//   * refused unless the victim OR the shooter is the player - gunmen do not
//     hurt each other - and the victim is in ACTOR_STATE 3, and its record's
//     +160 lacks 0x800;
//   * damage 6 (the baton's) reaches ONLY victims flagged 0x4000, and every
//     other damage is refused by them;
//   * the player's Body Shield, property 17, takes up to 100% off, leaving
//     at least 1;
//   * `+160 |= 0x1020`, health `+92 -= damage`;
//   * a gunman left alive REACTS (`sub_423EF0`): above property 24 his `+148`
//     action if he has one, at or below it action 4;
//   * a gunman killed drops the enemy count and plays a death clip whose TYPE
//     comes from the hit's direction (`sub_423E20`), `+160 |= 8`.
//
// A refused hit still STOPS the bolt: `Projectiles_Tick` retires the entry
// on any body it met, and the refusal only withholds the impact sprite.
//
// NOT modelled, and labelled: `sub_47FD90` / `sub_47DF60`, the two unread
// functions that types 13 and 10 put a 0x4000 victim's damage through; the
// explosion a damage over 10 sets off (`sub_424470`); the global
// `dword_90E0FC` that refuses every hit; and the player's own hurt and death
// (`sub_47D1F0`, `sub_423FC0`), which are the frontend's.
#pragma once

#include <cstdint>
#include <vector>

namespace omk {

struct ShootRecord;

// `sub_498860` (0x00498860): the line through `o` along the UNIT `dir`
// against a sphere. false when it misses; else the two crossings, `tIn`
// before `tOut`. Its callers accept `tIn <= length && tOut >= 0`.
bool shootRaySphere(const float o[3], const float dir[3], const float c[3], float r,
                    float& tIn, float& tOut);

// `sub_4986B0` (0x004986B0): the ray against a box in the box's own frame -
// Woo's test, written as the engine wrote it. Per axis the start is BELOW the
// box (the near plane is the minimum), ABOVE it, or between. If it is between
// on all three the start is the hit. Otherwise the plane with the largest
// parameter is the one met, and the other axes are checked at that point -
// but the assembly checks only ONE bound each: an axis whose start was above
// the box needs the hit at or above its minimum, one whose start was below
// needs it at or below its maximum, and an axis whose start lay BETWEEN its
// planes is not checked at all. That last is not standard Woo; it is what
// 0x004987F0..0x00498852 do, and it is transcribed rather than corrected.
bool shootRayBox(const float lo[3], const float hi[3], const float o[3],
                 const float dir[3], float hit[3]);

// One mesh of a posed body, in the world: the node's origin and its world
// rotation as a matrix, world = pos + M * local, M column-major (m[0..2] is
// the mesh's local X in the world). The bounds are the record's own.
struct HitMesh {
    float pos[3] = {0, 0, 0};
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    float centre[3] = {0, 0, 0};   // +76
    float radius = 0.0f;           // +88
    float boxMin[3] = {0, 0, 0};   // +92
    float boxMax[3] = {0, 0, 0};   // +104
};
struct HitBody {
    int actor = -1;
    int root = -1;                 // the node's own mesh: its sphere is the body's
    std::vector<HitMesh> meshes;
};
struct BodyHit {
    int   body = -1, actor = -1, mesh = -1;
    float dist = 0.0f;             // along the segment, from its start
    float at[3] = {0, 0, 0};       // in the world (the engine keeps it in the
                                   // mesh's frame; see shoothit.cpp)
};

// `sub_45E9C0` with `sub_45ECA0`: the segment a..b against every body but
// `exclude` (an actor id). true and the nearest hit when one is met.
bool shootSweepBodies(const float a[3], const float b[3],
                      const std::vector<HitBody>& bodies, int exclude, BodyHit& out);

// `sub_423E20` (0x00423E20): the dot of the bolt's horizontal heading with
// the victim's forward - `sub_442160(0, yaw, 0)` through (0, 0, -1) - in four
// bands, on the constants at 0x4BC274 (0.5) and 0x4BC224 (0.0):
//
//     |dot| <= 0.5:  0 when dot <= 0, else 1
//     otherwise:     2 when dot <= 0, else 3
//
// The decompiler's version lost the x87 branches; this is the assembly's.
int shootHitBand(float victimYawDeg, const float boltVel[3]);

// The death clip TYPE `sub_4240E0` asks `List_PickRandomByType` for, per
// band: 6, 7, 5, 8. A body with none of that type falls to type 5.
int shootDeathClipType(int band);

struct HitIn {
    int   damage = 0;              // the entry's +56
    bool  shooterIsPlayer = false;
    bool  victimIsPlayer = false;
    bool  victimInShoot = true;    // ACTOR_STATE 3
    int   bodyShield = 0;          // property 17 - read for the player only
    int   reactAt = 0;             // property 24 - read for a gunman only
    float victimYaw = 0.0f;
    float boltVel[3] = {0, 0, 0};
};
struct HitOut {
    bool refused = true;           // `return -1`: nothing done - the bolt still stops
    int  damage = 0;               // after the shield
    int  band = -1;
    int  healthWas = 0, health = 0;
    bool killed = false;
    int  deathType = -1;           // the clip TYPE, a gunman's
    int  action = -1;              // what `Shoot_ActorAction` was asked, -1 none
    bool enemyCountDrop = false;   // `--dword_4E9764`
};
HitOut shootApplyHit(ShootRecord& victim, const HitIn& in);

}  // namespace omk
