// SPDX-License-Identifier: GPL-3.0-or-later
// MELEE - the two combat contexts and what the engine does with them each
// frame. `todo/fight-mode.md` is the plan and the reading; this is step 1.
//
// A fight is entered by VM opcode 62 `fight.begin`, which switches both
// parties to `.CTL` slot 2 (the `*CMBT` files), calls `Fight_Engage`
// (0x0041A3B0) and parks the script at status 3. `Fight_Engage` writes
// ACTOR_STATE 2 to both fighters - it is the only writer of that state in the
// game - and calls `Fight_Begin` (0x004455B0) and `Fight_SelectAiProfile`
// (0x004652B0). From then until a knock-out, each frame runs
//
//     Actor_TickPlayerAndOpponent  0x00466710   per fighter: pre, channel, post
//       sub_4451F0                 0x004451F0   pre:  life out, replay camera
//       Cef_TickChannel                         the `.CTL` machine (channel.h)
//       sub_4452A0                 0x004452A0   post: entry bookkeeping, the KO
//     Fight_TickAI                 0x00464830   the opponent only: press buttons
//     Fight_ResolveBoth            0x0049B1F0   -> Fight_ResolveHit both ways
//     Fight_FaceOpponent           0x0049A550   x2
//     Fight_RecordFrame            0x0049B220   x2, the 60-frame replay ring
//     Fight_UpdateHealthBars       0x00445160   the two gauges
//
// **THE STANDARD IS DATA-CONSTRAINED, NOT ENGINE-VERIFIED** (`docs/PORTING.md`
// B1), and unlike every other runtime here that is a property of the subject
// rather than of the effort. The golden-trace logger only sees what a VM
// handler narrates through `Dbg_LogTagged`, and combat is two opcodes:
// `fight.begin` announces NOTHING and `player.become` announces to CHARACTERS,
// which the logger filters out itself. `traces/fight.log` was captured on
// 2026-08-31 to give this an oracle and settled it the other way round - the
// capture reached combat, 32 of its anchored scripts carry `fight.begin`, and
// it is silent about all of it. So what holds this honest is what the shipped
// data can falsify, and `tools/run_fight.cpp` asserts exactly that:
//
//   * damage is only ever a combat block's own `damage`, scaled by the two
//     stat multipliers and floored at 1 - never an invented number;
//   * every reaction a hit selects is one of the six entries `Fight_Begin`
//     cached by role code, or an entry the block names by low-16 id;
//   * hit points fall monotonically and are clamped at 0, and the pair that
//     reaches 0 lands in the winner/loser slots exactly once;
//   * every input word the AI injects is inside the profiles' own 0xCFF union
//     (`formats/ctl.h`), so the AI and a player press the same buttons;
//   * the fighters are never left closer than the separation radius after the
//     push, which is `Fight_Begin`'s own `max` of the two model radii.
//
// What is deliberately NOT here, so nobody looks for it: the camera (mode 14,
// `Fight_TickCamera`, step 4), the HUD gauges (step 5), and the bodies. This
// module owns the DECISIONS - who is hit, for how much, which reaction, who
// wins - and asks its caller for the world through `FightBody` below, the way
// `channel.h` records animation decisions rather than posing a skeleton.
#pragma once

#include "actor/channel.h"
#include "formats/ctl.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace omk {

