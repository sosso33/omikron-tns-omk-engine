// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/cursor.h"

#include <cmath>

namespace omk {
namespace {

// `sub_479220` / `sub_479340`'s ease, and the clamp is the interesting part:
// the step is `(target - cur) * speed * dt`, and then flt_4BCAB4 / flt_4BCAB8
// force it to at least +1 or at most -1. Without that a value one pixel short
// of its target never arrives, because `_ftol` truncates the fraction away.
int ease(int cur, int target, float speed, float dt) {
    if (cur == target) return cur;
    float step = static_cast<float>(target - cur) * speed * dt;
    if (step > 0.0f && step < 1.0f) step = 1.0f;
    else if (step <= 0.0f && step >= -1.0f) step = -1.0f;
    return cur + static_cast<int>(step);
}

}  // namespace

std::uint32_t UiCursor::rnd() {
    seed_ = seed_ * 1103515245u + 12345u;
    return (seed_ >> 16) & 0x7FFFu;
}

// `sub_478FE0`. The flag word is picked from the item's SHAPE - an elongated
// item (either side twice the other) gets 0x398400, a squarish one 0x3D8400,
// whose extra 0x40000 halves the orbit radius. Both carry 0x400 (orbit),
// 0x8000 (the trailing lerp speed), 0x10000 (random radii), 0x80000 (radius
// from the SHORT side) and 0x200000 (the renderer); neither carries 0x800
// (the rand jitter), 0x1000/0x2000 (a varying size factor) or 0x400000 (the
// second renderer), so those arms are read and not reachable here.
void UiCursor::rebuild(int cx, int cy, int w, int h) {
    cx_ = cx; cy_ = cy; w_ = w; h_ = h;
    flags_ = (w >= 2 * h || h >= 2 * w) ? 0x398400u : 0x3D8400u;
    int base = (flags_ & 0x80000u) ? (w >= h ? h : w) : w;
    if (flags_ & 0x40000u) base /= 2;
    for (int i = 0; i < 16; ++i) {
        El& e = el_[i];
        e.sizeFactor = 1.0f;                                  // no 0x1000/0x2000
        // 0x8000: flt_4BCAD0 - i * flt_4BCACC * flt_4BCAC4 = 0.5 - i*0.0125.
        // The later elements chase more slowly, which is what makes the set
        // trail behind the focus rather than move as one block.
        e.lerpSpeed = 0.5f - static_cast<float>(i) * 0.2f * 0.0625f;
        e.angle = static_cast<float>(rnd() % 360);
        e.angularSpeed = static_cast<float>(rnd() % 300 + 10) * 0.001f;
        e.rx = base >= 1 ? static_cast<int>(rnd() % static_cast<unsigned>(base)) : 0;
        e.ry = base >= 1 ? static_cast<int>(rnd() % static_cast<unsigned>(base)) : 0;
        // x/y/w/h are deliberately NOT reset - the initialiser writes only the
        // six fields above, so the elements ease in from wherever they were,
        // which is the sweep a player sees when the focus moves.
    }
}

const std::vector<UiCursor::Quad>&
UiCursor::tick(std::uint32_t item, int cx, int cy, int w, int h,
               int r, int g, int b, long dtMs) {
    if (item != item_ || cx != cx_ || cy != cy_ || w != w_ || h != h_) {
        if (item != item_) { seed_ = item ? item : 1u; item_ = item; }
        rebuild(cx, cy, w, h);
    }
    // The engine's delta is in 30ths of a second (`docs/BOOT.md` 4), which is
    // what `dword_4C30E0` holds and what every ease below is scaled by.
    const float dt = static_cast<float>(dtMs) * 30.0f / 1000.0f;

    // OSCILLATOR 3: period 500, lo 230, hi 235, a triangle (`sub_42B700`).
    oscMs_ = (oscMs_ + dtMs) % 500;
    {
        const int half = 500 / 2, lo = 230, hi = 235;
        alpha_ = oscMs_ < half ? lo + (hi - lo) * static_cast<int>(oscMs_) / half
                               : hi - (hi - lo) * (static_cast<int>(oscMs_) - half) / half;
    }

    quads_.clear();
    for (int i = 0; i < 16; ++i) {
        El& e = el_[i];
        e.x = ease(e.x, cx_, e.lerpSpeed, dt);            // sub_479220
        e.y = ease(e.y, cy_, e.lerpSpeed, dt);
        e.drawX = e.x;                                     // ...and its tail
        e.drawY = e.y;
        const int tw = static_cast<int>(static_cast<float>(w_) * e.sizeFactor);
        const int th = static_cast<int>(static_cast<float>(h_) * e.sizeFactor);
        e.w = ease(e.w, tw, e.lerpSpeed, dt);              // sub_479340
        e.h = ease(e.h, th, e.lerpSpeed, dt);
        if (flags_ & 0x400u) {                             // the orbit
            e.angle += e.angularSpeed * dt;
            if (e.angle >= 360.0f) e.angle -= 360.0f;
            else if (e.angle < 0.0f) e.angle += 360.0f;
            // Degrees into `fcos`/`fsin`, which the engine does not convert.
            e.drawX += static_cast<int>(static_cast<float>(e.rx) * std::cos(e.angle));
            e.drawY += static_cast<int>(static_cast<float>(e.ry) * std::sin(e.angle));
        }
        quads_.push_back({e.drawX - e.w / 2, e.drawY - e.h / 2,
                          e.drawX + e.w / 2, e.drawY + e.h / 2,
                          r, g, b, alpha_});
    }
    return quads_;
}

}  // namespace omk
