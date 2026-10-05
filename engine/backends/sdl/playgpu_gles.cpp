// SPDX-License-Identifier: GPL-3.0-or-later
// THE GPU WINDOW, GLES2 - `PlayState::gpu...` for the GLES variant, the
// Vita's (`todo/play-split.md` S5). Each body is the `#if defined(OMK_GLES)`
// arm it replaces, moved unchanged.
#include "playframe.h"
#include "playgpu_gles.h"
#include "sdlfront.h"

// The GLES window - this glue's, not the game's (the gateway: game code holds
// no host handle). One viewer a process, so one window.
static SDL_Window* glWin = nullptr;

bool PlayState::gpuWindowBuild() const { return true; }

void PlayState::gpuOpenWindow() {
    // ---- THE GLES2 WINDOW MODE (`todo/vita-port.md` F1) -------------------
    //
    // The PS Vita's renderer, and any host whose GPU speaks GLES2 / GL 2.1:
    // `-DOMK_GLES` with `backends/gles/glesrender.cpp` linked in. The same
    // shape as Vulkan's mode above - a window that carries a GL context
    // cannot also carry an `SDL_Renderer`, so this decides before the window
    // exists and `front.open` is skipped when it succeeds. What is presented is
    // the COMPOSED frame, the world read back and the interface drawn over it
    // on the CPU (`glesPresentSurface`); presenting the world without the
    // readback is G6. The window is whatever size the host gives - 960x544 on
    // a Vita - and the frame is scaled into it at its own aspect.
    if (!vkRen && !forceSoftware) {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) == 0) {
#if !defined(__APPLE__)
            // GLES 2 where the platform has it (the Vita's vitaGL, Linux,
            // WebGL); macOS's legacy GL 2.1 profile takes no attributes
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
            SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
            // A MEASUREMENT RUN'S WINDOW IS HIDDEN. GL needs a window for its
            // context, so `SDL_VIDEODRIVER=dummy` cannot keep this viewer off
            // the screen as it does the software one; a run that never
            // presents (`OMK_NO_GPU_PRESENT`) has no use for it being seen,
            // and on 2026-09-25 a batch of such runs put windows - one of them
            // playing the boot films - in front of the reader (CLAUDE.md 5).
            // `OMK_HIDDEN_WINDOW=1` hides it WITHOUT turning the GPU present
            // off: what a profile of the path a player takes needs
            // (todo/cpu-vs-original.md) - the readback path is not that path.
            const Uint32 glWinFlags = SDL_WINDOW_OPENGL |
                (omk::envSet("OMK_NO_GPU_PRESENT") || omk::envSet("OMK_HIDDEN_WINDOW")
                     ? SDL_WINDOW_HIDDEN : omk::sdlFrontend(front).windowFlags());
#if defined(OMK_SDL3)
            glWin = SDL_CreateWindow("OMK Engine (gles)", dispW, dispH, glWinFlags);
#else
            glWin = SDL_CreateWindow("OMK Engine (gles)", SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED, dispW, dispH, glWinFlags);
#endif
        }
        if (glWin && SDL_GL_CreateContext(glWin)) {
            SDL_GL_SetSwapInterval(1);
            omk::Renderer* gr = omk::makeGlesRenderer();
            if (texFilter > 0) gr->setTextureFilter(texFilter);   // the enhancements
            if (texAniso > 1) gr->setAnisotropy(texAniso);
            if (ssaa > 1) gr->setSupersample(ssaa);
            if (gr->init(dispW, dispH)) {
                glRen = gr;
                omk::sdlFrontend(front).attachWindow(glWin);   // F11 and row 2 act on it
                std::printf("renderer: GLES2 - %s\n", gr->name());
                // G4 (todo/vita-port.md): the tie is ~1 ms of CPU on an M1 and
                // ~50 in a console's city; whether the Vita's 16-bit depth
                // shows the coincident faces without it is to be LOOKED at
                if (noTieFlag) {
                    omk::glesSetDepthTie(gr, false);
                    std::printf("renderer: the depth tie is OFF (--no-tie)\n");
                }
            } else {
                delete gr;
            }
        }
        if (!glRen) {
            if (glWin) { SDL_DestroyWindow(glWin); glWin = nullptr; }
            std::printf("renderer: no GL context (%s) - the software reference\n",
                        SDL_GetError());
        }
    }
}

void PlayState::gpuOpenWorldHarness() {}

