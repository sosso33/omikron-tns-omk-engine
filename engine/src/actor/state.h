// SPDX-License-Identifier: GPL-3.0-or-later
// `ACTOR_STATE` 0..17 - the actor runtime as a LIVE machine.
//
// Int slot 101 of the 1312-byte actor record (byte `+404`) is the state the
// whole character system dispatches on: `Actors_TickAll` (0x004681C0) runs one
// pass over the hundred slots of `g_Actors` and switches on it, and the state
// also decides whether the `.CTL` effect records run, whether the walker runs,
// whether the head look-at runs and whether the actor's marker is drawn. Slot
// 102 (`+408`) is where the previous state PARKS while a dialogue or an
// interface screen holds the body, and the pair is what makes the machine a
// machine rather than a mode flag.
//
// **THE STANDARD HERE IS DATA-CONSTRAINED, NOT ENGINE-VERIFIED**, and the
// reason is structural rather than a shortfall of effort. Every other runtime
// in this tree is checked against something the original produced: the world
// scripts against `traces/*.log`, the interface against `tools/sim`. This one
// has no such oracle and cannot be given one from the golden-trace rig, which
// sees only what a VM handler narrates through `Dbg_LogTagged`. Combat is two
// opcodes: `fight.begin` (62) announces NOTHING at all, and `player.become`
// (56) announces to CHARACTERS, one of the three domains the logger drops
// itself. `Fight_TickAI`, `Fight_ResolveHit`, the eighteen states below and
// the `.CTL` transition matching are native code on the far side of that line.
// `traces/fight.log` was captured on 2026-08-31 to test this and settled it:
// the capture reached combat - 32 of the scripts its events anchor carry
// `fight.begin` - and says nothing about any of it.
//
// So the warranty is what the shipped data can falsify, and it is asserted by
// `tools/run_actor_states.cpp` (`verify.py: engine actor states`):
//
//   * every ACTOR_STATE transition the machine makes is one the BINARY writes,
//     from the table below, which was enumerated out of `Runtime.exe.asm` -
//     every store to `+194h` plus the `dword_910834[328*i]` alias that
//     `Actor_SetState` and `Fight_Engage` use, and which a byte-offset search
//     alone misses;
//   * every `.CTL` state the channel lands in is a real entry, reached through
//     an edge the link pass resolved;
//   * every transition is opened by its own `+4` input code under
//     `Cef_InputMatches`, and under the priority gate the edge taken carries
//     the highest priority any allowed candidate had;
//   * every reaction a combat block names resolves in the low-16 id space;
//   * every input word injected is inside the fight AI's 0xCFF bit union.
//
// What is NOT claimed: that playing through this feels like the game. Nothing
// available here could establish that, and saying so is cheaper than being
// caught by it later.
#pragma once

#include "actor/channel.h"

#include <cstdint>
#include <string>
#include <vector>

