// SPDX-License-Identifier: GPL-3.0-or-later
// `WinMain` -> `Game_Main` -> `Game_RunLoop` - the game's own boot chain.
//
// Everything below this line has been ported in slices and proved one at a
// time; nothing until now JOINED them. This is the join, and it is headless:
// no window, no DirectDraw, no audio. What it reproduces is the sequence and
// the clock, which is the part that has an oracle.
//
// The chain, from docs/BOOT.md:
//
//     WinMain          parse the command line: WINDOW, NOFMV, CONFIG
//       Game_Main
//         Game_Init
//         Movie_Play  FLIS\EIDOS.mpg / QUANTIC.mpg / GAME.mpg
//         Game_Start("aventure.scx")     the boot scene - the MAIN MENU is a
//                                        scene like any other, not a state
//         Game_RunLoop                   ... until WM_QUIT
//         Game_Shutdown
//
// Two things the loop must get right, and both are in `Game_Frame`:
//
//   * the DELTA is `30.0 / fps`, clamped at 3.0 - measured in thirtieths of a
//     second, so one unit IS one frame at 30 Hz. Every authored period in the
//     data is in those units.
//   * a resume RE-BASELINES the timers rather than integrating the gap. A
//     headless run has no idle, but the flag is modelled because the frame
//     function's first act depends on it.
#pragma once

#include <string>
#include <vector>

namespace omk {

struct BootOptions {
    std::string root;             // the `gamedata/` tree
    std::string tables;           // the `tables/` directory
    bool  playMovies = true;      // cleared by NOFMV
    bool  windowed   = false;     // set by WINDOW
    int   frames     = 0;         // 0 = until the loop ends
    bool  deriveUi   = true;      // walk the start menu for its own answer
    bool  scenes     = false;     // make each area's .SCX resident
};

BootOptions parseCommandLine(int argc, char** argv);

struct BootReport {
    bool  ok = false;
    std::string why;
    // what the movie stage did - see docs/BOOT.md 2 for the two skips
    int   moviesFound = 0;
    bool  moviesSkipped = false;
    // the boot scene, and what the session then decided
    // `aventure.scx` is NOT a menu and not a location: it is the global
    // effect and sound library - 20 sprites and 53 sound registrations, the
    // player's own footsteps, breathing, jumps and impacts - loaded once so
    // every scene can reference them.
    std::string bootScene;
    int   bootSprites = 0, bootSounds = 0;
    int   startArea = -1;         // IAM\START's +1414, not a literal
    // what the script's own `ui.open` asked for, and what walking it answered
    int   interfaceAnswer = -1;
    int   uiScreen = -1, uiVariable = -1;
    int   startupContexts = 0;
    int   conversations = 0;
    int   areasEntered = 0;
    int   framesRun = 0;
    std::vector<std::pair<std::string, int>> announced;
};

// Run the chain. `frames` bounds `Game_RunLoop`'s idle path, which is
// otherwise "until WM_QUIT" and a headless build has no message queue.
BootReport boot(const BootOptions& o);

}  // namespace omk
