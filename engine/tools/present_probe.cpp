// SPDX-License-Identifier: GPL-3.0-or-later
// THE PRESENT PASS GIVES THE READBACK'S BYTES - every colour, every dither cell.
//
//     present_probe
//
// `present.frag` (backends/vulkan, todo/optimization.md step 4b) replaces, on a
// frame nothing is drawn over, the CPU's readback + `quantise888DitherRow` +
// placement at the letterbox row + `expand565Rgba`. This holds it to those
// bytes over the whole domain rather than over whatever a scene happens to
// contain: a 4096 x 4102 picture holding each of the 2^24 colours once is
// loaded into the colour attachment, the pass runs with the picture placed 3
// rows down and 4096 rows tall (so the first and last 3 rows are the black
// bands - and 3, because an offset that is a multiple of 4 cannot tell the
// picture's dither row from the window's), and every output byte is compared
// with the CPU's. Sixteen runs shift
// the colours by 0..3 columns and 0..3 rows, so every colour meets every one of
// the 16 matrix cells; a seventeenth runs with the dither OFF against
// `rgb565`'s truncation.
//
// One line a run with its mismatching pixels, then `mismatches <total>`.
// Needs a Vulkan device; prints `skipped` without one. Writes nothing.
#include "o3de/renderer.h"
#include "ui/surface.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace omk {
Renderer* makeVulkanRenderer();
const char* vulkanDeviceName(Renderer*);
bool vulkanProbeUpload(Renderer*, const unsigned char* rgba, bool dither);
bool vulkanWorldPicture(Renderer*, int vy, int vh, std::vector<unsigned char>& rgba);
}  // namespace omk

int main() {
    // 3 rows down, NOT a multiple of 4: the dither's cell is the PICTURE's row,
    // and with an offset of 4 (or the street's 0, or the letterbox's 64) the
    // picture row and the window row fall in the same cell, so a shader using
    // the wrong one would pass.
    constexpr int kW = 4096, kRows = 4096, kVy = 3, kH = kRows + 2 * kVy;
    omk::Renderer* r = omk::makeVulkanRenderer();
    if (!r || !r->init(kW, kH)) {
        std::printf("skipped - no Vulkan device\n");
        return 0;
    }
    std::printf("device %s, %dx%d, picture %d rows at row %d\n", omk::vulkanDeviceName(r), kW, kH, kRows, kVy);
    std::vector<unsigned char> src(static_cast<std::size_t>(kW) * kH * 4, 0), out;
    std::vector<std::uint16_t> row(kW);
    const unsigned char* lut = omk::expand565Rgba();
    long total = 0;
    const auto run = [&](int k, int j, bool dither) {
        using clk = std::chrono::steady_clock;
        const auto t0 = clk::now();
        // colour index (y*4096 + x + k + 4096*j) mod 2^24 at picture pixel (x, y)
        for (int y = 0; y < kRows; ++y)
            for (int x = 0; x < kW; ++x) {
                const std::uint32_t c = (static_cast<std::uint32_t>(y) * 4096u + static_cast<std::uint32_t>(x) +
                                         static_cast<std::uint32_t>(k) + 4096u * static_cast<std::uint32_t>(j)) & 0xFFFFFFu;
                unsigned char* p = &src[4 * (static_cast<std::size_t>(y) * kW + static_cast<std::size_t>(x))];
                p[0] = static_cast<unsigned char>(c >> 16);
                p[1] = static_cast<unsigned char>(c >> 8);
                p[2] = static_cast<unsigned char>(c);
                p[3] = 255;
            }
        if (!omk::vulkanProbeUpload(r, src.data(), dither) ||
            !omk::vulkanWorldPicture(r, kVy, kRows, out) || out.size() != src.size()) {
            std::printf("run k %d j %d dither %d: the pass did not run\n", k, j, dither ? 1 : 0);
            ++total;
            return;
        }
        long bad = 0, band = 0;
        for (int Y = 0; Y < kH; ++Y) {
            const int wy = Y - kVy;
            const bool inPicture = wy >= 0 && wy < kRows;
            if (inPicture) {
                const unsigned char* s = &src[4 * static_cast<std::size_t>(wy) * kW];
                if (dither) {
                    omk::quantise888DitherRow(s, kW, wy, row.data());
                } else {
                    for (int x = 0; x < kW; ++x) row[static_cast<std::size_t>(x)] = omk::rgb565(s[4 * x], s[4 * x + 1], s[4 * x + 2]);
                }
            }
            for (int x = 0; x < kW; ++x) {
                const unsigned char* want = inPicture ? lut + 4 * static_cast<std::size_t>(row[static_cast<std::size_t>(x)])
                                                      : lut;   // entry 0: black, alpha 255
                if (std::memcmp(want, &out[4 * (static_cast<std::size_t>(Y) * kW + static_cast<std::size_t>(x))], 4) != 0) {
                    ++bad;
                    if (!inPicture) ++band;
                }
            }
        }
        const double ms = std::chrono::duration<double, std::milli>(clk::now() - t0).count();
        std::printf("run k %d j %d dither %d: %ld pixels mismatched (%ld in the bands) in %.0f ms\n",
                    k, j, dither ? 1 : 0, bad, band, ms);
        total += bad;
    };
    for (int j = 0; j < 4; ++j)
        for (int k = 0; k < 4; ++k) run(k, j, true);
    run(0, 0, false);
    std::printf("mismatches %ld\n", total);
    delete r;
    return total == 0 ? 0 : 3;
}
