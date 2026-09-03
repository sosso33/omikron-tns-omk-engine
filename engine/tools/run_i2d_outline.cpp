// SPDX-License-Identifier: GPL-3.0-or-later
// Reproduce the load panel's selection outline with the ported rasterizer.
//
//     run_i2d_outline <out.bin>
//
// `I2D_DrawRectOutline` (0x004777A0) draws the box around the selected save
// with **four `I2D_SubmitQuad` calls, flags = 4** - and `quadMode(4)` is mode
// 1, the 50% blend. The four quads, transcribed from its own arithmetic with
// `v7 = I2D_ScaleX(1)` (which is `screenWidth * 1 / 640` = **1** at 640x480):
//
//     top     x [a1-v7, a1+a3+v7)   y [a2-v7, a2]
//     bottom  x [a1-v7, a1+a3+v7)   y [a2+a4, a2+a4+v7]
//     left    x [a1-v7, a1)         y [a2, a2+a4]
//     right   x [a1+a3, a1+a3+v7)   y [a2, a2+a4]
//
// The rectangle itself was measured off `traces/frames/loadpanel-mode2.png`
// and then solved back through that arithmetic: the horizontal bars occupy
// rows 172-173 and 189-190 spanning x 19..390, and the vertical bars are the
// single columns 19 and 390 - which gives **a1=20, a2=173, a3=370, a4=16**.
//
// **What this tests, and it is the sharp one.** Mode 1 fills the half-open
// span `[left, right)` in x and the closed span in y - an asymmetry
// transcribed from `sub_48C060` and left deliberately untidied. It predicts
// horizontal bars **2 pixels tall** and vertical bars **1 pixel wide** from
// the same `v7 = 1`, which is exactly what the original's framebuffer shows.
// Make the span closed in x and both bars become 2 wide, and the port stops
// matching the engine.
//
// The tool emits the coverage mask; `verify.py` compares it against the
// captured frame, because the frame is a PNG and decoding one here would need
// a dependency `PORTING` A8 forbids on this side.
#include "platform/datafs.h"
#include "ui/surface.h"

#include <cstdio>
#include <fstream>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: run_i2d_outline <out.bin>\n"); return 2; }
    const int W = 640, H = 480;
    const int a1 = 20, a2 = 173, a3 = 370, a4 = 16;
    const int v7 = W * 1 / 640;                       // I2D_ScaleX(1)
    const int mode = omk::quadMode(4);                // the flags it submits

    omk::Surface fb(W, H, 0);
    const std::uint16_t c = omk::rgb565(255, 255, 255);

    // the four quads, in `I2D_DrawRectOutline`'s own order
    const int quads[4][8] = {
        {a1 - v7, a2 - v7, a1 + a3 + v7, a2 - v7, a1 + a3 + v7, a2,      a1 - v7, a2},
        {a1 - v7, a2 + a4, a1 + a3 + v7, a2 + a4, a1 + a3 + v7, a2+a4+v7, a1 - v7, a2+a4+v7},
        {a1 - v7, a2,      a1,           a2,      a1,           a2 + a4, a1 - v7, a2 + a4},
        {a1 + a3, a2,      a1 + a3 + v7, a2,      a1 + a3 + v7, a2 + a4, a1 + a3, a2 + a4},
    };
    long filled = 0;
    for (const auto& q : quads) {
        const int xs[4] = {q[0], q[2], q[4], q[6]};
        const int ys[4] = {q[1], q[3], q[5], q[7]};
        filled += omk::fillQuad(fb, xs, ys, c, mode);
    }

    // The CONNECTOR to the thumbnail, on a second surface so the two shapes
    // are scored separately. It is not a line: `Ui_DrawItem`'s vocabulary has
    // FILL (a quad), OUTLINE (four quads), CURSOR, ARROWS and MARKER
    // (triangles) and SPRITE - and no line at all. The captured bar is 2 rows
    // by 69 columns, which is a mode-1 quad's signature (closed in y, half
    // open in x) and not a Bresenham line's, which would be one row.
    omk::Surface cn(W, H, 0);
    const int cx[4] = {391, 460, 460, 391};
    const int cy[4] = {181, 181, 182, 182};
    const long connFilled = omk::fillQuad(cn, cx, cy, c, mode);

    // the coverage: every pixel the four quads touched
    std::vector<std::uint8_t> cov(static_cast<std::size_t>(W) * H, 0);
    long covered = 0, connCovered = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * W + x;
            if (fb.at(x, y) != 0) { cov[i] |= 1; ++covered; }
            if (cn.at(x, y) != 0) { cov[i] |= 2; ++connCovered; }
        }

    std::vector<std::uint8_t> o;
    const auto put = [&o](long v) {
        const auto u = static_cast<std::uint32_t>(static_cast<std::int32_t>(v));
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    put(W); put(H); put(mode); put(v7); put(filled); put(covered);
    put(connFilled); put(connCovered);
    if (!omk::safeOutputPath(argv[1])) return 2;
    std::ofstream out(argv[1], std::ios::binary);
    out.write(reinterpret_cast<const char*>(o.data()),
              static_cast<std::streamsize>(o.size()));
    out.write(reinterpret_cast<const char*>(cov.data()),
              static_cast<std::streamsize>(cov.size()));

    std::printf("outline: v7=%d mode=%d; %ld pixels written, %ld covered\n",
                v7, mode, filled, covered);
    std::printf("connector: %ld pixels written, %ld covered\n",
                connFilled, connCovered);
    return 0;
}