// The world a fight needs from its caller: where a fighter stands, which way
// he faces, and the channel driving him. The engine reads these off the actor
// record (+244/+248/+252 the position, +416..+424 the Euler, +396 the channel
// index); a probe supplies them directly and `omk-play` will supply the real
// actor in step 2.
struct FightBody {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float yaw = 0.0f;                 // actor +420, DEGREES, as the engine keeps it
    // The model's bounding radius at node +88, which `Fight_Begin` turns into
    // the separation distance by taking the larger of the two.
    float radius = 0.0f;
    CefChannel* channel = nullptr;
    bool isPlayer = false;            // the player takes DOUBLE damage
    // TRUE when the CALLER ticks this body's channel instead of `Fight`.
    //
    // `ACTOR_STATE` 2's own row in `actor/state.cpp` has `channelTicks` AND
    // `walks` true: melee runs `Cef_TickChannel` and then `Actor_ApplyMotion`,
    // the same pair an ordinary actor runs, so a fighter's clips carry him
    // about exactly as they do outside a fight. A frontend that already owns
    // that pair for this body - `PlayerController::tick` is both halves -
    // must run it rather than have the channel ticked here with no motion
    // pass, which would leave a fighter rooted to the spot while his clips
    // played. `Fight` then calls `bodyTick` below in the place the channel
    // tick occupies, so the order pre / channel / post is still the engine's.
    bool externallyTicked = false;
};

// `Fight_Begin`'s stat wiring, read from its six `Game_RaiseEvent(44, …)`
// calls: property 1 *Vie* is the hit points and the gauge's maximum, 16 *Carac
// Attack* scales the damage dealt, 18 *Carac Dodge* the damage taken, and 19
// *Carac Fight Experience* scales the CHANNEL RATE - an experienced fighter
// literally animates faster (`sub_45ACD0(chan, exp * 0.024390243)`).
struct FightStats {
    int vie = 0;            // property 1
    int attack = 0;         // property 16
    int dodge = 0;          // property 18
    int experience = 0;     // property 19
};

// One fighter's combat context - the 140-byte block at `dword_906F60` (the
// player) and `dword_907000` (the opponent). The engine's offsets are kept in
// the comments because every cross-reference in `readable/` is by offset.
struct FightContext {
    FightBody* body = nullptr;        // +0   the actor record
    int  prevEntry = -1;              // +8   the entry before this one
    int  lastFrameEntry = -1;         // +16  actor +192, last tick's frame
    int  entry = -1;                  // +20  the entry now playing
    int  scoredEntry = -1;            // +24  the entry that has already landed
                                      //      a hit - each move scores once
    float frameBefore = 0.0f;         // +28  actor +192, the clip frame before
    float frameAfter  = 0.0f;         // +32  actor +188, and after

    // +36..+60: the six reaction entries `Fight_Begin` caches by ROLE code
    // through `Cef_FindEntryByCodeGlobal`. Every combat file carries exactly
    // one entry of each and the adventure files none, which is what makes the
    // role reading a property of the content rather than of the parse.
    int hitHigh = -1;                 // +36  role 3  - the high guard/flinch
    int hitLow  = -1;                 // +40  role 4  - the low one
    int koEntry = -1;                 // +44  role 5  - the knock-out
    int crouched = -1;                // +48  role 9  - I_GROUND, knocked down
    int farEntry = -1;                // +56  role 18 - swapped in past 3 m
    int nearEntry = -1;               // +60  role 20 - and inside 1.5 m

    int prevState = 0;                // +64  last tick's state id
    int state = 0;                    // +68  the entry's own `+12` low byte
    int stateB = 0;                   // +72  BYTE1 of the same word
    float stateF = 0.0f;              // +76  BYTE2, as a float
    int stateD = 0;                   // +80  HIBYTE - 8 lifts the 0x200 skip
    std::uint32_t flags = 0;          // +84  status: 1 grounded, 2 facing
                                      //      override, 0x80 untouchable,
                                      //      0x100/0x200 AI latches, 0x40
    const CtlAiProfile* ai = nullptr;  // +88  the profile, by difficulty level
    int   aiIntent = 10;              // +92  what the AI last decided to do
    long  aiMoveStart = 0;            // +100 ms
    long  aiMoveDeadline = 0;         // +104 and when the wait is over
    long  aiTauntStart = 0;           // +108 the slot-7 pair, states 6 and 21
    long  aiTauntDeadline = 0;        // +112
    float carriedTimer = 0.0f;        // +116 copied across on flag 0x40
    int   carriedFlag = 0;            // +120
    // `sub_49A830`'s answer: the combat block's flag bits, which choose which
    // of the cached reaction entries may interrupt the current move. Derived
    // and carried; nothing feeds it back into the channel yet - see the note
    // at the top of fight.cpp.
    std::uint32_t allowedMask = 0;
    int   hp = 0;                     // +124 hit points, u16
    int   lastDamage = 0;             // +126 what the last hit cost
    int   knockdown = 0;              // +128 latched by a 0x10000000 reaction
    float attackMul = 0.0f;           // +132 property 16 x 0.005
    float dodgeMul = 0.0f;            // +136 property 18 x 0.005 x 0.25, plus
                                      //      the difficulty bonus for the player
};

