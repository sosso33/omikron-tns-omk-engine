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

    // ---- THE GEOMETRY (`todo/shoot-mode.md` 5c, 7a) --------------------
    // `sub_422540` writes these out of the CHARACTER's own properties, read
    // through event 44 (`Actor_GetProperty`). They are the reach and the
    // field of view a designer authored, in metres and degrees, and they are
    // NOT in the weapon table - `tables/shoot_weapons.json`'s `f0` is the
    // fire rate and its `f1` has no reader at all.
    float rangeAcquire = 0.0f;               // +32   property 26, 39 * metres
    float rangeInner   = 0.0f;               // +28   property 27, 39 * metres
    float rangeThird   = 0.0f;               // +36   property 30, 39 * metres
    float coneCos      = 0.0f;               // +40   cos(property 29 degrees)
    float weaponTimer  = 0.0f;               // +172  reloaded with the row's f0
    float stepRemaining = 0.0f;              // +68   distance left on the edge
};

// The six properties `sub_422540` asks for, in the order it asks - each read
// gating the next, so a character that answers none gets none of the rest.
// Metres and degrees as authored; the record holds them converted.
struct ShootProperties {
    int health      = 0;    // property 1  - 0 is rewritten to 10
    int rangeAcquireM = 0;  // property 26 - metres
    int rangeInnerM   = 0;  // property 27 - metres
    int rangeThirdM   = 0;  // property 30 - metres
    int coneDegrees   = 0;  // property 29
    int behaviourBits = 0;  // property 37 - fanned out into `flags`
};

// `sub_422540` (0x00422540). The inch-per-metre factor is the engine's own
// 39 - the same one the pedestrian spawn and the projectile speed use.
void initShootRecord(ShootRecord& r, const ShootProperties& p);

// What `sub_420C70` leaves behind for `sub_420EB0` to read rather than
// recompute. The engine keeps them in four globals; naming them is the whole
// of the difference.
struct AcquireOut {
    float dist2d2 = 0.0f;   // flt_90E118  the SQUARED 2D distance
    float dist3d  = 0.0f;   // flt_90E108
    float dot     = 0.0f;   // flt_90E0F0  the full 3D forward dot
    float dotFlat = 0.0f;   // flt_90E114  its horizontal part
    float cross   = 0.0f;   // flt_90E0F4  the left/right sign
};

// `sub_420C70` (0x00420C70): is `targetPos` inside the shooter's cone AND
// within his range?
//
// `self` is `Actor_GetPosAndFacing`'s four floats - x, y, z, yaw in degrees -
// which is what identifies it as the SHOOTER: it is the end that has a
// facing. `Shoot_ActorEnter` fills it with `Actor_GetPosAndFacing(self)`.
//
// The engine forms `self - targetPos` and dots it against `(0,0,1)` rotated
// by the yaw. That reads backwards until you remember the heading convention:
// a character faces **-Z** at yaw 0 (`docs/FILE_FORMATS.md`), so pointing
// FROM the target TO the shooter and comparing against +Z is the same test as
// pointing from the shooter to the target and comparing against his real
// facing. Both signs cancel, and getting either one alone wrong gives a
// machine that shoots at whatever is behind it - which is what the first
// transcription of this did, caught by `shoot_range`'s own assertion rather
// than by reading it again.
//
// `sub_420D90` is the same test with the range DOUBLED, the arm used where
// property 37's bit 4 is set; `doubleRange` selects it.
bool shootAcquires(const ShootRecord& r, const float self[4], const float targetPos[3],
                   AcquireOut& out, bool doubleRange = false);

// `sub_420EB0` (0x00420EB0) - the TURN, and the only consumer of what
// `shootAcquires` leaves behind: the engine keeps those four in globals
// precisely so this can read them instead of recomputing the geometry.
//
// It works on a SIGNED SQUARE - `dotFlat * |dotFlat|` against the squared 2D
// distance - so the three thresholds are cosines without a square root:
//
//   > 0.99 * dist2d^2   already aimed (about 5.7 deg): do nothing
//   > 0.80 * dist2d^2   close (about 26.6 deg): creep by ONE frame delta
//   > 0.2  * dist3d     in front but wide: 5.0 per frame delta
//   otherwise           behind: 10.0 per frame delta, or a SNAP if allowed
//
// The snap returns `180` when the target is hard behind
// (`signed < -0.80 * dist2d^2`) and otherwise `+/-90`, for the caller to play
// a turn animation instead of rotating. Everything else returns 0.
//
// `eulerY` is the actor's `+420`, in degrees, and `dt` is `flt_4C30D8`, the
// engine's frame delta (1.0 at 30 fps) - so the rates are degrees per frame.
//
// THE SIGNS ARE READ FROM THE LISTING, not guessed: Hex-Rays loses three FPU
// compare flags here and renders them as undefined variables. Every one is a
// `fcomp` against `flt_4BC224`, which is **0.0**, on `flt_90E0F4` - the cross
// - and the branches say `cross < 0` ADDS while `cross >= 0` SUBTRACTS, with
// the snap returning `+90` and `-90` on the same split. The third threshold
// really does compare a squared quantity against an unsquared distance
// (`flt_90E108 * 0.2`); it is transcribed as written rather than "corrected",
// because it is what runs.
int shootTurnToward(float& eulerY, const AcquireOut& a, bool allowSnap, float dt);

