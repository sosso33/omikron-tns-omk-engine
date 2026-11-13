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
    // `dword_4E9760`, THE FREEZE (todo/drift-audit.md S7): ops 106 / 107
    // (`shoot.freeze_all` / `.unfreeze_all`, bare jumps to 0x422B90 /
    // 0x422BD0) set or clear bit 0x8000 of `+160` on all 100 records and
    // write this - and do NOTHING while `g_ShootRecords` is null, i.e.
    // outside a phase. A record `sub_422540` makes while it is set starts
    // with the bit; the noise, a bolt's hit, a strike and an explosion
    // (`sub_4246E0`, `sub_4240E0`, `sub_423B10`, `sub_424470`) clear it on
    // every record before they act - a freeze holds until something happens.
    // `Shoot_Enter` / `Shoot_Leave` never write it, so it outlives a phase.
    // The records are the frontend's, so a frontend keeps their bit equal
    // to this (every writer touches all of them at once).
    bool frozen() const { return frozen_; }
    void setFrozen(bool on) { frozen_ = on; }
    // `g_PlayerBehaviourOff` (dword_4E9778), THE PLAYER'S SUSPEND
    // (todo/drift-audit.md S7): op 116 `shoot.player.suspend` (0x4050E0 ->
    // 0x422950) sets it with the player half of `Shoot_Leave`, op 117
    // `.resume` (0x405130 -> 0x4229C0) clears it with the player half of
    // `Shoot_Enter`. What it gates: `Shoot_TickNpc` skips EVERY gunman's
    // brain callback while it is set, and `Shoot_SyncHudHealth` the gauge.
    // 212 shipped sites - the ladders and doors a phase's scripts play as
    // a short cutscene. The counts tell a frontend each one happened.
    bool playerOff() const { return playerOff_; }
    void suspendPlayer() { playerOff_ = true; ++suspends_; }
    void resumePlayer()  { playerOff_ = false; ++resumes_; }
    long suspends() const { return suspends_; }
    long resumes()  const { return resumes_; }
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
    //
    // `Shoot_ActorEnter` (0x00422C10) runs NO action: it hands the type to
    // `sub_47DFD0`, whose default arm starts a type-11 clip and leaves `+156`
    // at 0 (todo/drift-audit.md S13) - not action 1, the patrol, which no
    // shipped enter asks for. An entered actor with no action yet reads
    // `kEnteredOnly`; `actorIn` is "in shoot mode" whatever the action.
    static constexpr int kEnteredOnly = -2;
    void actorEnter(int actor) { actors_[actor] = kEnteredOnly; actorArgs_[actor] = 0; }
    bool actorIn(int actor) const { return actors_.count(actor) != 0; }
    // Every REQUEST after the enter - op 84, a hit's reaction (`sub_423EF0`),
    // a noise heard (`sub_4246E0`) - is `Shoot_ActorAction` called at once in
    // the engine. The serial counts them so a frontend that applies them on
    // the actor's own tick can tell a new one from the last one.
    int  actorActionSerial(int actor) const {
        const auto it = serial_.find(actor);
        return it == serial_.end() ? 0 : it->second;
    }
    // `a3`, the opcode's THIRD operand, is kept beside the action: for the
    // patrol (action 1) it names the ROUTE, and dropping it would send every
    // patrolling gunman to the nearest free one whatever the designer asked
    // for (`todo/shoot-patrol.md` 2).
    void actorAction(int actor, int action, int a3 = 0) {
        actors_[actor] = action;
        actorArgs_[actor] = a3;
        ++serial_[actor];
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
    // an actor KEY renamed (`Session::actorKey`, todo/drift-audit.md M4): a
    // second copy of an id becomes the id's own once the lower slot is freed
    void forget(int key) { actors_.erase(key); serial_.erase(key); actorArgs_.erase(key); }
    void rekey(int from, int to) {
        for (auto* m : {&actors_, &serial_, &actorArgs_})
            if (auto it = m->find(from); it != m->end()) {
                (*m)[to] = it->second;
                m->erase(from);
            }
    }
    const std::map<int, int>& actorArgs() const { return actorArgs_; }
    // `Shoot_Leave` walks the 100 records and puts every actor in state 3
    // back; the cell each had claimed is released by the grid's owner.
    std::vector<int> actorsInMode() const;

    // ---- THE WEAPON CHANGE, `Game_HandleEvent` case 48 (0x004067D0) -------
    //
    // What `MDGUN` (0x0046B740, H1Avnt group 200 entry 134, input 0x2000),
    // `Shoot_Enter`, `shoot.player.resume` and a shot from an empty magazine
    // all raise. Two flags steer it, `dword_4E6C80` (ARMED) and `dword_4E6C84`
    // (LOAD):
    //
    //     if ARMED:  ++slot; then until back where it started:
    //                    slot 15 wraps to 5;
    //                    TAKE it if its object is in list 0 (carried) and
    //                      (slot >= 10 || i16(record + 250 + 2*slot) > 0)
    //                      - the ammunition, property 35, for slots 5..9;
    //                    LOAD = (a slot was taken)
    //     LOAD ? free the held slot and load the slot's object : nothing
    //     ARMED = 1; return the new object slot, or -1
    //
    // So a slot 10..14 needs no rounds, a slot 5..9 one at least, and coming
    // back round to the start changes nothing. The writers of the flags are
    // all read from the image: op 80 sets LOAD 1 / ARMED 0 before
    // `Shoot_Enter` (0x00403EE6), so the entry's own event loads the gun the
    // slot names; op 117 does the same when his hand is empty and clears both
    // when it is not (0x00405150); event 43 with MESSAGE 1 - the phase lost -
    // clears both (0x004079A9). An unarmed call only arms.
    //
    // `carried(object)` is `ObjectList_Find(0, object) != -1`; `ammoAt(slot)`
    // the record's `i16(+250 + 2*slot)`. -> the slot now held, or -1 when
    // nothing changed. A slot of -1 cannot reach this in the engine (op 80 has
    // set one); the engine's loop would not end there, the port returns -1.
    template <class Carried, class AmmoAt>
    int changeWeapon(Carried carried, AmmoAt ammoAt) {
        const int start = weapon_;
        if (cycleArmed_ && start >= 0) {
            int s = ++weapon_;
            if (s == start) cycleLoad_ = false;
            else for (;;) {
                if (s == 15) weapon_ = s = kWeaponSlotFirst;
                const int obj = objectForSlot(s);
                if (obj != -1 && carried(obj) && (s >= 10 || ammoAt(s) > 0)) {
                    cycleLoad_ = true;
                    break;
                }
                s = ++weapon_;
                if (s == start) { cycleLoad_ = false; break; }
            }
        }
        cycleArmed_ = true;
        ++changes_;
        return cycleLoad_ ? weapon_ : -1;
    }
    // op 80's and op 117's writes before their event 48 (above)
    void armWeaponLoad(bool load) { cycleLoad_ = load; cycleArmed_ = false; }
    // event 43, message 1
    void resetWeaponCycle() { cycleLoad_ = cycleArmed_ = false; }
    bool weaponCycleArmed() const { return cycleArmed_; }
    long weaponChanges() const { return changes_; }

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
    std::map<int, int> serial_;     // requests made, per actor
    std::map<int, int> actorArgs_;   // the same actors' a3
    std::vector<Event> log_;
    bool frozen_ = false;              // dword_4E9760
    bool playerOff_ = false;           // g_PlayerBehaviourOff
    long suspends_ = 0, resumes_ = 0;
    bool cycleArmed_ = false;          // dword_4E6C80
    bool cycleLoad_  = false;          // dword_4E6C84
    long changes_ = 0;                 // event 48s raised
};

}  // namespace omk
