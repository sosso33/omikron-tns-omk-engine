// SPDX-License-Identifier: GPL-3.0-or-later
// COMPOSE A SCREEN, HEADLESS - the same frame the window shows, with no window.
//
//     run_screen <gamedata> <tables/ui_widgets.json> <tables/ui.json> <out.bin>
//
// This is the reference half of `PORTING` A1's pair: the frontend is handed an
// already-composed framebuffer, so everything that decides what a frame LOOKS
// like can be checked without a device, and `make` needs nothing installed.
// The SDL player calls exactly this composer and then uploads the result.
#include "platform/datafs.h"
#include "platform/frontend.h"
#include <cstdlib>

#include "ui/cloud.h"
#include "ui/screendraw.h"
#include "ui/text.h"
#include "ui/widgets.h"

#include <cstdio>
#include <fstream>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: run_screen <gamedata> <ui_widgets.json> "
                             "<ui.json> <out.bin>\n");
        return 2;
    }
    const omk::DataFs fs(argv[1]);
    auto w = omk::UiWidgets::loadJson(argv[2]);
    if (!w.valid()) { std::fprintf(stderr, "cannot load %s\n", argv[2]); return 1; }
    w.loadScreens(argv[3]);
    const auto table = omk::FontTable::loadJson(argv[3]);
    const omk::TextLayout lay(table, std::string(argv[1]) + "/FONTS");
    omk::ScreenComposer comp(fs, w, lay);
    // The menu's animated background, on the REFERENCE side too. Without it
    // the composer draws a colour-keyed sheet over nothing and screen 29 comes
    // back transparent - and worse, the reference and the live window would
    // then differ, which is the one thing `engine: screen` exists to deny.
    // Frame 0, so the composition stays deterministic.
    omk::MenuCloud cloud;
    if (cloud.load(fs)) comp.attachCloud(&cloud);
    // The background ANIMATES, so a frame-for-frame comparison against the
    // live window has to name the same frame. `engine: screen` passes the one
    // the player's loop ends on; without it the two differ by a phase and the
    // check reports a frontend fault that is not there.
    if (argc > 5) comp.setFrame(std::atol(argv[5]));
    // An optional display size. The interface is authored at 640x480 and
    // `I2D_ScaleX/Y` scale its coordinates to whatever the display is, so a
    // check can compose the same screen at two resolutions and assert the
    // relation between them - which is what keeps the port from quietly
    // depending on one mode.
    int dw = 640, dh = 480;
    if (argc > 6) std::sscanf(argv[6], "%dx%d", &dw, &dh);
    comp.setDisplay(dw, dh);

    std::vector<std::int32_t> out;
    // Screen 29 is the start menu; 4 is the LIFT, whose seven slots are the
    // one panel whose coordinates an independent hand reading already fixed.
    for (int sid : {29, 4}) {
        omk::UiWalk walk(w);
        walk.open(sid);
        omk::Surface fb(dw, dh, 0);
        const auto f = comp.draw(fb, sid, walk);
        out.insert(out.end(), {sid, f.tilesDrawn, f.fullSheet ? 1 : 0,
                               f.itemsDrawn, f.textAdvance, f.centred,
                               static_cast<std::int32_t>(f.hash),
                               static_cast<std::int32_t>(f.painted)});
        std::printf("screen %2d: %s background (%d tiles), %d rows drawn, "
                    "%d px advance, %d centred, %ld of %d painted, hash %08X\n",
                    sid, f.fullSheet ? "full-sheet" : "tile-map", f.tilesDrawn,
                    f.itemsDrawn, f.textAdvance, f.centred, f.painted, 640 * 480,
                    f.hash);
    }

    // and the null frontend, so A8 rule 1's property is exercised rather than
    // asserted: the whole composition runs with no device present.
    omk::NullFrontend null;
    null.open(640, 480, "headless");
    omk::Surface fb(dw, dh, 0);
    omk::UiWalk walk(w);
    walk.open(29);
    comp.draw(fb, 29, walk);
    null.present(fb);
    out.push_back(static_cast<std::int32_t>(null.frames()));
    out.push_back(static_cast<std::int32_t>(null.lastPixels()));

    if (!omk::safeOutputPath(argv[4])) return 2;
    std::ofstream o(argv[4], std::ios::binary);
    const std::int32_t n = static_cast<std::int32_t>(out.size());
    o.write(reinterpret_cast<const char*>(&n), 4);
    o.write(reinterpret_cast<const char*>(out.data()),
            static_cast<std::streamsize>(out.size() * 4));

    // ...then screen 29's framebuffer, so the LIVE frontend's dump can be
    // diffed against it. That comparison is what `PORTING` A8 rule 3 is
    // about: the frontend uploads these pixels and must not touch them, so
    // the two must be identical to the byte.
    for (auto v : fb.px) {
        const char b[2] = {static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)};
        o.write(b, 2);
    }
    return 0;
}
