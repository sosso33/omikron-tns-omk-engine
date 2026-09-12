// SPDX-License-Identifier: GPL-3.0-or-later
#include <utility>
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
        // the floor's INTER-FLOOR LINKS (`formats/map2d.h`): the destination
        // floor, then the point on THIS floor and the point on that one
        f.links.resize(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            f.links[i].destFloor = u32(b, o);
            for (int k = 0; k < 3; ++k) {
                f.links[i].from[k] = f32(b, o + 4 + 4u * static_cast<std::size_t>(k));
                f.links[i].to[k]   = f32(b, o + 16 + 4u * static_cast<std::size_t>(k));
            }
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
            // ...and the same dwords named (`formats/map2d.h`): id, flags, and
            // `len` points of { u8 cellX, u8 cellZ, i16 clipId }. `body` is
            // kept whole beside them so nothing is lost to the naming.
            w.id    = words > 0 ? w.body[0] : 0;
            w.flags = words > 1 ? w.body[1] : 0;
            w.points.resize(w.len);
            for (std::uint32_t i = 0; i < w.len; ++i) {
                const std::uint32_t d = w.body[static_cast<std::size_t>(i) + 2];
                w.points[i].cellX  = static_cast<std::uint8_t>(d & 0xFF);
                w.points[i].cellZ  = static_cast<std::uint8_t>((d >> 8) & 0xFF);
                w.points[i].clipId = static_cast<std::int16_t>((d >> 16) & 0xFFFF);
            }
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

// ---- THE PATROL ROUTES, `sub_4354E0` .. `sub_435750` --------------------

int Map2d::routeFor(int floor, int cellX, int cellZ, int id) const {
    if (floor < 0 || static_cast<std::size_t>(floor) >= floors_.size()) return -1;
    const auto& ws = floors_[static_cast<std::size_t>(floor)].waypoints;
    if (ws.empty()) return -1;
    if (id <= 0) {
        // `v8 = 1000000` and the SQUARED cell distance of the first point,
        // strictly less - so the earliest of equals wins
        long best = 1000000;
        int found = -1;
        for (std::size_t i = 0; i < ws.size(); ++i) {
            if (ws[i].points.empty()) continue;
            const long dx = cellX - static_cast<long>(ws[i].points[0].cellX);
            const long dz = cellZ - static_cast<long>(ws[i].points[0].cellZ);
            const long d = dz * dz + dx * dx;
            if (d < best && !(ws[i].flags & 0x2u)) { best = d; found = static_cast<int>(i); }
        }
        return found;
    }
    for (std::size_t i = 0; i < ws.size(); ++i)
        if (static_cast<int>(ws[i].id) == id)
            return (ws[i].flags & 0x2u) ? -1 : static_cast<int>(i);
    return -1;                                  // `if (v4 >= v6) return 0`
}

int Map2d::routeNextIndex(int floor, int route, int index) {
    if (floor < 0 || static_cast<std::size_t>(floor) >= floors_.size()) return -1;
    auto& ws = floors_[static_cast<std::size_t>(floor)].waypoints;
    if (route < 0 || static_cast<std::size_t>(route) >= ws.size()) return -1;
    Map2dWaypoint& w = ws[static_cast<std::size_t>(route)];
    const int n = static_cast<int>(w.len);
    if (w.flags & 0x1u) {                       // ping-pong
        if (w.flags & 0x4u) {                   // coming back
            if (index - 1 < 0) { w.flags &= ~0x4u; return 0; }
            return index - 1;
        }
        if (index + 1 == n) { w.flags |= 0x4u; return n - 1; }
        return index + 1;
    }
    return index + 1 == n ? 0 : index + 1;      // plain: wrap to the start
}

int Map2d::routePoint(int floor, int route, int index, float& x, float& z) {
    if (floor < 0 || static_cast<std::size_t>(floor) >= floors_.size()) return -1;
    auto& f = floors_[static_cast<std::size_t>(floor)];
    if (route < 0 || static_cast<std::size_t>(route) >= f.waypoints.size()) return -1;
    Map2dWaypoint& w = f.waypoints[static_cast<std::size_t>(route)];
    if (index < 0 || static_cast<std::size_t>(index) >= w.points.size()) return -1;
    w.flags |= 0x2u;                            // `u32(a1, 8) |= 2` - TAKEN
    const double cell = static_cast<double>(scale_);
    x = static_cast<float>((static_cast<double>(w.points[static_cast<std::size_t>(index)].cellX)
                            + 0.5) * cell + f.bound[0]);
    z = static_cast<float>(f.bound[4] +
                           (static_cast<double>(w.points[static_cast<std::size_t>(index)].cellZ)
                            + 0.5) * cell);
    return w.points[static_cast<std::size_t>(index)].clipId;
}

int Map2d::routeClipId(int floor, int route, int index) const {
    if (floor < 0 || static_cast<std::size_t>(floor) >= floors_.size()) return 0;
    const auto& ws = floors_[static_cast<std::size_t>(floor)].waypoints;
    if (route < 0 || static_cast<std::size_t>(route) >= ws.size()) return 0;
    const auto& pts = ws[static_cast<std::size_t>(route)].points;
    if (index < 0 || static_cast<std::size_t>(index) >= pts.size()) return 0;
    return pts[static_cast<std::size_t>(index)].clipId;
}

void Map2d::routeRelease(int floor, int route) {
    if (floor < 0 || static_cast<std::size_t>(floor) >= floors_.size()) return;
    auto& ws = floors_[static_cast<std::size_t>(floor)].waypoints;
    if (route < 0 || static_cast<std::size_t>(route) >= ws.size()) return;
    ws[static_cast<std::size_t>(route)].flags &= ~0x2u;     // `u32(a1, 8) &= ~2`
}

bool Map2d::routeTaken(int floor, int route) const {
    if (floor < 0 || static_cast<std::size_t>(floor) >= floors_.size()) return false;
    const auto& ws = floors_[static_cast<std::size_t>(floor)].waypoints;
    if (route < 0 || static_cast<std::size_t>(route) >= ws.size()) return false;
    return (ws[static_cast<std::size_t>(route)].flags & 0x2u) != 0;
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

// `sub_4359A0` with a4 = 1, transcribed arm for arm rather than replaced by a
// generic Bresenham: the ORDER cells are visited in is what decides which one
// is reported, and both arms test TWICE per step (once after the minor-axis
// move, once after the major), which a textbook loop does not.
//
// The one deliberate divergence: the engine indexes the cell array with no
// bounds test at all, and `Map2dFloor::cell` returns 0 - a wall - outside the
// grid.  Blocking is the safe answer and the callers all start from a cell
// `floorAt` has already accepted.
bool Map2d::lineOfSight(int floor, int x0, int z0, int x1, int z1,
                        std::uint16_t doorOpenMask, int* blockX, int* blockZ) const {
    if (floor < 0 || static_cast<std::size_t>(floor) >= floors_.size()) return false;
    const Map2dFloor& f = floors_[static_cast<std::size_t>(floor)];

    auto report = [&](int x, int z) {
        if (blockX) *blockX = x;
        if (blockZ) *blockZ = z;
        return false;
    };
    auto open = [&](int x, int z) {
        return !sightBlockedValue(f.cell(x, z), doorOpenMask);
    };

    const int dx = x1 - x0, dz = z1 - z0;
    const int adx = dx < 0 ? -dx : dx, adz = dz < 0 ? -dz : dz;

    if (adx <= adz) {                       // steep: the row is the major axis
        int sx = dx;
        if (dz < 0) { x0 = x1; std::swap(z0, z1); sx = -sx; }
        const int xs = sx >= 0 ? 1 : -1;
        const int twoDx = 2 * adx, step = 2 * (adx - adz);
        int err = twoDx - adz, next = err;
        if (z0 >= z1) return true;
        for (;;) {
            if (err > 0) {
                x0 += xs;
                if (!open(x0, z0)) return report(x0, z0);
                next = err + step;
            } else {
                next = twoDx + err;
            }
            ++z0;
            if (!open(x0, z0)) return report(x0, z0);
            if (z0 >= z1) return true;
            err = next;
        }
    }

    int d = dz;                             // shallow: the column is the major
    if (dx < 0) { std::swap(x0, x1); z0 = z1; d = -dz; }
    const int zs = d < 0 ? -1 : 1;
    const int twoDz = 2 * adz, step = 2 * (adz - adx);
    int err = twoDz - adx;
    if (x0 >= x1) return true;
    for (;;) {
        if (err > 0) {
            err += step;
            ++x0;
            if (!open(x0, z0)) return report(x0, z0);
            z0 += zs;
        } else {
            err += twoDz;
            ++x0;
        }
        if (!open(x0, z0)) return report(x0, z0);
        if (x0 >= x1) return true;
    }
}

}  // namespace omk
