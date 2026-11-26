// SPDX-License-Identifier: GPL-3.0-or-later
// (the remaster's: compiled empty without OMK_REMASTER - `engine/Makefile`)
#if OMK_REMASTER

#include "o3de/emitterlight.h"

#include "platform/datafs.h"   // upperAscii

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace omk {

namespace {

// The hue of a sprite's BRIGHT part: its texels' mean weighted by their own
// brightness (an additive sprite is mostly black around its glow). White when
// there is no texture or no lit texel.
void spriteColour(const Texture* t, float rgb[3]) {
    rgb[0] = rgb[1] = rgb[2] = 1.0f;
    if (!t || !t->hasPixels()) return;
    double w = 0.0, r = 0.0, g = 0.0, b = 0.0;
    const std::size_t n = static_cast<std::size_t>(t->width) * static_cast<std::size_t>(t->height);
    for (std::size_t k = 0; k < n; ++k) {
        int cr, cg, cb;
        t->texel(k, cr, cg, cb);
        const double l = (0.299 * cr + 0.587 * cg + 0.114 * cb) / 255.0;
        w += l; r += l * cr; g += l * cg; b += l * cb;
    }
    if (w <= 0.0) return;
    rgb[0] = static_cast<float>(r / w / 255.0);
    rgb[1] = static_cast<float>(g / w / 255.0);
    rgb[2] = static_cast<float>(b / w / 255.0);
}

}  // namespace

std::vector<Light3do> emitterLights(const ParticleField& f,
                                    const std::function<const Texture*(int)>& texOf, float s,
                                    EmitterLightCount* count) {
    std::vector<Light3do> out;
    if (s <= 0.0f) return out;
    for (const Emitter& e : f.emitters()) {
        const FxEffect& fx = e.fx;
        const bool additive = fx.mode == 4 || fx.mode == 5;
        float c[3];
        spriteColour(texOf ? texOf(static_cast<int>(fx.sprite)) : nullptr, c);
        // a FLAME by its SPRITE's saturation: Anekbah's burner (`agaz`) ramps
        // a dim blue-grey that cancels its texture's orange in the product
        // (0.31, against the steam's 0.22), while the textures part cleanly -
        // the flame's 0.55, the smoke's 0.22
        const float spr[3] = {c[0], c[1], c[2]};
        const float th = std::max({c[0], c[1], c[2]}), tl = std::min({c[0], c[1], c[2]});
        const float spriteSat = th > 0.0f ? (th - tl) / th : 0.0f;
        // ...times the effect's colour, the mean of its birth and death ends
        const std::uint32_t c0 = fx.colour0, c1 = fx.colour1;
        c[0] *= (static_cast<float>((c0 >> 16) & 255) + static_cast<float>((c1 >> 16) & 255)) / 510.0f;
        c[1] *= (static_cast<float>((c0 >> 8) & 255) + static_cast<float>((c1 >> 8) & 255)) / 510.0f;
        c[2] *= (static_cast<float>(c0 & 255) + static_cast<float>(c1 & 255)) / 510.0f;
        const float hi = std::max({c[0], c[1], c[2]});
        // (a glow whose ramp is BLACK lights nothing: its hue would be 0/0 and
        // it would take a light slot - dark, as a flame's `hi > 0` makes it;
        // the audit's item 6, 2026-10-23)
        const bool glow = additive && fx.life <= 2.0f && hi > 0.0f;
        const bool flame = additive && !glow && hi > 0.0f && spriteSat >= kFlameSaturation;
        if (!glow && !flame) {
            if (count) ++count->dark;
            continue;
        }
        if (count) ++(glow ? count->glows : count->flames);
        Light3do l;
        l.name = "FXLIGHT";
        for (int k = 0; k < 3; ++k) l.pos[k] = e.pos[k];
        // the colour at full brightness, the brightness in the intensity - a
        // FLAME's hue its sprite's (seen, the burner is orange; its ramp's
        // blue-grey only dims it), a glow's the product
        const float* hue = flame ? spr : c;
        const float hh = flame ? th : hi;
        const auto byte = [&](float v) {
            return static_cast<std::uint32_t>(std::lround(std::clamp(v / hh, 0.0f, 1.0f) * 255.0f));
        };
        l.colour = (byte(hue[0]) << 16) | (byte(hue[1]) << 8) | byte(hue[2]);
        l.radiusA = glow ? kGlowReach : kFlameReach;
        l.radiusB = -l.radiusA * kInner;   // negative: a POINT light
        // a stream's particles OVERLAP - about `count x life` alive at once,
        // all added - so its light is brighter than one particle's colour
        const float overlap = glow ? 1.0f
            : std::clamp(std::sqrt(static_cast<float>(std::max<int>(1, fx.count)) * fx.life), 1.0f, kMaxOverlap);
        l.f32 = s * hi * overlap * (glow ? kGlowIntensity : kFlameIntensity);
        out.push_back(l);
    }
    return out;
}

bool SurfaceCuration::open(const std::string& path) {
    entries_.clear();
    sets_.clear();
    byTexture_.clear();
    count_ = 0;
    opened_ = false;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char line[512];
    while (std::fgets(line, sizeof line, f)) {
        std::string ln(line);
        if (const auto h = ln.find('#'); h != std::string::npos) ln.resize(h);
        const auto eq = ln.find('=');
        if (eq == std::string::npos) continue;
        char set[128] = {}, tx[128] = {}, rect[128] = {}, col[16] = {};
        const int k = std::sscanf(ln.substr(0, eq).c_str(), "%127s %127s %127s", set, tx, rect);
        if (k < 2) continue;
        Entry e;
        if (k == 3 && std::sscanf(rect, "%f,%f,%f,%f", &e.u0, &e.v0, &e.u1, &e.v1) == 4) e.hasRect = true;
        const int n = std::sscanf(ln.substr(eq + 1).c_str(), "%f %15s", &e.gain, col);
        if (n < 1) continue;
        if (n == 2) { e.colour = static_cast<std::uint32_t>(std::strtoul(col, nullptr, 16)) & 0xFFFFFFu; e.hasColour = true; }
        ++sets_[upperAscii(set)];
        {
            auto& bt = byTexture_[upperAscii(tx)];
            if (e.hasRect) bt.insert(std::find_if(bt.begin(), bt.end(), [](const Entry& x) { return !x.hasRect; }), e);
            else bt.push_back(e);
        }
        auto& v = entries_[upperAscii(set) + " " + upperAscii(tx)];
        // rectangles first, in file order; the texture-wide entry last
        if (e.hasRect) v.insert(std::find_if(v.begin(), v.end(), [](const Entry& x) { return !x.hasRect; }), e);
        else v.push_back(e);
        ++count_;
    }
    std::fclose(f);
    opened_ = true;
    return true;
}

bool SurfaceCuration::covers(const std::string& set) const {
    return opened_ && (sets_.count(upperAscii(set)) || sets_.count("*"));
}

const SurfaceCuration::Entry* SurfaceCuration::Lists::at(float u, float v) const {
    for (const std::vector<Entry>* l : {first, second})
        if (l)
            for (const Entry& e : *l)
                if (!e.hasRect || (u >= e.u0 && u < e.u1 && v >= e.v0 && v < e.v1)) return &e;
    return nullptr;
}

SurfaceCuration::Lists SurfaceCuration::listsAny(const std::string& texture) const {
    const auto it = byTexture_.find(upperAscii(texture));
    return {it != byTexture_.end() ? &it->second : nullptr, nullptr};
}

SurfaceCuration::Lists SurfaceCuration::listsFor(const std::string& set, const std::string& texture) const {
    const std::string t = upperAscii(texture);
    const auto a = entries_.find(upperAscii(set) + " " + t);
    const auto b = entries_.find("* " + t);
    return {a != entries_.end() ? &a->second : nullptr, b != entries_.end() ? &b->second : nullptr};
}

const SurfaceCuration::Entry* SurfaceCuration::findAny(const std::string& texture, float u, float v) const {
    const auto it = byTexture_.find(upperAscii(texture));
    if (it == byTexture_.end()) return nullptr;
    for (const Entry& e : it->second)
        if (!e.hasRect || (u >= e.u0 && u < e.u1 && v >= e.v0 && v < e.v1)) return &e;
    return nullptr;
}

bool SurfaceCuration::namesAny(const std::string& texture) const {
    return byTexture_.count(upperAscii(texture)) > 0;
}

void curateBloom(const Texture& t, PackImage& pk) {
    const SurfaceCuration& cur = surfaceCuration();
    if (!cur.active() || !t.hasPixels() || !cur.namesAny(t.name)) return;
    const SurfaceCuration::Lists lists = cur.listsAny(t.name);
    if (pk.emissive.empty()) {
        pk.ew = t.width; pk.eh = t.height;
        pk.emissive.assign(static_cast<std::size_t>(pk.ew) * pk.eh, 0);
    }
    for (int y = 0; y < pk.eh; ++y)
        for (int x = 0; x < pk.ew; ++x) {
            // the texel in the ORIGINAL's units, and its colour (the
            // replacement's where there is one)
            const float u = (static_cast<float>(x) + 0.5f) * static_cast<float>(t.width) / static_cast<float>(pk.ew);
            const float v = (static_cast<float>(y) + 0.5f) * static_cast<float>(t.height) / static_cast<float>(pk.eh);
            const SurfaceCuration::Entry* e = lists.at(u, v);
            if (!e) continue;
            float rgb[3];
            if (pk.ok && pk.w > 0 && pk.h > 0 && !pk.rgba.empty()) {   // not a block replacement
                const int cx = std::min(pk.w - 1, static_cast<int>(u * static_cast<float>(pk.w) / static_cast<float>(t.width)));
                const int cy = std::min(pk.h - 1, static_cast<int>(v * static_cast<float>(pk.h) / static_cast<float>(t.height)));
                const std::uint8_t* p = pk.rgba.data() + (static_cast<std::size_t>(cy) * pk.w + cx) * 4;
                for (int k = 0; k < 3; ++k) rgb[k] = static_cast<float>(p[k]) / 255.0f;
            } else {
                int r, g, b;
                t.texel(static_cast<std::size_t>(std::min(t.height - 1, static_cast<int>(v))) * t.width +
                            static_cast<std::size_t>(std::min(t.width - 1, static_cast<int>(u))), r, g, b);
                rgb[0] = static_cast<float>(r) / 255.0f; rgb[1] = static_cast<float>(g) / 255.0f; rgb[2] = static_cast<float>(b) / 255.0f;
            }
            // a curated rectangle is a LIGHT by the list's say: its texels
            // glow by their brightness alone (the neon rule's 0.65 value
            // missed the pink letters' 0.77-0.86)
            const float hi = std::max({rgb[0], rgb[1], rgb[2]});
            const float lit = std::clamp((hi - 0.3f) / 0.4f, 0.0f, 1.0f);
            const float add = lit * kCuratedBloom * std::min(1.0f, e->gain / 2.0f) * 255.0f;
            std::uint8_t& o = pk.emissive[static_cast<std::size_t>(y) * pk.ew + x];
            o = static_cast<std::uint8_t>(std::max<float>(o, std::min(255.0f, add)));
        }
}

bool bloomCurated(const Texture& t) {
    const SurfaceCuration& cur = surfaceCuration();
    return cur.active() && t.hasPixels() && cur.namesAny(t.name);
}

SurfaceCuration& surfaceCuration() {
    static SurfaceCuration c;
    return c;
}

bool SurfaceCuration::names(const std::string& set, const std::string& texture) const {
    const std::string t = upperAscii(texture);
    return entries_.count(upperAscii(set) + " " + t) || entries_.count("* " + t);
}

const SurfaceCuration::Entry* SurfaceCuration::find(const std::string& set, const std::string& texture,
                                                    float u, float v) const {
    const std::string t = upperAscii(texture);
    const std::string keys[2] = {upperAscii(set) + " " + t, "* " + t};
    for (const std::string& key : keys) {
        const auto it = entries_.find(key);
        if (it == entries_.end()) continue;
        for (const Entry& e : it->second)
            if (!e.hasRect || (u >= e.u0 && u < e.u1 && v >= e.v0 && v < e.v1)) return &e;
    }
    return nullptr;
}

std::vector<Light3do> surfaceLights(const Geometry& g, std::span<const Texture> tex,
                                    std::span<const PackImage> packs, float s, int* cells,
                                    const std::string& set, const SurfaceCuration* cur,
                                    std::vector<SurfaceStat>* stats) {
    std::vector<Light3do> out;
    if (cells) *cells = 0;
    if (s <= 0.0f) return out;
    const bool curated = cur && cur->covers(set);
    if (stats) { stats->clear(); stats->resize(tex.size()); for (std::size_t i = 0; i < tex.size(); ++i) (*stats)[i].texture = tex[i].name; }
    // n: the neon rule's share; gn: the curated GAIN, by light
    struct Acc { double w = 0, x = 0, y = 0, z = 0, r = 0, gg = 0, b = 0, n = 0, gn = 0; };
    std::unordered_map<std::uint64_t, Acc> acc;
    const auto cellKey = [](float x, float y, float z) {
        const auto u = [](float v) {
            return static_cast<std::uint64_t>(static_cast<std::uint32_t>(
                static_cast<int>(std::floor(v / kSurfaceCell)) + (1 << 20)) & 0x1FFFFFu);
        };
        return u(x) | (u(y) << 21) | (u(z) << 42);
    };
    for (const auto& bt : g.batches) {
        if (bt.blend == Blend::Mul) continue;   // a shadow, not light
        const std::size_t m = static_cast<std::size_t>(bt.material);
        if (m >= tex.size() || m >= packs.size()) continue;
        const PackImage& pk = packs[m];
        const Texture& t = tex[m];
        if (t.width <= 0 || t.height <= 0) continue;
        const bool lightMap = !pk.emissive.empty() && pk.ew > 0 && pk.eh > 0;   // else the neon rule alone
        // THE LIST, where given: a texture it does not name casts nothing
        const bool named = curated && cur->names(set, t.name);
        const SurfaceCuration::Lists lists = named ? cur->listsFor(set, t.name) : SurfaceCuration::Lists{};
        if (curated && !named && !stats) continue;
        // a texel's light and colour at a UV in the ORIGINAL's texels, wrapped
        const auto at = [&](float u, float v, float rgb[3], float& neonPart) {
            // only STRONG light counts - the light maps mark every bright
            // texel the bloom may lift, painted lamps and warm walls among
            // them; a light source is past kSurfaceFloor, weighted above it
            float e = 0.0f;
            if (lightMap) {
                const int ex = ((static_cast<int>(std::floor(u * static_cast<float>(pk.ew) / static_cast<float>(t.width))) % pk.ew) + pk.ew) % pk.ew;
                const int ey = ((static_cast<int>(std::floor(v * static_cast<float>(pk.eh) / static_cast<float>(t.height))) % pk.eh) + pk.eh) % pk.eh;
                e = std::max(0.0f, (static_cast<float>(pk.emissive[static_cast<std::size_t>(ey) * pk.ew + ex]) / 255.0f
                                    - kSurfaceFloor) / (1.0f - kSurfaceFloor));
            }
            if (pk.ok && pk.w > 0 && pk.h > 0 && !pk.rgba.empty()) {   // not a block replacement
                const int cx = ((static_cast<int>(std::floor(u * static_cast<float>(pk.w) / static_cast<float>(t.width))) % pk.w) + pk.w) % pk.w;
                const int cy = ((static_cast<int>(std::floor(v * static_cast<float>(pk.h) / static_cast<float>(t.height))) % pk.h) + pk.h) % pk.h;
                const std::uint8_t* p = pk.rgba.data() + (static_cast<std::size_t>(cy) * pk.w + cx) * 4;
                for (int k = 0; k < 3; ++k) rgb[k] = static_cast<float>(p[k]) / 255.0f;
            } else if (t.hasPixels()) {
                const int tx = ((static_cast<int>(std::floor(u)) % t.width) + t.width) % t.width;
                const int ty = ((static_cast<int>(std::floor(v)) % t.height) + t.height) % t.height;
                int r, gg, b;
                t.texel(static_cast<std::size_t>(ty) * t.width + tx, r, gg, b);
                rgb[0] = static_cast<float>(r) / 255.0f; rgb[1] = static_cast<float>(gg) / 255.0f; rgb[2] = static_cast<float>(b) / 255.0f;
            } else {
                rgb[0] = rgb[1] = rgb[2] = 1.0f;
            }
            // ...or a NEON's: a texel both bright and strongly saturated - a
            // tube's colour - lights whatever its light map says (Qalisar's
            // pink letters carry little of one: the bloom's white was the
            // halo sprite's) - ON A TEXTURE THE LIST NAMES ONLY (and for the
            // list's census, `stats`): the rule cannot tell a tube from a
            // bright painted panel, and where no list stands guard it lit
            // AIMPASSE's orange WOOD PLANKS (XAMPAK2) as a neon at up to
            // 1 + kNeonBoost - the first street of a new game evenly lit,
            // twice its level (the reader's Switch run, 2026-11-26)
            float neon = 0.0f;
            if (named || stats) {
                const float hi = std::max({rgb[0], rgb[1], rgb[2]}), lo = std::min({rgb[0], rgb[1], rgb[2]});
                const float sat = hi > 0.0f ? (hi - lo) / hi : 0.0f;
                neon = std::clamp((sat - kNeonSat) / 0.3f, 0.0f, 1.0f) *
                       std::clamp((hi - kNeonValue) / 0.25f, 0.0f, 1.0f);
            }
            neonPart = neon > e ? 1.0f : 0.0f;   // ...which of the two lit it
            return std::max(e, neon);
        };
        for (std::size_t i = bt.start; i + 2 < bt.start + bt.count && i + 2 < g.corners.size(); i += 3) {
            const Corner& a = g.corners[i];
            const Corner& b = g.corners[i + 1];
            const Corner& c = g.corners[i + 2];
            const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
            const double vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
            const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
            const double area = 0.5 * std::sqrt(nx * nx + ny * ny + nz * nz);
            if (area <= 0.0) continue;
            // a LATTICE over the triangle in texture space, as dense as its
            // texels allow (2..16 a side): a sign's letters are thin bright
            // texels inside a keyed-out quad, and its corners sample the key
            const double tu1 = b.u - a.u, tv1 = b.v - a.v, tu2 = c.u - a.u, tv2 = c.v - a.v;
            const double texels = std::fabs(tu1 * tv2 - tv1 * tu2) * 0.5;
            const int n = std::clamp(static_cast<int>(std::sqrt(texels) / 2.0), 2, 16);
            float e = 0.0f, col[3] = {0, 0, 0}, hueW = 0.0f, eNeon = 0.0f;
            // ...and the same through the LIST: the matched texels' light, its
            // gain, its colour
            float ec = 0.0f, ecGain = 0.0f, colC[3] = {0, 0, 0}, hueC = 0.0f;
            int samples = 0;
            for (int li = 0; li <= n; ++li)
                for (int lj = 0; li + lj <= n; ++lj) {
                    const float fu = static_cast<float>(li) / static_cast<float>(n);
                    const float fv = static_cast<float>(lj) / static_cast<float>(n);
                    float rgb[3], np = 0.0f;
                    const float su = a.u + fu * (b.u - a.u) + fv * (c.u - a.u);
                    const float sv = a.v + fu * (b.v - a.v) + fv * (c.v - a.v);
                    const float ek = at(su, sv, rgb, np);
                    e += ek;
                    eNeon += ek * np;
                    // the COLOUR from the tube's texels: weighted by their
                    // own saturation, so a white core or a grey texel adds
                    // light but not hue (the reader, 2026-10-20: "compute the
                    // light colour from the neon texture colours")
                    const float th = std::max({rgb[0], rgb[1], rgb[2]}), tl = std::min({rgb[0], rgb[1], rgb[2]});
                    const float ts = th > 0.0f ? (th - tl) / th : 0.0f;
                    const float cw = ek * (0.15f + ts);
                    for (int q = 0; q < 3; ++q) col[q] += cw * (th > 0.0f ? rgb[q] / th : 1.0f);
                    hueW += cw;
                    if (named && ek > 0.0f) {
                        const float wu = std::fmod(std::fmod(su, static_cast<float>(t.width)) + static_cast<float>(t.width), static_cast<float>(t.width));
                        const float wv = std::fmod(std::fmod(sv, static_cast<float>(t.height)) + static_cast<float>(t.height), static_cast<float>(t.height));
                        if (const SurfaceCuration::Entry* ce = lists.at(wu, wv)) {
                            ec += ek;
                            ecGain += ek * ce->gain;
                            if (ce->hasColour) {
                                colC[0] += ek * static_cast<float>((ce->colour >> 16) & 255) / 255.0f;
                                colC[1] += ek * static_cast<float>((ce->colour >> 8) & 255) / 255.0f;
                                colC[2] += ek * static_cast<float>(ce->colour & 255) / 255.0f;
                                hueC += ek;
                            } else {
                                for (int q = 0; q < 3; ++q) colC[q] += cw * (th > 0.0f ? rgb[q] / th : 1.0f);
                                hueC += cw;
                            }
                        }
                    }
                    ++samples;
                }
            if (e <= 0.0f) continue;
            const double w = area * e / samples;
            const float cxw = (a.x + b.x + c.x) / 3.0f, cyw = (a.y + b.y + c.y) / 3.0f, czw = (a.z + b.z + c.z) / 3.0f;
            if (stats) {
                SurfaceStat& st = (*stats)[m];
                st.lit += w; st.neon += w * eNeon / e;
                if (hueW > 0.0f) { st.r += w * col[0] / hueW; st.g += w * col[1] / hueW; st.b += w * col[2] / hueW; }
            }
            if (curated) {
                // the LIST's light alone: its texels, its gains, its colours
                if (ec <= 0.0f) continue;
                const double wc = area * ec / samples;
                Acc& q = acc[cellKey(cxw, cyw, czw)];
                q.w += wc; q.x += wc * cxw; q.y += wc * cyw; q.z += wc * czw;
                q.gn += wc * (ecGain / ec);
                if (hueC > 0.0f) { q.r += wc * colC[0] / hueC; q.gg += wc * colC[1] / hueC; q.b += wc * colC[2] / hueC; }
                continue;
            }
            Acc& q = acc[cellKey(cxw, cyw, czw)];
            q.w += w; q.x += w * cxw; q.y += w * cyw; q.z += w * czw;
            q.n += w * eNeon / e;
            // the hue, each triangle's by its light (`w`): the lit texels'
            // colours at full value, weighted by saturation
            if (hueW > 0.0f) { q.r += w * col[0] / hueW; q.gg += w * col[1] / hueW; q.b += w * col[2] / hueW; }
        }
    }
    for (const auto& [k, q] : acc) {
        (void)k;
        if (q.w < kSurfaceMin) continue;
        // a neon's light reads as its COLOUR: the tube's core is near white,
        // so the lit texels' mean is pushed from its grey by kSurfaceSat
        double rr = q.r, gg = q.gg, bb = q.b;
        const double grey = 0.299 * rr + 0.587 * gg + 0.114 * bb;
        rr = std::max(0.0, grey + (rr - grey) * kSurfaceSat);
        gg = std::max(0.0, grey + (gg - grey) * kSurfaceSat);
        bb = std::max(0.0, grey + (bb - grey) * kSurfaceSat);
        const double hi = std::max({rr, gg, bb});
        if (hi <= 0.0) continue;
        Light3do l;
        l.name = "FXSURFACE";
        l.pos[0] = static_cast<float>(q.x / q.w); l.pos[1] = static_cast<float>(q.y / q.w); l.pos[2] = static_cast<float>(q.z / q.w);
        const auto byte = [&](double v) { return static_cast<std::uint32_t>(std::lround(std::clamp(v / hi, 0.0, 1.0) * 255.0)); };
        l.colour = (byte(rr) << 16) | (byte(gg) << 8) | byte(bb);
        l.radiusA = kSurfaceReach;
        l.radiusB = -kSurfaceReach * kInner;
        // as strong as its light's AREA only in part (a sign's letters are
        // thin), and a NEON - lit by the neon rule, not a light map's warm
        // panel - up to 1 + kNeonBoost times: the neons are the night's light
        // (with the LIST, its gain in place of the boost)
        const float neonShare = static_cast<float>(q.n / q.w);
        const float boost = curated ? static_cast<float>(q.gn / q.w) : 1.0f + kNeonBoost * neonShare;
        l.f32 = s * kSurfaceIntensity * (0.5f + 0.5f * std::min(1.0f, static_cast<float>(std::sqrt(q.w)) / kSurfaceArea)) * boost;
        out.push_back(l);
    }
    if (cells) *cells = static_cast<int>(out.size());
    return out;
}

}  // namespace omk

#endif  // OMK_REMASTER
