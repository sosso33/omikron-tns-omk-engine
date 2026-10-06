// SPDX-License-Identifier: GPL-3.0-or-later
// THE GPU WINDOW, CITRO3D - `PlayState::gpu...` for the Nintendo 3DS build
// (`todo/3ds-port.md` step 3). The world is drawn by the citro3d backend
// (`c3drender.h`) into its own offscreen target; a frame with anything over
// it is read back and composited on the CPU as the software reference's is
// (the SDL host's GL1 shape), and a frame with nothing over it goes straight
// to the top screen (3c, `gpuPresentWorld` below).
//
// `--software` keeps the software reference, for laying the two side by side
// (a 3DS has no environment to set a variable in); so does a console where
// `init()` says no.
#include "playframe.h"
#include "c3drender.h"

// THE STRAIGHT PRESENT (3c): the build decides, frame by frame, whether
// anything is drawn over the world - the GLES window's "present pass", every
// gate in `playframe_world_draw.cpp` - and a frame nothing is drawn over goes
// to the top screen already halved by the GPU (`c3dPresentHalf`), with no
// readback, no composite and no CPU halving. The rest keep the CPU path.
bool PlayState::gpuWindowBuild() const { return true; }
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
void PlayState::gpuPresentWorld(bool& presentedWorld) {
    if (presentedWorld || !worldVk) return;
    static omk::Surface screen(400, 240, 0);
    if (!omk::c3dPresentHalf(worldVk, gpuVy, gpuVh, screen)) return;
    front.present(screen);          // 400x240: the frontend copies it 1:1, its stats and capture as ever
    presentedWorld = true;
}
bool PlayState::gpuWorldOnWindow() { return worldVk && world_ == worldVk; }
void PlayState::gpuOverlayDecision(const char*&) {}
void PlayState::gpuResize(int, int, bool&) {}
bool PlayState::gpuDriverRow(std::vector<std::string>&) { return false; }
// every 60 frames, beside the phases line: the backend's own counts
void PlayState::gpuReportTimings() { if (worldVk) omk::c3dReport(worldVk, n); }
void PlayState::gpuSlowFrameReport() {}
void PlayState::gpuFinishReport() {}
void PlayState::gpuVerifyWorldPicture() {}
