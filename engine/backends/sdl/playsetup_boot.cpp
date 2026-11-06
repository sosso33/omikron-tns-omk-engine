// SPDX-License-Identifier: GPL-3.0-or-later
// THE SETUP: the boot chain.
// A stretch of `main`'s body, split out by `todo/play-split.md`;
// `PlayState::run` calls the sections in
// order. Returns -1 to go on, or the exit code `main` returns.
#include "playstate.h"

int PlayState::setupBoot() {
    // ---- the boot chain, because the app does not start at a menu --------
    //
    // `docs/BOOT.md`: launch -> the three FLIS movies -> Game_Start
    // ("aventure.scx", which is the global sprite and sound library and NOT a
    // menu) -> the start area out of `IAM\START +1414` -> whose startup script
    // reaches `ui.open`. Running it here rather than opening screen 29
    // directly means the menu appears because the SCRIPT asked for it, which
    // is what `engine: boot` reproduces 42 of 42 from a cold start.
    //
    // **THE MOVIES ARE NOT PLAYED HERE.** `gamedata/FLIS/` holds three MPEG-1
    // program streams, decoded by the vendored pl_mpeg (`platform/movie.*`,
    // `PORTING` A8) and played before the menu by `playsetup_play.cpp`
    // (`--nofmv` skips them, as the engine's own switch does); this section
    // only reports the boot.
    //
    // **AND THE VIEWER DOES NOT RUN `omk::boot`** for this one report line:
    // that is a whole headless boot - `aventure.scx` (3 MB) read to COUNT its
    // sprites and sounds, a Session built, area 118 loaded and a frame run -
    // all thrown away, before the window, and then done again for real by
    // the sections after it. On an iBook G3 reading from a USB 1.1 stick that
    // is seconds of a launch with nothing on screen
    // (`todo/handoff-classic-mac.md` 3f). `Game_Main` reads the library once,
    // AFTER the films (`setupBodies` now runs after `setupPlay`), and the
    // area in its frame loop; `build/omk` keeps the headless boot and
    // `engine: boot` checks it.
    const auto& fs = *fs_;
    int movies = 0;
    for (const char* m : {"FLIS/EIDOS.mpg", "FLIS/QUANTIC.mpg", "FLIS/GAME.mpg"})
        if (fs.resolve(m)) ++movies;
    std::printf("boot: %d FLIS movie%s found; aventure.scx is read after them, "
                "as Game_Main does\n", movies, movies == 1 ? "" : "s");

    OMK_HEAPCHECK("before live session");
    return phase::Continue;
}
