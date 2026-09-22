// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/playhelpers.h"

#include "audio/mixer.h"   // `wavToDevice`'s loader

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace omk {

float shortArc(float deg) {
    while (deg > 180.0f)  deg -= 360.0f;
    while (deg <= -180.0f) deg += 360.0f;
    return deg;
}

std::vector<float> wavToDevice(std::span<const std::byte> file, int deviceRate) {
    const omk::audio::WavLoad w = omk::audio::loadWav(file);
    if (w.reject != omk::audio::WavReject::Ok || w.fmt.bits != 16 || !w.fmt.rate) return {};
    const auto* pcm = reinterpret_cast<const std::int16_t*>(file.data() + w.dataOffset);
    const std::size_t frames = w.dataBytes / (2u * (w.fmt.channels ? w.fmt.channels : 1));
    const double step = static_cast<double>(w.fmt.rate) / deviceRate;
    const std::size_t out = static_cast<std::size_t>(frames / step);
    std::vector<float> o;
    o.reserve(out * 2);
    for (std::size_t i = 0; i < out; ++i) {
        // Nearest-neighbour, deliberately: the alternative is a resampler,
        // and `PORTING` B5's argument applies - what a menu blip sounds like
        // through DirectSound's own resampler is the driver's, has no
        // reachable tier, and is not something to imitate precisely.
        const std::size_t src = static_cast<std::size_t>(i * step);
        if (src >= frames) break;
        const std::int16_t l = pcm[src * w.fmt.channels];
        const std::int16_t r = w.fmt.channels > 1 ? pcm[src * w.fmt.channels + 1] : l;
        o.push_back(l / 32768.0f);
        o.push_back(r / 32768.0f);
    }
    return o;
}


void drawSubtitleBox(Surface& fb, SubBox kind, int top, int dispW, int dispH) {
    if (kind == SubBox::None) return;
    // The 18, 8, 4 and 32 are LITERAL pixels in `sub_4400D0` - `v21 = 18`,
    // `(uint16_t)g_ScreenSize - 18`, `v6 - 8`, `a2 - 32` - not scaled by the
    // display, unlike the block's own `height * 64 / 480`. Scaling them put
    // the box a few rows lower than the engine does.
    // `top` is the TEXT's top, and the box is always `v6 - 8` from it: the
    // -4 (line) and -32 (replies) in `sub_4400D0` are how `v6` is derived
    // from `a2`, not a second offset on the box. Applying both put the reply
    // box 32 rows too high above its own text.
    const int x0 = 18, x1 = dispW - 18;
    const int y0 = top - 8;
    const int y1 = dispH - 18;
    (void)kind;
    if (x1 <= x0 || y1 <= y0) return;
    for (int y = y0 < 0 ? 0 : y0; y < y1 && y < fb.h; ++y) {
        for (int x = x0 < 0 ? 0 : x0; x < x1 && x < fb.w; ++x) {
            const std::size_t ovI = static_cast<std::size_t>(y) * static_cast<std::size_t>(fb.w) +
                                    static_cast<std::size_t>(x);
            const bool ovKey = overlayPlanes().on && fb.px[ovI] == kOverlayKey;
            if (ovKey) {
                overlayPlanes().row(ovI);
                overlayPlanes().m[ovI] = static_cast<std::uint8_t>(
                    overlayPlanes().m[ovI] * (kind == SubBox::Line ? 255 - 0x80 : 0x80) / 255);
            }
            std::uint16_t& px = ovKey ? overlayPlanes().c[ovI] : fb.px[ovI];
            int r = ((px >> 11) & 31) << 3, g = ((px >> 5) & 63) << 2, b = (px & 31) << 3;
            if (kind == SubBox::Line) {
                // dst *= (1 - src), src = 0x808080
                r = r * (255 - 0x80) / 255;
                g = g * (255 - 0x80) / 255;
                b = b * (255 - 0x80) / 255;
            } else {
                // src*(1-a) + dst*a, src = 0x002040, a = 0x80
                const int a = 0x80;
                r = (0x00 * (255 - a) + r * a) / 255;
                g = (0x20 * (255 - a) + g * a) / 255;
                b = (0x40 * (255 - a) + b * a) / 255;
            }
            px = static_cast<std::uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
}

std::string cp1252ToUtf8(const std::string& in) {
    static const unsigned short kHigh[32] = {   // 0x80..0x9F, the only holes
        0x20AC,0,0x201A,0x0192,0x201E,0x2026,0x2020,0x2021,0x02C6,0x2030,
        0x0160,0x2039,0x0152,0,0x017D,0,0,0x2018,0x2019,0x201C,0x201D,0x2022,
        0x2013,0x2014,0x02DC,0x2122,0x0161,0x203A,0x0153,0,0x017E,0x0178};
    std::string o;
    for (unsigned char c : in) {
        unsigned cp = c;
        if (c >= 0x80 && c <= 0x9F) { cp = kHigh[c - 0x80]; if (!cp) continue; }
        if (cp < 0x80) { o.push_back(static_cast<char>(cp)); }
        else if (cp < 0x800) {
            o.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            o.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return o;
}
}  // namespace omk