bool PlayState::gpuPresentSurface(const omk::Surface& pic) {
    if (glRen) {
        int ww = 0, wh = 0;
#if defined(OMK_SDL3)
        SDL_GetWindowSizeInPixels(glWin, &ww, &wh);
#else
        SDL_GL_GetDrawableSize(glWin, &ww, &wh);
#endif
        omk::glesPresentSurface(glRen, pic, ww, wh);
        const auto sw0 = SDL_GetPerformanceCounter();
        SDL_GL_SwapWindow(glWin);
        glSwapMs += static_cast<double>(SDL_GetPerformanceCounter() - sw0) * 1000.0 /
                    static_cast<double>(SDL_GetPerformanceFrequency());
        return true;
    }
    return false;
}

void PlayState::gpuPresentVerify(bool& presentedWorld) {
    // `OMK_VERIFY_GPU_PRESENT` on the GLES window: present the world
    // directly, read the window back and compare it byte for byte with
    // the CPU frame composed beside it - the Vulkan verify's GLES twin.
    // Only where the window is the frame's own size (1:1).
    if (gpuFrame && verifyGpuPresent && glRen) {
        int ww = 0, wh = 0;
#if defined(OMK_SDL3)
        SDL_GetWindowSizeInPixels(glWin, &ww, &wh);
#else
        SDL_GL_GetDrawableSize(glWin, &ww, &wh);
#endif
        if (ww == fb.w && wh == fb.h &&
            omk::glesPresentWorld(glRen, gpuVy, gpuVh, fb.w, fb.h, ww, wh)) {
            static std::vector<unsigned char> winPic;
            static long compared = 0, differing = 0;
            omk::glesWindowPicture(glRen, ww, wh, winPic);
            // compared in 565: the driver's 565 -> 888 expansion is
            // its own (rounding on a Mac), so an 888 compare reports
            // the driver (todo/vita-port.md, the 113303 differences)
            long diff = 0;
            for (std::size_t i = 0; i < fb.px.size(); ++i) {
                const unsigned char* q = &winPic[4 * i];
                const int r5 = (q[0] * 31 + 127) / 255, g6 = (q[1] * 63 + 127) / 255,
                          b5 = (q[2] * 31 + 127) / 255;
                if (static_cast<std::uint16_t>((r5 << 11) | (g6 << 5) | b5) != fb.px[i]) ++diff;
            }
            ++compared;
            if (diff) ++differing;
            if (diff || compared % 30 == 0)
                std::printf("gles present verify: frame %ld, %ld pixels differ; %ld of %ld frames differed\n",
                            n, diff, differing, compared);
            SDL_GL_SwapWindow(glWin);
            presentedWorld = true;
        }
    }
}

void PlayState::gpuPresentOverlay(bool& presentedWorld) {
    if (overlayFrame && glRen) {
        // the key resolves into the two planes: C in `fb`, M beside it
        // - only on the rows a pass touched; an untouched key pixel stays
        // the key, which the shader reads as "the world" by itself, so
        // M is zero everywhere else and mostly never changes
        static std::vector<std::uint8_t> ovMask, ovMaskRow;
        if (ovMask.size() != fb.px.size()) {
            ovMask.assign(fb.px.size(), std::uint8_t(0));
            ovMaskRow.assign(static_cast<std::size_t>(fb.h), 0);
        }
        for (int y = 0; y < fb.h; ++y) {
            const std::size_t o = static_cast<std::size_t>(y) * fb.w;
            if (!g_ov.rowInit[static_cast<std::size_t>(y)]) {
                if (ovMaskRow[static_cast<std::size_t>(y)]) {
                    std::fill(ovMask.begin() + o, ovMask.begin() + o + fb.w, std::uint8_t(0));
                    ovMaskRow[static_cast<std::size_t>(y)] = 0;
                }
                continue;
            }
            ovMaskRow[static_cast<std::size_t>(y)] = 1;
            for (std::size_t i = o; i < o + static_cast<std::size_t>(fb.w); ++i) {
                if (fb.px[i] == kOverlayKey) { fb.px[i] = g_ov.c[i]; ovMask[i] = g_ov.m[i]; }
                else ovMask[i] = 0;
            }
        }
        int ww = 0, wh = 0;
#if defined(OMK_SDL3)
        SDL_GetWindowSizeInPixels(glWin, &ww, &wh);
#else
        SDL_GL_GetDrawableSize(glWin, &ww, &wh);
#endif
        presentedWorld = omk::glesPresentOverlay(glRen, fb, ovMask.data(), ovMaskRow.data(), ovFade, gpuVy, gpuVh, ww, wh);
        if (presentedWorld) {
            static const char* winDump = std::getenv("OMK_GLES_WINDUMP");
            if (winDump && frames && n + 1 >= frames) {
                std::vector<unsigned char> pic;
                omk::glesWindowPicture(glRen, ww, wh, pic);
                if (omk::safeOutputPath(winDump)) {
                    std::ofstream o(winDump, std::ios::binary);
                    o.write(reinterpret_cast<const char*>(pic.data()), static_cast<std::streamsize>(pic.size()));
                }
            }
            SDL_GL_SwapWindow(glWin);
        }
    }
}

