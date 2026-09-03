// SPDX-License-Identifier: GPL-3.0-or-later
// Every FONTS/*.FNT, and the checks the layout could fail.
//
//     dump_fonts <gamedata/FONTS> <out.bin>
//
// The offsets are in EIGHT-BYTE units. Read as bytes, glyph blocks land in the
// wrong place and most run past EOF - so "0 glyphs outside the file, 0
// overlapping" is what says the unit is right.
#include "formats/fnt.h"
#include "platform/datafs.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: dump_fonts <gamedata/FONTS> <out.bin>\n");
        return 2;
    }
    omk::DataFs dir(argv[1]);
    int files = 0, startsAt2048 = 0, sameCoverage = 0;
    long glyphs = 0, outside = 0, overlapping = 0, pixels = 0, overRamp = 0;
    long unaccounted = 0, gaps = 0;
    for (const auto& p : dir.list(".", "fnt")) {
        const auto d = omk::DataFs::readPath(p);
        const auto f = omk::readFnt(d);
        if (!f.valid) continue;
        ++files;

        std::vector<std::pair<std::size_t, std::size_t>> blocks;
        std::size_t lowest = d.size();
        int covered = 0;
        for (std::size_t c = 0; c < omk::kFntGlyphs; ++c) {
            const auto& g = f.glyphs[c];
            if (!g.present) continue;
            ++glyphs;
            // every code 33..255 carries a glyph and space (32) does not -
            // it falls to the font record's default advance
            if (c >= 33 && c <= 255) ++covered;
            const auto end = g.offset + g.pixels();
            if (g.offset < omk::kFntHeader || end > d.size()) { ++outside; continue; }
            lowest = std::min(lowest, g.offset);
            pixels += static_cast<long>(g.pixels());
            for (std::size_t k = g.offset; k < end; ++k)
                if (static_cast<std::uint8_t>(d[k]) >= omk::kFntRamp) ++overRamp;
            blocks.push_back({g.offset, end});
        }
        if (covered == 223) ++sameCoverage;
        if (lowest == omk::kFntHeader) ++startsAt2048;

        // Two different quantities, and conflating them hides one: the gaps
        // BETWEEN glyph blocks (per-glyph alignment padding) and the tail
        // after the last one. The reference reports only the tail.
        std::sort(blocks.begin(), blocks.end());
        std::size_t reach = omk::kFntHeader;
        for (const auto& [a, b] : blocks) {
            if (a < reach) ++overlapping;
            if (a > reach) gaps += static_cast<long>(a - reach);
            reach = std::max(reach, b);
        }
        unaccounted += static_cast<long>(d.size() - reach);
    }
    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    put32(files); put32(startsAt2048); put32(sameCoverage);
    for (long v : {glyphs, outside, overlapping, pixels, overRamp,
                   unaccounted, gaps})
        put32(static_cast<std::int32_t>(v));
    if (!omk::safeOutputPath(argv[2])) return 2;
    std::ofstream f(argv[2], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));
    std::printf("%d fonts, %d whose pixels start at 2048, %d covering 33..255; "
                "%ld glyphs, %ld outside the file, %ld overlapping, "
                "%ld pixel bytes, %ld past the 32-entry ramp, %ld trailing "
                "bytes, %ld in gaps between glyphs\n",
                files, startsAt2048, sameCoverage, glyphs, outside,
                overlapping, pixels, overRamp, unaccounted, gaps);
    return 0;
}
