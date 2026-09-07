// SPDX-License-Identifier: GPL-3.0-or-later
// WHICH SCREENS KEEP THE WORLD BEHIND THEM - the 0x40000 bit of the screen
// record's +112.
//
//     screen_world <tables/ui_widgets.json> <tables/ui.json> <out.bin>
//
// `UI_LoadScreen` (0x00429BB0) reads the screen record's `+112`, masks it with
// `40000h`, and when the bit is CLEAR calls `sub_466B30`: `byte_90E155 = 0`
// (so `Game_Frame` stops submitting the full-screen 3D view through
// `sub_479C20`), `sub_46C290` (the sound bank is suspended) and the player's
// `+194` = ACTOR_STATE 9. `Ui_CloseScreenDefault` (0x0042A150) calls the
// partner `sub_466B60`. So the bit means "this screen shows the live world",
// and the default across the 37 is that a screen HIDES it.
//
// The port's `UiWidgets::worldBehind` is that mask, and this counts what it
// answers over every screen the table names. The interesting rows are the
// three that keep the world and the SNEAK (9), which does not - the sneak's
// page art has a deliberate hole in the middle, and drawing the city street
// through it is what a reader reported as a transparent background
// (todo/omk-play.md 82).
//
// out.bin: int32 screens, keep, hide, sneakKeeps, pauseKeeps,
//          shootMecaKeeps, shootHumanKeeps, unknownKeeps
#include "platform/datafs.h"
#include "ui/widgets.h"

#include <cstdint>
#include <cstdio>
#include <fstream>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: screen_world <ui_widgets.json> <ui.json> <out.bin>\n");
        return 2;
    }
    auto w = omk::UiWidgets::loadJson(argv[1]);
    if (!w.valid()) { std::fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
    w.loadScreens(argv[2]);

    int screens = 0, keep = 0, hide = 0;
    for (int id = 0; id < 37; ++id) {
        ++screens;
        if (w.worldBehind(id)) { ++keep; std::printf("screen %2d KEEPS the world\n", id); }
        else                     ++hide;
    }
    // and a screen the table does not name keeps it, which is the harness's
    // safer default rather than a fact about the game
    const int unknown = w.worldBehind(999) ? 1 : 0;

    const std::int32_t out[8] = {
        screens, keep, hide,
        w.worldBehind(9) ? 1 : 0,
        w.worldBehind(31) ? 1 : 0,
        w.worldBehind(33) ? 1 : 0,
        w.worldBehind(34) ? 1 : 0,
        unknown,
    };
    if (!omk::safeOutputPath(argv[3])) return 2;
    std::ofstream f(argv[3], std::ios::binary);
    f.write(reinterpret_cast<const char*>(out), sizeof(out));
    std::printf("%d screens, %d keep the world, %d hide it\n", screens, keep, hide);
    return 0;
}
