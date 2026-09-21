// SPDX-License-Identifier: GPL-3.0-or-later
// THE OVERLAY'S SIDE PLANES (`todo/vita-port.md` G6 steps 2 and 3). On the GLES
// window a frame with an interface on it is not composed over a readback of
// the world: the world's rows of the frame hold a KEY, and the GPU blends the
// finished frame over its own picture as `C + world * M`. Opaque drawing simply
// overwrites the key (C = the pixel, M = 0). The passes that READ the picture -
// the two dialogue boxes, the two screen fades, the HUD's blended quads - are
// all affine in it (`new = A * old + B`), so on a key pixel they run on these
// planes instead: the law itself on C, which starts at 0, and A on M, which
// starts at 255. Exact up to the GPU's 8-bit rounding, not a case per pass.
// SPARSE: a row of the planes is initialised the first time a pass touches it
// (`row`), so a frame costs the rows its boxes and bands cover, not the screen.
//
// This is the PORT's own device, not the engine's: the original blends on the
// card, which is what the planes hand back to the GPU.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace omk {

constexpr std::uint16_t kOverlayKey = 0xF81F;

struct OverlayPlanes {
    bool on = false;
    int w = 0;
    std::vector<std::uint16_t> c;
    std::vector<std::uint8_t>  m;
    std::vector<std::uint8_t>  rowInit;
    void begin(int width, int height) {
        on = true;
        w = width;
        const std::size_t count = static_cast<std::size_t>(width) * height;
        if (c.size() != count) { c.assign(count, 0); m.assign(count, 255); }
        rowInit.assign(static_cast<std::size_t>(height), 0);
    }
    void row(std::size_t index) {
        const std::size_t y = index / static_cast<std::size_t>(w);
        if (rowInit[y]) return;
        rowInit[y] = 1;
        std::fill(c.begin() + y * w, c.begin() + (y + 1) * w, std::uint16_t(0));
        std::fill(m.begin() + y * w, m.begin() + (y + 1) * w, std::uint8_t(255));
    }
};

// the one set of planes, off unless a frontend began an overlay frame
inline OverlayPlanes& overlayPlanes() {
    static OverlayPlanes planes;
    return planes;
}

}  // namespace omk
