// SPDX-License-Identifier: GPL-3.0-or-later
// THE FRAME'S SCREENS PHASE - the screens' rows and the HUDs.
// Part of `main`'s loop body, split out by `todo/play-split.md`; see `playframe.h` for what the names below refer to.
#include "playframe.h"

int PlayState::phaseScreens() {
    screensSneakRows();       // the sneak's inventory rows
    screensShopRows();        // the shop's stock rows
    screensMultiplanHints();  // Multiplan, the Gandhar door's cursor, the hint shop
    screensPuzzles();         // Den's locker, Gandhar's door, Xachen's cartridges
    screensTerminals();       // the terminal family's display
    screensLift();            // the lift's description box
    screensPropertyTail();    // the tail of Actor_SetProperty
    screensHuds();            // the fight HUD, the breath gauge, the shoot HUD
    screensFps();             // the fps counter
    screensFades();           // the screen fades, over everything but...
    screensLoadingLogo();     // ...the loading logo, layer 15 over their 9
    screensFlicker();         // the flicker catcher
    return phase::Continue;
}
