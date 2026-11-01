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
// is drawn in the window's own AGL context (`backends/gl1/gl1host.h`). So is
// the SDL build's on Mac OS X (`OMK_GL1_SDL_WINDOW`, `ppc-darwin.mk`): its
// window carries a GL context (`backends/gl1/gl1sdlhost.cpp`) and the world is
// drawn and presented there by the same paths - `OMK_GL1_OFFSCREEN=1` keeps
// the offscreen target and its readback, for laying the two side by side.
#include "playframe.h"
#include "../gl1/gl1render.h"
#include "platform/env.h"

#include <cstdlib>
#include <cstring>

// THE AGL BUILD PRESENTS STRAIGHT (todo/cpu-vs-original.md tier B): its world
// is drawn in the window's back buffer, so a frame nothing is drawn over is
// swapped as it stands and a frame with the interface over it is blended there
// - the GLES window's two paths, in fixed function (`gl1PresentWorld`,
// `gl1PresentOverlay`). The SDL build's GL window does the same; where SDL
// gives none it draws offscreen and keeps the CPU composite.
// `OMK_GL1_COMPOSITE=1` / `--gl1-composite` keep the composite on a window
// too, for laying the two side by side.
#if defined(OMK_GL1_AGL)
#  include "../gl1/gl1host.h"
bool PlayState::gpuWindowBuild() const { return true; }
#elif defined(OMK_GL1_SDL_WINDOW)
#  include "../gl1/gl1host.h"
#  include "../gl1/gl1sdlhost.h"
#  include "sdlfront.h"
bool PlayState::gpuWindowBuild() const { return omk::sdlGl1Window(); }
#else
bool PlayState::gpuWindowBuild() const { return false; }
#endif
void PlayState::gpuOpenWindow() {
#if defined(OMK_GL1_SDL_WINDOW)
    // the GL window, unless the run asks for the software reference or the
    // offscreen target (the refusals `gpuOpenWorldHarness` makes as well)
    const char* e = omk::envStr("OMK_GL1");
    if ((e && std::strcmp(e, "0") == 0) || forceSoftware || omk::envFlag("OMK_GL1_OFFSCREEN")) return;
    omk::sdlGl1OpenWindow(omk::sdlFrontend(front), dispW, dispH);
#endif
}
void PlayState::gpuOpenWorldHarness() {
    const char* e = omk::envStr("OMK_GL1");
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
    omk::gl1SetFullTextures(r, fullTexturesFlag);
    omk::gl1SetFogUnit(r, gl1FogUnit);
    omk::gl1SetNoLock(r, gl1NoLock);
    if (r->init(dispW, dispH)) {
        worldVk = r;
#if defined(OMK_GL1_ON_WINDOW)
        if (omk::gl1OnWindow(r))
            std::printf("renderer: the world through %s (the window's back buffer, presented "
                        "STRAIGHT - the interface blended over it on the GPU; --gl1-composite "
                        "reads it back and composes on the CPU)\n", r->name());
        else
#endif
        std::printf("renderer: the world through %s (offscreen; the frame is presented on the CPU)\n",
                    r->name());
        // the window was opened as "(software)" before this renderer existed;
        // the fps counter takes its base title from the window, so say it here
        front.setWindowTitle("OMK Engine (OpenGL 1.x)");
    } else {
        delete r;
        std::printf("renderer: no fixed-function OpenGL - the software reference\n");
    }
}
// a frame composed on the CPU: through the SDL build's GL window, where it has
// one (the window has no `SDL_Renderer` then); the AGL frontend presents its own
bool PlayState::gpuPresentSurface(const omk::Surface& pic) {
#if defined(OMK_GL1_SDL_WINDOW)
    if (omk::sdlGl1Window()) {
        omk::sdlGl1PresentFrame(pic);
        return true;
    }
#endif
    (void)pic;
    return false;
}
void PlayState::gpuPresentVerify(bool&) {}
#if defined(OMK_GL1_ON_WINDOW)
// THE WHITE-SCREEN PROBE (`todo/handoff-classic-mac.md` 3f steps 2j/2k):
// the first frame of every present path and every media line, and the 15th
// after it, log what the card drew (`gl1PresentOverlay`). The cause was the
// fog's colour sum left on (`Gl1Renderer::end`); the probe stays until the
// iBook confirms the fix.
namespace {
bool g_ovProbe = false;
}  // namespace
void PlayState::gpuPresentOverlay(bool& presentedWorld) {
    if (!overlayFrame || !worldVk) return;
    // the key resolved into the two planes (as `playgpu_gles.cpp` does): C
    // into `fb`, M beside it, on the rows a pass touched - and a FLAG a row
    // (step 3b, as GLES's): a row no pass touched keeps an all-zero mask,
    // written once when it stops being touched rather than every frame.
    // ...and ONLY THE BLOCKS a pass touched (step 3g, `OverlayPlanes::
    // rowBlocks`): an untouched pixel stays the key, which the overlay sends
    // as the same transparent texel the planes' c = 0, m = 255 gave; the
    // mask of a block touched last frame and not this one is zeroed once
    OMK_ZONE("gl1: overlay glue");
    static std::vector<std::uint8_t> ovMask, ovMaskRow;
    static std::vector<std::uint64_t> ovMaskBlocks;   // the blocks whose mask may be nonzero
    omk::resolveOverlayPlanes(fb.px.data(), fb.w, fb.h, g_ov, ovMask, ovMaskRow, ovMaskBlocks);
    int ww = 0, wh = 0;
    omk::gl1WindowSize(ww, wh);
    presentedWorld = omk::gl1PresentOverlay(worldVk, fb, ovMask.data(), ovMaskRow.data(), ovFade,
                                            ww, wh, g_ovProbe);
    static bool refusedTold = false;
    if (!presentedWorld && !refusedTold) {
        refusedTold = true;
        std::printf("gl1: the overlay REFUSED at frame %ld (window %dx%d, frame %dx%d) - "
                    "composed on the CPU\n", frameNo, ww, wh, fb.w, fb.h);
    }
}
void PlayState::gpuPresentWorld(bool& presentedWorld) {
    if (presentedWorld || !worldVk) return;
    int ww = 0, wh = 0;
    omk::gl1WindowSize(ww, wh);
    presentedWorld = omk::gl1PresentWorld(worldVk, ww, wh);
    static bool refusedTold = false;
    if (!presentedWorld && !refusedTold) {
        refusedTold = true;
        std::printf("gl1: the straight present REFUSED at frame %ld (window %dx%d, frame %dx%d) - "
                    "composed on the CPU\n", frameNo, ww, wh, fb.w, fb.h);
    }
}
bool PlayState::gpuWorldOnWindow() {
    static const bool composite = omk::envFlag("OMK_GL1_COMPOSITE");
    if (!worldVk || world_ != worldVk || composite || gl1Composite || !omk::gl1OnWindow(worldVk))
        return false;
    // ...and only a window that HOLDS the frame: the world is drawn at the
    // frame's size in the back buffer's corner and scaled up on the card to
    // a larger window (the SDL build's fullscreen, `Gl1Renderer::fitToWindow`);
    // a SMALLER one is composed on the CPU and fitted there - the straight
    // present would refuse it, and the frame would go out black.
    // `OMK_GL1_NO_FIT=1` composes every scaled frame on the CPU again (the
    // emulated Radeon samples the fit's copied texture with its bytes out of
    // order - `todo/handoff-classic-mac.md` 3h)
    static const bool noFit = omk::envFlag("OMK_GL1_NO_FIT");
    OMK_ZONE("gl1: window size");
    int ww = 0, wh = 0;
    omk::gl1WindowSize(ww, wh);
    if (ww == fb.w && wh == fb.h) return true;
    return !noFit && ww >= fb.w && wh >= fb.h;
}
void PlayState::gpuOverlayDecision(const char*& keep) {
    static const bool noOverlay = omk::envFlag("OMK_NO_OVERLAY");
    overlayFrame = softGate && !noOverlay && !verifyGpuPresent && gpuWorldOnWindow();
    if (overlayFrame) {
        static std::string overlayWhy;
        overlayWhy = std::string("overlay (") + keep + ")";
        keep = overlayWhy.c_str();
    }
    // THE PATH, logged where it CHANGES: straight, the overlay, or the CPU
    // composite (a readback) and why. White screens were seen in cutscenes
    // and never in adventure mode on a real iBook G3 (Rage 128, Mac OS X
    // 10.2), and a log is all that machine gives back; a build without the
    // instruments has no other per-frame count.
    static std::string lastPath;
    const std::string path = keep ? keep : "straight";
    // the white-screen probe: the first frame of every path and of every
    // media line, and the 15th after it
    static std::string lastLine;
    static long probeAt = -1;
    const bool mediaLine = path == "overlay (media line)";
    if (path != lastPath) {
        std::printf("gl1 present: %s from frame %ld\n", path.c_str(), frameNo);
        lastPath = path;
        probeAt = frameNo;
    }
    if (mediaLine && mediaText != lastLine) {
        lastLine = mediaText;
        probeAt = frameNo;
    }
    g_ovProbe = overlayFrame && (frameNo == probeAt || frameNo == probeAt + 15);
}
#else
void PlayState::gpuPresentOverlay(bool&) {}
void PlayState::gpuPresentWorld(bool&) {}
bool PlayState::gpuWorldOnWindow() { return false; }
void PlayState::gpuOverlayDecision(const char*&) {}
#endif
void PlayState::gpuResize(int, int, bool&) {}
bool PlayState::gpuDriverRow(std::vector<std::string>&) { return false; }
bool PlayState::gpuRelinkLighting(bool, bool) { return false; }   // nothing to relink
bool PlayState::gpuRetarget(int, int, float, float) { return false; }   // no target to remake
void PlayState::gpuReportTimings() {}
void PlayState::gpuSlowFrameReport() {}
void PlayState::gpuFinishReport() {}
void PlayState::gpuVerifyWorldPicture() {}
