// SPDX-License-Identifier: GPL-3.0-or-later
// The perso CHANNEL - one actor's live `.CTL` state machine.
//
// A `.CTL` is not a clip library with a graph bolted on: it is a machine, and
// an entry is both a state and the edge into itself. `formats/ctl.cpp` reads
// the machine; this runs it. The three functions transcribed here are
//
//     Cef_TickChannel   0x004A8160   one frame: consume input, take an edge,
//                                    advance, and fall through the GoTo when
//                                    the clip ends
//     Cef_FindTransition 0x004A8BD0  which edge, if any, the held input opens
//     GoToMove          0x004A7B80   commit a transition: the entry flags of
//                                    the state being LEFT and the one being
//                                    ENTERED, the alias and junction chains,
//                                    the blend or the cut
//
// **THE STANDARD THIS IS HELD TO IS DATA-CONSTRAINED, NOT ENGINE-VERIFIED.**
// Every other runtime in this tree has an oracle - the golden traces for the
// scripts, `tools/sim` for the UI - and this one cannot have either. The
// logger only ever sees what a VM handler narrates through `Dbg_LogTagged`,
// and the whole of combat is two opcodes: `fight.begin` (62) announces
// NOTHING, and `player.become` (56) announces to CHARACTERS, one of the three
// domains the logger filters out itself. `Fight_TickAI`, `Fight_ResolveHit`,
// the eighteen `ACTOR_STATE`s and every line below are native code that never
// touches the tag path. `traces/fight.log` was captured to prove otherwise and
// proved the opposite: the capture DID reach combat - 32 of the scripts its
// events anchor carry `fight.begin` - and the trace is silent about all of it.
//
// So what holds this port honest is what the SHIPPED DATA can falsify, and
// the sweep in `tools/run_actor_states.cpp` asserts exactly that:
//
//   * every state the machine lands in is a real entry of the file, reached
//     through an edge the link pass resolved - never a null, never a state of
//     another group where the engine resolves within one;
//   * every transition taken was opened by the candidate's own `+4` input
//     code under `Cef_InputMatches`, and no gated candidate of higher
//     priority was passed over;
//   * every reaction a combat block names resolves in the low-16 id space,
//     which is collision-free in all seven files;
//   * every input word the machine ever consumes is inside the fight AI's own
//     bit union, 0xCFF, or the 0x40000000 idle sentinel;
//   * and, since 2026-09-02, the two QUEUE rules of the commit below, which
//     that sweep could not see because it injects whole queues: over 53 banks
//     seeded the way `SetPersoBankGroup` seeds them - one word, the idle one -
//     a press is acted on THIS tick in 53 and parked behind the idle word in
//     0, and on the 9 reachable entries carrying flag 0x20000000 the queue is
//     cut to one rather than pushed. Remove the first rule and all 53 park;
//     the second fires 4088 times in the main sweep and changes not one of its
//     numbers, so it is asserted only where it can be seen.
//
// None of those can be satisfied by a wrong offset that happens to decode, and
// three of them are properties of the content rather than of the format. That
// is the whole of the warranty. Behaviour beyond it - that the game FEELS like
// this - is not claimed and cannot be, from here.
//
// What is deliberately absent, so nobody looks for it: the animation itself.
// `Actor_PlayClip` / `Actor_BlendToClip` / `sub_45C680` pose a skeleton, and
// this tree has no skeleton to pose. Everywhere the engine calls one, the
// channel records the DECISION - which clip, from which frame, blended over
// how many - the way the renderer port records draw decisions rather than
// pixels. The engine takes the same branch itself whenever the channel's
// no-playback flag 0x200 is set, which is how a dialogue-held actor already
// runs the machine with nothing playing.
#pragma once

#include "formats/ctl.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace omk {

// `Perso_SetInputEnabled` resets the queue to exactly this word, and
// `Fight_TickAI` interleaves it between presses so the machine sees a release.
// It is NOT the 0x80000000 "no input" EDGE code: `Cef_InputMatches` ends
// `if (a1 == 0x80000000) return a2 <= 0x2000`, so the idle word matches no
// idle edge.
inline constexpr std::uint32_t kIdleInput  = 0x40000000u;
inline constexpr std::uint32_t kNoInputEdge = 0x80000000u;

// **The engine's polled word is NEVER 0**, so 0 is free and this replica gives
// it a name rather than a silent meaning. `Cef_TickChannel` gets its word from
// `sub_4A7A20` (0x004A7A20), which opens
//
//     *a3 = 0;
//     if (!a2)                       // a2 is the raw device mask
//       *a3 = 0x40000000;
//
// and then ORs one bit per bound key. Nothing held is the IDLE word; something
// held is a bitfield inside `kInputBits`. A caller driving this channel from a
// device must therefore pass `kIdleInput`, not 0, when nothing is held - the
// two are NOT interchangeable at the commit below, where a queue whose only
// word carries the idle bit is DROPPED and a 0 would be pushed instead.
//
// `kQueueDrives` (0) is the replica's own convention, and it stands in for the
// engine's flag-0x80 path rather than for any word: `Cef_TickChannel` skips the
// whole input pass while `flags & 0x81`, so an actor the fight AI drives never
// consults the device at all and its queue is the only source of input. A
// harness that injects a move and then wants the machine to consume it passes
// `kQueueDrives` and `tick` takes the word from the queue front. It is a
// documented contract, not a fallback: `stats().queueDriven` counts every tick
// that used it, so a caller that meant to pass a device word and passed 0 is
// visible in the numbers instead of silently working.
inline constexpr std::uint32_t kQueueDrives = 0u;

