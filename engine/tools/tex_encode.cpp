// SPDX-License-Identifier: GPL-3.0-or-later
// THE TEXTURE PACK IN GPU BLOCK FORMATS (`o3de/texpack.h`, `formats/ktx2.h`) -
// for `tools/omktexpack.py compress`.
//
//     tex_encode [--max N] bc7|bc7n|bc4|bc1|bc3 <in.png> <out.ktx2>
//     tex_encode [--max N] dxt <in.png> <out-stem>
//     tex_encode [--max N] astc <in.png> <out.ktx2> <astcenc> <block>
//
// One pack PNG -> one KTX2 holding its WHOLE mip chain, each level the
// previous one halved by 2x2 averages of RGBA - what the Vulkan backend's
// linear blit ladder makes of a PNG at upload, so a compressed texture
// filters as the PNG did. Block formats cannot be blitted: the chain is made
// here. Then:
//   * bc7  - the colour, BC7 (bc7enc, perceptual weights, `third_party/bc7enc`)
//   * bc7n - a normal map, BC7 with LINEAR weights (no channel is luma)
//   * bc4  - a light map, its red channel, BC4 (rgbcx)
//   * bc1  - a colour with no alpha, BC1 (DXT1, rgbcx; 4 bits a texel)
//   * bc3  - a colour with alpha, BC3 (DXT5, rgbcx; 8 bits a texel)
//   * dxt  - bc1 when every texel of the written level 0 is opaque, else
//            bc3, to <out-stem>.bc1.ktx2 / .bc3.ktx2 (the Vita's pack,
//            `omktexpack.py vita`: its GPU samples DXT, neither BC7 nor ASTC)
//   * astc - any of them, ASTC at <block> (4x4, 5x5, 6x6, 8x8) by ARM's
//            `astcenc` (run per level, `-cl ... -medium`; a normal map too:
//            `-normal` would drop the height its alpha carries)
// --max N: the levels larger than N either way are left out - the file
// starts at the first level of the chain that fits, so its mips are still the
// 2x2 averages of the full image (the Vita's pack: 2x the original, not 4x).
// Prints "@tex_encode <codec> WxH levels N psnr P" - P the level 0 round trip
// in dB (decoded back by bc7decomp / rgbcx / `astcenc -dl`), over RGBA (or R
// for bc4).
#include "formats/ktx2.h"
#include "platform/datafs.h"
#include "platform/pngread.h"

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
#include "bc7enc/bc7enc.c"
// the decoders, compiled once in `src/formats/blockdecode.cpp`
#include "bc7enc/bc7decomp.h"
#include "bc7enc/rgbcx.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Rgba {
    int w = 0, h = 0;
    std::vector<std::uint8_t> px;
    const std::uint8_t* at(int x, int y) const {
        x = std::min(std::max(x, 0), w - 1);
        y = std::min(std::max(y, 0), h - 1);
        return &px[(static_cast<std::size_t>(y) * w + x) * 4];
    }
};

Rgba halve(const Rgba& a) {
    Rgba o;
    o.w = std::max(1, a.w / 2);
    o.h = std::max(1, a.h / 2);
    o.px.resize(static_cast<std::size_t>(o.w) * o.h * 4);
    for (int y = 0; y < o.h; ++y)
        for (int x = 0; x < o.w; ++x)
            for (int k = 0; k < 4; ++k) {
                const int s = a.at(2 * x, 2 * y)[k] + a.at(2 * x + 1, 2 * y)[k] + a.at(2 * x, 2 * y + 1)[k] +
                              a.at(2 * x + 1, 2 * y + 1)[k];
                o.px[(static_cast<std::size_t>(y) * o.w + x) * 4 + k] = static_cast<std::uint8_t>((s + 2) / 4);
            }
    return o;
}

// one level in 4x4 blocks: `enc(block 16 RGBA texels, out)`, the edges repeated
template <class Enc>
std::vector<std::uint8_t> blocks4(const Rgba& a, int bytes, Enc enc) {
    const int bx = (a.w + 3) / 4, by = (a.h + 3) / 4;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(bx) * by * bytes);
    std::uint8_t blk[64];
    for (int y = 0; y < by; ++y)
        for (int x = 0; x < bx; ++x) {
            for (int j = 0; j < 4; ++j)
                for (int i = 0; i < 4; ++i) std::memcpy(blk + (j * 4 + i) * 4, a.at(x * 4 + i, y * 4 + j), 4);
            enc(blk, &out[(static_cast<std::size_t>(y) * bx + x) * bytes]);
        }
    return out;
}

double psnr(double se, double n) {
    if (se <= 0) return 99.0;
    return 10.0 * std::log10(255.0 * 255.0 / (se / n));
}

