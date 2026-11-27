// SPDX-License-Identifier: GPL-3.0-or-later
// (the remaster's: compiled empty without OMK_REMASTER - `engine/Makefile`)
#if OMK_REMASTER

#include "o3de/lightprobe.h"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace omk {

std::uint64_t LightProbes::key(int x, int y, int z) {
    const auto u = [](int v) { return static_cast<std::uint64_t>(static_cast<std::uint32_t>(v + (1 << 20)) & 0x1FFFFFu); };
    return u(x) | (u(y) << 21) | (u(z) << 42);
}

namespace {

struct P { float x, y, z, r, g, b; };

// One opaque triangle into `add` as pieces no edge of which is longer than
// `cell`: each piece's centroid, its corners' interpolated colour and its
// AREA. A set's floor is a few large triangles whose corners can lie further
// apart than the probe's reach - Lahoreh's street start had no corner within
// `kReach` (600 units, ~15 m), so a body there was never tinted (2026-10-20).
template <class F>
void pieces(const P& a, const P& b, const P& c, float cell, int depth, F&& add) {
    const auto d2 = [](const P& u, const P& v) {
        return (u.x - v.x) * (u.x - v.x) + (u.y - v.y) * (u.y - v.y) + (u.z - v.z) * (u.z - v.z);
    };
    const float e = std::max({d2(a, b), d2(b, c), d2(c, a)});
    if (e > cell * cell && depth < 12) {
        const auto mid = [](const P& u, const P& v) {
            return P{(u.x + v.x) * 0.5f, (u.y + v.y) * 0.5f, (u.z + v.z) * 0.5f,
                     (u.r + v.r) * 0.5f, (u.g + v.g) * 0.5f, (u.b + v.b) * 0.5f};
        };
        const P ab = mid(a, b), bc = mid(b, c), ca = mid(c, a);
        pieces(a, ab, ca, cell, depth + 1, add);
        pieces(ab, b, bc, cell, depth + 1, add);
        pieces(ca, bc, c, cell, depth + 1, add);
        pieces(ab, bc, ca, cell, depth + 1, add);
        return;
    }
    const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
    const double vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    const double area = 0.5 * std::sqrt(nx * nx + ny * ny + nz * nz);
    if (area <= 0.0) return;
    add(P{(a.x + b.x + c.x) / 3.0f, (a.y + b.y + c.y) / 3.0f, (a.z + b.z + c.z) / 3.0f,
          (a.r + b.r + c.r) / 3.0f, (a.g + b.g + c.g) / 3.0f, (a.b + b.b + c.b) / 3.0f}, area);
}

}  // namespace

struct LightProbes::Near {
    std::mutex m;
    std::unordered_map<std::uint64_t, std::unique_ptr<std::vector<NearCell>>> lists;
};

const std::vector<LightProbes::NearCell>& LightProbes::nearOf(int cx, int cy, int cz) const {
    Near& nr = *near_;
    const std::uint64_t k = key(cx, cy, cz);
    {
        std::lock_guard<std::mutex> l(nr.m);
        if (const auto it = nr.lists.find(k); it != nr.lists.end()) return *it->second;
    }
    // the window read once, outside the lock (the cells do not change after
    // `build`); a list made twice by two threads at once: the first kept
    const int reach = static_cast<int>(std::ceil(kReach / kCell));
    auto list = std::make_unique<std::vector<NearCell>>();
    for (int z = cz - reach; z <= cz + reach; ++z)
        for (int y = cy - reach; y <= cy + reach; ++y)
            for (int x = cx - reach; x <= cx + reach; ++x)
                if (const auto it = cells_.find(key(x, y, z)); it != cells_.end()) {
                    const Cell& e = it->second;
                    list->push_back({e.x / e.w, e.y / e.w, e.z / e.w, e.w, e.r, e.g, e.b});
                }
    std::lock_guard<std::mutex> l(nr.m);
    auto& slot = nr.lists[k];
    if (!slot) slot = std::move(list);
    return *slot;   // a list never moves once made: its own allocation
}

