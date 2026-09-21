// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hudbar.h"

#include "ui/overlay.h"
#include "ui/text.h"

#include <algorithm>
#include <cmath>
#include <vector>

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
            // on an overlay frame's KEY pixel the law runs on the side planes
            // (`ui/overlay.h`): itself on C, its factor on M. The multiply's
            // factor is taken from GREEN - M is one channel, and every
            // multiply the HUD draws is a grey (0xE0E0E0).
            OverlayPlanes& ov = overlayPlanes();
            const std::size_t ovI = static_cast<std::size_t>(py) * static_cast<std::size_t>(dst.w) +
                                    static_cast<std::size_t>(px);
            const bool ovKey = ov.on && dst.px[ovI] == kOverlayKey;
            if (ovKey && (flags & 7u)) {
                ov.row(ovI);
                if (flags & 4u)      ov.m[ovI] = static_cast<std::uint8_t>(ov.m[ovI] * a / 255);
                else if (flags & 2u) ov.m[ovI] = static_cast<std::uint8_t>(ov.m[ovI] * (255 - sg) / 255);
            }
            std::uint16_t& d = ovKey && (flags & 7u) ? ov.c[ovI] : dst.px[ovI];
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
    // `IAM\SNEAK`, the same pass: the NUL-separated strings, labels 26..31
    // filtered to the `_UPPER` class and ranks 36..40 kept whole.
    const auto raw = fs.read("IAM/SNEAK");
    std::vector<std::string> parts(1);
    for (const auto b : raw) {
        const char c = static_cast<char>(b);
        if (c == 0) parts.emplace_back(); else parts.back().push_back(c);
    }
    const auto at = [&](std::size_t i) { return i < parts.size() ? parts[i] : std::string(); };
    for (int i = 0; i < 6; ++i) {
        std::string f;
        for (const char c : at(static_cast<std::size_t>(26 + i)))
            if (c >= 'A' && c <= 'Z') f.push_back(c);   // `isctype(c, _UPPER)`
        labels_[i] = f;
    }
    for (int i = 0; i < 5; ++i) ranks_[i] = at(static_cast<std::size_t>(36 + i));
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

