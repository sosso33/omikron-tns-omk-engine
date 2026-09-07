// SPDX-License-Identifier: GPL-3.0-or-later
// `Text_LayOutBlock` (0x0043F3E0) OVER THE SHIPPED STRINGS.
//
//     text_block <gamedata> <tables>
//
// The engine lays a block of text out in one pass - markup, wrap, alignment,
// vertical placement - and returns the height, which is the same number
// `Ui_ItemTextStyle` bounds a scrolling box against. This runs that pass over
// the object descriptions the sneak's examine page shows, in the page's own
// 400x260 box at (150, 100), and reports what a person can check by eye
// against a screenshot.
//
// One line per fact:
//   corpus N        object descriptions with any text, and how many WRAP
//   mk400 ...       the MK400 notice: lines, height, the widest line
//   advance ...     the line pitch, which is 120% of the font's own height
//   overflow ...    the longest description's height against the box
//   centre ...      a `{C}` line's x against the box, and a plain one's
//   height0 ...     the empty string, which returns the box's TOP
#include "platform/datafs.h"
#include "script/objects.h"
#include "ui/text.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: text_block <gamedata> <tables>\n");
        return 2;
    }
    const omk::DataFs fs(argv[1]);
    const auto fonts = omk::FontTable::loadJson(std::string(argv[2]) + "/ui.json");
    if (fonts.size() != 13) { std::fprintf(stderr, "no font table\n"); return 1; }
    omk::TextLayout lay(fonts, std::string(argv[1]) + "/FONTS");
    const auto objects = omk::loadObjects(fs);

    // The examine page's own box - item 0x004DE710, 400x260 at (150, 100) -
    // and its font is the item's `+36`, 'J'.
    omk::TextBlock box;
    box.left = 150; box.top = 100; box.right = 550; box.bottom = 360;
    box.font = 'J';
    box.measureOnly = true;

    int withText = 0, wrapped = 0, tallest = 0;
    std::string tallestName;
    for (const auto& o : objects) {
        if (o.description.empty()) continue;
        ++withText;
        const int h = lay.layOutBlock(nullptr, o.description, box);
        if (h > 260) ++wrapped;
        if (h > tallest) { tallest = h; tallestName = o.name; }
    }
    std::printf("corpus %d taller %d\n", withText, wrapped);

    const omk::ObjectRecord* mk = nullptr;
    for (const auto& o : objects)
        if (o.name.rfind("Notice MK400", 0) == 0) mk = &o;
    if (!mk) { std::fprintf(stderr, "no MK400 record\n"); return 1; }

    // Drawn into a real surface, so the LINES are counted the way a player
    // sees them rather than from the wrap alone.
    omk::Surface fb(640, 480);
    omk::TextBlock draw = box;
    draw.measureOnly = false;
    const int h = lay.layOutBlock(&fb, mk->description, draw);
    // How many rows of the box carry ink, grouped into runs - one run a line.
    int lines = 0, widest = 0;
    bool inRow = false;
    for (int y = 0; y < 480; ++y) {
        bool ink = false; int lo = 640, hi = -1;
        for (int x = 0; x < 640; ++x)
            if (fb.px[static_cast<std::size_t>(y) * 640 + x]) {
                ink = true; if (x < lo) lo = x; if (x > hi) hi = x;
            }
        if (ink && !inRow) ++lines;
        if (ink && hi - lo > widest) widest = hi - lo;
        inRow = ink;
    }
    std::printf("mk400 height %d lines %d widest %d\n", h, lines, widest);

    // THE PITCH. `v6 += 120 * i16i(font, 6) / 100` - 120% of the record's
    // `+12`, and nothing else decides it.
    const auto* fj = fonts.byLetter('J');
    std::printf("advance %d of height %d\n", 120 * fj->height / 100, fj->height);

    std::printf("overflow %d of %d for '%s'\n", tallest, 260, tallestName.c_str());

    // ALIGNMENT: `style & 0x1E` case 8 centres between left and right, and the
    // default leaves the pen where it is.
    omk::Surface a(640, 480), b2(640, 480);
    omk::TextBlock plain = draw;
    lay.layOutBlock(&a, "Khonsu", plain);
    omk::TextBlock centred = draw;
    centred.style = 8;
    lay.layOutBlock(&b2, "Khonsu", centred);
    const auto firstInk = [](const omk::Surface& s) {
        for (int x = 0; x < 640; ++x)
            for (int y = 0; y < 480; ++y)
                if (s.px[static_cast<std::size_t>(y) * 640 + x]) return x;
        return -1;
    };
    std::printf("centre %d plain %d\n", firstInk(b2), firstInk(a));

    // `{P}` IS NOT A DIRECTIVE AND DOES NOTHING. `P` is not a case in the
    // switch, so it reaches the flush test - and so does its closing brace -
    // but neither changes a style bit, so `(style ^ working) & 0x1C1E` is 0
    // and no line breaks. Every shipped `{P}` sits beside a `\r\n\r\n` that
    // does the paragraph break for real, which is why nobody could see it.
    // An ALIGNMENT directive does break the line, `{C}` at its closing brace
    // and `{F}` at its own letter, because both reach the same test with the
    // bit changed.
    int lp = 0, lq = 0, lc = 0;
    const std::string plainTwo = "Alpha beta\r\n\r\nGamma delta";
    const std::string withP    = "Alpha beta {P}{P}\r\n\r\nGamma delta";
    const std::string withC    = "Alpha beta{C}Gamma delta";
    const std::string noBrace   = "Alpha betaGamma delta";
    const int hp = lay.layOutBlock(nullptr, plainTwo, box);
    omk::BlockResult r1, r2, r3, r4;
    lay.layOutBlock(nullptr, plainTwo, box, &r1);
    lay.layOutBlock(nullptr, withP,    box, &r2);
    lay.layOutBlock(nullptr, withC,    box, &r3);
    lay.layOutBlock(nullptr, noBrace,  box, &r4);
    lp = r1.lines; lq = r2.lines; lc = r3.lines;
    // P: the same line count as the string without it. C: two lines where the
    // same text with no brace at all is one.
    std::printf("brace P %d plain %d C %d bare %d height %d\n",
                lq, lp, lc, r4.lines, hp);

    // ...and how many shipped strings use `{P}` at all.
    int pSites = 0, pNextToCrlf = 0;
    for (const auto& o : objects) {
        for (std::size_t k = 0; k + 2 < o.description.size(); ++k)
            if (o.description.compare(k, 3, "{P}") == 0) {
                ++pSites;
                if (o.description.find("\r\n\r\n") != std::string::npos)
                    ++pNextToCrlf;
            }
    }
    std::printf("psites %d beside_crlf %d\n", pSites, pNextToCrlf);

    // `if (!*a1) return dword_907A18` - the empty string reports the box's
    // TOP, where every other path reports a height. Kept because its one
    // consumer subtracts the box height and floors at 0.
    std::printf("height0 %d\n", lay.layOutBlock(nullptr, "", box));
    return 0;
}
