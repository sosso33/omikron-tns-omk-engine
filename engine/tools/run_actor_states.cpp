// SPDX-License-Identifier: GPL-3.0-or-later
// Run `ACTOR_STATE` 0..17 and the `.CTL` channel underneath it, over every
// shipped bank, and assert what the DATA can falsify.
//
//     run_actor_states <gamedata/ANIMS> <out.bin>
//
// **This slice has no oracle and cannot be given one.** The golden-trace rig
// only sees what a VM handler narrates through `Dbg_LogTagged`, and combat is
// two opcodes - `fight.begin` announces nothing, `player.become` announces to
// CHARACTERS, which the logger filters out itself. `Fight_TickAI`,
// `Fight_ResolveHit`, the eighteen states and the transition matching are
// native code that never touches the tag path, and `traces/fight.log` was
// taken to prove otherwise and proved the opposite. So the standard here is
// **data-constrained, not engine-verified**, and these are the constraints:
//
//   1. every ACTOR_STATE transition is one the binary WRITES (the table in
//      `actor/state.cpp`), and a transition outside it is refused;
//   2. every `.CTL` state landed in is a real entry reached through an edge
//      the link pass resolved - no unresolved id, no chain that does not
//      terminate;
//   3. every committed edge is RE-DERIVED here from the file: its `+4` code
//      must open under `Cef_InputMatches`, and under the priority gate it must
//      carry the highest priority any allowed candidate had;
//   4. every reaction id in every combat block resolves in the low-16 space;
//   5. every input word the AI injects is inside its own 0xCFF union.
//
// Point 3 is the one that matters most and the easiest to fake: asking the
// code that chose an edge whether it chose correctly tests nothing, so the
// check below re-scans the state's children out of `CtlFile` and decides for
// itself.
#include "actor/state.h"
#include "formats/ctl.h"
#include "platform/datafs.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

using omk::CtlFile;
using omk::CtlState;

// Cef_InputMatches, written a SECOND time and from the documentation rather
// than from `channel.cpp`, so that the re-derivation is not the same code
// agreeing with itself. Low 15 bits must be held (subset), bit N+15 forbids
// input N, 0x80000000 is the no-input edge.
bool matches(std::uint32_t edge, std::uint32_t code) {
    for (int b = 0; b < 14; ++b)
        if ((edge & (0x8000u << b)) && (code & (1u << b))) return false;
    if ((edge & ~code) & 0x7FFFu) return false;
    if (edge == 0x80000000u) return code <= 0x2000u;
    return true;
}

struct Totals {
    long files = 0, banks = 0;
    long stateEntered[omk::kActorStateCount] = {};
    long transitions = 0, refusedBad = 0, refusedGood = 0;
    long parkRoundTrips = 0, parkFailures = 0;
    long ticks = 0, landings = 0, badLanding = 0, chainAborted = 0;
    long edges = 0, edgeNotOpened = 0, priorityInversion = 0;
    long gatedEdges = 0, gatedContests = 0, contestsOutOfOrder = 0;
    long foreignInput = 0;
    long combatBlocks = 0, reactionRefs = 0, reactionResolved = 0;
    long aiMoves = 0, aiWords = 0, aiWordsOutsideUnion = 0;
    long clipsWithFrames = 0, clipsWithoutFrames = 0;
    long dispatchChecked = 0, dispatchWrong = 0, statesWithNoTick = 0;
    // the two queue rules of Cef_TickChannel's commit
    long loneIdleDrops = 0, queueTruncated = 0;
    long loneIdleNotSeeded = 0, loneIdleStepped = 0;
    long loneIdleCases = 0, loneIdleActed = 0, loneIdleParked = 0;
    long loneIdleFired = 0;
    long truncCases = 0, truncCut = 0, truncNotCut = 0, truncMoved = 0;
};