// THE AI's BUILT-IN MOVES - eight count+pointer pairs compiled into the
// executable at 0x004CAD0C..0x004CADA0, which `Fight_TickAI` presses directly
// rather than taking from a `.CTL` profile. A replica cannot read them out of
// `gamedata/`, so they are lifted to `tables/fight_ai_moves.json` like every
// other compiled-in table and handed to `Fight` by its caller.
//
// They keep their ADDRESS names, per the repo's naming rule: what each is FOR
// is established only by the branch that presses it, and that is recorded in
// `fight.cpp` where the branch is.
//
//     0x004CAD0C  5 words  [2, 2, 2, 2, 2]              pressed while closing
//     0x004CAD30  1 word   [0x40000000]                 the idle word alone -
//                          the direction comes from the OR modifier
//     0x004CAD38  5 words  [8, 8, 8, 8, 8]
//     0x004CAD54  5 words  [4, 4, 4, 4, 4]
//     0x004CAD6C  2 words  [0x48, 0x40000000]           the four two-word
//     0x004CAD78  2 words  [0x88, 0x40000000]           combos, each a press
//     0x004CAD88  2 words  [0x40, 0x40000000]           and a release
//     0x004CAD98  2 words  [0x80, 0x40000000]
//
// Every word is inside the profiles' 0xCFF union or is the idle sentinel.
struct FightAiTables {
    std::vector<std::uint32_t> m4CAD0C, m4CAD30, m4CAD38, m4CAD54;
    std::vector<std::uint32_t> m4CAD6C, m4CAD78, m4CAD88, m4CAD98;
    // The shipped values, so a probe or the viewer can run without the JSON
    // while `verify.py` asserts the lift against the executable itself.
    static FightAiTables shipped();
};

// CAMERA MODE 14, `Fight_TickCamera` (0x00446500) and its six helpers.
//
// The preset table says this camera is COMPUTED, not authored: row 14 is all
// zeros with `fov` 0 and both subjects 8, so there is no offset to resolve -
// the function places the eye and the target itself every frame, around the
// MIDPOINT of the two fighters.
//
// The state machine (`dword_906F50`), chosen each tick:
//
//   1  the orbit          `sub_446000`, the ordinary fight view
//   2  the THROW swing    `sub_446240`, while either fighter is in state 11
//   3  a steady orbit     `sub_445D30`, 3.5 degrees a frame
//   4  a TRANSITION       `sub_445E30`, ten frames lerping 2 -> 1
//   7  the KO             `sub_445C20`, placed once per replay pass
//   8  close              `sub_446000` with the eye ease at 0.01 and the
//                         camera recomputed every third frame
//
// and the shared tail eases the eye 9.8425198 (0.25 m) a frame toward the
// target, then clamps its height above the midpoint.
//
// **Options row 18 `Caméra de combat` is what `byte_906F20` holds** - "Vue de
// dos" (0) against "Vue de côté" (1), the save header's `+42` - and the two
// really are two rigs: at 0 the heading offset is forced to -70 degrees, the
// extra height term is dropped and 59.055119 (1.5 m) comes off the eye, and
// the tail clamps the eye 1.5 m above the midpoint instead of 2.5 m.
struct FightCamera {
    float eye[3] = {0, 0, 0};
    float at[3]  = {0, 0, 0};
    int   state = 1;              // dword_906F50
    bool  placed = false;         // byte_530C80, the once-per-state latch
    float heading = 0.0f;         // flt_530C20, the orbit's angle in degrees
    float radius = 0.0f;          // dword_530C1C
    float height = 0.0f;          // dword_530C2C
    float swing = 0.0f;           // flt_530C84, the throw's degrees a frame
    float shake = 0.0f;           // flt_530C28 / flt_530C78, the hit shake
    float shakePhase = 0.0f;      // dword_530C68
    float shakeFrom[2] = {0, 0};  // dword_530C88 / dword_530C8C
    // the ten-frame transition (`sub_445E30`): where it started, the per-frame
    // deltas it walks, its length in frames and its clock
    float fromEye[3] = {0, 0, 0}, fromAt[3] = {0, 0, 0};
    float stepEye[3] = {0, 0, 0}, stepAt[3] = {0, 0, 0};
    float travel = 10.0f, clock = 0.0f;
    long  frames = 0;             // dword_906F24, the divider's counter
    int   divider = 1;            // dword_906F28
    // the tail's collision solve (`sub_416570`): whether this frame's ray hit,
    // and how many frames have hit since the fight began - instruments only
    bool  rayHit = false;
    long  rayHits = 0;
};

