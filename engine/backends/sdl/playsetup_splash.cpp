// SPDX-License-Identifier: GPL-3.0-or-later
// THE SETUP: the splash screen, the player's damage, the frame's instruments.
// A stretch of `main`'s body, split out by `todo/play-split.md`;
// `PlayState::run` calls the sections in
// order. Returns -1 to go on, or the exit code `main` returns.
#include "playstate.h"
#include "platform/profile.h"   // OMK_PROFILE: the instruments' cost probe

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
            const std::uint32_t until = front.ticksMs() + 5000;
            while (front.ticksMs() < until) {
                omk::HostInput h;
                if (!front.pump(h)) break;      // the window closed
                present(sf);
                front.delayMs(16);
            }
        } else {
            std::printf("no IMAGES/OMIKRON.BMP - no splash\n");
        }
    }

    std::printf("screen %d. arrows move, ENTER confirms, TAB closes, "
                "ESC opens the pause screen.\n", screenId);

    fb = omk::Surface(dispW, dispH, 0);
    frameNo = 0;
    // WAIT FOR THE KEY THAT SKIPPED THE MOVIE TO COME UP. Any key now ends a
    // movie and ESC also quits, so one press did both: skipped the last movie
    // and closed the menu behind it - "0 frames presented", with the window
    // gone before it drew. A game edge-triggers; this is the frame-zero half.
    for (int guard = 0; guard < 300; ++guard) {
        if (!front.pump(host)) break;
        if (host.held.empty() && host.pad.buttons == 0) break;   // pad buttons too
        front.delayMs(10);
    }
    lastMs = front.ticksMs();
    fpsMeter.since = lastMs, fpsMeter.lastMs = lastMs, fpsMeter.worst = 0;
    fpsMeter.frames = 0;
    // Where `Script_Display3DSprite` puts a sprite: the active camera's
    // TARGET, read by the handler on every tick it runs (program.h has the
    // trace; the XYZ table it would prefer is never written). The runner is
    // handed the camera the LAST frame drew with - one frame behind, since
    // the script ticks before this frame's camera is settled.
    spriteAnchorSet = false;
    // The 30 Hz pacer's next deadline, in seconds on the performance counter
    // (see the cap at the bottom of this loop).
    paceNext = 0.0;
    // THE FRAME'S PHASES, timed - an instrument for the Vita, where the city
    // runs slowly and the SLOW FRAME line below gives only the total.
    // Four spans on the performance counter: the simulation and the draw
    // submission up to the readback, the readback itself (on GLES the wait for
    // the GPU), the CPU compose after it, and the present with its swap.
    // Averaged over 60 frames and printed as one `frame phases:` line.
    phaseHz = static_cast<double>(front.perfFrequency());
#if OMK_PROFILE
    {
        // WHAT THE INSTRUMENTS COST on this machine, so a span made of many
        // small timed calls can be read for what it is: one clock read, and
        // one add into a named span (a name looked up by its pointer)
        constexpr int kN = 2000;
        const double t0 = phaseNow();
        double sink = 0.0;
        for (int k = 0; k < kN; ++k) sink += phaseNow();
        const double t1 = phaseNow();
        for (int k = 0; k < kN; ++k) phases.span["instrument probe"] += 1e-9;
        const double t2 = phaseNow();
        phases.span.erase("instrument probe");
        // ...and the ALLOCATOR under every container and string: one small
        // block made and freed, alone and in a burst of kN held at once (on
        // classic Mac OS each is a Memory Manager `NewPtr` / `DisposePtr`)
        for (int k = 0; k < kN; ++k) { volatile char* p = new char[32]; p[0] = 1; delete[] p; }
        const double t3 = phaseNow();
        std::vector<char*> burst(kN);
        for (auto& p : burst) p = new char[32];
        for (auto* p : burst) delete[] p;
        const double t4 = phaseNow();
        std::printf("profile: a clock read costs %.2f us, a named span add %.2f us, a 32-byte "
                    "new+delete %.2f us (%.2f in a burst of %d)%s\n",
                    (t1 - t0) * 1e6 / kN, (t2 - t1) * 1e6 / kN, (t3 - t2) * 1e6 / kN,
                    (t4 - t3) * 1e6 / kN, kN, sink < 0 ? " " : "");
    }
#endif
    phases.top = 0.0, phases.rb0 = -1.0, phases.rb1 = -1.0;
    phases.n = 0;
    phases.gpu = 0;                          // frames presented from the GPU
    return phase::Continue;
}
