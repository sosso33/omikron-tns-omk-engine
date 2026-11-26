// SPDX-License-Identifier: GPL-3.0-or-later
// The font pack - see fontpack.h. Not a port of anything in the original.
// (the remaster's: compiled empty without OMK_REMASTER - `engine/Makefile`)
#if OMK_REMASTER

#include "ui/fontpack.h"
#include "ui/text.h"

#include "platform/datafs.h"
#include "platform/events.h"
#include "platform/hostfs.h"
#include "platform/packkey.h"
#include "platform/pngread.h"
#include "platform/sync.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

namespace omk {

std::uint64_t fontKey(const Font& f) {
    Fnv1a h;
    for (const std::byte b : f.blob) h.mix(static_cast<std::uint8_t>(b));
    return h.h;
}

HdLayout hdLayout(const Font& f, int k) {
    HdLayout l;
    l.k = k;
    l.w = kFontAtlasW;
    int x = 0, y = 0, shelf = 0;
    for (std::size_t c = 0; c < kFntGlyphs; ++c) {
        const Glyph& g = f.glyphs[c];
        if (!g.present || g.width <= 0 || g.height <= 0) continue;
        const int w = g.width * k, h = g.height * k;
        if (x > 0 && x + w > kFontAtlasW) { x = 0; y += shelf + kFontGap; shelf = 0; }
        l.x[c] = x; l.y[c] = y;
        x += w + kFontGap;
        shelf = std::max(shelf, h);
    }
    l.h = y + shelf;
    return l;
}

struct FontPack::Impl {
    std::string dir;
    std::set<std::uint64_t> keys;
    std::map<const Font*, std::shared_ptr<const HdFont>> byFont;   // null: looked, none
    Stats st;
    Mutex m;
};

bool FontPack::open(const std::string& dir) {
    active_ = false;
    impl_ = std::make_shared<Impl>();
    impl_->dir = dir;
    if (!hostfs::isDirectory(dir)) return false;
    for (const auto& name : hostfs::listNames(dir)) {
        if (name.size() != 20 || name.substr(16) != ".png") continue;
        char* end = nullptr;
        // the hex KEPT while `end` points into it: a temporary's buffer is
        // gone at the semicolon (AddressSanitizer, 2026-11-26)
        const std::string hex = name.substr(0, 16);
        const unsigned long long k = std::strtoull(hex.c_str(), &end, 16);
        if (end && *end == '\0') impl_->keys.insert(k);
    }
    active_ = !impl_->keys.empty();
    return active_;
}

std::size_t FontPack::files() const { return impl_ ? impl_->keys.size() : 0; }

FontPack::Stats FontPack::stats() const {
    if (!impl_) return {};
    ScopedLock l(impl_->m);
    return impl_->st;
}

std::shared_ptr<const HdFont> FontPack::find(const Font& f) {
    if (!active_ || !shown_ || !f.valid) return nullptr;
    ScopedLock l(impl_->m);
    const auto it = impl_->byFont.find(&f);
    if (it != impl_->byFont.end()) return it->second;
    std::shared_ptr<const HdFont> out;
    const std::uint64_t key = fontKey(f);
    if (impl_->keys.count(key)) {
        const auto bytes = DataFs::readPath(impl_->dir + "/" + keyHex(key) + ".png");
        PngImage img;
        if (!bytes.empty() && decodePng(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), img)) {
            for (int k = 2; k <= 16 && !out; k *= 2) {
                auto lay = hdLayout(f, k);
                if (lay.w != img.w || lay.h != img.h) continue;
                auto hd = std::make_shared<HdFont>();
                hd->lay = lay;
                hd->cov.resize(static_cast<std::size_t>(img.w) * static_cast<std::size_t>(img.h));
                for (std::size_t i = 0; i < hd->cov.size(); ++i) hd->cov[i] = img.rgba[4 * i];
                out = std::move(hd);
            }
        }
        if (out) {
            ++impl_->st.decoded;
            event("fontpack.face", {{"key", keyHex(key)}, {"k", std::to_string(out->lay.k)}});
        } else {
            ++impl_->st.refused;   // unreadable, or no `k` lays the face out at its size
        }
    }
    impl_->byFont.emplace(&f, out);
    return out;
}

FontPack& fontPack() {
    static FontPack p;
    return p;
}

}  // namespace omk

namespace omk {

// THE FONT PACK's draw, `TextLayout`'s - kept here, beside the pack it reads,
// so a build without OMK_REMASTER carries neither (`ui/text.cpp` calls it)
// THE FONT PACK's glyph (`ui/fontpack.h`): each destination pixel the AREA
// AVERAGE of the HD coverage under it - the box the pixel covers at the glyph
// scale, edge texels weighted by how much of them it takes - so a coverage
// `k` times finer is brought down rather than up, at any scale. Blended as
// `drawGlyphScaled` blends.
void TextLayout::drawGlyphHd(Surface& dst, int pen, int top, const Glyph& gl, const HdFont& hd,
                             unsigned char code, const std::uint8_t rgb[3], int clipTop, int clipBottom) const {
    const int W = glyphPx(gl.width), H = glyphPx(gl.height);
    const int gw = gl.width * hd.lay.k, gh = gl.height * hd.lay.k;
    const float step = static_cast<float>(hd.lay.k) * static_cast<float>(den_) / static_cast<float>(num_);
    // the texels [a, b) covers along one axis, as (first, last, weights of the two ends)
    const auto span = [](float a, float b, int n, int& i0, int& i1, float& w0, float& w1) {
        a = std::clamp(a, 0.0f, static_cast<float>(n));
        b = std::clamp(b, 0.0f, static_cast<float>(n));
        i0 = static_cast<int>(a);
        i1 = std::min(static_cast<int>(std::ceil(b)) - 1, n - 1);
        w0 = static_cast<float>(i0 + 1) - a;
        w1 = b - static_cast<float>(i1);
        if (i0 == i1) w0 = w1 = b - a;
    };
    for (int dy = 0; dy < H; ++dy) {
        const int py = top + dy;
        if (py < 0 || py >= dst.h || py < clipTop || py >= clipBottom) continue;
        int y0, y1; float wy0, wy1;
        span(dy * step, (dy + 1) * step, gh, y0, y1, wy0, wy1);
        if (y1 < y0) continue;
        for (int dx = 0; dx < W; ++dx) {
            const int px = pen + dx;
            if (px < 0 || px >= dst.w) continue;
            int x0, x1; float wx0, wx1;
            span(dx * step, (dx + 1) * step, gw, x0, x1, wx0, wx1);
            if (x1 < x0) continue;
            float sum = 0.0f;
            for (int y = y0; y <= y1; ++y) {
                const float wy = y == y0 ? wy0 : (y == y1 ? wy1 : 1.0f);
                float row = 0.0f;
                for (int x = x0; x <= x1; ++x)
                    row += hd.at(code, x, y) * (x == x0 ? wx0 : (x == x1 ? wx1 : 1.0f));
                sum += row * wy;
            }
            const int c = static_cast<int>(sum / (step * step) * (31.0f / 255.0f) + 0.5f);
            if (c > 0) blendCoverage(dst, px, py, std::min(c, 31), rgb);
        }
    }
}

}  // namespace omk

#endif  // OMK_REMASTER
