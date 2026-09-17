// SPDX-License-Identifier: GPL-3.0-or-later
// THE HEALTH GAUGE - `Hud_DrawBar` (0x00447B10), mode 0.
//
// Shoot mode's screen 34 draws it from its full-screen item's callback
// (0x42E870: `Hud_DrawBar(dword_90E100, 200, 0, 0)` after the crosshair), and
// `dword_90E100` is the PLAYER's shoot record `+92` - `Shoot_SyncHudHealth`
// (0x00424880) copies it every step of the mode - which `Shoot_Enter`'s own
// `sub_422540(player)` set from his property 1, a 0 rewritten to 10.
//
// The function clamps the value to [0, max], takes `p = 100 * v / max`,
// advances a frame counter into the SCROLLING COLUMN
// `dword_530CA8 = ++dword_531050 % (W - 3)` (W the width of `jauge1.bmp`),
// and for mode 0 calls two helpers, each read here in full:
//
// * `sub_4480D0(p, side)` - the GAUGE, a vertical bar at x = 24 (mirrored to
//   W - 24 for side 1) from y = 22 to 458, all in 640x480 units through
//   `Hud_ScaleX/Y`:
//     layer 2, flags 4, colour 0 - a DIAMOND of radius 17 at each end and the
//       14-wide column between them: the black frame;
//     layer 3, key source - two blits from `jauge1.bmp` (`jauge2` for side 1)
//       into the 6-wide channel: the EMPTY part from source column 0..3 and
//       the FULL part from the scrolling column, each stretched from its share
//       of the bitmap's 512 rows;
//     layer 4, flags 2, colour 0xE0E0E0 - a quad over the empty part;
//   and returns the fill's TOP EDGE.
// * `sub_446E20(top, side)` - 35 SPARKS per side, each `{x, y, age}` in
//   `unk_530E88` (side 0) / `unk_530CE0` (side 1): x drifts 0..2 outward, y
//   rises 0..3, age climbs 0x04000000 a frame; one that leaves the screen's
//   2-pixel margin or passes age 0xF0000000 is reborn at the top edge,
//   x = 24 +- 1. The live ones are radius-2 diamonds of `age | rgb`, the rgb
//   `dword_4C79F0[side]` = 0x18AD8C / 0x8C1008, layer 5, flags 4. It skips
//   the whole walk when `top == 458` - a LITERAL, so at any display but
//   640x480 an empty bar still sparks. Transcribed, not tidied.
//
// `Hud_Refresh` (0x00448FA0), which `Shoot_Enter` calls, seeds both sides
// with `sub_446C40(side, 22)`: every spark at y = 22, x = 24 +- 1, age
// 0x20000000.
//
// **Which back end.** An I2D quad has two drawers (`sub_480BD0`): the
// software rasterizer's bounding-box fill (`quadMode`/`fillQuad`,
// `ui/surface.h`) and the Direct3D one. This follows the D3D arm, which is
// what a player with a 3D card saw and what `ui/screendraw.cpp`'s item fill
// was confirmed against: `sub_480AC0` turns bits 0..2 into the blend pair
//     flags & 1 -> ONE, ONE                  (add)
//     flags & 2 -> ZERO, INVSRCCOLOR         dst * (1 - src)
//     flags & 4 -> INVSRCALPHA, SRCALPHA     src * (1 - a) + dst * a
// so the frame (alpha 0) is opaque black, the empty part is darkened to
// 31/255 of the jauge beneath it, and a spark FADES as its age grows. The
// polygon is the four points' own, not their bounding box: the diamonds are
// diamonds.
//
// **Two things are reconstruction, and labelled.** `rand()` is the MSVC CRT's
// generator (`seed * 214013 + 2531011`, bits 16..30) on a seed of this
// module's own - the engine's is shared with everything else, so the SEQUENCE
// cannot match and only its distribution does. And the swim-state rescale
// every coordinate carries (actor `+404 == 14`) is left out: the player cannot
// be in the water state in shoot mode. Mode 1 (the horizontal bar) is not
// ported - nothing asks for it.
//
// **Mode 2 is mode 0 plus the STAT CARD** (`sub_447000`, 2026-09-17), which
// melee asks for: `Fight_UpdateHealthBars` (0x00445160) draws
// `Hud_DrawBar(player, 200, 0, 2)` and `(opponent, 200, 1, 0)` every fight
// frame outside a KO replay, and mode 2 calls `sub_447000` first while
// `Sys_GetTimeMs() < dword_531030 + 4000` - the four seconds after
// `Hud_Refresh`, which `Fight_Begin` calls. The card is six rows at
// y = 348 + 20 * row, all in 640x480 units:
//
// * the values are `Hud_Refresh`'s snapshot of the PLAYER's properties, in
//   `dword_530CB0[0..5]`: 16, 19 / 41, 17, 3, 18, 2;
// * the labels are `IAM\SNEAK` strings 26..31 (`dword_4C79F8`) with every
//   character removed that is not `_UPPER` - the loader's `isctype(c, 1)`,
//   `ebp` = 1 in the listing - so "Attaque" is drawn as "A". They go in the
//   box x 46..66 (70 - 2*12 .. 70 - 4), y - 8 .. y + 24, right-aligned in font
//   'C' (`params` = {0x24, -1,-1,-1, 'C'}: TEXTP_ALIGN_4 | TEXTP_SLOT2);
// * row 1 draws no bar: its value is an index into the RANK names, `IAM\SNEAK`
//   strings 36..40 (`sub_49C9D0`), in the box x 70..510, left-aligned
//   (`params[0]` = 0x22);
// * every other row draws a black frame (layer 2, flags 4) over x 70..170,
//   y +- 4; a `jaugeg.bmp` blit (layer 3, key source) of source rows
//   `column..column+3` and source columns `H*(200-v)/200 .. H` (H being
//   jauge1's HEIGHT, `dword_530C9C`) into x 70 .. 70 + v*100/200, y +- 2; and
//   an ADDITIVE quad over the same rectangle (layer 3, flags 1) in the row's
//   colour from `dword_4C7A10`.
//
// Inside layer 3 the HEAD cache draws the first node first and the rest in
// REVERSE (docs/UI.md): row 0's blit, then rows 5..2 as tint-then-blit, then
// row 0's tint last. The gauges' own layer-3 blits fall between, but they do
// not overlap the card. What is NOT known is what else the frame put on layer 3
// before the card; this assumes nothing did, and is labelled as that. A rank
// index outside 0..4 reads past `dword_531038`'s five pointers in the engine;
// here it draws no rank, labelled. And `_pctype` at a byte >= 0x80 is indexed
// NEGATIVELY by the engine's `movsx`; no shipped label needs it, and such a byte
// is simply dropped here.
//
// **Mode 1 is the BREATH gauge** (2026-09-17, `todo/swimming.md` step 4), and
// what asks for it is the water: `sub_4A8F30`'s underwater arm calls
// `Hud_DrawBar(1000 * (start + 40000 - now) / 40000, 1000, 0, 1)` every tick
// until the 40 seconds are up, having called `Hud_Refresh` on the first. It is
// a HORIZONTAL bar and shares nothing with mode 0's code but the counter:
//
// * layer 2, flags 4, colour 0 - a DIAMOND of radius 17 at (100, 24) and at
//   (540, 24), and the 14-high column between them;
// * layer 3, key source - two blits from `jaugeg.bmp` (`dword_53104C`, the
//   stat card's bitmap, not jauge1): the FULL part reads source columns
//   0..W*p/100 at the SCROLLING row, the empty part reads source row 0..3 from
//   column W*p/100 on. W and the row are jauge1's size, which is what the
//   engine indexes jaugeg with - transcribed, not corrected;
// * layer 4 - `0xE0E0E0` with flags 2 over the empty part (darkened to 31/255
//   of the jauge), then `0x103080` with flags 1 - ADDITIVE, and a water blue -
//   over the full part. Mode 0 has no such tint.
//
// No sparks. And the engine takes the column's vertical centre from
// `Hud_ScaleX(24)`, not `Hud_ScaleY` - kept, because at 640x480 they agree and
// at any other display the engine's own bar is what a replica must draw.
#pragma once

