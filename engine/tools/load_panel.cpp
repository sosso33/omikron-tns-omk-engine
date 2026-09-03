// SPDX-License-Identifier: GPL-3.0-or-later
// The start menu's load panel - the differential against `sim: load panel`.
//
//     load_panel <tables/ui_widgets.json> <gamedata/IAM/GAMES> <out.bin>
//
// The panel's shape depends on the save directory and on nothing else, so it
// is driven twice: once against the shipped `IAM\GAMES` and once against a
// synthetic one-profile directory. Showing it BRANCH is the point - a model
// that only ever ran the empty case would look identical to one that ignored
// the directory.
//
// out.bin: int32 entries, profiles, geometry, directoryBytes,
//          int32 emptyFocus, emptyMode, emptyLive,
//          int32 fullFocus, fullMode, fullLive
#include "platform/datafs.h"
#include "ui/widgets.h"

#include <cstdio>
#include <fstream>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: load_panel <ui_widgets.json> <GAMES> "
                             "<out.bin>\n");
        return 2;
    }
    const auto w = omk::UiWidgets::loadJson(argv[1]);
    const auto dir = omk::saveDirectory(argv[2], w);
    const int profiles = omk::saveProfiles(dir);

    const auto empty = omk::loadPanelFor(0, w);
    const auto full  = omk::loadPanelFor(1, w);
    const auto live = [](const omk::LoadPanelState& s) {
        int n = 0;
        for (const auto& [a, ok] : s.buttons) { (void)a; n += ok ? 1 : 0; }
        return n;
    };

    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    put32(static_cast<std::int32_t>(dir.size()));
    put32(profiles);
    put32(static_cast<std::int32_t>(w.savesHeader() +
          w.saveSlot() * static_cast<std::size_t>(w.saveSlots())));
    put32(static_cast<std::int32_t>(w.saveSlots() * 72));
    put32(empty.focus); put32(empty.mode); put32(live(empty));
    put32(full.focus);  put32(full.mode);  put32(live(full));
    if (!omk::safeOutputPath(argv[3])) return 2;
    std::ofstream f(argv[3], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));

    std::printf("save directory: %zu entries, %d distinct profiles; "
                "file geometry %zu, the builder memsets %d\n",
                dir.size(), profiles,
                w.savesHeader() + w.saveSlot() * static_cast<std::size_t>(w.saveSlots()),
                w.saveSlots() * 72);
    std::printf("  empty directory: focus %d, mode %d, %d of %zu buttons live\n",
                empty.focus, empty.mode, live(empty), empty.buttons.size());
    std::printf("  one profile:     focus %d, mode %d, %d of %zu buttons live\n",
                full.focus, full.mode, live(full), full.buttons.size());
    return 0;
}