// The fourteen key-binding slots the engine polls, and the ten the fight AI
// ever presses. `Input_Poll` maps binding k to bit 1<<k; the union of every
// input word in every shipped AI profile is 0xCFF - never CTRL, SPACE, SHIFT
// or TAB, which are exactly the four non-combat bindings.
inline constexpr std::uint32_t kInputBits   = 0x3FFFu;
inline constexpr std::uint32_t kFightAiBits = 0x0CFFu;

// One decision the channel made, in place of the animation it cannot play.
struct ChannelEvent {
    enum class Kind {
        Cut,        // Actor_PlayClip: a hard change to `to`'s clip
        Blend,      // Actor_BlendToClip: `frames` frames into `to`'s clip
        Turn,       // Cef_ApplyTurn applied whole, on leaving
        Shift,      // Cef_ApplyRootShift applied whole, on leaving
        Move,       // Cef_QueueSpecialMove: a tab_special_move[] handler
    };
    Kind kind = Kind::Cut;
    int  from = -1;             // state index, -1 when entered from nowhere
    int  to   = -1;
    float startFrame = 1.0f;
    int  frames = 0;            // blend length, or the target clip's length
    std::uint32_t input = 0;    // the word that opened the edge, if any
    std::string name;           // the move name, for Kind::Move
};

// One edge the machine actually COMMITTED, with everything the sweep needs to
// re-derive the decision from the file rather than take the runtime's word for
// it: which state it left, which candidate it chose, the input word that was
// matched, and whether the priority gate was in force. Re-deriving is the
// point - an invariant checked by asking the code that made the decision
// whether it made it correctly checks nothing.
struct EdgeTaken {
    int from = -1;
    int to   = -1;
    std::uint32_t word = 0;
    bool gated = false;
    std::uint16_t threshold = 0;
    bool fromQueue = false;      // the queue pass, not the live-input pass
    // The rest of what `Cef_FindTransition` was given, so the re-derivation
    // can apply the SAME candidate test. These are the decision's INPUTS, not
    // its answer - handing them over keeps the second transcription second
    // without letting it disagree over a candidate the runtime never
    // considered. A window it did not model is what made eight legitimate
    // picks in `H1Cmbt` look like priority inversions: `HRSTEP` was beaten on
    // paper by entry 28, priority 2, whose cancel window is frames 10..13.
    int   mustHave = -1, mustAlso = -1;
    bool  useWindow = true;
    float prevFrame = 1.0f, curFrame = 1.0f;   // the channel's +12 / +16
};

// Why a transition was refused, so the sweep can tell "no edge was open" from
// "an edge was open and the machine took the wrong one".
struct ChannelStats {
    long ticks = 0;
    long transitions = 0;       // GoToMove calls that committed
    long landings = 0;          // states entered
    long badLanding = 0;        // a landing on an unresolved / out-of-range id
    long badEdge = 0;           // an edge taken that its own +4 does not open
    long priorityInversion = 0; // a gated pick beaten by a higher priority
    long aliasSteps = 0;        // alias / junction hops chased
    long chainAborted = 0;      // a chain that hit the depth guard: a CYCLE
    long queuePops = 0, queueResets = 0;
    long queueDriven = 0;       // ticks handed `kQueueDrives` (0), not a word
    long queueLoneIdleDrops = 0;// LABEL_75: a lone idle word dropped on commit
    long queueTruncated = 0;    // state flag 0x20000000 cut the queue to one
    long clipEnds = 0;          // the clip ran out and the GoTo was taken
    long foreignInput = 0;      // a consumed word outside kInputBits|kIdleInput
};

// The channel record: 57 ints at `dword_8F5920 + 57*index`, only the fields
// something below reads. The byte offsets are kept in the comments because
// every cross-reference in `readable/` is by offset.
class CefChannel {
public:
    CefChannel(const CtlFile& ctl) : ctl_(&ctl) {}

    // --- what the ACTOR_STATE layer above calls ------------------------
    //
    // SetPersoBankGroup (0x0045A630): clear the queue to the idle word and
    // GoToMove into the group's flag-0x20 default entry. Returns false the
    // way the original logs "SetPersoBank, start move not found".
    bool setBankGroup(int groupIndex);
    // Cef_FindGroupById (0x0046ACE0) / Cef_DefaultGroup (0x0046AD90).
    int  findGroupById(std::int32_t id) const;
    int  defaultGroup() const;

