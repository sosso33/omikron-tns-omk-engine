// SPDX-License-Identifier: GPL-3.0-or-later
// The menu's animated background. See `cloud.h` for where every constant in
// here comes from; nothing below is chosen.
#include "ui/cloud.h"
#include "platform/cacheline.h"
#include "platform/profile.h"
#include "platform/threads.h"

#include <cmath>
#include <cstring>

namespace omk {
namespace {

// `cloud.bmp` is an 8-bit 256x256 and only its INDICES are wanted - the
// emboss works on the greyscale height, and the colour comes from the ramp.
bool readIndices(std::span<const std::byte> d, std::vector<std::uint8_t>& out) {
    if (d.size() < 54 || static_cast<char>(d[0]) != 'B' ||
        static_cast<char>(d[1]) != 'M') return false;
    const auto u32 = [&](std::size_t o) {
        return static_cast<std::uint32_t>(d[o]) |
               (static_cast<std::uint32_t>(d[o + 1]) << 8) |
               (static_cast<std::uint32_t>(d[o + 2]) << 16) |
               (static_cast<std::uint32_t>(d[o + 3]) << 24);
    };
    const std::uint32_t off = u32(10);
    const int w = static_cast<int>(u32(18)), h = static_cast<int>(u32(22));
    const int bpp = static_cast<int>(d[28]) | (static_cast<int>(d[29]) << 8);
    if (w != 256 || h != 256 || bpp != 8) return false;
    const std::size_t row = (static_cast<std::size_t>(w) + 3u) & ~3u;
    if (off + row * static_cast<std::size_t>(h) > d.size()) return false;
    out.assign(static_cast<std::size_t>(w) * h, 0);
    for (int y = 0; y < h; ++y) {
        // a .bmp is bottom-up
        const std::size_t src = off + static_cast<std::size_t>(h - 1 - y) * row;
        for (int x = 0; x < w; ++x)
            out[static_cast<std::size_t>(y) * 256 + x] =
                static_cast<std::uint8_t>(d[src + static_cast<std::size_t>(x)]);
    }
    return true;
}

int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

}  // namespace

void MenuCloud::buildRamp(std::vector<std::uint16_t>& out) {
    out.assign(64, 0);
    // The two halves, exactly as 0x004B22D0 builds them. `>> 5` is the
    // image's `sar 5` after its round-toward-zero fixup; every value here is
    // positive, so a plain shift is the same arithmetic.
    int lo1 = 4049, mid1 = 947, hi1 = 13;     // var_43C, var_440, ebx
    int lo2 = 608,  mid2 = 544, hi2 = 416;    // ebp,     var_454, var_44C
    for (int i = 0; i < 32; ++i) {
        // `Color_Sum` takes a COLORREF, so the LOW byte is RED. Reading it as
        // 0xRRGGBB instead gives a blue-to-olive ramp that looks plausible and
        // matches nothing: the captured menu's own pixels are what settle it.
        out[static_cast<std::size_t>(i)] =
            rgb565(clamp255(lo1 >> 5), clamp255(mid1 >> 5), clamp255(hi1 >> 5));
        out[static_cast<std::size_t>(32 + i)] =
            rgb565(clamp255(lo2 >> 5), clamp255(mid2 >> 5), clamp255(hi2 >> 5));
        lo1 -= 111; mid1 -= 13; hi1 += 13;
        lo2 += 19;  mid2 += 94; hi2 += 92;
    }
}

bool MenuCloud::load(const DataFs& fs) {
    buildRamp(ramp_);
    const auto p = fs.resolve("IMAGES/cloud.bmp");
    if (!p) return false;
    return readIndices(DataFs::readPath(*p), tex_);
}

void MenuCloud::prepare(long frame) const {
    const double f = static_cast<double>(frame);

    // TWO PASSES, and folding them into one is what went wrong three times.
    //
    //   1. emboss the cloud at **256x256** into a work buffer - which is what
    //      the 0x20000 malloc is, 256*256*2 - with a light whose weights run
    //      over 256 and therefore never reach the clamps;
    //   2. RESAMPLE that buffer to 640x480 through the warp: the X source
    //      comes from the ROW table and the Y source from the COLUMN table.
    //
    // The second pass is the wave. The buffer is 256 wide against a 640-wide
    // screen so it repeats, but each row is displaced horizontally and each
    // column vertically, which breaks the repeats up instead of lining them
    // into squares. Applying the warp to the CLOUD instead - one pass - either
    // smooths it into a flow or leaves hard 256-pixel seams, and neither is
    // what the original shows in play.

    // ---- pass 1: 256x256, `loc_4B1BC4`.
    //
    // (Written for a slow CPU - an iBook G3 draws the
    // menu at ~6 fps - and SAME PIXELS, byte for byte: the weights the
    // engine counts down per row and per pixel, `(255 - ly) - j2` and
    // `(255 - lx) - i2`, ARE `cy - ly` and `cx - lx` since `cy = 255 - j2`
    // and `cx = 255 - i2`; the two rows are fetched once a row; and the
    // clamp to 0..63 and the ramp are ONE lookup in `shade_`, which spans
    // every value the sum can take - gradients of -128..127 times weights of
    // -383..255, twice, shifted by 5.)
    OMK_ZONE("ui: cloud emboss");   // the profiler (todo/debug-tools.md)
    const double t = 0.0785 * f;
    const int lx = static_cast<int>(std::cos(t) * 64.0) + 128;
    const int ly = static_cast<int>(std::sin(t) * 64.0) + 128;
    constexpr int kShadeLo = -3100, kShadeHi = 3100;   // (128*383*2) >> 5 = 3064
    if (shade_.empty()) {
        shade_.resize(kShadeHi - kShadeLo + 1);
        for (int k = kShadeLo; k <= kShadeHi; ++k) {
            int v = k + 32;
            if (v < 0) v = 0;
            if (v > 63) v = 63;
            shade_[static_cast<std::size_t>(k - kShadeLo)] = ramp_[static_cast<std::size_t>(v)];
        }
    }
    const std::uint16_t* shade = shade_.data() - kShadeLo;
    buf_.resize(256u * 256u);
    std::uint16_t* buf = buf_.data();
    const std::uint8_t* tex = tex_.data();
    for (int j2 = 0; j2 < 256; ++j2) {
        const int cy = 255 - j2;                   // `dh` counts down from 255
        const int wy = cy - ly;                    // `ebp`, `dec` per row
        const std::uint8_t* row = tex + static_cast<std::size_t>(cy) * 256;
        const std::uint8_t* below = tex + static_cast<std::size_t>((cy + 1) & 0xFF) * 256;
        std::uint16_t* out = buf + static_cast<std::size_t>(j2) * 256;
        claimLines(out, 256 * sizeof(std::uint16_t));   // the row is written whole (`platform/cacheline.h`)
        // TWO PIXELS AN ITERATION: one pixel is a
        // chain - two loads, a subtract, a multiply, a shift, the ramp's load
        // - that the G3 ran at ~35 cycles a pixel, waiting on each link. Two
        // independent chains side by side give it the other to work on; the
        // arithmetic of each pixel is the one below, unchanged.
        for (int i2 = 0; i2 < 256; i2 += 2) {
            const int cx = 255 - i2, cxb = cx - 1;
            const int h0 = row[cx], h0b = row[cxb];
            const int gxb = static_cast<std::int8_t>(h0b - row[cx]);   // (cxb + 1) & 0xFF == cx
            const int gyb = static_cast<std::int8_t>(h0b - below[cxb]);
            out[i2 + 1] = shade[(gxb * (cxb - lx) + gyb * wy) >> 5];
            // `sub al, [esi+ebx]` then `movsx` - the gradients are SIGNED
            // BYTES, so a wrap reads as a small step and not as 255.
            const int gx = static_cast<std::int8_t>(h0 - row[(cx + 1) & 0xFF]);
            const int gy = static_cast<std::int8_t>(h0 - below[cx]);
            out[i2] = shade[(gx * (cx - lx) + gy * wy) >> 5];
        }
    }

    // ---- the warp tables, `sub_4B1F40`. Stored as BYTES.
    std::uint8_t* colTab = colTab_;
    std::uint8_t* rowTab = rowTab_;
    {
        const double pA =  0.009925 * f, pB = -0.013915 * f;   // the row table
        const double pC = -0.007685 * f, pD =  0.015635 * f;   // the column one
        for (int k = 0; k < 480; ++k)
            rowTab[k] = static_cast<std::uint8_t>(static_cast<int>(
                std::cos(pB + 0.0067 * k) * 32.0 + std::cos(pA - 0.0043 * k) * 64.0));
        for (int k = 0; k < 640; ++k)
            colTab[k] = static_cast<std::uint8_t>(static_cast<int>(
                (std::cos(pC + 0.0057 * k) + std::cos(pD - 0.0099 * k)) * 48.0));
    }
}

void MenuCloud::draw(Surface& fb, long frame) const {
    if (tex_.empty() || fb.w != 640 || fb.h != 480) return;
    prepare(frame);
    const std::uint16_t* buf = buf_.data();
    const std::uint8_t* colTab = colTab_;
    const std::uint8_t* rowTab = rowTab_;

    // ---- pass 2: 256x256 -> 640x480 through the warp, `loc_4B1E4D`.
    //
    //     al = rowTab[479 - y]      the X source, then `inc al` per pixel
    //     ah = colTab[639 - x] + y  the Y source; `bl` is a per-row `inc`
    //     out = buf[ah * 256 + al]
    //
    // (The column table reversed once a frame, and `al` an 8-bit counter
    // whose wrap is the register's own, as `inc al` wraps - the same pixels.)
    OMK_ZONE("ui: cloud warp");   // the profiler (todo/debug-tools.md)
    std::uint8_t colRev[640];
    for (int x = 0; x < 640; ++x) colRev[x] = colTab[639 - x];
    // each row its own, read from `buf` alone: split over the pool (the
    // Vita's three runners; one where there are no threads) - the same pixels
    Threads::shared().parallelFor(0, 480, 32, [&](std::size_t from, std::size_t to) {
    for (int y = static_cast<int>(from); y < static_cast<int>(to); ++y) {
        std::uint8_t al = rowTab[479 - y];
        const std::uint8_t yy = static_cast<std::uint8_t>(y);
        std::uint16_t* dst = &fb.px[static_cast<std::size_t>(y) * 640];
        // the row's lines claimed before it is written whole: on the G3 each
        // store to a line not in the cache read the line first, and this
        // pass is 614 KB a frame (step 3o, `platform/cacheline.h`)
        claimLines(dst, 640 * sizeof(std::uint16_t));
        for (int x = 0; x < 640; x += 2, al = static_cast<std::uint8_t>(al + 2)) {
            const std::uint8_t ah = static_cast<std::uint8_t>(colRev[x] + yy);
            const std::uint8_t ahb = static_cast<std::uint8_t>(colRev[x + 1] + yy);
            const std::uint8_t alb = static_cast<std::uint8_t>(al + 1);
            dst[x] = buf[static_cast<std::size_t>(ah) << 8 | al];
            dst[x + 1] = buf[static_cast<std::size_t>(ahb) << 8 | alb];
        }
    }
    });
}

bool MenuCloud::drawScaled(Surface& fb, long frame) const {
    if (tex_.empty() || !fb.valid()) return false;
    if (fb.w == 640 && fb.h == 480) { draw(fb, frame); return true; }
    prepare(frame);
    const std::uint16_t* buf = buf_.data();
    // `blt`'s NEAREST rule for a 640x480 source onto w x h, per column once
    // (a runtime divide per pixel is a library call on the A9)
    std::vector<int> sxOf(static_cast<std::size_t>(fb.w));
    std::vector<int> colOf(static_cast<std::size_t>(fb.w));
    for (int x = 0; x < fb.w; ++x) {
        const int sx = x * 640 / fb.w;
        sxOf[static_cast<std::size_t>(x)] = sx;
        colOf[static_cast<std::size_t>(x)] = colTab_[639 - sx];
    }
    // each row its own: split over the pool, as `draw`'s (an open menu's
    // cloud is ~70% of its interface frame - 2026-11-18, the M1's sample)
    Threads::shared().parallelFor(0, static_cast<std::size_t>(fb.h), 32, [&](std::size_t from, std::size_t to) {
    for (int y = static_cast<int>(from); y < static_cast<int>(to); ++y) {
        const int sy = y * 480 / fb.h;
        const int al0 = rowTab_[479 - sy];
        std::uint16_t* dst = &fb.px[static_cast<std::size_t>(y) * static_cast<std::size_t>(fb.w)];
        claimLines(dst, static_cast<std::size_t>(fb.w) * sizeof(std::uint16_t));   // written whole
        for (int x = 0; x < fb.w; ++x) {
            // pass 2's pixel (sx, sy): al = rowTab[479 - sy] + sx, ah = colTab[639 - sx] + sy
            const int ah = (colOf[static_cast<std::size_t>(x)] + sy) & 0xFF;
            const int al = (al0 + sxOf[static_cast<std::size_t>(x)]) & 0xFF;
            dst[x] = buf[static_cast<std::size_t>(ah) * 256 + static_cast<std::size_t>(al)];
        }
    }
    });
    return true;
}

}  // namespace omk