// One decision, recorded rather than drawn - the same idea as ChannelEvent.
struct FightEvent {
    enum class Kind {
        Hit,          // a landed blow: damage, reaction, who
        Block,        // the defender's guard took it (chip damage)
        Graze,        // a miss by stance that still costs 1 (2 for the player)
        Throw,        // attacker state 10 caught a standing target
        Knockdown,    // the reaction carried 0x10000000
        Ko,           // hit points reached 0
        Separate,     // the pair was pushed back to the radius
        AiMove,       // the AI injected a move: which slot, how many words
    };
    Kind kind = Kind::Hit;
    bool onPlayer = false;       // the SUFFERER for a hit, the actor for a move
    int  damage = 0;
    int  reaction = -1;          // the entry the victim was thrown into
    int  fromEntry = -1;         // the attacker's move
    int  slot = -1;              // AiMove: which of the twelve situation slots
    int  words = 0;              // AiMove: how many input words were pressed
};

struct FightStatsCounters {
    long frames = 0;
    long hits = 0, blocks = 0, grazes = 0, throws = 0;
    long aiGuards = 0;   // `Fight_TickAI`'s defence raised the 0x40 guard
    // THE KNOCKDOWN ARM TAKEN WITH A REACTION THAT IS NOT ONE. A killing blow
    // chooses between `crouched`, `reaction` and `koEntry` on the `+128`
    // latch, and the latch is set only by a reaction whose `+12` carries
    // `0x10000000`. So the arm and the reaction must agree; they stopped
    // agreeing when the latch was never cleared, and the loser was sent into
    // an ordinary flinch that never reaches role state 6 or 7 - the fight then
    // runs for ever with a fighter on 0 hit points. MUST BE 0.
    long knockdownArmWithoutBit = 0;
    long knockdowns = 0, kos = 0;
    long aiMoves = 0, aiWords = 0, aiWordsOutsideUnion = 0;
    long damageDealt = 0;
    long damageNotFromBlock = 0;    // must stay 0: every point traced to a block
    long reactionUnresolved = 0;    // must stay 0
    long hpWentUp = 0;              // must stay 0
    long separations = 0, tooCloseAfterPush = 0;   // the second must stay 0
    long replayFrames = 0, replayPasses = 0;
};

// The fight itself: the two contexts and the globals around them
// (`flt_906F2C` the radius, `dword_906F30`/`34` loser and winner,
// `dword_906F40` the KO counter, `flt_906F44` the separation, `dword_9070AC`
// the two replay passes).
class Fight {
public:
    // `rand` is injected so a probe is reproducible; the engine calls `rand()`
    // and `Sys_GetTimeMs()`, and both enter the AI's decisions.
    using RandFn = std::function<int()>;
    using TimeFn = std::function<long()>;

