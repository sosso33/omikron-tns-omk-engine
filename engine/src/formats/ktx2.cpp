// SPDX-License-Identifier: GPL-3.0-or-later
// KTX2 - see ktx2.h. Not a port of anything in the original.
// (the remaster's: compiled empty without OMK_REMASTER - `engine/Makefile`)
#if OMK_REMASTER

#include "formats/ktx2.h"

#include <cstring>

namespace omk {
namespace {

constexpr std::uint8_t kId[12] = {0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, '\r', '\n', 0x1A, '\n'};

std::uint32_t u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}
std::uint64_t u64(const std::uint8_t* p) {
    return static_cast<std::uint64_t>(u32(p)) | static_cast<std::uint64_t>(u32(p + 4)) << 32;
}
void put32(std::vector<std::uint8_t>& o, std::uint32_t v) {
    for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
}
void put64(std::vector<std::uint8_t>& o, std::uint64_t v) {
    put32(o, static_cast<std::uint32_t>(v));
    put32(o, static_cast<std::uint32_t>(v >> 32));
}
void set64(std::vector<std::uint8_t>& o, std::size_t at, std::uint64_t v) {
    for (int k = 0; k < 8; ++k) o[at + static_cast<std::size_t>(k)] = static_cast<std::uint8_t>(v >> (8 * k));
}

std::size_t levelBytes(const BlockFormat& f, int w, int h) {
    return static_cast<std::size_t>((w + f.bw - 1) / f.bw) * static_cast<std::size_t>((h + f.bh - 1) / f.bh) *
           static_cast<std::size_t>(f.bytes);
}

bool fail(std::string* why, const char* s) {
    if (why) *why = s;
    return false;
}

}  // namespace

BlockFormat blockFormat(std::uint32_t f) {
    switch (f) {
        case kVkBc1RgbaUnorm: return {4, 4, 8, false};
        case kVkBc3Unorm: return {4, 4, 16, false};
        case kVkBc4Unorm: return {4, 4, 8, false};
        case kVkBc7Unorm: return {4, 4, 16, false};
        case kVkAstc4x4Unorm: return {4, 4, 16, true};
        case kVkAstc5x5Unorm: return {5, 5, 16, true};
        case kVkAstc6x6Unorm: return {6, 6, 16, true};
        case kVkAstc8x8Unorm: return {8, 8, 16, true};
        default: return {};
    }
}

bool readKtx2(std::vector<std::uint8_t> file, Ktx2& out, std::string* why) {
    out = Ktx2{};
    const std::size_t n = file.size();
    const std::uint8_t* p = file.data();
    if (n < 80 || std::memcmp(p, kId, 12) != 0) return fail(why, "not a KTX2 file");
    const std::uint32_t fmt = u32(p + 12), typeSize = u32(p + 16), w = u32(p + 20), h = u32(p + 24);
    const std::uint32_t depth = u32(p + 28), layers = u32(p + 32), faces = u32(p + 36), nl = u32(p + 40),
                        sc = u32(p + 44);
    const BlockFormat bf = blockFormat(fmt);
    if (!bf.bytes) return fail(why, "a format the pack does not use");
    if (typeSize != 1 || depth != 0 || layers != 0 || faces != 1) return fail(why, "not one 2D image");
    if (sc != 0) return fail(why, "supercompressed");
    if (w == 0 || h == 0 || w > 16384 || h > 16384) return fail(why, "a size out of range");
    if (nl == 0 || nl > 15 || 80 + static_cast<std::size_t>(nl) * 24 > n) return fail(why, "a bad level count");
    int lw = static_cast<int>(w), lh = static_cast<int>(h);
    for (std::uint32_t l = 0; l < nl; ++l) {
        const std::uint8_t* e = p + 80 + static_cast<std::size_t>(l) * 24;
        const std::uint64_t off = u64(e), len = u64(e + 8);
        if (len != levelBytes(bf, lw, lh)) return fail(why, "a level's length is not its block count");
        if (off > n || len > n - off) return fail(why, "a level runs past the file");
        out.levels.push_back({static_cast<std::size_t>(off), static_cast<std::size_t>(len)});
        lw = lw > 1 ? lw / 2 : 1;
        lh = lh > 1 ? lh / 2 : 1;
    }
    out.vkFormat = fmt;
    out.w = static_cast<int>(w);
    out.h = static_cast<int>(h);
    out.bytes = std::move(file);
    return true;
}

