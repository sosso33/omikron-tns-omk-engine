// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/playhelpers.h"

#include "audio/mixer.h"   // `wavToDevice`'s loader
#include "formats/le.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace omk {

float shortArc(float deg) {
    while (deg > 180.0f)  deg -= 360.0f;
    while (deg <= -180.0f) deg += 360.0f;
    return deg;
}

// The same sound kept in its file's form (`DeviceSound`, audio/hostmix.h):
// its 16-bit samples at their own rate, and the index rule of
// `wavToDevice` below, so `at(i)` is that function's float i and `size` its
// length. The sound's file stays the source of truth; the floats are made
// as the mixer reads them.
std::shared_ptr<const omk::DeviceSound> wavToDeviceSound(std::span<const std::byte> file,
                                                         int deviceRate) {
    const omk::audio::WavLoad w = omk::audio::loadWav(file);
    if (w.reject != omk::audio::WavReject::Ok || w.fmt.bits != 16 || !w.fmt.rate) return nullptr;
    auto d = std::make_shared<omk::DeviceSound>();
    d->channels = w.fmt.channels ? w.fmt.channels : 1;
    d->pcm.resize(w.dataBytes / 2);
    for (std::size_t i = 0; i < d->pcm.size(); ++i)        // a .wav is little-endian
        d->pcm[i] = loadLE<std::int16_t>(file.data() + w.dataOffset + 2 * i);
    const std::size_t frames = w.dataBytes / (2u * d->channels);
    if (deviceRate % static_cast<int>(w.fmt.rate) == 0) {
        const int k = deviceRate / static_cast<int>(w.fmt.rate);
        if (k == 1 || k == 2 || k == 4 || k == 8) {
            d->exact = true;
            d->shift = k == 1 ? 0u : k == 2 ? 1u : k == 4 ? 2u : 3u;
            d->size = frames * static_cast<std::size_t>(k) * 2;
            return d;
        }
    }
    // `wavToDevice`'s other arm: `out` frames, cut where the nearest source
    // frame runs off the end
    d->step = static_cast<double>(w.fmt.rate) / deviceRate;
    std::size_t out = static_cast<std::size_t>(frames / d->step);
    while (out > 0 && static_cast<std::size_t>((out - 1) * d->step) >= frames) --out;
    d->size = out * 2;
    return d;
}

