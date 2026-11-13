// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/bytes.h"
#include "script/globaldata.h"

#include <algorithm>
#include <cstring>

namespace omk {
namespace {

using omk::le::i16;
using omk::le::u32;

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

std::int32_t globalHintPriceVar(std::span<const std::byte> d) {
    return i16(d, 72);
}

std::int32_t globalEventObject(std::span<const std::byte> d, int n) {
    const std::size_t o = 66u + 2u * static_cast<std::size_t>(n);
    return o + 2 <= d.size() ? i16(d, o) : -1;
}

std::vector<Destination> globalDestinations(std::span<const std::byte> d) {
    std::vector<Destination> out;
    const auto base = u32(d, 16);
    const auto n    = i16(d, 28);
    for (int k = 0; k < n; ++k) {
        const std::size_t o = base + 36u * static_cast<std::size_t>(k);
        if (o + 36 > d.size()) break;
        Destination e;
        e.bit = i16(d, o);
        e.area = i16(d, o + 2);
        // `sub_40E540` returns `record + 4` as a `char *`, so the name is
        // inline and NUL-terminated - 32 bytes of room, and every one of the
        // 39 shipped records terminates inside them.
        const char* p = reinterpret_cast<const char*>(&d[o + 4]);
        const std::size_t room = 32;
        // std::find, not strnlen: Mac OS X has strnlen only from 10.7, and the
        // PowerPC build (ppc-darwin.mk) targets 10.4
        e.name.assign(p, std::find(p, p + room, '\0'));
        out.push_back(std::move(e));
    }
    return out;
}

}  // namespace omk