#include "platform/datafs.h"
#include "ui/surface.h"

#include <cstdint>
#include <string>

namespace omk { class TextLayout; }

namespace omk {

// One I2D quad through the D3D arm: the convex polygon of the four points,
// pixel centres on integers with the top-left fill rule, vertex 0's colour
// (flags without 0x8 are flat), blended by bits 0..2 as above.
// -> the number of pixels written.
long fillQuadD3d(Surface& dst, const int x[4], const int y[4],
                 std::uint32_t argb, std::uint32_t flags);

inline constexpr int kHudSparks = 35;

struct HudSpark { int x = 0, y = 0; std::uint32_t age = 0; };

struct HudBarFrame {
    int  percent  = 0;    // p
    int  top      = 0;    // sub_4480D0's return
    int  column   = 0;    // dword_530CA8
    int  quads    = 0;    // gauge quads drawn
    int  blits    = 0;    // jauge blits accepted
    int  sparks   = 0;    // sparks drawn this frame
    int  cardRows = 0;    // stat-card bars drawn (mode 2's `sub_447000`)
    int  cardText = 0;    // stat-card text blocks laid out
};

// The six rows' colours, `dword_4C7A10`
inline constexpr std::uint32_t kHudCardRgb[6] = {0xFF2ABAu, 0x838EFFu, 0xDFFF87u,
                                                 0x65CFFFu, 0xFF9547u, 0x83FFD7u};

class HudBar {
public:
    // `Hud_LoadResources` (0x004490D0): the three bitmaps, and jauge1's size
    // into `dword_530C98/9C`.
    bool load(const DataFs& fs);
    bool loaded() const { return jauge_[0].valid(); }
    // `sub_446C40(side, y)`, for the display width `screenW`.
    void refresh(int side, int y, int screenW);
    // `Hud_Refresh`'s other half: the player's six properties (16, 19, 17, 3,
    // 18, 2 - the raw values; the 19 is divided by 41 here, as the engine
    // does) and the clock it stamps into `dword_531030`.
    void refreshCard(const int props[6], double nowMs);
    // `Hud_DrawBar(value, max, side, mode)`; modes 0, 1 and 2. Mode 2's card
    // needs `text` and the current clock; without `text` it is skipped.
    HudBarFrame draw(Surface& fb, int value, int max, int side, int mode,
                     const TextLayout* text = nullptr, double nowMs = 0.0);
    const std::string& cardLabel(int row) const { return labels_[row % 6]; }
    const std::string& rankName(int i) const { return ranks_[i % 5]; }
    int cardValue(int row) const { return card_[row % 6]; }
    const HudSpark& spark(int side, int i) const { return sparks_[side & 1][i]; }

private:
    int rand_();
    int gauge(Surface& fb, int percent, int side, HudBarFrame& out);
    int sparks(Surface& fb, int top, int side);
    void card(Surface& fb, const TextLayout& text, HudBarFrame& out);
    void breath(Surface& fb, int percent, HudBarFrame& out);

    Surface jauge_[2];                 // jauge1 / jauge2
    Surface jaugeG_;                   // jaugeg.bmp - the stat card's and mode 1's
    int bmpW_ = 0, bmpH_ = 0;          // dword_530C98 / dword_530C9C
    int counter_ = 0;                  // dword_531050
    int column_  = 0;                  // dword_530CA8
    HudSpark sparks_[2][kHudSparks];
    std::uint32_t seed_ = 1;
    std::string labels_[6];            // dword_530CC8, filtered to _UPPER
    std::string ranks_[5];             // dword_531038
    int card_[6] = {0, 0, 0, 0, 0, 0}; // dword_530CB0
    double stampMs_ = -1e18;           // dword_531030
};

}  // namespace omk
