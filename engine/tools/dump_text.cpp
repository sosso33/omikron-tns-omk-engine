// SPDX-License-Identifier: GPL-3.0-or-later
// Laying out interface text - the differential against tools/uitext.py.
//
//     dump_text <tables/ui.json> <gamedata/FONTS> <out.bin>
//
// Widths in pixels are the thing to compare: an advance is the glyph's own
// width or the face's default, plus the face's kerning, and every one of those
// three comes from a different place. A layout that got any of them wrong
// still renders something, and only the measured width says which.
#include "platform/datafs.h"
#include "ui/text.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: dump_text <ui.json> <gamedata/FONTS> <out.bin>\n");
        return 2;
    }
    const auto table = omk::FontTable::loadJson(argv[1]);
    const omk::TextLayout lay(table, argv[2]);

    struct Case { const char* text; char face; };
    const Case cases[] = {
        {"Nouvelle partie", 'J'},
        {"Charger une partie", 'J'},
        {"{fS}Options", 'J'},                       // a face change mid-string
        {"{fCI255120045}rouge", 'J'},               // chained face + ink
        {"{C}centre", 'J'},
        {"Kay'l", 'D'},
        {"[compte]", 'J'},                          // counted spans, no styling
        {"{X001002}decale", 'J'},                   // a move layout skips
    };

    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    put32(static_cast<std::int32_t>(table.size()));
    put32(static_cast<std::int32_t>(sizeof cases / sizeof cases[0]));
    for (const auto& c : cases) {
        const auto p = omk::parseMarkup(c.text, c.face);
        put32(static_cast<std::int32_t>(p.run.size()));
        put32(lay.measure(p.run));
        put32(lay.height(p.run));
        put32(p.align);
        // and the ink of the last character, which is what `{I}` sets
        const auto& last = p.run.empty() ? omk::StyledChar{} : p.run.back();
        put32(last.rgb[0]); put32(last.rgb[1]); put32(last.rgb[2]);
        put32(last.face);
        std::printf("%-26s %2zu chars, %4d px, %2d tall, align %2d, "
                    "ink %3d/%3d/%3d, face %c\n",
                    c.text, p.run.size(), lay.measure(p.run), lay.height(p.run),
                    p.align, last.rgb[0], last.rgb[1], last.rgb[2],
                    last.face ? last.face : '-');
    }
    if (!omk::safeOutputPath(argv[3])) return 2;
    std::ofstream f(argv[3], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));
    return 0;
}
