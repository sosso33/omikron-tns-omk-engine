// SPDX-License-Identifier: GPL-3.0-or-later
#include "script/globaldata.h"

namespace omk {
namespace {

std::int16_t i16(std::span<const std::byte> d, std::size_t o) {
    if (o + 2 > d.size()) return 0;
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(d[o]) |
                                     (static_cast<std::uint16_t>(d[o + 1]) << 8));
}

std::uint32_t u32(std::span<const std::byte> d, std::size_t o) {
    if (o + 4 > d.size()) return 0;
    std::uint32_t v = 0;
    for (int k = 3; k >= 0; --k)
        v = (v << 8) | static_cast<std::uint32_t>(d[o + static_cast<std::size_t>(k)]);
    return v;
}

}  // namespace

std::vector<Recipe> globalRecipes(std::span<const std::byte> d) {
    std::vector<Recipe> out;
    const auto base = u32(d, 12);
    const auto n    = i16(d, 26);
    for (int k = 0; k < n; ++k) {
        const std::size_t o = base + 8u * static_cast<std::size_t>(k);
        if (o + 8 > d.size()) break;
        out.push_back({i16(d, o), i16(d, o + 2), i16(d, o + 4), i16(d, o + 6)});
    }
    return out;
}

std::int32_t globalSpellItem(std::span<const std::byte> d) {
    // int16, not int32: +66 is another field, and reading a dword here gives
    // 29229386 - a number with no meaning that nothing downstream rejects.
    return i16(d, 64);
}

}  // namespace omk
