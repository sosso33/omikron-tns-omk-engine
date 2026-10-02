// SPDX-License-Identifier: GPL-3.0-or-later
// THE SETUP: the boot chain.
// A stretch of what was `main`'s body, moved byte for byte by
// `todo/play-split.md` (2026-10-02); `PlayState::run` calls the sections in
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
    // **THE MOVIES ARE STEPPED, NOT DECODED.** `gamedata/FLIS/` holds three MPEG-1
    // program streams and `PORTING` A8 names pl_mpeg as the vendored decoder
    // for them; it is not integrated, so the boot finds the three files,
    // reports them, and moves on. The first thing a player sees is therefore
    // missing here, and saying so is the point - a window that opens on the
    // menu would imply the app starts there.
    bo.root = fr; bo.tables = tb; bo.frames = 1;
    br = omk::boot(bo);
    std::printf("boot: %d FLIS movie%s found; %s -> %d sprites, %d sounds; "
                "start area %d\n",
                br.moviesFound, br.moviesFound == 1 ? "" : "s",
                br.bootScene.c_str(), br.bootSprites, br.bootSounds,
                br.startArea);

    OMK_HEAPCHECK("before live session");
    return -1;
}
