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
#include "ui/models.h"
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
        // ...and now try to reach the slider page's ROWS, which is what
        // `sub_49D4D0` exists for. Report the list the walk lands on after
        // each press so a check can see the state machine step.
        const auto where = [&](const char* tag) {
            const omk::UiPanel* q = walk.panel();
            const int c = walk.currentList();
            std::printf("  %-10s panel %#x list %d (%#x)\n", tag, q ? q->addr : 0, c,
                        (q && c >= 0 && c < (int)q->lists.size()) ? q->lists[c].addr : 0);
        };
        where("entered");
        for (auto b : {omk::kUiLeft, omk::kUiUp, omk::kUiUp}) {
            walk.press(b);
            where(b == omk::kUiLeft ? "listAxis" : "crossAxis");
        }
        // ...and now WALK THE ROWS. Three live destinations bound into
        // nine widgets: entering from the header must land on a live row and
        // stepping must move between the three and stop, never leaving the
        // highlight on a widget that draws nothing.
        walk.bindRows(omk::kListSneakRows, 3);
        const auto rowSel = [&]() {
            for (const auto& l : now->lists)
                if (l.addr == omk::kListSneakRows) return walk.selectionOf(l);
            return -1;
        };
        std::printf("rows: enter %d", rowSel());
        for (int k = 0; k < 4; ++k) {
            walk.press(omk::kUiUp);
            std::printf(" up %d(l%d)", rowSel(), walk.currentList());
        }
        for (int k = 0; k < 5; ++k) {
            walk.press(omk::kUiDown);
            std::printf(" dn %d(l%d)", rowSel(), walk.currentList());
        }
        std::printf("\n");
    }
    // ---- THE INTERFACE REMEMBERS, because `list+2` is a STATIC record ---
    //
    // Seeded by the linker (all four of the sneak's ship 0), written by
    // `Ui_MoveSelection`, overwritten only where an open callback writes it,
    // and never reset - so the device remembers the verb you last used across
    // closing and reopening it. A walk built fresh each open forgets. Two
    // walks sharing one `UiListState` is the engine's data segment.
    {
        omk::UiListState st;
        omk::UiWalk a(w, st);
        a.open(omk::kScreenSneak);
        a.bindRows(omk::kListSneakRows, 1);
        a.press(omk::kUiConfirm);                 // into the verb panel
        a.press(omk::kUiRight);
        a.press(omk::kUiRight);                   // ...and along to Examiner
        int was = -1;
        for (const auto& l : a.panel()->lists)
            if (l.addr == omk::kListSneakVerbs) was = a.selectionOf(l);
        omk::UiWalk b(w, st);                     // the screen closes and reopens
        b.open(omk::kScreenSneak);
        b.bindRows(omk::kListSneakRows, 1);
        b.press(omk::kUiConfirm);
        int now2 = -1;
        for (const auto& l : b.panel()->lists)
            if (l.addr == omk::kListSneakVerbs) now2 = b.selectionOf(l);
        omk::UiWalk c(w);                         // ...and with no shared state
        c.open(omk::kScreenSneak);
        c.bindRows(omk::kListSneakRows, 1);
        c.press(omk::kUiConfirm);
        int fresh = -1;
        for (const auto& l : c.panel()->lists)
            if (l.addr == omk::kListSneakVerbs) fresh = c.selectionOf(l);
        std::printf("verb left at %d, remembered %d, private walk %d\n",
                    was, now2, fresh);
    }

    // ---- A SLIDER ROW IS NOT AN INVENTORY ROW -------------------------
    //
    // One callback, three arms, dispatched on `dword_670CB8`. Confirming a
    // destination must NOT descend into the verb panel.
    {
        omk::UiWalk sw2(w);
        sw2.open(omk::kScreenSneak);
        for (auto b : {omk::kUiRight, omk::kUiUp, omk::kUiConfirm})
            sw2.press(b);                       // into the slider page
        sw2.bindRows(omk::kListSneakRows, 3);
        sw2.press(omk::kUiLeft);                // its header
        sw2.press(omk::kUiUp);                  // ...and its rows
        sw2.press(omk::kUiConfirm);
        std::printf("slider row confirm lands on %#x\n",
                    sw2.panel() ? sw2.panel()->addr : 0);
    }

    // ---- THE OBJECT FLOW: rows -> verbs -> examine --------------------
    //
    // Both descents are `sub_42A370` from a CALLBACK, not an item `+44`, so
    // neither panel was in the tree until 2026-09-04. Confirming a row must
    // reach the verb panel with ONLY the verbs selectable, and confirming
    // "Examiner" must reach the examine page.
    {
        omk::UiWalk f(w);
        f.open(omk::kScreenSneak);
        f.bindRows(omk::kListSneakRows, 1);
        if (const int* c0 = f.itemColour(0x004DE440u))
            std::printf("on open, rows are %d %d %d (%zu coloured)\n", c0[0], c0[1], c0[2], f.colourCount());
        else
            std::printf("on open, rows have NO colour\n");
        f.press(omk::kUiConfirm);
        const omk::UiPanel* q = f.panel();
        int vsel = -1;
        for (const auto& l : (q ? q->lists : std::vector<omk::UiList>{}))
            if (l.addr == omk::kListSneakVerbs) vsel = f.selectionOf(l);
        std::printf("verb default %d\n", vsel);
        std::printf("confirm row -> panel %#x, list %d, tabs off %d rows off %d\n",
                    q ? q->addr : 0, f.currentList(),
                    f.listOff(omk::kListSneakTabs) ? 1 : 0,
                    f.listOff(omk::kListSneakRows) ? 1 : 0);
        // ...then step to "Examiner" and confirm it.
        for (int k = 0; k < 2; ++k) f.press(omk::kUiRight);  // -> Examiner
        f.press(omk::kUiConfirm);
        q = f.panel();
        // ...and the COLOUR must still be the inventory page's amber: the
        // verb panel writes no colour of its own, so clearing on a panel
        // change sends the whole device back to its (255, 0, 0) placeholder.
        {
            int vc[3] = {-1, -1, -1};
            if (const int* c = f.itemColour(0x004DE440u))   // the first row
                { vc[0] = c[0]; vc[1] = c[1]; vc[2] = c[2]; }
            std::printf("verb panel rows still %d %d %d (%zu coloured)\n", vc[0], vc[1], vc[2], f.colourCount());
        }
        std::printf("confirm Examiner -> panel %#x, verbs off %d\n",
                    q ? q->addr : 0, f.listOff(omk::kListSneakVerbs) ? 1 : 0);
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
        // ---- THE THREE 3D PREVIEWS ---------------------------------
        //
        // `I2D_Submit3DView` through the bounding-box arm of `sub_478DE0`.
        // What is asserted is that each 50x50 slot gets a substantial
        // picture: the wrong arm - the CHARACTER view's 118.110 - puts these
        // 3-to-8-unit objects a hundred units off and paints two pixels.
        omk::UiModels mods;
        const int loaded = mods.load(fs);
        comp.attachModels(&mods);
        omk::Surface mv(640, 480, 0);
        omk::UiWalk mw(w);
        mw.open(omk::kScreenSneak);
        const auto mf = comp.draw(mv, omk::kScreenSneak, mw);
        int painted[3] = {0, 0, 0};
        for (int sl = 0; sl < 3; ++sl)
            for (int yy = 100 + sl * 100; yy < 150 + sl * 100; ++yy)
                for (int xx = 110; xx < 160; ++xx)
                    if (mv.px[static_cast<std::size_t>(yy) * 640 + xx]) ++painted[sl];
        // Isolate the models: compose once WITHOUT them and once with, and
        // measure what changed inside each slot.
        omk::Surface bare(640, 480, 0);
        comp.attachModels(nullptr);
        omk::UiWalk bw2(w); bw2.open(omk::kScreenSneak);
        comp.draw(bare, omk::kScreenSneak, bw2);
        comp.attachModels(&mods);
        for (int sl = 0; sl < 3; ++sl) {
            int lo[2] = {99, 99}, hi[2] = {-1, -1};
            for (int yy = 100 + sl * 100; yy < 150 + sl * 100; ++yy)
                for (int xx = 110; xx < 160; ++xx) {
                    const std::size_t o = static_cast<std::size_t>(yy) * 640 + xx;
                    if (mv.px[o] == bare.px[o]) continue;
                    const int rx = xx - 110, ry = yy - (100 + sl * 100);
                    if (rx < lo[0]) lo[0] = rx;  if (rx > hi[0]) hi[0] = rx;
                    if (ry < lo[1]) lo[1] = ry;  if (ry > hi[1]) hi[1] = ry;
                }
            std::printf("slot %d model bbox %d x %d\n", sl,
                        hi[0] - lo[0] + 1, hi[1] - lo[1] + 1);
        }
        std::printf("previews %d loaded, %d drawn, slots %d %d %d of 2500\n",
                    loaded, mf.modelsDrawn, painted[0], painted[1], painted[2]);

        std::printf("cursor quads %d, alpha %d, focused row paints %d %d %d\n",
                    f.cursorQuads, cur.alpha(), ((u >> 11) & 31) * 255 / 31,
                    ((u >> 5) & 63) * 255 / 63, (u & 31) * 255 / 31);
    }
    return 0;
}