bool writeTga(const std::string& path, const Rgba& a) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::uint8_t hd[18] = {};
    hd[2] = 2;   // uncompressed true colour
    hd[12] = static_cast<std::uint8_t>(a.w & 255); hd[13] = static_cast<std::uint8_t>(a.w >> 8);
    hd[14] = static_cast<std::uint8_t>(a.h & 255); hd[15] = static_cast<std::uint8_t>(a.h >> 8);
    hd[16] = 32;
    hd[17] = 0x28;   // 8 alpha bits, top-left origin
    std::fwrite(hd, 1, 18, f);
    std::vector<std::uint8_t> row(static_cast<std::size_t>(a.w) * 4);
    for (int y = 0; y < a.h; ++y) {
        for (int x = 0; x < a.w; ++x) {
            const std::uint8_t* p = a.at(x, y);
            row[x * 4] = p[2]; row[x * 4 + 1] = p[1]; row[x * 4 + 2] = p[0]; row[x * 4 + 3] = p[3];
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    return true;
}

std::vector<std::uint8_t> readAll(const std::string& p) {
    const auto b = omk::readWholeFile(p);
    return std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t*>(b.data()),
                                     reinterpret_cast<const std::uint8_t*>(b.data()) + b.size());
}

}  // namespace

int main(int argc, char** argv) {
    int maxSize = 0;
    if (argc >= 3 && std::strcmp(argv[1], "--max") == 0) {
        maxSize = std::atoi(argv[2]);
        if (maxSize < 4) { std::fprintf(stderr, "--max %s: at least 4\n", argv[2]); return 2; }
        argc -= 2;
        argv += 2;
    }
    if (argc < 4) {
        std::fprintf(stderr, "usage: tex_encode [--max N] bc7|bc7n|bc4|bc1|bc3 <in.png> <out.ktx2>\n"
                             "       tex_encode [--max N] dxt <in.png> <out-stem>\n"
                             "       tex_encode [--max N] astc <in.png> <out.ktx2> <astcenc> <block>\n");
        return 2;
    }
    std::string codec = argv[1], out = argv[3];
    const std::string in = argv[2];
    const auto png = readAll(in);
    omk::PngImage img;
    std::string why;
    if (png.empty() || !omk::decodePng(png.data(), png.size(), img, &why)) {
        std::fprintf(stderr, "%s: %s\n", in.c_str(), why.empty() ? "unreadable" : why.c_str());
        return 1;
    }
    std::vector<Rgba> chain(1);
    chain[0].w = img.w; chain[0].h = img.h; chain[0].px = std::move(img.rgba);
    while (chain.back().w > 1 || chain.back().h > 1) chain.push_back(halve(chain.back()));
    if (maxSize)
        while (chain.size() > 1 && (chain[0].w > maxSize || chain[0].h > maxSize)) chain.erase(chain.begin());
    if (codec == "dxt") {
        bool opaque = true;
        for (std::size_t q = 3; q < chain[0].px.size() && opaque; q += 4) opaque = chain[0].px[q] == 255;
        codec = opaque ? "bc1" : "bc3";
        out += opaque ? ".bc1.ktx2" : ".bc3.ktx2";
    }
    if (!omk::safeOutputPath(out)) return 2;

    std::vector<std::vector<std::uint8_t>> levels;
    std::uint32_t fmt = 0;
    double se = 0, n = 0;
    if (codec == "bc7" || codec == "bc7n") {
        fmt = omk::kVkBc7Unorm;
        bc7enc_compress_block_init();
        bc7enc_compress_block_params p;
        bc7enc_compress_block_params_init(&p);
        if (codec == "bc7n") bc7enc_compress_block_params_init_linear_weights(&p);
        for (const Rgba& a : chain)
            levels.push_back(blocks4(a, 16, [&](const std::uint8_t* blk, std::uint8_t* o) {
                bc7enc_compress_block(o, blk, &p);
                if (&a != &chain[0]) return;
                bc7decomp::color_rgba d[16];
                bc7decomp::unpack_bc7(o, d);
                for (int i = 0; i < 16; ++i) {
                    const std::uint8_t* q = reinterpret_cast<const std::uint8_t*>(&d[i]);
                    for (int k = 0; k < 4; ++k) se += (q[k] - blk[i * 4 + k]) * (q[k] - blk[i * 4 + k]);
                    n += 4;
                }
            }));
    } else if (codec == "bc4") {
        fmt = omk::kVkBc4Unorm;
        rgbcx::init();
        for (const Rgba& a : chain)
            levels.push_back(blocks4(a, 8, [&](const std::uint8_t* blk, std::uint8_t* o) {
                rgbcx::encode_bc4(o, blk, 4);
                if (&a != &chain[0]) return;
                std::uint8_t d[64];
                rgbcx::unpack_bc4(o, d, 4);
                for (int i = 0; i < 16; ++i) { se += (d[i * 4] - blk[i * 4]) * (d[i * 4] - blk[i * 4]); n += 1; }
            }));
    } else if (codec == "bc1" || codec == "bc3") {
        // level 10 of rgbcx's 0..18: its documented good default; no 3-colour
        // blocks (their black is transparent on some decoders)
        const bool bc1 = codec == "bc1";
        fmt = bc1 ? omk::kVkBc1RgbaUnorm : omk::kVkBc3Unorm;
        rgbcx::init();
        for (const Rgba& a : chain)
            levels.push_back(blocks4(a, bc1 ? 8 : 16, [&](const std::uint8_t* blk, std::uint8_t* o) {
                if (bc1) rgbcx::encode_bc1(10, o, blk, false, false);
                else rgbcx::encode_bc3(10, o, blk);
                if (&a != &chain[0]) return;
                std::uint8_t d[64];
                if (bc1) rgbcx::unpack_bc1(o, d, true);
                else rgbcx::unpack_bc3(o, d);
                for (int i = 0; i < 16; ++i)
                    for (int k = 0; k < 4; ++k) { se += (d[i * 4 + k] - blk[i * 4 + k]) * (d[i * 4 + k] - blk[i * 4 + k]); n += 1; }
            }));
    } else if (codec == "astc" && argc >= 6) {
        const std::string enc = argv[4], block = argv[5];
        int bw = 0, bh = 0;
        if (std::sscanf(block.c_str(), "%dx%d", &bw, &bh) != 2) return 2;
        fmt = bw == 4 && bh == 4 ? omk::kVkAstc4x4Unorm : bw == 5 && bh == 5 ? omk::kVkAstc5x5Unorm
            : bw == 6 && bh == 6 ? omk::kVkAstc6x6Unorm : bw == 8 && bh == 8 ? omk::kVkAstc8x8Unorm : 0;
        if (!fmt) { std::fprintf(stderr, "astc: block %s is not 4x4, 5x5, 6x6 or 8x8\n", block.c_str()); return 2; }
        const std::string tmp = out + ".lvl";
        for (std::size_t l = 0; l < chain.size(); ++l) {
            const std::string tga = tmp + ".tga", astc = tmp + ".astc";
            if (!writeTga(tga, chain[l])) return 1;
            // -cl: LDR, linear (the engine's textures are UNORM); quiet; one
            // thread - the pipeline runs several of these at once
            const std::string cmd = "'" + enc + "' -cl '" + tga + "' '" + astc + "' " + block + " -medium -silent -j 1 > /dev/null 2>&1";
            if (std::system(cmd.c_str()) != 0) { std::fprintf(stderr, "%s: astcenc failed on level %zu\n", in.c_str(), l); return 1; }
            const auto a = readAll(astc);
            // the .astc header: magic, block size, the image size in 24-bit fields
            if (a.size() < 16 || a[0] != 0x13 || a[1] != 0xAB || a[2] != 0xA1 || a[3] != 0x5C) return 1;
            levels.emplace_back(a.begin() + 16, a.end());
            if (l == 0) {
                // the round trip, by astcenc's own decoder
                const std::string back = tmp + ".back.png";
                const std::string dcmd = "'" + enc + "' -dl '" + astc + "' '" + back + "' -silent > /dev/null 2>&1";
                omk::PngImage d;
                if (std::system(dcmd.c_str()) == 0) {
                    const auto t = readAll(back);
                    const Rgba& a0 = chain[0];
                    if (!t.empty() && omk::decodePng(t.data(), t.size(), d) && d.w == a0.w && d.h == a0.h)
                        for (std::size_t q = 0; q < d.rgba.size(); ++q) {
                            const int e = d.rgba[q] - a0.px[q];
                            se += e * e;
                            n += 1;
                        }
                }
                std::remove(back.c_str());
            }
            std::remove(tga.c_str());
            std::remove(astc.c_str());
        }
    } else {
        std::fprintf(stderr, "unknown codec %s\n", codec.c_str());
        return 2;
    }
    const auto file = omk::writeKtx2(fmt, chain[0].w, chain[0].h, levels);
    // round-tripped through the reader before it is written: a file the
    // engine would refuse is never left in the pack
    omk::Ktx2 check;
    if (!omk::readKtx2(file, check, &why)) { std::fprintf(stderr, "%s: the KTX2 made does not read back: %s\n", out.c_str(), why.c_str()); return 1; }
    const std::string part = out + ".part";
    std::FILE* f = std::fopen(part.c_str(), "wb");
    if (!f) return 1;
    std::fwrite(file.data(), 1, file.size(), f);
    std::fclose(f);
    std::rename(part.c_str(), out.c_str());
    std::printf("@tex_encode %s %dx%d levels %zu psnr %.2f\n", codec.c_str(), chain[0].w, chain[0].h, levels.size(),
                psnr(se, n));
    return 0;
}