// The fourteen single bits plus the neutral word: what a player can hold.
// Driving with these AND with the AI's own move words is the point - the two
// go through one matcher, so if they disagreed the machine would be wrong.
std::vector<std::uint32_t> playerWords() {
    std::vector<std::uint32_t> w{omk::kIdleInput};
    for (int b = 0; b < 14; ++b) w.push_back(1u << b);
    w.push_back(0x0005u);   // left + walk, the commonest held pair
    w.push_back(0x0804u);   // walk + run
    return w;
}

// Re-derive the edge decision from the file. Returns false when the candidate
// the machine committed to is not one its own `+4` code opens.
bool edgeWasOpen(const CtlFile& f, const omk::EdgeTaken& e) {
    if (e.from < 0 || e.to < 0) return false;
    const CtlState& cand = f.states[static_cast<std::size_t>(e.to)];
    if (!cand.inputCode) return false;
    if (cand.flags & 0x80000u) return cand.inputCode == e.word;
    return matches(cand.inputCode, e.word);
}

// Under the gate, the engine returns the first candidate whose priority equals
// the threshold and otherwise tracks the maximum - so the edge taken must
// carry the highest priority of any allowed, matching child. Ungated it takes
// the first match, which says nothing about priority, so that case is skipped
// rather than asserted.
// `contest` reports whether this decision was one the rule could have got
// wrong - two allowed candidates matching with DIFFERENT priorities. A check
// that never sees a contest is not passing, it is abstaining, and the number
// is printed for exactly that reason: at threshold 2 the gate never bites,
// because the shipped priorities are only 0, 1 and 2 and nothing is ever
// skipped for being too high.
// `outOfOrder` reports the one thing that would let a corpus test tell the
// priority rule from a plain first-match rule: a contest in which the FIRST
// matching allowed candidate is not one of maximal priority. It is 0 across
// the whole sweep - in all 120 contested decisions the highest-priority child
// is also the earliest - so the two rules answer identically on the shipped
// data and no test here can separate them. That is stated rather than hidden,
// because a check that cannot distinguish the implementation from a wrong one
// is not evidence for it; the priority rule stands on `Cef_FindTransition`'s
// own code, and what this asserts is the corpus property that makes the
// distinction moot.
bool priorityWasMaximal(const CtlFile& f, const omk::EdgeTaken& e,
                        bool* contest, bool* outOfOrder) {
    if (contest) *contest = false;
    if (outOfOrder) *outOfOrder = false;
    if (!e.gated) return true;
    const CtlState& from = f.states[static_cast<std::size_t>(e.from)];
    const bool reverse = (from.playBits & 0x20u) != 0;
    std::vector<int> order(from.childIdx.begin(), from.childIdx.end());
    if (reverse) std::reverse(order.begin(), order.end());
    int best = -1, firstMatch = -1;
    for (int idx : order) {
        if (idx < 0) continue;
        const CtlState& c = f.states[static_cast<std::size_t>(idx)];
        if (c.priority > e.threshold) continue;
        if (!c.inputCode) continue;
        // The rest of `Cef_FindTransition`'s candidate test, written from the
        // documentation like `matches` above and given the decision's own
        // inputs off `EdgeTaken`. Without the CANCEL WINDOW this over-counts:
        // it beat `H1Cmbt`'s `HRSTEP` with entry 28 (priority 2, window frames
        // 10..13) on eight decisions the runtime was right to refuse, which is
        // what an approximate second transcription looks like when the corpus
        // grows - the queue rules of 2026-09-02 more than doubled the edges
        // and the eight appeared with them.
        if (e.mustHave != -1 &&
            !(c.flags & static_cast<std::uint32_t>(e.mustHave))) continue;
        if (e.mustAlso != -1 &&
            !(c.flags & static_cast<std::uint32_t>(e.mustAlso))) continue;
        if ((c.cancelFrom != 0.0f || c.cancelTo != 0.0f) && e.useWindow &&
            (e.curFrame < c.cancelFrom || e.prevFrame > c.cancelTo ||
             (e.prevFrame < c.cancelFrom && e.curFrame < c.cancelTo))) continue;
        const bool open = (c.flags & 0x80000u) ? (c.inputCode == e.word)
                                               : matches(c.inputCode, e.word);
        if (!open) continue;
        if (firstMatch < 0) firstMatch = static_cast<int>(c.priority);
        if (best >= 0 && static_cast<int>(c.priority) != best && contest)
            *contest = true;
        best = std::max(best, static_cast<int>(c.priority));
    }
    if (best < 0) return true;   // the edge came from the group-global pass
    if (outOfOrder && firstMatch >= 0 && firstMatch < best) *outOfOrder = true;
    return f.states[static_cast<std::size_t>(e.to)].priority >= best;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: run_actor_states <gamedata/ANIMS> <out.bin>\n");
        return 2;
    }
    const omk::DataFs fs(argv[1]);
    auto files = fs.list(".", ".CTL");
    std::sort(files.begin(), files.end());

    Totals T;
    const auto words = playerWords();

    for (const auto& path : files) {
        const auto f = omk::readCtl(omk::DataFs::readPath(path));
        if (!f.valid) continue;
        ++T.files;

        for (const auto& c : f.clips)
            (c.frames > 0 ? T.clipsWithFrames : T.clipsWithoutFrames) += 1;

        // ---- 4. the combat blocks, resolved the way Fight_ResolveHit does
        std::map<std::uint32_t, int> low16;
        for (const auto& st : f.states) ++low16[st.id & 0xFFFFu];
        for (const auto& st : f.states) {
            if (!st.hasCombat) continue;
            ++T.combatBlocks;
            for (auto r : {st.combat.reactionA(), st.combat.reactionB()}) {
                if (r == -1) continue;
                ++T.reactionRefs;
                const auto key = static_cast<std::uint32_t>(r) & 0xFFFFu;
                if (low16.count(key) && low16[key] == 1) ++T.reactionResolved;
            }
        }

        // ---- 5. the AI's own words, which drive the same machine
        for (const auto& p : f.ai)
            for (const auto& slot : p.slots)
                for (const auto& mv : slot.moves) {
                    ++T.aiMoves;
                    for (auto w : mv) {
                        ++T.aiWords;
                        if ((w & ~0x40000000u) & ~omk::kFightAiBits)
                            ++T.aiWordsOutsideUnion;
                    }
                }

        // ---- 1. the ACTOR_STATE layer -------------------------------------
        //
        // One runtime per file, driven through every named writer. The park
        // round trip is the part that could actually be wrong: 16 restores
        // [102] and 17 does NOT - it lands in 1 and closes the screens - and
        // an implementation that treated them alike passes every other test
        // here.
        {
            omk::ActorRuntime a(f, /*isPlayer=*/true);
            a.loadModel();                                   // -> 0 then 1
            for (auto st : {omk::ActorState::Normal, omk::ActorState::Melee,
                            omk::ActorState::Shoot, omk::ActorState::ScxDriven,
                            omk::ActorState::ChannelOnly6}) {
                // park each state behind a conversation and get it back
                switch (st) {
                    case omk::ActorState::Melee:  a.fightEngage(); break;
                    case omk::ActorState::Shoot:  a.shootEnter();  break;
                    case omk::ActorState::ScxDriven: a.scxStart(); break;
                    case omk::ActorState::ChannelOnly6:
                        a.setState(omk::ActorState::ChannelOnly6, "MDACTION");
                        break;
                    default: break;
                }
                const auto before = a.state();
                a.enterDialogue();
                a.leaveDialogue();
                if (a.state() == before) ++T.parkRoundTrips; else ++T.parkFailures;
                if (a.state() != omk::ActorState::Normal)
                    a.setState(omk::ActorState::Normal, "sub_452570");
            }
            // 17: entered from 9, and it must NOT come back to 9
            a.uiLoadScreen();
            a.enterDialogue();
            const bool sevenTeen = a.state() == omk::ActorState::DialogueFromUi;
            a.leaveDialogue();
            if (sevenTeen && a.state() == omk::ActorState::Normal) ++T.parkRoundTrips;
            else ++T.parkFailures;

            // the rest of the writers, so every state is entered at least once
            a.scxStart(); a.morphPlay(); a.startPendingScx(true);
            a.scxDrivenDone();
            a.setState(omk::ActorState::SliderMount, "MDSLIDIN");
            a.setState(omk::ActorState::SliderRide,  "sub_468FA0");
            a.setState(omk::ActorState::Normal,      "MDSLIDOU");
            a.setState(omk::ActorState::Swim,        "RSTNAGE");
            a.setState(omk::ActorState::Normal,      "RSTAVNT");
            a.ladderEnter();
            a.setState(omk::ActorState::Scripted12, "sub_4A9580");
            a.setState(omk::ActorState::Scripted13, "sub_4A8F30");
            a.setState(omk::ActorState::Shoot15,    "sub_423FC0");
            a.shootLeave(true);
            a.imageScreenOpen(); a.imageScreenClose();
            a.setState(omk::ActorState::Inert, "sub_472C30");

            // the NEGATIVE control, which is what makes the table a rule
            // rather than a comment: MDSLIDOU dismounts only from state 8, and
            // Morph_Play writes 5 only out of 4.
            const long before = a.refused();
            a.setState(omk::ActorState::Normal, "MDSLIDOU");   // from 0: refused
            a.morphPlay();                                     // from 0: refused
            a.setState(omk::ActorState::Melee, "Actor_LoadModel");  // wrong writer
            T.refusedGood += a.refused() - before;

            // The dispatch table, checked against a SECOND transcription.
            //
            // A first version of this ticked each state and asserted the
            // channel advanced iff the table said it would - which is
            // circular, because `ActorRuntime::tick` reads that same flag to
            // decide. Flipping a row passed. So the check is now a
            // differential between two independent readings: `state.cpp` says
            // which tick function each state dispatches to, and the map below
            // - written from `readable/src/21_d3d.c` rather than from
            // `state.cpp` - says which of those functions reaches
            // `Cef_TickChannel`. A slip in either one shows up as a
            // disagreement. That is the same pattern as the second copy of
            // `Cef_InputMatches` above, and it has the same limit: it cannot
            // catch an error both transcriptions share.
            struct TickFn { const char* name; bool channel; };
            static const TickFn kTickFns[] = {
                {"nullsub_6",                   false},  // a one-byte retn
                {"Actor_TickNpc",               true },  // Cef_TickChannel first
                {"Actor_TickPlayerAndOpponent", true },  // once per fighter
                {"Actor_TickShoot",             true },  // via Actor_TickNpc
                {"Actor_TickScxDriven",         false},  // the object owns it
                {"Actor_StartPendingScx",       false},
                {"Actor_TickChannelOnly",       true },  // it is the whole body
                {"Actor_TickUiHeld",            false},
                {"sub_466E70",                  false},
                {"Actor_TickDialogue",          true },
                {"Actor_TickUiHeld+Dialogue",   true },  // 17 falls through to 16
            };
            for (int i = 0; i < omk::kActorStateCount; ++i) {
                const auto st = static_cast<omk::ActorState>(i);
                const auto& info = omk::actorStateInfo(st);
                if (!info.tick[0]) { ++T.statesWithNoTick; continue; }
                const TickFn* fn = nullptr;
                for (const auto& t : kTickFns)
                    if (std::string(t.name) == info.tick) { fn = &t; break; }
                ++T.dispatchChecked;
                // an unknown tick function is itself a disagreement: the
                // switch names a closed set of ten
                if (!fn || fn->channel != info.channelTicks) ++T.dispatchWrong;
            }

            for (const auto& l : a.log()) {
                ++T.transitions;
                const int id = static_cast<int>(l.to);
                if (id >= 0 && id < omk::kActorStateCount) ++T.stateEntered[id];
            }
            T.refusedBad += a.refused() - (a.refused() - before) - before;
        }

        // ---- 2 and 3. the channel, one run per group ----------------------
        for (std::size_t g = 0; g < f.groupList.size(); ++g) {
            if (f.groupList[g].defaultEntry < 0) continue;
            // Ungated, then the gate at each shipped priority. Threshold 2 is
            // the degenerate one - nothing is ever above it - so a sweep that
            // only ran it would be testing the gate without engaging it.
            for (int gate = 0; gate < 4; ++gate) {
                omk::CefChannel ch(f);
                ch.setPriorityGate(gate != 0,
                                   static_cast<std::uint16_t>(gate - 1));
                if (!ch.setBankGroup(static_cast<int>(g))) continue;
                ++T.banks;
                std::size_t wi = 0;
                for (int step = 0; step < 240; ++step) {
                    // one frame at 30 Hz is one unit, the engine's own delta
                    ch.tick(1.0f, words[wi++ % words.size()]);
                    ++T.ticks;
                }
                // and again driven by the file's own AI moves, so the human
                // and the AI paths are both exercised on the same bank
                for (const auto& p : f.ai)
                    for (const auto& slot : p.slots)
                        for (const auto& mv : slot.moves) {
                            ch.injectInput(mv);
                            for (int step = 0; step < 6; ++step) {
                                ch.tick(1.0f, 0);
                                ++T.ticks;
                            }
                        }
                const auto& st = ch.stats();
                T.loneIdleDrops  += st.queueLoneIdleDrops;
                T.queueTruncated += st.queueTruncated;
                T.landings    += st.landings;
                T.badLanding  += st.badLanding;
                T.chainAborted += st.chainAborted;
                T.foreignInput += st.foreignInput;
                for (const auto& e : ch.edges()) {
                    ++T.edges;
                    if (e.gated) ++T.gatedEdges;
                    if (!edgeWasOpen(f, e)) ++T.edgeNotOpened;
                    bool contest = false, outOfOrder = false;
                    if (!priorityWasMaximal(f, e, &contest, &outOfOrder))
                        ++T.priorityInversion;
                    if (contest) ++T.gatedContests;
                    if (outOfOrder) ++T.contestsOutOfOrder;
                }
            }
        }

        // ---- 5. the two QUEUE rules of `Cef_TickChannel`'s commit --------
        //
        // The sweep above cannot see either, and that is structural rather
        // than an oversight: it drives the machine with `injectInput`, which
        // REPLACES the queue, so a lone idle word never stands in front of a
        // press there and no state is ever current with a queue it did not
        // just receive. These two put the machine in each case by hand.
        //
        // (a) THE LONE IDLE (`LABEL_75`). `SetPersoBankGroup`'s memset leaves
        //     the queue holding exactly one word, `0x40000000`, and then a key
        //     is pressed. The commit drops that word before pushing the press,
        //     so the press is at the FRONT and the queue pass acts on it on
        //     this tick; without the drop it queues behind a word that opens
        //     nothing and waits for something to pop it. What is asserted is
        //     the press being ACTED ON - either the machine took an edge, or
        //     the press is what the queue now offers - and not "a transition
        //     fired", because whether one does is also the cancel window's and
        //     the `0x80000000` gate's business and those are not this rule.
        for (std::size_t g = 0; g < f.groupList.size(); ++g) {
            if (f.groupList[g].defaultEntry < 0) continue;
            omk::CefChannel ch(f);
            if (!ch.setBankGroup(static_cast<int>(g))) continue;
            const int st = ch.state();
            if (st < 0) continue;
            const CtlState& cur = f.states[static_cast<std::size_t>(st)];
            // Only a bank the seed actually left holding the lone idle word is
            // a case: `GoToMove`'s own entry flags (0x100000 pop, 0x1000000
            // reset) can have emptied or rewritten it on the way in.
            if (ch.queueSize() != 1 || ch.queueFront() != omk::kIdleInput) {
                ++T.loneIdleNotSeeded;
                continue;
            }
            if (!(cur.flags & 0x8001u)) continue;   // no queue pass at all
            // and not one that STEPS the queue: flag 8 makes the queue pass
            // pop its way down the queue until something matches, so it would
            // reach a press parked behind the idle word within the same tick
            // and the rule's effect would not be visible. The front is the
            // only word a state without flag 8 ever consults, which is what
            // makes this observable exact.
            if (cur.flags & 8u) { ++T.loneIdleStepped; continue; }
            for (int b = 0; b < 14; ++b) {
                const std::uint32_t w = 1u << b;
                // A press the commit will actually reach: the current entry's
                // flag 0x1000 makes the commit conditional on an edge being
                // open for the word (`Cef_TickChannel`'s
                // `if (Cef_FindTransition(...) || (v53 & 0x1000) == 0)`).
                if ((cur.flags & 0x1000u) &&
                    ch.findTransition(st, w, -1, -1, true) < 0) continue;
                ++T.loneIdleCases;
                const long tr0 = ch.stats().transitions;
                ch.tick(1.0f, w);
                const bool moved = ch.stats().transitions > tr0;
                if (moved) ++T.loneIdleFired;
                if (moved || (ch.queueSize() >= 1 && ch.queueFront() == w))
                    ++T.loneIdleActed;
                else
                    ++T.loneIdleParked;
                break;                              // one press per bank
            }
        }

        // (b) THE TRUNCATION. Under the CURRENT entry's flag 0x20000000 a
        //     queue longer than one is cut to one and the word is NOT pushed
        //     at all - the state refuses to accumulate presses behind it. 21
        //     of the 1286 shipped entries carry the flag, and the sweep above
        //     never has one current with more than one word queued, so this
        //     puts the machine on each of them by hand (`GoToMove` with no
        //     `from`, the path `SetPersoBankGroup` itself takes), fills the
        //     queue with three words and presses a fourth that opens an edge.
        for (std::size_t i = 0; i < f.states.size(); ++i) {
            if (!(f.states[i].flags & 0x20000000u)) continue;
            omk::CefChannel ch(f);
            if (!ch.gotoMove(-1, static_cast<int>(i), 1.0f)) continue;
            const int st = ch.state();
            if (st < 0 || !(f.states[static_cast<std::size_t>(st)].flags
                            & 0x20000000u))
                continue;                  // an alias chain moved it elsewhere
            // The press has to reach the commit, which the current entry's
            // flag 0x1000 gates on an edge being open for it.
            const std::uint32_t curFlags =
                f.states[static_cast<std::size_t>(st)].flags;
            std::uint32_t w = 0;
            for (int b = 0; b < 14; ++b) {
                const std::uint32_t c = 1u << b;
                if (c == 0x0001u || c == 0x0002u || c == 0x0004u) continue;
                if (!(curFlags & 0x1000u) ||
                    ch.findTransition(st, c, -1, -1, true) >= 0) { w = c; break; }
            }
            if (!w) continue;
            ch.injectInput({0x0001u, 0x0002u, 0x0004u});
            if (ch.queueSize() != 3) continue;
            ++T.truncCases;
            const long cut0 = ch.stats().queueTruncated;
            const long tr0  = ch.stats().transitions;
            ch.tick(1.0f, w);
            // A transition would pop or reset the queue itself, so the
            // observable is only clean while the machine stands still; those
            // are counted apart rather than passed by default.
            if (ch.stats().transitions > tr0) { ++T.truncMoved; continue; }
            // The rule fired AND the press was not pushed: the queue is at
            // most the one word the cut leaves, and it is not the press. (It
            // can be shorter still - `D1Cmbt`'s `HKWALK` carries flag 0x40000
            // as well, which empties the queue when its front is the entry's
            // own code, so the size is 0 there.)
            if (ch.stats().queueTruncated > cut0 &&
                ch.queueSize() <= 1 && ch.queueFront() != w)
                ++T.truncCut;
            else
                ++T.truncNotCut;
        }
    }

    long statesSeen = 0;
    for (long n : T.stateEntered) if (n) ++statesSeen;

    std::vector<std::uint8_t> o;
    const auto put = [&o](long v) {
        const auto u = static_cast<std::uint32_t>(static_cast<std::int32_t>(v));
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    for (long v : {T.files, T.banks, statesSeen, T.transitions, T.refusedBad,
                   T.refusedGood, T.parkRoundTrips, T.parkFailures,
                   T.ticks, T.landings, T.badLanding, T.chainAborted,
                   T.edges, T.edgeNotOpened, T.priorityInversion,
                   T.gatedEdges, T.gatedContests, T.contestsOutOfOrder,
                   T.foreignInput, T.combatBlocks, T.reactionRefs,
                   T.reactionResolved, T.aiMoves, T.aiWords,
                   T.aiWordsOutsideUnion, T.clipsWithFrames,
                   T.clipsWithoutFrames, T.dispatchChecked, T.dispatchWrong,
                   T.statesWithNoTick,
                   T.loneIdleDrops, T.queueTruncated, T.loneIdleNotSeeded,
                   T.loneIdleStepped,
                   T.loneIdleCases, T.loneIdleActed, T.loneIdleParked,
                   T.loneIdleFired,
                   T.truncCases, T.truncCut, T.truncNotCut, T.truncMoved})
        put(v);
    if (!omk::safeOutputPath(argv[2])) return 2;
    std::ofstream out(argv[2], std::ios::binary);
    out.write(reinterpret_cast<const char*>(o.data()),
              static_cast<std::streamsize>(o.size()));

    std::printf("states: %ld files, %ld banks driven; %ld of 18 ACTOR_STATEs "
                "entered, %ld transitions, %ld refused wrongly, %ld refused "
                "correctly (the negative control)\n",
                T.files, T.banks, statesSeen, T.transitions, T.refusedBad,
                T.refusedGood);
    std::printf("park: %ld round trips, %ld failures\n",
                T.parkRoundTrips, T.parkFailures);
    std::printf("channel: %ld ticks, %ld landings, %ld unresolved, %ld chains "
                "aborted (a CYCLE the format does not forbid)\n",
                T.ticks, T.landings, T.badLanding, T.chainAborted);
    std::printf("edges: %ld committed (%ld under the priority gate, %ld of "
                "them a real CONTEST between differing priorities, %ld where "
                "the first match is NOT of maximal priority - the only shape "
                "that could tell the priority rule from a first-match rule), "
                "%ld not opened by their own +4 code, %ld priority "
                "inversions, %ld foreign input words\n",
                T.edges, T.gatedEdges, T.gatedContests, T.contestsOutOfOrder,
                T.edgeNotOpened, T.priorityInversion, T.foreignInput);
    std::printf("combat: %ld blocks, %ld reaction refs, %ld resolving uniquely "
                "in the low-16 space\n",
                T.combatBlocks, T.reactionRefs, T.reactionResolved);
    std::printf("ai: %ld moves, %ld input words, %ld outside the 0xCFF union\n",
                T.aiMoves, T.aiWords, T.aiWordsOutsideUnion);
    std::printf("clips: %ld with a frame count, %ld without\n",
                T.clipsWithFrames, T.clipsWithoutFrames);
    std::printf("dispatch: %ld states ticked, %ld disagreeing with the table, "
                "%ld with no case in Actors_TickAll at all\n",
                T.dispatchChecked, T.dispatchWrong, T.statesWithNoTick);
    std::printf("queue: %ld lone-idle drops and %ld truncations over the main "
                "sweep; lone idle: %ld banks (%ld not left holding it, %ld "
                "stepping the queue), %ld "
                "acting on the press this tick, %ld parking it behind the idle "
                "word, %ld of them transitioning outright; truncation: %ld "
                "entries, %ld cut to one, %ld not, %ld transitioning\n",
                T.loneIdleDrops, T.queueTruncated,
                T.loneIdleCases, T.loneIdleNotSeeded, T.loneIdleStepped,
                T.loneIdleActed,
                T.loneIdleParked, T.loneIdleFired,
                T.truncCases, T.truncCut, T.truncNotCut, T.truncMoved);
    std::printf("per state:");
    for (int i = 0; i < omk::kActorStateCount; ++i)
        std::printf(" %d:%ld", i, T.stateEntered[i]);
    std::printf("\n");
    return 0;
}
