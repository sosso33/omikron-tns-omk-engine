// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/surface.h"

#include <algorithm>
#include <cstring>

namespace omk {
namespace {

std::uint32_t u32(std::span<const std::byte> d, std::size_t o) {
    if (o + 4 > d.size()) return 0;
    return static_cast<std::uint32_t>(d[o]) |
           static_cast<std::uint32_t>(d[o + 1]) << 8 |
           static_cast<std::uint32_t>(d[o + 2]) << 16 |
           static_cast<std::uint32_t>(d[o + 3]) << 24;
}
std::int32_t i32(std::span<const std::byte> d, std::size_t o) {
    return static_cast<std::int32_t>(u32(d, o));
}
std::uint16_t u16(std::span<const std::byte> d, std::size_t o) {
    if (o + 2 > d.size()) return 0;
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(d[o]) |
           (static_cast<std::uint16_t>(d[o + 1]) << 8));
}

}  // namespace

Surface surfaceFromBmp(std::span<const std::byte> d) {
    Surface s;
    if (d.size() < 54 || static_cast<char>(d[0]) != 'B' || static_cast<char>(d[1]) != 'M')
        return s;
    const auto bits   = u32(d, 10);
    const auto hdr    = u32(d, 14);
    const auto w      = i32(d, 18);
    const auto hRaw   = i32(d, 22);
    const auto bpp    = u16(d, 28);
    const auto comp   = u32(d, 30);
    if (hdr < 40 || w <= 0 || hRaw == 0 || bpp != 8 || comp != 0) return s;
    // A negative height is a top-down BMP. The shipped ones are bottom-up, but
    // the sign is the format's own and costs one line to honour.
    const bool topDown = hRaw < 0;
    const int h = topDown ? -hRaw : hRaw;

    // the palette sits between the header and the pixels
    std::uint16_t pal[256] = {};
    const std::size_t palAt = 14 + hdr;
    for (int i = 0; i < 256; ++i) {
        const std::size_t o = palAt + 4u * static_cast<std::size_t>(i);
        if (o + 3 > d.size()) break;
        pal[i] = rgb565(static_cast<int>(d[o + 2]),   // BGRA on disk
                        static_cast<int>(d[o + 1]),
                        static_cast<int>(d[o]));
    }
    const std::size_t stride = (static_cast<std::size_t>(w) + 3u) / 4u * 4u;
    if (bits + stride * static_cast<std::size_t>(h) > d.size()) return s;

    s = Surface(w, h);
    for (int y = 0; y < h; ++y) {
        const std::size_t row = bits + stride *
            static_cast<std::size_t>(topDown ? y : h - 1 - y);
        for (int x = 0; x < w; ++x)
            s.set(x, y, pal[static_cast<std::uint8_t>(d[row + static_cast<std::size_t>(x)])]);
    }
    return s;
}

bool blt(Surface& dst, Rect dr, const Surface& src, Rect sr,
         std::uint32_t flags, std::uint16_t srcKey, std::uint16_t dstKey) {
    if (!dst.valid() || !src.valid()) return false;
    const int dw = dr.right - dr.left, dh = dr.bottom - dr.top;
    const int sw = sr.right - sr.left, sh = sr.bottom - sr.top;
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return false;
    if (sr.left < 0 || sr.top < 0 || sr.right > src.w || sr.bottom > src.h) return false;
    if (dr.left < 0 || dr.top < 0 || dr.right > dst.w || dr.bottom > dst.h) return false;

    const bool keySrc  = (flags & kBltKeySrc)  != 0;
    const bool keyDest = (flags & kBltKeyDest) != 0;

    for (int y = 0; y < dh; ++y) {
        // nearest-neighbour, which is what a 1999 blitter does; when the
        // rectangles are the same size this is the identity and the whole
        // thing is a row copy with two optional tests.
        const int sy = sr.top + (sh == dh ? y : y * sh / dh);
        for (int x = 0; x < dw; ++x) {
            const int sx = sr.left + (sw == dw ? x : x * sw / dw);
            const std::uint16_t v = src.at(sx, sy);
            if (keySrc && v == srcKey) continue;
            if (keyDest && dst.at(dr.left + x, dr.top + y) != dstKey) continue;
            dst.set(dr.left + x, dr.top + y, v);
        }
    }
    return true;
}


