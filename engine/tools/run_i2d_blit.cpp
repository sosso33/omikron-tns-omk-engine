// SPDX-License-Identifier: GPL-3.0-or-later
// The I2D blit back end, RUN: the shipped bitmap through the ported display
// list into an RGB565 framebuffer.
//
//     run_i2d_blit <gamedata> <out.bin>
//
// This is the first output slice with a real oracle. `docs/PORTING.md` B6 sets
// the acceptance criterion for the I2D back end at **tier 4** - a captured
// menu frame reproduced pixel-exact from the shipped assets plus the ported
// display list - and B5 says why 2D can reach it: `Blt` is a memory copy with
// an optional colour key, so there is no filtering to differ.
//
// The tool emits the framebuffer as raw little-endian RGB565 and `verify.py`
// does the comparison, because the committed reference is a PNG and decoding
// one in C++ would mean a dependency `PORTING` A8 forbids on this side.
//
// **What is compared, and what is not.** Screen 29's own bitmap is
// `gfxint.BMP` - the title on black - and the capture's top 103 rows were
// measured identical across three frames, so that region is deterministic and
// is the target. The rest of the menu is the animated tile map drawn over it,
// which `PORTING` B5 says a check must not assert. The tool reports both so
// the difference is visible rather than hidden by a well-chosen crop.
#include "platform/datafs.h"
#include "ui/i2d.h"
#include "ui/surface.h"

#include <cstdio>
#include <fstream>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: run_i2d_blit <gamedata> <out.bin>\n");
        return 2;
    }
    const omk::DataFs fs(argv[1]);

    // `I2D_LoadBitmap` (0x00428A20) keeps one 264-byte record per file name;
    // here the table is a vector and the index is the handle.
    std::vector<omk::Surface> bitmaps;
    bitmaps.push_back(omk::surfaceFromBmp(fs.read("I2D/bitmaps/gfxint.BMP")));
    const auto& bmp = bitmaps[0];
    if (!bmp.valid()) {
        std::fprintf(stderr, "gfxint.BMP did not read\n");
        return 2;
    }

    // The screen's background blit: the whole bitmap onto the whole back
    // buffer. Source rectangle first, destination second - the order the
    // drawer's `Blt` call settles (i2d.h).
    omk::I2dList list;
    const omk::I2dPoint src[2] = {{0, 0, 0}, {bmp.w, bmp.h, 0}};
    const omk::I2dPoint dst[2] = {{0, 0, 0}, {640, 480, 0}};
    const auto accepted = list.blitBitmap(0, src, dst, /*bitmap=*/0, /*flags=*/0);

    omk::Surface fb(640, 480, 0);
    list.present(fb, bitmaps);

    // out.bin: the counts, then the framebuffer as raw LE uint16
    std::vector<std::uint8_t> head;
    const auto put = [&head](long v) {
        const auto u = static_cast<std::uint32_t>(static_cast<std::int32_t>(v));
        for (int k = 0; k < 4; ++k) head.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    long drawn = 0;
    for (int id : list.order()) { (void)id; ++drawn; }
    put(bmp.w); put(bmp.h);
    put(accepted == omk::I2dRefusal::Accepted ? 1 : 0);
    put(drawn);
    put(fb.w); put(fb.h);

    if (!omk::safeOutputPath(argv[2])) return 2;
    std::ofstream out(argv[2], std::ios::binary);
    out.write(reinterpret_cast<const char*>(head.data()),
              static_cast<std::streamsize>(head.size()));
    for (auto v : fb.px) {
        const char b[2] = {static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)};
        out.write(b, 2);
    }

    std::printf("gfxint.BMP %dx%d -> %s; %ld node%s in the list; framebuffer "
                "%dx%d RGB565 written\n",
                bmp.w, bmp.h,
                accepted == omk::I2dRefusal::Accepted ? "accepted" : "REFUSED",
                drawn, drawn == 1 ? "" : "s", fb.w, fb.h);
    return 0;
}