void PlayState::gpuPresentWorld(bool& presentedWorld) {
    if (!presentedWorld && glRen) {
        int ww = 0, wh = 0;
#if defined(OMK_SDL3)
        SDL_GetWindowSizeInPixels(glWin, &ww, &wh);
#else
        SDL_GL_GetDrawableSize(glWin, &ww, &wh);
#endif
        presentedWorld = omk::glesPresentWorld(glRen, gpuVy, gpuVh, fb.w, fb.h, ww, wh);
        if (presentedWorld) {
            const auto sw0 = SDL_GetPerformanceCounter();
            SDL_GL_SwapWindow(glWin);
            glSwapMs += static_cast<double>(SDL_GetPerformanceCounter() - sw0) * 1000.0 /
                        static_cast<double>(SDL_GetPerformanceFrequency());
        }
    }
}

bool PlayState::gpuWorldOnWindow() {
    omk::Renderer& world = *world_;
    bool onVulkan = false;
    if (glRen && &world == glRen) onVulkan = true;
    return onVulkan;
}

void PlayState::gpuOverlayDecision(const char*& keep) {
    omk::Renderer& world = *world_;
    {
        static const bool noOverlay = std::getenv("OMK_NO_OVERLAY") != nullptr;
        overlayFrame = softGate && !noOverlay && !verifyGpuPresent &&
                       glRen && &world == glRen;
        if (overlayFrame) {
            // the statistics name the soft gate behind the overlay
            static std::string overlayWhy;
            overlayWhy = std::string("overlay (") + keep + ")";
            keep = overlayWhy.c_str();
        }
    }
}

void PlayState::gpuResize(int, int, bool&) {}
bool PlayState::gpuDriverRow(std::vector<std::string>&) { return false; }

void PlayState::gpuReportTimings() {
    if (glRen) {
        double g[4];
        omk::glesTakeTimings(g);
        std::printf("frame %ld gles (ms, mean of 60): glReadPixels %.1f, to-565 %.1f, "
                    "texture upload %.1f, present draw %.1f, swap %.1f; "
                    "%.0f buffer patches a frame (%.0f the depth tie's)\n", n,
                    g[0] / 60.0, g[1] / 60.0, g[2] / 60.0, g[3] / 60.0, glSwapMs / 60.0,
                    omk::glesTakePatches() / 60.0, omk::glesTakeTiePatches() / 60.0);
        {
            // the draw-state cache's work (todo/optimization.md step 17)
            long sc[3];
            omk::glesTakeStateCalls(sc);
            std::printf("frame %ld gles state (a frame, mean of 60): %.0f draws, "
                        "%.0f state calls made, %.0f skipped\n", n,
                        sc[0] / 60.0, sc[1] / 60.0, sc[2] / 60.0);
        }
        {
            double gw[7];
            omk::glesTakeWindow(gw);
            std::printf("frame %ld gles world (a frame, mean of 60): %.1f vertex uploads, "
                        "%.1f of them whole, %.1f streamed, %.0f KB sent, %.1f ms; draws %.1f ms, "
                        "ties %.1f ms\n", n,
                        gw[0] / 60.0, gw[5] / 60.0, gw[6] / 60.0, gw[1] / 60.0, gw[2] / 60.0,
                        gw[3] / 60.0, gw[4] / 60.0);
        }
        std::printf("frame %ld overlay: %ld plane rows re-sent in 60 frames\n", n,
                    omk::glesTakeOverlayRows(glRen));
        glSwapMs = 0.0;
    }
}

void PlayState::gpuSlowFrameReport() {
    if (glRen) std::printf("; %s", omk::glesFrameReport().c_str());
}

void PlayState::gpuFinishReport() {
    // the buffers of geometries that are gone (todo/optimization.md step 26)
    if (glRen) {
        long gs[3];
        omk::glesGeometryStats(glRen, gs);
        std::printf("gles: %ld geometries released over the run, %ld vertex buffers and %ld "
                    "posed buffers held at the end\n", gs[0], gs[1], gs[2]);
    }
}

void PlayState::gpuVerifyWorldPicture() {}
