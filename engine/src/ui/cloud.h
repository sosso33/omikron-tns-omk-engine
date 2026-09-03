// SPDX-License-Identifier: GPL-3.0-or-later
// THE MENU'S ANIMATED BACKGROUND - an embossed, warped cloud.
//
// The start menu's own sheet, `I2D/bitmaps/gfxint.bmp`, is the title on a
// uniform fill: every pixel of the area below it is palette index **255**,
// which is rgb(4,4,4). That is the I2D colour key, so the sheet is
// TRANSPARENT there and something on a lower layer shows through. This is
// that something, and until it was found the replica drew the menu on black.
//
// The chain, traced from screen 29's open callback (0x00479D10):
//
//     sub_4B19C0()                       right after the F1Avnt.CTL load
//       sub_4B2220("IMAGES\CLOUD.BMP", &ramp)   the texture AND the ramp
//       malloc(0x20000)                  256*256*2 - the 16-bit work buffer
//       CreateSurface(0x280 x 0x1E0)     a 640x480 offscreen surface
//
// and then, per frame, `sub_4B1B00` runs TWO PASSES - and folding them into
// one is what went wrong three times over:
//
//   1. **Emboss the cloud at 256x256** into the work buffer (which is what the
//      0x20000 malloc is: 256*256*2). A light at
//      `(cos(t)*64 + 128, sin(t)*64 + 128)`, `t` advancing 0.0785 a frame -
//      2*pi/80, an 80-frame turn - gives two weights that DECREMENT, one per
//      pixel and one per row, from `255 - light`. Over 256 they stay small,
//      which is why the result never saturates. Each pixel takes the cloud's
//      x and y gradients as SIGNED BYTES, dots them with the weights, shifts
//      by 5, adds 32, clamps to 0..63 and looks up the ramp.
//   2. **Resample that 256x256 buffer to 640x480 through the warp.** The X
//      source comes from the 480-entry ROW table and the Y source from the
//      640-entry COLUMN table:
//
//          al = rowTab[479 - y]        then `inc al` per pixel
//          ah = colTab[639 - x] + y    `bl` being a per-row `inc`
//          out = buf[ah * 256 + al]
//
//      This is the WAVE. The buffer is 256 wide against a 640-wide screen so
//      it repeats - but each row is displaced horizontally and each column
//      vertically, which breaks the repeats up instead of lining them into
//      squares.
//
// **Four attempts, three of them written down as fact.** The tables were first
// read as per-pixel offsets into the CLOUD on their own axes (a smooth flow);
// then, when a window of the decompiled renderer showed no read of them, they
// were dropped entirely and documented as "there is no warp" (flat tiling);
// then applied crossed but still to the cloud, in one pass (hard 256-pixel
// seams). A reader watching the original rejected each: "deformed squares",
// then "it should create a wave feeling", then "normal squares are not
// supposed to be recognizable". What settled it was reading the RAW ASSEMBLY
// of the whole 382-line function instead of a decompiled excerpt - the second
// pass is there in plain `mov ah, byte_6A0630[edx*4]`, and the decompiler had
// named those reads off neighbouring addresses so every grep for the table's
// own symbol came back empty.
//
// **The ramp is built from constants in the image, not from the .bmp** (whose
// own palette is greyscale). Two halves of 32, from 0x004B22D0:
//
//     first  i: R=(4049 - 111i)>>5  G=(947 - 13i)>>5  B=(13 + 13i)>>5
//     second i: R=(608  +  19i)>>5  G=(544 + 94i)>>5  B=(416 + 92i)>>5
//
// packed `high<<16 | mid<<8 | low` and handed to `Color_Sum` - which takes a
// COLORREF, so the LOW byte is RED. Read the other way the ramp comes out
// blue-to-olive; read correctly it runs **rust -> dark -> teal**, and 97% of
// the captured menu's background pixels land within 24 of one of its entries.
//
// Tier 2, corpus-constrained: the ramp and the texture are the shipped bytes
// and the phases are the image's own constants, so the data could fail this.
// What is NOT established is the exact frame the capture was taken on - the
// animation has no phase reference in a still - so a check may assert the
// ramp and the coverage and must not assert a pixel.
#pragma once

#include "platform/datafs.h"
#include "ui/surface.h"

#include <cstdint>
#include <vector>

namespace omk {

class MenuCloud {
public:
    // -> false when `IMAGES/cloud.bmp` is not there; the caller then draws
    // what it drew before, which is black.
    bool load(const DataFs& fs);
    bool valid() const { return !tex_.empty(); }

    // One frame into `fb`, which must be 640x480. `frame` is the frame
    // counter: every clock in the engine is in thirtieths of a second
    // (docs/BOOT.md 4) and these phases are per-frame steps, so this is a
    // count and not a duration.
    void draw(Surface& fb, long frame) const;

    // The 64 colours, for a check to look at.
    const std::vector<std::uint16_t>& ramp() const { return ramp_; }
    static void buildRamp(std::vector<std::uint16_t>& out);

private:
    std::vector<std::uint8_t>  tex_;    // 256x256, the cloud's own indices
    std::vector<std::uint16_t> ramp_;   // 64 entries, RGB565
};

}  // namespace omk