// ------------------------------------------------- the software rasterizer

long drawLine(Surface& dst, int x0, int y0, int x1, int y1, std::uint16_t colour,
              int left, int right, int top, int bottom) {
    if (!dst.valid()) return 0;
    // the guard the original opens with: a sane, non-negative clip rectangle
    if (!(left <= right && top <= bottom && left >= 0 && top >= 0)) return 0;
    if (right >= dst.w) right = dst.w - 1;
    if (bottom >= dst.h) bottom = dst.h - 1;

    // Eight clip steps, each endpoint against each side. Transcribed with the
    // original's asymmetries intact - the second step measures against the
    // ORIGINAL x1 where its neighbours use the running one, and the fifth
    // divides by `(x1 - x0)` with the operands in that order where the first
    // uses the other. Tidying either changes which pixel a clipped line
    // starts on.
    if (x0 < left) {
        if (x1 < left) return 0;
        y0 += (y1 - y0) * (left - x0) / (x1 - x0);
        x0 = left;
    }
    if (x0 > right) {
        if (x1 > right) return 0;
        y0 += (y1 - y0) * (right - x0) / (x1 - x0);
        x0 = right;
    }
    if (y0 < top) {
        if (y1 < top) return 0;
        x0 += (top - y0) * (x1 - x0) / (y1 - y0);
        y0 = top;
    }
    if (y0 > bottom) {
        if (y1 > bottom) return 0;
        x0 += (bottom - y0) * (x1 - x0) / (y1 - y0);
        y0 = bottom;
    }
    if (x1 < left) {
        if (x0 < left) return 0;
        y1 += (y1 - y0) * (left - x1) / (x1 - x0);
        x1 = left;
    }
    if (x1 > right) {
        if (x0 > right) return 0;
        y1 += (y1 - y0) * (right - x1) / (x1 - x0);
        x1 = right;
    }
    if (y1 < top) {
        if (y0 < top) return 0;
        x1 += (top - y1) * (x1 - x0) / (y1 - y0);
        y1 = top;
    }
    if (y1 > bottom) {
        if (y0 > bottom) return 0;
        x1 += (bottom - y1) * (x1 - x0) / (y1 - y0);
        y1 = bottom;
    }

    // the degenerate case the original tests before Bresenham
    if (x0 == x1 && y0 == y1) {
        if (x0 >= left && x0 <= right && y0 >= top && y0 <= bottom) {
            dst.set(x0, y0, colour);
            return 1;
        }
        return 0;
    }

    // Bresenham. The original swaps so that x0 <= x1 - which is also what
    // makes drawing an edge in either direction give the same pixels - and
    // carries the error as an unsigned add whose CARRY is the "step now" test,
    // initialised to -(dx)>>1.
    if (x0 > x1) { std::swap(x0, x1); std::swap(y0, y1); }
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 - y0, ystep = 1;
    if (y1 < y0) { dy = y0 - y1; ystep = -1; }

    long written = 0;
    int x = x0, y = y0;
    const auto plot = [&] {
        if (x >= left && x <= right && y >= top && y <= bottom) {
            dst.set(x, y, colour);
            ++written;
        }
    };
    if (dx >= dy) {
        std::uint32_t e = static_cast<std::uint32_t>(-dx >> 1);
        for (int n = dx + 1; n > 0; --n) {
            plot();
            ++x;
            const std::uint32_t sum = e + static_cast<std::uint32_t>(dy);
            const bool carry = sum < e;
            e = sum;
            if (carry) { y += ystep; e -= static_cast<std::uint32_t>(dx); }
        }
    } else {
        std::uint32_t e = static_cast<std::uint32_t>(-dy >> 1);
        for (int n = dy + 1; n > 0; --n) {
            plot();
            y += ystep;
            const std::uint32_t sum = e + static_cast<std::uint32_t>(dx);
            const bool carry = sum < e;
            e = sum;
            if (carry) { ++x; e -= static_cast<std::uint32_t>(dy); }
        }
    }
    return written;
}

