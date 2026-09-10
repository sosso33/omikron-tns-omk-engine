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
// be in the water state in shoot mode. Mode 1 (the horizontal bar and
// `jaugeg.bmp`) and mode 2's `sub_447000` are not ported - nothing in shoot
// mode asks for them.
#pragma once

#include "platform/datafs.h"
#include "ui/surface.h"

#include <cstdint>

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
};

class HudBar {
public:
    // `Hud_LoadResources` (0x004490D0): the three bitmaps, and jauge1's size
    // into `dword_530C98/9C`.
    bool load(const DataFs& fs);
    bool loaded() const { return jauge_[0].valid(); }
    // `sub_446C40(side, y)`, for the display width `screenW`.
    void refresh(int side, int y, int screenW);
    // `Hud_DrawBar(value, max, side, mode)`; mode 0 only.
    HudBarFrame draw(Surface& fb, int value, int max, int side, int mode);
    const HudSpark& spark(int side, int i) const { return sparks_[side & 1][i]; }

private:
    int rand_();
    int gauge(Surface& fb, int percent, int side, HudBarFrame& out);
    int sparks(Surface& fb, int top, int side);

    Surface jauge_[2];                 // jauge1 / jauge2
    Surface jaugeG_;                   // mode 1's, loaded as the engine does
    int bmpW_ = 0, bmpH_ = 0;          // dword_530C98 / dword_530C9C
    int counter_ = 0;                  // dword_531050
    int column_  = 0;                  // dword_530CA8
    HudSpark sparks_[2][kHudSparks];
    std::uint32_t seed_ = 1;
};

}  // namespace omk
