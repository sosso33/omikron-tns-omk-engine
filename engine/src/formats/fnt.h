// SPDX-License-Identifier: GPL-3.0-or-later
// `FONTS/*.FNT` - the interface's proportional, anti-aliased bitmap fonts.
//
// One blob, no header of its own:
//
//     +0      256 glyph records of 8 bytes, indexed BY CHARACTER CODE:
//               +0  u16  the pixel data's offset in EIGHT-BYTE UNITS;
//                        0 = absent, and the renderer falls back to the font
//                        record's default advance
//               +2  i16  the bottom edge, relative to the baseline
//               +4  i16  the width - plus kerning, the advance
//               +6  i16  the height
//     +2048   the pixels: width x height bytes a glyph, row-major, top row
//             first, one byte a pixel holding a COVERAGE level 0..31
//
// **The coverage is the whole colour model.** `sub_43EA10` builds a 32-entry
// ramp of the requested colour once and each non-zero byte indexes it, so a
// glyph is greyscale antialiasing that takes the text's colour at draw time
// and zero is transparent. A reader that treated the byte as an intensity, or
// as a palette index, would be drawing something else.
//
// The offset being in EIGHT-BYTE UNITS is the trap: read as bytes, every
// glyph lands in the wrong place and most run past the file.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace omk {

inline constexpr std::size_t kFntGlyphs = 256;   // the renderer indexes with a u8
inline constexpr std::size_t kFntHeader = kFntGlyphs * 8;
inline constexpr int kFntRamp = 32;              // word_52F5B8[32]

struct Glyph {
    bool         present = false;
    std::size_t  offset = 0;      // already in BYTES, from the file's start
    std::int16_t bottom = 0;
    std::int16_t width = 0;
    std::int16_t height = 0;
    std::size_t  pixels() const {
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    }
};

struct Font {
    bool valid = false;
    std::size_t size = 0;
    Glyph glyphs[kFntGlyphs];
    // The blob itself, kept so a glyph's COVERAGE bytes are reachable from
    // the Font alone. `Glyph::offset` indexes it. Without this a caller has to
    // hold the file span alongside the Font and keep the two in step, which is
    // exactly the kind of pairing that goes wrong once a font is cached.
    std::vector<std::byte> blob;

    // -> the `width * height` coverage bytes of one glyph, or an empty span.
    std::span<const std::byte> coverage(unsigned char code) const {
        const Glyph& g = glyphs[code];
        if (!g.present || g.offset + g.pixels() > blob.size()) return {};
        return std::span<const std::byte>(blob).subspan(g.offset, g.pixels());
    }
};

Font readFnt(std::span<const std::byte> d);

}  // namespace omk