    Fight(RandFn rand, TimeFn now, FightAiTables builtin = FightAiTables::shipped())
        : rand_(std::move(rand)), now_(std::move(now)),
          builtin_(std::move(builtin)) {}

    // Fight_Begin (0x004455B0) + Fight_SelectAiProfile (0x004652B0). `level`
    // is op 62's THIRD field - `VARIABLES[175] 'Niveau Combat'`, 0..2 - and
    // selects the profile whose id is level + 1. `difficulty` is options row
    // 16 `word_90E1A6`, which adds a flat 0.5 / 0.25 / 0 to the PLAYER's dodge
    // multiplier and nothing else.
    // `combatCamera` is options row 18 (`byte_90E1AA` -> `byte_906F20`):
    // 0 "Vue de dos", 1 "Vue de côté". It changes the camera's rig, not a
    // detail of it - see `FightCamera`.
    bool begin(FightBody& player, const FightStats& playerStats,
               FightBody& opponent, const FightStats& opponentStats,
               int level, int difficulty, int combatCamera = 1);

    // The fight camera as of this frame - `Fight_TickCamera`'s own eye and
    // target. Stepped inside `step()`, because the engine ticks it from the
    // camera pass with the two combat contexts in hand.
    const FightCamera& camera() const { return cam_; }

    // One frame, in the engine's own order. Returns false once the fight is
    // over - `dword_906F40` past `dword_9070AC`, which is `sub_445AC0`.
    bool step(float dt, std::uint32_t playerInput);

    // What to run in place of the channel tick for an `externallyTicked`
    // body - `PlayerController::tick(dt, word)`, which is `Cef_TickChannel`
    // followed by `Actor_ApplyMotion`.
    using BodyTick = std::function<void(float dtFrames, std::uint32_t input)>;
    void setBodyTick(BodyTick t) { bodyTick_ = std::move(t); }
    // THE CAMERA'S WORLD RAY, `sub_444810` - the segment a..b against the
    // linked set, true and the hit point when it meets something. The fight
    // camera's tail casts it from the look-at point to the wanted eye
    // (`sub_416570`). None installed: the eye is never pulled in.
    using CameraRay = std::function<bool(const float a[3], const float b[3], float hit[3])>;
    void setCameraRay(CameraRay r) { cameraRay_ = std::move(r); }

    bool over() const { return over_; }
    // Which side won, once `over()`: true when the OPPONENT was the loser.
    bool playerWon() const { return playerWon_; }

    const FightContext& player()   const { return a_; }
    const FightContext& opponent() const { return b_; }
    float separation() const { return separation_; }   // flt_906F44
    float radius() const { return radius_; }           // flt_906F2C
    int   koCounter() const { return koCounter_; }     // dword_906F40

    const std::vector<FightEvent>& events() const { return events_; }
    void clearEvents() { events_.clear(); }
    const FightStatsCounters& stats() const { return stats_; }

