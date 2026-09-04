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
                  (row->x + row->w / 2)];
        std::printf("row bar paints %d %d %d\n", ((v >> 11) & 31) * 255 / 31,
                    ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31);
    }
    return 0;
}
