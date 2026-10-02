// SPDX-License-Identifier: GPL-3.0-or-later
// THE GPU WINDOW, GL1 - `PlayState::gpu...` for the fixed-function OpenGL 1.x
// variant (`backends/gl1/`, `todo/classic-mac-port-1999.md` step 4). There is
// still no GPU WINDOW: the world is drawn by the fixed-function backend into
// its own offscreen framebuffer, and its `readback()` frame is composited and
// presented on the CPU exactly as the software reference's is - the world
// harness slot `--world-vulkan` uses. Everything else is `playgpu_none.cpp`.
//
// `OMK_GL1=0` keeps the software reference, for laying the two side by side;
// so does a machine with no accelerated OpenGL, where `init()` says no.
#include "playframe.h"
#include "../gl1/gl1render.h"

#include <cstdlib>
#include <cstring>

bool PlayState::gpuWindowBuild() const { return false; }
void PlayState::gpuOpenWindow() {}
void PlayState::gpuOpenWorldHarness() {
    const char* e = std::getenv("OMK_GL1");
    if (e && std::strcmp(e, "0") == 0) {
        std::printf("renderer: OMK_GL1=0 - the software reference\n");
        return;
    }
    omk::Renderer* r = omk::makeGl1Renderer();
    if (r->init(dispW, dispH)) {
        worldVk = r;
        std::printf("renderer: the world through %s (offscreen; the frame is presented on the CPU)\n",
                    r->name());
    } else {
        delete r;
        std::printf("renderer: no fixed-function OpenGL - the software reference\n");
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
