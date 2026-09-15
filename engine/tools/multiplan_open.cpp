// SPDX-License-Identifier: GPL-3.0-or-later
// MULTIPLAN, screen 2 - the storage kiosk's walk (todo/multiplan.md step 1).
//
//     multiplan_open <gamedata> <tables/ui_widgets.json> <tables/ui.json>
//
// The kiosk moves objects between the sneak (object list 0) and its shared
// storage (list 1), and which one its rows show is `dword_68A610`, the SOURCE:
// the open writes 0, the button list's hook `0x004B09A0` switches it as the
// selection moves (the "vers le multiplan" button shows the sneak, the other
// three the kiosk) and repaints the rows and header in the selected button's
// colour, and the panel hook `0x004B0B00` hands LEFT/RIGHT between the
// buttons and the rows. This prints each of those after each key, so
// `verify.py: engine: multiplan open` can hold the walk to the reading.
#include "platform/datafs.h"
#include "ui/widgets.h"

#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: multiplan_open <gamedata> <ui_widgets.json> <ui.json>\n");
        return 2;
    }
    auto w = omk::UiWidgets::loadJson(argv[2]);
    if (!w.valid()) { std::fprintf(stderr, "cannot load %s\n", argv[2]); return 1; }
    w.loadScreens(argv[3]);

    omk::UiWalk walk(w);
    if (!walk.open(2) || !walk.panel()) { std::printf("screen 2: no panel\n"); return 1; }
    // two objects in whichever list is showing, so the focus rules can fire
    walk.bindRows(omk::kListMultiplanRows, 2);

    const auto report = [&](const char* after) {
        const omk::UiPanel* p = walk.panel();
        int btn = -1;
        for (const auto& l : p->lists)
            if (l.addr == omk::kListMultiplanButtons) btn = walk.selectionOf(l);
        const int* rc = nullptr;
        const int* hc = nullptr;
        for (const auto& l : p->lists) {
            if (l.addr == omk::kListMultiplanRows && !l.items.empty())
                rc = walk.itemColour(l.items.front().addr);
            if (l.addr == omk::kListMultiplanHeader && !l.items.empty())
                hc = walk.itemColour(l.items.front().addr);
        }
        std::printf("%-12s panel %#x list %d button %d source %d rows %d %d %d header %d %d %d\n",
                    after, p ? p->addr : 0u, walk.currentList(), btn,
                    walk.multiplanSource(),
                    rc ? rc[0] : -1, rc ? rc[1] : -1, rc ? rc[2] : -1,
                    hc ? hc[0] : -1, hc ? hc[1] : -1, hc ? hc[2] : -1);
    };
    report("open");
    walk.press(omk::kUiDown);   report("DOWN");       // -> "vers le sneak": the kiosk
    walk.press(omk::kUiDown);   report("DOWN");       // -> Examiner
    walk.press(omk::kUiDown);   report("DOWN");       // -> Detruire
    walk.press(omk::kUiDown);   report("DOWN wraps"); // -> "vers le multiplan": the sneak
    walk.press(omk::kUiLeft);   report("LEFT");       // buttons -> rows (rows hold 2)
    walk.press(omk::kUiRight);  report("RIGHT");      // rows -> buttons
    walk.press(omk::kUiConfirm); report("ENTER");     // the button's callback: rows
    return 0;
}
