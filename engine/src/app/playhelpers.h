// SPDX-License-Identifier: GPL-3.0-or-later
// THE VIEWER'S FILE-LEVEL HELPERS, moved out of `backends/sdl/play.cpp`
// (`todo/play-split.md` S1, 2026-09-22).
//
// These four were the top of that file and they are the easy half of the
// split: each is a pure function of its arguments, none touches SDL, and none
// touches any of `main`'s 468 top-level names - which is why they moved first
// and why the move is checkable by `scripts/play-golden.sh` alone.
// `src/app/` is the VIEWER's own layer rather than the engine's: the audio
// conversion below belongs to the frontend (`PORTING` A8 rule 3) and stays the
// frontend's here, one directory away from `src/audio/`.
//
// NOTHING WAS REWRITTEN ON THE WAY - the plan's own rule. The one textual
// change is that `drawSubtitleBox` reaches the overlay planes through
// `omk::overlayPlanes()` instead of `play.cpp`'s `g_ov`, which is a reference
// to that same object.
#pragma once

#include "o3de/renderer.h"
#include "ui/overlay.h"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace omk {

// ------------------------------------------------------ THE INTERFACE SOUNDS
//
// `docs/UI.md`: every screen names up to twelve sounds and the slots are
// POSITIONAL - **slot 0 is the selection move, slot 1 the confirm, slot 2 the
// screen opening**. The start menu's are 1, 2, 0, which the table resolves to
// `men002`, `men003`, `men001`. Both halves were already lifted; nothing was
// playing them, which is what a player reported.
//
// The samples are the engine's own 22050/16-bit mono or stereo and the device
// is running at the movie's 44100 float stereo, so they are converted here.
// That conversion is the FRONTEND's: `PORTING` A8 rule 3 forbids a dependency
// doing work a reference implementation ports, and there is no ported mixer to
// step on - `Sound_Init` hands a DirectSound primary buffer to the driver and
// DirectSound sums into it, so the summing never had a portable half
// (`src/audio/mixer.h`).
// An angle difference on the SHORT arc, wrapped to (-180, 180]. A camera roll
// is stored 4096-per-turn and a small negative one reads as ~+359; the two are
// the same rotation standing still and a full turn apart once interpolated
// (CLAUDE.md 1, and `verify.py: camera roll`).
float shortArc(float deg);


std::vector<float> wavToDevice(std::span<const std::byte> file, int deviceRate);

// THE DIALOGUE SUBTITLE.
//
// The BOX is the engine's, read out of `Dialog_TickUI` (0x0046A200) rather
// than chosen:
//
//     v3 = height << 6
//     Text_DrawBlock(32, 0, (width - 64) + 32, v3 / 480, text, params)
//     dword_6A52C4 = height - v3 / 480        <- where it is placed
//     dword_907961 = 0xFFFFFF                 <- the ink
//     off_4C71A8   = &unk_808080              <- the second colour
//
// `Text_DrawBlock(left, top, right, bottom, ...)`, so at 640x480 that is a
// block inset **32 pixels from each side** and **64 tall**, placed against the
// BOTTOM of the screen - which `Text_DrawBlock`'s own docstring says in as
// many words ("0x0041E040 measures once to place a subtitle against the bottom
// of the screen"). Both numbers scale with the display: `width - 64` and
// `height * 64 / 480`.
//
// **What is NOT recovered**: the reply menu has no recovered placement at all
// and is stacked under the line, with the selected row in the ink and the rest
// in the 0x808080 the engine also loads. So the BOX is read from the engine
// and the STACK inside it is a reconstruction; they are labelled differently
// on purpose. The WRAP is no longer among them - `Text_LayOutBlock` is ported
// (2026-09-07) and `drawSubtitle` lays every string through it.
//
// `wrapInto` was this file's own greedy break on spaces and has gone with it.

// THE SUBTITLE BOX, and both of its blends.
//
// `Dialog_TickUI` draws no box - it makes no call but `Text_DrawBlock`. The
// box is the TEXT RENDERER's: `sub_4400D0` (0x004400D0), called from
// `Game_Tick` as `sub_4400D0(0, dword_6A52C4, dword_6A52C0, height - 1, ...)`,
// submits one quad before the glyphs and switches on `off_4C71A8`, the second
// colour `Dialog_TickUI` sets:
//
//     off_4C71A8 == 0x80002040  ->  flags 4, top = a2 - 32      the REPLIES
//     off_4C71A8 == 0x00808080  ->  flags 2, top = a2 - 4       the LINE
//     x 18 .. width-18,  y (top - 8) .. height - 18
//     I2D_SubmitQuad(&v21, flags, 10)
//
// `I2D_SubmitQuad` copies 48 bytes - four vertices of (x, y, colour) plus the
// flag word - and `sub_480BD0` fills all four corners from the FIRST one
// unless flag 8 is set, which neither of these sets: both are a flat fill.
//
// The flags are the BLEND, through `sub_480AC0`, which sets D3D render states
// 19 (SRCBLEND) and 20 (DESTBLEND):
//
//     & 1   src 2 ONE,          dst 2 ONE            additive
//     & 2   src 1 ZERO,         dst 4 INVSRCCOLOR    dst *= (1 - src)
//     & 4   src 6 INVSRCALPHA,  dst 5 SRCALPHA       src*(1-a) + dst*a
//
// So the LINE's box is `dst * (1 - 0x808080/255)`, a 50% DARKENING - which
// over the black letterbox band is invisible, and is why a reader watching
// the original could not say whether a plain subtitle had a box. The REPLY
// box is 50% of the navy 0x002040. Each is gated on a driver-capability test
// (`sub_464730/40/50`) that can clear the bit and blank the colour; both are
// supported here.
enum class SubBox { None, Line, Replies };
void drawSubtitleBox(Surface& fb, SubBox kind, int top, int dispW, int dispH);

// cp1252 to UTF-8, for the CONSOLE only. The dialogue pool is cp1252 - which
// is what `FONTS/*.FNT` is indexed by, so the strings stay cp1252 everywhere
// that matters and this converts a copy on its way to a terminal.
std::string cp1252ToUtf8(const std::string& in);

}  // namespace omk
