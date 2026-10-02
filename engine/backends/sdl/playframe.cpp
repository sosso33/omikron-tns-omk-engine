// SPDX-License-Identifier: GPL-3.0-or-later
// `PlayState::step` - one turn of the viewer's loop, which is its six phases
// in order (`playframe_<phase>.cpp`, `todo/play-split.md`). Each phase is a
// stretch of what was `main`'s `for (;;)` body, moved byte for byte inside
// `do { ... } while (false)`, so a `break` in it still ends the loop.
#include "playframe.h"

int PlayState::step() {
    int r = -1;
    if ((r = phaseInput()) != -1) return r;
    if ((r = phaseControl()) != -1) return r;
    if ((r = phaseModes()) != -1) return r;
    if ((r = phaseWorld()) != -1) return r;
    if ((r = phaseScreens()) != -1) return r;
    if ((r = phasePresent()) != -1) return r;
    return -1;
}
