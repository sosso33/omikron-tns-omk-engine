// SPDX-License-Identifier: GPL-3.0-or-later
// COMPOSE A SCREEN, HEADLESS - the same frame the window shows, with no window.
//
//     run_screen <gamedata> <ui_widgets.json> <ui.json> <out.bin> [frame] [WxH] [uiscaling]
//                [textscaling] [again]
//
// `again` composes the LIFT (4) and the SNEAK (9) as 4, 9, 4, 9 on ONE
// composer, and each once on a fresh one, and prints the six hashes and
// nothing else: a screen's scaled background is kept between frames
// (`ScreenComposer::background`), and a stale one shows only on the screen
// AFTER another (`verify.py: engine: screen background kept`).
//
// This is the reference half of `PORTING` A1's pair: the frontend is handed an
// already-composed framebuffer, so everything that decides what a frame LOOKS
// like can be checked without a device, and `make` needs nothing installed.
// The SDL player calls exactly this composer and then uploads the result.
#include "platform/datafs.h"
#include "formats/le.h"
#include "platform/frontend.h"
#include <cstdlib>

#include "ui/cloud.h"
#include "ui/screendraw.h"
#include "ui/text.h"
#include "ui/widgets.h"
#include "platform/env.h"

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
    omk::TextLayout lay(table, std::string(argv[1]) + "/FONTS");
    omk::ScreenComposer comp(fs, w, lay);
    // The menu's animated background, on the REFERENCE side too. Without it
    // the composer draws a colour-keyed sheet over nothing and screen 29 comes
    // back transparent - and worse, the reference and the live window would
    // then differ, which is the one thing `engine: screen` exists to deny.
    // Frame 0, so the composition stays deterministic.
    //
    // ...AND `OMK_NOCLOUD=1` LEAVES IT OFF, which two checks need. With the
    // tile background colour-keyed the way `Ui_DrawPanelBack` keys it, the
    // artwork's transparent cells show whatever is beneath - the cloud here,
    // the world in the game - so every pixel of the frame is non-zero and
    // `painted` stops measuring the ARTWORK's coverage, which is the whole
    // quantity `engine: screen scale` is built on. Without the cloud it
    // measures it again.
    omk::MenuCloud cloud;
    if (!omk::envFlag("OMK_NOCLOUD") && cloud.load(fs)) comp.attachCloud(&cloud);
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
    comp.setReportHash(true);   // the frame's hash and painted count, off in play
    // ...and the INTERFACE SCALING enhancement (`todo/enhancements.md` 3),
    // 0 nearest as the original stretches. It can only matter where the
    // display is not 640x480, which is the pair the check composes.
    if (argc > 7) comp.setScaling(std::atoi(argv[7]));
    // ...and TEXT SCALING (`todo/enhancements.md`): 1 = fit, the glyphs at the
    // smaller of the layout's two scales - the same rule as omk-play's.
    if (argc > 8 && std::atoi(argv[8]) > 0) {
        const int filt = argc > 7 ? std::atoi(argv[7]) : 0;
        if (dw * 480 <= dh * 640) lay.setGlyphScale(dw, 640, filt);
        else                      lay.setGlyphScale(dh, 480, filt);
    }

    if (argc > 9 && std::string(argv[9]) == "again") {
        const auto hashOf = [&](omk::ScreenComposer& c, int sid) {
            omk::UiWalk walk(w);
            walk.open(sid);
            omk::Surface fb(dw, dh, 0);
            return c.draw(fb, sid, walk).hash;
        };
        omk::ScreenComposer fresh4(fs, w, lay), fresh9(fs, w, lay);
        for (omk::ScreenComposer* c : {&fresh4, &fresh9}) {
            c->setDisplay(dw, dh);
            c->setReportHash(true);
            if (argc > 7) c->setScaling(std::atoi(argv[7]));
        }
        std::printf("again: shared 4 %08X\n", hashOf(comp, 4));
        std::printf("again: shared 9 %08X\n", hashOf(comp, 9));
        std::printf("again: shared 4 %08X\n", hashOf(comp, 4));
        std::printf("again: shared 9 %08X\n", hashOf(comp, 9));
        std::printf("again: fresh 4 %08X\n", hashOf(fresh4, 4));
        std::printf("again: fresh 9 %08X\n", hashOf(fresh9, 9));
        return 0;
    }

    std::vector<std::int32_t> out;
    // Screen 29 is the start menu; 4 is the LIFT, whose seven slots are the
    // one panel whose coordinates an independent hand reading already fixed.
    for (int sid : {29, 4}) {
        omk::UiWalk walk(w);
        walk.open(sid);
        omk::Surface fb(dw, dh, 0);
        const auto f = comp.draw(fb, sid, walk);
        // `spritesDrawn` joins the row because the SPRITE path is what
        // `Ui_DrawItemSprite` does. Without it the LIFT's seven slots read
        // as 0 rows, which looks like a loss and is right: they are ICONS,
        // `+24`/`+32` both zero, and no text is theirs.
        out.insert(out.end(), {sid, f.tilesDrawn, f.fullSheet ? 1 : 0,
                               f.itemsDrawn, f.textAdvance, f.centred,
                               static_cast<std::int32_t>(f.hash),
                               static_cast<std::int32_t>(f.painted),
                               f.spritesDrawn});
        std::printf("screen %2d: %s background (%d tiles), %d rows drawn, "
                    "%d px advance, %d centred, %ld of %d painted, hash %08X\n",
                    sid, f.fullSheet ? "full-sheet" : "tile-map", f.tilesDrawn,
                    f.itemsDrawn, f.textAdvance, f.centred, f.painted, dw * dh,
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
    omk::writeLE(o, &n, 1);
    omk::writeLE(o, out.data(), out.size());

    // ...then screen 29's framebuffer, so the LIVE frontend's dump can be
    // diffed against it. That comparison is what `PORTING` A8 rule 3 is
    // about: the frontend uploads these pixels and must not touch them, so
    // the two must be identical to the byte.
    for (auto v : fb.px) {
        const char b[2] = {static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)};
        o.write(b, 2);
    }

    // ...and then the LIFT's, appended, because screen 4 is the one screen
    // with a FILL whose record colour is not the (255, 0, 0) placeholder 209
    // of the tree's 222 fills carry. Its description panel is the only place
    // the fill rule can be checked against a picture of the original, so the
    // pixels have to be reachable. Appended rather than substituted: the menu
    // frame above is what `menu layout` and `engine: screen` read.
    omk::Surface lift(dw, dh, 0);
    omk::UiWalk lw(w);
    lw.open(4);
    comp.draw(lift, 4, lw);
    for (auto v : lift.px) {
        const char b[2] = {static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)};
        o.write(b, 2);
    }
    return 0;
}
