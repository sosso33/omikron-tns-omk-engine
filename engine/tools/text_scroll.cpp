// SPDX-License-Identifier: GPL-3.0-or-later
// THE EXAMINE PAGE'S TEXT SCROLLS - `sub_42A9A0` and bank C `0x2`.
//
//     text_scroll <tables>
//
// Ten lists in the widget tree name hook `0x0042A9A0`, and between them they
// hold the four items carrying bank C `0x2`. The hook is not a selection
// mover: it steps `dword_6A5090` by eight pixels on UP or DOWN and falls
// through to the default. `Ui_ItemTextStyle` then clamps that global against
// the laid-out height of the block, which is why the offset can be moved
// freely here and is only bounded by the draw.
//
// This walks the device to the examine page the way a player does - a row,
// RIGHT twice onto "Examiner", confirm - and drives the scroll.
//
// One line per fact:
//   lists      how many lists name the hook, and how many items carry the bit
//   page       the panel reached and the list the walk is standing in
//   down N     the offset after N presses of DOWN
//   up         ...and after the same number of UP
//   reopen     the offset after re-entering the page (sub_49B950 zeroes it)
#include "ui/widgets.h"
#include "actor/moves.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: text_scroll <tables>\n"); return 2; }
    const auto w = omk::UiWidgets::loadJson(std::string(argv[1]) + "/ui_widgets.json");
    if (!w.valid()) { std::fprintf(stderr, "no ui_widgets.json\n"); return 1; }

    int hooked = 0, bits = 0;
    for (const auto& p : w.all())
        for (const auto& l : p.lists) {
            if (l.hook == omk::kScrollTextBox) ++hooked;
            for (const auto& it : l.items)
                if ((it.flags[2] & 0x80000000u) &&
                    (it.flags[2] & omk::kItemScrolls)) ++bits;
        }
    std::printf("lists %d items %d\n", hooked, bits);

    omk::UiListState st;
    omk::UiWalk walk(w, st);
    walk.open(omk::kScreenSneak);
    walk.bindRows(omk::kListSneakRows, 1, 0);
    // `list+2` is a STATIC record and the verb bar keeps what it last chose,
    // so only the FIRST walk has to move onto "Examiner"; the second finds it
    // already selected. Moving twice again would wrap 2 -> 0 -> 1 and open
    // `Utiliser sur` instead, which is the engine's behaviour and not a fault.
    const auto toExamine = [&](bool move) {
        walk.press(omk::kUiConfirm);                 // a row -> the verb panel
        if (move) { walk.press(omk::kUiRight);       // Utiliser -> Utiliser sur
                    walk.press(omk::kUiRight); }     // ...-> Examiner
        walk.press(omk::kUiConfirm);                 // -> the examine page
    };
    toExamine(true);
    const auto* p = walk.panel();
    const int cur = walk.currentList();
    const omk::UiList* l = (p && cur >= 0 &&
                            static_cast<std::size_t>(cur) < p->lists.size())
                         ? &p->lists[static_cast<std::size_t>(cur)] : nullptr;
    std::printf("page %s list %s\n",
                p && p->addr == omk::kPanelSneakExamine ? "examine" : "NOT examine",
                l && l->hook == omk::kScrollTextBox ? "scroller" : "not the scroller");

    for (int i = 0; i < 4; ++i) walk.press(omk::kUiDown);
    std::printf("down 4 offset %d\n", walk.textScroll());
    for (int i = 0; i < 4; ++i) walk.press(omk::kUiUp);
    std::printf("up 4 offset %d\n", walk.textScroll());

    // ...and past the top, because the HOOK has no clamp - the draw does.
    for (int i = 0; i < 3; ++i) walk.press(omk::kUiUp);
    std::printf("past the top offset %d\n", walk.textScroll());

    // `sub_49B950` zeroes it, so a second object is read from the top.
    // `loc_49BEF8`'s tail is how a page is left - `sub_42A370(screen, panel)`
    // - which is `installPanel` here, not a BACK bit.
    walk.installPanel(omk::kPanelSneakInventory);
    toExamine(false);
    const auto* p2 = walk.panel();
    std::printf("reopen offset %d page %s\n", walk.textScroll(),
                p2 && p2->addr == omk::kPanelSneakExamine ? "examine" : "elsewhere");
    return 0;
}
