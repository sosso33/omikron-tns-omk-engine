// SPDX-License-Identifier: GPL-3.0-or-later
// THE FRAME'S INPUT PHASE - input, the pause screen, the game tick, scripted object motion, the sound effects.
// Part of `main`'s loop body, moved byte for byte by `todo/play-split.md`
// (2026-10-02); see `playframe.h` for what the names below refer to.
#include "playframe.h"

int PlayState::phaseInput() {
    bool done = false;
    do {
        if (const int part = inputPump(); part != -1) { if (part == -2) break; return part; }
        inputPause();
        inputTick();
        inputMotion();
        inputSounds();
        done = true;
    } while (false);
    return done ? -1 : -2;
}
