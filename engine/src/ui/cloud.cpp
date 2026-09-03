// SPDX-License-Identifier: GPL-3.0-or-later
// The menu's animated background. See `cloud.h` for where every constant in
// here comes from; nothing below is chosen.
#include "ui/cloud.h"

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

void MenuCloud::draw(Surface& fb, long frame) const {
    if (tex_.empty() || fb.w != 640 || fb.h != 480) return;
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
    // smooths it into a flow or leaves hard 256-pixel seams, and a reader
    // watching the original rejected both.

    // ---- pass 1: 256x256, `loc_4B1BC4`.
    const double t = 0.0785 * f;
    const int lx = static_cast<int>(std::cos(t) * 64.0) + 128;
    const int ly = static_cast<int>(std::sin(t) * 64.0) + 128;
    std::vector<std::uint16_t> buf(256u * 256u);
    for (int j2 = 0; j2 < 256; ++j2) {
        const int cy = (255 - j2) & 0xFF;          // `dh` counts down from 255
        const int wy = (255 - ly) - j2;            // `ebp`, `dec` per row
        for (int i2 = 0; i2 < 256; ++i2) {
            const int cx = (255 - i2) & 0xFF;      // `dl` counts down, wrapping
            const int wx = (255 - lx) - i2;        // `ecx`, reset per row
            const int h0 = tex_[static_cast<std::size_t>(cy) * 256 + cx];
            const int hx = tex_[static_cast<std::size_t>(cy) * 256 + ((cx + 1) & 0xFF)];
            const int hy = tex_[static_cast<std::size_t>((cy + 1) & 0xFF) * 256 + cx];
            // `sub al, [esi+ebx]` then `movsx` - the gradients are SIGNED
            // BYTES, so a wrap reads as a small step and not as 255.
            const int gx = static_cast<std::int8_t>(h0 - hx);
            const int gy = static_cast<std::int8_t>(h0 - hy);
            int v = ((gx * wx + gy * wy) >> 5) + 32;
            if (v < 0) v = 0;
            if (v > 63) v = 63;
            buf[static_cast<std::size_t>(j2) * 256 + i2] =
                ramp_[static_cast<std::size_t>(v)];
        }
    }

    // ---- the warp tables, `sub_4B1F40`. Stored as BYTES.
    std::uint8_t colTab[640], rowTab[480];
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

    // ---- pass 2: 256x256 -> 640x480 through the warp, `loc_4B1E4D`.
    //
    //     al = rowTab[479 - y]      the X source, then `inc al` per pixel
    //     ah = colTab[639 - x] + y  the Y source; `bl` is a per-row `inc`
    //     out = buf[ah * 256 + al]
    for (int y = 0; y < 480; ++y) {
        int al = rowTab[479 - y];
        std::uint16_t* dst = &fb.px[static_cast<std::size_t>(y) * 640];
        for (int x = 0; x < 640; ++x) {
            const int ah = (colTab[639 - x] + y) & 0xFF;
            dst[x] = buf[static_cast<std::size_t>(ah) * 256 + al];
            al = (al + 1) & 0xFF;
        }
    }
}

}  // namespace omk