bool LightProbes::build(const Geometry& g) {
    near_ = std::make_shared<Near>();
    cells_.clear();
    n_ = 0;
    double luma = 0.0, area = 0.0;
    const auto add = [&](const P& p, double w) {
        Cell& e = cells_[key(static_cast<int>(std::floor(p.x / kCell)), static_cast<int>(std::floor(p.y / kCell)),
                             static_cast<int>(std::floor(p.z / kCell)))];
        e.r += w * p.r; e.g += w * p.g; e.b += w * p.b;
        e.x += w * p.x; e.y += w * p.y; e.z += w * p.z;
        e.w += w;
        luma += w * (0.299 * p.r + 0.587 * p.g + 0.114 * p.b);
        area += w;
        ++n_;
    };
    // the set's OPAQUE surface, by area: what a body stands among
    for (const auto& b : g.batches) {
        if (b.blend != Blend::Opaque) continue;
        for (std::size_t t = b.start; t + 2 < b.start + b.count && t + 2 < g.corners.size(); t += 3) {
            const auto pt = [&](std::size_t i) {
                const Corner& c = g.corners[i];
                return P{c.x, c.y, c.z, c.r, c.g, c.b};
            };
            pieces(pt(t), pt(t + 1), pt(t + 2), kCell, 0, add);
        }
    }
    if (area > 0.0) luma /= area;
    // THE MEAN a body sees, not the set's: the probe at body height over the
    // set's FLOORS (opaque faces turned up - Y points down - by their area),
    // so the average place a body stands in keeps the original's grey
    double fl = 0.0, fa = 0.0, fc[3] = {0.0, 0.0, 0.0};
    for (const auto& b : g.batches) {
        if (b.blend != Blend::Opaque) continue;
        for (std::size_t t = b.start; t + 2 < b.start + b.count && t + 2 < g.corners.size(); t += 3) {
            const Corner& p0 = g.corners[t];
            const Corner& p1 = g.corners[t + 1];
            const Corner& p2 = g.corners[t + 2];
            const double ux = p1.x - p0.x, uy = p1.y - p0.y, uz = p1.z - p0.z;
            const double vx = p2.x - p0.x, vy = p2.y - p0.y, vz = p2.z - p0.z;
            const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
            const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len <= 0.0 || std::fabs(ny) / len < 0.9) continue;   // level, either winding
            const float at[3] = {(p0.x + p1.x + p2.x) / 3.0f, (p0.y + p1.y + p2.y) / 3.0f - kBodyHeight,
                                 (p0.z + p1.z + p2.z) / 3.0f};
            float rgb[3];
            if (!sample(at, rgb)) continue;
            const double floorArea = len * 0.5;
            fl += floorArea * (0.299 * rgb[0] + 0.587 * rgb[1] + 0.114 * rgb[2]);
            for (int k = 0; k < 3; ++k) fc[k] += floorArea * rgb[k];
            fa += floorArea;
        }
    }
    meanLuma_ = fa > 0.0 ? static_cast<float>(fl / fa) : static_cast<float>(luma);
    for (int k = 0; k < 3; ++k) meanRgb_[k] = fa > 0.0 ? static_cast<float>(fc[k] / fa) : meanLuma_;
    return n_ > 0;
}

bool LightProbes::sample(const float p[3], float rgb[3]) const {
    if (!n_ || !near_) return false;
    const int cx = static_cast<int>(std::floor(p[0] / kCell));
    const int cy = static_cast<int>(std::floor(p[1] / kCell));
    const int cz = static_cast<int>(std::floor(p[2] / kCell));
    const double r2 = static_cast<double>(kReach) * kReach;
    double w = 0.0, r = 0.0, g = 0.0, b = 0.0;
    // the window's cells that exist, in its z, y, x order (`nearOf`)
    for (const NearCell& e : nearOf(cx, cy, cz)) {
        const double n = e.w;
        const double dx = e.cx - p[0], dy = e.cy - p[1], dz = e.cz - p[2];
        const double d2 = dx * dx + dy * dy + dz * dz;
        if (d2 >= r2) continue;
        const double f = 1.0 - d2 / r2;
        const double k = f * f;   // per unit area: the cell's sums carry its area
        w += k * n; r += k * e.r; g += k * e.g; b += k * e.b;
    }
    if (w <= 0.0) return false;
    rgb[0] = static_cast<float>(r / w);
    rgb[1] = static_cast<float>(g / w);
    rgb[2] = static_cast<float>(b / w);
    return true;
}

void LightProbes::tint(const float p[3], float s, float base, float out[3]) const {
    out[0] = out[1] = out[2] = 1.0f;
    float rgb[3];
    if (s <= 0.0f || base <= 0.0f || meanLuma_ <= 0.0f || !sample(p, rgb)) return;
    // the start ADDS the place's departure from the set's mean colour: the
    // grey a set gives its bodies can be nearly black (Anekbah's is 10 of
    // 255) and the lamps outweigh any multiple of it - a tint by ratio did
    // not show (the reader, 2026-10-20: "difficult to see a difference")
    //
    // ...and a DARKER place shades it in PROPORTION, as a shadow does: added,
    // a departure larger than the grey took a body to black - by a fire in
    // Anekbah whose glow is a sprite, never painted into its alcove (0.25
    // against the set's 0.44, the reader's play test, 2026-10-20)
    for (int k = 0; k < 3; ++k) {
        const float m = meanRgb_[k];
        const float start = rgb[k] >= m || m <= 0.0f
            ? base + s * (rgb[k] - m)
            : base * (1.0f - s * (1.0f - rgb[k] / m));
        out[k] = std::clamp(start, 0.0f, 1.0f) / base;
    }
}

}  // namespace omk

#endif  // OMK_REMASTER
