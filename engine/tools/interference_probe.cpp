// SPDX-License-Identifier: GPL-3.0-or-later
// The interference box, `sub_432940` (ui/interference.h), run over frames.
//
//     interference_probe
//
// MULTIPLAN's box (60,130) 450x260 at 640x480, applied to a fresh BLACK frame
// 3000 times from the .bss start. Black makes every write visible: a pixel the
// band ORs lights up, a scatter write is one of four greys, a shift moves
// nothing. Prints what the dice did and the invariants the rule implies:
// nothing written outside the box, the band only on its eight rows two apart,
// every scatter colour one of the four.
#include "ui/interference.h"

#include <cstdio>
#include <set>

int main() {
    const int X = 60, Y = 130, W = 450, H = 260;
    omk::UiInterference n;
    omk::Surface fb(640, 480, 0);

    // 040404 is NOT black in 565: the green field keeps one bit (0x0020).
    const std::set<std::uint16_t> greys = {omk::rgb565(0x80, 0x80, 0x80), omk::rgb565(0x50, 0x50, 0x50),
                                           omk::rgb565(0x04, 0x04, 0x04), omk::rgb565(0x60, 0x60, 0x60)};
    long frames = 3000, shifted = 0, scatterFrames = 0, bandFrames = 0, restarts = 0;
    long outside = 0, offGrid = 0, badScatter = 0, scattered = 0, ored = 0;
    int maxBandBottom = 0, prevDensity = 0;
    for (long f = 0; f < frames; ++f) {
        std::fill(fb.px.begin(), fb.px.end(), 0);
        const int bandTop0 = n.band ? Y + n.bandY : -1;
        const bool scatterOn = n.density > 0;
        const auto r = n.apply(fb, X, Y, W, H);
        if (r.bandTop != bandTop0) ++offGrid;
        shifted += r.shifted;
        if (scatterOn) ++scatterFrames;
        if (r.bandTop >= 0) {
            ++bandFrames;
            if (r.bandTop + 15 > maxBandBottom) maxBandBottom = r.bandTop + 15;
        }
        if (n.density == 8 && prevDensity != 8) ++restarts;
        prevDensity = n.density;
        scattered += r.scattered;
        ored += r.ored;
        for (int y = 0; y < fb.h; ++y)
            for (int x = 0; x < fb.w; ++x) {
                const std::uint16_t v = fb.at(x, y);
                if (!v) continue;
                if (x < X || x >= X + W || y < Y || y >= Y + H) { ++outside; continue; }
                const bool bandRow = r.bandTop >= 0 && y >= r.bandTop && y < r.bandTop + 16 &&
                                     ((y - r.bandTop) % 2) == 0;
                if (!bandRow && !greys.count(v)) ++badScatter;
            }
    }
    std::printf("frames %ld shifted %ld scatter %ld band %ld restarts %ld\n",
                frames, shifted, scatterFrames, bandFrames, restarts);
    std::printf("pixels scattered %ld ored %ld\n", scattered, ored);
    std::printf("outside %ld offgrid %ld badscatter %ld bandbottom-inside %d\n",
                outside, offGrid, badScatter, maxBandBottom < Y + H ? 1 : 0);
    return 0;
}
