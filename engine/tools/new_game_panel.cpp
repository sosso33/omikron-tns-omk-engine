// SPDX-License-Identifier: GPL-3.0-or-later
// THE START MENU'S NEW-GAME DIALOG, composed headless beside the menu itself.
//
//     new_game_panel <gamedata> <ui_widgets.json> <ui.json> <out.bin>
//
// `run_screen` composes a screen's TOP panel. This one descends: CONFIRM on
// "Nouvelle partie" installs the child panel 0x004CF280, which is where the
// player types a name - and it is the one panel in the tree whose text comes
// from an item's `+20` DRAW HOOK rather than from any field a composer reads.
//
// What it measures, per panel:
//
//   * rows drawn and the pen advance, with the blink LOW and HIGH - the caret
//     is on oscillator 1, so exactly one row appears between them;
//   * where the caret lands, against the measured width of the text before it;
//   * how many pixels of the top 150 rows are the sheet's own background,
//     RGB565 0x0020 = rgb(4, 4, 4). That number separates the two panels: the
//     menu's own panel carries the 640x150 sprite item that paints the title
//     band opaquely, and the child does not carry it, so the animated cloud
//     runs to y = 0 behind the dialog. A composer that blits the sheet for a
//     panel with no tiles paints the band on BOTH.
#include "platform/datafs.h"
#include "platform/frontend.h"

#include "ui/cloud.h"
#include "ui/iamtext.h"
#include "ui/screendraw.h"
#include "ui/text.h"
#include "ui/widgets.h"

#include <cstdio>
#include <fstream>
#include <algorithm>
#include <vector>

namespace {

// The sheet's own background, palette index 255 = rgb(4, 4, 4), through 565.
constexpr std::uint16_t kSheetBack = 0x0020;

int bandPixels(const omk::Surface& fb) {
    int n = 0;
    for (int y = 0; y < 150 && y < fb.h; ++y)
        for (int x = 0; x < fb.w; ++x)
            n += fb.px[static_cast<std::size_t>(y) * fb.w + x] == kSheetBack;
    return n;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: new_game_panel <gamedata> "
                             "<ui_widgets.json> <ui.json> <out.bin>\n");
        return 2;
    }
    const omk::DataFs fs(argv[1]);
    auto w = omk::UiWidgets::loadJson(argv[2]);
    if (!w.valid()) { std::fprintf(stderr, "cannot load %s\n", argv[2]); return 1; }
    w.loadScreens(argv[3]);
    const auto table = omk::FontTable::loadJson(argv[3]);
    const omk::TextLayout lay(table, std::string(argv[1]) + "/FONTS");
    omk::ScreenComposer comp(fs, w, lay);
    omk::MenuCloud cloud;
    if (cloud.load(fs)) comp.attachCloud(&cloud);
    comp.setFrame(0);
    comp.setDisplay(640, 480);

    std::vector<std::int32_t> out;
    omk::Surface low(640, 480, 0), high(640, 480, 0);
    const auto shot = [&](const omk::UiWalk& walk, long clockMs,
                          omk::Surface* keep) {
        comp.setClockMs(clockMs);
        omk::Surface fb(640, 480, 0);
        const auto f = comp.draw(fb, 29, walk);
        out.push_back(f.itemsDrawn);
        out.push_back(f.textAdvance);
        out.push_back(bandPixels(fb));
        std::printf("  rows %d, advance %d, band %d\n",
                    f.itemsDrawn, f.textAdvance, bandPixels(fb));
        if (keep) *keep = fb;
        return f;
    };

    // 1. the menu's own panel, blink low
    omk::UiWalk menu(w);
    menu.open(29);
    std::printf("menu panel %08X\n", menu.panel() ? menu.panel()->addr : 0);
    out.push_back(static_cast<std::int32_t>(menu.panel() ? menu.panel()->addr : 0));
    shot(menu, 0, nullptr);

    // 2. the new-game dialog, a name typed, blink low then high
    omk::UiWalk dlg(w);
    dlg.open(29);
    dlg.press(omk::kUiConfirm);
    dlg.typeName("Burntpin");
    std::printf("dialog panel %08X, name '%s', caret %d\n",
                dlg.panel() ? dlg.panel()->addr : 0, dlg.name().c_str(),
                dlg.nameCursor());
    out.push_back(static_cast<std::int32_t>(dlg.panel() ? dlg.panel()->addr : 0));
    out.push_back(dlg.nameCursor());
    // ...with the caret stepped BACK ONE, so it is not at the end of the
    // buffer. A caret drawn after the last character instead of after the
    // cursor lands on the same pixel when they coincide, and the box below is
    // the only thing that can tell them apart.
    dlg.press(omk::kUiLeft);
    out.push_back(dlg.nameCursor());
    shot(dlg, 0, &low);
    shot(dlg, 500, &high);

    // 3. where the caret lands: the pen width of everything before it, in the
    // item's own face. `Ui_DrawNameField` measures `label + " : " + typed`
    // truncated at `strlen(label) + cursor + 3`.
    const auto strings = omk::iamStrings(fs, "IAM/" + w.textFile(29));
    const std::string label = strings.size() > 13 ? strings[13] : std::string();
    const std::string shown = label + " : " + dlg.name();
    const std::string cut = shown.substr(
        0, label.size() + 3 + static_cast<std::size_t>(dlg.nameCursor()));
    const auto pre = omk::parseMarkup(cut, 'J', 255, 255, 255).run;
    out.push_back(static_cast<std::int32_t>(label.size()));
    out.push_back(lay.measure(pre));
    std::printf("label '%s', shown '%s', before the caret '%s' = %d\n",
                label.c_str(), shown.c_str(), cut.c_str(), lay.measure(pre));

    // ...and WHERE the caret is drawn, as the bounding box of the pixels the
    // two blink phases differ in. Nothing else on the dialog moves between
    // them, so this box IS the caret - and it has to start at the item's own
    // x plus the width measured above, which is the whole of what
    // `Ui_DrawNameField`'s second `Text_DrawBlock` does.
    int cx0 = 640, cy0 = 480, cx1 = -1, cy1 = -1;
    for (int y = 0; y < 480; ++y)
        for (int x = 0; x < 640; ++x)
            if (low.px[static_cast<std::size_t>(y) * 640 + x] !=
                high.px[static_cast<std::size_t>(y) * 640 + x]) {
                cx0 = std::min(cx0, x); cx1 = std::max(cx1, x);
                cy0 = std::min(cy0, y); cy1 = std::max(cy1, y);
            }
    out.push_back(cx0); out.push_back(cy0);
    out.push_back(cx1); out.push_back(cy1);
    std::printf("caret box %d,%d .. %d,%d\n", cx0, cy0, cx1, cy1);

    // 4. RIGHT moves the caret back and refuses to run past the end.
    dlg.press(omk::kUiRight);
    dlg.press(omk::kUiRight);
    out.push_back(dlg.nameCursor());
    std::printf("caret after two RIGHT %d\n", dlg.nameCursor());

    if (!omk::safeOutputPath(argv[4])) return 2;
    std::ofstream f(argv[4], std::ios::binary);
    f.write(reinterpret_cast<const char*>(out.data()),
            static_cast<std::streamsize>(out.size() * sizeof(std::int32_t)));
    return 0;
}
