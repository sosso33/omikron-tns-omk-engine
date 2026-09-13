// SPDX-License-Identifier: GPL-3.0-or-later
// THE PIXEL CONVERSION TABLES GIVE THE FUNCTIONS' BITS - checked exhaustively.
//
//     pixel_tables
//
// The Vulkan backend's frame goes 888 -> dithered 565 on the way back to the
// CPU and 565 -> RGBA8 on the way out to the window, a pixel at a time, and a
// profile of the street put the first at ~2 ms a frame. `ui/surface.*` now
// does both through tables (todo/optimization.md step 4), held to the
// functions' answers:
//
//   * every (r, g, b) at every one of the 16 dither cells through the channel
//     tables, against `quantise888Dither` - 2^24 x 16 colours;
//   * whole rows through `quantise888DitherRow` at widths 1, 3, 640, 641 and
//     800 and at rows 0..7, against the function a pixel at a time;
//   * every one of the 65536 565 values through `expand565Rgba`, against the
//     per-channel bit replication `presentSurface` used.
//
// Prints the counts and `mismatches <total>`. Writes nothing.
#include "ui/surface.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

// THE REFERENCE, NEVER INLINED - and that is load-bearing. Called inline in the
// row loop below, Apple clang -O2 auto-vectorised `quantise888Dither` over the
// row and the vectorised evaluation returned DIFFERENT values: on the same
// pixels the function's outputs summed to 578065495 where the scalar function
// and the row tables both give 542859991, so the check reported ~8000 "row
// mismatches" whose count changed with the link. With -fno-vectorize, or
// through this wrapper, the reference is scalar and agrees exactly. Recorded in
// todo/optimization.md step 4 as a lead about the function itself (the
// software rasterizer calls it in per-pixel loops); the Vulkan readback it
// replaces was measured byte-identical to the tables on real dithered frames.
[[gnu::noinline]] std::uint16_t reference(int r, int g, int b, int x, int y) {
    return omk::quantise888Dither(r, g, b, x, y);
}

}  // namespace

int main() {
    long checked = 0, mismatches = 0;

    // 1. every colour at every cell, through a one-pixel "row" positioned at
    //    that cell - the row function is the only public way into the tables
    unsigned char px[4] = {0, 0, 0, 255};
    std::uint16_t out = 0;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            for (int r = 0; r < 256; ++r)
                for (int g = 0; g < 256; ++g)
                    for (int b = 0; b < 256; ++b) {
                        // a row of x+1 pixels whose LAST sits at column x
                        px[0] = static_cast<unsigned char>(r);
                        px[1] = static_cast<unsigned char>(g);
                        px[2] = static_cast<unsigned char>(b);
                        // the row function takes column 0 as the first pixel, so
                        // shift the cell by writing at column x of a short row
                        unsigned char row[16];
                        std::memset(row, 0, sizeof row);
                        std::memcpy(row + 4 * x, px, 4);
                        std::uint16_t o[4];
                        omk::quantise888DitherRow(row, x + 1, y, o);
                        out = o[x];
                        ++checked;
                        if (out != reference(r, g, b, x, y)) ++mismatches;
                    }
    const long colourChecks = checked, colourMismatches = mismatches;

    // 2. whole rows, pseudo-random pixels
    std::uint64_t s = 0x2545F4914F6CDD1DULL;
    const auto next = [&] { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return static_cast<unsigned char>(s >> 56); };
    long rowChecks = 0, rowMismatches = 0;
    for (int w : {1, 3, 640, 641, 800})
        for (int y = 0; y < 8; ++y) {
            std::vector<unsigned char> rgba(4 * static_cast<std::size_t>(w));
            for (auto& c : rgba) c = next();
            std::vector<std::uint16_t> o(static_cast<std::size_t>(w));
            omk::quantise888DitherRow(rgba.data(), w, y, o.data());
            for (int x = 0; x < w; ++x) {
                ++rowChecks;
                const unsigned char* p = &rgba[4 * static_cast<std::size_t>(x)];
                if (o[static_cast<std::size_t>(x)] != reference(p[0], p[1], p[2], x, y))
                    ++rowMismatches;
            }
        }

    // 3. every 565 value through the expansion table
    long expChecks = 0, expMismatches = 0;
    const unsigned char* lut = omk::expand565Rgba();
    for (unsigned v = 0; v < 65536u; ++v) {
        const int r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
        const unsigned char want[4] = {static_cast<unsigned char>((r << 3) | (r >> 2)),
                                       static_cast<unsigned char>((g << 2) | (g >> 4)),
                                       static_cast<unsigned char>((b << 3) | (b >> 3)), 255};
        ++expChecks;
        if (std::memcmp(lut + 4 * v, want, 4) != 0) ++expMismatches;
    }

    const long total = colourMismatches + rowMismatches + expMismatches;
    std::printf("colours %ld mismatches %ld | rows %ld mismatches %ld | expand %ld mismatches %ld\n",
                colourChecks, colourMismatches, rowChecks, rowMismatches, expChecks, expMismatches);
    std::printf("mismatches %ld\n", total);
    return total == 0 ? 0 : 3;
}
