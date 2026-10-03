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
    glWin = nullptr;
    glRen = nullptr;
    (void)glWin;   // read by the GLES window's file
    // THE GPU WINDOW, per backend (`playgpu_<backend>.cpp`, todo/play-split.md
    // S5): Vulkan's swapchain, or a GLES2 context, or neither - and then the
    // software reference's window below.
    gpuOpenWindow();
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
    gpuOpenWorldHarness();   // --world-vulkan
    if (aaSamples > 1)
        std::printf("aa: %dx MSAA - an ENHANCEMENT the original never had; %s\n", aaSamples,
                    vkRen ? "drawn by the Vulkan backend"
                          : "the software reference has none, --vulkan for it");
    // The filter is the DEVICE's (docs/ASSETS.md 4): bilinear is what the
    // original's hardware arm set and the default, nearest its software
    // devices', trilinear an enhancement. The software reference stands for
    // the software devices and point-samples whatever is asked.
    {
        const bool gpu = vkRen || glRen || worldVk;
        if (texFilter == 2)
            std::printf("filter: trilinear%s - an ENHANCEMENT the original never had (it shipped "
                        "one level, MIP NONE); %s\n", texAniso > 1 ? " with anisotropy" : "",
                        vkRen || (worldVk && worldVulkan) ? "drawn by the Vulkan backend"
                        : gpu ? "this backend has no mip chain and draws it bilinear"
                              : "the software reference point-samples, --vulkan for it");
        else
            std::printf("filter: %s - %s; %s\n", omk::textureFilterName(texFilter),
                        texFilter == 1 ? "what the original drew on a 3D card (MAG/MIN LINEAR, no mipmaps)"
                                       : "what the original's software devices drew (POINT)",
                        gpu ? "drawn by the GPU backend"
                            : "the software reference point-samples, as the original's software devices did");
    }
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
