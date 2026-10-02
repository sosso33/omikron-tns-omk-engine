// SPDX-License-Identifier: GPL-3.0-or-later
// THE SETUP: the interface sounds, the renderer, the GLES window, the music, the world.
// A stretch of what was `main`'s body, moved byte for byte by
// `todo/play-split.md` (2026-10-02); `PlayState::run` calls the sections in
// order. Returns -1 to go on, or the exit code `main` returns.
#include "playstate.h"

int PlayState::setupDevices() {
    const auto& fs = *fs_;
    auto& comp = *comp_;
    // ---- the INTERFACE SOUNDS.
    //
    // `docs/UI.md`: every screen names up to twelve, and the slots are
    // POSITIONAL - 0 is the selection move, 1 the confirm, 2 the screen
    // opening. The start menu's are 1, 2, 0, which the table resolves to
    // `men002`, `men003`, `men001`. Both halves were already lifted and
    // nothing was playing them.
    optWalkSeen = nullptr;   // the walk of 29 the options were opened for
    optEntered = false;                    // focused for this visit to panel 0x004CF420
    pendingDisplay = std::pair<int, int>{0, 0};   // options row 2, served between frames

    // fullscreen is a creation flag for whichever window comes up below
    front.setFullscreen(fullscreenFlag >= 0 ? fullscreenFlag != 0 : settings.fullscreen);
    comp.setDisplay(dispW, dispH);
    applyTextScale(dispW, dispH);
    std::printf("display %dx%d (the interface is authored at 640x480 and "
                "scaled by I2D_ScaleX/Y)\n", dispW, dispH);

    OMK_HEAPCHECK("before renderer");
    // ---- THE RENDERER ---------------------------------------------------
    //
    // Vulkan when the machine has it, the software reference otherwise - and
    // the fallback is not a courtesy, it is `PORTING` A1's closing rule: a bare
    // checkout with no SDK must build and pass the whole suite. `--software`
    // forces the reference, which is the half every check is written against.
    //
    // The window mode is the awkward part and it is Vulkan's, not a choice
    // here: a window carrying a Vulkan surface cannot also carry an
    // `SDL_Renderer`, so the texture-upload path and the swapchain are
    // mutually exclusive and the decision has to be made before the window
    // exists. The order below is likewise Vulkan's - the instance needs the
    // window system's extensions, the surface needs the instance, and the
    // device's queue choice needs the surface.
    //
    // **What the GPU draws here is the 3D only.** The interface, the subtitle
    // and the menu are the ported I2D layer and stay on the CPU (`renderer.h`
    // says why: a Blt is a memory copy and there is nothing a GPU would make
    // more correct), so a frame is composed as before - the 3D read back into
    // the framebuffer, the 2D drawn over it - and `presentSurface` uploads the
    // finished picture through the swapchain. That readback is the cost the
    // scene viewer's `--vulkan` avoids by presenting the attachment directly,
    // and it cannot be avoided while anything is composited on the CPU.
    vkWin = nullptr;
    vkRen = nullptr;
    worldVk = nullptr;   // --world-vulkan, the offscreen harness
#if defined(OMK_VULKAN)
    if (!forceSoftware && !worldVulkan) {
        if (SDL_Init(SDL_INIT_VIDEO) == 0) {
            vkWin = SDL_CreateWindow("OMK Engine (vulkan)",
                                     SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     dispW, dispH, SDL_WINDOW_VULKAN | front.windowFlags());
        }
        if (vkWin) {
            unsigned nx = 0;
            SDL_Vulkan_GetInstanceExtensions(vkWin, &nx, nullptr);
            std::vector<const char*> ext(nx);
            SDL_Vulkan_GetInstanceExtensions(vkWin, &nx, ext.data());
            omk::Renderer* vr = omk::makeVulkanRenderer();
            if (aaSamples > 1) vr->setMultisample(aaSamples);   // the enhancements
            if (texFilter > 0) vr->setTextureFilter(texFilter);
            if (texAniso > 1) vr->setAnisotropy(texAniso);
            if (ssaa > 1) vr->setSupersample(ssaa);
            omk::vulkanNeedExtensions(vr, ext.data(), nx);
            void* inst = omk::vulkanCreateInstance(vr);
            VkSurfaceKHR surf{};
            if (inst &&
                SDL_Vulkan_CreateSurface(vkWin, static_cast<VkInstance>(inst), &surf) &&
                omk::vulkanAttachSurface(vr, reinterpret_cast<unsigned long long>(surf)) &&
                vr->init(dispW, dispH)) {
                vkRen = vr;
                std::printf("renderer: VULKAN - %s\n", omk::vulkanDeviceName(vr));
            } else {
                delete vr;
                SDL_DestroyWindow(vkWin); vkWin = nullptr;
                std::printf("renderer: no Vulkan device - the software "
                            "reference\n");
            }
        }
    }
#endif
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
    glWin = nullptr;
    glRen = nullptr;
    (void)glWin;   // read only by the OMK_GLES blocks
#if defined(OMK_GLES)
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
            const Uint32 glWinFlags = SDL_WINDOW_OPENGL |
                (omk::envSet("OMK_NO_GPU_PRESENT") ? SDL_WINDOW_HIDDEN : front.windowFlags());
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
            if (gr->init(dispW, dispH)) {
                glRen = gr;
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
#endif
    if (!vkRen && !glRen && !front.open(dispW, dispH, "OMK Engine (software)")) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    if (!vkRen && !glRen) std::printf("renderer: the software reference\n");
    // F11 and the resolution row act on whichever window is up
    if (vkRen) front.attachWindow(vkWin);
    else if (glRen) front.attachWindow(glWin);
    if (front.fullscreen())
        std::printf("fullscreen: on - the %dx%d frame scaled to the desktop at its aspect "
                    "(F11 toggles)\n", dispW, dispH);
#if defined(OMK_VULKAN)
    // ...and the harness: a Vulkan renderer with no surface, for the WORLD
    // alone. `run_vulkan` and `shadow_probe` already prove the backend comes
    // up offscreen; this is the same thing inside the viewer.
    if (!vkRen && worldVulkan) {
        omk::Renderer* wv = omk::makeVulkanRenderer();
        if (wv && aaSamples > 1) wv->setMultisample(aaSamples);
        if (wv && texFilter > 0) wv->setTextureFilter(texFilter);
        if (wv && texAniso > 1) wv->setAnisotropy(texAniso);
        if (wv && ssaa > 1) wv->setSupersample(ssaa);
        if (wv && wv->init(dispW, dispH)) {
            worldVk = wv;
            std::printf("renderer: the world through VULKAN offscreen - %s "
                        "(a harness; the frame is still presented on the CPU)\n",
                        omk::vulkanDeviceName(wv));
        } else { delete wv; std::printf("--world-vulkan: no offscreen device\n"); }
    }
#endif
    if (aaSamples > 1)
        std::printf("aa: %dx MSAA - an ENHANCEMENT the original never had; %s\n", aaSamples,
                    vkRen ? "drawn by the Vulkan backend"
                          : "the software reference has none, --vulkan for it");
    if (texFilter > 0)
        std::printf("filter: %s%s - an ENHANCEMENT the original never had; %s\n",
                    omk::textureFilterName(texFilter), texAniso > 1 ? " with anisotropy" : "",
                    vkRen ? "drawn by the Vulkan backend"
                          : "the software reference has none, --vulkan for it");
    // One place that decides where a finished framebuffer goes, so the movies,
    // the splash and the frame loop cannot drift apart about it.
    glSwapMs = 0.0;   // SDL_GL_SwapWindow's share, for the phase line
    (void)glSwapMs;
    // The name field is real typing, so ask the host for characters. Not on
    // the Vita: there SDL answers with the system's on-screen keyboard, which
    // would cover the game from the first frame (`todo/vita-port.md` F4).
#if !defined(__vita__)
    SDL_StartTextInput();
#endif

    OMK_HEAPCHECK("before music");
    // ---- THE MUSIC.
    //
    // Which track and whether it loops are the SCRIPT's decisions: AREA 118's
    // startup script reaches `music.play 109, loop` before the `ui.open` that
    // raises the menu, so the music arrives the same way the menu does. The
    // area header's own default (`AREA +142`) is 0 here - silent - so nothing
    // but the script names it.
    //
    // The streaming and the LOOP live in `src/audio/music.h`, not here. A
    // frontend is a device; it must not be deciding when a track restarts.
    game.audio.adpcmTables = omk::AdpcmTables::loadJson(tb + "/adpcm.json");
    // The cutscene VOICES - `media.play` (op 92). `sub_41B200` plays one
    // through the morph streamer after a `Morph_Stop()`, so ONE at a time and
    // a second cuts the first (src/audio/voiceover.h).
    voiceLib.load(fs);

    OMK_HEAPCHECK("before world");
    // ---- THE WORLD ------------------------------------------------------
    //
    // What is on screen after the menu is the SCRIPT's answer, not this
    // file's, and it arrives in two halves the Session already resolves:
    //
    //   * the DECOR SET is the resident area header's `+88` - the `.3DO` stem
    //     `Area_LoadSet` builds `MESHES\DECORS\%s.3DO` from. AREA 118, the
    //     one `IAM\START` starts in, names `GRID`; its startup script ends
    //     with `area.goto 222`, which is `AIMPASSE`, so the set changes
    //     underneath without this loop asking for it.
    //   * the CAMERA is whichever id the script's last `camera.set` /
    //     `camera.set.wait` named, resolved through `Camera_FindWorld`'s three
    //     tables and moved over the frames its second field asks for
    //     (`src/o3de/worldcam.h`).
    //
    // So there is nothing here to choose. This loads what the Session names
    // and draws through the camera the Session hands it, at whatever the
    // display size is - a horizontal fov and `tanv = tanh / (W/H)`, so the
    // aspect is the window's and no resolution is baked in.
    //
    // **The letterbox is NOT applied**, and that is deliberate: the 1.818:1
    // strip is measured off DIALOGUE captures and a reader watching the
    // original confirmed it belongs to conversations and cutscenes, not to
    // free play. Imposing it here would be generalising a camera-mode property
    // to all rendering, which is the mistake `--letterbox` exists to avoid in
    // the scene viewer.
    //
    // **Known gaps, stated rather than hidden**: `RCamera` carries no ROLL, so
    // the 1155 world cameras with a non-zero one are drawn upright (4226 of
    // 5381 have roll 0, AREA 118's six among them); there are no characters,
    // no props and no `.SCX` scene objects in the picture, only the decor set;
    // and the frame is drawn by the software reference rasterizer, which
    // `PORTING` B6 carries as a reference implementation and not as a port.
    OMK_HEAPCHECK("before speaker");
    return -1;
}
