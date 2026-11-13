// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/bytes.h"
#include "ui/surface.h"
#include "platform/threads.h"
#include "ui/overlay.h"
#include "ui/uipack.h"

#include <cmath>
#include <functional>

#include <algorithm>
#include <cstring>
#if OMK_THREADS
#include <mutex>
#endif

namespace omk {
namespace {

using omk::le::i32;
using omk::le::u16;
using omk::le::u32;

}  // namespace

namespace {
Surface decodeBmp(std::span<const std::byte> d) {
    Surface s;
    if (d.size() < 54 || static_cast<char>(d[0]) != 'B' || static_cast<char>(d[1]) != 'M')
        return s;
    const auto bits   = u32(d, 10);
    const auto hdr    = u32(d, 14);
    const auto w      = i32(d, 18);
    const auto hRaw   = i32(d, 22);
    const auto bpp    = u16(d, 28);
    const auto comp   = u32(d, 30);
    if (hdr < 40 || w <= 0 || hRaw == 0 || (bpp != 8 && bpp != 24) || comp != 0) return s;
    // A negative height is a top-down BMP. The shipped ones are bottom-up, but
    // the sign is the format's own and costs one line to honour.
    const bool topDown = hRaw < 0;
    const int h = topDown ? -hRaw : hRaw;

    // ...AND ONE SHEET IS 24-BIT. Ten of the eleven `I2D\bitmaps` are 8-bit
    // palettes; `boutiq.BMP`, the ten shops' button atlas, is BGR at 24 bits,
    // and `I2D_CreateSurfaceFromBmp` hands both to DirectDraw alike. Taking
    // only 8 leaves the shops' sheet invalid, and every one of their button
    // sprites is skipped (the icons on the right go missing). The
    // key stays pure black (`SetColorKey {0,0}`), which is 0 in 565 too.
    if (bpp == 24) {
        const std::size_t stride24 = (static_cast<std::size_t>(w) * 3u + 3u) / 4u * 4u;
        if (bits + std::uint64_t{stride24} * std::uint64_t(h) > d.size()) return s;   // 64 bits: a 32-bit size_t wraps
        s = Surface(w, h);
        for (int y = 0; y < h; ++y) {
            const std::size_t row = bits + stride24 *
                static_cast<std::size_t>(topDown ? y : h - 1 - y);
            for (int x = 0; x < w; ++x) {
                const std::size_t o = row + 3u * static_cast<std::size_t>(x);
                s.set(x, y, rgb565(static_cast<int>(d[o + 2]),
                                   static_cast<int>(d[o + 1]),
                                   static_cast<int>(d[o])));
            }
        }
        return s;
    }

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
}  // namespace

Surface surfaceFromBmp(std::span<const std::byte> d) {
    Surface s = decodeBmp(d);
    // THE INTERFACE PACK (`ui/uipack.h`): noted when the pack has an HD
    // version of exactly this bitmap; nothing when no pack is open
#if OMK_REMASTER
    uiPack().attach(s);
#endif
    return s;
}

namespace {

// THE STRETCHED-SPRITE CACHE (the reader's "still high on main menu, maybe
// linked to the logo", 2026-11-02). The start menu's title band is a 640x150
// colour-keyed sprite stretched to the window every frame - 2.1 million
// keyed bilinear pixels at 3024x1964, a core and a half - and it never
// changes. A big scaled key-source blit is therefore kept as what it WRITES:
// per destination row, the runs of pixels it draws and their values. A later
// blit of the same source pixels (a hash of the source rectangle), the same
// rectangles' sizes, key and filter copies the runs. The pixels a keyed blit
// draws are found by drawing it twice, over two different fills
// (`WrittenLayer`, surface.h). Byte-identical to drawing it.
struct SpriteCache {
    std::uint64_t hash = 0;
    int sw = 0, sh = 0, dw = 0, dh = 0, filter = 0;
    std::uint16_t key = 0;
    WrittenLayer layer;
    long used = 0;
};
#if OMK_THREADS
std::mutex g_spriteMu;
using SpriteLock = std::lock_guard<std::mutex>;
#else
struct SpriteLock { explicit SpriteLock(int) {} };   // one thread: nothing to order
int g_spriteMu = 0;
#endif
std::vector<SpriteCache> g_sprites;   // a handful: the screens' big sprites
long g_spriteTick = 0;
thread_local bool t_spriteFilling = false;

// A fingerprint of a rectangle's pixels: the 64-bit FNV-1a hash, fed one
// 565 word at a time. Its two constants are the published ones
// (Fowler, Noll, Vo): where the hash starts, and what each step multiplies by.
constexpr std::uint64_t kFnv64OffsetBasis = 14695981039346656037ull;
constexpr std::uint64_t kFnv64Prime = 1099511628211ull;

std::uint64_t rectHash(const Surface& s, Rect r) {
    std::uint64_t hash = kFnv64OffsetBasis;
    for (int y = r.top; y < r.bottom; ++y) {
        const std::uint16_t* row = &s.px[static_cast<std::size_t>(y) * s.w + r.left];
        for (int x = 0; x < r.right - r.left; ++x) {
            hash ^= row[x];
            hash *= kFnv64Prime;
        }
    }
    return hash;
}

// "Big": a quarter of a 640x480 frame and up - the size from which a blit
// is worth splitting over the thread pool, and worth caching
constexpr long kBigBlitPixels = 640L * 480L / 4L;

// A blit's rows, split over the shared pool when it is big (a quarter of a
// 640x480 frame and up), whole on the caller's thread otherwise
void rows(int dw, int dh, const std::function<void(std::size_t, std::size_t)>& body) {
    if (static_cast<long>(dw) * dh < kBigBlitPixels) { body(0, static_cast<std::size_t>(dh)); return; }
    Threads::shared().parallelFor(0, static_cast<std::size_t>(dh), 16, body);
}

}  // namespace

bool WrittenLayer::beginCapture() { const bool was = t_spriteFilling; t_spriteFilling = true; return was; }
void WrittenLayer::endCapture(bool was) { t_spriteFilling = was; }

void WrittenLayer::keep(const Surface& overBlack, const Surface& overWhite) {
    w_ = overBlack.w; h_ = overBlack.h;
    rowAt_.assign(1, 0);
    runs_.clear(); px_.clear();
    for (int y = 0; y < h_; ++y) {
        const std::size_t o = static_cast<std::size_t>(y) * w_;
        // a pixel the drawing writes came out the same over both fills
        const auto written = [&](int x) { return overBlack.px[o + x] == overWhite.px[o + x]; };
        for (int x = 0; x < w_;) {
            if (!written(x)) { ++x; continue; }
            const int runStart = x;
            while (x < w_ && written(x)) ++x;
            runs_.push_back({runStart, x - runStart, px_.size()});
            px_.insert(px_.end(), overBlack.px.begin() + static_cast<long>(o + runStart),
                       overBlack.px.begin() + static_cast<long>(o + x));
        }
        rowAt_.push_back(runs_.size());
    }
}

void WrittenLayer::apply(Surface& dst, int x, int y) const {
    if (!valid() || x < 0 || y < 0 || x + w_ > dst.w || y + h_ > dst.h) return;
    // split like a blit: a big layer's rows over the pool, each row its own
    rows(w_, h_, [&](std::size_t from, std::size_t to) {
        for (std::size_t r = from; r < to; ++r) {
            std::uint16_t* out = &dst.px[(static_cast<std::size_t>(y) + r) * dst.w + x];
            for (std::size_t i = rowAt_[r]; i < rowAt_[r + 1]; ++i)
                std::memcpy(out + runs_[i].x, &px_[runs_[i].at], static_cast<std::size_t>(runs_[i].n) * 2);
        }
    });
}

bool blt(Surface& dst, Rect dr, const Surface& src, Rect sr,
         std::uint32_t flags, std::uint16_t srcKey, std::uint16_t dstKey,
         int filter) {
    if (!dst.valid() || !src.valid()) return false;
    const int dw = dr.right - dr.left, dh = dr.bottom - dr.top;
    const int sw = sr.right - sr.left, sh = sr.bottom - sr.top;
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return false;
    if (sr.left < 0 || sr.top < 0 || sr.right > src.w || sr.bottom > src.h) return false;
    if (dr.left < 0 || dr.top < 0 || dr.right > dst.w || dr.bottom > dst.h) return false;

    const bool keySrc  = (flags & kBltKeySrc)  != 0;
    const bool keyDest = (flags & kBltKeyDest) != 0;

    // ---- THE INTERFACE PACK (`ui/uipack.h`, the remaster's) --------------
    // An ENLARGING blit of a bitmap the pack has an HD version of draws from
    // that, its rectangle scaled by the factor - the same blit, a finer
    // source. Its key is the original's (0), so a key-source blit with any
    // other key keeps the original. The HD surface is not noted, so this
    // recurses once.
#if OMK_REMASTER
    if ((dw > sw || dh > sh) && (!keySrc || srcKey == 0))
        if (const auto hd = uiPack().find(src)) {
            const int k = hd->w / src.w;
            return blt(dst, dr, *hd, Rect{sr.left * k, sr.top * k, sr.right * k, sr.bottom * k},
                       flags, srcKey, dstKey, filter);
        }
#endif

    // ---- THE STRETCHED-SPRITE CACHE (above) -------------------------------
    if (keySrc && !keyDest && (sw != dw || sh != dh) && static_cast<long>(dw) * dh >= kBigBlitPixels &&
        !t_spriteFilling) {
        const std::uint64_t srcHash = rectHash(src, sr);
        SpriteLock lock(g_spriteMu);
        ++g_spriteTick;
        SpriteCache* hit = nullptr;
        for (auto& c : g_sprites)
            if (c.hash == srcHash && c.sw == sw && c.sh == sh && c.dw == dw && c.dh == dh && c.key == srcKey &&
                c.filter == filter) { hit = &c; break; }
        if (!hit) {
            SpriteCache c;
            c.hash = srcHash; c.sw = sw; c.sh = sh; c.dw = dw; c.dh = dh; c.key = srcKey; c.filter = filter;
            c.layer.capture(dw, dh, [&](Surface& fill) {
                blt(fill, {0, 0, dw, dh}, src, sr, flags, srcKey, dstKey, filter);
            });
            // at most eight: the least recently used goes
            if (g_sprites.size() >= 8)
                g_sprites.erase(std::min_element(g_sprites.begin(), g_sprites.end(),
                                                 [](const SpriteCache& l, const SpriteCache& r) {
                                                     return l.used < r.used;
                                                 }));
            g_sprites.push_back(std::move(c));
            hit = &g_sprites.back();
        }
        hit->used = g_spriteTick;
        hit->layer.apply(dst, dr.left, dr.top);
        return true;
    }

    // ---- THE INTERFACE SCALING ENHANCEMENT (surface.h) -------------------
    //
    // Only when the rectangles differ in size: at 1:1 the loop below is a row
    // copy and must stay bit-identical, which is what makes the authored
    // 640x480 frame the same picture in both modes.
    //
    // **The colour key decides the SILHOUETTE and the blend decides the
    // COLOUR, and they are kept apart.** Blending a key texel into its
    // neighbours is what fringes a keyed edge - the same fault the texture
    // filter met at the cutout path (`docs/ASSETS.md` 4) and solved with a
    // premultiplied alpha this 565 surface does not have. So here: the
    // NEAREST tap, the one the unfiltered path would have taken, says whether
    // the pixel is drawn at all; the four taps around it are averaged with
    // the key ones LEFT OUT and the weights renormalised over the rest. The
    // set of pixels written is therefore identical to nearest, texel for
    // texel, and only their colours move.
    // THE COLUMN TABLES, built once per blit. Every per-pixel
    // `x * sw / dw` and `(x + 0.5) * sw / dw - 0.5` below is a division, and
    // the Vita's Cortex-A9 has NO integer divide instruction: each one is a
    // library call. Per pixel, the start menu scaled to 960x544 spends ~360 ms
    // a frame in them on the console. The same expressions, evaluated once per column
    // instead of once per pixel, so the output is byte-identical.
    thread_local std::vector<int> colNear, colX0, colX1, colWx;
    colNear.resize(static_cast<std::size_t>(dw));
    for (int x = 0; x < dw; ++x) colNear[static_cast<std::size_t>(x)] = sw == dw ? x : x * sw / dw;
    // the CALLER's tables, by reference: inside a row run on a pool worker the
    // `thread_local` names would be THAT thread's copies, empty
    const std::vector<int>& nearCol = colNear;
    const std::vector<int>& x0Col = colX0;
    const std::vector<int>& x1Col = colX1;
    const std::vector<int>& wxCol = colWx;
    if (filter >= 1 && (sw != dw || sh != dh)) {
        // THE WEIGHTS IN FIXED POINT, 16 bits - as a blitter's hardware does.
        // This was double arithmetic per pixel, and the menus' animated cloud,
        // composed at 640x480 and stretched every frame, cost three cores at
        // 3024x1964 (the reader's "300% in the menu", 2026-11-02). The tap
        // positions are the same exact pixel-centre rationals,
        // `((2x + 1) sw - dw) / 2dw`, so the taps are the same; only a
        // colour's last rounding step can differ from the double result.
        constexpr int kOne = 1 << 16;
        const auto tap = [](int i, int s, int d, int& i0, int& i1, int& w) {
            const long long num = static_cast<long long>(2 * i + 1) * s - d;   // fx * 2d
            const long long den = 2LL * d;
            long long f = num >= 0 ? num / den : -((-num + den - 1) / den);   // floor
            w = static_cast<int>(((num - f * den) * kOne + den / 2) / den);
            int a = static_cast<int>(f), b = a + 1;
            i0 = a < 0 ? 0 : a > s - 1 ? s - 1 : a;
            i1 = b < 0 ? 0 : b > s - 1 ? s - 1 : b;
        };
        colX0.resize(static_cast<std::size_t>(dw));
        colX1.resize(static_cast<std::size_t>(dw));
        colWx.resize(static_cast<std::size_t>(dw));
        for (int x = 0; x < dw; ++x)
            tap(x, sw, dw, colX0[static_cast<std::size_t>(x)], colX1[static_cast<std::size_t>(x)],
                colWx[static_cast<std::size_t>(x)]);
        const auto r8 = [](std::uint16_t v) { const int c = (v >> 11) & 31; return (c << 3) | (c >> 2); };
        const auto g8 = [](std::uint16_t v) { const int c = (v >> 5) & 63;  return (c << 2) | (c >> 4); };
        const auto b8 = [](std::uint16_t v) { const int c = v & 31;         return (c << 3) | (c >> 2); };
        // ---- NO KEY: SEPARABLE (the menus' cloud, 640x480 to the window) ----
        // With no key there is nothing to renormalise, and bilinear is two
        // linear passes: each SOURCE row is interpolated across once - a band
        // of output rows shares ~dh/sh of them - and each output pixel is the
        // vertical blend of two such rows, a loop the compiler vectorises.
        // The rows are held at 8 fractional bits and the vertical weight
        // taken to 8 bits, a hardware blitter's precision. Each band computes
        // its own source rows from the source alone, so the pool's split
        // changes nothing (`threads.h`'s rule).
        if (!keySrc && !keyDest) {
            const auto band = [&](std::size_t yFrom, std::size_t yTo) {
                if (yFrom >= yTo) return;
                int ya, yb, wq;
                tap(static_cast<int>(yFrom), sh, dh, ya, yb, wq);
                const int first = ya;
                tap(static_cast<int>(yTo) - 1, sh, dh, ya, yb, wq);
                const int last = std::max(ya, yb);
                const int n = last - first + 1;
                // planar R, G, B rows ALREADY in 565's 5/6-bit ranges, << 8 -
                // the quantisation folded into this pass (a division per
                // source pixel, not per output one), so the vertical loop
                // below is plain int32 arithmetic the compiler vectorises
                // this runner's own (a pool worker's thread_local is its own)
                thread_local std::vector<std::int32_t> hr;
                hr.resize(static_cast<std::size_t>(n) * 3 * dw);
                for (int k = 0; k < n; ++k) {
                    const std::uint16_t* row = &src.px[static_cast<std::size_t>(sr.top + first + k) * src.w + sr.left];
                    std::int32_t* R = &hr[(static_cast<std::size_t>(k) * 3 + 0) * dw];
                    std::int32_t* G = R + dw;
                    std::int32_t* B = G + dw;
                    for (int x = 0; x < dw; ++x) {
                        const std::uint16_t p0 = row[x0Col[static_cast<std::size_t>(x)]];
                        const std::uint16_t p1 = row[x1Col[static_cast<std::size_t>(x)]];
                        const int w1 = wxCol[static_cast<std::size_t>(x)], w0 = kOne - w1;
                        // (8-bit value << 16) * 31 / 255, then >> 8
                        R[x] = ((r8(p0) * w0 + r8(p1) * w1) / 255 * 31 + 128) >> 8;
                        G[x] = ((g8(p0) * w0 + g8(p1) * w1) / 255 * 63 + 128) >> 8;
                        B[x] = ((b8(p0) * w0 + b8(p1) * w1) / 255 * 31 + 128) >> 8;
                    }
                }
                for (int y = static_cast<int>(yFrom); y < static_cast<int>(yTo); ++y) {
                    int y0, y1, wy;
                    tap(y, sh, dh, y0, y1, wy);
                    const int v1 = (wy + 128) >> 8, v0 = 256 - v1;
                    const std::int32_t* R0 = &hr[(static_cast<std::size_t>(y0 - first) * 3) * dw];
                    const std::int32_t* R1 = &hr[(static_cast<std::size_t>(y1 - first) * 3) * dw];
                    const std::int32_t *G0 = R0 + dw, *B0 = G0 + dw, *G1 = R1 + dw, *B1 = G1 + dw;
                    std::uint16_t* out = &dst.px[static_cast<std::size_t>(dr.top + y) * dst.w + dr.left];
                    for (int x = 0; x < dw; ++x) {
                        // (value << 8) * (weight of 256) = value << 16
                        const int r = (R0[x] * v0 + R1[x] * v1 + (1 << 15)) >> 16;
                        const int g = (G0[x] * v0 + G1[x] * v1 + (1 << 15)) >> 16;
                        const int b = (B0[x] * v0 + B1[x] * v1 + (1 << 15)) >> 16;
                        out[x] = static_cast<std::uint16_t>((r << 11) | (g << 5) | b);
                    }
                }
            };
            rows(dw, dh, band);
            return true;
        }
        // THE ROWS ON THE THREAD POOL: each is computed alone and writes only
        // itself (a dest-keyed test reads its own pixel), so split they are
        // byte-identical - and a 640x480 screen stretched to 4K is 8.3
        // million of them. A small blit stays inline.
        const auto rowsF = [&](std::size_t yFrom, std::size_t yTo) {
        for (int y = static_cast<int>(yFrom); y < static_cast<int>(yTo); ++y) {
            // Pixel CENTRES, which is what puts the resampled grid where the
            // nearest one is rather than half a texel off it.
            int y0, y1, wy;
            tap(y, sh, dh, y0, y1, wy);
            const int ny = sh == dh ? y : y * sh / dh;   // the nearest path's row
            const std::uint16_t* row0 = &src.px[static_cast<std::size_t>(sr.top + y0) * src.w + sr.left];
            const std::uint16_t* row1 = &src.px[static_cast<std::size_t>(sr.top + y1) * src.w + sr.left];
            const std::uint16_t* rowN = &src.px[static_cast<std::size_t>(sr.top + ny) * src.w + sr.left];
            std::uint16_t* out = &dst.px[static_cast<std::size_t>(dr.top + y) * dst.w + dr.left];
            for (int x = 0; x < dw; ++x) {
                const std::uint16_t nearest = rowN[nearCol[static_cast<std::size_t>(x)]];
                if (keySrc && nearest == srcKey) continue;
                if (keyDest && out[x] != dstKey) continue;

                const int x0 = x0Col[static_cast<std::size_t>(x)];
                const int x1 = x1Col[static_cast<std::size_t>(x)];
                const int wx = wxCol[static_cast<std::size_t>(x)];
                const std::uint16_t t[4] = {row0[x0], row0[x1], row1[x0], row1[x1]};
                // the four weights, summing to kOne (the products in 64 bits
                // before the shift: 2^16 * 2^16 overflows an int)
                const int w11 = static_cast<int>((static_cast<long long>(wx) * wy) >> 16);
                const int w10 = wx - w11, w01 = wy - w11;
                const int w[4] = {kOne - wx - wy + w11, w10, w01, w11};
                int r = 0, g = 0, b = 0, tot = 0;
                for (int i = 0; i < 4; ++i) {
                    if (keySrc && t[i] == srcKey) continue;   // never averaged in
                    r += w[i] * r8(t[i]);
                    g += w[i] * g8(t[i]);
                    b += w[i] * b8(t[i]);
                    tot += w[i];
                }
                // `tot` is 0 only when every tap is the key, and the nearest
                // tap is one of the four, so the test above has already
                // continued - but a renormalisation must never divide by a
                // number it has not looked at. A weight can round to 0 too.
                if (tot <= 0) { out[x] = nearest; continue; }
                // ROUNDED, not dithered: the dither is for a 3D frame's
                // gradients, and noise in a menu is noise in a menu.
                if (tot == kOne)
                    out[x] = quantise888((r + kOne / 2) >> 16, (g + kOne / 2) >> 16, (b + kOne / 2) >> 16);
                else
                    out[x] = quantise888((r + tot / 2) / tot, (g + tot / 2) / tot, (b + tot / 2) / tot);
            }
        }
        };
        rows(dw, dh, rowsF);
        return true;
    }

    rows(dw, dh, [&](std::size_t yFrom, std::size_t yTo) {
    for (int y = static_cast<int>(yFrom); y < static_cast<int>(yTo); ++y) {
        // nearest-neighbour, which is what a 1999 blitter does; when the
        // rectangles are the same size this is the identity and the whole
        // thing is a row copy with two optional tests.
        const int sy = sr.top + (sh == dh ? y : y * sh / dh);
        for (int x = 0; x < dw; ++x) {
            const int sx = sr.left + nearCol[static_cast<std::size_t>(x)];
            const std::uint16_t v = src.at(sx, sy);
            if (keySrc && v == srcKey) continue;
            if (keyDest && dst.at(dr.left + x, dr.top + y) != dstKey) continue;
            dst.set(dr.left + x, dr.top + y, v);
        }
    }
    });
    return true;
}


// ------------------------------------------------- the software rasterizer

// The opaque Gouraud line (flags 0x18: bit 3 shades, bits 0..2 clear so no
// blend): Bresenham, the last pixel left out, colour lerped per step.
void drawLineGouraud(Surface& fb, int x0, int y0, int x1, int y1,
                 std::uint32_t c0, std::uint32_t c1) {
    const int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
    const int n = std::max(dx, dy);
    if (n == 0) return;
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy, x = x0, y = y0;
    for (int i = 0; i < n; ++i) {
        if (x >= 0 && y >= 0 && x < fb.w && y < fb.h) {
            const auto ch = [&](int s) {
                const int a = static_cast<int>((c0 >> s) & 255u);
                const int b = static_cast<int>((c1 >> s) & 255u);
                return a + (b - a) * i / n;
            };
            fb.px[static_cast<std::size_t>(y) * static_cast<std::size_t>(fb.w) +
                  static_cast<std::size_t>(x)] = rgb565(ch(16), ch(8), ch(0));
        }
        const int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 < dx)  { err += dx; y += sy; }
    }
}


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
            // on an overlay frame's KEY pixel (`ui/overlay.h`) the 50% blend
            // and the add run on the side planes: the law on C, and the 50%'s
            // halving on M. The SUBTRACT is not affine in an unsigned C; no
            // caller draws one over the world, so it is left as it was.
            OverlayPlanes& ov = overlayPlanes();
            const std::size_t ovI = static_cast<std::size_t>(py) * static_cast<std::size_t>(dst.w) +
                                    static_cast<std::size_t>(px);
            const bool ovKey = ov.on && (mode == 1 || mode == 2) && dst.px[ovI] == kOverlayKey;
            if (ovKey) {
                ov.row(ovI);
                if (mode == 1) ov.m[ovI] = static_cast<std::uint8_t>(ov.m[ovI] >> 1);
            }
            const std::uint16_t d = ovKey ? ov.c[ovI] : dst.at(px, py);
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
            if (ovKey) ov.c[ovI] = v; else dst.set(px, py, v);
            ++written;
        }
    }
    return written;
}

}  // namespace omk

