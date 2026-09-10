// SPDX-License-Identifier: GPL-3.0-or-later
//
// THE SHOT, FROM THE TRIGGER TO THE POOL (`todo/shoot-mode.md` 7h).
//
// The engine does not fire off the trigger bit. It fires through the `.CTL`
// channel and two one-shot globals, and every link of the chain is read:
//
//   1  the trigger - slot 4 of the *Tirer* scheme, `0x10` - drives `H1Avnt`
//      group 200 into its entry [132]: clip -1, no GoTo, move `MDSHOOT0`;
//   2  `Cef_QueueSpecialMove` queues the handler, and `Actors_TickAll` drains
//      the queue AFTER the actor's state tick. `MDSHOOT0` is
//      `tab_special_move[8]`, 0x0046B610, and its whole body is
//
//          8b 44 24 04          mov  eax, [esp+4]
//          83 b8 94 01 00 00 03 cmp  dword [eax+194h], 3   ; ACTOR_STATE
//          75 0a                jnz  done
//          c7 05 3c ae 53 00 .. mov  dword_53AE3C, 1       ; THE LATCH
//          c3                   ret
//
//      so it arms the latch in ACTOR_STATE 3 and does nothing anywhere else.
//      (`tools/asmfn.py 0x0046B610` returns a DIFFERENT function - the equip
//      code of its neighbour - because the address has no `proc` label; that
//      is how `todo/omk-play.md` 97 came to call this "the equip, not the
//      shot". The bytes say otherwise.)
//   3  the NEXT frame's channel tick, `sub_45C680` case 3 (and `sub_45CF50`,
//      the same code on a clip's roll-over): when `sub_45AB80` says the
//      current entry's `+12` is -1 - which every one of group 200's 24
//      entries is - it takes the SHOOT branch: reload the record's `+172`
//      from the weapon row when it has run out, call `sub_47C2A0(actor, -1,
//      latch, record)`, and clear the latch. Otherwise the latch goes
//      straight to the request.
//   4  `sub_47C2A0` is the GATE, and it is where the rate lives. Outcome 2
//      raises the REQUEST `dword_4E9744` for the player (a gunman fires
//      `Actor_TickProjectiles` on the spot);
//   5  the frame loop, after `Actors_TickAll`: `if (dword_4E9744)
//      Actor_TickProjectiles(player), dword_4E9744 = 0`.
//
// (`loc_45C4DD`, which the handoff named as the latch's one set, is the
// opposite: `mov dword_53AE3C, ebp` after `xor ebp, ebp` - a CLEAR, inside
// `Actor_PlayClip`.)
//
// WHAT THE GATE DOES, read from `sub_47C2A0` (0x0047C2A0). Three numbers on
// the shoot record and three of its flags:
//
//   +172  the rate countdown, in frames - reloaded to the row's `f0` by the
//         caller when it reaches 0, and a shot is taken only on the tick it
//         EQUALS `f0` again, i.e. the tick after a reload;
//   +176  how far the weapon is LOWERED, 0 (up, aimed) .. 1 (down). A pull
//         raises it at 0.2 a frame and nothing fires until it reads exactly
//         0; letting go lowers it at 0.1 a frame;
//   +160  0x40000  a pull PENDING - set by the first pulled tick and held,
//                  latch or not, until a shot clears it. So one tap fires
//                  one round even if the weapon took five frames to come up;
//         0x80000  the pull already counted - stops a held trigger re-arming
//                  0x40000 after every shot; cleared with it when released;
//         0x10000  a shot this pull - set on firing, cleared on the next
//                  pulled tick (or when the weapon is fully down).
//
// WHAT IS NOT HERE, and is labelled rather than guessed: the gate's AIM half
// (the player's target search over 100 actors through `sub_47CA50` /
// `sub_47CB30`, the 30 deg/frame slew of the arms, `sub_434C30`'s pose and
// `sub_4248C0`'s dodge on the target found), the upper-body clip it lays over
// the channel (`sub_471070` with group 202's default), and `Game_HandleEvent`
// 48, the weapon change a spent magazine triggers.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace omk {

struct ShootRecord;

// One row of the two weapon tables `Shoot_InitWeapon` picks from - the
// player's at 0x004C3658, everybody else's at 0x004C36F8. Lifted to
// `tables/shoot_weapons.json`; the names are what the fields DO, now that
// `Actor_TickProjectiles`' record path is read:
struct ShootWeaponRow {
    float rate = 0.0f;       // f0: frames between shots, `+172`'s reload
    float speed = 0.0f;      // f1: the projectile's `+8`, inches a frame
    int   damage = 0;        // i2: the projectile's `+56`, what a hit deals
    int   key = -1;          // the weapon TYPE the row answers; -1 ends it
    int   ammoIndex = 0;     // 0 = no magazine; else property 35's slot + 1
};

