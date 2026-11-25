// SPDX-License-Identifier: GPL-3.0-or-later
// BC7 / BC4 on the CPU - see blockdecode.h. Not a port of anything in the original.
// (the remaster's: compiled empty without OMK_REMASTER - `engine/Makefile`)
#if OMK_REMASTER

#include "formats/blockdecode.h"

#include <algorithm>
// rgbcx.h calls memcpy / memset without <cstring>: the host's and Android's
// headers bring it in transitively, newlib's (the Switch) do not
#include <cstring>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include "bc7enc/bc7decomp.cpp"
#define RGBCX_IMPLEMENTATION
#include "bc7enc/rgbcx.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace omk {
namespace {

// each 4x4 block of `w x h` in order, `size` bytes a block: `put(block, x0, y0)`
template <typename Put>
bool eachBlock(std::span<const std::uint8_t> blocks, int w, int h, std::size_t size, Put put) {
    if (w <= 0 || h <= 0) return false;
    const int bx = (w + 3) / 4, by = (h + 3) / 4;
    if (blocks.size() < static_cast<std::size_t>(bx) * static_cast<std::size_t>(by) * size) return false;
    const std::uint8_t* b = blocks.data();
    for (int y = 0; y < by; ++y)
        for (int x = 0; x < bx; ++x, b += size) put(b, x * 4, y * 4);
    return true;
}

}  // namespace

bool decodeBc7(std::span<const std::uint8_t> blocks, int w, int h, std::vector<std::uint8_t>& rgba) {
    rgba.assign(static_cast<std::size_t>(std::max(w, 0)) * static_cast<std::size_t>(std::max(h, 0)) * 4, 0);
    return eachBlock(blocks, w, h, 16, [&](const std::uint8_t* b, int x0, int y0) {
        bc7decomp::color_rgba d[16];
        bc7decomp::unpack_bc7(b, d);
        for (int i = 0; i < 16; ++i) {
            const int x = x0 + (i & 3), y = y0 + (i >> 2);
            if (x >= w || y >= h) continue;
            std::uint8_t* o = &rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + x) * 4];
            o[0] = d[i].r; o[1] = d[i].g; o[2] = d[i].b; o[3] = d[i].a;
        }
    });
}

bool decodeBc4(std::span<const std::uint8_t> blocks, int w, int h, std::vector<std::uint8_t>& r) {
    r.assign(static_cast<std::size_t>(std::max(w, 0)) * static_cast<std::size_t>(std::max(h, 0)), 0);
    return eachBlock(blocks, w, h, 8, [&](const std::uint8_t* b, int x0, int y0) {
        std::uint8_t d[16];
        rgbcx::unpack_bc4(b, d, 1);
        for (int i = 0; i < 16; ++i) {
            const int x = x0 + (i & 3), y = y0 + (i >> 2);
            if (x < w && y < h) r[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + x] = d[i];
        }
    });
}

}  // namespace omk

#endif  // OMK_REMASTER
