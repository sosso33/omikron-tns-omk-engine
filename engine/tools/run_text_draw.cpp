// SPDX-License-Identifier: GPL-3.0-or-later
// `Text_DrawRun` ported: the start menu's four labels, rasterised.
//
//     run_text_draw <gamedata> <tables/ui.json> <out.bin>
//
// The layout half was already here - `parseMarkup`, the font table, the
// advance. This is the half that was not: a glyph byte is a COVERAGE level
// 0..31 into a 32-entry ramp of the text colour, rebuilt when that colour
// changes, with zero transparent.
//
// **The oracle is the engine's own framebuffer.** The glyph pixels of the
// start menu were measured identical across three captures of an animated
// screen - mask AND values - so they are deterministic and exactly
// reproducible, which is what makes this tier 4 rather than a differential
// against `uitext.py`.
//
// The colours are read out of those frames rather than assumed: the focused
// row is white and the other three are `0x7F7F7F`, which through the ramp
// gives 565 `0x7BEF` and displays as (123,125,123). That the two differ only
// in the colour handed in - identical glyphs - is what makes the focus
// highlight a brightness and not a second face.
//
// Emits the rendered surface; `verify.py` compares it against the capture,
// because the frame is a PNG and decoding one here would need a dependency
// `PORTING` A8 forbids on this side.
#include "platform/datafs.h"
#include "ui/surface.h"
#include "ui/text.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: run_text_draw <gamedata> <tables/ui.json> <out.bin>\n");
        return 2;
    }
    const auto table = omk::FontTable::loadJson(argv[2]);
    const omk::TextLayout lay(table, std::string(argv[1]) + "/FONTS");

    // MENUINTR is face `I`, the row the compiled font table names for the
    // start menu; the labels come out of IAM\Menu and are hard-coded here
    // because this tool is about the RASTERISER, not the text archives.
    struct Row { const char* text; int y; std::uint8_t v; };
    const Row rows[4] = {
        {"Nouvelle partie",    127, 255},   // focused
        {"Charger une partie", 208, 0x7F},
        {"Options",            290, 0x7F},
        {"Quitter",            370, 0x7F},
    };

    const int W = 640, H = 480;
    omk::Surface fb(W, H, 0);
    long drawn = 0;
    for (const auto& r : rows) {
        auto run = omk::parseMarkup(r.text, 'I', r.v, r.v, r.v).run;
        // centred, as the menu draws them
        const int width = lay.measure(run);
        lay.drawRun(fb, (W - width) / 2, r.y, run);
        drawn += width;
    }
    long painted = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (fb.at(x, y)) ++painted;

    std::vector<std::uint8_t> head;
    const auto put = [&head](long v) {
        const auto u = static_cast<std::uint32_t>(static_cast<std::int32_t>(v));
        for (int k = 0; k < 4; ++k) head.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    put(W); put(H); put(painted); put(drawn);
    if (!omk::safeOutputPath(argv[3])) return 2;
    std::ofstream out(argv[3], std::ios::binary);
    out.write(reinterpret_cast<const char*>(head.data()),
              static_cast<std::streamsize>(head.size()));
    for (auto v : fb.px) {
        const char b[2] = {static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)};
        out.write(b, 2);
    }
    std::printf("text: %ld pixels painted, %ld total advance\n", painted, drawn);
    return 0;
}
