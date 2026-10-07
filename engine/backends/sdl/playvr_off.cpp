// SPDX-License-Identifier: GPL-3.0-or-later
// VR, NOT BUILT - the `PlayState::vr*` methods as empty stubs for every build
// without OMK_VR (`todo/quest-port.md` §5): the Vita, the classic Mac, the 3DS,
// the PowerPC Mac, and the main Makefile's `VR=0`. Their build files compile
// every `backends/sdl/*.cpp`, so this file is how they link without knowing VR
// exists; the real methods are `backends/vr/playvr_<part>.cpp`.
#if !OMK_VR

#include "playframe.h"

#include <cstdio>
#include <cstring>

void PlayState::vrSetup(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (std::strncmp(argv[i], "--vr", 4) == 0) {
            std::printf("vr: not built (a build without OMK_VR) - %s ignored\n", argv[i]);
            return;
        }
}
void PlayState::vrAfterWorldCamera() {}
bool PlayState::vrWorldDraw(const omk::View&, const omk::MirrorPlane&) { return false; }
bool PlayState::vrHidesPlayer() const { return false; }
bool PlayState::vrDrawsBehindScreen(bool) { return false; }
bool PlayState::vrNoSideCull() const { return false; }
void PlayState::vrAdventureInput(std::uint32_t&) {}

#endif  // !OMK_VR