std::vector<std::uint8_t> writeKtx2(std::uint32_t vkFormat, int w, int h,
                                    const std::vector<std::vector<std::uint8_t>>& levels) {
    const BlockFormat bf = blockFormat(vkFormat);
    const std::size_t nl = levels.size();
    std::vector<std::uint8_t> o(kId, kId + 12);
    // one array of one type: where `std::uint32_t` is `unsigned long` (the
    // 3DS's newlib) a braced list mixing it with `1u` deduces nothing
    const std::uint32_t header[9] = {vkFormat, 1, static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), 0, 0,
                                     1, static_cast<std::uint32_t>(nl), 0};
    for (const std::uint32_t v : header) put32(o, v);
    // the DATA FORMAT DESCRIPTOR: one basic block, one sample over the whole
    // block (the spec's form for a block-compressed format)
    // KHR_DF_MODEL_BC1A 128, BC3 130, BC4 131, BC7 134, ASTC 162
    const std::uint32_t model = bf.astc ? 162u
                              : vkFormat == kVkBc1RgbaUnorm ? 128u
                              : vkFormat == kVkBc3Unorm     ? 130u
                              : vkFormat == kVkBc4Unorm     ? 131u
                                                            : 134u;
    const std::uint32_t dfdSize = 4 + 24 + 16;
    const std::uint32_t dfdOff = static_cast<std::uint32_t>(80 + 24 * nl);
    put32(o, dfdOff);
    put32(o, dfdSize);
    put32(o, 0);   // no key/value data
    put32(o, 0);
    put64(o, 0);   // no supercompression global data
    put64(o, 0);
    const std::size_t index = o.size();
    o.resize(o.size() + 24 * nl, 0);
    put32(o, dfdSize);
    put32(o, 0);                                   // vendor 0 (Khronos), descriptor type 0
    put32(o, 2u | (24u + 16u) << 16);              // version 2, block size
    o.push_back(static_cast<std::uint8_t>(model));
    o.push_back(1);                                // BT.709 primaries
    o.push_back(1);                                // linear: the engine's textures are UNORM
    o.push_back(0);                                // straight alpha
    o.push_back(static_cast<std::uint8_t>(bf.bw - 1));
    o.push_back(static_cast<std::uint8_t>(bf.bh - 1));
    o.push_back(0);
    o.push_back(0);
    o.push_back(static_cast<std::uint8_t>(bf.bytes));   // bytesPlane0
    for (int k = 0; k < 7; ++k) o.push_back(0);
    // the sample: bit 0, the block's bits, channel 0 (red / data)
    o.push_back(0); o.push_back(0);
    o.push_back(static_cast<std::uint8_t>(bf.bytes * 8 - 1));
    o.push_back(0);
    for (int k = 0; k < 4; ++k) o.push_back(0);
    put32(o, 0);
    put32(o, 0xFFFFFFFFu);
    // the levels, SMALLEST FIRST (the spec's recommended order), each aligned
    // to the block size; the index keeps level 0 first
    const std::size_t align = static_cast<std::size_t>(bf.bytes);
    for (std::size_t l = nl; l-- > 0;) {
        while (o.size() % align) o.push_back(0);
        set64(o, index + l * 24, o.size());
        set64(o, index + l * 24 + 8, levels[l].size());
        set64(o, index + l * 24 + 16, levels[l].size());
        o.insert(o.end(), levels[l].begin(), levels[l].end());
    }
    return o;
}

}  // namespace omk

#endif  // OMK_REMASTER
