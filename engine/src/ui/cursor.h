// SPDX-License-Identifier: GPL-3.0-or-later
// `Ui_DrawItemCursor` - THE HIGHLIGHT, and it is what tells a player where
// they are.
//
// Item flag `0x40000200`, bank B - by far the commonest decoration in the
// tree, and unported until 2026-09-04, when a player said of the sneak that
// "the hovering effect is absent so it is very difficult to know where I am".
// It is not a box and not a fill: `sub_479920` drives a pool of SIXTEEN
// elements that chase the focused item and orbit it.
//
// THE POOL is one global (`dword_6A4D20`), so exactly one cursor is on screen
// at a time, and its layout closes exactly on the 220 dwords the earlier read
// measured - 0x30 of header plus 16 x 0x34:
//
//     +0x00 cx   +0x04 cy   +0x08 w   +0x0C h   +0x10 colour
//     +0x14 item                                +0x2C flags
//
//     element (0x34):
//     +0x00 x      +0x04 y       the eased position
//     +0x08 drawX  +0x0C drawY   that position plus this frame's orbit
//     +0x10 sizeFactor
//     +0x14 w      +0x18 h       the eased size
//     +0x1C lerpSpeed
//     +0x20 rx     +0x24 ry      the orbit's radii
//     +0x28 angle  +0x2C angularSpeed
//     +0x30 colour
//
// 0x30 + 16*0x34 = 880 = 220 dwords. The size was known before the layout
// was, so the layout is a parse the data could have failed.
//
// THE FRAME, from `sub_479920`:
//   * if (cx, cy, w, h) differ from the pool's, rebuild (`sub_478FE0`);
//   * `sub_479220` eases each element's x/y toward the centre, then copies
//     x/y into drawX/drawY - which is why the orbit below does not
//     accumulate, and the reason to read that tail rather than assume;
//   * `sub_479340` eases each element's w/h toward the item's size;
//   * flag 0x400: angle += angularSpeed * dt, wrapped to [0, 360), then
//     drawX += rx*cos(angle), drawY += ry*sin(angle). The wrap is in DEGREES
//     and `fcos`/`fsin` take RADIANS - the engine does not convert, so this
//     is a wobble rather than a revolution. Ported as written.
//   * the colour is the item's own `+8/+9/+10` (so the sneak's pages tint
//     their own cursor), or (255, 50, 50) on the `0x42000000` arm;
//   * THE ALPHA IS OSCILLATOR 3 - period 500, lo 230, hi 235, a TRIANGLE
//     (`sub_42B700`: lo->hi over the first half, hi->lo over the second).
//     Not oscillator 2's 45..200, which is what this was first assumed to
//     be; at that alpha each quad would be nearly opaque and the highlight
//     would blow out. The number matters because the blend is the fill's
//     inverse one, so a HIGH alpha is a FAINT quad: 16 of them at ~0.09
//     each leave the bar at about 80% of the source colour.
//
// THE DRAW (`sub_4795F0`) is one quad per element, `drawX/drawY` +- w/2, h/2,
// through `sub_4285E0(points, 4, 8)` - blend MODE 4, the same
// `src*(1-a) + dst*a` as `Ui_DrawItemFill`, at layer 8.
//
// THE GATE, read at the call site rather than taken from the docs: bank B
// `0x40000200` on the item, then bank A `0x20000001` - FOCUSED - on the item
// AND on its list. So it marks the one row that is both selected and in the
// focused list, which is exactly the row a player is standing on.
//
// TIER 5: every number here is read out of the image, and nothing is checked
// against a capture of the original - a still frame cannot judge sixteen
// eased elements. What a capture does show, and what this reproduces, is a
// bright soft bar the size of the row.
#pragma once

#include <cstdint>
#include <vector>

namespace omk {

class UiCursor {
public:
    struct Quad { int x0, y0, x1, y1, r, g, b, alpha; };

    // One frame for `item`, whose scaled centre is (cx, cy) and size (w, h).
    // `dtMs` advances both the eases and the oscillator. Returns the sixteen
    // quads in submission order.
    const std::vector<Quad>& tick(std::uint32_t item, int cx, int cy, int w, int h,
                                  int r, int g, int b, long dtMs);
    // The oscillator-3 value the last tick used, so a check can assert it.
    int alpha() const { return alpha_; }

private:
    void rebuild(int cx, int cy, int w, int h);
    // `rand()` in the original is the CRT's, seeded by the process. A replica
    // that wants a reproducible check cannot use that, so this is a small LCG
    // seeded from the item address - the ONE deviation in this file, and it
    // changes which pseudo-random offsets the sixteen elements get, not the
    // rule that produces them.
    std::uint32_t rnd();

    struct El {
        int   x = 0, y = 0, drawX = 0, drawY = 0;
        float sizeFactor = 1.0f;
        int   w = 0, h = 0;
        float lerpSpeed = 0.5f;
        int   rx = 0, ry = 0;
        float angle = 0.0f, angularSpeed = 0.0f;
    };
    std::uint32_t item_ = 0, seed_ = 1;
    int  cx_ = 0, cy_ = 0, w_ = 0, h_ = 0;
    std::uint32_t flags_ = 0;
    long oscMs_ = 0;
    int  alpha_ = 230;
    El   el_[16];
    std::vector<Quad> quads_;
};

}  // namespace omk
