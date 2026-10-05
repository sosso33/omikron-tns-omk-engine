// SPDX-License-Identifier: GPL-3.0-or-later
// THE GPU WINDOW, CITRO3D - `PlayState::gpu...` for the Nintendo 3DS build
// (`todo/3ds-port.md` step 3). There is no GPU window: the world is drawn by
// the citro3d backend (`c3drender.h`) into its own offscreen target, and its
// `readback()` frame is composited and presented on the CPU exactly as the
// software reference's is - the SDL host's GL1 shape (`backends/sdl/
// playgpu_gl1.cpp`) and the world-harness slot `--world-vulkan` uses.
//
// `--software` keeps the software reference, for laying the two side by side
// (a 3DS has no environment to set a variable in); so does a console where
// `init()` says no.
#include "playframe.h"
#include "c3drender.h"

bool PlayState::gpuWindowBuild() const { return false; }
void PlayState::gpuOpenWindow() {}
void PlayState::gpuOpenWorldHarness() {
    if (forceSoftware) {
        std::printf("renderer: --software - the software reference\n");
        return;
    }
    omk::Renderer* r = omk::makeC3dRenderer();
    if (texFilter > 0) r->setTextureFilter(texFilter);
    if (r->init(dispW, dispH)) {
        worldVk = r;
        std::printf("renderer: the world through %s (offscreen; the frame is presented on the CPU)\n",
                    r->name());
    } else {
        delete r;
        std::printf("renderer: no citro3d - the software reference\n");
    }
}
bool PlayState::gpuPresentSurface(const omk::Surface&) { return false; }
void PlayState::gpuPresentVerify(bool&) {}
void PlayState::gpuPresentOverlay(bool&) {}
void PlayState::gpuPresentWorld(bool&) {}
bool PlayState::gpuWorldOnWindow() { return false; }
void PlayState::gpuOverlayDecision(const char*&) {}
void PlayState::gpuResize(int, int, bool&) {}
bool PlayState::gpuDriverRow(std::vector<std::string>&) { return false; }
void PlayState::gpuReportTimings() {}
void PlayState::gpuSlowFrameReport() {}
void PlayState::gpuFinishReport() {}
void PlayState::gpuVerifyWorldPicture() {}
