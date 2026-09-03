// SPDX-License-Identifier: GPL-3.0-or-later
// See `addresses.h` for where every field comes from.
#include "formats/addresses.h"

#include "o3de/worldcam.h"

namespace omk {
namespace {

std::int32_t i32at(std::span<const std::byte> d, std::size_t o) {
    return static_cast<std::int32_t>(
        static_cast<std::uint32_t>(d[o]) |
        (static_cast<std::uint32_t>(d[o + 1]) << 8) |
        (static_cast<std::uint32_t>(d[o + 2]) << 16) |
        (static_cast<std::uint32_t>(d[o + 3]) << 24));
}

std::int16_t i16at(std::span<const std::byte> d, std::size_t o) {
    return static_cast<std::int16_t>(
        static_cast<std::uint16_t>(d[o]) |
        (static_cast<std::uint16_t>(d[o + 1]) << 8));
}

}  // namespace

std::vector<Address> readAddresses(std::span<const std::byte> b) {
    std::vector<Address> out;
    if (b.size() < 84) return out;
    const auto at = i32at(b, 60);
    const auto n  = i16at(b, 82);
    if (n <= 0 || at <= 0) return out;
    const auto base = static_cast<std::size_t>(at);
    if (base + 16u * static_cast<std::size_t>(n) > b.size()) return out;
    out.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const std::size_t o = base + 16u * static_cast<std::size_t>(i);
        Address a;
        for (int k = 0; k < 3; ++k)
            a.pos[k] = rawToWorld(i32at(b, o + 4u * static_cast<std::size_t>(k)));
        // The same 4096-per-turn field a camera's roll uses, wrapped the same
        // way - a heading is an angle and angles wrap (CLAUDE.md 1).
        a.yaw = angle4096(i16at(b, o + 12));
        a.id  = i16at(b, o + 14);
        out.push_back(a);
    }
    return out;
}

}  // namespace omk
