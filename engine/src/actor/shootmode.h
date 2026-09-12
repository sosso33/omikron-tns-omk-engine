// SPDX-License-Identifier: GPL-3.0-or-later
// SHOOT MODE - the game's third-person gunfight, as the eleven decisions
// `Shoot_Enter` (0x004222D0) and `Shoot_Leave` (0x00422730) make.
//
// This is the MODE, not the AI: whose brains are in `actor/shoot.h` and are
// still unwired for the generic arm (`todo/standing-unknowns.md` 2). What is
// here is everything the two functions decide, which is all data or all
// transcription, and the opcodes that drive them.
//
//     shoot.begin <object>   op 80, 0x00403E80 - 30 shipped sites
//     shoot.end   <clear>    op 81, 0x00403F10 - 74
//     shoot.actor.enter      op 82 - 317
//     shoot.actor.action     op 84 - 319
//
// **`shoot.begin`'s operand is a WEAPON OBJECT.** The handler passes it to
// `Weapon_SlotForObject` (0x0040EA50), which scans the ten int16s at
// `IAM\GLOBAL +42` and returns the slot 5..14 - and stores the answer in
// `dword_4C0134` ONLY if that is still -1, so a gun already in hand beats the
// script's choice. An object that names no weapon (including the -1 that 27
// of the 30 sites pass) falls to the DEFAULT, slot **11**, which the table
// says is object 42, the `Gun Waver`. The other three sites pass object 40,
// the `Baton de pouvoir`, slot 10.
//
// **`shoot.end`'s operand is a CLEAR flag**: nonzero puts `dword_4C0134` back
// to -1 so the next fight re-chooses, zero leaves the weapon in hand. Shipped:
// 43 zeros, 30 ones and one -1.
//
// The entry, in the order the engine does it:
//
//   1  100 records of 192 bytes, zeroed          `Mem_Alloc(0x4B00)`
//   2  the player's record: `+188 = -1`, `+160 |= 2`
//   3  `g_ShootMode = 1`
//   4  the player to `ACTOR_STATE` 3, actor `+1302 = 1`
//   5  the LIBRARY swaps: `Game_Start("shoot2.scx")` + `scptdata\shoot2.sfx`
//   6  the HUD: event 44 property 7 on the PLAYER - character type 5
//      (Mecagarde) opens screen 33, anything else screen 34 + `Hud_Refresh`
//   7  camera mode 4, both camera actors the player
//   8  `.CTL` group 200 and its default entry
//   9  the weapon: if actor `+44`, event 48 -> `Shoot_InitWeapon`
//  10  `Input_InstallScheme(2)`
//  11  one `Shoot_TickPlayer`
//
// and the exit undoes exactly those: `Game_Start("aventure.scx")`, the player
// back to `ACTOR_STATE` 1 with his hierarchy rebound, every other actor in
// state 3 RELEASED from the navigation grid (its saved cell byte written back
// from record `+189`, `sub_435970`), the records freed.
//
// What is NOT here, and is labelled rather than guessed: `Shoot_InitWeapon`'s
// contents, what a shot IS, and the generic brain's geometry. The frontend
// asks `state()` what the mode decided and does the drawing.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace omk {

// `IAM\GLOBAL +42`: ten int16 object ids, slots 5..14. Three ship as -1.
inline constexpr int kWeaponSlotFirst = 5;
inline constexpr int kWeaponSlotCount = 10;
inline constexpr int kWeaponSlotDefault = 11;      // the handler's fallback

class ShootMode {
public:
    // The weapon table, read out of the GLOBAL file's bytes.
    void setWeaponTable(const std::int16_t* slots, int n);
    // `Weapon_SlotForObject`: -1 for -1 and for an object in no slot.
    int slotForObject(int object) const;
    int objectForSlot(int slot) const;

    // op 80. -> whether the mode turned on (it refuses when already in it,
    // which is the handler's `if (dword_6A05E0) return`).
    bool begin(int weaponObject);
    // op 81, `clear` = the operand: nonzero forgets the weapon slot.
    bool end(int clear);

    bool active() const { return active_; }
    int  weaponSlot() const { return weapon_; }
    int  weaponObject() const { return objectForSlot(weapon_); }

    // Which HUD screen the entry opens, from the PLAYER's character type
    // (event 44 property 7). Type 5 is the Mecagarde.
    static int hudScreenFor(int characterType) { return characterType == 5 ? 33 : 34; }
    int hudScreen() const { return hud_; }
    // ...and it RECOMPUTES the screen, because `Shoot_Enter` reads property 7
    // at its step 6 and the entry may already have run `begin`.
    void setPlayerType(int t) { type_ = t; hud_ = hudScreenFor(t); }
    // ...and the type itself, because `sub_47CC70` tests it too: the same 5
    // that opens screen 33 makes the mover's sounds mechanical and pins its
    // pitch (`actor/shootmove.h`).
    int playerType() const { return type_; }

    // The library the mode has resident - the one thing about it a person can
    // hear. `Game_Start` on the way in and out.
    static const char* enterLibrary() { return "shoot2.scx"; }
    static const char* leaveLibrary() { return "aventure.scx"; }
    const char* library() const { return active_ ? enterLibrary() : leaveLibrary(); }

    // The constants the entry installs, exported so a frontend does not
    // re-declare them and a check can assert them.
    static constexpr int kPlayerState   = 3;      // ACTOR_STATE on the way in
    static constexpr int kPlayerStateOut = 1;     // ...and on the way out
    static constexpr int kChannelGroup  = 200;    // the `.CTL` group
    static constexpr int kCameraMode    = 4;
    static constexpr int kInputScheme   = 2;
    static constexpr int kRecordCount   = 100;
    static constexpr int kRecordBytes   = 192;

    // `shoot.actor.enter` / `.action`, kept as the Session already kept them.
    void actorEnter(int actor) { actors_[actor] = 1; actorArgs_[actor] = 0; }
    // `a3`, the opcode's THIRD operand, is kept beside the action: for the
    // patrol (action 1) it names the ROUTE, and dropping it - which this did
    // until 2026-09-12 - sent every patrolling gunman to the nearest free one
    // whatever the designer asked for (`todo/shoot-patrol.md` 2).
    void actorAction(int actor, int action, int a3 = 0) {
        actors_[actor] = action;
        actorArgs_[actor] = a3;
    }
    int  actorAction(int actor) const {
        const auto it = actors_.find(actor);
        return it == actors_.end() ? -1 : it->second;
    }
    int  actorActionArg(int actor) const {
        const auto it = actorArgs_.find(actor);
        return it == actorArgs_.end() ? 0 : it->second;
    }
    const std::map<int, int>& actors() const { return actors_; }
    const std::map<int, int>& actorArgs() const { return actorArgs_; }
    // `Shoot_Leave` walks the 100 records and puts every actor in state 3
    // back; the cell each had claimed is released by the grid's owner.
    std::vector<int> actorsInMode() const;

    // What the last begin/end decided, for a probe and for the frontend.
    struct Event { bool enter = false; int weapon = -1; int hud = -1; const char* why = ""; };
    const std::vector<Event>& log() const { return log_; }
    void clearLog() { log_.clear(); }

private:
    bool active_ = false;
    int  weapon_ = -1;                 // dword_4C0134
    int  hud_    = -1;
    int  type_   = -1;                 // the player's character type
    std::vector<std::int16_t> table_;  // GLOBAL +42
    std::map<int, int> actors_;
    std::map<int, int> actorArgs_;   // the same actors' a3
    std::vector<Event> log_;
};

}  // namespace omk