    // Perso_InjectInput (0x0045A9F0): load a move - a SEQUENCE of words - into
    // the queue, each OR'd with `orWith`. This is the whole of how the fight
    // AI drives an actor: it presses buttons into the same queue the player's
    // keys feed, and the machine below cannot tell the two apart.
    void injectInput(const std::vector<std::uint32_t>& words,
                     std::uint32_t orWith = 0);

    // Perso_SetInputEnabled (0x0045A3E0) toggles flag 0x80 and, when clearing
    // it, resets the queue; Perso_SetNoPlayback (0x0045A470) is flag 0x200.
    void setInputEnabled(bool on);
    void setNoPlayback(bool on);

    // The priority gate: channel flag 0x400 makes `Cef_FindTransition` honour
    // the threshold at `+212` instead of taking the first match it finds. Both
    // paths are exercised by the sweep, because they answer differently and
    // only one of them is the one a shipped bank uses.
    void setPriorityGate(bool on, std::uint16_t threshold = 0) {
        if (on) flags_ |= 0x400u; else flags_ &= ~0x400u;
        priorityThreshold_ = threshold;
    }

    // Cef_TickChannel (0x004A8160). `dt` is in FRAMES - the engine's delta is
    // `30.0 / fps`, so one unit is one frame at 30 Hz, the same clock the
    // ambient emitters and the scene programs run on.
    //
    // `code` is the word `sub_4A7A20` would have produced this tick, and the
    // contract is exactly that function's: **nothing held is `kIdleInput`, not
    // 0.** The only 0 this accepts is `kQueueDrives`, and it means something
    // different in kind - "there is no device this tick, the queue is the
    // input" - which is the AI's flag-0x80 path; see the constant above. Every
    // tick that takes it is counted in `stats().queueDriven`.
    bool tick(float dt, std::uint32_t code);

    // Cef_FindTransition (0x004A8BD0). -1 when no edge is open.
    int findTransition(int from, std::uint32_t code,
                       int mustHave, int mustAlso, bool useWindow) const;

    // GoToMove (0x004A7B80).
    bool gotoMove(int from, int to, float startFrame);

    // Fight_ResolveHit's half that belongs here: Cef_FindEntryByCode
    // (0x0047DC40) matches the LOW SIXTEEN BITS of an entry id, and
    // Cef_FindEntryByCodeGlobal (0x0047DE40) does the same across the file for
    // the six role codes Fight_Begin caches.
    int findEntryByCode(std::int32_t code) const;
    int findEntryByRole(std::int32_t role) const;

    int   state() const { return cur_; }
    float frame() const { return frame_; }
    // The input queue as the engine keeps it: the count at `+24` and the first
    // word at `+28`. Read-only, and the sweep's only way to SEE the commit's
    // two queue rules - a counter the rule increments proves nothing a stubbed
    // rule would not also prove.
    std::size_t   queueSize()  const { return queue_.size(); }
    std::uint32_t queueFront() const { return queue_.empty() ? 0u : queue_.front(); }
    const CtlFile& ctl() const { return *ctl_; }
    const std::vector<ChannelEvent>& events() const { return events_; }
    const std::vector<EdgeTaken>& edges() const { return edges_; }
    void clearEvents() { events_.clear(); edges_.clear(); }
    const ChannelStats& stats() const { return stats_; }
    ChannelStats& stats() { return stats_; }
    std::uint32_t flags() const { return flags_; }

private:
    bool inputMatches(std::uint32_t edge, std::uint32_t code) const;
    bool edgeOpens(const CtlState& cand, std::uint32_t code,
                   int mustHave, int mustAlso, bool useWindow) const;
    // An entry's GoTo edge as the RUNTIME sees it: `dynamicReturn_` first,
    // then the file. `GoToMove`'s flag-0x800 write goes into the engine's own
    // loaded copy, so every read after it must see it.
    int  gotoOf(int stateIndex) const;
    void popQueue();
    void resetQueue();
    int  clipFrames(int stateIndex) const;

    const CtlFile* ctl_;
    int   cur_     = -1;        // +184 the current entry
    int   pending_ = -1;        // +192 the blend target
    float pendingFrame_ = 1.0f; // +196
    float clipLen_ = 1.0f;      // +8   the current clip's length in frames
    float frame_   = 1.0f;      // +16  after this tick
    float prev_    = 1.0f;      // +12  before it
    std::uint32_t flags_ = 0;   // +4
    std::uint32_t lastInput_ = 0;                 // +20
    std::vector<std::uint32_t> queue_;            // +24 count, +28.. words
    std::uint32_t latch_[20] = {};                // +92, the 20-slot id set
    std::uint16_t priorityThreshold_ = 0;         // +212
    int repeat_ = 0;                              // +220
    // Entry flag 0x800 rewrites the target's GoTo to point back at the state
    // it was entered from - a dynamic return edge. The engine writes that into
    // its own loaded copy of the file; here a `CtlFile` is const and shared
    // between actors, so the rewrite is carried per channel instead.
    std::map<int, int> dynamicReturn_;
    std::vector<ChannelEvent> events_;
    std::vector<EdgeTaken> edges_;
    ChannelStats stats_;
};

}  // namespace omk