long drawTriangleWire(Surface& dst, const int xy[6], std::uint16_t colour,
                      int left, int right, int top, int bottom) {
    // `sub_4806C0`'s mode-2 branch, verbatim: three edges, 0->1, 1->2, 2->0.
    return drawLine(dst, xy[0], xy[1], xy[2], xy[3], colour, left, right, top, bottom)
         + drawLine(dst, xy[2], xy[3], xy[4], xy[5], colour, left, right, top, bottom)
         + drawLine(dst, xy[4], xy[5], xy[0], xy[1], colour, left, right, top, bottom);
}


// ------------------------------------------------------------- the quad

int quadMode(std::uint32_t flags) {
    // `I2D_SubmitQuad`'s third argument, as both back ends read it.
    int mode = 0;
    if (flags & 7u) {
        mode = 1;
        if (flags & 1u) mode = 2;
        if (flags & 2u) mode = 3;
    }
    return mode;
}

namespace {
// The 565 channel masks, and the half-mask the 50% blend uses. The engine
// keeps these as the last and middle entries of the three conversion tables
// `sub_440A20` fills from the surface's pixel format - red[255] is the red
// mask, red[127] doubled is the mask with its low bit cleared - so they follow
// the format rather than being constants. Written out here for 565, which is
// what `PORTING` A3 fixes the reference framebuffer as.
constexpr std::uint16_t kMaskR = 0xF800, kMaskG = 0x07E0, kMaskB = 0x001F;
constexpr std::uint16_t kHalf  = static_cast<std::uint16_t>(
    (kMaskR & ~0x0800u) | (kMaskG & ~0x0020u) | (kMaskB & ~0x0001u));  // 0xF7DE

std::uint16_t satAdd(std::uint16_t a, std::uint16_t b, std::uint16_t m) {
    const unsigned s = static_cast<unsigned>(a & m) + static_cast<unsigned>(b & m);
    return static_cast<std::uint16_t>(s >= m ? m : s);
}
std::uint16_t satSub(std::uint16_t a, std::uint16_t b, std::uint16_t m) {
    const unsigned x = a & m, y = b & m;
    return static_cast<std::uint16_t>(x < y ? 0u : x - y);
}
}  // namespace

long fillQuad(Surface& dst, const int x[4], const int y[4],
              std::uint16_t colour, int mode) {
    if (!dst.valid()) return 0;
    int l = x[0], r = x[0], t = y[0], b = y[0];
    for (int i = 1; i < 4; ++i) {
        if (x[i] < l) l = x[i];
        if (x[i] > r) r = x[i];
        if (y[i] < t) t = y[i];
        if (y[i] > b) b = y[i];
    }
    // clamped to the SCREEN, which is what the original clamps to - there is
    // no clip rectangle parameter on this path
    if (l < 0) l = 0;
    if (t < 0) t = 0;
    if (r >= dst.w) r = dst.w - 1;
    if (b >= dst.h) b = dst.h - 1;
    if (l > r) return 0;

    // modes 0 and 1 stop before `r`; modes 2 and 3 include it
    const int last = (mode >= 2) ? r : r - 1;
    long written = 0;
    for (int py = t; py <= b; ++py) {
        for (int px = l; px <= last; ++px) {
            const std::uint16_t d = dst.at(px, py);
            std::uint16_t v = colour;
            switch (mode) {
                case 0: break;
                case 1:
                    v = static_cast<std::uint16_t>(
                        ((kHalf & colour) >> 1) + ((kHalf & d) >> 1));
                    break;
                case 2:
                    v = static_cast<std::uint16_t>(satAdd(colour, d, kMaskR) |
                                                   satAdd(colour, d, kMaskG) |
                                                   satAdd(colour, d, kMaskB));
                    break;
                default:
                    v = static_cast<std::uint16_t>(satSub(d, colour, kMaskR) |
                                                   satSub(d, colour, kMaskG) |
                                                   satSub(d, colour, kMaskB));
                    break;
            }
            dst.set(px, py, v);
            ++written;
        }
    }
    return written;
}

}  // namespace omk