class ShootWeaponTable {
public:
    static ShootWeaponTable loadJson(const std::string& path);
    bool loaded() const { return !player_.empty() && !other_.empty(); }
    // `Shoot_InitWeapon`'s walk: the first row whose key is `type`, stopping
    // at the -1 row. nullptr when none - the record's `+180` stays 0 and the
    // gate returns at its first line.
    const ShootWeaponRow* find(int type, bool player) const;
    const std::vector<ShootWeaponRow>& rows(bool player) const {
        return player ? player_ : other_;
    }

private:
    std::vector<ShootWeaponRow> player_, other_;
};

// `Shoot_InitWeapon`'s TYPE: event 46 property 3 of the object in the hand,
// which is the OBJECTS record's `+2` (`Game_HandleEvent` case 46 reads
// `i16i(record, 1)`) - the kind. One hand-written exception: a type 1 whose
// model name, uppercased, carries a `B` eleven characters from its end is
// type -2. That name is the held node's descriptor `+48`, which
// `Scene_Load3DO` fills with its own PATH - and `Object_Load` builds the
// path as "MESHES\OBJETS\" + `Object_ModelPath(stem)`, which appends ".3DO"
// (0x4C0D1C). So position -11 is the first letter of a SEVEN-letter stem,
// and of the seven shipped weapons only `BATPOUV`, the Baton de pouvoir,
// has one that is a `B`: the -2 row is the baton's.
int shootWeaponType(int objectKind, const std::string& modelName);

// The two one-shot globals of the chain.
struct ShotLatch {
    bool latch = false;      // dword_53AE3C - `MDSHOOT0` sets it
    bool request = false;    // dword_4E9744 - the frame loop services it
};

inline constexpr std::uint32_t kShootPullPending = 0x40000;
inline constexpr std::uint32_t kShootPullCounted = 0x80000;
inline constexpr std::uint32_t kShootFired       = 0x10000;

// `MDSHOOT0` (0x0046B610): the latch, in ACTOR_STATE 3 only.
// -> whether it armed.
bool mdShoot0(ShotLatch& l, int actorState);

// `sub_47C2A0`'s return: 0 released, 1 pulled (or a pull pending), 2 FIRED.
enum class FireGate { Released = 0, Pulled = 1, Fired = 2 };

// `sub_47C2A0`'s TIMING half, on the record's `+160/+172/+176/+180`. `dt` is
// `flt_4C30D8`. The arithmetic is the engine's: the steps are formed in
// double and stored back to float, and the fire test is an exact float
// compare - which is why the probe steps it rather than predicting it.
FireGate shootFireGate(ShootRecord& r, bool pulled, float dt);

// `sub_45C680` case 3's shoot branch (`LABEL_63`) and `sub_45CF50`'s copy of
// it. `shootPose` is `sub_45AB80`: the channel's current entry has `+12 ==
// -1`. For the player, a FIRED gate raises the request; for anyone else the
// caller fires directly, so the result is returned either way.
FireGate shootChannelTick(ShootRecord& r, ShotLatch& l, bool shootPose,
                          bool isPlayer, float dt);

// `sub_442160(a, b, c, m)` (0x00442160), the engine's Euler matrix, and
// `Matrix3x3_RotateVector` (0x00442D70), which applies it in the ROW-vector
// convention. Transcribed rather than folded into a closed form so the probe
// can hold the two against each other.
void shootEulerMatrix(float a, float b, float c, float m[9]);
void shootRotateRow(const float v[3], const float m[9], float out[3]);

// `Actor_TickProjectiles`' record path, the PLAYER's aim: the shot's matrix
// is `sub_442160(0, (yaw + 90) deg, -pitch deg)` - `v54 = -dword_657A10`,
// `v52 = (+420 + 90)` - and the direction is `(-1, 0, 0)` through it. At
// pitch 0 that is exactly the `(0, 0, -1)` forward `sub_47CA50` aims with,
// and a POSITIVE pitch rises (y grows down), which is `dword_657A10`'s sense
// and this port's `shootPitch`'s.
void shootShotDirection(float yawDeg, float pitchDeg, float dir[3]);

}  // namespace omk
