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
// The CLASSIC Mac build links this file too (`OMK_GL1_AGL`): there the world
// is drawn in the window's own AGL context (`backends/gl1/gl1host.h`).
#include "playframe.h"
#include "../gl1/gl1render.h"

#include <cstdlib>
#include <cstring>

// THE AGL BUILD PRESENTS STRAIGHT (todo/cpu-vs-original.md tier B): its world
// is drawn in the window's back buffer, so a frame nothing is drawn over is
// swapped as it stands and a frame with the interface over it is blended there
// - the GLES window's two paths, in fixed function (`gl1PresentWorld`,
// `gl1PresentOverlay`). The SDL host's GL1 draws offscreen and keeps the CPU
// composite. `OMK_GL1_COMPOSITE=1` / `--gl1-composite` keep it on AGL too, for
// laying the two side by side.
#if defined(OMK_GL1_AGL)
#  include "../gl1/gl1host.h"
bool PlayState::gpuWindowBuild() const { return true; }
#else
bool PlayState::gpuWindowBuild() const { return false; }
#endif
void PlayState::gpuOpenWindow() {}
void PlayState::gpuOpenWorldHarness() {
    const char* e = std::getenv("OMK_GL1");
    if (e && std::strcmp(e, "0") == 0) {
        std::printf("renderer: OMK_GL1=0 - the software reference\n");
        return;
    }
    // `--software` too: classic Mac OS has no environment to set OMK_GL1 in
    if (forceSoftware) {
        std::printf("renderer: --software - the software reference\n");
        return;
    }
    omk::Renderer* r = omk::makeGl1Renderer();
    if (texFilter > 0) r->setTextureFilter(texFilter);
    if (r->init(dispW, dispH)) {
        worldVk = r;
#if defined(OMK_GL1_AGL)
        std::printf("renderer: the world through %s (the window's back buffer, read back; "
                    "the composited frame drawn over it and swapped)\n", r->name());
#else
        std::printf("renderer: the world through %s (offscreen; the frame is presented on the CPU)\n",
                    r->name());
#endif
        // the window was opened as "(software)" before this renderer existed;
        // the fps counter takes its base title from the window, so say it here
        front.setWindowTitle("OMK Engine (OpenGL 1.x)");
    } else {
        delete r;
        std::printf("renderer: no fixed-function OpenGL - the software reference\n");
    }
}
bool PlayState::gpuPresentSurface(const omk::Surface&) { return false; }
void PlayState::gpuPresentVerify(bool&) {}
#if defined(OMK_GL1_AGL)
void PlayState::gpuPresentOverlay(bool& presentedWorld) {
    if (!overlayFrame || !worldVk) return;
    // the key resolved into the two planes (as `playgpu_gles.cpp` does): C
    // into `fb`, M beside it, on the rows a pass touched
    static std::vector<std::uint8_t> ovMask;
    if (ovMask.size() != fb.px.size()) ovMask.assign(fb.px.size(), std::uint8_t(0));
    for (int y = 0; y < fb.h; ++y) {
        const std::size_t o = static_cast<std::size_t>(y) * fb.w;
        if (!g_ov.rowInit[static_cast<std::size_t>(y)]) {
            std::fill(ovMask.begin() + static_cast<long>(o), ovMask.begin() + static_cast<long>(o + fb.w),
                      std::uint8_t(0));
            continue;
        }
        for (std::size_t i = o; i < o + static_cast<std::size_t>(fb.w); ++i) {
            if (fb.px[i] == kOverlayKey) { fb.px[i] = g_ov.c[i]; ovMask[i] = g_ov.m[i]; }
            else ovMask[i] = 0;
        }
    }
    int ww = 0, wh = 0;
    omk::gl1AglWindowSize(ww, wh);
    presentedWorld = omk::gl1PresentOverlay(worldVk, fb, ovMask.data(), ovFade, ww, wh);
}
void PlayState::gpuPresentWorld(bool& presentedWorld) {
    if (presentedWorld || !worldVk) return;
    int ww = 0, wh = 0;
    omk::gl1AglWindowSize(ww, wh);
    presentedWorld = omk::gl1PresentWorld(worldVk, ww, wh);
}
bool PlayState::gpuWorldOnWindow() {
    static const bool composite = omk::envSet("OMK_GL1_COMPOSITE");
    return worldVk && world_ == worldVk && !composite && !gl1Composite;
}
void PlayState::gpuOverlayDecision(const char*& keep) {
    static const bool noOverlay = omk::envSet("OMK_NO_OVERLAY");
    overlayFrame = softGate && !noOverlay && !verifyGpuPresent && gpuWorldOnWindow();
    if (overlayFrame) {
        static std::string overlayWhy;
        overlayWhy = std::string("overlay (") + keep + ")";
        keep = overlayWhy.c_str();
    }
}
#else
void PlayState::gpuPresentOverlay(bool&) {}
void PlayState::gpuPresentWorld(bool&) {}
bool PlayState::gpuWorldOnWindow() { return false; }
void PlayState::gpuOverlayDecision(const char*&) {}
#endif
void PlayState::gpuResize(int, int, bool&) {}
bool PlayState::gpuDriverRow(std::vector<std::string>&) { return false; }
void PlayState::gpuReportTimings() {}
void PlayState::gpuSlowFrameReport() {}
void PlayState::gpuFinishReport() {}
void PlayState::gpuVerifyWorldPicture() {}
