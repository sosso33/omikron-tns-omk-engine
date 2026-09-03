// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/fnt.h"

namespace omk {
namespace {
std::uint16_t u16(std::span<const std::byte> d, std::size_t o) {
    if (o + 2 > d.size()) return 0;
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(d[o]) | static_cast<std::uint16_t>(d[o + 1]) << 8);
}
std::int16_t i16(std::span<const std::byte> d, std::size_t o) {
    return static_cast<std::int16_t>(u16(d, o));
}
}  // namespace

Font readFnt(std::span<const std::byte> d) {
    Font f;
    f.size = d.size();
    if (d.size() < kFntHeader) return f;
    for (std::size_t c = 0; c < kFntGlyphs; ++c) {
        const auto o = 8u * c;
        const auto units = u16(d, o);
        Glyph g;
        g.present = units != 0;
        // EIGHT-BYTE units, not bytes
        g.offset = static_cast<std::size_t>(units) * 8u;
        g.bottom = i16(d, o + 2);
        g.width  = i16(d, o + 4);
        g.height = i16(d, o + 6);
        f.glyphs[c] = g;
    }
    f.blob.assign(d.begin(), d.end());
    f.valid = true;
    return f;
}

}  // namespace omk
