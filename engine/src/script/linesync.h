// SPDX-License-Identifier: GPL-3.0-or-later
// THE LINE SYNC - `Game_Frame` (0x0041F740) pulling the simulation along with
// a conversation line's audio (todo/drift-audit.md T2, docs/BOOT.md 4).
//
// `Game_Frame` computes its delta as `30 / fps` capped at 3 frames - below
// 10 fps the game SLOWS DOWN - and then, while `sub_42CC10()` (the morph
// player's line length `flt_4EB10C` is nonzero: a line is loaded) has been
// true on this frame and the last, replaces it with the advance of the line's
// AUDIO clock, `sub_42BC30(0)`: the voice's play position in frames of
// `g_MorphFps`, which both `Morph_SetAudioFormat` callers set to 30.
//
//     edge (off -> on):   flt_4E9704 = clock
//     on, on:             flt_4E9704 += flt_4C30D8      (last frame's delta)
//                         flt_4C30D8 = clock - flt_4E9704 + flt_4C30D8
//                         and 0 when that is <= 0
//
// So the whole simulation catches up with the voice at any frame rate - the
// 3-frame cap does not apply to it - and `sub_42D120` samples the face at the
// same clock. In SECONDS here: the formula is linear, so 30ths and seconds
// give the same answer.
#pragma once

namespace omk {

struct LineSync {
    bool   prev   = false;   // `dword_4E9730`: last frame's `sub_42CC10()`
    double expect = 0.0;     // `flt_4E9704`, seconds
    // `clock` is the line's audio position in seconds, negative when no line
    // plays (or no device can say); `lastDelta` the delta the frame before
    // used; `delta` this frame's, already clamped. -> this frame's delta.
    double step(double clock, double lastDelta, double delta) {
        const bool now = clock >= 0.0;
        if (now && !prev) expect = clock;
        else if (now && prev) {
            expect += lastDelta;
            delta = clock - expect + delta;
            if (delta <= 0.0) delta = 0.0;
        }
        prev = now;
        return delta;
    }
};

}  // namespace omk
