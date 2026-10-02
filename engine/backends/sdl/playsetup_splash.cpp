// SPDX-License-Identifier: GPL-3.0-or-later
// THE SETUP: the splash screen, the player's damage, the frame's instruments.
// A stretch of what was `main`'s body, moved byte for byte by
// `todo/play-split.md` (2026-10-02); `PlayState::run` calls the sections in
// order. Returns -1 to go on, or the exit code `main` returns.
#include "playstate.h"

int PlayState::setupSplash() {
    const auto& fs = *fs_;
    // ---- THE SPLASH SCREEN ------------------------------------------
    //
    // `Game_Main` shows it between `Game_Start("aventure.scx")` and
    // `Game_RunLoop`, which is exactly here:
    //
    //     push offset aAventureScx_3 ; "aventure.scx"
    //     call sub_41B5A0            ; Game_Start
    //     push offset aImagesOmikronB ; "IMAGES\OMIKRON.BMP"
    //     call sub_420A20            ; <- the splash
    //     call sub_439310            ; Game_RunLoop
    //
    // and `sub_420A20` is: load the bitmap, blit it over the whole screen,
    // present, free it, and **`Sleep(0x1388)` - five seconds**. It is a
    // blocking sleep, so unlike the movies it is NOT skippable, and that is
    // reproduced rather than improved on. Events are pumped through the wait
    // only so the host can close the window; no key shortens it.
    if (!frames) {
        const omk::Surface splash =
            omk::surfaceFromBmp(fs.read("IMAGES/OMIKRON.BMP"));
        if (splash.valid()) {
            // The splash is a 640x480 file and `Blt` stretches, which is
            // how the original puts it on a bigger display too.
            omk::Surface sf(dispW, dispH, 0);
            omk::blt(sf, {0, 0, dispW, dispH}, splash,
                     {0, 0, splash.w, splash.h}, omk::kBltWait);
            present(sf);
            std::printf("splash: IMAGES/OMIKRON.BMP, 5 s (Sleep(0x1388) - "
                        "not skippable in the original either)\n");
            const Uint32 until = SDL_GetTicks() + 5000;
            while (SDL_GetTicks() < until) {
                omk::HostInput h;
                if (!front.pump(h)) break;      // the window closed
                present(sf);
                SDL_Delay(16);
            }
        } else {
            std::printf("no IMAGES/OMIKRON.BMP - no splash\n");
        }
    }

    std::printf("screen %d. arrows move, ENTER confirms, TAB closes, "
                "ESC opens the pause screen.\n", screenId);

    fb = omk::Surface(dispW, dispH, 0);
    n = 0;
    // WAIT FOR THE KEY THAT SKIPPED THE MOVIE TO COME UP. Any key now ends a
    // movie and ESC also quits, so one press did both: skipped the last movie
    // and closed the menu behind it - "0 frames presented", with the window
    // gone before it drew. A game edge-triggers; this is the frame-zero half.
    for (int guard = 0; guard < 300; ++guard) {
        if (!front.pump(host)) break;
        if (host.held.empty() && host.pad.buttons == 0) break;   // pad buttons too
        SDL_Delay(10);
    }
    lastMs = SDL_GetTicks();
    fpsSince = lastMs, fpsLastMs = lastMs, fpsWorst = 0;
    fpsFrames = 0;
    // Where `Script_Display3DSprite` puts a sprite: the active camera's
    // TARGET, read by the handler on every tick it runs (program.h has the
    // trace; the XYZ table it would prefer is never written). The runner is
    // handed the camera the LAST frame drew with - one frame behind, since
    // the script ticks before this frame's camera is settled.
    spriteAnchorSet = false;
    // The 30 Hz pacer's next deadline, in seconds on the performance counter
    // (see the cap at the bottom of this loop).
    paceNext = 0.0;
    // THE FRAME'S PHASES, timed - an instrument for the Vita (2026-09-18: the
    // city "unplayable", and the SLOW FRAME line below gives only the total).
    // Four spans on the performance counter: the simulation and the draw
    // submission up to the readback, the readback itself (on GLES the wait for
    // the GPU), the CPU compose after it, and the present with its swap.
    // Averaged over 60 frames and printed as one `frame phases:` line.
    phaseHz = static_cast<double>(SDL_GetPerformanceFrequency());
    phTop = 0.0, phRb0 = -1.0, phRb1 = -1.0;
    phN = 0;
    phGpu = 0;                          // frames presented from the GPU
    return -1;
}
