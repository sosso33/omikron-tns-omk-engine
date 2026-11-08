// SPDX-License-Identifier: GPL-3.0-or-later
// The sneak's QUIT tab (todo/sneak.md §5e step 6).
//
//     sneak_quit <tables/ui_widgets.json> <tables/ui.json>
//
// The tab column's last icon (0x004DE118, `IAM\Sneak` 32 "Quitter le jeu")
// carries a callback AND a child page. Every tab icon carries bank A 0x40, so
// the MOVE onto it installs its page (`Ui_MoveSelection`'s tail,
// `UiWalk::enterOnMove`): each DOWN below changes the page, and the Options
// page's hook 0x0049D960 lets UP/DOWN through. The Quit page's builder
// `sub_49D980` hides the Oui/Non list `0x004DEBA0` and selects Oui; the
// confirm runs the icon's callback `0x0049DBF0`, which shows the list,
// selects "Non" (`word_4DEBA2 = 1`) and makes it current (`+24 = 1`); LEFT and
// ENTER on "Oui" are `0x0049DBA0`, the quit request.
//
// Walked from screen 9: RIGHT onto the tab column, DOWN three times to the
// icon, ENTER, LEFT, ENTER. Printed after each key: the panel, the current list
// INDEX, the tab's selection, the Oui/Non list's state and selection, whether
// the walk went approximate, and whether the quit request is up.
#include "ui/widgets.h"

#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: sneak_quit <ui_widgets.json> <ui.json>\n");
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
        int sel = -1;
        if (p)
            for (const auto& l : p->lists)
                if (l.addr == omk::kListSneakTabs) sel = walk.selectionOf(l);
        // ...and the Oui/Non list's own selection, which the builder and the
        // callback write (`word_4DEBA2`). Read through the TREE, so it reads
        // the same before the page is installed.
        int qsel = -2;
        bool hidden = false;   // the pair's DRAW bit, on its items (`I2D_SetFlagOnAllRows`)
        if (const omk::UiList* ql = w.listAt(omk::kListSneakQuit)) {
            qsel = walk.selectionOf(*ql);
            for (const auto& it : ql->items)
                if (walk.itemFlagsOn(it.addr) & 1u) hidden = true;
        }
        std::printf("%-6s panel %#x list %d tab %d quitlist %s sel %d approx %d quit %d\n", after,
                    p ? p->addr : 0u, walk.currentList(), sel,
                    hidden ? "hidden" : "shown", qsel,
                    walk.approximate() ? 1 : 0, walk.quitRequested() ? 1 : 0);
    };
    walk.press(omk::kUiRight);   report("RIGHT");    // the rows -> the tab column
    walk.press(omk::kUiDown);    report("DOWN");
    walk.press(omk::kUiDown);    report("DOWN");
    walk.press(omk::kUiDown);    report("DOWN");     // "Quitter le jeu"
    walk.press(omk::kUiConfirm); report("ENTER");    // 0x0049DBF0
    walk.press(omk::kUiLeft);    report("LEFT");     // "Oui" (the pair's hook 0x0042A930 runs across)
    walk.press(omk::kUiConfirm); report("ENTER");    // 0x0049DBA0, the request
    return 0;
}