// ---- THE GENERIC BRAIN, `sub_424DE0` (`todo/shoot-mode.md` 7c) ---------
//
// The machine is an outer switch on the state at record `+156` - 1..15 and
// 28 - where each arm computes an OUTCOME and every arm then funnels through
// one shared epilogue. That shape is the thing to hold on to: the states
// decide, the epilogue acts.
//
// The outcome (`v180` in the decompilation) is what the epilogue dispatches
// on. Two of its five values are read and three are not, and they are named
// that way rather than guessed at:
enum class ShootOutcome {
    FireIfReady = 0,   // fire when the weapon's countdown has expired
    Fire        = 1,   // the full arm: reload from the row, `Actor_TickProjectiles`
    Outcome2    = 2,   // NOT READ
    Outcome3    = 3,   // NOT READ
    Outcome4    = 4,   // NOT READ
    None        = -1,  // the arm set none
};

// What one tick of the brain needs from the world, and what it wants done.
// Passing them rather than reaching for globals is the same choice the
// channel port made with its input word.
struct ShootFrameIn {
    float self[4]   = {0, 0, 0, 0};   // x, y, z, yaw - `Actor_GetPosAndFacing`
    float target[4] = {0, 0, 0, 0};   // the target's, fetched once by the prologue
    float clipFrame = 0.0f;           // actor `+188`, the animation's frame
    float clipFrames = 0.0f;          // `Anim_Frames` of the running clip
    float dt = 1.0f;                  // `flt_4C30D8`
    bool  targetPredicate = false;    // `sub_426E00` - NOT READ, supplied
    int   targetPredicateBits = 0;    // its bitfield, for the arms that test it
    int   defaultClipType = 0;        // `a2`, the action the caller asked for

    // `sub_426C20` - NOT READ. States 1 and 4 branch on it: 1 means "take the
    // step", 0 means nothing, and 90 / 180 / -90 are the same turn-animation
    // requests `sub_420EB0` makes. Supplied rather than invented.
    int   moveCode = 0;
    int   myNode = -1, targetNode = -1;   // record `+188` on each side
    // the edge states 1 and 2 work along: `u32(rec, 4)`, its from/to points.
    bool  hasEdge = false;
    float edgeFrom[3] = {0, 0, 0};
    float edgeTo[3]   = {0, 0, 0};
    // the cell the step would land on, already read through `sub_4358D0`.
    // Its REFUSAL set is `{-128, 0, 2, 3}` - and note the -128: this is the
    // second site that reads the occupancy stamp as a SIGNED byte, which is
    // the finding `todo/omk-play.md` 96's neighbour records from `movsx`.
    int   stepCellValue = 1;
    bool  hasRoute = false;           // `u32(rec, 24)` - state 4 needs one
    bool  routeAdvanced = false;      // `sub_4356B0` found the next point
    float movedThisFrame = 0.0f;      // state 2: how far the body actually went
    // the hub's own two: actor `+164` / `+84` gate its firing arm (both
    // UNREAD, so they arrive as one bool), and `sub_421CD0` - also unread -
    // decides whether its middle arm does anything at all.
    bool  canFire = false;
    bool  holdStill = false;          // `sub_421CD0(actor, rec, 1)` was true
    int   scriptStep = 0;             // record `+144`, tested against 8
    // state 15's two: the grid line of sight (`sub_4359A0`, which the port
    // HAS - `Map2d::lineOfSight`) and whether the target is still alive.
    bool  gridLineOfSight = false;
    bool  targetAlive = true;
};

struct ShootStep {
    // THE DEFAULT IS 0, NOT "NONE": `sub_424DE0` opens with `v180 = 0.0`
    // before its switch, so an arm that sets nothing still leaves the
    // epilogue asking to fire-if-ready. `None` here means only one thing -
    // the outcome was recomputed by a function nobody has read.
    ShootOutcome outcome = ShootOutcome::FireIfReady;
    int  nextState = -1;        // -1 = stay
    int  clipType  = -1;        // a clip to pick, or -1
    float turnTotal = 0.0f;     // degrees the picked clip must cover
    float turnRate  = 0.0f;     // degrees per delta if there is NO clip
    bool  unread = false;       // this state's arm has not been transcribed

    // ---- what an arm asks the world to do -----------------------------
    // The brain decides and the caller acts, so the machine stays testable
    // without a live grid under it - the same split the renderer port uses.
    bool  takeStep = false;     // state 1 committed to the edge
    float headingDeg = 0.0f;    //   ...and this is the heading it set (`+420`)
    float stepLength = 0.0f;    //   ...and the distance it must cover (`+68`)
    bool  swapOccupancy = false;// restore the old cell, stamp the new one
    float climb = 0.0f;         // state 2's vertical step for this frame
    bool  arrived = false;      // state 2 ran `+68` out
    bool  releaseRoute = false; // state 4 let its route go
    // the arm reached a point where the engine reads an outcome out of a
    // function nobody has transcribed (`sub_4272B0`), so `outcome` is None
    // because it is UNKNOWN, not because the arm chose nothing.
    bool  outcomeFromUnread = false;
};

// One tick of the generic arm. `eulerY` is the actor's `+420` and is written.
// States whose arm has not been read set `unread` and change nothing - the
// port refuses to invent a branch, which is the rule this whole file follows.
ShootStep shootGenericStep(ShootRecord& r, const ShootFrameIn& in, float& eulerY);

// The states of `sub_424DE0` whose arm IS transcribed, so a check can assert
// the coverage rather than a comment claiming it.
const std::vector<int>& genericStatesRead();

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
