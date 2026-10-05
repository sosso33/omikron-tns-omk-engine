// SPDX-License-Identifier: GPL-3.0-or-later
//
// ASTAROTH - the end-game boss, shoot character type 13 (`todo/astaroth.md`).
//
// He is not a gunman with a table but a hand-written machine, and the fight
// is built on ENGINE GLOBALS rather than on his shoot record:
//
//   dword_657AFC    how many of his six weak points are down
//   dword_657B00[6] the weak points: SET MESHES of `PAstarot.3DO`, found by
//                   name (`PAame01`..`PAame06`, the table at 0x4CFCD0)
//   dword_657B18[6] the bolts each still takes - 3 at the setup
//   dword_657AF4    his `AstDos` mesh, the back a bolt must strike (step 2)
//   flt_657AF8      the difficulty factor, 1.0 / 1.2 / 1.4
//   flt_6A062C      his animation rate (step 3)
//   off_4C8444      the WORLD-HIT CALLBACK: `Projectiles_Tick` calls it with
//                   the set mesh a bolt's world ray struck (0x44DDDF), and
//                   his setup installs `sub_47FCF0` there
//
// One fight at a time, as the engine's globals allow.
#pragma once

#include "actor/shoot.h"

#include <functional>

namespace omk {

inline constexpr int kAstarothType = 13;
inline constexpr int kAstarothSouls = 6;
// `off_4CFCD0`: six names and a NULL, read from the image (0x4CFD98..0x4CFDC0).
// `PAstarot.3DO` carries a seventh, `PAame10`, which nothing names.
inline constexpr const char* kAstarothSoulNames[kAstarothSouls] = {
    "PAame01", "PAame02", "PAame03", "PAame04", "PAame05", "PAame06"};
// `dword_4CFCF0`: the message each soul posts when it goes down
inline constexpr int kAstarothSoulMessages[kAstarothSouls] = {27, 28, 29, 30, 31, 32};
inline constexpr int kAstarothSoulHits = 3;

struct AstarothFight {
    int   destroyed = 0;                              // dword_657AFC
    int   soulMesh[kAstarothSouls] = {-1, -1, -1, -1, -1, -1};   // dword_657B00, -1 = 0
    int   hits[kAstarothSouls] = {0, 0, 0, 0, 0, 0};  // dword_657B18
    int   backMesh = -1;                              // dword_657AF4 (in HIS model)
    float difficulty = 1.0f;                          // flt_657AF8
    float rate = 1.0f;                                // flt_6A062C
    // `off_4C8444 == sub_47FCF0`. `Shoot_Enter` resets it (`sub_44CD90(0)`,
    // 0x4222F6) and the sixth soul clears it (0x47FD74).
    bool  callbackArmed = false;
};

// `sub_47FF70` (0x0047FF70), what `Shoot_ActorEnter` -> `sub_47DFD0` runs for
// a type-13 character - and again from his tick's default arm:
//
//   dword_657AFC = 0;  +156 = 29;  +88 = 10;  +160 |= 0x4020;
//   (sub_4B2E90: the stand grid, clip id 13 - step 3)
//   sub_44CD90(sub_47FCF0);                 the world-hit callback
//   memset(0x657A30, 0, 0xC0);              no reader found - not modelled
//   dword_657AF4 = o3de_FindMeshByName(his node, "AstDos");
//   for each name: dword_657B00[i] = o3de_FindNodeByName(world root, name);
//                  sub_436F50(it) - SHOWN (flag 2 cleared over its subtree);
//                  dword_657B18[i] = 3;
//   (dword_657AF0 = slot 1's shot-sprite row - step 3)
//   flt_6A062C = 1.0;
//   flt_657AF8 = difficulty 0 -> 1.0, 1 -> 1.2, 2 -> 1.4, else unchanged
//
// `findSoul(name)` answers the set mesh of that name (-1 for none - the
// engine stores a null, which no struck mesh then matches). The souls to
// SHOW are `fight.soulMesh` afterwards; showing is the caller's.
void astarothSetup(AstarothFight& fight, ShootRecord& rec,
                   const std::function<int(const char*)>& findSoul, int backMesh,
                   int difficulty);

// `sub_47FCF0` (0x0047FCF0), the world-hit callback, given the set mesh a
// bolt struck:
//
//   for (i = 5; i >= 0; --i) if (dword_657B00[i] && dword_657B00[i] == mesh) break;
//   if (found && --dword_657B18[i] == 0) {
//       sub_436F20(mesh);                       HIDDEN: flag 2 over its subtree
//       ++dword_657AFC;  dword_657B00[i] = 0;
//       Game_RaiseEvent(43, {dword_4CFCF0[i], i});   MESSAGE 27+i, sender i
//   }
//   if (dword_657AFC == 6) sub_44CD90(0);       tested on EVERY call
//
// Any bolt, any owner, any damage - it tests neither. The sender is the raw
// index: `Message_RunHandlers` maps a sender only for messages 0..12.
struct SoulHit {
    int  soul = -1;          // which, -1 when the mesh is none of them
    int  hitsLeft = -1;
    bool destroyed = false;  // -> hide `mesh`, post `message` from `soul`
    int  message = -1;
    bool disarmed = false;   // the callback cleared by this call
};
SoulHit astarothSoulHit(AstarothFight& fight, int mesh);

}  // namespace omk
