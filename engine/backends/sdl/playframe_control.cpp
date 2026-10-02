// SPDX-License-Identifier: GPL-3.0-or-later
// THE FRAME'S CONTROL PHASE - the hand-over and the controller's frame.
// Part of `main`'s loop body, moved byte for byte by `todo/play-split.md`
// (2026-10-02); see `playframe.h` for what the names below refer to.
#include "playframe.h"

int PlayState::phaseControl() {
    // ---- the hand-over, and the controller's frame ------------------
    controlBinding();     // who is bound to him and who poses him; the shoot camera
    controlFlight();      // the bolts' flight, before the actors tick
    controlAdventure();   // adventure mode's controller frame
    return -1;
}
