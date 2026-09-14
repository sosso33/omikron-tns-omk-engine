// SPDX-License-Identifier: GPL-3.0-or-later
// WHAT A SHOP LOOKS LIKE THE MOMENT IT OPENS - `Ui_OpenShop` (0x004AE540).
//
//     shop_open <gamedata> <tables/ui_widgets.json> <tables/ui.json>
//
// Ten screens (20..28, 32) share one panel, and the open writes two things
// the widget record cannot carry: the BUTTON list's selection `word_4E3372`
// (1 at the bank, 0 in the other nine) and, from the button that selection
// names, the colour of the nine stock rows and the header - the (255, 0, 0)
// placeholder replaced by `sub_4296D0`. It prints, for the pharmacy, the bank
// and the Lahoreh library: the selection, which of "Acheter" / "Vente" is
// hidden, the colour written onto both lists, and the pixel a stock row's bar
// then composes to - so `verify.py: engine: shop open` can tell a per-screen
// value from one left over by the screen opened before it.
#include "formats/iam.h"
#include "platform/datafs.h"
#include "script/inventory.h"
#include "script/objects.h"
#include "ui/screendraw.h"
#include "ui/text.h"
#include "ui/widgets.h"

#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: shop_open <gamedata> <ui_widgets.json> <ui.json>\n");
        return 2;
    }
    const omk::DataFs fs(argv[1]);
    auto w = omk::UiWidgets::loadJson(argv[2]);
    if (!w.valid()) { std::fprintf(stderr, "cannot load %s\n", argv[2]); return 1; }
    w.loadScreens(argv[3]);
    const auto table = omk::FontTable::loadJson(argv[3]);
    const omk::TextLayout lay(table, std::string(argv[1]) + "/FONTS");
    omk::ScreenComposer comp(fs, w, lay);
    comp.setDisplay(640, 480);

    // ONE shared store across the three opens, as the data segment is: a
    // colour the bank did not write would survive from the pharmacy.
    omk::UiListState st;
    for (int screen : {21, 20, 32}) {
        omk::UiWalk walk(w, st);
        if (!walk.open(screen) || !walk.panel()) {
            std::printf("screen %d: no panel\n", screen);
            continue;
        }
        const omk::UiPanel* p = walk.panel();
        int sel = -1;
        const omk::UiList* buttons = nullptr;
        for (const auto& l : p->lists)
            if (l.addr == omk::kListShopButtons) { buttons = &l; sel = walk.selectionOf(l); }
        // "Acheter" is the list's item 0 and "Vente" item 1; their hide is
        // the `0x40000001` each arm sets on the other.
        int hidden[2] = {-1, -1};
        if (buttons && buttons->items.size() >= 2)
            for (int k = 0; k < 2; ++k) {
                std::uint32_t eff[3];
                buttons->items[static_cast<std::size_t>(k)].effective(buttons->broadcast, eff);
                hidden[k] = (eff[1] & 1) ? 1 : 0;
            }
        const auto first = [&](std::uint32_t list) -> const omk::UiItem* {
            for (const auto& l : p->lists)
                if (l.addr == list && !l.items.empty()) return &l.items.front();
            return nullptr;
        };
        const omk::UiItem* row = first(omk::kListShopRows);
        const omk::UiItem* head = first(omk::kListShopHeader);
        const int* rc = row ? walk.itemColour(row->addr) : nullptr;
        const int* hc = head ? walk.itemColour(head->addr) : nullptr;

        omk::Surface fb(640, 480, 0);
        comp.draw(fb, screen, walk);
        int px[3] = {-1, -1, -1};
        if (row) {
            // the bar's flat right-hand end, past any label
            const std::uint16_t v = fb.px[static_cast<std::size_t>(row->y + row->h / 2) * 640 +
                                          static_cast<std::size_t>(row->x + row->w - 20)];
            px[0] = ((v >> 11) & 31) * 255 / 31;
            px[1] = ((v >> 5) & 63) * 255 / 63;
            px[2] = (v & 31) * 255 / 31;
        }
        std::printf("screen %d param %d: buttons select %d, hidden acheter %d vente %d, "
                    "rows %d %d %d, header %d %d %d, row bar paints %d %d %d\n",
                    screen, w.screenParam(screen), sel, hidden[0], hidden[1],
                    rc ? rc[0] : -1, rc ? rc[1] : -1, rc ? rc[2] : -1,
                    hc ? hc[0] : -1, hc ? hc[1] : -1, hc ? hc[2] : -1,
                    px[0], px[1], px[2]);
    }

    // ---- THE TWO HOOKS: the panel's `0x004AEE00` and the rows' `0x0042AFF0`
    //
    // The shop opens on its BUTTON list. LEFT crosses into the rows through
    // the panel hook - `sub_42A710` unless the buttons are current AND "Back"
    // (button 3) is selected, where it declines - and DOWN then walks the
    // sixteen books through the nine widgets with the centred window.
    {
        omk::UiWalk lib(w);
        lib.open(32);
        lib.bindRows(omk::kListShopRows, 16);
        const auto rowSel = [&](omk::UiWalk& wk) {
            for (const auto& l : wk.panel()->lists)
                if (l.addr == omk::kListShopRows) return wk.selectionOf(l);
            return -1;
        };
        const int opened = lib.currentList();
        lib.press(omk::kUiLeft);
        const int crossed = lib.currentList();
        for (int k = 0; k < 10; ++k) lib.press(omk::kUiDown);
        std::printf("walk 32: opens on list %d, LEFT -> list %d, ten DOWNs -> widget %d "
                    "window %d (book %d)%s\n", opened, crossed, rowSel(lib),
                    lib.rowWindow(omk::kListShopRows),
                    rowSel(lib) + lib.rowWindow(omk::kListShopRows),
                    lib.approximate() ? " approximate" : "");
        omk::UiWalk ph(w);
        ph.open(21);
        const auto btnSel = [&]() {
            for (const auto& l : ph.panel()->lists)
                if (l.addr == omk::kListShopButtons) return ph.selectionOf(l);
            return -1;
        };
        std::printf("walk 21: opens on list %d button %d; DOWN ->", ph.currentList(), btnSel());
        // Acheter -> preview -> Back: TWO presses, because the hidden Vente
        // is skipped - a third wraps round to Acheter
        for (int k = 0; k < 2; ++k) {
            ph.press(omk::kUiDown);
            std::printf(" %d(l%d)", btnSel(), ph.currentList());
        }
        std::printf("; log:");
        for (const auto& e : ph.log()) std::printf(" [%s]", e.c_str());
        const int btn = btnSel();
        ph.press(omk::kUiLeft);
        std::printf("\nwalk 21: button %d, LEFT there -> list %d\n", btn, ph.currentList());
        // ...and a BUTTON's own confirm (0x004AED00): the focus goes to the
        // rows when the binder counted any, and nowhere when it counted none.
        for (int count : {2, 0}) {
            omk::UiWalk cb(w);
            cb.open(21);
            cb.bindRows(omk::kListShopRows, count);
            cb.press(omk::kUiConfirm);
            std::printf("walk 21: %d rows bound, ENTER on Acheter -> list %d\n",
                        count, cb.currentList());
        }
    }

    // ---- THE STOCK: list 3 is the active AREA block's `+8` ----------------
    //
    // Sixteen int16 ids ending at 0xFFFF, which `Game_HandleEvent` case 9
    // points list 3 at on entering an area. Printed by name for the pharmacy,
    // a restaurant, the bank (which sells, and ships none) and the Lahoreh
    // library (all sixteen), so a wrong offset or a wrong terminator reads as
    // the wrong goods rather than as a plausible count.
    const auto objects = omk::loadObjects(fs);
    const std::vector<omk::Recipe> noRecipes;
    const omk::Inventory inv(objects, noRecipes);
    const auto areaFile = fs.read("IAM/AREA");
    const auto arch = omk::IamArchive::open(areaFile);
    for (int area : {39, 30, 83, 86}) {
        const auto ids = omk::shopStock(arch.chunk(static_cast<std::size_t>(area)));
        std::printf("stock area %d: %zu", area, ids.size());
        for (int id : ids) std::printf(" | %d %s", id, inv.displayName(id, 0).c_str());
        std::printf("\n");
    }
    return 0;
}
