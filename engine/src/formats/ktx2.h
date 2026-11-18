// SPDX-License-Identifier: GPL-3.0-or-later
// KTX2 - the Khronos texture container (KTX 2.0, `https://registry.khronos.org/
// KTX/specs/2.0/ktxspec.v2.html`), for the remaster's texture pack in GPU
// block formats (`o3de/texpack.h`). NOT A FORMAT OF THE ORIGINAL'S: the
// game's textures are `.3DT` palettes; this holds what the pack makes of them.
//
// Only what the pack writes is read: one 2D image, no array, no faces, no
// supercompression, every mip level present, in one of the block formats
// below. Anything else is refused with the reason - the reader checks every
// level's length against its block count and every offset against the file,
// so a truncated or foreign file never reaches a GPU.
//
//     +0   the identifier, 12 bytes
//     +12  vkFormat, typeSize (1), width, height, depth (0), layers (0),
//          faces (1), levelCount, supercompression (0)      - u32 each
//     +48  dfdByteOffset, dfdByteLength, kvdByteOffset, kvdByteLength (u32),
//          sgdByteOffset, sgdByteLength (u64)
//     +80  the level index: levelCount x (offset, length, uncompressed
//          length), u64 each, level 0 (the largest) first
//     then the data format descriptor (one basic block), and the levels -
//     written smallest first, each aligned to its block size.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace omk {

// The Vulkan format numbers the file carries (`VkFormat`), UNORM throughout:
// the engine's textures are R8G8B8A8_UNORM, no sRGB decode.
// BC1 (DXT1) and BC3 (DXT5): the Vita's pack (`omktexpack.py vita`) - its
// GPU samples UBC1..5 (sceGxm) and neither BC7 nor ASTC
inline constexpr std::uint32_t kVkBc1RgbaUnorm = 133;
inline constexpr std::uint32_t kVkBc3Unorm = 137;
inline constexpr std::uint32_t kVkBc4Unorm = 139;
inline constexpr std::uint32_t kVkBc7Unorm = 145;
inline constexpr std::uint32_t kVkAstc4x4Unorm = 157;
inline constexpr std::uint32_t kVkAstc5x5Unorm = 161;
inline constexpr std::uint32_t kVkAstc6x6Unorm = 165;
inline constexpr std::uint32_t kVkAstc8x8Unorm = 171;

struct BlockFormat {
    int bw = 0, bh = 0, bytes = 0;   // texels a block each way, bytes a block
    bool astc = false;
};
// -> the block layout of a format the pack uses, or {0,0,0} for any other
BlockFormat blockFormat(std::uint32_t vkFormat);

struct Ktx2 {
    std::uint32_t vkFormat = 0;
    int w = 0, h = 0;
    std::vector<std::uint8_t> bytes;                          // the whole file
    std::vector<std::pair<std::size_t, std::size_t>> levels;  // offset, length; level 0 first
    std::span<const std::uint8_t> level(std::size_t i) const {
        return {bytes.data() + levels[i].first, levels[i].second};
    }
};

// `file` taken over; false with `why` on anything this does not read
bool readKtx2(std::vector<std::uint8_t> file, Ktx2& out, std::string* why = nullptr);

// One file of `vkFormat` from its levels, level 0 (w x h) first, each the
// block count of its size - halved each level, never under 1.
std::vector<std::uint8_t> writeKtx2(std::uint32_t vkFormat, int w, int h,
                                    const std::vector<std::vector<std::uint8_t>>& levels);

}  // namespace omk
