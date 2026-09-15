// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/interference.h"

#include <vector>

namespace omk {

int UiInterference::rand() {
    seed = seed * 214013u + 2531011u;
    return static_cast<int>((seed >> 16) & 0x7FFF);
}

namespace {

// One row's `memmove(row, row + shift, w)`: every pixel of [x, x + w) takes
// the value `shift` pixels to its right, read before any is written.
void shiftRow(Surface& fb, int x, int row, int w, int shift, std::vector<std::uint16_t>& tmp) {
    if (row < 0 || row >= fb.h || w <= 0) return;
    tmp.resize(static_cast<std::size_t>(w));
    for (int i = 0; i < w; ++i) {
        const int sx = x + i + shift;
        const int dx = x + i;
        tmp[static_cast<std::size_t>(i)] =
            (sx >= 0 && sx < fb.w) ? fb.at(sx, row)
            : (dx >= 0 && dx < fb.w) ? fb.at(dx, row) : 0;
    }
    for (int i = 0; i < w; ++i) {
        const int dx = x + i;
        if (dx >= 0 && dx < fb.w) fb.set(dx, row, tmp[static_cast<std::size_t>(i)]);
    }
}

std::uint16_t grey(int v) { return rgb565(v, v, v); }

}  // namespace

UiInterference::Frame UiInterference::apply(Surface& fb, int x, int y, int w, int h) {
    Frame f;
    std::vector<std::uint16_t> tmp;
    const std::uint16_t scatter[4] = {grey(0x80), grey(0x50), grey(0x04), grey(0x60)};
    const std::uint16_t orMask[8]  = {grey(0x08), grey(0x08), grey(0x10), grey(0x10),
                                      grey(0x20), grey(0x20), grey(0x40), grey(0x80)};

    // 1. the row pairs
    if (rand() % 25 == 0) {
        f.shifted = true;
        if (h > 0) {
            int row = y;
            for (unsigned pairs = static_cast<unsigned>(h + 1) >> 1; pairs; --pairs) {
                const int s = rand() % 4 - 2;
                shiftRow(fb, x, row, w, s, tmp);
                shiftRow(fb, x, row + 1, w, s, tmp);
                row += 2;
            }
        }
    }

    // 2. the scatter
    int d = density;
    if (d > 0) {
        for (int yy = y; yy < y + h; yy += rand() % (density + 40)) {
            if (x < x + w) {
                int xx = x;
                do {
                    const std::uint16_t c = scatter[rand() % 4];
                    if (xx >= 0 && xx < fb.w && yy >= 0 && yy < fb.h) {
                        fb.set(xx, yy, c);
                        ++f.scattered;
                    }
                    xx += rand() % (density + 80);
                } while (xx < x + w);
            }
        }
        d = density;
    }

    // 3. the band
    if (band) {
        static constexpr int kLess[8] = {4, 4, 0, 2, 0, 4, 2, 4};
        f.bandTop = y + bandY;
        for (int k = 0; k < 8; ++k) {
            const int row = y + bandY + 2 * k;
            shiftRow(fb, x, row, w, rand() % 8 - kLess[k], tmp);
            for (int xx = x; xx < x + w; xx += rand() % 4 + 1) {
                if (xx >= 0 && xx < fb.w && row >= 0 && row < fb.h) {
                    fb.set(xx, row, static_cast<std::uint16_t>(fb.at(xx, row) | orMask[k]));
                    ++f.ored;
                }
            }
        }
        d = density;
    }

    // 4. the state
    bool zero = d == 0;
    bandY += 4;
    if (d > 0) {
        density = d - 1;
        zero = d == 1;
    }
    if (zero && rand() % 30 == 0) density = 8;
    if (bandY + 16 > h) {
        bandY = 0;
        band = rand() % 3 == 0;
    }
    return f;
}

}  // namespace omk
