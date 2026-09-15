// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "ui/surface.h"

namespace omk {

// ---- THE INTERFERENCE: `sub_432940` (0x00432940) ---------------------------
//
// A screen's "monitor" box - item draw hook `0x00477ED0`, three items in the
// shipped tree: the sneak's 500x280 page (`0x004DE1A8`), the terminal pages'
// 430x320 (`0x004E4090`, screens 5, 11, 15..19) and MULTIPLAN's 450x260
// (`0x004E5850`). The hook scales the item's rectangle and submits it through
// `sub_4286F0` at the item's layer (7); the drawer `sub_481090` hands the
// rectangle to this function, which LOCKS THE BACK BUFFER and scrambles
// whatever layers 0..6 already put there. Nothing is drawn of its own: the box
// is an effect on the pixels under it.
//
// One frame, in the function's order:
//
//   1. `rand() % 25 == 0`: for (h + 1) / 2 PAIRS of rows from the top, both
//      rows of a pair shifted by one `rand() % 4 - 2` pixels (a `memmove` of
//      w pixels, so the source runs past the rectangle's edge by up to two);
//   2. while the DENSITY (`dword_52B954`) is positive, a scatter: rows step
//      `rand() % (density + 40)`, pixels along a row `rand() % (density + 80)`,
//      each hit set to one of four greys 808080 505050 040404 606060;
//   3. while the BAND flag (`dword_52B950`) is set, eight rows two apart from
//      `y + bandY` (`dword_52B94C`), each shifted by `rand() % 8` less
//      4, 4, 0, 2, 0, 4, 2, 4 pixels, then every `rand() % 4 + 1`-th pixel
//      ORed with 080808 080808 101010 101010 202020 202020 404040 808080;
//   4. the state: bandY += 4; a positive density counts down, and when it is
//      (or has just reached) 0 a `rand() % 30 == 0` restarts it at 8; when
//      bandY + 16 passes h, bandY returns to 0 and the band is on again for
//      the next sweep only if `rand() % 3 == 0`.
//
// The three globals are .bss, so all start at 0: no scatter and no band until
// the dice start one. They are SHARED by every box that draws in a frame.
//
// **The rule is exact; the random STREAM is a reconstruction.** `rand` is the
// CRT's (`seed * 214013 + 2531011`, bits 16..30), but its seed is the whole
// process's and every other caller advances it, so the sequence the original
// drew cannot be reproduced. This one is seeded at 1, the CRT's own default,
// and advanced only here.
struct UiInterference {
    int density = 0;       // dword_52B954
    int bandY = 0;         // dword_52B94C
    bool band = false;     // dword_52B950
    std::uint32_t seed = 1;

    // What one frame did, for a probe.
    struct Frame {
        bool shifted = false;  // step 1 fired
        int scattered = 0;     // pixels step 2 set
        int bandTop = -1;      // the band's first row, -1 when off
        int ored = 0;          // pixels step 3 ORed
    };

    // The rectangle is already in framebuffer pixels (the hook's
    // `I2D_ScaleX/Y`). Pixels outside the framebuffer are neither read nor
    // written - the original would, and never does for the shipped boxes.
    Frame apply(Surface& fb, int x, int y, int w, int h);

    int rand();
};

}  // namespace omk
