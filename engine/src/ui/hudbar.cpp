// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hudbar.h"

#include <algorithm>
#include <cmath>

namespace omk {

namespace {

// `Hud_ScaleX` / `Hud_ScaleY` (0x00447A50 / 0x00447AB0) without the swim arm;
// `sub_4480D0` and `sub_446E20` inline exactly this for every coordinate
int scaleX(int v, int w) { return w == 640 ? v : v * w / 640; }
int scaleY(int v, int h) { return h == 480 ? v : v * h / 480; }

// `dword_4C79F0`, the sparks' rgb by side
constexpr std::uint32_t kSparkRgb[2] = {0x18AD8Cu, 0x8C1008u};

void unpack(std::uint16_t d, int& r, int& g, int& b) {
    r = ((d >> 11) & 31) * 255 / 31;
    g = ((d >> 5) & 63) * 255 / 63;
    b = (d & 31) * 255 / 31;
}

std::uint16_t pack(int r, int g, int b) {
    r = std::clamp(r, 0, 255); g = std::clamp(g, 0, 255); b = std::clamp(b, 0, 255);
    return static_cast<std::uint16_t>(((r * 31 / 255) << 11) | ((g * 63 / 255) << 5) |
                                      (b * 31 / 255));
}

}  // namespace

long fillQuadD3d(Surface& dst, const int x[4], const int y[4],
                 std::uint32_t argb, std::uint32_t flags) {
    if (!dst.valid()) return 0;
    const int y0 = std::max(0, std::min({y[0], y[1], y[2], y[3]}));
    const int y1 = std::min(dst.h, std::max({y[0], y[1], y[2], y[3]}));
    const int sr = static_cast<int>((argb >> 16) & 255u);
    const int sg = static_cast<int>((argb >> 8) & 255u);
    const int sb = static_cast<int>(argb & 255u);
    const int a  = static_cast<int>((argb >> 24) & 255u);
    long n = 0;
    for (int py = y0; py < y1; ++py) {
        // the row's span: every edge whose half-open [top, bottom) holds the
        // pixel centre, which is the top-left rule's vertical half
        double xl = 1e30, xr = -1e30;
        bool hit = false;
        for (int e = 0; e < 4; ++e) {
            const int ax = x[e], ay = y[e], bx = x[(e + 1) & 3], by = y[(e + 1) & 3];
            if (ay == by || py < std::min(ay, by) || py >= std::max(ay, by)) continue;
            const double xe = ax + double(py - ay) * double(bx - ax) / double(by - ay);
            xl = std::min(xl, xe);
            xr = std::max(xr, xe);
            hit = true;
        }
        if (!hit) continue;
        const int xa = std::max(0, static_cast<int>(std::ceil(xl)));
        const int xb = std::min(dst.w, static_cast<int>(std::ceil(xr)));
        for (int px = xa; px < xb; ++px) {
            std::uint16_t& d = dst.px[static_cast<std::size_t>(py) * static_cast<std::size_t>(dst.w) +
                                     static_cast<std::size_t>(px)];
            int dr, dg, db;
            unpack(d, dr, dg, db);
            // `sub_480AC0`: bit 2 is tested last and wins, then bit 1, then 0
            if (flags & 4u)
                d = pack((sr * (255 - a) + dr * a) / 255, (sg * (255 - a) + dg * a) / 255,
                         (sb * (255 - a) + db * a) / 255);
            else if (flags & 2u)
                d = pack(dr * (255 - sr) / 255, dg * (255 - sg) / 255, db * (255 - sb) / 255);
            else if (flags & 1u)
                d = pack(dr + sr, dg + sg, db + sb);
            else
                d = pack(sr, sg, sb);
            ++n;
        }
    }
    return n;
}

int HudBar::rand_() {
    seed_ = seed_ * 214013u + 2531011u;
    return static_cast<int>((seed_ >> 16) & 0x7FFFu);
}

bool HudBar::load(const DataFs& fs) {
    jauge_[0] = surfaceFromBmp(fs.read("IMAGES/jauge1.bmp"));
    bmpW_ = jauge_[0].w;
    bmpH_ = jauge_[0].h;
    jauge_[1] = surfaceFromBmp(fs.read("IMAGES/jauge2.bmp"));
    jaugeG_   = surfaceFromBmp(fs.read("IMAGES/jaugeg.bmp"));
    return jauge_[0].valid();
}

void HudBar::refresh(int side, int y, int screenW) {
    const int base = scaleX(24, screenW);
    for (HudSpark& s : sparks_[side & 1]) {
        int j = rand_() % 3 - 1;
        if (screenW != 640) j = j * screenW / 640;   // the 0x66666667 multiply
        s.x = (side ? screenW - base : base) + j;
        s.y = y;
        s.age = 0x20000000u;
    }
}

int HudBar::gauge(Surface& fb, int p, int side, HudBarFrame& out) {
    const int W = fb.w, H = fb.h;
    const auto sx = [&](int v) { return scaleX(v, W); };
    const auto sy = [&](int v) { return scaleY(v, H); };
    const auto mx = [&](int v) { return side ? W - v : v; };

    // layer 2, flags 4, colour 0: the two end diamonds and the column. The
    // HEAD cache draws a layer's nodes in reverse submission order, so the
    // column (submitted third) goes down first.
    const int dx[4] = {mx(sx(24) - sx(17)), mx(sx(24)), mx(sx(17) + sx(24)), mx(sx(24))};
    const int topY[4] = {sy(22), sy(22) - sy(17), sy(22), sy(22) + sy(17)};
    const int botY[4] = {sy(458), sy(458) - sy(17), sy(458), sy(458) + sy(17)};
    const int cx[4] = {mx(sx(24) - sx(7)), mx(sx(7) + sx(24)), mx(sx(7) + sx(24)),
                       mx(sx(24) - sx(7))};
    const int cy[4] = {sy(22), sy(22), sy(458), sy(458)};
    fillQuadD3d(fb, cx, cy, 0u, 4u);
    fillQuadD3d(fb, dx, botY, 0u, 4u);
    fillQuadD3d(fb, dx, topY, 0u, 4u);
    out.quads += 3;

    const int e = 100 - p;
    const int top = sy(22) + e * (sy(458) - sy(22)) / 100;

    // layer 3: the jauge's two blits, key source. The empty part reads source
    // columns 0..3 over the bitmap's top e%; the full part reads the
    // SCROLLING column over the rest. Side 1 reads the bitmap mirrored.
    const Surface& bmp = jauge_[side & 1];
    if (bmp.valid()) {
        struct Blit { int sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1; };
        const int split = e * bmpH_ / 100;
        int dx0 = sx(24) - sx(3), dx1 = sx(24) + sx(3);
        if (side) { dx0 = W - (sx(24) + sx(3)); dx1 = W - (sx(24) - sx(3)); }
        Blit a{0, 0, 3, split, dx0, sy(22) + sy(3), dx1, top + sy(3)};
        Blit b{column_, split, column_ + 3, bmpH_, dx0, top - sy(3), dx1, sy(458) - sy(3)};
        if (side) {
            a.sx0 = bmpW_ - 3;           a.sx1 = bmpW_;
            b.sx0 = bmpW_ - column_ - 3; b.sx1 = bmpW_ - column_;
        }
        for (const Blit* k : {&b, &a}) {
            // `I2D_BlitBitmap`'s three refusals - no test on the destination Y
            if (k->sx0 >= k->sx1 || k->sy0 >= k->sy1 || k->dx0 >= k->dx1) continue;
            if (blt(fb, Rect{k->dx0, k->dy0, k->dx1, k->dy1}, bmp,
                    Rect{k->sx0, k->sy0, k->sx1, k->sy1}, kBltWait | kBltKeySrc, 0, 0))
                ++out.blits;
        }
    }

    // layer 4, flags 2, colour 0xE0E0E0: over the empty part
    const int ex[4] = {mx(sx(24) - sx(3)), mx(sx(24) + sx(3)), mx(sx(24) + sx(3)),
                       mx(sx(24) - sx(3))};
    const int ey[4] = {sy(22) + sy(3), sy(22) + sy(3), top, top};
    fillQuadD3d(fb, ex, ey, 0xE0E0E0u, 2u);
    ++out.quads;
    return top;
}

int HudBar::sparks(Surface& fb, int top, int side) {
    if (top == 458) return 0;                    // the literal, unscaled
    const int W = fb.w;
    const int dir = side ? 1 : -1;
    struct Live { int x, y; std::uint32_t c; };
    Live live[kHudSparks];
    int n = 0;
    for (HudSpark& s : sparks_[side & 1]) {
        s.x += dir * (rand_() % 3);
        s.y -= rand_() % 4;
        s.age += 0x04000000u;
        if (s.x <= 2 || s.x >= W - 2 || s.y <= 2 || s.age > 0xF0000000u) {
            s.y = top;
            const int base = scaleX(24, W);
            int j = rand_() % 3 - 1;
            if (W != 640) j = j * W / 640;
            s.x = (side ? W - base : base) + j;
            s.age = 0x20000000u;
        } else {
            live[n++] = {s.x, s.y, s.age | kSparkRgb[side & 1]};
        }
    }
    // layer 5, flags 4: a radius-2 diamond each, in the HEAD cache's order
    for (int i = n - 1; i >= 0; --i) {
        const Live& l = live[i];
        const int xs[4] = {l.x - 2, l.x, l.x + 2, l.x};
        const int ys[4] = {l.y, l.y - 2, l.y, l.y + 2};
        fillQuadD3d(fb, xs, ys, l.c, 4u);
    }
    return n;
}

HudBarFrame HudBar::draw(Surface& fb, int value, int max, int side, int mode) {
    HudBarFrame out;
    int v = value < 0 ? 0 : value;
    if (v > max) v = max;
    out.percent = max > 0 ? 100 * v / max : 0;
    // `dword_530CA8 = ++dword_531050 % (dword_530C98 - 3)` - before the mode
    // dispatch, so every call advances it
    ++counter_;
    if (bmpW_ > 3) column_ = counter_ % (bmpW_ - 3);
    out.column = column_;
    if (mode == 1) return out;                   // the horizontal bar: not ported
    out.top = gauge(fb, out.percent, side, out);
    out.sparks = sparks(fb, out.top, side);
    return out;
}

}  // namespace omk
