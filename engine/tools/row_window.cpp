// SPDX-License-Identifier: GPL-3.0-or-later
// THE SNEAK'S ROW WINDOW - `sub_42AFF0`, and the tenth object.
//
//     row_window <tables>
//
// The nine row widgets of list 0x004DE6F0 are a WINDOW onto a list that may
// be longer. `sub_42AAE0(list, window)` binds them and `sub_42AFF0` moves the
// selection AND the window: a centred rule, where the cursor moves until it
// reaches the middle widget and then the window moves under it instead.
//
// Until 2026-09-04 the window was hardcoded 0, so a list longer than nine was
// truncated - carry ten things and the tenth could not be reached at all.
// This drives DOWN until nothing moves and reports the furthest row reached,
// which is the number that was wrong.
//
// One line per fact, `key ...`:
//   widgets   how many row widgets the list has, and the window's start
//   walk N    driving DOWN N times: the row reached, the window, the marks
//   back      driving UP the same number of times returns to row 0
#include "ui/widgets.h"
#include "actor/moves.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: row_window <tables>\n"); return 2; }
    const auto w = omk::UiWidgets::loadJson(std::string(argv[1]) + "/ui_widgets.json");
    if (!w.valid()) { std::fprintf(stderr, "no ui_widgets.json\n"); return 1; }

    const omk::UiList* rows = nullptr;
    if (const auto* p = w.at(omk::kPanelSneakInventory))
        for (const auto& l : p->lists)
            if (l.addr == omk::kListSneakRows) rows = &l;
    if (!rows) { std::fprintf(stderr, "no row list\n"); return 1; }
    const int widgets = static_cast<int>(rows->items.size());

    // Twelve rows in nine widgets - the case the old code could not reach.
    const int count = 12;
    omk::UiListState st;
    omk::UiWalk walk(w, st);
    walk.open(omk::kScreenSneak);
    walk.bindRows(omk::kListSneakRows, count, 0);
    std::printf("widgets %d window %d count %d\n", widgets,
                walk.rowWindow(omk::kListSneakRows), count);

    // DOWN until it stops moving, and the furthest ROW the selection reaches.
    int best = 0;
    for (int i = 0; i < 40; ++i) {
        walk.press(omk::kUiDown);
        walk.bindRows(omk::kListSneakRows, count,
                      walk.rowWindow(omk::kListSneakRows));
        const auto* sel = walk.selected();
        const int row = sel ? walk.rowOf(sel->addr) : -1;
        if (row > best) best = row;
    }
    const std::uint32_t topMark = walk.rowArrows(rows->items.front().addr);
    const std::uint32_t botMark = walk.rowArrows(rows->items.back().addr);
    std::printf("walk down reached row %d of %d, window %d, top_mark %d bot_mark %d\n",
                best, count - 1, walk.rowWindow(omk::kListSneakRows),
                (topMark & 0x100000u) ? 1 : 0, (botMark & 0x200000u) ? 1 : 0);

    for (int i = 0; i < 40; ++i) {
        walk.press(omk::kUiUp);
        walk.bindRows(omk::kListSneakRows, count,
                      walk.rowWindow(omk::kListSneakRows));
    }
    const auto* sel = walk.selected();
    std::printf("walk up returned to row %d, window %d\n",
                sel ? walk.rowOf(sel->addr) : -1,
                walk.rowWindow(omk::kListSneakRows));
    return 0;
}
