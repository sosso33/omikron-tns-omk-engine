// SPDX-License-Identifier: GPL-3.0-or-later
// WHAT COLOUR THE SNEAK'S PAGE PAINTS ITSELF - the answer to the question
// `Ui_DrawItemFill` left open.
//
//     sneak_colour <gamedata> <tables/ui_widgets.json> <tables/ui.json>
//
// 209 of the widget tree's 222 fill items ship `+8/+9/+10` = (255, 0, 0), so
// the record is a placeholder and the colour is written at run time. It is
// written by `sub_4296D0` (0x004296D0), a three-byte setter run over every
// item of a list, and what each sneak page hands it is ITS OWN TAB ICON's
// colour - the icons are list 0x004DE210 and carry the five page colours.
//
// This prints the icon, the colour the builder wrote onto the row list, and
// the pixel the composer then paints, so `verify.py: sneak page colour` can
// assert all three without a window.
#include "actor/moves.h"        // kScreenSneak - the screen table row
#include "platform/datafs.h"
#include "ui/cursor.h"
#include "ui/screendraw.h"
#include "ui/text.h"
#include "ui/widgets.h"

#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: sneak_colour <gamedata> <ui_widgets.json>"
                             " <ui.json>\n");
        return 2;
    }
    const omk::DataFs fs(argv[1]);
    auto w = omk::UiWidgets::loadJson(argv[2]);
    if (!w.valid()) { std::fprintf(stderr, "cannot load %s\n", argv[2]); return 1; }
    w.loadScreens(argv[3]);

    // The five tab icons, in the order they stand in the column. Their
    // records ARE the page colours, which is the finding.
    const omk::UiPanel* p = w.screen(omk::kScreenSneak);
    if (!p) { std::fprintf(stderr, "no panel for screen 9\n"); return 1; }
    for (const auto& l : p->lists)
        for (const auto& it : l.items)
            if (it.addr >= 0x004DDFB0u && it.addr <= 0x004DE0D0u &&
                (it.addr - 0x004DDFB0u) % 0x48u == 0)
                std::printf("icon %#x at (%3d,%3d) rgb %3d %3d %3d\n",
                            it.addr, it.x, it.y, it.rgb[0], it.rgb[1], it.rgb[2]);

    omk::UiWalk walk(w);
    walk.open(omk::kScreenSneak);
    for (std::uint32_t list : {omk::kListSneakRows, omk::kListSneakVerbs,
                               omk::kListSneakEcho}) {
        for (const auto& l : p->lists) {
            if (l.addr != list || l.items.empty()) continue;
            const int* c = walk.itemColour(l.items.front().addr);
            std::printf("list %#x written %d %d %d over %zu items\n", list,
                        c ? c[0] : -1, c ? c[1] : -1, c ? c[2] : -1,
                        l.items.size());
        }
    }

    // ...THEN WALK TO ANOTHER PAGE, which is where this went wrong. The tab
    // column's items carry a `child` panel, so confirming one DESCENDS - the
    // walk changes panel without `open()` being called again. A builder run
    // only from `open()` therefore leaves the new page wearing the old page's
    // colour, which is what a player saw: "I was in the slider menu, but
    // everything was in amber".
    //
    // RIGHT leaves the rows for the tab column, UP steps from the inventory
    // tab to the slider tab, CONFIRM enters it. (`sneak chain` walks RIGHT,
    // UP, UP, CONFIRM to the identity page the same way.)
    for (std::uint32_t bits : {omk::kUiRight, omk::kUiUp, omk::kUiConfirm})
        walk.press(bits);
    if (const omk::UiPanel* now = walk.panel()) {
        std::printf("walked to panel %#x%s\n", now->addr,
                    walk.approximate() ? " (approximate)" : "");
        // The slider page's header is TWO items at one coordinate; its
        // builder shows exactly one. Drawing both is what a player saw as
        // overlapping text.
        std::printf("slider header off %d %d shown %d\n",
                    walk.itemOff(0x004DE968u) ? 1 : 0,
                    walk.itemOff(0x004DE9B0u) ? 1 : 0,
                    walk.itemOff(0x004DE920u) ? 0 : 1);
        for (const auto& l : now->lists) {
            if (l.addr != omk::kListSneakRows || l.items.empty()) continue;
            const int* c = walk.itemColour(l.items.front().addr);
            std::printf("page rows now %d %d %d\n", c ? c[0] : -1,
                        c ? c[1] : -1, c ? c[2] : -1);
        }
    }
    walk.open(omk::kScreenSneak);          // back to the inventory page

    // ...and the pixel. The fill is `src * (1 - 200/255)` over whatever is
    // under it, so the row bar over the page's dark window is the number a
    // capture of the original can be held against.
    const auto table = omk::FontTable::loadJson(argv[3]);
    const omk::TextLayout lay(table, std::string(argv[1]) + "/FONTS");
    omk::ScreenComposer comp(fs, w, lay);
    comp.setDisplay(640, 480);
    omk::Surface fb(640, 480, 0);
    comp.draw(fb, omk::kScreenSneak, walk);
    const omk::UiItem* row = nullptr;
    for (const auto& l : p->lists)
        if (l.addr == omk::kListSneakRows && !l.items.empty())
            row = &l.items.front();
    if (row) {
        const std::uint16_t v =
            fb.px[static_cast<std::size_t>(row->y + row->h / 2) * 640 +
                  (row->x + row->w - 20)];
        std::printf("row bar paints %d %d %d\n", ((v >> 11) & 31) * 255 / 31,
                    ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31);

        // ...and again with the HIGHLIGHT attached. `Ui_DrawItemCursor` puts
        // sixteen quads over the focused row at oscillator 3's alpha, and
        // because the blend is the fill's inverse one a HIGH alpha is a FAINT
        // quad - sixteen of them at ~0.09 each leave the bar near 80% of the
        // source. So the number to assert is that the focused row gets
        // brighter by a factor a person can see, which is what a player said
        // was missing: "the hovering effect is absent".
        omk::UiCursor cur;
        comp.attachCursor(&cur);
        comp.setDeltaMs(33);
        omk::Surface lit(640, 480, 0);
        omk::UiWalk w2(w);
        w2.open(omk::kScreenSneak);
        omk::ScreenFrame f;
        for (int i = 0; i < 60; ++i) {          // let the eases settle
            lit = omk::Surface(640, 480, 0);
            f = comp.draw(lit, omk::kScreenSneak, w2);
        }
        // Sampled at the bar's flat RIGHT-HAND END, past the label - the
        // same place the original's screenshots were measured, and now that
        // the cursor draws OVER the text (layer 8 against 6) a mid-bar
        // sample lands on a glyph.
        const std::uint16_t u =
            lit.px[static_cast<std::size_t>(row->y + row->h / 2) * 640 +
                   (row->x + row->w - 20)];
        std::printf("cursor quads %d, alpha %d, focused row paints %d %d %d\n",
                    f.cursorQuads, cur.alpha(), ((u >> 11) & 31) * 255 / 31,
                    ((u >> 5) & 63) * 255 / 63, (u & 31) * 255 / 31);
    }
    return 0;
}
