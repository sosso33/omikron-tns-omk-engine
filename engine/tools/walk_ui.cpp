// SPDX-License-Identifier: GPL-3.0-or-later
// The widget tree walked with the engine's own input words - the differential
// against tools/sim/ui.py.
//
//     walk_ui <tables/ui_widgets.json> <out.bin>
//
// For every screen the table carries a panel for, this opens it and reports
// what the walk settles on, then drives DOWN four times and reports again.
// `tools/sim/ui.py` does the same thing reading the executable directly; the
// two must agree on every screen, or one of them has the record layout wrong.
//
// out.bin: int32 panels, lists, items, selectableItems, hookedLists,
//          int32 defaultWalkLists, screensSettlingOnAUsableList,
//          int32 approximateOnOpen,
//          then per screen: int32 screen, listCount, curList, sel0, selAfter4Down
#include "platform/datafs.h"
#include "ui/widgets.h"

#include <array>
#include <cstdio>
#include <fstream>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: walk_ui <ui_widgets.json> <out.bin>\n");
        return 2;
    }
    const auto w = omk::UiWidgets::loadJson(argv[1]);
    if (!w.valid()) { std::fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }

    long lists = 0, items = 0, selectable = 0, hooked = 0, plain = 0;
    long settled = 0, approxOnOpen = 0;
    std::vector<std::array<int, 5>> rows;
    for (const auto& p : w.all()) {
        for (const auto& l : p.lists) {
            ++lists;
            if (l.hook) ++hooked; else ++plain;
            for (const auto& it : l.items) {
                ++items;
                if (it.selectable(l.broadcast)) ++selectable;
            }
        }
        omk::UiWalk walk(w);
        walk.open(p.screen);
        if (walk.approximate()) ++approxOnOpen;
        const int cur0 = walk.currentList(), sel0 = walk.selection();
        if (cur0 >= 0 && static_cast<std::size_t>(cur0) < p.lists.size()) ++settled;
        for (int k = 0; k < 4; ++k) walk.press(omk::kUiDown);
        rows.push_back({p.screen, static_cast<int>(p.lists.size()), cur0, sel0,
                        walk.selection()});
    }

    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    put32(static_cast<std::int32_t>(w.all().size()));
    for (long v : {lists, items, selectable, hooked, plain, settled, approxOnOpen})
        put32(static_cast<std::int32_t>(v));
    for (const auto& r : rows) for (int v : r) put32(v);
    if (!omk::safeOutputPath(argv[2])) return 2;
    std::ofstream f(argv[2], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));

    std::printf("%zu panels, %ld lists (%ld hooked, %ld on the default walk), "
                "%ld items of which %ld selectable\n",
                w.all().size(), lists, hooked, plain, items, selectable);
    std::printf("%ld screens settle on a list in range, %ld are approximate "
                "before any key is pressed\n", settled, approxOnOpen);
    for (const auto& r : rows)
        std::printf("    screen %-3d %d lists, settles on list %d item %d, "
                    "after 4x DOWN item %d\n", r[0], r[1], r[2], r[3], r[4]);
    return 0;
}