namespace omk {

namespace {

// One channel of `quantise888Dither` for every value and every matrix cell:
// the same offset, clamp and rounding, shifted into its place in the 565 word.
struct DitherTables {
    std::uint16_t r[16][256], g[16][256], b[16][256];
    DitherTables() {
        const auto cl = [](int v) { return v < 0 ? 0 : v > 255 ? 255 : v; };
        for (int cell = 0; cell < 16; ++cell) {
            const int t = kBayer4[cell] - 8;
            const int d5 = (t * 8) / 16, d6 = (t * 4) / 16;
            for (int v = 0; v < 256; ++v) {
                r[cell][v] = static_cast<std::uint16_t>(((cl(v + d5) * 31 + 127) / 255) << 11);
                g[cell][v] = static_cast<std::uint16_t>(((cl(v + d6) * 63 + 127) / 255) << 5);
                b[cell][v] = static_cast<std::uint16_t>((cl(v + d5) * 31 + 127) / 255);
            }
        }
    }
};

const DitherTables& ditherTables() {
    static const DitherTables t;
    return t;
}

}  // namespace

void quantise888DitherRow(const unsigned char* rgba, int w, int y, std::uint16_t* out) {
    const DitherTables& t = ditherTables();
    const int row = (y & 3) * 4;
    for (int x = 0; x < w; ++x) {
        const int cell = row + (x & 3);
        const unsigned char* s = rgba + 4 * static_cast<std::size_t>(x);
        out[x] = static_cast<std::uint16_t>(t.r[cell][s[0]] | t.g[cell][s[1]] | t.b[cell][s[2]]);
    }
}

const unsigned char* expand565Rgba() {
    static const std::vector<unsigned char> table = [] {
        std::vector<unsigned char> e(65536u * 4u);
        for (unsigned v = 0; v < 65536u; ++v) {
            const int r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
            e[4 * v + 0] = static_cast<unsigned char>((r << 3) | (r >> 2));
            e[4 * v + 1] = static_cast<unsigned char>((g << 2) | (g >> 4));
            e[4 * v + 2] = static_cast<unsigned char>((b << 3) | (b >> 3));
            e[4 * v + 3] = 255;
        }
        return e;
    }();
    return table.data();
}

}  // namespace omk
