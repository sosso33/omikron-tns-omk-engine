// SPDX-License-Identifier: GPL-3.0-or-later
// The SHOOT AI - the four callbacks `Shoot_ActorEnter` picks between.
//
// Shoot mode is the game's third-person gunfight: `Shoot_Enter` (0x004222D0)
// allocates 100 records of 192 bytes, sets `g_ShootMode`, puts the player in
// `ACTOR_STATE` 3 and installs `.CTL` group 200 with camera mode 4. Every
// other actor in the fight gets a record too, and one function pointer in it:
//
//     Shoot_ActorEnter (0x00422C10) raises event 44 for property 7 - the
//     character type - and switches on it:
//
//         type  7  X-Tech    nullsub_9    does nothing; an inert target
//         type 10  Gandhar   0x0047F6F0   a behaviour-SCRIPT machine
//         type 13  Astaroth  0x004800C0   a hand-written state machine
//         default            0x00424DE0   the generic shooter, 16 states
//
//     `Shoot_TickNpc` (0x004279C0) calls it once a frame, per actor.
//
// **The docs said this subsystem had "no data at all". That is true of the
// dispatch and false of the AI.** Gandhar plays three behaviour scripts
// compiled into the executable, through two twelve-entry handler tables, and
// the character types are a name table the binary carries - all of it now in
// `tables/shoot_ai.json`, all of it chaining end to end. The negative result
// was about the `switch` and got generalised to the subsystem, which is the
// shape `CLAUDE.md` §1 warns about: a negative is a fact about the hypothesis,
// never about the field.
//
// **What the shipped data says about the four arms**, which is what decides
// how much each one is worth:
//
//     317 `shoot.actor.enter` sites, 306 resolving in their own chunk
//         302  ->  the generic shooter
//           3  ->  Astaroth
//           1  ->  Gandhar
//           0  ->  X-Tech
//
// and **no shipped character record carries type 7 at all** - 1032 records,
// zero X-Techs. `nullsub_9` is unreachable content, like the six spell recipes
// whose gate is never 8. The generic shooter is 99% of the subsystem.
//
// **THE STANDARD, and it is lower than the actor state machine's.** That row
// was data-constrained: the `.CTL` corpus could falsify a wrong reading. Here
// only the tables and the dispatch touch shipped data; the three machines are
// CODE, and two of them are hand-written geometry with no data behind them at
// all. So:
//
//   * **Gandhar is ported exactly** - he is table-driven, and the tables are
//     lifted and self-checking. Running him is running the shipped script;
//   * **X-Tech is ported exactly** - it does nothing;
//   * **Astaroth is ported as his state graph** with the constants his own
//     code carries (the 195 / 273 / 156 / 78 unit distances, the 3700 / 2300 /
//     1200 impulses, the 1.0 / 1.5 / 2.0 speed and 60 / 40 / 30 degree turn
//     bands). The per-state geometry calls out to helpers this tree has no
//     equivalent for and they are named, not reimplemented;
//   * **the generic shooter is ported as its state graph** - 16 states and the
//     five-way sub-switch inside state 6 - and NOT as its 1500 lines of
//     per-state geometry. What it decides is here; how it aims is not.
//
// That is `RECONSTRUCTION.md` §3's "read and explained" rather than "verified",
// and it is written here rather than left to be discovered.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace omk {

// The 14 character types, from the binary's own name table at 0x004CFA30.
// Property 7, `Type Spectre`, at character record `+176`.
enum class CharType : int {
    NoOne = 0, ManPasser = 1, WomanPasser = 2, ManEnemy = 3, WomanEnemy = 4,
    Mecagarde = 5, Mecadog = 6, XTech = 7, ZTech = 8, Incarnable = 9,
    Gandhar = 10, Zombie = 11, Spectre = 12, Astaroth = 13,
};
inline constexpr int kCharTypeCount = 14;
const char* charTypeName(int type);          // "" outside 0..13

// Which callback `Shoot_ActorEnter` installs. `Unset` is not an arm of the
// switch - it is what 330 of the 1032 shipped records carry at `+176` (-1),
// and it reaches `Generic` through `default:`, which is why that arm is
// load-bearing rather than defensive.
enum class ShootBrain { Inert, Gandhar, Astaroth, Generic };
ShootBrain shootBrainFor(std::uint32_t characterType);
const char* shootBrainName(ShootBrain b);

// One entry of a behaviour script: play `action` for `repeats` ticks.
struct ShootScriptStep { int action = 0; int repeats = 1; bool rewind = false; };

// One of Gandhar's twelve actions.
struct ShootAction {
    int  code = 0;              // 16..27, what lands in record +156
    int  row  = 0;              // its slot in the two handler tables
    int  clipType = -1;         // the animation TYPE it asks for, -1 = none
    bool setsFlag = false;      // channel flag 0x800 set (true) or cleared
    const char* note = "";
};