std::vector<float> wavToDevice(std::span<const std::byte> file, int deviceRate) {
    const omk::audio::WavLoad w = omk::audio::loadWav(file);
    if (w.reject != omk::audio::WavReject::Ok || w.fmt.bits != 16 || !w.fmt.rate) return {};
    const auto* pcm = reinterpret_cast<const std::int16_t*>(file.data() + w.dataOffset);
    // a .wav is little-endian PCM; on a big-endian host read it into a copy
    std::vector<std::int16_t> native;
    if constexpr (std::endian::native == std::endian::big) {
        native.resize(w.dataBytes / 2);
        for (std::size_t i = 0; i < native.size(); ++i)
            native[i] = loadLE<std::int16_t>(file.data() + w.dataOffset + 2 * i);
        pcm = native.data();
    }
    const std::size_t frames = w.dataBytes / (2u * (w.fmt.channels ? w.fmt.channels : 1));
    // AN EXACT MULTIPLE (2026-09-27): a device rate 1, 2, 4 or 8 times the
    // sound's makes `i * step` exact in double, so the loop below takes source
    // frame `i / k` - written out here without its double multiply and
    // push_back per sample, the same floats (the voices' resample got the same
    // treatment; on a console each play converted its sound again)
    if (w.fmt.rate > 0 && deviceRate % static_cast<int>(w.fmt.rate) == 0) {
        const int k = deviceRate / static_cast<int>(w.fmt.rate);
        if (k == 1 || k == 2 || k == 4 || k == 8) {
            const std::size_t ch = w.fmt.channels ? w.fmt.channels : 1;
            std::vector<float> o(frames * static_cast<std::size_t>(k) * 2);
            float* d = o.data();
            for (std::size_t f = 0; f < frames; ++f) {
                const std::int16_t* p = pcm + f * ch;
                const float l = p[0] / 32768.0f;
                const float r = ch > 1 ? p[1] / 32768.0f : l;
                for (int j = 0; j < k; ++j) { d[0] = l; d[1] = r; d += 2; }
            }
            return o;
        }
    }
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

Geometry relight(const Geometry& src, Light mode) {
    if (mode == Light::Colour) return src;
    Geometry g = src;
    for (auto& c : g.corners) {
        if (mode == Light::Off) { c.r = c.g = c.b = 1.0f; continue; }
        const float y = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
        c.r = c.g = c.b = y;
    }
    return g;
}


bool envSet(const char* name) {
    struct Seen { const char* name; bool set; };
    static Seen seen[32];
    static int n = 0;
    for (int i = 0; i < n; ++i)                       // the literal's own pointer
        if (seen[i].name == name) return seen[i].set;
    for (int i = 0; i < n; ++i)                       // ...or the same text
        if (std::strcmp(seen[i].name, name) == 0) return seen[i].set;
    const bool set = std::getenv(name) != nullptr;
    if (n < static_cast<int>(sizeof seen / sizeof seen[0])) seen[n++] = Seen{name, set};
    return set;
}

// Raw int16 PCM to the device's interleaved float, the same nearest-neighbour
// step `wavToDevice` uses and for the same reason (B5: a resampler's sound is
// the driver's and has no reachable tier). This one exists because a dialogue
// line arrives already DECODED - out of a `.3DM`'s ADPCM block - rather than
// as a `.wav` file, so there is no header to read.
std::vector<float> resampleToDevice(const std::vector<std::int16_t>& pcm,
                                    int channels, int rate, int deviceRate) {
    if (pcm.empty() || channels <= 0 || rate <= 0) return {};
    const std::size_t frames = pcm.size() / static_cast<std::size_t>(channels);
    // THE VOICES' OWN RATIO, exactly twice (22050 -> 44100): the loop below
    // then takes source frame `i * 0.5` = `i / 2`, so every frame twice, and
    // this writes the same floats without its double multiply and push_back
    // per sample - a 27 s line is 2.4 million of them, which a console's A9
    // spends a visible part of a line's start on (2026-09-23)
    // ...and THE SAME RATE, the device's since it runs at the primary's
    // 22050: a conversion to float and nothing else
    if (deviceRate == rate && (channels == 1 || channels == 2)) {
        std::vector<float> o(frames * 2);
        float* w = o.data();
        const std::int16_t* p = pcm.data();
        // (times 1/32768, not divided by it: a power of two, so the same
        // float, without a divide a sample on an A9)
        constexpr float k = 1.0f / 32768.0f;
        for (std::size_t f = 0; f < frames; ++f, p += channels) {
            w[0] = p[0] * k;
            w[1] = channels > 1 ? p[1] * k : w[0];
            w += 2;
        }
        return o;
    }
    if (deviceRate == 2 * rate && (channels == 1 || channels == 2)) {
        std::vector<float> o(frames * 4);
        float* w = o.data();
        const std::int16_t* p = pcm.data();
        for (std::size_t f = 0; f < frames; ++f, p += channels) {
            const float l = p[0] / 32768.0f;
            const float r = channels > 1 ? p[1] / 32768.0f : l;
            w[0] = l; w[1] = r; w[2] = l; w[3] = r;
            w += 4;
        }
        return o;
    }
    const double step = static_cast<double>(rate) / deviceRate;
    std::vector<float> o;
    o.reserve(static_cast<std::size_t>(frames / step) * 2);
    for (std::size_t i = 0;; ++i) {
        const std::size_t src = static_cast<std::size_t>(i * step);
        if (src >= frames) break;
        const std::int16_t l = pcm[src * static_cast<std::size_t>(channels)];
        const std::int16_t r = channels > 1
            ? pcm[src * static_cast<std::size_t>(channels) + 1] : l;
        o.push_back(l / 32768.0f);
        o.push_back(r / 32768.0f);
    }
    return o;
}

}  // namespace omk
