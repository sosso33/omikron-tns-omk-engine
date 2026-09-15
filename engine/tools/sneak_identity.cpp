// SPDX-License-Identifier: GPL-3.0-or-later
// The sneak's IDENTITY page walk (todo/sneak.md §5e step 1).
//
//     sneak_identity <tables/ui_widgets.json> <tables/ui.json>
//
// The page's builder `0x0049C100` puts the tab row on "Identite" and switches
// the Characteristics content off; the tab row's hook `0x0049C160` swaps the
// two contents as LEFT/RIGHT move along it; and the panel hook `0x0049C1D0`
// hands LEFT off the first tab and RIGHT off the second to the list mover,
// and picks the far tab when the row is entered by a LEFT. Opened the way a
// player reaches it - screen 9, RIGHT onto the tab column, UP twice, ENTER -
// and then walked along the row both ways, printing after each key the
// panel, the current list, the row's selection and which content is off.
#include "ui/widgets.h"

#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: sneak_identity <ui_widgets.json> <ui.json>\n");
        return 2;
    }
    auto w = omk::UiWidgets::loadJson(argv[1]);
    if (!w.valid()) { std::fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
    w.loadScreens(argv[2]);

    omk::UiWalk walk(w);
    if (!walk.open(9) || !walk.panel()) { std::printf("screen 9: no panel\n"); return 1; }
    walk.bindRows(omk::kListSneakRows, 1);

    const auto report = [&](const char* after) {
        const omk::UiPanel* p = walk.panel();
        const omk::UiList* cl = nullptr;
        int sel = -1;
        if (p) {
            const int c = walk.currentList();
            if (c >= 0 && static_cast<std::size_t>(c) < p->lists.size())
                cl = &p->lists[static_cast<std::size_t>(c)];
            for (const auto& l : p->lists)
                if (l.addr == omk::kListSneakIdentity) sel = walk.selectionOf(l);
        }
        std::printf("%-8s panel %#x list %#x tab %d identity %s characteristics %s\n", after,
                    p ? p->addr : 0u, cl ? cl->addr : 0u, sel,
                    walk.itemOff(omk::kItemSneakIdentityText) ? "off" : "on",
                    walk.itemOff(omk::kItemSneakCharacteristics) ? "off" : "on");
    };
    walk.press(omk::kUiRight);   report("RIGHT");   // rows -> the tab column
    walk.press(omk::kUiUp);      report("UP");
    walk.press(omk::kUiUp);      report("UP");      // the blue identity icon
    walk.press(omk::kUiConfirm); report("ENTER");   // the page
    walk.press(omk::kUiRight);   report("RIGHT");   // column -> the tab row, first tab
    walk.press(omk::kUiRight);   report("RIGHT");   // Identite -> Caracteristiques
    walk.press(omk::kUiRight);   report("RIGHT");   // off the far tab
    walk.press(omk::kUiLeft);    report("LEFT");    // back onto the row, the FAR tab
    walk.press(omk::kUiLeft);    report("LEFT");    // Caracteristiques -> Identite
    walk.press(omk::kUiLeft);    report("LEFT");    // off the first tab
    return 0;
}