// The health bands. `sub_47FB40`'s caller re-reads them EVERY frame, and a
// change of band resets both the script index and the repeat counter - so a
// wounded Gandhar restarts his routine rather than resuming it.
inline constexpr int kWoundedAt  = 100;   // hp <= 100
inline constexpr int kCriticalAt = 50;    // hp <= 50

// The 192-byte shoot record, only the fields something below reads. Offsets
// are kept in the comments because every cross-reference in `readable/` is by
// offset, and several of these were established from more than one caller.
struct ShootRecord {
    ShootBrain brain = ShootBrain::Generic;  // +0    the callback pointer
    int   clipList   = 0;                    // +20   sub_434530(type)
    float groundY    = 0.0f;                 // +60   node y minus the height
    float height     = 0.0f;                 // +64   max of the model extents
    std::uint32_t type = 0;                  // +80   the character type
    int   health     = 0;                    // +92   Hud_DrawBar's value
    int   target     = -1;                   // +96   the actor being fought
    int   repeats    = 0;                    // +100  ticks done in this step
    int   destX = 0, destZ = 0;              // +136/+140  Shoot_Think's spot
    int   scriptStep = 0;                    // +144  index into the script
    int   state      = 0;                    // +156  the action / state code
    std::uint32_t flags = 0;                 // +160  bit 0 ticking, 8, 0x800
    float clipLen    = 0.0f;                 // +164  sub_434890(list)
    float timer      = 0.0f;                 // +168  a countdown in FRAMES
    int   node       = -1;                   // +188  Shoot_Think's nav node
    int   band       = 0;                    // +190  0 none, 1 wounded, 2 crit
};

// The behaviour-script walk, `sub_47FB40` (0x0047FB40). Returns the action to
// play and advances the record; a `{0, n}` entry rewinds to the start.
int shootScriptAdvance(ShootRecord& r, const std::vector<ShootScriptStep>& s);

// One actor's shoot AI. `tick` is `Shoot_TickNpc`'s call into the callback.
class ShootAi {
public:
    // `tables/shoot_ai.json`, already parsed. Gandhar needs all three scripts;
    // the other three arms need none.
    struct Tables {
        std::vector<ShootScriptStep> healthy, wounded, critical;
        std::vector<ShootAction> actions;   // 12, indexed by code - see byCode
        const ShootAction* byCode(int code) const;
    };

    ShootAi(const Tables& t, std::uint32_t characterType);

    // One frame. `dt` is in FRAMES, the engine's own delta. Returns the name
    // of the arm that ran, so a caller can see the dispatch happen.
    const char* tick(float dt);

    // `dword_657A28`, the pulse that says the current action has finished.
    // It is set OUTSIDE the callback - the animation reaching its end - and
    // Gandhar's arm consumes it to step his script. It is a parameter here
    // for the same reason the channel takes its input word as one: modelling
    // the machine means modelling what it does with the signal, and inventing
    // a duration for an action whose real length comes from a clip this tree
    // cannot play would be putting a guess where a fact belongs.
    void signalActionComplete() { actionDone_ = true; }

    ShootRecord& rec() { return rec_; }
    const ShootRecord& rec() const { return rec_; }

    // What the machine decided this tick, in place of the geometry this tree
    // cannot run - the same way the renderer port records draw decisions.
    struct Decision {
        enum class Kind { Action, StateChange, Fire, Died, Wait, Idle };
        Kind kind = Kind::Idle;
        int  from = 0, to = 0;
        int  clipType = -1;
        float amount = 0.0f;      // a wait in frames, or an impulse
        const char* why = "";
    };
    const std::vector<Decision>& log() const { return log_; }
    void clearLog() { log_.clear(); }

private:
    void tickGandhar(float dt);
    void tickAstaroth(float dt);
    void tickGeneric(float dt);
    const std::vector<ShootScriptStep>& scriptForHealth();

    const Tables* t_;
    ShootRecord rec_;
    bool actionDone_ = false;
    std::vector<Decision> log_;
};

// The two hand-written machines' state sets, as read. They are exported so
// the sweep can assert the runtime never leaves them - the three arms use
// OVERLAPPING numbers in `+156` that mean different things, which is a real
// trap: 16..21 is Gandhar's action range AND Astaroth's state range.
const std::vector<int>& astarothStates();       // 16..21, 27, 29
const std::vector<int>& genericStates();        // 1..15, 28
// Astaroth's transitions, `from -> to`, and the generic shooter's.
struct ShootEdge { int from, to; const char* why; };
const std::vector<ShootEdge>& astarothEdges();
const std::vector<ShootEdge>& genericEdges();

}  // namespace omk
