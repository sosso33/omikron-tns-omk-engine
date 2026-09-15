// SPDX-License-Identifier: GPL-3.0-or-later
// The sneak's QUIT tab (todo/sneak.md §5e step 6).
//
//     sneak_quit <tables/ui_widgets.json> <tables/ui.json>
//
// The tab column's last icon (0x004DE118, `IAM\Sneak` 32 "Quitter le jeu")
// carries a callback AND a child page, and `Ui_ConfirmSelection` prefers the
// callback - so the page `0x004DF0C0` is never installed and its Oui/Non list
// `0x004DEBA0`, which belongs to that page alone, is drawn by nothing. What
// the callback `0x0049DBF0` does do is show that list, select "Non"
// (`word_4DEBA2 = 1`) and write `1` into whatever panel is CURRENT.
//
// Walked from screen 9: RIGHT onto the tab column, DOWN three times to the
// icon, ENTER. Printed after each key: the panel, the current list INDEX, the
// tab's selection, whether the walk went approximate, and whether the quit
// request is up.
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
        // ...and the Oui/Non list's own selection, which the callback writes
        // (`word_4DEBA2 = 1`, "Non"). It is read through the TREE, not through
        // the current panel, because the page that carries it is never
        // installed.
        int qsel = -2;
        if (const omk::UiList* ql = w.listAt(omk::kListSneakQuit)) qsel = walk.selectionOf(*ql);
        std::printf("%-6s panel %#x list %d tab %d quitlist %s sel %d approx %d quit %d\n", after,
                    p ? p->addr : 0u, walk.currentList(), sel,
                    walk.listOff(omk::kListSneakQuit) ? "hidden" : "shown", qsel,
                    walk.approximate() ? 1 : 0, walk.quitRequested() ? 1 : 0);
    };
    walk.press(omk::kUiRight);   report("RIGHT");    // the rows -> the tab column
    walk.press(omk::kUiDown);    report("DOWN");
    walk.press(omk::kUiDown);    report("DOWN");
    walk.press(omk::kUiDown);    report("DOWN");     // "Quitter le jeu"
    walk.press(omk::kUiConfirm); report("ENTER");    // 0x0049DBF0
    return 0;
}