// `Hud_DrawBar`'s mode-1 arm WHOLE (`ui/hudbar.h`): the horizontal breath
// gauge the water asks for. Each family is drawn in reverse submission order,
// the way one I2D layer's HEAD cache draws it.
void HudBar::breath(Surface& fb, int p, HudBarFrame& out) {
    const int W = fb.w, H = fb.h;
    const auto sx = [&](int v) { return scaleX(v, W); };
    const auto sy = [&](int v) { return scaleY(v, H); };

    // layer 2, flags 4, colour 0: the column, then the two end diamonds
    const int cx[4] = {sx(100), sx(100), sx(540), sx(540)};
    // the engine's own `Hud_ScaleX(24)` for the vertical centre - see the header
    const int cy[4] = {sx(24) - sy(7), sx(24) + sy(7), sx(24) + sy(7), sx(24) - sy(7)};
    fillQuadD3d(fb, cx, cy, 0u, 4u);
    ++out.quads;
    for (const int at : {540, 100}) {
        const int dx[4] = {sx(at) - sx(17), sx(at), sx(at) + sx(17), sx(at)};
        const int dy[4] = {sy(24), sy(24) - sy(17), sy(24), sy(24) + sy(17)};
        fillQuadD3d(fb, dx, dy, 0u, 4u);
        ++out.quads;
    }

    const int fill = sx(100) + p * (sx(540) - sx(100)) / 100;
    out.top = fill;                      // the fill's RIGHT edge, this bar's `top`

    // layer 3: jaugeg's two blits, key source, indexed with jauge1's size
    if (jaugeG_.valid()) {
        struct Blit { int sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1; };
        const int split = bmpW_ * p / 100;
        const Blit empty{split, 0, bmpW_, 3, fill, sy(24) - sy(3), sx(540), sy(24) + sy(3)};
        const Blit full{0, column_, split, column_ + 3,
                        sx(100), sy(24) - sy(3), fill, sy(24) + sy(3)};
        for (const Blit* k : {&full, &empty}) {
            // `I2D_BlitBitmap`'s three refusals - no test on the destination Y
            if (k->sx0 >= k->sx1 || k->sy0 >= k->sy1 || k->dx0 >= k->dx1) continue;
            if (blt(fb, Rect{k->dx0, k->dy0, k->dx1, k->dy1}, jaugeG_,
                    Rect{k->sx0, k->sy0, k->sx1, k->sy1}, kBltWait | kBltKeySrc, 0, 0))
                ++out.blits;
        }
    }

    // layer 4: the additive blue over the full part, then the grey over the empty
    const int fy[4] = {sy(24) - sy(3), sy(24) + sy(3), sy(24) + sy(3), sy(24) - sy(3)};
    const int fx[4] = {sx(100), sx(100), fill, fill};
    fillQuadD3d(fb, fx, fy, 0x103080u, 1u);
    const int ex[4] = {fill, fill, sx(540), sx(540)};
    fillQuadD3d(fb, ex, fy, 0xE0E0E0u, 2u);
    out.quads += 2;
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

void HudBar::refreshCard(const int props[6], double nowMs) {
    for (int i = 0; i < 6; ++i) card_[i] = props[i];
    card_[1] = props[1] / 41;                    // `dword_530CB4 = v2 / 41`
    stampMs_ = nowMs;
}

void HudBar::card(Surface& fb, const TextLayout& text, HudBarFrame& out) {
    const int W = fb.w, H = fb.h;
    const auto sx = [&](int v) { return scaleX(v, W); };
    const auto sy = [&](int v) { return scaleY(v, H); };
    struct Bar { int y, v; };
    Bar bars[6];
    int nb = 0;
    for (int row = 0; row < 6; ++row) {
        if (row == 1) continue;
        const int y = 348 + 20 * row;
        bars[nb++] = {y, card_[row]};
        // layer 2, flags 4, colour 0: the frame
        const int fx[4] = {sx(70), sx(70), sx(170), sx(170)};
        const int fy[4] = {sy(y) + sy(4), sy(y) - sy(4), sy(y) - sy(4), sy(y) + sy(4)};
        fillQuadD3d(fb, fx, fy, 0u, 4u);
    }
    // layer 3 in the HEAD cache's order: the first row's blit, then the rest
    // reversed as tint-then-blit, then the first row's tint
    const auto blit = [&](const Bar& b) {
        if (!jaugeG_.valid()) return;
        const int s0 = bmpH_ * (200 - b.v) / 200, s1 = bmpH_;
        const int d0 = sx(70), d1 = sx(70) + b.v * (sx(170) - sx(70)) / 200;
        if (s0 >= s1 || column_ >= column_ + 3 || d0 >= d1) return;   // I2D_BlitBitmap's refusals
        if (blt(fb, Rect{d0, sy(b.y) - sy(2), d1, sy(b.y) + sy(2)}, jaugeG_,
                Rect{s0, column_, s1, column_ + 3}, kBltWait | kBltKeySrc, 0, 0))
            ++out.cardRows;
    };
    const auto tint = [&](const Bar& b, std::uint32_t rgb) {
        const int x1 = sx(70) + b.v * (sx(170) - sx(70)) / 200;
        const int qx[4] = {sx(70), sx(70), x1, x1};
        const int qy[4] = {sy(b.y) + sy(2), sy(b.y) - sy(2), sy(b.y) - sy(2), sy(b.y) + sy(2)};
        fillQuadD3d(fb, qx, qy, rgb, 1u);
    };
    const int rowOf[5] = {0, 2, 3, 4, 5};
    if (nb > 0) blit(bars[0]);
    for (int i = nb - 1; i >= 1; --i) {
        tint(bars[i], kHudCardRgb[rowOf[i]]);
        blit(bars[i]);
    }
    if (nb > 0) tint(bars[0], kHudCardRgb[0]);

    // the text: the labels, and row 1's rank
    for (int row = 0; row < 6; ++row) {
        const int y = 348 + 20 * row;
        TextBlock tb;
        tb.font = tb.altFont = 'C';
        tb.screenW = W; tb.screenH = H;
        if (row == 1 && card_[1] >= 0 && card_[1] < 5) {
            tb.left = sx(70); tb.right = sx(510);
            tb.top = sy(y) - 2 * sy(4); tb.bottom = sy(y) + 6 * sy(4);
            tb.style = 2;
            text.layOutBlock(&fb, ranks_[card_[1]], tb);
            ++out.cardText;
        }
        tb.left = sx(70) - 2 * sx(12); tb.right = sx(70) - sx(4);
        tb.top = sy(y) - 2 * sy(4); tb.bottom = sy(y) + 6 * sy(4);
        tb.style = 4;
        text.layOutBlock(&fb, labels_[row], tb);
        ++out.cardText;
    }
}

HudBarFrame HudBar::draw(Surface& fb, int value, int max, int side, int mode,
                         const TextLayout* text, double nowMs) {
    HudBarFrame out;
    int v = value < 0 ? 0 : value;
    if (v > max) v = max;
    out.percent = max > 0 ? 100 * v / max : 0;
    // `dword_530CA8 = ++dword_531050 % (dword_530C98 - 3)` - before the mode
    // dispatch, so every call advances it
    ++counter_;
    if (bmpW_ > 3) column_ = counter_ % (bmpW_ - 3);
    out.column = column_;
    if (mode == 1) { breath(fb, out.percent, out); return out; }
    if (mode == 2 && text && nowMs < stampMs_ + 4000.0) card(fb, *text, out);
    out.top = gauge(fb, out.percent, side, out);
    out.sparks = sparks(fb, out.top, side);
    return out;
}

}  // namespace omk
