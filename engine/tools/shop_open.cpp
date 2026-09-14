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
#include "platform/datafs.h"
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
    return 0;
}
