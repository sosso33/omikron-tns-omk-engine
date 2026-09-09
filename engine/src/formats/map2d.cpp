// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/map2d.h"
#include "platform/datafs.h"

#include <cstring>

namespace omk {
namespace {

std::uint32_t u32(std::span<const std::byte> b, std::size_t o) {
    std::uint32_t v = 0;
    if (o + 4 > b.size()) return 0;
    std::memcpy(&v, b.data() + o, 4);
    return v;
}
float f32(std::span<const std::byte> b, std::size_t o) {
    float v = 0;
    if (o + 4 > b.size()) return 0;
    std::memcpy(&v, b.data() + o, 4);
    return v;
}
std::int32_t i32(std::span<const std::byte> b, std::size_t o) {
    std::int32_t v = 0;
    if (o + 4 > b.size()) return 0;
    std::memcpy(&v, b.data() + o, 4);
    return v;
}

}  // namespace

bool Map2d::load(std::span<const std::byte> b) {
    valid_ = false;
    floors_.clear();
    if (b.size() < 8) return false;
    scale_ = u32(b, 0);
    const std::uint32_t count = u32(b, 4);
    if (scale_ == 0 || count == 0 || count > 4096) return false;
    std::size_t o = 8;
    floors_.resize(count);
    for (auto& f : floors_) {
        const std::uint32_t n = u32(b, o);
        o += 4;
        if (o + 28u * n > b.size()) return false;
        f.segments.resize(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            f.segments[i].kind = u32(b, o);
            for (int k = 0; k < 6; ++k) f.segments[i].v[k] = f32(b, o + 4 + 4u * static_cast<std::size_t>(k));
            o += 28;
        }
        for (int k = 0; k < 6; ++k) f.bound[k] = f32(b, o + 4u * static_cast<std::size_t>(k));
        f.w = u32(b, o + 24);
        f.h = u32(b, o + 28);
        o += 32;
        const std::size_t n2 = static_cast<std::size_t>(f.w) * f.h;
        if (o + n2 > b.size()) return false;
        f.cells.resize(n2);
        std::memcpy(f.cells.data(), b.data() + o, n2);
        o += n2;
    }
    for (auto& f : floors_) {
        const std::uint32_t k = u32(b, o);
        o += 4;
        f.waypoints.resize(k);
        for (auto& w : f.waypoints) {
            w.len = u32(b, o);
            const std::size_t words = static_cast<std::size_t>(w.len) + 2;
            if (o + 4 + 4 * words > b.size()) return false;
            w.body.resize(words);
            for (std::size_t i = 0; i < words; ++i) w.body[i] = u32(b, o + 4 + 4 * i);
            o += 4 * (words + 1);
        }
    }
    // one 192-byte door table per floor, and the walk must land on the size:
    // that is the invariant `verify.py: map2d` already asserts on 16 of 16.
    for (auto& f : floors_) {
        for (int i = 0; i < 16; ++i) {
            f.doors[i].arm         = i32(b, o + 0);
            f.doors[i].openObject  = i32(b, o + 4);
            f.doors[i].closeObject = i32(b, o + 8);
            o += 12;
        }
    }
    if (o != b.size()) return false;
    valid_ = true;
    return true;
}

bool Map2d::loadFile(const std::string& path) {
    const auto raw = DataFs::readPath(path);
    return raw.empty() ? false : load(raw);
}

int Map2d::floorAt(float x, float y, float z, int exclude) const {
    // `sub_435020`, including its two oddities: the y test is `<= 0` against
    // bound[3] with Y pointing DOWN, and the winner is the GREATEST such y.
    int best = -1;
    double bestY = -100000.0;
    for (std::size_t i = 0; i < floors_.size(); ++i) {
        if (static_cast<int>(i) == exclude) continue;
        const Map2dFloor& f = floors_[i];
        if (!scale_) continue;
        const long cx = static_cast<long>((x - f.bound[0]) / static_cast<float>(scale_));
        const long cz = static_cast<long>((z - f.bound[4]) / static_cast<float>(scale_));
        if (cx < 0 || cx >= static_cast<long>(f.w) || cz < 0 || cz >= static_cast<long>(f.h))
            continue;
        const double dy = static_cast<double>(y) - f.bound[3];
        if (dy <= 0.0 && dy > bestY) { bestY = dy; best = static_cast<int>(i); }
    }
    return best;
}

bool Map2d::cellAt(int floor, float x, float z, int& cx, int& cz) const {
    if (floor < 0 || static_cast<std::size_t>(floor) >= floors_.size() || !scale_) return false;
    const Map2dFloor& f = floors_[static_cast<std::size_t>(floor)];
    cx = static_cast<int>((x - f.bound[0]) / static_cast<float>(scale_));
    cz = static_cast<int>((z - f.bound[4]) / static_cast<float>(scale_));
    return true;
}

bool Map2d::blocked(int floor, int cx, int cz) const {
    if (floor < 0 || static_cast<std::size_t>(floor) >= floors_.size()) return true;
    const Map2dFloor& f = floors_[static_cast<std::size_t>(floor)];
    // the engine's own margin: `< 1` refuses, not `< 0`
    if (cx < 1 || static_cast<std::uint32_t>(cx) >= f.w ||
        cz < 1 || static_cast<std::uint32_t>(cz) >= f.h) return true;
    return blockedValue(f.cell(cx, cz));
}

}  // namespace omk