    // Exposed for the probe, which re-derives them from the file rather than
    // taking this class's word for it.
    static bool windowOpen(const CtlCombat& blk, float before, float after);

private:
    void tickFighter(FightContext& c, float dt, std::uint32_t input, bool isAi);
    void preStep(FightContext& c);        // sub_4451F0
    void postStep(FightContext& c);       // sub_4452A0
    void resolveHit(FightContext& att, FightContext& def);   // 0x0049A960
    void faceOpponent(FightContext& c, const FightContext& other, bool ease);
    void keepSeparation(FightContext& c, FightContext& other);
    void tickAi(FightContext& c, FightContext& other);       // 0x00464830
    void injectMove(FightContext& c, const CtlAiSlot& slot, int slotIndex);
    // `Perso_InjectInput` with one of the built-in tables above, which is what
    // every branch of `Fight_TickAI` outside the profile families presses.
    // `modifier` < 0 uses the AI's OR modifier (`dword_53AE14`); a call site that
    // passes a literal to `Perso_InjectInput` passes it here
    void injectWords(FightContext& c, const std::vector<std::uint32_t>& words,
                     int modifier = -1);
    // sub_465160 (slots 4/5/6) and sub_465210 (slots 1/2/3) are one function
    // with a different base: roll once, walk the three cumulative weights.
    void pickFromFamily(FightContext& c, int base);
    // `Fight_TickCamera` (0x00446500) and the helpers each arm calls.
    void tickCamera(float dt);
    void camOrbit(bool ease, float angleOff, float height, float atLift);  // sub_446000
    void camThrow(float height);                                           // sub_446240
    void camSteady(float degPerFrame, float eyeUp, float atUp);            // sub_445D30
    bool camTransition(float dt);                                          // sub_445E30
    void camPlace(float radius, float headingDeg, float height,            // sub_445C20
                  float atLift, bool moveTarget);
    void camShake(float dt, FightContext& c);                        // sub_4463C0
    void camMidpoint(float out[3]) const;
    void recordFrame();                   // 0x0049B220
    bool replayStep();                    // sub_49B2E0, the KO replay
    void measureSeparation();             // sub_49A4F0
    int  entryByRole(const FightContext& c, int role) const;
    int  entryByLow16(const FightContext& c, int id) const;
    // `scaled` is the difference between the two damage paths in
    // `Fight_ResolveHit`: a landed blow is the block's damage through both
    // stat multipliers, while a BLOCK and a GRAZE cost a flat 1 - doubled for
    // the player like everything else, but never scaled.
    void applyDamage(FightContext& def, const FightContext& att,
                     double base, bool scaled, FightEvent::Kind kind,
                     int reaction);

    RandFn rand_;
    TimeFn now_;
    BodyTick bodyTick_;
    CameraRay cameraRay_;
    FightAiTables builtin_;
    FightContext a_, b_;          // dword_906F60 / dword_907000
    float radius_ = 0.0f;         // flt_906F2C
    float separation_ = 0.0f;     // flt_906F44
    int   koCounter_ = 0;         // dword_906F40
    int   replayPasses_ = 2;      // dword_9070AC, set to 2 by Fight_Begin
    bool  over_ = false, playerWon_ = false;
    bool  decided_ = false;       // dword_906F30 != 0: a loser is recorded
    FightContext* loser_ = nullptr;   // dword_906F30 itself
    // `OMK_KOTRACE=1`: 90 frames of the loser's channel after the killing
    // blow - the entry, its clip owner and the state the fight reads from it.
    // A fight that does not end is a loser who never reaches state 6 or 7.
    int   koTraceLeft_ = 0;
    const FightContext* koTraceWho_ = nullptr;
    FightCamera cam_;
    int   combatCamera_ = 1;      // byte_906F20, options row 18
    int   camPrevState_ = 1;      // the state the last tick ran, for 2 -> 4
    // `dword_530C24`: which REPLAY PASS the KO camera was last placed for.
    // The +-45 / +-135 nudges fire once when this changes, not every tick.
    int   camPass_ = 0;
    // `dword_53AE14`: the modifier the AI ORs into every word it injects
    // while an approach (2) or a move family (8) is running.
    std::uint32_t aiOr_ = 0;

    // The replay ring: sixty frames of both bodies, slid down when full
    // (`qmemcpy(&unk_6A17E0, &unk_6A1828, 0x1098)` drops the oldest).
    struct ReplayFrame {
        float x[2] = {}, y[2] = {}, z[2] = {};
        float yaw[2] = {};
        int   entry[2] = {-1, -1};
        float frame[2] = {};
        float dt = 0.0f;
    };
    std::vector<ReplayFrame> ring_;
    std::size_t replayCursor_ = 0;         // dword_6A17D0

    std::vector<FightEvent> events_;
    FightStatsCounters stats_;
};

}  // namespace omk
