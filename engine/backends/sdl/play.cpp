// SPDX-License-Identifier: GPL-3.0-or-later
// THE LIVE FRONTEND - a window, a keyboard, and the ported framebuffer.
//
//     omk-play <gamedata> <tables/>                        the interface, live
//     omk-play <gamedata> <tables/> --scene Aapkayl        the SCENE VIEWER
//
// **`backends/sdl/` is the only place in the tree that includes an SDL header**
// - this file and, since `todo/play-split.md` S1c, `sdlfront.{h,cpp}`, the
// window, keyboard, pad and audio device - which is
// `docs/PORTING.md` A8 rule 2. (`backends/vulkan/vkrender.cpp` is the only one
// that includes Vulkan's, on the same terms and for the same reason; the rule
// is one dependency per backend file, not one backend file in total.) Rule 3 is the one that is not hygiene, and
// it decides what this file may do: SDL creates a window, reports keys and
// uploads a texture. It does **not** blit, scale, blend, lay out or draw text.
// Every pixel it shows was put there by `blt`, `fillQuad` and `drawRun` - the
// ported drawers, each already checked against the engine's own framebuffer -
// and letting SDL do any of that would make the checks test SDL while staying
// exactly as green.
//
// A8 names SDL3. Only SDL2 is installed on the machine this was written on, and
// the surface used here is a dozen calls that both versions have, so it builds
// against either and the Makefile prefers 3. That divergence is recorded in A8
// rather than left for someone to find.
#include "playshared.h"
#include "playstate.h"
#include "playframe.h"


int PlayState::run(int argc, char** argv) {
    OMK_HEAPCHECK("main");
    // THE SETUP, in order (`playsetup_<section>.cpp`, todo/play-split.md)
    int r = -1;
    if ((r = setupOptions(argc, argv)) != -1) return r;
    if ((r = setupBoot()) != -1) return r;
    if ((r = setupSession()) != -1) return r;
    if ((r = setupAdventure()) != -1) return r;
    if ((r = setupDevices()) != -1) return r;
    if ((r = setupBodies()) != -1) return r;
    if ((r = setupPlay()) != -1) return r;
    if ((r = setupSplash()) != -1) return r;
    // THE FRAME LOOP - `PlayState::step` in `playframe.cpp` (todo/play-split.md).
    for (;;) {
        const int stepped = step();
        if (stepped == -2) break;
        if (stepped >= 0) return stepped;
    }
    return finish();
}

// THE VIEWER, as one object (`todo/play-split.md` S3): what `main` held as
// locals is `PlayState`'s members, and `main`'s body is `PlayState::run`. On
// the heap, as its members are hundreds of containers and caches - the Vita's
// main thread has a small stack.
int main(int argc, char** argv) {
    const auto ps = std::make_unique<PlayState>();
    return ps->run(argc, argv);
}
