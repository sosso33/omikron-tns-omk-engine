// SPDX-License-Identifier: GPL-3.0-or-later
#include "actor/channel.h"

#include <algorithm>
#include <cmath>

namespace omk {
namespace {

// GoToMove and the alias chase both follow pointer chains the file could in
// principle make circular - `to->gotoState` while `to->flags & 0x8000`, and
// `to->flags & 2` adopting another entry's start frame. The engine has no
// guard: it would hang. The replica counts instead, so a cycle becomes a
// number the sweep can assert is zero rather than a lock-up.
constexpr int kChainGuard = 64;

}  // namespace

int CefChannel::clipFrames(int stateIndex) const {
    if (stateIndex < 0 || stateIndex >= static_cast<int>(ctl_->states.size()))
        return 1;
    const int clip = ctl_->states[static_cast<std::size_t>(stateIndex)].clip;
    if (clip < 0 || clip >= static_cast<int>(ctl_->clips.size())) return 1;
    const auto n = ctl_->clips[static_cast<std::size_t>(clip)].frames;
    return n > 0 ? n : 1;
}

// Cef_InputMatches (0x004A8AD0), transcribed. The low 15 bits of the edge code
// are inputs that must be HELD - a subset test, not an equality - and bit N+15
// forbids input bit N. The engine writes the fourteen forbid-tests out one by
// one; the loop below is the same predicate, and the last two lines are the
// engine's own tail verbatim.
bool CefChannel::inputMatches(std::uint32_t edge, std::uint32_t code) const {
    for (int bit = 0; bit < 14; ++bit)
        if ((edge & (0x8000u << bit)) && (code & (1u << bit))) return false;
    if (edge & (edge ^ code) & 0x7FFFu) return false;
    // The "no input" edge: open only while nothing above bit 13 is pressed.
    // This is also why the queue's idle word 0x40000000 matches NO idle edge -
    // it is larger than 0x2000 - and the two constants are routinely confused.
    if (edge == kNoInputEdge) return code <= 0x2000u;
    return true;
}

// The candidate test `Cef_FindTransition` repeats inline in both of its search
// passes. Kept as one function here and named in `channel.h`'s terms.
bool CefChannel::edgeOpens(const CtlState& cand, std::uint32_t code,
                           int mustHave, int mustAlso, bool useWindow) const {
    if (!cand.inputCode) return false;
    if (mustHave != -1 && !(cand.flags & static_cast<std::uint32_t>(mustHave)))
        return false;
    if (mustAlso != -1 && !(cand.flags & static_cast<std::uint32_t>(mustAlso)))
        return false;
    if ((cand.cancelFrom != 0.0f || cand.cancelTo != 0.0f) && useWindow) {
        // The cancel window, against the frames of the CURRENT clip before and
        // after this tick. The third clause is kept exactly as compiled - it
        // is not a tidier form of the first two, and rewriting it changes
        // which frame a cancel opens on.
        if (frame_ < cand.cancelFrom || prev_ > cand.cancelTo ||
            (prev_ < cand.cancelFrom && frame_ < cand.cancelTo))
            return false;
    }
    if (cand.flags & 0x80000u)        // exact code, not the bitfield test
        return cand.inputCode == code;
    return inputMatches(cand.inputCode, code);
}

int CefChannel::findTransition(int from, std::uint32_t code,
                               int mustHave, int mustAlso, bool useWindow) const {
    if (from < 0 || from >= static_cast<int>(ctl_->states.size())) return -1;
    const CtlState& f = ctl_->states[static_cast<std::size_t>(from)];
    const bool gated = (flags_ & 0x400u) != 0;   // honour the +212 threshold
    const bool reverse = (f.playBits & 0x20u) != 0;
    int best = -1;

    const int n = static_cast<int>(f.childIdx.size());
    for (int i = 0; i < n; ++i) {
        const int idx = f.childIdx[static_cast<std::size_t>(reverse ? n - 1 - i : i)];
        if (idx < 0) continue;                    // an edge the link pass lost
        const CtlState& cand = ctl_->states[static_cast<std::size_t>(idx)];
        if (gated && cand.priority > priorityThreshold_) continue;
        if (!edgeOpens(cand, code, mustHave, mustAlso, useWindow)) continue;
        if (!gated) return idx;
        if (cand.priority == priorityThreshold_) return idx;
        if (best < 0 ||
            cand.priority > ctl_->states[static_cast<std::size_t>(best)].priority)
            best = idx;
    }

    // The GROUP-GLOBAL edges: flag 0x2000 on the current state lets it inherit
    // every entry of its group carrying 0x4000, so a guard break or a hit
    // reaction need not be wired into every state by hand.
    if (best < 0 && (f.flags & 0x2000u)) {
        const int cg = (cur_ >= 0 && cur_ < static_cast<int>(ctl_->states.size()))
                           ? ctl_->states[static_cast<std::size_t>(cur_)].group
                           : f.group;
        for (std::size_t i = 0; i < ctl_->states.size(); ++i) {
            const CtlState& cand = ctl_->states[i];
            if (cand.group != cg) continue;
            if (gated && cand.priority > priorityThreshold_) continue;
            if (!(cand.flags & 0x4000u)) continue;
            if (!edgeOpens(cand, code, -1, -1, useWindow)) continue;
            if (!gated) return static_cast<int>(i);
            if (cand.priority == priorityThreshold_) return static_cast<int>(i);
            if (best < 0 ||
                cand.priority > ctl_->states[static_cast<std::size_t>(best)].priority)
                best = static_cast<int>(i);
        }
    }
    return best;
}

// Every read of an entry's GoTo edge, and the reason it is a function.
//
// `GoToMove` writes one: `if (to->flags & 0x800) to->gotoState = from;` - the
// DYNAMIC RETURN EDGE, an entry that goes back wherever it was entered from.
// The engine writes that straight into its own loaded copy of the file, so
// every later read of that entry's GoTo - the alias chase two lines below, the
// clip-end fall-through, the 0x8000 skip, the phase-match target - sees the
// runtime value, not the authored one. A `CtlFile` here is const and shared
// between actors, so the write is carried per channel in `dynamicReturn_` and
// every read goes through this.
//
// It was written and never read until 2026-09-02, which cost nothing while the
// queue rules were missing and 956 bad landings the moment they landed: Sham's
// `SH_STAND` reaches two nameless flag-0x800 aliases (flags 0xC0084813) whose
// AUTHORED GoTo is 0. Read statically they are a landing on nothing - which is
// a null deref in the engine, so it cannot be what the engine does; read
// dynamically they return to `SH_STAND`, which is what a return edge is for.
int CefChannel::gotoOf(int stateIndex) const {
    const auto it = dynamicReturn_.find(stateIndex);
    if (it != dynamicReturn_.end()) return it->second;
    if (stateIndex < 0 || stateIndex >= static_cast<int>(ctl_->states.size()))
        return -1;
    return ctl_->states[static_cast<std::size_t>(stateIndex)].gotoIdx;
}

void CefChannel::popQueue() {
    if (queue_.empty()) return;
    queue_.erase(queue_.begin());
    ++stats_.queuePops;
}

void CefChannel::resetQueue() {
    queue_.assign(1, kIdleInput);
    ++stats_.queueResets;
}

bool CefChannel::gotoMove(int from, int to, float startFrame) {
    if (to < 0) { ++stats_.badLanding; return false; }
    const auto& S = ctl_->states;
    int final = -1;
    int guard = 0;

    for (;;) {
        if (++guard > kChainGuard) { ++stats_.chainAborted; return false; }
        repeat_ = (to == from) ? repeat_ + 1 : 0;
        if (flags_ & 0x81u) repeat_ = 0;

        if (from >= 0) {
            if (!(flags_ & 0x20u)) {          // not while following an alias
                const CtlState& f = S[static_cast<std::size_t>(from)];
                if (f.flags & 0x400000u) popQueue();
                if (f.flags & 0x4000000u) resetQueue();
                // Cef_ApplyTurn / Cef_ApplyRootShift, applied WHOLE because
                // the state is being left. The over-the-window modes (0x40 /
                // 0x80) are the other bit of each pair and belong to the
                // per-tick path below.
                if ((f.flags & 0x100u) && !(flags_ & 0x200u) && f.hasTurn)
                    events_.push_back({ChannelEvent::Kind::Turn, from, to,
                                       startFrame, 0, 0, {}});
                if ((f.flags & 0x200u) && !(flags_ & 0x200u) && f.hasShift)
                    events_.push_back({ChannelEvent::Kind::Shift, from, to,
                                       startFrame, 0, 0, {}});
            }
        } else {
            // Entered from nowhere: skip the pass-through states. This is the
            // path SetPersoBankGroup takes on the very first transition.
            while (S[static_cast<std::size_t>(to)].flags & 0x8000u) {
                to = gotoOf(to);
                ++stats_.aliasSteps;
                if (to < 0) { ++stats_.badLanding; return false; }
                if (++guard > kChainGuard) { ++stats_.chainAborted; return false; }
            }
        }

        const CtlState& t = S[static_cast<std::size_t>(to)];
        if (t.flags & 0x10u)
            events_.push_back({ChannelEvent::Kind::Move, from, to, startFrame,
                               0, 0, t.moveName});
        if (t.flags & 0x10000000u) lastInput_ = 0;
        if (t.flags & 0x100000u) popQueue();
        if (t.flags & 0x1000000u) resetQueue();
        if (t.flags & 0x40000000u) {
            // record the entered code in the 20-slot latch set at +92
            for (int i = 0; i < 20; ++i) {
                if (latch_[i] == t.inputCode) break;
                if (!latch_[i]) { latch_[i] = t.inputCode; break; }
            }
        }
        // flag 0x800 rewrites the target's GoTo to point back at `from` - a
        // dynamic RETURN edge. The replica does not write it back into the
        // shared file the way the engine writes it into its own loaded copy,
        // because a `CtlFile` here is const and shared between actors; the
        // return is carried per channel instead.
        if (t.flags & 0x800u) dynamicReturn_[to] = from;

        if (!(t.flags & 2u)) break;
        flags_ |= 0x20u;                       // an alias: adopt its start
        startFrame = t.startFrame;             // frame and follow the goto
        to = gotoOf(to);
        ++stats_.aliasSteps;
        if (to < 0) { ++stats_.badLanding; return false; }
    }

    if (from >= 0 && (S[static_cast<std::size_t>(from)].flags & 0x8000u) &&
        (S[static_cast<std::size_t>(to)].flags & 0x8000u)) {
        const float sf = S[static_cast<std::size_t>(to)].startFrame;
        const int nxt = gotoOf(to);
        ++stats_.aliasSteps;
        if (nxt < 0) { ++stats_.badLanding; return false; }
        return gotoMove(from, nxt, sf);        // and run the entry pass again
    }

    flags_ &= ~0x20u;

    // Rolled past the clip's end: fold the overshoot into the new start, so a
    // transition taken late does not lose the frames it was late by.
    if (frame_ > clipLen_ && clipLen_ > 1.0f) {
        double over = static_cast<double>(frame_) - 1.0;
        while (over >= static_cast<double>(clipLen_) - 1.0)
            over -= static_cast<double>(clipLen_) - 1.0;
        startFrame += static_cast<float>(over);
    }

    const CtlState& t = S[static_cast<std::size_t>(to)];
    int newLen = 0;
    if (from >= 0 && (S[static_cast<std::size_t>(from)].flags & 0x8000u)) {
        const CtlState& f = S[static_cast<std::size_t>(from)];
        if (!(flags_ & 0x12u) && !(f.flags & 0x8000000u)) {
            // ---- blended transition ---------------------------------------
            int real = to, g2 = 0;
            while (S[static_cast<std::size_t>(real)].flags & 0x8002u) {
                real = gotoOf(real);
                ++stats_.aliasSteps;
                if (real < 0) { ++stats_.badLanding; return false; }
                if (++g2 > kChainGuard) { ++stats_.chainAborted; return false; }
            }
            int blend;
            if (t.flags & 0x20000u) blend = f.blendFrames;
            else if (static_cast<double>(f.blendFrames) >= frame_)
                blend = f.blendFrames - static_cast<int>(frame_) + 1;
            else blend = 1;
            events_.push_back({ChannelEvent::Kind::Blend, from, real, startFrame,
                               blend, lastInput_, {}});
            newLen = blend;
            flags_ |= 2u;                      // blending; the target parks
            pendingFrame_ = startFrame;        // in pending_ until it lands
            pending_ = to;
            startFrame = 1.0f;
            final = from;
        } else {
            // ---- direct cut ------------------------------------------------
            cur_ = to;
            events_.push_back({ChannelEvent::Kind::Cut, from, to, startFrame,
                               clipFrames(to), lastInput_, {}});
            newLen = clipFrames(to);
            flags_ &= ~2u;
            flags_ &= ~0x10u;
            final = to;
        }
    } else if (!(t.flags & 0x8000u)) {
        cur_ = to;
        events_.push_back({ChannelEvent::Kind::Cut, from, to, startFrame,
                           clipFrames(to), lastInput_, {}});
        newLen = clipFrames(to);
        flags_ &= ~0x10u;
        final = to;
    } else {
        // `to` is itself a pass-through: blend into the clip owner behind its
        // GoTo chain, out of the clip being left.
        int real = to, g2 = 0;
        if (t.flags & 0x8002u) {
            do {
                real = gotoOf(real);
                ++stats_.aliasSteps;
                if (real < 0) { ++stats_.badLanding; return false; }
                if (++g2 > kChainGuard) { ++stats_.chainAborted; return false; }
            } while (S[static_cast<std::size_t>(real)].flags & 0x8002u);
        }
        float start = t.startFrame;
        if (t.flags & 0x10000u) {
            // Phase-match: carry the current phase into the target clip, which
            // is what keeps a gait when walk hands over to run. The engine
            // writes the result back into the entry; the replica keeps it
            // local for the same reason as the dynamic return above.
            const int tgt = gotoOf(to);
            const float tlen = static_cast<float>(clipFrames(tgt));
            start = frame_ / clipLen_ * tlen + static_cast<float>(t.phaseOffset);
            if (start > tlen) start = start - tlen + 1.0f;
        }
        cur_ = to;
        events_.push_back({ChannelEvent::Kind::Blend, from, real, start,
                           t.blendFrames, lastInput_, {}});
        newLen = t.blendFrames;
        flags_ &= ~0x10u;
        final = to;
    }

    clipLen_ = static_cast<float>(newLen > 0 ? newLen : 1);
    if (startFrame > clipLen_) startFrame = clipLen_;
    prev_  = frame_;
    frame_ = startFrame;
    ++stats_.transitions;
    ++stats_.landings;
    if (final >= 0 && (S[static_cast<std::size_t>(final)].flags & 0x800000u))
        popQueue();
    return true;
}

int CefChannel::findGroupById(std::int32_t id) const {
    for (std::size_t i = 0; i < ctl_->groupList.size(); ++i)
        if (ctl_->groupList[i].id == id) return static_cast<int>(i);
    return -1;
}

int CefChannel::defaultGroup() const {
    for (std::size_t i = 0; i < ctl_->groupList.size(); ++i)
        if (ctl_->groupList[i].flags & 1u) return static_cast<int>(i);
    return -1;
}

bool CefChannel::setBankGroup(int groupIndex) {
    if (groupIndex < 0 || groupIndex >= static_cast<int>(ctl_->groupList.size()))
        return false;
    // The memset in the original clears the queue and the latch set together,
    // then seeds both with the idle word.
    queue_.assign(1, kIdleInput);
    for (auto& l : latch_) l = 0;
    lastInput_ = kIdleInput;
    pendingFrame_ = 1.0f;
    flags_ &= ~1u;
    const int entry = ctl_->groupList[static_cast<std::size_t>(groupIndex)].defaultEntry;
    if (entry < 0) return false;         // "SetPersoBank, start move not found"
    return gotoMove(cur_, entry, 1.0f);  // "SetPersoBank, error on GoToMove"
}

void CefChannel::injectInput(const std::vector<std::uint32_t>& words,
                             std::uint32_t orWith) {
    queue_.clear();
    for (auto w : words) queue_.push_back(w | orWith);
}

void CefChannel::setInputEnabled(bool on) {
    if (on) {
        flags_ |= 0x80u;
    } else {
        flags_ &= ~0x80u;
        queue_.assign(1, kIdleInput);
        lastInput_ = kIdleInput;
    }
}

void CefChannel::setNoPlayback(bool on) {
    if (on) flags_ |= 0x200u; else flags_ &= ~0x200u;
}

bool CefChannel::tick(float dt, std::uint32_t code) {
    // The effect pass runs on EVERY path, including the self-transition the
    // clip wrap takes (`gotoMove(cur_, cur_, 1.0f)`), which returns early -
    // and that path is precisely the one a looping walk goes round on.
    const bool r = tickMachine(dt, code);
    tickEffects();
    return r;
}

bool CefChannel::tickMachine(float dt, std::uint32_t code) {
    ++stats_.ticks;
    if (cur_ < 0) return false;
    const auto& S = ctl_->states;
    const int from = cur_;
    const std::uint32_t stateFlags = S[static_cast<std::size_t>(from)].flags;

    // ---- the input pass ------------------------------------------------
    //
    // The engine polls the device here (`sub_43E080` / `sub_4A7A20`); the
    // replica is handed the word instead, because that is exactly what
    // `Perso_InjectInput` does for the AI and the machine below cannot tell
    // the two apart. `sub_4A7A20` never yields 0 - nothing held is
    // `kIdleInput` - so the one 0 that arrives here is `kQueueDrives`, the
    // documented "no device this tick" contract of `channel.h`, standing in
    // for the flag-0x80 path where the engine skips the poll altogether. It
    // is counted rather than silent.
    std::uint32_t word = code;
    if (word == kQueueDrives) {
        ++stats_.queueDriven;
        if (!queue_.empty()) word = queue_.front();
    }
    if (word && (word & ~(kInputBits | kIdleInput | kNoInputEdge)))
        ++stats_.foreignInput;

    if (!(flags_ & 0x81u) && word) {
        // the 20-slot latch: an input already consumed by the state that
        // recorded it is masked out of the word before the search
        for (int i = 0; i < 20 && word; ++i) {
            if (!latch_[i]) continue;
            if (inputMatches(latch_[i], word)) word = ~latch_[i] & word;
            else latch_[i] = 0;
        }
        // The walker's own edges: (2, 784) picks ordinary movement, and the
        // loop repeats while a transition carries an apply-on-transition turn,
        // shift or special move, each of which consumes its own input bits.
        int guard = 0;
        for (;;) {
            if (++guard > kChainGuard) { ++stats_.chainAborted; break; }
            const int t = findTransition(from, word, 2, 784, true);
            if (t < 0) break;
            const CtlState& e = S[static_cast<std::size_t>(t)];
            bool consumed = false;
            // `sub_45C080` / `Cef_ApplyRootShift`, NOT the on-transition
            // `Cef_ApplyTurn` of `gotoMove` above: the block read is the
            // CANDIDATE's (`u32(v15, 44) + 8`) and it is a RATE, scaled by the
            // frame dt. Emitted as the Rate kinds so the controller cannot
            // apply the wrong record - which is what it did until 2026-09-03,
            // reading `from`'s turn (zero, in H_WALK) and so turning by
            // nothing. This loop IS the diagonal walk.
            if (e.flags & 0x100u) { if (!(flags_ & 0x200u) && e.hasTurn)
                                        events_.push_back({ChannelEvent::Kind::TurnRate,
                                                           from, t, 0, 0, word, {}});
                                    consumed = true; word &= ~e.inputCode; }
            if (e.flags & 0x200u) { if (!(flags_ & 0x200u) && e.hasShift)
                                        events_.push_back({ChannelEvent::Kind::ShiftRate,
                                                           from, t, 0, 0, word, {}});
                                    consumed = true; word &= ~e.inputCode; }
            if ((e.flags & 0x10u) && (e.flags & 0x200000u)) {
                events_.push_back({ChannelEvent::Kind::Move, from, t, 0, 0,
                                   word, e.moveName});
                consumed = true; word &= ~e.inputCode;
            }
            // `v16[2]` in the original is entry `+8` - the flags - not the
            // role word at `+12`. The two are one dword apart and reading the
            // wrong one here would pop the queue on a role code.
            if (e.flags & 0x10000000u) lastInput_ = 0;
            if (e.flags & 0x100000u) popQueue();
            if (e.flags & 0x1000000u) resetQueue();
            if (!consumed) break;
        }
        // ---- the commit, transcribed whole ------------------------------
        //
        // `Cef_TickChannel` 0x004A853A..0x004A85B3, and the two rules the
        // first port of this block dropped are the middle of it:
        //
        //     loc_4A853A: mov  ecx, [esp+40h+var_30]   ; the word
        //                 test ecx, ecx
        //                 jnz  short loc_4A854B
        //                 mov  ecx, 40000000h          ; wholly consumed ->
        //                 mov  [esp+40h+var_30], ecx   ; the idle word
        //     loc_4A854B: cmp  ecx, [esi+14h]          ; == lastInput_ ?
        //                 jz   loc_4A81E4              ; nothing to commit
        //                 mov  [esi+14h], ecx          ; lastInput_ = word
        //                 mov  eax, [esi+18h]          ; n = queue count
        //                 cmp  eax, 0Fh
        //                 ja   loc_4A81E4              ; n > 15: drop the word
        //                 mov  edx, [esi+0B8h]         ; the CURRENT entry
        //                 test dword ptr [edx+8], 20000000h
        //                 jz   short loc_4A8583
        //                 cmp  eax, 1
        //                 jbe  short loc_4A8586
        //                 mov  dword ptr [esi+18h], 1  ; (2) count = 1
        //                 jmp  loc_4A81E4
        //     loc_4A8583: cmp  eax, 1
        //     loc_4A8586: jnz  short loc_4A859A
        //                 test dword ptr [esi+1Ch], 40000000h
        //                 jz   short loc_4A859A
        //                 mov  dword ptr [esi+1Ch], 0  ; (1) queue[0] = 0
        //                 xor  eax, eax                ;     n = 0
        //     loc_4A859A: test eax, eax
        //                 jz   short loc_4A85A6
        //                 jbe  short loc_4A85AB        ; dead: CF is 0 here
        //                 cmp  [esi+eax*4+18h], ecx    ; queue[n-1] == word ?
        //                 jz   short loc_4A85AB
        //     loc_4A85A6: mov  [esi+eax*4+1Ch], ecx    ; queue[n++] = word
        //                 inc  eax
        //     loc_4A85AB: mov  [esi+18h], eax
        //
        // (+18h is the count, +1Ch the first word, so `[esi+eax*4+18h]` is
        // `queue[n-1]` - the back - and `[esi+eax*4+1Ch]` is `queue[n]`.)
        //
        // **(1) a LONE IDLE WORD IS DROPPED.** `setBankGroup` and the entry
        // flags 0x1000000/0x4000000 all seed the queue with exactly
        // `[kIdleInput]`, and a word pushed behind it would wait for something
        // to pop it - the queue pass reads only the FRONT. Dropping it is what
        // makes a press act on the tick it arrives. The test is on the BIT,
        // not on equality with the constant.
        //
        // **(2) under the current entry's flag 0x20000000, a queue longer than
        // one is CUT to one** and the word is not pushed at all: the state
        // refuses to accumulate a buffer of presses behind it.
        //
        // Neither rule can be seen by a sweep that injects whole queues, which
        // is why both survived `engine: actor states` (todo/actor-runtime.md 1).
        if (findTransition(from, word, -1, -1, true) >= 0 ||
            !(stateFlags & 0x1000u)) {
            if (!word) word = kIdleInput;
            if (word != lastInput_) {
                lastInput_ = word;
                std::size_t n = queue_.size();
                if (n <= 15) {
                    // `[esi+0B8h]` is the current entry; `cur_` has not moved
                    // since `from` was taken - nothing above commits a
                    // transition - so `stateFlags` IS `[[esi+0B8h]+8]`.
                    if ((stateFlags & 0x20000000u) && n > 1) {
                        queue_.resize(1);
                        ++stats_.queueTruncated;
                    } else {
                        if (n == 1 && (queue_[0] & kIdleInput)) {
                            queue_.clear();
                            n = 0;
                            ++stats_.queueLoneIdleDrops;
                        }
                        if (n == 0 || queue_.back() != word)
                            queue_.push_back(word);
                    }
                }
            }
        }
    }

    // ---- the frame, and what the end of the clip does --------------------
    const bool finished = clipLen_ <= frame_;
    if (stateFlags & 0x8001u) {
        int taken = -1;
        int mustHave = -1;
        bool stepQueue = false;
        if (!queue_.empty()) {
            mustHave = (flags_ & 2u) ? 4 : (finished ? -1 : 0x80000000);
            stepQueue = (stateFlags & 8u) != 0;
            if (stepQueue) {
                while (!queue_.empty()) {
                    taken = findTransition(from, queue_.front(), mustHave, -1, false);
                    if (taken >= 0) {
                        const CtlState& e = S[static_cast<std::size_t>(taken)];
                        if ((e.cancelFrom != 0.0f || e.cancelTo != 0.0f) &&
                            (frame_ < e.cancelFrom || prev_ > e.cancelTo ||
                             (prev_ < e.cancelFrom && frame_ < e.cancelTo))) {
                            taken = -1;
                        }
                        break;
                    }
                    popQueue();
                }
            } else {
                taken = findTransition(from, queue_.front(), mustHave, -1, true);
            }
        }
        if (taken >= 0 && taken != gotoOf(from)) {
            edges_.push_back({from, taken, queue_.empty() ? 0u : queue_.front(),
                              (flags_ & 0x400u) != 0, priorityThreshold_, true,
                              mustHave, -1, !stepQueue, prev_, frame_});
            return gotoMove(from, taken, 1.0f);
        }
        if (finished) {
            ++stats_.clipEnds;
            if (flags_ & 2u) {                     // a blend landed
                const float sf = pendingFrame_;
                pendingFrame_ = 1.0f;
                return gotoMove(from, pending_, sf);
            }
            flags_ |= 0x10u;
            return gotoMove(from, gotoOf(from),
                            S[static_cast<std::size_t>(from)].startFrame);
        }
    } else {
        if (stateFlags & 0x400u) {
            int t = -1;
            if (!queue_.empty())
                t = findTransition(cur_, queue_.front(), -1, -1, true);
            if (t >= 0) {
                edges_.push_back({cur_, t, queue_.front(),
                                  (flags_ & 0x400u) != 0, priorityThreshold_, true,
                                  -1, -1, true, prev_, frame_});
                return gotoMove(cur_, t, 1.0f);
            }
            if ((stateFlags & 0x40000u) && !queue_.empty() &&
                queue_.front() == S[static_cast<std::size_t>(from)].inputCode) {
                queue_.clear();
                lastInput_ = 0;
            }
        }
        if (finished) {
            ++stats_.clipEnds;
            return gotoMove(cur_, cur_, 1.0f);
        }
    }

    prev_  = frame_;
    if (!(flags_ & 0x200u)) frame_ += dt;
    return true;
}

// `Cef_TickEffects` (0x0045ADF0), the sound half. Its test is
// `if (soundId && !(flags & 1) && effectClock >= record[+12])`, then
// `Scene_FindSoundIndex` and `Sound_Play3D` - so a record fires once and is
// then latched, the engine's latch being a pair of runtime words on the effect
// instance. What re-arms it is the paragraph below, and it is the part that
// matters: `H_WALK` loops without being re-entered, so a latch that only the
// state change clears makes a walk fall silent after two steps.
//
// The SPRITE half (`Cef_SpawnEffect`) is not done here: it wants the scene's
// chunk-4 registry and an attach point on the live skeleton, and the particle
// field it feeds already has an owner in `o3de/particles.h`. Sound is what a
// player hears missing.
void CefChannel::tickEffects() {
    sounds_.clear();
    if (cur_ < 0) return;
    // RE-ARM. A latch cleared only on a state change fires `H_WALK`'s pair
    // once and then goes silent for as long as you walk - 2 footfalls in 300
    // frames - because the state loops its clip without ever being re-entered.
    // The engine re-arms: `Cef_TickEffects`'s tail zeroes the instance's two
    // latch words when its effect clock leaves the record's window. These
    // records carry an OPEN window (0..0), and what that path reduces to for
    // them is not traced here - so the rule below is a RECONSTRUCTION, chosen
    // because it is the one thing that is certainly true of a looping clip and
    // is what the sound is for: the frame going backwards is a wrap, and a
    // wrap starts the footfalls again.
    if (cur_ != fxState_ || frame_ < fxFrame_) { fxState_ = cur_; firedFx_.clear(); }
    fxFrame_ = frame_;
    const auto& fx = ctl_->states[static_cast<std::size_t>(cur_)].effects;
    for (std::size_t i = 0; i < fx.size(); ++i) {
        const auto& e = fx[i];
        if (!e.sound) continue;
        if (frame_ < e.soundAt) continue;
        if (!firedFx_.insert(static_cast<int>(i)).second) continue;
        sounds_.push_back({e.sound, e.attach});
    }
}

int CefChannel::findEntryByCode(std::int32_t code) const {
    // Cef_FindEntryByCode matches the LOW SIXTEEN BITS of an entry id. That
    // works only because the low-16 space is collision-free in every shipped
    // file - a property of the content, which the format does not enforce and
    // the sweep asserts.
    const auto want = static_cast<std::uint32_t>(code) & 0xFFFFu;
    for (std::size_t i = 0; i < ctl_->states.size(); ++i)
        if ((ctl_->states[i].id & 0xFFFFu) == want) return static_cast<int>(i);
    return -1;
}

int CefChannel::findEntryByRole(std::int32_t role) const {
    for (std::size_t i = 0; i < ctl_->states.size(); ++i)
        if (ctl_->states[i].role == role) return static_cast<int>(i);
    return -1;
}

}  // namespace omk