namespace omk {

// The eighteen. The names are what the code demonstrably does; where nothing
// establishes a purpose the state keeps its number, per the naming rule.
enum class ActorState : int {
    Inert          = 0,   // nullsub_6 - no tick at all
    Normal         = 1,   // the ordinary actor
    Melee          = 2,   // the player and g_FightOpponentRec, and only those
    Shoot          = 3,
    ScxDriven      = 4,   // an SCX scene object owns the body (slot [43])
    ScxPending     = 5,   // resume the object a spoken line interrupted
    ChannelOnly6   = 6,
    SliderMount    = 7,   // Slider_TickRide; NOT a case in Actors_TickAll
    SliderRide     = 8,   // and MDSLIDOU dismounts from THIS one, not from 7
    UiHeld         = 9,   // an interface screen holds the body
    ImageScreen    = 10,  // a full-screen bitmap holds it
    Ladder11       = 11,  // Actor_ApplyMotion's ladder, and a scripted variant
    Scripted12     = 12,
    Scripted13     = 13,
    Swim           = 14,  // RSTNAGE - `nage` - and MDDIVEND write it
    Shoot15        = 15,
    Dialogue       = 16,
    DialogueFromUi = 17,  // entered from state 9; leaves to 1, not to [102]
};

inline constexpr int kActorStateCount = 18;

// What `Actors_TickAll` does with each state, read from the switch itself.
struct ActorStateInfo {
    int          id;
    const char*  name;
    const char*  tick;          // the per-state function, "" when none
    std::uint32_t tickAddr;
    bool channelTicks;          // does Cef_TickChannel run this frame
    bool walks;                 // does Actor_ApplyMotion run
    bool effects;               // do the .CTL effect records run (player and
                                // opponent only, and never in 4/5/9/10/16/17)
    bool scansZones;            // does Actor_ScanZones run
    bool marker;                // is the marker object at slot [20] drawn
    const char* note;
};

const ActorStateInfo& actorStateInfo(ActorState s);

// One edge of the ACTOR_STATE machine, and the function in the binary that
// writes it. `kAny` as `from` means the writer does not read the old state;
// `kParked` as `to` means it restores slot [102].
inline constexpr int kAny    = -1;
inline constexpr int kParked = -2;

struct ActorTransition {
    int from;
    int to;
    const char*   writer;
    std::uint32_t addr;
    const char*   why;
};

const std::vector<ActorTransition>& actorTransitions();

// `.CTL` group ids the engine asks for by number, from `Cef_FindGroupById`'s
// call sites. A bank that lacks one simply does not answer - the combat files
// carry stale authoring ids and are reached through the graph instead - so
// every install below has to tolerate a miss the way the engine does.
inline constexpr std::int32_t kGroupFalling   = 2;
inline constexpr std::int32_t kGroupGetUp     = 45;
inline constexpr std::int32_t kGroupLocomotion = 100;
inline constexpr std::int32_t kGroupShoot     = 200;
inline constexpr std::int32_t kGroupLadder    = 300;
inline constexpr std::int32_t kGroupSlider    = 61;   // sub_468FA0's riding pose
inline constexpr std::int32_t kGroupDialogue  = 400;

// One actor's live state, with its `.CTL` channel underneath.
class ActorRuntime {
public:
    ActorRuntime(const CtlFile& ctl, bool isPlayer)
        : channel_(ctl), isPlayer_(isPlayer) {}

    ActorState state()  const { return state_; }
    ActorState parked() const { return parked_; }
    CefChannel& channel() { return channel_; }
    const CefChannel& channel() const { return channel_; }

    // Every transition goes through here, and every one names the function in
    // the binary that writes the slot. A transition that is not in the table
    // is REFUSED and counted rather than silently applied - the point of the
    // table is that it is a closed set, and a closed set nothing enforces is
    // just a comment.
    bool setState(ActorState to, const char* writer);

    // --- the writers, in the binary's own vocabulary ---------------------
    void loadModel();                       // Actor_LoadModel        0x0041A730
    void playerSetActor();                  // Player_SetActor        0x00419E10
    bool fightEngage();                     // Fight_Engage           0x0041A3B0
    bool fightEnd(bool winner);             // sub_445AC0             0x00445AC0
    bool shootEnter();                      // Shoot_Enter            0x004222D0
    bool shootLeave(bool toNormal);         // Shoot_Leave            0x00422730
    bool scxStart();                        // ScriptObject_StartOnActor 0x0041BA80
    bool morphPlay();                       // Morph_Play             0x0041AFC0
    bool startPendingScx(bool pending);     // Actor_StartPendingScx  0x00466A60
    bool scxDrivenDone();                   // Actor_TickScxDriven    0x00466990
    bool uiLoadScreen();                    // UI_LoadScreen          0x00429BB0
    bool uiHeldRelease();                   // Actor_TickUiHeld       0x00466CC0
    bool imageScreenOpen();                 // sub_468F20             0x00468F20
    bool imageScreenClose();                // sub_466E70             0x00466E70
    bool enterDialogue();                   // Actor_EnterDialogueMode 0x00468DE0
    bool leaveDialogue();                   // Actor_LeaveDialogueMode 0x00468E80
    bool ladderEnter();                     // Actor_ApplyMotion      0x004672D0
    bool falling();                         // Walk_GroundResponse    0x00465460

    // Actors_TickAll's per-actor pass. Returns the name of the tick function
    // the switch dispatched to, "" for a state the switch does not name -
    // which is a real answer, not a gap: state 7 has no case, and the slider
    // drives that actor from `Sliders_Tick` instead.
    const char* tick(float dt, std::uint32_t input);

    struct Log { ActorState from, to; const char* writer; };
    const std::vector<Log>& log() const { return log_; }
    long refused() const { return refused_; }

private:
    bool installGroup(std::int32_t groupId);

    CefChannel channel_;
    bool       isPlayer_;
    ActorState state_  = ActorState::Inert;
    ActorState parked_ = ActorState::Inert;
    std::vector<Log> log_;
    long refused_ = 0;
};

}  // namespace omk
