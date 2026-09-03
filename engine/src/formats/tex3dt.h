// SPDX-License-Identifier: GPL-3.0-or-later
// The .3DT textures that sit beside each .3DO.
//
// One .3dt holds every texture of the matching .3DO, in material order; each
// is a palette followed by image data:
//
//     palette      3 bytes a colour - 16 colours when bpp == 4, else 256
//     image data   `dataSize` bytes, from the material record
//
// `dataSize == 65536` means a raw 256x256 image. Otherwise it is an LZ scheme
// (see decodeImage). The walk is exact: palette + data per material must land
// on exactly the file's length, which holds for all 635 .3DO/.3dt pairs.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace omk {

struct Texture {
    std::string              name;
    int                      width  = 0;
    int                      height = 0;
    int                      bpp    = 0;
    // width*height*3 bytes, palette applied. Empty only on a malformed file.
    std::vector<std::uint8_t> rgb;
    // false if the stream did not produce exactly width*height indices. True
    // for every one of the 2534 textures shipped under gamedata/MESHES; kept because
    // this same reader is used on the .3DO files embedded in the SCX stream.
    bool exact = false;
};

// Expand `data` until `want` palette indices are produced.
//
// The first byte is always a literal. After it the stream is groups of eight:
// a control byte supplies 8 flags, MSB first. A clear flag copies one literal
// byte; a set flag introduces a token
//
//     size = (token & 0xFC) / 4 + 3        the run length
//     kind =  token & 3
//        0   repeat the previous pixel size-1 times
//        1   back-reference, next byte     + 1 = offset
//        2   back-reference, next two bytes (big-endian) + 1 = offset
//        3   back-reference, next byte * 256 = offset
//
// Always returns exactly `want` bytes: a run that overshoots is trimmed and a
// short stream is padded with its last pixel, so a caller never gets a partial
// image. `exact` reports which happened.
std::vector<std::uint8_t> decodeImage(std::span<const std::byte> data,
                                      std::size_t want, bool& exact);

// Decode every texture of a model, from the two files' bytes.
// `d` is the .3DO, `t` the .3dt. -> one entry a material, in material order.
std::vector<Texture> textures(std::span<const std::byte> d,
                              std::span<const std::byte> t);

}  // namespace omk
