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
#include "playframe.h"

namespace {









// keymap, charmap, AudioLock, optionDisplayModes and SdlFrontend moved to
// `sdlfront.{h,cpp}` (`todo/play-split.md` S1c).


int sceneViewer(const std::string& fr, const std::string& setName,
                int camIndex, const float* eyeArg, const float* atArg,
                float fovArg, bool letterbox, int frameBudget,
                const std::string& dump, bool startVulkan, bool noDelay,
                int aaSamples, int texFilter, int texAniso, int ssaa,
                bool sceneDither) {
    // The set. A bare name is looked up in MESHES/DECORS, which is where the
    // decor sets live; anything with a slash is taken as given, so a character
    // model or another folder can be opened without a special case.
    std::string path = setName;
    if (path.find('/') == std::string::npos) {
        if (path.size() < 4 || path.substr(path.size() - 4) != ".3DO")
            path += ".3DO";
        path = fr + "/MESHES/DECORS/" + path;
    }
    const auto d = omk::DataFs::readPath(path);
    if (d.empty()) { std::fprintf(stderr, "cannot read %s\n", path.c_str()); return 1; }
    std::string tpath = path.substr(0, path.rfind('.')) + ".3DT";
    const auto tres = omk::DataFs::readPath(tpath);
    const auto geo = omk::buildGeometry(d, omk::DrawFilter::Engine);
    const auto tex = tres.empty() ? std::vector<omk::Texture>{}
                                  : omk::textures(d, tres);
    if (geo.corners.empty()) {
        std::fprintf(stderr, "%s has no drawable geometry\n", path.c_str());
        return 1;
    }

    // The set's OWN cameras, out of the .3DO's 52-byte records - name, eye,
    // target and fov. Stepping these with [ and ] is what makes the viewer a
    // check rather than a toy: they are the framings the authors chose, so a
    // set that looks right from all of them is right where it was meant to be
    // looked at.
    std::vector<omk::Camera> cams;
    if (const auto h = omk::readHeader(d)) cams = omk::readCameras(d, *h);

    ViewCam vc;
    vc.fov = fovArg > 0 ? fovArg : 60.0f;
    int ci = camIndex;
    if (eyeArg && atArg) {
        vc.lookAt(eyeArg, atArg);
        ci = -1;
    } else if (!cams.empty()) {
        if (ci < 0 || ci >= static_cast<int>(cams.size())) ci = 0;
        vc.lookAt(cams[ci].pos, cams[ci].target);
        if (fovArg <= 0 && cams[ci].fov > 0) vc.fov = cams[ci].fov;
    } else {
        // No camera anywhere: stand off the geometry's own centroid, the way
        // `run_anekbah` derives its framing, so the set is at least in shot.
        double c[3] = {0, 0, 0};
        for (const auto& k : geo.corners) { c[0] += k.x; c[1] += k.y; c[2] += k.z; }
        const double n = static_cast<double>(geo.corners.size());
        const float t[3] = {static_cast<float>(c[0] / n), static_cast<float>(c[1] / n),
                            static_cast<float>(c[2] / n)};
        const float e[3] = {t[0], t[1] - 100.0f, t[2] - 400.0f};
        vc.lookAt(e, t);
    }

    std::printf("%s: %zu corners, %zu batches, %zu textures, %zu cameras\n",
                path.c_str(), geo.corners.size(), geo.batches.size(),
                tex.size(), cams.size());
    for (std::size_t i = 0; i < cams.size(); ++i)
        std::printf("  cam %2zu  %-20s fov %.1f\n", i, cams[i].name, cams[i].fov);
    std::printf(
        "\nW/S fly, A/D strafe, Q/E up-down, SHIFT faster, arrows look,\n"
        "[ ] step the set's cameras, L cycles the light "
        "(colour = what the game draws, grey = this repo's old bug, off),\n"
        "V swaps the software reference for the Vulkan backend, "
        "P prints the camera, ESC quits.\n\n");

    SdlFrontend front;
    SDL_Window* vkWin = nullptr;
    bool direct = false;
    omk::Renderer* vkRen = nullptr;   // owns the swapchain, whoever is drawing

    const int PW = 640, PH = letterbox ? 352 : 480;
    const int PY = (480 - PH) / 2;

    // ---- the renderers, both behind `PORTING` A2's boundary.
    //
    // The viewer draws through `Renderer` rather than calling `drawGeometry`,
    // so what is on screen is the same path `verify.py` measures AND the same
    // path the GPU backend implements. `V` swaps them in place, which is the
    // most useful thing this window can do: the reference and the live one,
    // same camera, same frame, a keypress apart.
    float sceneShimmer = 0.0f;   // `dword_907310` for the set viewer's own path
    Uint32 sceneShimmerMs = 0;   // the wall clock it advances on, interactively
    omk::SoftwareRenderer sw;
    sw.init(PW, PH);
    omk::Renderer* live = nullptr;
#if defined(OMK_VULKAN)
    live = omk::makeVulkanRenderer();
    if (live && aaSamples > 1) live->setMultisample(aaSamples);   // the enhancements
    if (live && texFilter > 0) live->setTextureFilter(texFilter);
    if (live && texAniso > 1) live->setAnisotropy(texAniso);
    if (live && ssaa > 1) live->setSupersample(ssaa);
    if (live && !live->init(PW, PH)) { delete live; live = nullptr; }
    if (live) {
        live->setTextures(tex);
        std::printf("vulkan: %s  (V swaps the backend)\n",
                    omk::vulkanDeviceName(live));
    } else {
        std::printf("vulkan: no device - software only\n");
    }
    if (aaSamples > 1)
        std::printf("aa: %dx MSAA asked - an ENHANCEMENT the original never had; "
                    "%s\n", aaSamples,
                    live ? "the Vulkan backend has it, the software one does not"
                         : "no Vulkan device, so nothing here draws it");
    if (texFilter > 0)
        std::printf("filter: %s%s asked - an ENHANCEMENT the original never had; "
                    "%s\n", omk::textureFilterName(texFilter),
                    texAniso > 1 ? " with anisotropy" : "",
                    live ? "the Vulkan backend has it, the software one does not"
                         : "no Vulkan device, so nothing here draws it");
#else
    std::printf("built without Vulkan - software only "
                "(`make vulkan` needs pkg-config vulkan + glslc)\n");
#endif
    sw.setTextures(tex);
    omk::Renderer* ren = (startVulkan && live) ? live
                                              : static_cast<omk::Renderer*>(&sw);
    std::printf("backend: %s\n", ren->name());

    // ---- DIRECT PRESENTATION.
    //
    // A window carrying a Vulkan surface cannot also carry an `SDL_Renderer`,
    // so this is a WINDOW MODE rather than a runtime toggle: `--vulkan` opens
    // a Vulkan window and the frame never touches the CPU, while without it the
    // window keeps its texture-upload path and `V` can still swap backends
    // through `readback()` for comparison.
    //
    // The order below is Vulkan's, not a preference: the instance needs the
    // window system's extensions, the surface needs the instance, and the
    // device's queue choice needs the surface.
#if defined(OMK_VULKAN)
    if (startVulkan) {
        if (SDL_Init(SDL_INIT_VIDEO) == 0) {
            vkWin = SDL_CreateWindow("OMK Engine - scene viewer (vulkan)",
                                     SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     640, 480, SDL_WINDOW_VULKAN);
        }
        if (vkWin) {
            unsigned n = 0;
            SDL_Vulkan_GetInstanceExtensions(vkWin, &n, nullptr);
            std::vector<const char*> ext(n);
            SDL_Vulkan_GetInstanceExtensions(vkWin, &n, ext.data());
            omk::Renderer* vr = omk::makeVulkanRenderer();
            if (aaSamples > 1) vr->setMultisample(aaSamples);
            if (texFilter > 0) vr->setTextureFilter(texFilter);
            if (texAniso > 1) vr->setAnisotropy(texAniso);
            if (ssaa > 1) vr->setSupersample(ssaa);
            omk::vulkanNeedExtensions(vr, ext.data(), n);
            void* inst = omk::vulkanCreateInstance(vr);
            // Value-initialised rather than VK_NULL_HANDLE: this file
            // includes SDL_vulkan.h, which declares the handle types but not
            // Vulkan's constants - and A8 rule 2 keeps vulkan.h out of here.
            VkSurfaceKHR surf{};
            if (inst && SDL_Vulkan_CreateSurface(vkWin, static_cast<VkInstance>(inst), &surf)
                && omk::vulkanAttachSurface(vr, reinterpret_cast<unsigned long long>(surf))
                && vr->init(PW, PH)) {
                delete live;
                live = vr;
                live->setTextures(tex);
                direct = true;
                vkRen = vr;   // the one that owns the swapchain
                std::printf("vulkan: PRESENTING DIRECTLY - no readback, "
                            "no texture upload\n");
            } else {
                delete vr;
                SDL_DestroyWindow(vkWin); vkWin = nullptr;
                std::printf("vulkan: direct presentation unavailable, "
                            "falling back to the upload path\n");
            }
        }
    }
#endif
    if (!direct && !front.open(640, 480, "OMK Engine - scene viewer (software)")) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1;
    }
    if (direct) ren = live;

    // The mirror, if this set has one (mesh flag 0x100000 - ASSETS 4c).
    const omk::MirrorPlane mp = omk::mirrorPlane(d);
    bool mirrorOn = true, mirrorWas = false;
    if (mp.found)
        std::printf("mirror: mesh %d, plane point (%.0f %.0f %.0f) "
                    "normal (%.2f %.2f %.2f) - M toggles the pass\n",
                    mp.mesh, mp.point[0], mp.point[1], mp.point[2],
                    mp.normal[0], mp.normal[1], mp.normal[2]);
    else
        std::printf("this set has no mirror mesh\n");

    Light light = Light::Colour;
    omk::Geometry lit = geo;
    omk::Surface fb(640, 480, 0);
    std::set<int> was;
    long frames = 0;

    for (;;) {
        omk::HostInput host;
        if (!front.pump(host)) break;
        if (host.held.count(0x01)) break;                       // ESC

        // Held keys fly; the ones that CHANGE something are edge-triggered, so
        // holding [ does not run through every camera in one frame. This is
        // the viewer's own filter and not `Game_Frame`'s - nothing here goes
        // near the binding tables.
        const auto down = [&](int dik) { return host.held.count(dik) != 0; };
        const auto hit  = [&](int dik) { return down(dik) && !was.count(dik); };

        const float step = down(0x2A) ? 40.0f : 8.0f;           // LSHIFT
        float f[3], r[3];
        vc.forward(f); vc.right(r);
        for (int k = 0; k < 3; ++k) {
            if (down(0x11)) vc.eye[k] += f[k] * step;           // W
            if (down(0x1F)) vc.eye[k] -= f[k] * step;           // S
            if (down(0x20)) vc.eye[k] += r[k] * step;           // D
            if (down(0x1E)) vc.eye[k] -= r[k] * step;           // A
        }
        if (down(0x10)) vc.eye[1] -= step;                      // Q: up (Y down)
        if (down(0x12)) vc.eye[1] += step;                      // E: down
        if (down(0xCB)) vc.yaw   -= 0.03f;                      // left
        if (down(0xCD)) vc.yaw   += 0.03f;                      // right
        if (down(0xC8)) vc.pitch += 0.02f;                      // up
        if (down(0xD0)) vc.pitch -= 0.02f;                      // down
        vc.pitch = std::clamp(vc.pitch, -1.5f, 1.5f);

        if (!cams.empty() && (hit(0x1A) || hit(0x1B))) {        // [ ]
            const int n = static_cast<int>(cams.size());
            ci = ((ci < 0 ? 0 : ci) + (hit(0x1B) ? 1 : n - 1)) % n;
            vc.lookAt(cams[ci].pos, cams[ci].target);
            if (cams[ci].fov > 0) vc.fov = cams[ci].fov;
            std::printf("cam %d  %s  fov %.1f\n", ci, cams[ci].name, vc.fov);
        }
        if (hit(0x32)) {                                        // M
            mirrorOn = !mirrorOn;
            std::printf("mirror pass %s\n", mirrorOn ? "on" : "off");
        }
        if (hit(0x2F) && live) {                                // V
            ren = (ren == &sw) ? live : static_cast<omk::Renderer*>(&sw);
            std::printf("backend: %s\n", ren->name());
        }
        if (hit(0x26)) {                                        // L
            light = light == Light::Colour ? Light::Grey
                  : light == Light::Grey   ? Light::Off : Light::Colour;
            lit = relight(geo, light);
            std::printf("light: %s\n",
                        light == Light::Colour ? "colour (what the game draws)"
                      : light == Light::Grey   ? "grey (this repo's own pre-2026-08-29 bug)"
                                               : "off (full bright)");
        }
        if (hit(0x19)) {                                        // P
            float at[3]; vc.aim(at);
            std::printf("--eye %.0f,%.0f,%.0f --at %.0f,%.0f,%.0f --fov %.1f\n",
                        vc.eye[0], vc.eye[1], vc.eye[2], at[0], at[1], at[2], vc.fov);
        }
        was = host.held;

        omk::RCamera cam;
        for (int k = 0; k < 3; ++k) cam.eye[k] = vc.eye[k];
        vc.aim(cam.at);
        cam.hfovDeg = vc.fov;
        cam.w = PW; cam.h = PH;

        // The submissions, in `buildGeometry`'s order - which is the engine's
        // own (`Render_FlushBuckets` ascending), not this file's. A backend
        // receives them and never reorders.
        omk::View view; view.cam = cam;
        // THE SHIMMER's clock, advanced here as well - the set viewer is
        // exactly where a skyline gets looked at, and it draws through its own
        // path rather than the frame loop's.
        // `2 * frameDelta`, and this loop has no Session to ask: a
        // frame-bounded run keeps exactly 1.0 a frame (`engine: shimmer`
        // counts the frames that move), a live one measures its own delta,
        // clamped at the engine's 3.0 - the loop sleeps 16 ms, so it was
        // shimmering twice as fast as the game (todo/sixty-fps.md 2).
        {
            float d = 1.0f;
            const Uint32 nowMs = SDL_GetTicks();
            if (!frameBudget && sceneShimmerMs)
                d = std::min(3.0f, static_cast<float>(nowMs - sceneShimmerMs) * 30.0f / 1000.0f);
            sceneShimmerMs = nowMs;
            sceneShimmer += 2.0f * d;
        }
        while (sceneShimmer >= omk::kShimmerWrap) sceneShimmer -= omk::kShimmerWrap;
        view.shimmerClock = sceneShimmer;
        view.dither = sceneDither;
        // `drawWithMirror` submits in `buildGeometry`'s order - the engine's
        // own - and adds the reflection pass when the set has a mirror mesh
        // and the camera is in front of it. With no mirror it is one pass and
        // one readback, exactly as before.
        const auto ms = omk::drawWithMirror(*ren, lit, tex, view,
                                            mirrorOn ? mp : omk::MirrorPlane{});
        if (ms.active != mirrorWas) {
            std::printf("mirror %s%s (%ld px, camera %.0f in front)\n",
                        ms.active ? "REFLECTING" : "not in view",
                        ms.native ? " [native: the two passes]" : "",
                        ms.maskPixels, ms.distance);
            mirrorWas = ms.active;
        }
        // The readback is the whole cost direct presentation removes, so it
        // must not happen when nothing needs it. It IS needed when the mirror
        // pass composited (that frame lives only on the CPU) and always in the
        // upload path.
        static const omk::Surface kNone(1, 1, 0);
        // A frame that stayed on the GPU needs no readback at all - which is
        // the whole point of the native mirror pass, and of direct
        // presentation. The readback is for the upload path, for a software
        // frame, and for a mirror the CPU had to composite.
        const bool onGpu = direct && ren == vkRen && (!ms.active || ms.native);
        const omk::Surface& pic = onGpu ? kNone : ren->readback();

        // The letterbox is a PLACEMENT, not a blit: the picture is copied into
        // the framebuffer's middle rows and the bands stay black, which is what
        // the captures show. Nothing is scaled, filtered or blended, so what
        // the window uploads is what `drawGeometry` produced.
#if defined(OMK_VULKAN)
        if (direct) {
            // The finished frame is already in the GPU's colour attachment,
            // so it goes straight to the swapchain - no readback, no upload,
            // no SDL texture. The exception is a frame the MIRROR pass
            // composited, which exists only as a CPU `Surface` and has to be
            // uploaded; that is the one frame that still round-trips, and a
            // GPU composite is what would remove it.
            // The SWAPCHAIN belongs to the Vulkan renderer, but the frame
            // may have come from the software one - `V` still swaps them here.
            // A frame it did not draw itself (software, or one the mirror pass
            // composited on the CPU) is uploaded; its own is presented with no
            // CPU involvement at all. Presenting through `ren` instead froze
            // the window the moment `V` was pressed, because the cast to the
            // Vulkan renderer simply failed and nothing was presented.
            if (onGpu) omk::vulkanPresent(vkRen);
            else       omk::vulkanPresentSurface(vkRen, pic);
        } else
#endif
        {
            std::fill(fb.px.begin(), fb.px.end(), std::uint16_t(0));
            for (int y = 0; y < PH; ++y)
                std::copy_n(pic.px.begin() + static_cast<std::size_t>(y) * PW, PW,
                            fb.px.begin() + static_cast<std::size_t>(y + PY) * 640);
            front.present(fb);
        }
        std::fflush(stdout);   // so a crash keeps its log; one cost nothing else
        ++frames;
        if (frameBudget && frames >= frameBudget) break;
        if (!noDelay) SDL_Delay(16);   // --nodelay: for measuring, not for playing
    }
    std::printf("%ld frames presented (%s)%s\n", frames, ren->name(),
                direct ? ", presented directly" : "");
    // The framebuffer the WINDOW was shown, raw LE RGB565 - the same dump the
    // interface path writes, so a shot from here can be read by the same
    // Python and laid beside a capture. `--frames 1 --dump` is also how this
    // is smoke-tested without a person pressing ESC.
    if (!dump.empty()) {
        // Direct presentation never fills `fb` - that is the point - so a dump
        // asks the renderer for the frame explicitly. It reads the colour
        // attachment rather than the swapchain, so it verifies everything up
        // to the blit and not the blit itself; the blit is a person's job.
        if (direct) {
            const omk::Surface& last = ren->readback();
            std::fill(fb.px.begin(), fb.px.end(), std::uint16_t(0));
            for (int y = 0; y < PH && y < last.h; ++y)
                std::copy_n(last.px.begin() + static_cast<std::size_t>(y) * PW, PW,
                            fb.px.begin() + static_cast<std::size_t>(y + PY) * 640);
        }
        std::ofstream o(dump, std::ios::binary);
        for (auto v : fb.px) {
            const char b2[2] = {static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)};
            o.write(b2, 2);
        }
        // the SIZE, not a literal: this said "640x480" whatever it wrote.
        std::printf("wrote %s (%dx%d RGB565)\n", dump.c_str(), fb.w, fb.h);
    }
    if (!direct) front.close();
    // The teardown comes LAST, after the dump: `ren` points at `live`, and
    // freeing the renderer before reading a frame out of it is a
    // use-after-free that crashed 4 runs of 4. The renderer owns the Vulkan
    // surface, so it must still go before the window that surface was made
    // from.
    delete live;
    live = nullptr;
    if (vkWin) { SDL_DestroyWindow(vkWin); SDL_Quit(); }
    return 0;
}

}  // namespace


int main(int argc, char** argv) {
    OMK_HEAPCHECK("main");
    // THE FLAGS (`todo/play-split.md` S2): `omk::PlayOptions`, parsed in
    // `src/app/playoptions.cpp`. Each old local is a REFERENCE to its field, so
    // not one line below changed; a later step that moves a phase drops the
    // aliases it no longer needs.
    omk::PlayOptions opt;
    if (const int rc = opt.parse(argc, argv); rc >= 0) return rc;
    const std::string& fr = opt.fr;
    auto& tb = opt.tb;
    auto& screenId = opt.screenId;
    auto& frames = opt.frames;
    auto& playMovies = opt.playMovies;
    auto& dump = opt.dump;
    auto& typeText = opt.typeText;
    auto& dispW = opt.dispW;
    auto& dispH = opt.dispH;
    auto& resFlag = opt.resFlag;
    auto& fullscreenFlag = opt.fullscreenFlag;
    auto& scripted = opt.scripted;
    auto& bankReject = opt.bankReject;
    auto& keyEvery = opt.keyEvery;
    auto& holdStream = opt.holdStream;
    auto& snapsDir = opt.snapsDir;
    auto& flickerDir = opt.flickerDir;
    auto& waterCamPreset = opt.waterCamPreset;
    auto& snapEvery = opt.snapEvery;
    auto& saveFile = opt.saveFile;
    auto& savesPath = opt.savesPath;
    auto& slotArg = opt.slotArg;
    auto& saveSlotArg = opt.saveSlotArg;
    auto& saveNameArg = opt.saveNameArg;
    auto& areaArg = opt.areaArg;
    auto& addressArg = opt.addressArg;
    auto& density = opt.density;
    auto& fightArg = opt.fightArg;
    auto& noFoeCollision = opt.noFoeCollision;
    auto& noFightCamRay = opt.noFightCamRay;
    auto& foeAtSet = opt.foeAtSet;
    auto& foeAtXZ = opt.foeAtXZ;
    auto& fightLevelArg = opt.fightLevelArg;
    auto& rideArg = opt.rideArg;
    auto& boardArg = opt.boardArg;
    auto& configFile = opt.configFile;
    auto& densityFlag = opt.densityFlag;
    auto& clipFlag = opt.clipFlag;
    auto& clipArg = opt.clipArg;
    auto& skyFlag = opt.skyFlag;
    auto& shadowFlag = opt.shadowFlag;
    auto& threadFlag = opt.threadFlag;
    auto& noTieFlag = opt.noTieFlag;
    auto& cpuBodiesFlag = opt.cpuBodiesFlag;
    auto& detailFlag = opt.detailFlag;
    auto& aaFlag = opt.aaFlag;
    auto& filterFlag = opt.filterFlag;
    auto& anisoFlag = opt.anisoFlag;
    auto& shadowQFlag = opt.shadowQFlag;
    auto& uiScaleFlag = opt.uiScaleFlag;
    auto& textScaleFlag = opt.textScaleFlag;
    auto& lightingFlag = opt.lightingFlag;
    auto& ssaaFlag = opt.ssaaFlag;
    auto& radarFlag = opt.radarFlag;
    auto& frameRateFlag = opt.frameRateFlag;
    auto& smoothAnimFlag = opt.smoothAnimFlag;
    auto& dither = opt.dither;
    auto& mouseInvertX = opt.mouseInvertX;
    auto& mouseInvertY = opt.mouseInvertY;
    auto& shootEyeLift = opt.shootEyeLift;
    auto& shootEyeSet = opt.shootEyeSet;
    auto& enhanceAll = opt.enhanceAll;
    auto& drawFog = opt.drawFog;
    auto& lightCrowd = opt.lightCrowd;
    auto& fogRGB = opt.fogRGB;
    auto& giveList = opt.giveList;
    auto& moneyArg = opt.moneyArg;
    auto& ringsArg = opt.ringsArg;
    auto& varList = opt.varList;
    auto& newWorld = opt.newWorld;
    auto& sceneChunk = opt.sceneChunk;
    auto& zoneEnable = opt.zoneEnable;
    auto& zoneDisable = opt.zoneDisable;
    auto& sceneLoads = opt.sceneLoads;
    auto& noCrowd = opt.noCrowd;
    auto& noScriptSprites = opt.noScriptSprites;
    auto& scxPlay = opt.scxPlay;
    auto& openSneak = opt.openSneak;
    auto& startShoot = opt.startShoot;
    auto& shootHealth = opt.shootHealth;
    auto& fightHealth = opt.fightHealth;
    auto& shootEndAt = opt.shootEndAt;
    auto& standAt = opt.standAt;
    auto& haveStand = opt.haveStand;
    auto& scene = opt.scene;
    auto& camIndex = opt.camIndex;
    auto& eyeA = opt.eyeA;
    auto& atA = opt.atA;
    auto& fovA = opt.fovA;
    auto& callDialog = opt.callDialog;
    auto& animHoldHarness = opt.animHoldHarness;
    auto& haveEye = opt.haveEye;
    auto& haveAt = opt.haveAt;
    auto& letterbox = opt.letterbox;
    auto& startVulkan = opt.startVulkan;
    auto& noDelay = opt.noDelay;
    auto& speed = opt.speed;
    auto& forceSoftware = opt.forceSoftware;
    auto& showFps = opt.showFps;
    auto& worldVulkan = opt.worldVulkan;
    // THE GAME'S STATE (`todo/play-split.md` S3), one group at a time; each
    // old local below is a REFERENCE to its field where it used to be declared.
    omk::Game game;
    bool boardPress = false;
    bool mountSpent = false;    // the action button is edged, not held
    bool calledOpenTold = false;
    bool boarded = false;           // aboard, `Slider_TickRide` not yet driving
    // ...and the BOARDING that comes first: `MDACTION` has put him at the
    // door, group 60 (`H_SLDIN`, 72 frames) is playing the door and the step
    // in, and the channel's own `MDSLIDIN` entry at the end of it is what
    // takes him aboard. Between the two he is ACTOR_STATE 6.
    bool  boarding = false;
    float doorOff[3] = {0, 0, 0};   // the placement, in the SLIDER's frame
    int   doorOffState = 0;         // 0 not read yet, 1 read, -1 unavailable
    double boardCam = 0;            // frames (at 30 Hz) left of `Camera_Request(9, ..)`
    // ...and the EXIT, which is the same shape mirrored: `sub_468FA0` places
    // him from group 61's clip against a DIFFERENT reference (slf_113.3da,
    // `dword_9103D8`) and plays `H_SLDOUT`.
    bool  leaving = false;
    float exitOff[3] = {0, 0, 0};
    int   exitOffState = 0;
    // ---- THE SLIDER'S OWN DOOR CLIPS -------------------------------------
    //
    // `Cef_TickChannel`'s ACTOR_STATE switch (19_dsound.c, cases 6 and 8)
    // plays a clip ON THE SLIDER while the character plays `H_SLDIN` /
    // `H_SLDOUT`, driven by the SAME clock `a2`:
    //
    //     sub_437FC0(sub, dword_90EF28);              // bind
    //     sub_437FE0(sub, dword_90EF28, 0.0, a2, &d); // sample
    //     sub_438310(slider, &sp);
    //     sub_437F80(sub, sp + d, sp.y - 33.149605 + d.y, sp.z + d.z);
    //
    // `dword_90EF28` is `ANIMS\slf_112.3da` and `dword_9103D8` is
    // `slf_113.3da` (05_sys.c 1842-1844) - and the clips are **72 and 51
    // frames**, exactly the lengths of `H_SLDIN` and `H_SLDOUT`. So the door
    // is an ANIMATION, not the two-state model swap `sub_4521E0` does, and
    // this port had read the clips only for their root key and never played
    // them. `build/slider_doorclip` measures what each drives: of the five
    // tracks, one moves - **`SlPorteG` turns 71.4 degrees**, the gull-wing
    // swing - and the other four and the root hold still.
    omk::NodeTracks doorIn, doorOut;
    bool doorClipsRead = false;
    int  journeyTo = -1;            // the address the journey ends at
    // `dword_6A17CC` - which destination row the call was made for.
    int  calledDestination = -1;
    // THE LIVE RIDE, when there is one. `todo/slider.md` step 3's harness.
    std::optional<omk::SliderRide> ride;
    bool scxPlayed = false;
    // ...and the save's OWN placement, which is `State_Apply`'s and not a
    // harness flag: a loaded game stands where it was saved unless something
    // explicit says otherwise.
    float savedAt[3] = {0, 0, 0}, savedYaw = 0.0f;
    bool  haveSavedPlacement = false;

    // The viewer takes the whole program: it wants no boot chain, no widget
    // tree and no movies, and mixing it into the interface loop would make
    // both harder to read than either is worth.
    if (!scene.empty())
        return sceneViewer(fr, scene, camIndex, haveEye ? eyeA : nullptr,
                           haveAt ? atA : nullptr, fovA, letterbox, frames, dump,
                           startVulkan, noDelay, aaFlag < 0 ? 0 : aaFlag,
                           filterFlag < 0 ? 0 : filterFlag, anisoFlag < 0 ? 1 : anisoFlag,
                           // the scene viewer runs BEFORE settings resolve, so
                           // it takes the flag alone; `--config` is the game's
                           ssaaFlag < 0 ? 1 : ssaaFlag, dither);

    const omk::DataFs fs(fr);
    auto w = omk::UiWidgets::loadJson(tb + "/ui_widgets.json");
    // `Ui_BuildLoadPanel`'s layout, applied for screen 29.  The four buttons
    // of the load panel all ship at (460, 210) and the builder moves three of
    // them apart; without this they draw on top of one another and the panel
    // shows one line of text where the original shows three.  The engine does
    // this in the OPEN callback and would redo it for screen 30's save
    // layout - which this port does not open yet, so it is applied once.
    omk::applyLoadPanelLayout(w, 29);
    if (!w.valid())
        std::printf("tables: no widget tree (ui_widgets.json) - the interface "
                    "screens cannot be drawn or walked, so the Session answers "
                    "them itself and the start menu is skipped\n");
    w.loadScreens(tb + "/ui.json");
    const auto fonts = omk::FontTable::loadJson(tb + "/ui.json");
    omk::TextLayout lay(fonts, fr + "/FONTS");   // not const: the text-scaling enhancement
    omk::ScreenComposer comp(fs, w, lay);
    // SCREEN 35, the options (`todo/options-menu.md`): the page tree out of
    // the widget lift, the 74 rows out of `ui.json`, and the screen's own
    // strings - the labels by `+24`, the save prompt's and Accel 3D's.
    const omk::OptionTree optTree =
        omk::OptionTree::loadJson(tb + "/ui_widgets.json", tb + "/ui.json");
    omk::OptionsMenu optMenu(optTree);
    optMenu.setText(omk::iamStrings(fs, "IAM/Options"));
    // The world rendered at a panel's 3D VIEWPORT item's size, for the
    // composer to place (`ScreenComposer::attachView3D`).
    omk::Surface view3dPic;
    // The menu's animated background - `IMAGES/cloud.bmp` embossed by a
    // rotating light and warped by two cosine tables (`ui/cloud.h`). The
    // screen's own sheet is colour-keyed over it.
    omk::MenuCloud cloud;
    if (cloud.load(fs)) comp.attachCloud(&cloud);
    else std::printf("no IMAGES/cloud.bmp - the menu draws on black\n");

    // The real input path: scancodes in, the live binding tables and
    // `Game_Frame`'s edge filter in the middle, one 14-bit word out. Nothing
    // here hands the walk a word directly, which is the whole point of
    // `verify.py: engine input`.
    const auto schemes = omk::ControlSchemes::loadJson(tb + "/key_bindings.json");
    omk::Input in(schemes);
    in.installScheme(0);
    in.setRepeatMask(0x203F);          // `Ui_BeginScreen`

    OMK_HEAPCHECK("before boot chain");
    // ---- the boot chain, because the app does not start at a menu --------
    //
    // `docs/BOOT.md`: launch -> the three FLIS movies -> Game_Start
    // ("aventure.scx", which is the global sprite and sound library and NOT a
    // menu) -> the start area out of `IAM\START +1414` -> whose startup script
    // reaches `ui.open`. Running it here rather than opening screen 29
    // directly means the menu appears because the SCRIPT asked for it, which
    // is what `engine: boot` reproduces 42 of 42 from a cold start.
    //
    // **THE MOVIES ARE STEPPED, NOT DECODED.** `gamedata/FLIS/` holds three MPEG-1
    // program streams and `PORTING` A8 names pl_mpeg as the vendored decoder
    // for them; it is not integrated, so the boot finds the three files,
    // reports them, and moves on. The first thing a player sees is therefore
    // missing here, and saying so is the point - a window that opens on the
    // menu would imply the app starts there.
    omk::BootOptions bo;
    bo.root = fr; bo.tables = tb; bo.frames = 1;
    const omk::BootReport br = omk::boot(bo);
    std::printf("boot: %d FLIS movie%s found; %s -> %d sprites, %d sounds; "
                "start area %d\n",
                br.moviesFound, br.moviesFound == 1 ? "" : "s",
                br.bootScene.c_str(), br.bootSprites, br.bootSounds,
                br.startArea);

    OMK_HEAPCHECK("before live session");
    // ---- THE LIVE SESSION -----------------------------------------------
    //
    // `boot()` runs the chain and reports; this RUNS it, frame by frame, with
    // a person at the keyboard. The difference is where the menu's answer
    // comes from: `attachUi` DERIVES one by walking the tree, which is right
    // for a headless check and wrong for a game. `answerUiFromPerson` parks
    // the script instead - `Game_HandleEvent` case 5 - and nothing releases it
    // until somebody presses a key.
    //
    // So the menu is not opened by this file. AREA 118's own startup script
    // reaches `ui.open(29, -1, -> variable 19)` and parks; the frontend asks
    // the Session which screen is waiting and opens THAT. A build that opened
    // screen 29 itself would still show a menu and would be a different
    // program.
    const auto opcodes = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    // The ONE that cannot be worked around: without the operand lengths the
    // VM cannot even step an instruction, and a hand-written table is not an
    // option - CLAUDE.md records one being wrong three ways in an hour. It is
    // in the repo as `tables/vm_opcodes.json`.
    if (!opcodes.valid()) {
        std::fprintf(stderr,
            "no VM opcode table: looked for %s/vm_opcodes.json\n"
            "  this one is required - the world scripts cannot be decoded "
            "without the operand lengths.\n"
            "  pass the directory as the second argument, e.g. "
            "omk-play <gamedata> tables\n",
            tb.empty() ? "<no tables dir>" : tb.c_str());
        return 1;
    }
    omk::GameState state = omk::GameState::fromFile(fr + "/IAM/START");
    // THE SETTINGS, from the three sources in their own order: the engine's
    // defaults (`sub_41F4C0`), then the ini, then the save file's 3496-byte
    // header - which is what the options MENU last wrote, and so is later
    // than the ini and wins (`platform/settings.h`, GAME_STATE 8a). The
    // header is read from the same bytes the slot comes out of, because it
    // IS the head of that file.
    //
    // WHICH FILE, and it is one decision made once so the settings and the
    // slot cannot come from two different places: an explicit `--save` names
    // a file directly (the fixtures do), otherwise a `--slot` reads the saves
    // file - the writable one if it exists, else the shipped `IAM/GAMES`.
    std::vector<std::byte> saveBytes;
    std::string saveFrom, loadedName;
    if (!saveFile.empty()) {
        saveBytes = omk::DataFs::readPath(saveFile);
        saveFrom = saveFile;
    } else if (slotArg >= 0) {
        saveFrom = savesPath;
        saveBytes = omk::readSaveFile(savesPath, fr + "/IAM/GAMES", &saveFrom);
        if (saveBytes.empty())
            std::fprintf(stderr, "--slot: no save file at %s, and none in the "
                                 "game tree either\n", savesPath.c_str());
    }
    std::optional<omk::SettingsBlock> saveSettings;
    if (!saveBytes.empty())
        saveSettings = omk::readSettingsBlock(saveBytes);
    // ...AND WITH NEITHER, THE SAVES FILE'S HEADER ALL THE SAME. The game's
    // boot calls `SaveDir_Load` unconditionally (`03_win32.c`, after
    // `sub_41F4C0`'s defaults): the header of its saves file IS its settings,
    // whether or not a game is loaded. So a plain start takes what the options
    // menu last saved - the writable file, else the shipped `IAM/GAMES` - and
    // loads no slot (that is still `--slot`'s, through `saveBytes` above).
    if (!saveSettings && saveFile.empty() && slotArg < 0) {
        const auto head = omk::readSaveFile(savesPath, fr + "/IAM/GAMES");
        if (!head.empty()) saveSettings = omk::readSettingsBlock(head);
    }
    const omk::OptionsFile ini =
        configFile.empty() ? omk::OptionsFile{} : omk::loadOptionsFile(configFile);
    if (!configFile.empty() && !ini.loaded)
        std::fprintf(stderr, "%s: no such config file - using defaults\n", configFile.c_str());
    // not const: the options menu writes `settings.v` as the game's apply
    // hooks write `byte_90E180`, and what reads it later reads the new value
    omk::Settings settings = omk::resolveSettings(ini, saveSettings);
    // THE DISPLAY SIZE: `--res`, else row 2 as the settings resolved it - the
    // save header (`+12`/`+14`, what the menu last saved) over the ini's
    // `screen_x` / `screen_y` - else 800x600. A run that must be a given size
    // says so with `--res`; the checks do (`verify.py`'s viewer wrapper).
    if (!resFlag && settings.screen != omk::Settings::Source::Default &&
        settings.v.screenX >= 320 && settings.v.screenY >= 240 &&
        settings.v.screenX <= 8192 && settings.v.screenY <= 8192) {
        dispW = settings.v.screenX;
        dispH = settings.v.screenY;
    }
    // THE THREE VOLUME ROWS (10..12) - ATTENUATIONS, and one law for all
    // three: `-10000 * a / 100` hundredths of a dB, so `a` dB. Read where the
    // engine reads them (`05_sys.c` ~1037..1062, every frame): the music's is
    // summed into `Music_SetVolume` (the Session's `musicOption_`), the
    // dialogue's goes to the speech buffer `Morph_Start` plays a line through
    // (`sub_42BBB0`), the effects' is ADDED to every world voice's own volume
    // (`sub_46C1C0`, `dword_53B31C`). The interface's blips take none of them.
    // A sound already playing keeps the gain it started with - the engine
    // re-sets its sixteen voices at once, which this frontend cannot.
    const auto attGain = [](int a) {
        return static_cast<float>(std::pow(10.0, -std::clamp(a, 0, 100) / 20.0));
    };
    const auto fxGain = [&] { return attGain(settings.v.volumeEffects); };
    const auto dialogueGain = [&] { return attGain(settings.v.volumeDialogue); };
    // ...and the block carries the mode that RUNS, as `g_ScreenSize` does:
    // row 2 reads it back and a settings save writes it
    settings.v.screenX = dispW;
    settings.v.screenY = dispH;
    // THE SHOOTING RANGE'S HIGH SCORES, out of the save header's +724 - the
    // screen's own draw hook bases its rows there, so they travel with the
    // OPTIONS and not with a game. Empty in both shipped saves, which is a
    // table nobody has played into.
    static std::array<std::pair<std::string, int>, 20> highScores;
    if (saveSettings)
        for (std::size_t k = 0; k < highScores.size(); ++k)
            highScores[k] = {saveSettings->highScores[k].name,
                             saveSettings->highScores[k].ms};
    // A flag typed on the command line is the most recent word of all.
    if (!densityFlag) density = settings.v.streetActivity;
    // UNLIMITED DRAW DISTANCE (`todo/enhancements.md` 4). `--clip 0` says it
    // on the command line, `[Enhancements] clipdistance = 0` in the file, and
    // `--enhance-all` includes it. What it lifts is the visible-set walk's
    // distance test and the fog, whose range IS the clip distance - so with
    // no distance there is nothing to fade over and the fog goes off.
    const bool unlimitedClip = (clipFlag && clipArg <= 0)
                            || (!clipFlag && (enhanceAll || settings.unlimitedDrawDistance));
    double clipInches =
        unlimitedClip ? std::numeric_limits<double>::infinity()
                      : clipFlag ? static_cast<double>(clipArg) * omk::kInchesPerMetre
                                 : settings.clipInches();
    if (unlimitedClip)
        std::printf("clip: UNLIMITED - an ENHANCEMENT the original never had "
                    "(the option's five values stop at %d m); the fog goes with it, "
                    "its range being the clip distance's own\n", omk::kMaxOptionClipMetres);
    // one line the first frame that draws, so a run says what the option did
    bool clipReport = true;
    // THE FLICKER CATCHER (--flicker <dir>). A fault a player sees for one to
    // five frames cannot be screenshotted and cannot be found by choosing a
    // frame to render: a reader reported "a Kay'l with a black background" and
    // could not say where. So the viewer watches its own output - a frame far
    // darker than its neighbours is a body drawn on a set that is not there -
    // and writes the frames AROUND the dip, with a line of context each, so
    // the event arrives on disk instead of in a description.
    //
    // It is an INSTRUMENT and no part of the port: nothing here changes a
    // pixel, and with the flag absent none of it runs.
    std::vector<std::vector<std::uint16_t>> flickRing;   // the last kFlickPre frames
    std::vector<std::string> flickNote;                  // ...and their context
    std::vector<long> flickLit;                          // the lit-count window
    std::string frameNote;                               // this frame's context
    int  flickAfter = 0;                                 // frames still to write
    long flickEvent = -1, flickQuietUntil = -1;
    bool drawSky = skyFlag >= 0 ? skyFlag != 0 : settings.v.sky;
    // Options rows 5 and 7, with a flag beating the save the way --sky does.
    bool drawShadows = shadowFlag >= 0 ? shadowFlag != 0 : settings.v.shadows;
    // THREADED BODIES (`todo/vita-port.md` P4). ON by default since 2026-09-23:
    // the posing is bit-identical either way (`omk::Threads`' chunks are
    // disjoint and the merge is in index order), so this is not an enhancement
    // in the off-by-default sense - nothing the player sees changes - and the
    // one reason it started off, a Vita half that had never run on a device,
    // is gone: `omk_bench` on a real console said `threads: EXACT` with the
    // inline pass's own hash, 2.71x on three runners. `--no-thread-bodies`
    // puts it back on one core, for an A/B.
    const bool threadBodies = threadFlag > 0;
    if (threadBodies)
        std::printf("bodies: posed over %d runners (the default; --no-thread-bodies for one); the frame is "
                    "bit-identical to one runner\n", omk::Threads::shared().runners());
    // The ENHANCEMENT, and it is subordinate to the option above: with row 5
    // off nothing draws whatever this says. Default 0 = what the engine draws.
    // `--enhance-all` is a BASE: an explicit flag beats it, and so does a
    // specific `[Enhancements]` key, because `resolveSettings` decided that
    // half already.
    const auto enh = [&](int flag, int fromSettings, int top) {
        return flag >= 0 ? flag : enhanceAll ? top : fromSettings;
    };
    // not const: a renderer that cannot draw an enhancement refuses it, once
    // the renderer exists (below `world`)
    int shadowQuality = enh(shadowQFlag, settings.shadowQuality,
                            omk::kMaxShadowQuality);
    int        shadowDetail = detailFlag >= 0 ? detailFlag : settings.v.levelOfDetail;
    // Row 7. Per pixel ALSO widens who receives: the engine lights the
    // procedural crowd and nothing else, and this lets every character.
    int        lighting = enh(lightingFlag, settings.lighting, omk::kMaxLighting);
    if (lighting > 0)
        std::printf("lighting: per pixel - an ENHANCEMENT the original never had "
                    "(it lights the crowd alone, per vertex); every character receives\n");
    // The enhancement: OFF unless --aa or [Enhancements] said otherwise.
    const int aaSamples = enh(aaFlag, settings.antiAliasing, omk::kMaxAntiAliasing);
    const int texFilter = enh(filterFlag, settings.textureFilter, omk::kMaxTextureFilter);
    const int texAniso  = enh(anisoFlag, settings.anisotropy, omk::kMaxAnisotropy);
    // Supersampling is NOT under `--enhance-all` (settings.h, kMaxSupersample):
    // only `--ssaa` or `supersampling =` turn it on.
    const int ssaa      = ssaaFlag >= 0 ? ssaaFlag : settings.supersample;
    // The RADAR (`ui/radar.h`): the game draws shoot mode's minimap only when
    // a script's op 146 has turned it on; `always` draws it in every shoot
    // phase whose area has a radar file.
    const bool radarAlways = enh(radarFlag, settings.radarAlways ? 1 : 0, omk::kMaxRadar) > 0;
    // Row 11: the PACER's rate. Not the simulation's - that steps on the
    // measured delta whatever this is - and never a `--frames` run's, which
    // steps a fixed 1/30 and never reaches the pacer.
    const int frameRate = enh(frameRateFlag, settings.frameRate, omk::kMaxFrameRate);
    // Row 12, set once for every `composePose` that takes a float frame
    const bool smoothAnim = enh(smoothAnimFlag, settings.smoothAnimation ? 1 : 0,
                                omk::kMaxAnimation) > 0;
    omk::setPoseSmoothing(smoothAnim);
    if (smoothAnim)
        std::printf("animation: smooth - an ENHANCEMENT: bodies are slerped between "
                    "two keys; the original truncates to one (`Anim_ApplyNodeFrame`'s _ftol)\n");
    if (frameRate != 30)
        std::printf("framerate: %d - an ENHANCEMENT: the port presents at 30 by default; "
                    "the original had no cap and stepped on 30/fps, as this does\n", frameRate);
    if (radarAlways)
        std::printf("radar: always - an ENHANCEMENT: the game shows shoot mode's minimap "
                    "only when a script turns it on, and in seven of its nine arenas only "
                    "for object 980, which nothing in the shipped game gives\n");
    // The INTERFACE's own, and the one enhancement here that is not the
    // Vulkan backend's: the 640x480 layer is composed on the CPU for both, so
    // a filtered stretch reaches the software renderer too.
    const int uiScaling = enh(uiScaleFlag, settings.uiScaling, omk::kMaxUiScaling);
    comp.setScaling(uiScaling);
    // TEXT SCALING (settings.h `textScaling`): the glyphs at the smaller of the
    // layout's two scales, so a line keeps its proportion to its box whatever
    // the display. Re-applied wherever the display size changes.
    const int textScaling = enh(textScaleFlag, settings.textScaling, omk::kMaxTextScaling);
    const auto applyTextScale = [&](int w, int h) {
        if (textScaling <= 0) { lay.setGlyphScale(1, 1, 0); return; }
        if (w * 480 <= h * 640) lay.setGlyphScale(w, 640, uiScaling);
        else                    lay.setGlyphScale(h, 480, uiScaling);
    };
    if (textScaling > 0)
        std::printf("text scaling: fit - an ENHANCEMENT: the original draws glyphs at their "
                    "native size whatever the display (`I2D_ScaleX/Y` move them, never "
                    "enlarge them); here they scale with the layout%s\n",
                    uiScaling > 0 ? ", filtered" : "");
    if (uiScaling > 0)
        std::printf("ui scaling: linear - an ENHANCEMENT the original never had "
                    "(DirectDraw's Blt takes one texel); it changes nothing at "
                    "640x480, where nothing stretches\n");
    if (enhanceAll || settings.enhanceAll)
        std::printf("enhancements: all on - %dx MSAA, %s filtering, anisotropy %d, "
                    "%s shadows, %s lighting, %s interface, %s draw distance. As high as each goes "
                    "unless a specific "
                    "setting said otherwise; none of it is what the original drew, and "
                    "the device reduces what it cannot meet. Supersampling is separate "
                    "(--ssaa N / supersampling = N): %dx\n",
                    aaSamples, omk::textureFilterName(texFilter), texAniso,
                    omk::shadowQualityName(shadowQuality), omk::lightingName(lighting),
                    omk::uiScalingName(uiScaling), unlimitedClip ? "unlimited" : "capped", ssaa);
    // The clip half of the line reads differently when it is unlimited:
    // "0 m = inf in" is arithmetic rather than a report.
    char clipText[128];
    if (unlimitedClip)
        std::snprintf(clipText, sizeof clipText,
                      "clip UNLIMITED (enhancement), no fog, no distance cull");
    else
        std::snprintf(clipText, sizeof clipText,
                      "clip %d m (%s) = %.0f in, near/far split %.0f/%.0f",
                      clipFlag ? clipArg : settings.v.clipDistance,
                      clipFlag ? "flag" : omk::sourceName(settings.clipDistance),
                      clipInches, clipInches * 0.25, clipInches * 0.95);
    std::printf("settings: %s;"
                " crowd %d (%s); sky %d (%s), shadows %d (%s), detail %d (%s);"
                " aa %d (%s, enhancement), filter %s (%s, enhancement),"
                " anisotropy %d (%s, enhancement), interface %s (%s, enhancement),"
                " text %s (%s, enhancement)\n",
                clipText,
                density, densityFlag ? "flag" : omk::sourceName(settings.streetActivity),
                drawSky ? 1 : 0, skyFlag >= 0 ? "flag" : omk::sourceName(settings.sky),
                settings.v.shadows ? 1 : 0, omk::sourceName(settings.shadows),
                settings.v.levelOfDetail, omk::sourceName(settings.levelOfDetail),
                aaSamples, aaFlag >= 0 ? "flag" : omk::sourceName(settings.antiAliasingSource),
                omk::textureFilterName(texFilter),
                filterFlag >= 0 ? "flag" : omk::sourceName(settings.textureFilterSource),
                texAniso, anisoFlag >= 0 ? "flag" : omk::sourceName(settings.anisotropySource),
                omk::uiScalingName(uiScaling),
                uiScaleFlag >= 0 ? "flag" : omk::sourceName(settings.uiScalingSource),
                omk::textScalingName(textScaling),
                textScaleFlag >= 0 ? "flag" : omk::sourceName(settings.textScalingSource));
    if (!ini.unknown.empty()) {
        std::printf("settings: %zu key(s) under [Preferences] the engine never reads:",
                    ini.unknown.size());
        for (const auto& k : ini.unknown) std::printf(" %s", k.c_str());
        std::printf("\n");
    }
    if (!saveBytes.empty()) {
        const int slotNo = slotArg >= 0 ? slotArg : 0;
        const auto slot = omk::readSaveSlot(saveBytes, slotNo);
        if (!slot) { std::fprintf(stderr, "%s: not a save file, or it has no slot %d\n",
                                  saveFrom.c_str(), slotNo); return 1; }
        // AN EMPTY SLOT IS NOT A SAVE.  `SaveDir_Build` skips a slot whose
        // name begins with a zero and the load panel never offers one, so
        // loading it would put the player in area 0 out of a zeroed DB and
        // read as a fault in the port.  The distinction worth keeping is
        // between a slot that was CLEARED - `SaveDir_ClearSlot` writes one
        // byte and leaves the DB on disk - and one that was never written,
        // where there is nothing behind the name either.
        if (slot->name.empty()) {
            bool anything = false;
            for (const auto b : slot->state.raw())
                if (b != std::byte{0}) { anything = true; break; }
            if (!anything) {
                std::fprintf(stderr, "%s: slot %d is empty. Occupied slots:",
                             saveFrom.c_str(), slotNo);
                int shown = 0;
                for (int k = 0; k < static_cast<int>(omk::kSaveSlots); ++k) {
                    const auto o = omk::readSaveSlot(saveBytes, k);
                    if (!o || o->name.empty()) continue;
                    if (shown++ < 12) std::fprintf(stderr, " %d (%s)", k, o->name.c_str());
                }
                std::fprintf(stderr, shown ? "\n" : " none\n");
                return 1;
            }
            std::printf("save: slot %d's name is empty - `SaveDir_ClearSlot` "
                        "writes one byte and leaves the rest, so this slot was "
                        "DELETED and its game is still on disk. Loading it\n",
                        slotNo);
        }
        state = slot->state;
        // THE CLOCK COMES FROM THE SLOT, and this used only to print it.
        //
        // `gamestate.h`: the clock and the timer are engine globals and NOT
        // part of the 8192-byte image, so restoring the DB restores
        // everything EXCEPT the date and time - and the slot header is the
        // only place they exist. Loading a save therefore left the game at
        // day 0, 00:00:00 while the loader printed the save's real date one
        // line above.
        //
        // Nothing noticed because nothing DREW the clock. The sneak's own
        // clock row (`sub_0049E090`) is the first thing in this port to show
        // it, and it showed "1 Aqed 7216 - 0:00:00" against a save the same
        // function had just printed as a different date.
        if (newWorld) {
            // The save brought its own world - doors opened, addresses
            // enabled. Put a new game's back, keeping the player record the
            // save is loaded FOR.
            if (state.debugCopyWorldFrom(omk::GameState::fromFile(fr + "/IAM/START")))
                std::printf("--newgame-world: the six state arrays and the "
                            "three object lists reset to IAM/START (a harness "
                            "write, not `Game_NewGame`)\n");
        }
        state.setClockDay(slot->day);
        state.setClock(slot->time);
        // `State_Apply`'s FIRST use of the header pair, and the port had only
        // ever read its second:
        //
        //     u16(u32(g_GameDB, 12), 2 * i16(g_GameDB, 1414)) = u16(g_GameDB, 1416);
        //     result = Area_Load(i16(g_GameDB, 1414), 0);
        //
        // `+12` is the scene-per-area table, so the header's scene is copied
        // INTO it for the header's area before the area is loaded - and
        // `Area_Load` reads exactly that entry at its top (`v35 = i16(u32(
        // g_GameDB, 12), 2 * a1)`) and ends `Scene_Load(slot, v35)`.  So the
        // header is what decides which SCENE goes over the area a save
        // resumes in, and the table merely carries it there.
        //
        // In all four save slots this tree has the two already agree, which
        // is why nothing has broken; they can disagree, because they have
        // different writers - the table is written by opcode 71 `scene.load`
        // and the header by `State_Save` out of the live resident slot - and
        // when they do, the engine's answer is the HEADER's.
        // `verify.py: engine: save write` measures the agreement so a
        // divergence cannot pass unnoticed.
        state.setSceneOfArea(state.currentArea(), state.currentScene());
        // WHERE THE SAVE SAYS HE WAS.  `State_Apply` converts +44..+56 back to
        // world units and stands the player there; the port had never read
        // those four fields at all, so a loaded save came up wherever the
        // harness put him.  Kept here and applied at the hand-over, because
        // that is when there is a player to place.
        state.placementWorld(savedAt, savedYaw);
        // ...and `State_Apply`'s own guard on it.  The whole spawn half -
        // the model, the bank list, `Player_SetActor` and the placement with
        // them - sits inside `if (u16(g_PlayerRecord, 272) != 0xFFFF)`, so a
        // block whose player record names no actor has a placement that means
        // nothing and the engine never applies it.  `IAM\START` is exactly
        // that block, which is why a new game stands wherever the opening
        // puts him.
        loadedName = slot->name;
        haveSavedPlacement = state.playerActorId() != -1;
        if (!haveSavedPlacement)
            std::printf("save: slot %d's player record names no actor (+272 "
                        "is 0xFFFF), so its placement is not applied - which "
                        "is `State_Apply`'s own guard\n", slotNo);
        // The row the load panel would draw for this slot, which is the
        // directory's four fields in the order the original puts them
        // (GAME_STATE 8): the character name, the date, the time - over a
        // heading that is the profile name.
        std::printf("save: the load panel's row for it is \"%s - %s - %s\" "
                    "under \"Joueur : %s\"\n",
                    state.characterName().c_str(),
                    omk::formatDate(slot->day).c_str(),
                    omk::formatTime(slot->time).c_str(), slot->name.c_str());
        std::printf("save: slot %d '%s', %s %s, area %d scene %d, standing at "
                    "%.0f %.0f %.0f facing %.0f\n", slotNo, slot->name.c_str(),
                    omk::formatDate(slot->day).c_str(), omk::formatTime(slot->time).c_str(),
                    state.currentArea(), state.currentScene(),
                    savedAt[0], savedAt[1], savedAt[2], savedYaw);
    }
    // Asking for a SLOT is asking to resume: adventure mode in the save's own
    // area, at its own placement, which is what `Game_LoadSave` does.  A bare
    // `--save FILE` keeps its old meaning - the DB as a starting state, the
    // intro still playing - because every street-start recipe in the tree is
    // written that way and pairs it with `--area`.
    // ...and a LOAD sets it too, below: loading a save is resuming, so the
    // hand-over should not wait on the scene's beats any more than `--slot`
    // does. Not const for that reason.
    bool forceAdventure = areaArg >= 0 || slotArg >= 0;
    // THE INVENTORY, out of the game data: `IAM\OBJECT`'s 1002 records and
    // `IAM\GLOBAL +12`'s eleven combination recipes. `script/inventory.h` was
    // written, checked and never consumed by anything that runs - the sneak
    // is what the channel exists for, so this is where it is loaded.
    // `--money N`: the player record's `+172`, the seteks, written before the
    // first frame so a purchase can be driven from a save that has none (the
    // shipped fixture reads 0). A HARNESS write like `--give`, not anything
    // the game does.
    if (moneyArg >= 0) {
        state.setMoney(std::min(moneyArg, 0xFFFF));
        std::printf("--money: the player record's +172 set to %d (a harness write)\n",
                    state.money());
    }
    // `--rings N`: the ANNEAUX at `+174`, the other half of the same pair.
    // A save costs one and a hint three, and both shipped fixtures carry
    // two - so neither purchase can be driven from them without this. A
    // HARNESS write, like `--money` and `--give`.
    if (ringsArg >= 0) {
        state.setRings(std::min(ringsArg, 0xFFFF));
        std::printf("--rings: the player record's +174 set to %d (a harness write)\n",
                    state.rings());
    }
    if (!giveList.empty()) {
        int placed = 0, refused = 0;
        std::string cur;
        for (char ch : giveList + ",") {
            if (ch != ',') { cur.push_back(ch); continue; }
            if (cur.empty()) continue;
            // `LIST:ID` names the list, a bare `ID` means list 0. Op 49
            // `var.set.has_object`'s FIELD 0 is the list and field 1 the
            // object, and the lists are not interchangeable: the flat's lift
            // gate asks `has_object 1, 3, 20` - list ONE for the police card
            // - so a bag written only into list 0 never satisfies it, and the
            // cutscene behind that gate could not be reached at all.
            int list = 0;
            std::string idPart = cur;
            const auto colon = cur.find(':');
            if (colon != std::string::npos) {
                list = std::atoi(cur.substr(0, colon).c_str());
                idPart = cur.substr(colon + 1);
            }
            const int id = std::atoi(idPart.c_str());
            cur.clear();
            if (id <= 0) continue;
            // `debugPutObject` fills the FIRST free slot, so the ids land in
            // the order they are given - which is the reverse of what the
            // game's own `ObjectList_InsertFront` would do, and is fine for a
            // harness whose point is to have a bag at all.
            if (state.debugPutObject(list, id)) ++placed;
            else { ++refused; std::printf("--give: no free slot for object %d "
                                          "in list %d\n", id, list); }
        }
        std::printf("--give: %d object%s put in the named list%s, %d refused "
                    "(a harness write, not `inventory.add`)\n",
                    placed, placed == 1 ? "" : "s",
                    giveList.find(':') == std::string::npos ? " (0, carried)" : "",
                    refused);
    }
    if (!varList.empty()) {
        std::string cur;
        int wrote = 0;
        for (char ch : varList + ",") {
            if (ch != ',') { cur.push_back(ch); continue; }
            const auto eq = cur.find('=');
            if (eq != std::string::npos) {
                const int id = std::atoi(cur.substr(0, eq).c_str());
                const int v  = std::atoi(cur.substr(eq + 1).c_str());
                state.setVar(id, v);
                std::printf("--var: VARIABLES[%d] = %d\n", id, v);
                ++wrote;
            }
            cur.clear();
        }
        std::printf("--var: %d variable%s written straight into the DB "
                    "(a harness write, not a script)\n", wrote,
                    wrote == 1 ? "" : "s");
    }
    const auto objectRecords = omk::loadObjects(fs);
    const auto globalFile = fs.read("IAM/GLOBAL");
    const auto recipes = omk::globalRecipes(globalFile);
    omk::Inventory inv(objectRecords, recipes);
    // `GLOBAL +16` - the sneak's slider destinations, 39 of them.
    const auto destinations = omk::globalDestinations(globalFile);
    bool sliderTold = false;
    std::string examineTold;
    std::string examineText;
    // The memo the body box was fed this frame, reported AFTER the draw with
    // the lines the composer actually laid out - a line printed beside the
    // fill says what was intended, not what was drawn, and a mutation that
    // cut the text off from the composer passed exactly that way.
    std::string memoBodyPending;
    if (objectRecords.empty())
        std::printf("no IAM/OBJECT - the sneak's inventory page will be "
                    "empty\n");
    omk::Session session(fr + "/IAM", state, opcodes);
    if (bankReject) {
        session.setBankReject(true);
        std::printf("DEBUG --bank-reject: every bank refused, the object stays in hand - "
                    "NOT the original's rule\n");
    }
    if (!session.loadAnnounceMap(tb + "/vm_announce.json"))
        std::printf("tables: no vm_announce.json - the log will name fewer "
                    "operands\n");
    // A person answers the screens only when there ARE screens to draw.
    session.answerUiFromPerson(w.valid());
    // There is a frame clock here, so `camera.set.wait` can do what the
    // handler does: hold the script for the length of the move it started.
    // Without it AREA 118's six intro cameras and its `area.goto` all happen
    // in one frame and the introduction is never seen.
    session.setCameraWait(true);
    // And the waiting `scx.play*` variants, which is the beat before the
    // intro's conversation: AREA 118 shows Kay'l and starts his animation with
    // `scx.play.actor.wait`, holding the script while the camera travels.
    session.setObjectWait(true);
    // And a conversation takes as long as its own voice does. Without this the
    // frontend closed each one on the next frame - a labelled stand-in that
    // got the intro's SHAPE wrong, because the grid tunnel AREA 118 cuts to
    // AFTER `dialog.start 272` then arrived two seconds after the menu instead
    // of three minutes. `src/script/dialogue.h` carries the reasoning; the
    // decision is on the ported side and this file only plays the audio.
    session.attachDialogue(fr + "/MORPH");
    // STREET LIFE: the pedestrians of an area naming a circuit spawn at its
    // load, at the density the options menu will one day hand in.
    session.setStreetActivity(density);
    session.setMusicOption(settings.v.volumeMusic);   // options row 11
    session.setDataRoot(fr);   // IAM\OBJECT for the kind ladder, crowd or not
    if (!noCrowd) session.loadTraffic(fr);
    // A movie chain is the intro's; a street start skips it.
    if (forceAdventure) playMovies = false;
    const int startArea = areaArg >= 0 ? areaArg : state.currentArea();
    if (startArea < 0) { std::fprintf(stderr, "IAM/START names no area\n"); return 1; }
    session.loadArea(startArea);
    // ...with the SET it names, because an area whose chunk did not arrive
    // (an unreadable IAM\AREA) still "loads" - with no script, no set and no
    // scene, and a run that never starts (a console, 2026-09-18)
    std::printf("session: area %d loaded, set '%s', waiting for its script\n",
                startArea, session.setName().c_str());
    // A HARNESS, and the narrowest one in this viewer: `zone.enable N`, the
    // opcode itself, on a zone the STORY would have enabled. Everything after
    // it is the game's own path - the player walks in, the zone fires its own
    // enter script, and that script is what runs `shoot.begin` and the
    // `shoot.actor.enter` / `.action` calls. AREA 141's 'Start Shoot' (2295)
    // is enabled by the Nout book cutscene, which a headless run cannot reach;
    // with it enabled the catacombs' ten spectres enter on ACTION 1 and
    // PATROL (`todo/shoot-patrol.md` 5a).
    // `--zone-disable N`: the mirror, for a start that skipped the script which
    // would have disabled it - the supermarket harness never runs AREA 231's
    // record 1, the airlock cutscene in, so its zone 3949 stays enabled from a
    // save made before the supermarket and walking OUT re-fires that cutscene.
    // `--scene-load A,S`: opcode 71, `scene.load`, and nothing else. Over an
    // area that is not resident it only RECORDS the scene in the DB, and
    // `Area_Load` brings it in when the area loads - which is how the story
    // puts SCENE 56 over the supermarket before the player walks in from the
    // airlock. `--scene-chunk` starts INSIDE the area instead, skipping the
    // airlock cutscene (AREA 231 record 1) the real path plays.
    for (const auto& [sa, ss] : sceneLoads) {
        session.sceneLoad(sa, ss);
        std::printf("--scene-load: SCENE %d recorded over AREA %d (the `scene.load` "
                    "opcode, nothing else)\n", ss, sa);
    }
    for (const int z : zoneDisable) {
        session.disableZoneById(z);
        std::printf("--zone-disable: ZONE %d disabled (the `zone.disable` opcode, nothing "
                    "else)\n", z);
    }
    for (const int z : zoneEnable) {
        session.enableZoneById(z);
        std::printf("--zone-enable: ZONE %d enabled (the `zone.enable` opcode, nothing "
                    "else) - walk into it and its own script runs\n", z);
    }
    if (sceneChunk >= 0) {
        session.sceneLoad(startArea, sceneChunk);
        std::printf("--scene-chunk: SCENE %d over AREA %d - its startup script "
                    "runs, which is what SHOWS an area's props\n",
                    sceneChunk, startArea);
    }
    if (forceAdventure) {
        const auto& rs = session.residentSlot(session.activeSlot());
        for (const auto& ad : rs.addresses)
            std::printf("address %d at %.0f %.0f %.0f yaw %.0f\n", ad.id, ad.pos[0], ad.pos[1], ad.pos[2], ad.yaw);
        // THE PRECEDENCE, and the save's own placement is now in it: an
        // explicit `--stand` or `--address` is the reader's word and wins;
        // otherwise a save that was made IN THIS AREA says where he stood;
        // otherwise the area's first ADDRESSES record, which is the harness
        // default and was previously the only one.  The area test matters -
        // `--save X --area 0` deliberately drops a save's player record into
        // a city he was never in, and his apartment coordinates mean nothing
        // there.
        const bool useSaved = haveSavedPlacement && !haveStand && addressArg < 0 &&
                              state.currentArea() == startArea;
        if (useSaved) {
            session.setPlayerPosition(savedAt, savedYaw);
            std::printf("save: the player stands where the save left him, "
                        "%.0f %.0f %.0f facing %.0f\n",
                        savedAt[0], savedAt[1], savedAt[2], savedYaw);
        }
        if (!useSaved && addressArg < 0 && !rs.addresses.empty())
            addressArg = rs.addresses.front().id;
        if (addressArg < 0 && haveStand) {
            // an area with no ADDRESSES table (the Impasse airlock, 142):
            // `--stand` is the only placement there is, so it is the one
            session.setPlayerPosition(standAt, standAt[3]);
            std::printf("street start: no address in area %d - --stand places the player at "
                        "%.0f %.0f %.0f facing %.0f\n", startArea, standAt[0], standAt[1], standAt[2], standAt[3]);
        }
        if (addressArg >= 0) {
            if (session.placeActorAt(addressArg))
                std::printf("street start: the player at address %d, %.0f %.0f %.0f facing %.0f\n",
                            addressArg, session.playerPos()[0], session.playerPos()[1],
                            session.playerPos()[2], session.playerYaw());
            else
                std::printf("street start: address %d is not in area %d\n", addressArg, startArea);
        }
        // `--ride`: MOUNT him where he now stands. `MDSLIDIN`'s own gate is
        // ACTOR_STATE 6 plus a slider standing OPEN (its mode 3), and neither
        // exists here - this is the harness, and it says so.
        if (rideArg) {
            omk::SliderRide r;
            r.x = session.playerPos()[0];
            r.y = session.playerPos()[1];
            r.z = session.playerPos()[2];
            r.yaw = session.playerYaw();
            ride = r;
            std::printf("ride: mounted at %.0f %.0f %.0f facing %.0f - the "
                        "harness, NOT `MDSLIDIN` (which wants ACTOR_STATE 6 "
                        "and a slider in mode 3)\n", r.x, r.y, r.z, r.yaw);
        }
        // ...and the camera a hand-over ends on: `Camera Player` (0), the
        // follow preset, which the intro's scripts request and this has to.
        session.requestCamera(0, 0);
        const auto& pd = session.sliders();
        std::string models;
        for (const auto& m : pd.models()) { if (!models.empty()) models += ","; models += m.name; }
        std::printf("street life: circuit %s, %d walkers at density %d (%s)\n",
                    rs.opt.empty() ? "none" : rs.opt.c_str(), pd.liveCount(), pd.streetActivity(),
                    models.empty() ? "no models" : models.c_str());
    }
    // The area's own `.SCX`, so `scx.play*` has objects to start - and so the
    // WAITING variants (46, 58, 60) have something to wait ON. Without it
    // AREA 118's `character.show 310` + `scx.play.actor.wait 310, 1` starts
    // nothing and the script runs straight into `dialog.start`, which is the
    // ~5 second beat before the conversation going missing.
    // The scene's `.SFX` too - `AREA +97` names both, and starting an object
    // fires the set pieces keyed to it.
    if (session.loadScene(fr + "/SCPTDATA", omk::ChunkKind::Area, startArea))
    {
        std::string sfxName = session.scene().file();
        const auto dot = sfxName.rfind('.');
        if (dot != std::string::npos) sfxName = sfxName.substr(0, dot) + ".sfx";
        session.sceneMutable().attachSfx(fr + "/SCPTDATA", sfxName);
        const auto& sf = session.scene().sfx();
        std::printf("scene: %s resident, %zu objects; %s: %zu effects, "
                    "%zu set pieces (%d of them keyed so this trigger cannot "
                    "reach them)\n",
                    session.scene().file().c_str(),
                    session.scene().scene().scene().objects.size(),
                    sfxName.c_str(), sf.effects.size(), sf.pieces.size(),
                    session.scene().standingPieces());
    }
    else
        std::printf("scene: area %d names no .SCX\n", startArea);


    // The screen currently on the player's hands, if any. `walk` is only
    // constructed once a script has actually asked for a screen - or, since
    // the sneak, once the PLAYER has.
    std::unique_ptr<omk::UiWalk> walk;
    int openScreen = -1, conversations = 0, lastArea = -1;
    OMK_HEAPCHECK("before sneak call");
    omk::LoadPanel loadPanelState;   // rebuilt each time a screen opens
    int  pendingLoadSlot = -1;       // `dword_4C09B4`
    bool quitRequested = false;      // `dword_4E6C9C`, the pause screen's Oui
    bool exitProgram = false;        // the start menu's Oui - WM_QUIT
    // `dword_670BF0`: a sneak CALL is up. Set by the videophone's open arm,
    // cleared by the first close attempt.
    bool videophoneCall = false;
    bool videophoneSpoke = false;   // a line or a voice-over has played
    bool callHarness = false;       // `--call` opened this one
    int  callPending = -1;          // ...and the conversation it owes
    // THE LOADING SEQUENCE IS NOT WIRED HERE, and a first version of it
    // was. `Charger` answers **0**, and AREA 118's parked startup script
    // has an arm for exactly that: the Grid fly-through - cameras
    // 2152/2153/2154/2158 over `scx.play 20` with a `media.play 753` - after
    // which the script ends. So the engine covers a load with its own
    // script, and the port only has to answer the right number.
    omk::UiCursor uiCursor;   // Ui_DrawItemCursor's one pool (dword_6A4D20)
    omk::UiListState uiLists; // every list's `+2`, for as long as we run
    // The sneak's three turning previews. Loaded once - the engine loads them
    // in the sneak's OPEN callback and frees them in its close, which for a
    // viewer that opens the device repeatedly is the same three files each
    // time.
    omk::UiModels uiModels;
    if (const int n = uiModels.load(fs))
        std::printf("sneak: %d of 3 preview models loaded (%s...)\n",
                    n, uiModels.name(0).c_str());
    // WHO asked for the open screen, because the two ends differ. `ui.open`
    // parks its caller at status 6 and every close path posts event 5 with
    // the answer, so leaving IS an answer and the script resumes. The sneak
    // has no caller at all: `sub_0046ADF0` calls `UI_OpenScreen(9, -1, ...)`
    // and the `-1` is the waiting-context argument, so `dword_930744` is
    // never written and there is nothing to resume. Answering the Session for
    // it would release whatever script happened to be parked.
    bool screenFromScript = true;
    // The input word as it stood when the current screen opened - see the
    // edge gate below. Cleared once those bits are released.
    std::uint32_t screenOpenBits = 0;
    // A screen the PLAYER asked for this frame, before it is opened below -
    // so the open, its sounds and its bookkeeping stay in one place.
    int playerScreen = -1;
    // The world's action button is spent until it is RELEASED - see the
    // one-activation-per-press gate below. Cleared where `bits` is taken, so
    // a frame that never reaches the gate cannot leave it stale.
    bool actionSpent = false;
    // The sneak's inventory ROWS - item address -> what that row shows. The
    // nine slots of list 0x004DE6F0 ship `+28` as -1 and are never bound,
    // because their text is the carried object list read through the channel
    // (`Game_HandleEvent` 29 and 33), not a string in `IAM\Sneak`.
    std::map<std::uint32_t, std::string> sneakRows;
    // THE ADDRESS MAP, watched. A screen whose script OPENS A PLACE is the whole
    // point of reading it - a reader: *"some scenes can be triggered only if an
    // entry on a terminal has been read"* - and ops 87/88 write a 791-bit map
    // that nothing announced. Kay'l's terminal enables ADDRESSES 33, 'Anekbah -
    // Bar Zone 52', when his fourth dossier has been read, and the memo that
    // goes with it names the mission. Watched per frame and said once per
    // change, so a play log shows what a screen actually unlocked.
    std::vector<std::uint8_t> addrSeen;
    // The row widgets `sub_42AAE0` switches off - past the object count, so
    // tag -1 and `0x40000001` set. Without it every one of the nine rows
    // draws its fill and the page is striped.
    std::set<std::uint32_t> sneakHidden;
    int sneakTold = 0;             // one line per run, not one per frame
    int replySel = 0;            // which reply the player is on
    int actionTold = 0;          // one line for a press that reaches nothing
    // THE ACTION BUTTON IS AN EDGE (omk-play 74). Bit 0x10 arrives as a LEVEL
    // - the world's repeat mask is 0, so a held key is set every frame - and
    // this remembers the previous frame's so the press fires once. See the
    // long note at the dispatch for why the engine needs no such variable.
    // THE PRESS BITS ARE EDGES (omk-play 74). In the world the repeat mask is
    // 0, so every bit arrives as a LEVEL and a held key is set every frame -
    // right for a walk, wrong for anything that COUNTS presses. This is the
    // previous frame's word; `edgeBits` below is the difference.
    std::uint32_t prevBits = 0;
    // `tab_special_move[]` as a TABLE rather than a string compare: a fired
    // move resolves to its ROW - the index the engine dispatches on and the
    // handler address it calls - so an unknown name comes back nullptr
    // instead of falling off the end of an if-chain. The shipped `.CTL` files
    // use 54 distinct names against the table's 66 rows.
    omk::SpecialMoves specialMoves =
        omk::SpecialMoves::loadJson(tb.empty() ? std::string() : tb + "/special_moves.json");
    // The sneak's CITY MAP tables - the four map rectangles and the fifteen
    // place overrides, both `.data` in the executable (`ui/citymap.h`).
    const omk::CityMaps cityMaps =
        omk::CityMaps::loadJson(tb.empty() ? std::string() : tb + "/city_maps.json");
    if (!cityMaps.valid())
        std::printf("sneak map: tables/city_maps.json not read - `Lire plan` will "
                    "bounce back, because no city can be matched\n");
    if (!specialMoves.valid())
        std::printf("special moves: tables/special_moves.json not read - the take will "
                    "still work, but a fired move cannot name its row\n");
    // THE WEAPON TABLES `Shoot_InitWeapon` picks a row from - the rate, the
    // projectile's speed and what it deals (`actor/shootfire.h`).
    const omk::ShootWeaponTable shootWeapons =
        omk::ShootWeaponTable::loadJson(tb.empty() ? std::string() : tb + "/shoot_weapons.json");
    if (!shootWeapons.loaded())
        std::printf("shoot weapons: tables/shoot_weapons.json not read - shoot mode "
                    "will aim and never fire\n");
    // the LOOPING scene voices, keyed the way `Script_StopSound` matches them:
    // by (wav, node). Only loops are kept - a one-shot ends by itself.
    std::map<std::pair<int,int>, int> sceneVoices;
    // omk-play 72: WHICH audio source is the one that will not stop. Every
    // start is labelled with its length, and `flushAudio` says how many
    // one-shots it does NOT clear - the streamed music is flushed on a switch
    // and `shots_` are not, so anything long started as a one-shot outlives
    // an area change, a music switch and a cutscene.
    const auto sfxLog = [&](const char* what, std::size_t samples, int a, int b,
                            float gain = 1.0f, const float* peakOf = nullptr) {
        const double secs = samples / static_cast<double>(kDeviceRate) / 2.0;
        // the PEAK of what is fed, because a reader heard the effects "very
        // low" against the music: the number says whether the source or the
        // mix is quiet. Measured ONCE, where the sample is converted
        // (`SfxSample::peak`) - it was a pass over every sample on every play.
        const float peak = peakOf ? *peakOf : 0.0f;
        const bool pcm = peakOf != nullptr;
        if (secs >= 0.75 || pcm)
            std::printf("audio: %-14s %6.2f s  (%d, %d)  gain %.2f  peak %.3f\n",
                        what, secs, a, b, gain, static_cast<double>(peak));
    };
    int         takeCandidate = -1;      // `dword_53AF6C`, MDACTION's pick
    // Which HEIGHT the take was, kept from MDACTION so the put-back can match
    // it. The engine keeps the same thing in `dword_53AE5C` - `(ret == 2) ? 3
    // : 0`, which `sub_46B530` turns back into a group (omk-play 69).
    bool        takeWasLow = true;
    int         heldInHand = -1;         // the object drawn on the left hand: from MDGETOBJ to the release
    // The spoken line's SCROLL, in pixels, and the overflow it is clamped to -
    // `dword_6A52C0` and `dword_53AE24`. One pixel a tick while held, which is
    // what `Dialog_TickUI` does with input bits 4 (up) and 8 (down).
    int lineScroll = 0, lineOverflow = 0;
    bool menuShown = false;
    int  lastDlgCam = -2;
    // The absolute world cameras the script has set, as rays: during the
    // beat before a conversation they are what aims at the character.
    std::vector<omk::CameraRay> worldRays;
    int lastRayCam = -2;
    // THE CAMERA EDITING - camera mode 13. Every `scx.play*` handler ends by
    // asking `ScriptObject_HasCamEditing` and, when the object has a chunk-10
    // editing linked, requests mode 13 with the call's last field as the
    // travel (`SceneRunner::ActiveEditing` quotes the assembly). Mode 13 is
    // "follow the scene's active camera": the camera tick copies eye, target,
    // fov and roll out of `dword_9103D4` every frame, which is what
    // `Script_PlayScript` sampled from the editing at the OBJECT'S clock. It
    // supersedes the mode-12 world camera `camera.set` chose for as long as
    // the editing runs; when nothing sets an active camera and the mode is
    // still 13, the frame loop requests mode 0 with travel 0 (05_sys.c 2140)
    // - a cut back. The Session knows which editing is driving and what it
    // says; the travel FROM the previous camera is here, because this is
    // where the previous camera is: the one last drawn.
    int   editingShown = -1;                  // the announced editing's program
    bool  haveLastDrawn = false;              // a 3D camera has been drawn
    float lastEye[3] = {0, 0, 0}, lastAt[3] = {0, 0, 0}, lastFov = 75.0f;
    // (struct CtlSpriteInst: `backends/sdl/playtypes.h`, todo/play-split.md)
    std::vector<CtlSpriteInst> ctlSprites;
    int   ctlFxState = -1; float ctlFxFrame = -1.0f;
    // ...and the MELEE OPPONENT's, from his own channel (`todo/fight-mode.md`
    // 15.10): `Cef_TickEffects` runs on every channel the engine ticks, and
    // `Actor_TickPlayerAndOpponent` ticks two.
    std::vector<CtlSpriteInst> foeSprites;
    int   foeFxState = -1; float foeFxFrame = -1.0f;
    long  foeSpritesDrawn = 0;       // particle-frames placed on his bones
    long  foeSpritesPooled = 0;      // ...of which the sprite had a texture slot
    omk::ParticleField ctlField; omk::Geometry ctlGeo;
    bool  takeCam = false;            // `C+12 == 1`: the mode-1 camera is live
    int   takeCamPhase = 0;           // 1 travelling in, 2 holding, 3 travelling back
    float takeCamClock = 0.0f;        // frames since the request
    float takeCamFromEye[3] = {0, 0, 0}, takeCamFromAt[3] = {0, 0, 0}, takeCamFromFov = 75.0f;
    // preset 1: 62 cm to his side, 75 cm above the pelvis, 12 cm back, looking
    // 12 cm up and 50 cm ahead of him. In the mode-0 convention the follow
    // camera uses (`point = subject - R(yaw) * offset`, mode 0 = 3 m behind).
    constexpr float kTakeCamEye[3] = {24.4094f, 29.5276f, -4.7244f};
    constexpr float kTakeCamAt[3]  = {0.0f, 4.7244f, 19.685f};
    constexpr float kTakeCamFov    = 75.0f;
    constexpr float kTakeCamTravel = 30.0f;
    // ...and the same machinery serves any PLAYER-subject preset asked for
    // with a travel: `Camera_Request(mode, block)` with `block+24` the
    // frames. Preset 17 is what `sub_4570F0` asks for when the ride ends -
    // eye (-39.3701, 78.7402, 0) = 1.00 m and 2.00 m, target the actor,
    // subjects 0 and 0, over `dword_930818 = 60.0` frames. The take's
    // preset 1 was the only one wired, as constants.
    float takeCamEye[3] = {kTakeCamEye[0], kTakeCamEye[1], kTakeCamEye[2]};
    float takeCamAt[3]  = {kTakeCamAt[0], kTakeCamAt[1], kTakeCamAt[2]};
    float takeCamFov    = kTakeCamFov;
    float takeCamTravel = kTakeCamTravel;
    auto takeCamRequest = [&](int phase) {
        takeCamPhase = phase;
        takeCamClock = 0.0f;
        for (int k = 0; k < 3; ++k) { takeCamFromEye[k] = lastEye[k]; takeCamFromAt[k] = lastAt[k]; }
        takeCamFromFov = lastFov;
        if (phase == 1) {                       // the take: preset 1, 30 frames
            for (int k = 0; k < 3; ++k) { takeCamEye[k] = kTakeCamEye[k]; takeCamAt[k] = kTakeCamAt[k]; }
            takeCamFov = kTakeCamFov; takeCamTravel = kTakeCamTravel;
        }
    };
    auto playerCamRequest = [&](const float eye[3], const float at[3], float fov, float frames) {
        takeCamRequest(1);
        for (int k = 0; k < 3; ++k) { takeCamEye[k] = eye[k]; takeCamAt[k] = at[k]; }
        takeCamFov = fov; takeCamTravel = frames;
        takeCam = true;
    };
    // THE FALL CAMERAS, `sub_414DE0(actor, mode, flag)` (`todo/falls.md` 1): a
    // player-subject request gated on the mode already up. 18 and 19 are the
    // overhead presets (eye 3 m above him, fov 75); 16 and 0 travel back to the
    // follow camera, the take's own way home. `fallCamMode` is `C+12` for these
    // four; the `C+140` test on 18 is not modelled (untraced) and reads 0.
    int fallCamMode = 0;
    bool fallBanded = false;   // `+1304` set this fall (1, 3 or 4) - once per fall
    auto fallCamRequest = [&](int mode, bool flag, const char* who, int actorState) {
        static constexpr float kOverEye[3] = {0.0f, 118.1102f, -3.937f};
        static constexpr float kOverAt[3]  = {0.0f, 0.0f, 0.0f};
        // `if (a1 && u32(a1, 404) != 3)` - nothing at all in ACTOR_STATE 3
        if (actorState == 3) return;
        float travel = -1.0f;
        if (mode == 18) {
            travel = flag ? 60.0f : 30.0f;
            playerCamRequest(kOverEye, kOverAt, 75.0f, travel);
        } else if (mode == 19 && fallCamMode == 18) {
            travel = 60.0f;
            playerCamRequest(kOverEye, kOverAt, 75.0f, travel);
        } else if ((mode == 16 && fallCamMode == 18) || (mode == 0 && fallCamMode == 19)) {
            travel = mode == 16 ? 30.0f : 90.0f;
            takeCamRequest(3);
            takeCamTravel = travel;
            takeCam = true;
        }
        if (travel < 0.0f) return;
        fallCamMode = (mode == 18 || mode == 19) ? mode : 0;
        std::printf("frame %ld: %s - sub_414DE0 camera %d over %.0f frames\n",
                    session.frameNo(), who, mode, double(travel));
    };
    float lastRoll = 0.0f;              // the camera ROLL, blended like the fov
    // THE CAMERA HOLDS WHEN AN EDITING ENDS, and the fall-back this used to do
    // is a PREFERENCE that ships off. `Game_Frame` (05_sys.c 2144) requests
    // mode 0 on the player only under
    //
    //     else if (Camera_GetMode(C) == 13 && byte_910322 && g_PlayerActorRec)
    //
    // and `byte_910322` is the `[Preferences]` key `autocameraplayer`, read
    // with a default of "0" (Runtime.exe.asm:23252; the only other write in
    // the image is the defaults block that zeroes it, so nothing else can turn
    // it on). With it off the branch never runs, and the camera tick's own
    // mode-13 arm is `if (dword_9103D4) { copy eye/at/fov/roll }` (sub_417CF0,
    // 04_sys.c 3701): a null active camera copies NOTHING, so the block keeps
    // the values it had. The view therefore FREEZES on the editing's last
    // frame until something else requests a camera.
    //
    // Measured before this landed, on the intro path: the Impasse's eight
    // gaps were 0 of 480000 pixels lit - a black frame after every beat and a
    // 73-frame black stretch mid-cutscene - because the Session still held
    // camera 2158, AREA 118's, from the area the player had just left
    // (next-tasks 5).
    bool  musicPaused = false;          // the pause screen suspends the sound
    bool  holdEditCam = false;          // mode 13 with no active camera: hold
    unsigned long heldUnderRequests = 0;  // the Session's request count when the hold began
    bool  editFromKnown = false;              // ...and it was captured for the travel
    float editFromEye[3] = {0, 0, 0}, editFromAt[3] = {0, 0, 0}, editFromFov = 75.0f;
    float editFromRoll = 0.0f;
    bool  rollTold = false;
    int fxSpriteWas = -2;

    OMK_HEAPCHECK("before adventure mode");
    // ---- ADVENTURE MODE ------------------------------------------------
    //
    // The Impasse's cutscene ends `camera.set 0,0,2` + `scene.load 237,57` +
    // `player.anim.release`: the hand-over to the person at the keyboard.
    // Until 2026-09-02 nothing here moved him after it. The controller
    // (`actor/player.h`) is the engine's own chain - the input word into the
    // `.CTL` channel, the clip's root motion out of it, the walker under it,
    // the follow camera behind - and this file only builds it, feeds it the
    // word `Input::frame` makes, and draws where it says.
    //
    // WHEN: the Session's camera is a world camera whose two subjects are
    // actor 0 (SCENE 55's camera 0 - an eye 119 behind and 26 above him,
    // the follow shape `worldcam.h` describes), no scene program is playing
    // the player, he has been placed, and no conversation, screen or
    // editing owns the frame. `player.anim.release` (op 105) is a no-op in
    // the Session, so the program ending is the signal it leaves - which is
    // also what `Actor_TickScxDriven` keys on (the program's `IsBusy`).
    std::unique_ptr<omk::PlayerController> player;
    omk::CtlFile playerCtl;
    // `Player_GoToMove` behind `player.move` (63) and `player.move.wait` (89)
    // - `PlayerController::goToMove`. Neither reached the walker until
    // 2026-09-08: op 63 was recorded and dropped, and 89 ran on because no
    // hook was installed, so the `player.move 100` in front of every staged
    // sequence never stopped him and he played his walk out under the
    // cutscene camera. In AREA 222's tutorial that is 19 units short of the
    // zone he had just been teleported out of.
    //
    // `moveWaitCtx` / `moveWaitGroup` are `dword_930744` / `dword_91068C`:
    // the context a 89 must report to, and the group it entered. `Game_Tick`
    // (0x004200F0) checks them after `Actors_TickAll` and raises event 3 the
    // frame the channel's current entry is in some other group - the release
    // below, before the next pump.
    int moveWaitCtx = -1, moveWaitGroup = -1;
    session.setMoveHook([&](int groupId, int ctx) -> bool {
        if (!player) return false;
        if (!player->goToMove(groupId)) {
            std::printf("frame %ld: player.move%s %d - no group %d in the player's bank, "
                        "the script runs on\n", session.frameNo(),
                        ctx >= 0 ? ".wait" : "", groupId, groupId);
            return false;
        }
        const int g = player->ctlGroup();
        const int st = player->ctlState();
        const char* nm = (st >= 0 && st < static_cast<int>(playerCtl.states.size()))
                             ? playerCtl.states[static_cast<std::size_t>(st)].name.c_str()
                             : "?";
        if (ctx >= 0) { moveWaitCtx = ctx; moveWaitGroup = g; }
        std::printf("frame %ld: player.move%s %d - the channel put on group %d (index %d)'s "
                    "entry '%s' at %.1f %.1f %.1f%s\n", session.frameNo(),
                    ctx >= 0 ? ".wait" : "", groupId, groupId, g, nm,
                    player->pos()[0], player->pos()[1], player->pos()[2],
                    ctx >= 0 ? ", the script parked until the channel leaves it" : "");
        return true;
    });
    std::vector<std::byte> playerCtlData;
    std::vector<omk::Mesh> playerMeshes;
    omk::MeshNameIndex playerBoneIdx;      // rebuilt wherever `playerMeshes` is
    std::vector<omk::Texture> playerTex;
    omk::Geometry playerRest, playerPosed;
    // ...or, where the renderer poses bodies, his REST drawn with one affine a
    // mesh (`todo/gpu-skinning.md` step 5). `playerPosedFrame` is the last
    // frame his corners were built on the CPU - what a corner reader must check.
    std::vector<float> playerAffine;
    long playerPosedFrame = -1;
    std::vector<omk::CollisionSphere> playerSpheres;   // the crowd push tests these
    float playerReach = 0.0f;                           // his model's +88
    omk::TriangleSoup playerSoup;
    // ...and each of its triangles' MESH FLAGS, through the slots' `soupMesh`
    // (`todo/swimming.md`): what the step refusal and the water entry read.
    // Rebuilt whenever the soup it was built for is not the one there now.
    std::vector<std::uint32_t> playerSoupFlags;
    const float* soupFlagsFor = nullptr;
    std::size_t  soupFlagsSize = 0;
    // ...and its probe GRID, rebuilt wherever `playerSoup` is refilled: the
    // shadows and the crowd's feet probe it every frame, and a linear scan of
    // the city per bone was most of the frame (todo/optimization.md step 2)
    omk::SplitSoupGrid playerGrid;
    // Which of `playerSoup`'s triangles belong to a mesh that has moved since the
    // sets last changed: those go in `playerGrid.moving`, rebuilt every moving
    // frame, the rest in `playerGrid.fixed`, rebuilt only when this changes
    // (todo/optimization.md step 7c).
    std::vector<std::uint8_t> playerMovingTri, playerFixedTri;
    std::vector<std::uint32_t> playerMovingIds;   // the same set, ascending: the moving layer's build list
    const auto rebuildFixedGrid = [&]() {
        playerFixedTri.assign(playerMovingTri.size(), 0);
        for (std::size_t t = 0; t < playerMovingTri.size(); ++t) playerFixedTri[t] = !playerMovingTri[t];
        playerGrid.fixed = omk::buildSoupGrid(playerSoup, 256.0, &playerFixedTri);
    };
    const auto rebuildMovingGrid = [&]() {
        // from the id list, NOT the mask: the mask build walks all of the soup
        // (15137 triangles in Anekbah) to find the ~750 that move
        playerGrid.moving = omk::buildSoupGrid(playerSoup, 256.0,
                                               std::span<const std::uint32_t>(playerMovingIds));
        playerGrid.useParts = false;    // the grid stands for the moving layer again
    };
    // True while `playerSoup` / `playerSteep` are exactly the shown slots' soups
    // concatenated - set by a full merge, cleared by a slot load - so a moving
    // mesh can be copied in at its offset instead of re-merging everything.
    bool mergedValid = false;
    // the same merge for the STEEP faces, so the controller can stand on a
    // slope and slide off it instead of finding no floor (omk-play 67)
    omk::TriangleSoup playerSteep;
    // ...and ITS two-layer grid, kept exactly as `playerGrid` is kept over
    // `playerSoup` - rebuilt wherever `playerSteep` is refilled, the moving
    // layer from the steep triangles the motion patch re-places. The player's
    // body sweep and the camera's sweep walk it (todo/optimization.md step 11).
    omk::SplitSoupGrid playerSteepGrid;
    std::vector<std::uint8_t> steepMovingTri, steepFixedTri;
    std::vector<std::uint32_t> steepMovingIds;
    const auto rebuildSteepFixedGrid = [&]() {
        steepFixedTri.assign(steepMovingTri.size(), 0);
        for (std::size_t t = 0; t < steepMovingTri.size(); ++t) steepFixedTri[t] = !steepMovingTri[t];
        playerSteepGrid.fixed = omk::buildSoupGrid(playerSteep, 256.0, &steepFixedTri);
    };
    const auto rebuildSteepMovingGrid = [&]() {
        playerSteepGrid.moving = omk::buildSoupGrid(playerSteep, 256.0,
                                                    std::span<const std::uint32_t>(steepMovingIds));
        playerSteepGrid.useParts = false;
    };
    std::string playerModel, playerCtlName;
    bool  playerReady = false, adventure = false, followCam = false;
    // A `scx.play.player` program owns his body right now (op 46/90).
    bool  playerProgram = false;
    bool  playerProgramWas = false;   // ...and did last frame, for the hand-back
    bool  mirrorLive = false;         // the set's mirror is reflecting, said once
    bool  mirrorSeen = false;         // ...and has actually covered a pixel
    float playerHeadAt[3] = {0, 0, 0};   // the player's `Tete`, for subject kinds 0/1
    float playerHeadRise = 0.0f;         // the DRAWN head over the pelvis (Y up), the swim log
    bool  playerHeadKnown = false;
    std::vector<float> playerMeshAt;     // ...and every mesh, for the shadow
    std::vector<float> playerMeshRot;    // ...and each one's world rotation, nine
                                         // floats a mesh like `Staged::meshRot` -
                                         // what the hit sweep turns his box by
    bool  playerMeshAtKnown = false;
    int   placementSeen = 0;      // Session::placementSeq() as last consumed
    long  heldFrames = 0;         // frames under player.anim.hold
    // The `media.play` SUBTITLE: `Subtitle_Show(unk_4E6268)` is step 13 of
    // the handler (todo/pending/E1.md 1) - the ZVO record's +280 description,
    // `{C}`-prefixed when the player is in ACTOR_STATE 3 or 15, on screen for
    // 80 ms a character and never less than two seconds (`Subtitle_Show`,
    // readable/src/05_sys.c). The airlock's line 410 is the first the port
    // shows: its voice is a JINGOFF3 substitute, and the TEXT is what the
    // player reads.
    std::string mediaText;
    // In FRAMES AT 30 HZ, run down by the delta (todo/sixty-fps.md 2)
    double mediaTextFrames = 0;
    // THE MEDIA BITMAP - `media.play` on a kind-16 DOCUMENT.
    //
    // `if (rec[+2] == 16)` takes the other arm entirely: build
    // `IMAGES\<stem>.BMP`, `I2D_LoadBitmap` it, put the player in ACTOR_STATE
    // **10** (`ImageScreen`, "a full-screen bitmap holds it") and play NO
    // audio. It stays up until the NEXT `media.play`, which frees it (step 7,
    // `I2D_FreeBitmap` then ACTOR_STATE 1).
    //
    // That is the game's TITLE CARD: object 715 `ZVO G001 TITRE` is kind 16
    // with stem `ZVOG001`, so its `+280` description is `{X030040}{f3}` and
    // nothing else - the words are in `IMAGES/ZVOG001.BMP`, 640x480 with the
    // logo on black. Nothing here drew it, which is why the Bowie opening
    // came up without its title (`todo/omk-play.md` 59).
    omk::Surface mediaBmp;
    float playerFeet = 0.0f;
    bool  playerFeetKnown = false;
    // ...AND THE PELVIS TRANS THE ANCHOR WAS LATCHED AT, which is the whole of
    // `todo/player-vertical.md` step 1. `playerFeet` is a POSE's lowest corner
    // and every pose carries its own pelvis translation, so an anchor latched
    // without recording that translation has no shared origin with the drop
    // measured below - see the long note at the latch.
    float playerRootRef = 0.0f;
    bool  playerStandLatched = false;   // the anchor came from H_STAND, not from whatever was up
    // The model-space x/z of the hierarchy root - the PELVIS - which is what
    // a turn must pivot about. `HO1_FN`'s is (2.87, 17.94); rotating about
    // (0,0) instead swings him around a point half a metre away.
    float playerRootXZ[2] = {0.0f, 0.0f};
    float lastRootDrop = 0.0f;         // the crouch's root drop as drawn last frame (the held prop rides it)
    int   playerCamId = -2;
    // The facing at the hand-over. `Actor_TickScxDriven` sets +1308 when
    // the player's program ends and `Actor_TickNpc` then derives the facing
    // from the node's matrix - which after a scene clip is the clip's root
    // rotation at the frame reached. Tracked while the program runs, since
    // `scene.load` replaces the runner and its clips with it.
    float handoverFacing = 0.0f;
    bool  handoverFacingKnown = false;
    // A player program has RUN in this area: the hand-over is its ending,
    // not its absence. SCENE 55's startup script opens with `camera.set 0`
    // before any beat starts, so a camera-only signal fired a frame into
    // the Impasse with GRID's floor and the DB record from before
    // `player.become 49` - the first headless run showed exactly that.
    // Reset on every area change; an area whose scene has no programs at
    // all (a plain arrival) needs no beat to end.
    bool  playerDrivenSeen = false;
    int   playerDrivenArea = -1;
    double frameSec = 1.0 / 30.0;
    // GAME TIME in frames at 30 Hz: the sum of the deltas, so it stands still
    // under the pause and runs at the same speed at any presentation rate.
    // `n` counts PRESENTED frames and is the clock of the harness and the
    // log, never of anything the game times (todo/sixty-fps.md 2).
    double gameClock = 0.0;
    // (struct HoldRun: `backends/sdl/playtypes.h`, todo/play-split.md)
    std::vector<HoldRun> holds;
    {
        std::string cur;
        for (char c : holdStream + ",") {
            if (c != ',') { cur.push_back(c); continue; }
            if (cur.empty()) continue;
            HoldRun r;
            const auto star = cur.find('*');
            const std::string head = star == std::string::npos ? cur : cur.substr(0, star);
            r.frames = star == std::string::npos ? 1 : std::atoi(cur.c_str() + star + 1);
            if (!head.empty() && head[0] == 'k') {
                std::string k;
                for (char h : head.substr(1) + "+") {
                    if (h == '+') { if (!k.empty()) r.keys.push_back(std::atoi(k.c_str())); k.clear(); }
                    else k.push_back(h);
                }
            }
            holds.push_back(r);
            cur.clear();
        }
    }
    long handoverFrame = -1;

    OMK_HEAPCHECK("before interface sounds");
    // ---- the INTERFACE SOUNDS.
    //
    // `docs/UI.md`: every screen names up to twelve, and the slots are
    // POSITIONAL - 0 is the selection move, 1 the confirm, 2 the screen
    // opening. The start menu's are 1, 2, 0, which the table resolves to
    // `men002`, `men003`, `men001`. Both halves were already lifted and
    // nothing was playing them.
    auto& sndMove = game.audio.sndMove;
    auto& sndConfirm = game.audio.sndConfirm;
    auto& sndBack = game.audio.sndBack;
    auto& optSndMove = game.audio.optSndMove;
    auto& optSndConfirm = game.audio.optSndConfirm;
    auto& optSndBack = game.audio.optSndBack;
    const omk::UiWalk* optWalkSeen = nullptr;   // the walk of 29 the options were opened for
    bool optEntered = false;                    // focused for this visit to panel 0x004CF420
    std::pair<int, int> pendingDisplay{0, 0};   // options row 2, served between frames
    const auto loadSlot = [&](int screen, int slot) {
        const std::string& nm = w.soundName(screen, slot);
        if (nm.empty()) return std::vector<float>{};
        const auto path = fs.resolve("I2D/sounds/" + nm + ".wav");
        if (!path) return std::vector<float>{};
        return wavToDevice(omk::DataFs::readPath(*path), kDeviceRate);
    };

    SdlFrontend front;
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
    SDL_Window* vkWin = nullptr;
    omk::Renderer* vkRen = nullptr;
    omk::Renderer* worldVk = nullptr;   // --world-vulkan, the offscreen harness
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
    SDL_Window* glWin = nullptr;
    omk::Renderer* glRen = nullptr;
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
    double glSwapMs = 0.0;   // SDL_GL_SwapWindow's share, for the phase line
    (void)glSwapMs;
    const auto present = [&](const omk::Surface& pic) {
#if defined(OMK_VULKAN)
        if (vkRen) { omk::vulkanPresentSurface(vkRen, pic); return; }
#endif
#if defined(OMK_GLES)
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
            return;
        }
#endif
        front.present(pic);
    };
    // The name field is real typing, so ask the host for characters. Not on
    // the Vita: there SDL answers with the system's on-screen keyboard, which
    // would cover the game from the first frame (`todo/vita-port.md` F4).
#if !defined(__vita__)
    SDL_StartTextInput();
#endif
    // Queued, not mixed: a menu plays one blip at a time and the device is a
    // FIFO. Flushing first keeps them prompt - a blip that waits behind the
    // previous one arrives after the selection has already moved on.
    // Mixed OVER whatever is streaming, not queued behind it and not flushing
    // it - which is what lets a blip and the music coexist.
    const auto blip = [&](const std::vector<float>& v) { front.playSound(v); };

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
    const auto& adpcmTables = game.audio.adpcmTables;
    auto& music = game.audio.music;
    auto& playingTrack = game.audio.playingTrack;
    // The cutscene VOICES - `media.play` (op 92). `sub_41B200` plays one
    // through the morph streamer after a `Morph_Stop()`, so ONE at a time and
    // a second cuts the first (src/audio/voiceover.h).
    auto& voiceLib = game.audio.voiceLib;
    voiceLib.load(fs);
    auto& voices = game.audio.voices;
    auto& voiceOverShot = game.audio.voiceOverShot;

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
    // ---- THE SPEAKER --------------------------------------------------
    //
    // A conversation's cameras all aim at the character speaking it, so
    // without him they aim at nothing and the frame is black - which is what
    // the replica drew. Three things resolve him, and all three are the
    // engine's own:
    //
    //   * WHICH model: the DIALOG chunk's word 0 is the speaker's actor id,
    //     and the 276-byte actor record carrying that id at +272 names the
    //     model at +144 (`sub_40B190` is the scan);
    //   * WHERE he stands: the least-squares convergence of the line cameras'
    //     rays, dropped onto the walkable floor (`actor/speaker.h`);
    //   * HOW he is posed: the line's own `.3DM`, whose per-frame node
    //     quaternions compose down the mesh hierarchy (`actor/pose.h`).
    //
    // The pose advances with the VOICE - frame = elapsed * 30 - because that
    // is the clock the line runs on.
    // What is left here belongs to the LINE, not to a body: the model name,
    // the `.3DM` and its face vertices, the voice, and the camera solve that
    // says where a speaker no scene object drives is standing. The GEOMETRY
    // moved into `Staged`/`CharModel` above, one per actor (issue 41).
    std::vector<omk::Mesh> speakerMeshes;
    omk::NodeTracks speakerTracks;
    // The scene clip's frame the last time it was drawn, and the frame it was
    // on when the current line began - `Morph_Play` hands the morph player
    // the actor's clip AND its frame (rec[47]), and the blend-in eases from
    // that frame into the line (pose.h, BLENDING TWO POSES).
    float sceneFrameLast = 0.0f, lineIdleFrame = 0.0f;
    // the line's .3DM, for the FACE - the conversation's own bytes, shared
    std::shared_ptr<const std::vector<std::byte>> speakerMorph;
    std::string speakerModel, speakerVoice;
    int voiceShot = -1;   // the line's voice in the mixer, for the press that cuts it
    float speakerAt[3] = {0, 0, 0};      // the camera solve, a GROUND point
    bool  speakerSolved = false;
    bool  speakerReady = false;
    int   speakerConv = -1;
    // Whether the player's actor is in DIALOGUE MODE, so the two sides of
    // `Actor_EnterDialogueMode` / `Actor_LeaveDialogueMode` are called once
    // each per conversation. See the transition below.
    bool  dialogMode = false;
    bool  shootMode  = false;      // ops 80/81, `actor/shootmode.h`

    OMK_HEAPCHECK("before every body");
    // (struct CharModel: `backends/sdl/playtypes.h`, todo/play-split.md)
    // (struct CharBank: `backends/sdl/playtypes.h`, todo/play-split.md)
    // `std::map` is node-based, so a `Staged`'s pointer into these survives
    // every later insert.
    std::map<std::string, CharModel> charModels;
    // (struct PropModel: `backends/sdl/playtypes.h`, todo/play-split.md)
    std::map<std::string, PropModel> propModels;
    omk::Geometry propGeo;                 // the shown props, in world space
    // Which model each of `propGeo`'s batches came from: the geometry is
    // built before the pool assigns the sections their bases, so a batch's
    // slot is resolved at submission through its owner.
    std::vector<const PropModel*> propBatchOwner;
    std::set<int> propsTold;               // one line per prop, not per frame
    std::map<std::string, CharBank>  charBanks;
    // (struct Staged: `backends/sdl/playtypes.h`, todo/play-split.md)
    // OWNING POINTERS, not a vector of values: the Vulkan backend caches a
    // vertex buffer by (pointer, revision), so a `Staged` may never be moved
    // by a reallocation. Dropping one frees its address, which a later one
    // could reuse - `posed.revision` is taken from the global `worldGeoRev`
    // every frame it is drawn, so a reused address can never carry a
    // revision the backend has already seen.
    std::vector<std::unique_ptr<Staged>> staged;
    // (struct PedJob: `backends/sdl/playtypes.h`, todo/play-split.md)
    std::vector<PedJob> pedJobs;

    // (struct PedStaged: `backends/sdl/playtypes.h`, todo/play-split.md)
    std::vector<std::unique_ptr<PedStaged>> pedStaged;
    // level-k tracks: the level-0 ones with every mesh index moved k skeletons on
    std::map<std::pair<const omk::NodeTracks*, int>, omk::NodeTracks> pedLodTracks;
    std::map<std::string, std::map<int, omk::Geometry>> pedLodRest;   // model -> root mesh -> its subtree's rest
    // The skeleton a set of tracks poses: the first track's mesh followed up
    // to its root. A model with one skeleton answers its only root.
    const auto skeletonRootWalk = [](const CharModel& mo, const omk::NodeTracks& t) -> int {
        int m = -1;
        if (!t.ids.empty() && t.ids[0] >= 0)
            for (std::size_t j = 0; j < mo.meshes.size(); ++j)
                if (mo.meshes[j].index == t.ids[0]) { m = static_cast<int>(j); break; }
        if (m < 0) return mo.root;
        for (int guard = 0; guard < 64; ++guard) {
            const std::int32_t pid = mo.meshes[static_cast<std::size_t>(m)].parent;
            int next = -1;
            for (std::size_t j = 0; j < mo.meshes.size(); ++j)
                if (mo.meshes[j].id == pid) { next = static_cast<int>(j); break; }
            if (next < 0) return m;
            m = next;
        }
        return m;
    };
    // ...cached on the model by the first track's mesh id, the only thing
    // of the tracks the walk reads
    const auto skeletonRootOf = [&](const CharModel& mo, const omk::NodeTracks& t) -> int {
        const std::int32_t key = t.ids.empty() ? std::numeric_limits<std::int32_t>::min() : t.ids[0];
        const auto it = mo.skelRootByFirstId.find(key);
        if (it != mo.skelRootByFirstId.end()) return it->second;
        const int r = skeletonRootWalk(mo, t);
        mo.skelRootByFirstId.emplace(key, r);
        return r;
    };
    const auto hasSeveralSkeletons = [](const CharModel& mo) {
        if (mo.severalSkeletons >= 0) return mo.severalSkeletons == 1;
        int roots = 0;
        for (const auto& m : mo.meshes) {
            bool hasParent = false;
            for (const auto& p : mo.meshes) if (p.id == m.parent) { hasParent = true; break; }
            if (!hasParent) ++roots;
        }
        mo.severalSkeletons = roots > 1 ? 1 : 0;
        return roots > 1;
    };
    const auto lodChainOf = [](const CharModel& mo) -> int {
        if (mo.lodLevels >= 0) return mo.lodLevels;
        mo.lodLevels = 0;
        const std::size_t nm = mo.meshes.size();
        for (std::size_t j = 0; j < nm; ++j)
            if (mo.meshes[j].index != static_cast<std::int32_t>(j)) return 0;   // the rule is by index
        std::unordered_map<std::int32_t, int> byId;
        for (std::size_t j = 0; j < nm; ++j) byId.emplace(mo.meshes[j].id, static_cast<int>(j));
        std::vector<int> treeOf(nm, -1);
        std::vector<int> roots;
        for (std::size_t j = 0; j < nm; ++j) {
            int m = static_cast<int>(j);
            for (int guard = 0; guard < 64; ++guard) {
                const auto it = byId.find(mo.meshes[static_cast<std::size_t>(m)].parent);
                if (it == byId.end() || it->second == m) break;
                m = it->second;
            }
            treeOf[j] = m;
            if (m == static_cast<int>(j)) roots.push_back(m);
        }
        if (roots.size() < 2 || roots.size() > 4) return 0;
        // largest first, by the corners each draws (`sub_453A70`: vertex +
        // face count - the order is what matters, and corners are faces x 3)
        std::map<int, std::size_t> size;
        for (std::size_t c = 0; c < mo.rest.cornerMesh.size(); ++c) {
            const auto mi = mo.rest.cornerMesh[c];
            if (mi >= 0 && static_cast<std::size_t>(mi) < nm) ++size[treeOf[static_cast<std::size_t>(mi)]];
        }
        std::stable_sort(roots.begin(), roots.end(), [&](int a, int b) { return size[a] > size[b]; });
        int count = 0;
        for (std::size_t j = 0; j < nm; ++j) count += treeOf[j] == roots[0];
        for (std::size_t k = 1; k < roots.size(); ++k) {
            int cnt = 0;
            for (std::size_t j = 0; j < nm; ++j) {
                cnt += treeOf[j] == roots[k];
                if (treeOf[j] != roots[0]) continue;
                const std::size_t jj = j + k * static_cast<std::size_t>(count);
                if (jj >= nm || treeOf[jj] != roots[k]) return 0;
            }
            if (cnt != count) return 0;
        }
        for (std::size_t k = 0; k < roots.size(); ++k) {
            mo.lodRootAt[k] = roots[k];
            mo.lodMask[k].assign(nm, 0);
            for (std::size_t j = 0; j < nm; ++j) mo.lodMask[k][j] = treeOf[j] == roots[k] ? 1 : 0;
        }
        mo.lodCount = count;
        mo.lodLevels = static_cast<int>(roots.size());
        return mo.lodLevels;
    };
    const auto lodTracksFor = [&](const omk::NodeTracks* base, int level, int count) -> const omk::NodeTracks* {
        if (!base || level == 0) return base;
        const auto key = std::make_pair(base, level);
        auto it = pedLodTracks.find(key);
        if (it == pedLodTracks.end()) {
            omk::NodeTracks t = *base;
            for (auto& id : t.ids) if (id >= 0) id += level * count;
            it = pedLodTracks.emplace(key, std::move(t)).first;
        }
        return &it->second;
    };
    const auto lodRestFor = [&](const std::string& model, const CharModel& mo, int rootMesh) -> const omk::Geometry& {
        auto& per = pedLodRest[model];
        auto it = per.find(rootMesh);
        if (it != per.end()) return it->second;
        // the meshes whose ancestor chain ends at `rootMesh`
        std::vector<bool> keep(mo.meshes.size(), false);
        for (std::size_t i = 0; i < mo.meshes.size(); ++i) {
            int m = static_cast<int>(i);
            for (int guard = 0; guard < 64 && m >= 0; ++guard) {
                if (m == rootMesh) { keep[i] = true; break; }
                const std::int32_t pid = mo.meshes[static_cast<std::size_t>(m)].parent;
                int next = -1;
                for (std::size_t j = 0; j < mo.meshes.size(); ++j)
                    if (mo.meshes[j].id == pid) { next = static_cast<int>(j); break; }
                m = next;
            }
        }
        omk::Geometry g;
        g.batches.clear();
        for (const auto& b : mo.rest.batches) {
            omk::Batch nb = b;
            nb.start = g.corners.size(); nb.count = 0;
            for (std::size_t c = b.start; c + 3 <= b.start + b.count; c += 3) {
                const auto mi = mo.rest.cornerMesh[c];
                if (mi < 0 || static_cast<std::size_t>(mi) >= keep.size() || !keep[static_cast<std::size_t>(mi)]) continue;
                for (int k = 0; k < 3; ++k) {
                    g.corners.push_back(mo.rest.corners[c + static_cast<std::size_t>(k)]);
                    g.cornerMesh.push_back(mo.rest.cornerMesh[c + static_cast<std::size_t>(k)]);
                    if (!mo.rest.cornerVertex.empty()) g.cornerVertex.push_back(mo.rest.cornerVertex[c + static_cast<std::size_t>(k)]);
                    if (!mo.rest.cornerDeclared.empty()) g.cornerDeclared.push_back(mo.rest.cornerDeclared[c + static_cast<std::size_t>(k)]);
                }
                nb.count += 3;
            }
            if (nb.count) g.batches.push_back(nb);
        }
        return per.emplace(rootMesh, std::move(g)).first->second;
    };
    // (struct VehStaged: `backends/sdl/playtypes.h`, todo/play-split.md)
    std::vector<std::unique_ptr<VehStaged>> vehStaged;
    int vehDrawn = 0, vehLive = 0, vehStopped = 0;
    long vehTold = -1;
    // `sub_453A70`: the model's root sub-objects sorted by vertex+face count
    // DESCENDING - the LOD ladder. Sub-object 0 is what `sub_4544B0` hands
    // ambient traffic (`v16[1]`); the reserved slider takes sub-object 1
    // (`v16[2]`), which is read from the call sites, NOT judged by eye, and
    // not drawn here because the player's ride is not ported.
    const auto heaviestRootOf = [](const CharModel& mo) -> int {
        int best = mo.root;
        std::size_t bestWeight = 0;
        for (std::size_t i = 0; i < mo.meshes.size(); ++i) {
            bool hasParent = false;
            for (const auto& q : mo.meshes) if (q.id == mo.meshes[i].parent) { hasParent = true; break; }
            if (hasParent) continue;
            // the subtree's weight, the counts `sub_453A70` adds (+44 and +48)
            std::size_t w = 0;
            for (std::size_t j = 0; j < mo.meshes.size(); ++j) {
                int m = static_cast<int>(j);
                for (int guard = 0; guard < 64 && m >= 0; ++guard) {
                    if (static_cast<std::size_t>(m) == i) {
                        w += static_cast<std::size_t>(mo.meshes[j].vertices) +
                             static_cast<std::size_t>(mo.meshes[j].triangles) +
                             static_cast<std::size_t>(mo.meshes[j].quads);
                        break;
                    }
                    const std::int32_t pid = mo.meshes[static_cast<std::size_t>(m)].parent;
                    int next = -1;
                    for (std::size_t k = 0; k < mo.meshes.size(); ++k)
                        if (mo.meshes[k].id == pid) { next = static_cast<int>(k); break; }
                    m = next;
                }
            }
            if (w > bestWeight) { bestWeight = w; best = static_cast<int>(i); }
        }
        return best;
    };
    std::map<std::pair<int, int>, omk::NodeTracks> pedTracks;   // (sex, clip slot) -> its tracks
    // bumped wherever `pedTracks` or `pedStaged` is cleared: a walker's cached
    // root / rest / feet (`PedStaged::cacheGen`) are then taken afresh
    long pedCacheGen = 0;
    std::vector<std::byte> pedAni;
    std::string pedAniName;
    int pedDrawn = 0, pedLive = 0, pedInAction = 0, pedIdle = 0, pedOffView = 0;
    // how many (walker, light) pairs actually reached this frame
    int pedLit = 0;
    long pedTold = -1;
    // THE SHOOT-MODE POSE. These characters carry no `.CTL` in any of the
    // three 9-byte slots, so nothing the actor runtime does can pose them:
    // `Shoot_ActorEnter` resolves their CHARACTER TYPE's group in the area's
    // `.ani` (`sub_434530`) and `Shoot_ActorAction` asks it for a clip by
    // BEHAVIOUR type through `List_PickRandomByType`:
    //
    //     action 0, 5        type 11, else 25
    //     action 1           type 9
    //     action 2,3,4,6,7   type 10, else 9
    //
    // WHAT RUNS HERE IS THE SCRIPT'S LAST ACTION, NOT THE AI, and that is a
    // decision rather than an omission. `Shoot_TickNpc` calls one of four
    // brains every frame; `actor/shoot.h` models them and is deliberately not
    // wired to this, because the generic arm - 302 of the 306 shipped sites -
    // "takes the first edge and RECORDS the choice rather than pretending to
    // compute it": the real branch needs the navigation node, the line of
    // sight and the weapon's range, none of which this tree has. Running it
    // here would draw a deterministic first-edge walk as if it were the
    // game's behaviour, which is putting a guess where a fact belongs.
    //
    // THE PICK, though, is a fact and is now faithful. `List_PickRandomByType`
    // returns a RANDOM one of the matches, and 40 of the 195 (library, group,
    // behaviour type) buckets in the shipped `.ani` hold more than one clip -
    // up to six - so the choice is real in a fifth of them. The roll is seeded
    // per (actor, action) rather than re-rolled every frame: the engine rolls
    // once per `Shoot_ActorAction` and this has no AI asking again, so a
    // per-request seed is the same shape and keeps a still frame
    // reproducible. LABELLED as that.
    std::map<int, std::vector<omk::PedClip>> shootClips;      // character type -> its group
    const auto shootClipFor = [&](int group, int action) -> const omk::PedClip* {
        if (pedAni.empty()) return nullptr;
        auto it = shootClips.find(group);
        if (it == shootClips.end())
            it = shootClips.emplace(group, omk::animGroupClips(pedAni, group)).first;
        if (it->second.empty()) return nullptr;
        int want[2] = {9, -1};
        if (action == 0 || action == 5)      { want[0] = 11; want[1] = 25; }
        else if (action == 1)                { want[0] = 9;  want[1] = -1; }
        else if (action >= 2 && action <= 7) { want[0] = 10; want[1] = 9;  }
        for (int w : want) {
            if (w < 0) continue;
            std::vector<const omk::PedClip*> m;
            for (const auto& c : it->second) if (c.type == w) m.push_back(&c);
            if (m.empty()) continue;
            // the roll: a cheap LCG on (group, action), so two characters of
            // one type asking for one action can still draw different clips
            std::uint32_t r = static_cast<std::uint32_t>(group * 2654435761u
                                                         + action * 40503u + w);
            r ^= r >> 15; r *= 2246822519u; r ^= r >> 13;
            return m[r % m.size()];
        }
        return &it->second.front();          // "anim non existante dans le .ANI"
    };
    // `List_PickRandomByType(list, type)` for a DEATH clip (`sub_4240E0`): a
    // clip of that TYPE, and type 5 when the group has none. The engine's pick
    // is `rand()`; this one is a fixed function of (group, type), labelled.
    const auto shootClipOfType = [&](int group, int type) -> const omk::PedClip* {
        if (pedAni.empty()) return nullptr;
        auto it = shootClips.find(group);
        if (it == shootClips.end())
            it = shootClips.emplace(group, omk::animGroupClips(pedAni, group)).first;
        for (int t : {type, 5}) {
            std::vector<const omk::PedClip*> m;
            for (const auto& c : it->second) if (c.type == t) m.push_back(&c);
            if (!m.empty())
                return m[static_cast<std::size_t>(group * 7 + t) % m.size()];
        }
        return nullptr;
    };
    // ...and `List_PickRandomByType` as the brain's LABEL_177 calls it: a clip
    // of EXACTLY that type or none - no type-5 fallback, since the null return
    // is what sends the arm to its fallback turn rate. The same fixed pick.
    const auto shootClipExact = [&](int group, int type) -> const omk::PedClip* {
        if (pedAni.empty()) return nullptr;
        auto it = shootClips.find(group);
        if (it == shootClips.end())
            it = shootClips.emplace(group, omk::animGroupClips(pedAni, group)).first;
        std::vector<const omk::PedClip*> m;
        for (const auto& c : it->second) if (c.type == type) m.push_back(&c);
        return m.empty() ? nullptr : m[static_cast<std::size_t>(group * 7 + type) % m.size()];
    };
    // ...and `sub_434630(list, slot)`: the clip whose +4 SLOT is that, or none
    // - how `sub_4272B0` case 9 finds the attack at +108
    const auto shootClipBySlot = [&](int group, int slot) -> const omk::PedClip* {
        if (pedAni.empty() || !slot) return nullptr;
        auto it = shootClips.find(group);
        if (it == shootClips.end())
            it = shootClips.emplace(group, omk::animGroupClips(pedAni, group)).first;
        for (const auto& c : it->second) if (c.slot == slot) return &c;
        return nullptr;
    };
    const auto pedTracksFor = [&](int sex, const omk::PedClip& c, const std::vector<omk::Mesh>& meshes)
        -> const omk::NodeTracks* {
        // `PlayerController::poseTracks`'s recipe over the crowd library: the
        // descriptor's tracks resolve to meshes by name, key 0 is the rest
        // sentinel so frame f reads key f + 1
        const auto key = std::make_pair(sex, c.slot);
        auto it = pedTracks.find(key);
        if (it != pedTracks.end()) return it->second.valid() ? &it->second : nullptr;
        omk::NodeTracks t;
        const auto d = omk::animDescriptor(pedAni, c.descriptor);
        if (d && d->frames > 0 && !meshes.empty()) {
            const auto lower = [](std::string v) {
                for (auto& ch : v) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
                return v;
            };
            t.count = static_cast<int>(d->tracks.size());
            t.frames = d->frames;
            t.rootTrack = -1;
            // THE BONE NAMES CARRY A TWO-LETTER SKELETON PREFIX and the library
            // does not share it with every model: the men's clips say
            // `PhBassin`, the women's idle `ShBassin`, Jaunpur's men are
            // `KhBassin` and their women `FhBassin`. Matched by the whole
            // name, a woman idled and every Jaunpur man walked in a T-pose
            // (a reader's frame, 2026-09-03). The bone is the name after the
            // prefix, resolved inside the FIRST skeleton - the exact name is
            // tried first, for the one model whose prefix does agree.
            // THE BONE IS THE NAME AFTER ITS PREFIX, AND THE PREFIX IS NOT
            // TWO LETTERS - it is whatever the library and the model each
            // chose. `braqueur.ani`'s tracks are `UBassin`, `UCuissed`,
            // `UPiedd`; VIR_FN's meshes are `ViBassin`, `ViCuissed`,
            // `ViPiedd`. Stripping a fixed 2 from both gives "assin" against
            // "bassin" and NOT ONE of the nineteen tracks bound, which is why
            // the Shooting gallery's gunmen stood in their rest pose
            // (`todo/omk-play.md` 96). The crowd's four libraries all happen
            // to use two-letter prefixes (`Ph`, `Sh`, `Kh`, `Fh`), so the
            // fixed strip was right everywhere it had been looked at.
            //
            // The ENGINE does not strip at all: `o3de_FindMeshByName`
            // (0x00436D90) is `o3de_Traverse` running a `strstr` over the
            // node names and keeping the LAST match, and its callers pass the
            // BARE bone - `Bassin`, `Tete`, `Buste`, `Cuisseg`, `Piedd`
            // (04_sys.c 5497-5513, seventeen of them in a row). So the bone
            // name is a substring and the prefix's length never enters into
            // it. The same `strstr`-on-the-last-match is what finds the
            // shadow bones (`docs/ASSETS.md`).
            //
            // Reproduced here without hard-coding the seventeen: every prefix
            // in the corpus is a capitalised letter followed by lower case,
            // and every bone starts with a capital, so THE BONE BEGINS AT THE
            // SECOND UPPERCASE LETTER. `verify.py: bone names` measures that
            // over every shipped library and character model rather than
            // taking it on trust.
            const auto suffix = [&](const std::string& n) {
                for (std::size_t i = 1; i < n.size(); ++i)
                    if (n[i] >= 'A' && n[i] <= 'Z') return lower(n.substr(i));
                return n.size() > 2 ? lower(n.substr(2)) : lower(n);
            };
            int firstRoot = -1;
            for (std::size_t j = 0; j < meshes.size() && firstRoot < 0; ++j) {
                bool hasParent = false;
                for (const auto& p : meshes) if (p.id == meshes[j].parent) { hasParent = true; break; }
                if (!hasParent) firstRoot = static_cast<int>(j);
            }
            const auto underFirst = [&](std::size_t j) {
                int m = static_cast<int>(j);
                for (int guard = 0; guard < 64 && m >= 0; ++guard) {
                    if (m == firstRoot) return true;
                    const std::int32_t pid = meshes[static_cast<std::size_t>(m)].parent;
                    int next = -1;
                    for (std::size_t q = 0; q < meshes.size(); ++q) if (meshes[q].id == pid) { next = static_cast<int>(q); break; }
                    m = next;
                }
                return false;
            };
            for (const auto& tr : d->tracks) {
                std::int32_t mi = -1;
                const std::string want = lower(tr.name);
                for (const auto& m : meshes) if (lower(m.name) == want) { mi = m.index; break; }
                if (mi < 0) {
                    const std::string bone = suffix(tr.name);
                    for (std::size_t j = 0; j < meshes.size(); ++j)
                        if (suffix(meshes[j].name) == bone && underFirst(j)) { mi = meshes[j].index; break; }
                }
                t.ids.push_back(mi);
            }
            t.quats.assign(static_cast<std::size_t>(d->frames), {});
            t.trans.assign(static_cast<std::size_t>(d->frames), {0.0f, 0.0f, 0.0f});
            for (int f = 0; f < d->frames; ++f) {
                auto& row = t.quats[static_cast<std::size_t>(f)];
                row.resize(d->tracks.size());
                for (std::size_t i = 0; i < d->tracks.size(); ++i) {
                    const omk::AnimTrack& tr = d->tracks[i];
                    if (!tr.rotOffset || tr.rotKeys <= 0) continue;
                    int k = f + 1;
                    if (k >= tr.rotKeys) k = tr.rotKeys - 1;
                    const std::size_t o = tr.rotOffset + 16u * static_cast<std::size_t>(k);
                    if (o + 16 > pedAni.size()) continue;
                    float q[4];
                    std::memcpy(q, pedAni.data() + o, 16);
                    row[i] = {q[0], q[1], q[2], q[3]};
                }
            }
        }
        it = pedTracks.emplace(key, std::move(t)).first;
        return it->second.valid() ? &it->second : nullptr;
    };
    // one shoot record per gunman, built from his own properties the first
    // frame he is staged and kept for the run (`todo/shoot-mode.md` 7d)
    // Is the FIRST-PERSON shoot camera actually the one in force? Hiding the
    // player is right only while it is: a reader found Kay'l missing at the
    // end of the supermarket cutscene because the port hid him for "shoot
    // mode" while a script's own camera had taken the view back to third
    // person, leaving an empty room (`todo/omk-play.md` 97).
    // (the mouse's sensitivities are the engine's own now - `mouseSensX` below)

    bool shootCameraLive = false;
    // The first-person AIM. Yaw is the player's own facing (the mouse turns
    // the body, which is what the shoot scheme's `Tourner` keys do too);
    // pitch is the camera's alone, since nothing in the 14-slot word carries
    // it and the body has no bone for it here. Clamped to +/-70 degrees, a
    // choice this port is making - the engine's own limit is untraced.
    float shootPitch = 0.0f;
    std::map<int, omk::ShootRecord> shootBrains;
    // A GUNMAN'S SHOTS (`sub_424DE0`'s fire epilogue, below): which of them
    // has said how his weapon resolved, and how many bolts each has fired.
    // The jitter's `rand()` is the CRT's own generator from its default seed
    // - the engine's is one stream shared by every caller, so this is the
    // formula and not the engine's place in the sequence.
    std::map<int, long> gunShots;
    std::set<int> gunTold;
    // (struct GunClip: `backends/sdl/playtypes.h`, todo/play-split.md)
    std::map<int, GunClip> gunClips;
    // (struct GunAnim: `backends/sdl/playtypes.h`, todo/play-split.md)
    std::map<int, GunAnim> gunAnims;
    std::map<int, int> gunCurType;       // the clip TYPE his last action started
    // ...or the clip SLOT the brain started (`sub_4272B0` case 9, the attack by
    // +108) - when set it wins over the type, and an action's type clears it
    std::map<int, int> gunCurSlot;
    // THE PLAYER'S DEATH (`sub_423FC0`): the countdown `dword_4E975C` his death
    // clip runs for, and the gunmen told to stand down on their next tick
    float playerDeathCountdown = 0.0f;
    std::set<int> gunStandDown;
    std::set<int> gunLooped;          // who has said his clip looped
    // (struct GunAim: `backends/sdl/playtypes.h`, todo/play-split.md)
    std::map<int, GunAim> gunAims;
    std::set<int> gunAimTold;         // who has said his first aim
    std::set<int> gunDrawnTold;       // whose gun has been said drawn
    std::set<int> gunBarrelTold;      // whose barrel direction has been said
    // THE GAUGE'S OWN VALUE, `dword_90E100` - what `Hud_DrawBar` draws. It is
    // NOT his record's +92: `Shoot_Enter` seeds it, `sub_423A40` (a property 1
    // write, the medikits) and a hit he survives copy +92 into it, and the
    // killing hit returns before it is touched, so the bar keeps its last value.
    int hudHealth = 0;
    // the CRT's `rand()` (MSVC: `seed * 214013 + 2531011`, bits 16..30) from
    // its default seed, drawn by the gunmen AND the melee AI - the engine's is
    // one stream too. Never the host's `std::rand()`, which is another
    // generator and is shared with whatever system library calls it.
    std::uint32_t gunRandSeed = 1;
    // THE PLAYER'S SHOT (`actor/shootfire.h`, todo/shoot-mode.md 7h): his own
    // shoot record - the engine keeps one for him among the 100, and the
    // gate's three numbers live on it - the two one-shot globals, and the
    // projectile pool the request fills.
    omk::ShootRecord playerShootRec;
    omk::ShotLatch shotLatch;
    // the arm's aim angles, `dword_6579A0/A4` (`actor/shootaim.h`)
    omk::ShootAim shootAim;
    // the first-person MOVER's block at `dword_6579B0` (`actor/shootmove.h`),
    // and the run it is on, for the log: frames and distance since it started
    omk::ShootMover shootMover;
    // THE SHOOT HUD (`todo/shoot-mode.md` 8.3): screen 34's panel composed over
    // the frame while the mode runs - a walk of its own, so it takes no input -
    // the runtime texts its items' native callbacks produce, and
    // `dword_90E11C`, the ammo counter `Shoot_InitWeapon` and the shot write.
    std::unique_ptr<omk::UiWalk> hudWalk;
    omk::HudBar hudBar;                  // `Hud_DrawBar` mode 0, the health gauge
    omk::Radar radar;                    // 0x42F000, screen 34's minimap
    omk::Map2d shootMap;                 // MAP2D\<+106>.MPT - the noise's floors
    omk::ShootField shootField;          // `sub_436260`'s distance field toward the player
    std::set<int> gunCellSeeded;
    std::set<int> gunEntryPending;   // his entry action, waiting for a floor         // gunmen whose +136/+140 came off the grid
    // THE NOISE (`sub_4246E0`, `actor/shoot.h`): at a shot's muzzle, and where
    // a bolt stops on the world or on a body. Every gunman with a brain is a
    // record, tested in actor order (the engine's is slot order). His FLOOR
    // is his record's `+188`, as the engine reads it - 0 for a gunman, the
    // zeroed record's, since `Shoot_Think` (its writer) is not wired; this read
    // `sub_435020` of where he stood at first, which found -1 for the two
    // supermarket gunmen standing off the grid and called 22 of a reader's
    // noises "another floor" on a one-floor map. Two things stand in,
    // labelled: a record the port marks dead (`+160 & 8`) is
    // passed over, for the death arm's `& ~0x40` the port does not run; and
    // `Shoot_ActorAction` is RECORDED on the Session, as the hit's is - its own
    // arms are not ported, and an action of -1 (whose arm is case 0) is not
    // recorded at all.
    const auto shootNoise = [&](long frame, int from, const float at[3], const char* what) {
        if (!shootMap.valid()) return;
        const int nf = shootMap.floorAt(at[0], at[1], at[2], -1);
        // one line per noise, so a silence says WHY: how many records were
        // passed over for each of the tests, and the first one's hearing
        int tested = 0, dead = 0, alertedAlready = 0, unentered = 0, far = 0, heard = 0;
        int firstCells = -1;
        for (auto& [actor, rec] : shootBrains) {
            if (nf == -1) break;
            if (actor == from) continue;
            ++tested;
            // dead: flag 8 with no health - flag 8 alone is a clip playing
            if ((rec.flags & 8u) && rec.health <= 0) { ++dead; continue; }
            if (!(rec.flags & 0x40u)) { ++unentered; continue; }
            if (rec.flags & 0x20u) { ++alertedAlready; continue; }
            const Staged* sp = nullptr;
            for (const auto& up : staged)
                if (up && up->actor == actor) { sp = up.get(); break; }
            if (!sp) continue;
            std::int32_t cells = 0;
            session.actorProperty(actor, 28, cells);
            if (firstCells < 0) firstCells = static_cast<int>(cells);
            // his FLOOR is his record's `+188`, a signed byte - `movsx edx,
            // byte [esi+1Ch]`
            const int sf = static_cast<int>(static_cast<std::int8_t>(rec.node & 0xFF));
            const omk::NoiseHearing h = omk::shootHearNoise(
                rec, sp->drawAt, at, static_cast<int>(cells),
                static_cast<float>(shootMap.scale()), nf, sf);
            if (!h.heard) { ++far; continue; }
            ++heard;
            if (h.act && h.action >= 0)
                session.shootModeMutable().actorAction(actor, h.action);
            std::printf("frame %ld: NOISE (sub_4246E0) - %s at %.0f %.0f %.0f, floor %d: "
                        "actor %d %s (hearing %d cells of %u), his floor %d, action %d\n",
                        frame, what, double(at[0]), double(at[1]), double(at[2]), nf, actor,
                        h.alerted ? "ALERTED" : "heard it from another floor",
                        int(cells), shootMap.scale(), sf, h.act ? h.action : -99);
        }
        std::printf("frame %ld: NOISE (sub_4246E0) - %s at %.0f %.0f %.0f, floor %d: %d "
                    "records - %d dead, %d not entered, %d already alerted, %d out of "
                    "hearing (the first hears %d cells), %d heard\n", frame, what,
                    double(at[0]), double(at[1]), double(at[2]), nf, tested, dead,
                    unentered, alertedAlready, far, firstCells, heard);
    };
    std::map<std::uint32_t, std::string> hudRows;
    int  hudAmmo = -1;
    std::string hudTold;
    long  shootMoveFrames = 0;
    float shootMoveDist = 0.0f;
    float shootMoveFrom[3] = {0.0f, 0.0f, 0.0f};
    // `sub_47D370`'s sensitivities and its invert, options rows 23, 24 and 25
    // (`word_90E1AC` / `word_90E1AE` / `byte_90E1B0`, the header's +44/+46/
    // +48) out of the save header or the ini's MouseSensX / MouseSensY - the
    // defaults 20, 15 and off. The mouse and the turn keys share row 23.
    const int  mouseSensX = settings.v.mouseSensitivityX;
    const int  mouseSensY = settings.v.mouseSensitivityY;
    // `--invert-y` flips the row, as choosing it in the menu would
    const bool mouseInverted = settings.v.mouseInverted != mouseInvertY;
    bool shootPitchDirty = false;   // MDLUP / MDLDO moved the pitch this frame
    // THE RAISE (`actor/shootaim.h`): the player's pose for this frame with the
    // shoot aim layer over its upper body - every bone the table at 0x4C3798
    // marks takes `S_AUTOLK`'s grid (group 202's default) at the aim angles,
    // lowered toward the stance's key 1 (group 200's default) by the weapon's
    // +176. Outside shoot mode it is the pose as it was. The arm, the gun and
    // the muzzle all read it, so what fires is what is drawn.
    const auto playerPoseNow = [&](omk::PlayerController* pl, bool aimLayer,
                                   const omk::NodeTracks& pt, float frame)
        -> std::vector<omk::MeshPose> {
        if (!pl || !aimLayer || !pt.valid())
            return omk::composePose(playerMeshes, pt, frame, false);
        const omk::NodeTracks* aim = pl->clipTracks(pl->groupDefaultClip(202));
        const omk::NodeTracks* stance = pl->clipTracks(pl->groupDefaultClip(200));
        if (!aim || aim->frames < 15) return omk::composePose(playerMeshes, pt, frame, false);
        // the aim layer bends ONE key - the truncated one (enhancement 12
        // smooths the plain pose above, not this)
        const int fi = static_cast<int>(std::floor(frame));
        const int f = fi < 0 ? 0 : (fi >= pt.frames ? pt.frames - 1 : fi);
        omk::NodeTracks one;
        one.count = pt.count;
        one.frames = 1;
        one.rootTrack = pt.rootTrack;
        one.ids = pt.ids;
        one.names = pt.names;
        one.quats.push_back(pt.quats[static_cast<std::size_t>(f)]);
        if (!pt.trans.empty())
            one.trans.push_back(pt.trans[static_cast<std::size_t>(
                f < static_cast<int>(pt.trans.size()) ? f : static_cast<int>(pt.trans.size()) - 1)]);
        auto& row = one.quats[0];
        for (std::size_t i = 0; i < one.ids.size() && i < row.size(); ++i) {
            const std::int32_t mi = one.ids[i];
            if (mi < 0 || static_cast<std::size_t>(mi) >= playerMeshes.size()) continue;
            if (!omk::shootAimMarked(playerMeshes[static_cast<std::size_t>(mi)].slot)) continue;
            // the bone's `S_AUTOLK` keys 1..15: the tracks' frame k is key k + 1
            std::vector<omk::Quatf> keys;
            for (std::size_t j = 0; j < aim->ids.size(); ++j) {
                if (aim->ids[j] != mi) continue;
                for (int k = 0; k < 15; ++k)
                    keys.push_back(aim->quats[static_cast<std::size_t>(k)][j]);
                break;
            }
            if (keys.size() < 15) continue;
            omk::Quatf q = omk::shootAimBone(keys, shootAim.yaw, shootAim.pitch);
            // the stance's key 1 for this bone - identity when the stance has
            // no track for it, as `sub_471950`'s `v72 = 1.0` is
            if (stance && !stance->quats.empty()) {
                omk::Quatf key1{};
                for (std::size_t j = 0; j < stance->ids.size(); ++j)
                    if (stance->ids[j] == mi) { key1 = stance->quats[0][j]; break; }
                q = omk::shootAimLower(q, key1, playerShootRec.weaponLowered);
            }
            row[i] = q;
        }
        return omk::composePose(playerMeshes, one, 0, false);
    };
    // A GUNMAN'S AIM LAYER - the same `sub_471950` bend, on HIS pieces. The
    // fire gate's target arm binds his group's type-18 clip (`sub_434590(+20,
    // 18)`, the S_AUTOLK of his library: 15 frames in braqueur.ani) over the
    // bones his `+84` table marks (0x4C3798 for every type but 13, the
    // player's own table), with the type-17 clip as the stance the lowering
    // blends to (`dword_6A472C`), and bends them by his aim angles and his
    // `+176`. Only while the gate ran him this tick (`gunAims`).
    const auto gunmanPoseNow = [&](int actor, int deathType, const CharModel* mo,
                                   const omk::NodeTracks& pt, float frame)
        -> std::vector<omk::MeshPose> {
        const auto aimIt = gunAims.find(actor);
        const auto recIt = shootBrains.find(actor);
        if (!mo || !pt.valid() || deathType >= 0 || aimIt == gunAims.end() ||
            !aimIt->second.live || recIt == shootBrains.end() ||
            session.typeOfActor(actor) == 13u)      // 0x4C37E8, not lifted
            return omk::composePose(mo->meshes, pt, frame, false);
        const int grp = static_cast<int>(session.typeOfActor(actor));
        const omk::PedClip* c18 = (grp >= 0 && grp < 64) ? shootClipExact(grp, 18) : nullptr;
        const omk::PedClip* c17 = (grp >= 0 && grp < 64) ? shootClipExact(grp, 17) : nullptr;
        const omk::NodeTracks* aim = c18 ? pedTracksFor(grp, *c18, mo->meshes) : nullptr;
        const omk::NodeTracks* stance = c17 ? pedTracksFor(grp, *c17, mo->meshes) : nullptr;
        if (!aim || aim->frames < 15) return omk::composePose(mo->meshes, pt, frame, false);
        // the aim layer bends ONE key - the truncated one (enhancement 12
        // smooths the plain pose above, not this)
        const int fi = static_cast<int>(std::floor(frame));
        const int f = fi < 0 ? 0 : (fi >= pt.frames ? pt.frames - 1 : fi);
        omk::NodeTracks one;
        one.count = pt.count;
        one.frames = 1;
        one.rootTrack = pt.rootTrack;
        one.ids = pt.ids;
        one.names = pt.names;
        one.quats.push_back(pt.quats[static_cast<std::size_t>(f)]);
        if (!pt.trans.empty())
            one.trans.push_back(pt.trans[static_cast<std::size_t>(
                f < static_cast<int>(pt.trans.size()) ? f : static_cast<int>(pt.trans.size()) - 1)]);
        auto& row = one.quats[0];
        for (std::size_t i = 0; i < one.ids.size() && i < row.size(); ++i) {
            const std::int32_t mi = one.ids[i];
            if (mi < 0 || static_cast<std::size_t>(mi) >= mo->meshes.size()) continue;
            if (!omk::shootAimMarked(mo->meshes[static_cast<std::size_t>(mi)].slot)) continue;
            std::vector<omk::Quatf> keys;
            for (std::size_t j = 0; j < aim->ids.size(); ++j) {
                if (aim->ids[j] != mi) continue;
                for (int k = 0; k < 15; ++k)
                    keys.push_back(aim->quats[static_cast<std::size_t>(k)][j]);
                break;
            }
            if (keys.size() < 15) continue;
            omk::Quatf q = omk::shootAimBone(keys, aimIt->second.yaw, aimIt->second.pitch);
            if (stance && !stance->quats.empty()) {
                omk::Quatf key1{};
                for (std::size_t j = 0; j < stance->ids.size(); ++j)
                    if (stance->ids[j] == mi) { key1 = stance->quats[0][j]; break; }
                q = omk::shootAimLower(q, key1, recIt->second.weaponLowered);
            }
            row[i] = q;
        }
        return omk::composePose(mo->meshes, one, 0, false);
    };
    omk::ProjectilePool projectiles;
    long shotsFired = 0;
    // `scptdata\shoot2.sfx`, which `Shoot_Enter` loads for its section A -
    // the SHOT SPRITES, looked up by the held gun's root mesh name - and the
    // gun's own model facts a shot needs: that name, and where its `tir` node
    // sits. `Object_Load` unlinks `tir` from the gun at load (the name is the
    // string at 0x4C2E4C) and `Actor_TickProjectiles` clones it for each shot,
    // linked under the HAND with its own +128 local - so the muzzle is the
    // hand's pose applied to `tir`'s local, and the bolt IS that mesh.
    omk::SfxFile shootSfx;
    // ...and `Game_Start("shoot2.scx")`: the mode's LIBRARY, loaded into
    // `stru_930780` over `aventure.scx`, which is the scene `Sfx_TickAmbient`
    // resolves a shot effect's SOUND id in (`Scene_FindSoundIndex`) when the
    // emitter names no scene of its own - and the shot's never does.
    std::unique_ptr<omk::ScxRuntime> shootRt;
    // (struct SfxSample: `backends/sdl/playtypes.h`, todo/play-split.md)
    std::map<std::pair<const std::byte*, std::size_t>, SfxSample> sfxCache;
    std::string sfxSceneWas;
    const auto sfxPcm = [&sfxCache](std::span<const std::byte> wav) -> const SfxSample& {
        const auto key = std::make_pair(wav.data(), wav.size());
        auto it = sfxCache.find(key);
        if (it == sfxCache.end()) {
            SfxSample sm;
            auto v = std::make_shared<std::vector<float>>(wavToDevice(wav, kDeviceRate));
            for (const float x : *v) sm.peak = std::max(sm.peak, std::fabs(x));
            sm.pcm = std::move(v);
            it = sfxCache.emplace(key, std::move(sm)).first;
        }
        return it->second;
    };
    const auto shotSound = [&](long frame, int effectId, const float at[3],
                               const float* listener, const char* what) {
        const omk::FxEffect* e = effectId ? shootSfx.byId(effectId) : nullptr;
        if (!e || e->sound == 0xFFFF || e->sound == -1 || !shootRt) return;
        const int w = shootRt->wavBydId(e->sound);
        if (w < 0) {
            std::printf("frame %ld: SHOT SOUND %s - effect %d's sound %d is not in shoot2.scx\n",
                        frame, what, effectId, e->sound);
            return;
        }
        float d = 0.0f;
        if (listener) {
            const float dx = at[0] - listener[0], dy = at[1] - listener[1],
                        dz = at[2] - listener[2];
            d = std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        const float gain = d <= 78.0f ? 1.0f : 78.0f / std::min(d, 584.0f);
        const SfxSample& sm = sfxPcm(shootRt->wavData(w));
        std::printf("frame %ld: SHOT SOUND %s - effect %d sound %d '%s', %.0f from him, "
                    "gain %.2f\n", frame, what, effectId, e->sound,
                    shootRt->wavName(w).c_str(), double(d), double(gain));
        if (!sm.pcm->empty()) front.playSound(sm.pcm, false, gain * fxGain());
    };
    // (struct GunFacts: `backends/sdl/playtypes.h`, todo/play-split.md)
    std::map<std::string, GunFacts> gunFacts;
    std::string shotGunStem;          // the stem the player's bolts are cloned from
    const auto gunFactsFor = [&](const std::string& stem) -> const GunFacts& {
        auto it = gunFacts.find(stem);
        if (it != gunFacts.end()) return it->second;
        GunFacts g;
        if (const auto mo = fs.resolve("MESHES/OBJETS/" + stem + ".3DO")) {
            const auto md = omk::DataFs::readPath(*mo);
            if (const auto mh = omk::readHeader(md)) {
                const auto ms = omk::readMeshes(md, *mh);
                // the model's FIRST node is what `FindNodeByName(v8, 0)` gives
                // `Object_Load` as the slot's node, and `sub_44EEB0` reads the
                // shot sprite's name off it
                if (!ms.empty()) g.root = ms.front().name;
                for (std::size_t i = 0; i < ms.size(); ++i) {
                    std::string nm = ms[i].name;
                    for (auto& ch : nm) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    if (nm != "tir") continue;
                    g.tirMesh = static_cast<int>(i);
                    for (int k = 0; k < 3; ++k) {
                        g.tirLocal[k] = ms[i].local[k];
                        g.tirPos[k] = ms[i].pos[k];
                    }
                }
                g.ok = g.tirMesh >= 0;
            }
        }
        return gunFacts.emplace(stem, g).first->second;
    };
    long stagedEver = 0;                 // for the summary line
    std::vector<int> stagedIds;
    // The pool is rebuilt on a COMPOSITION change, not on a size change: two
    // models with the same texture count swapping is exactly what a size test
    // cannot see.
    std::uint64_t poolComposition = 1, poolBuiltFor = 0, poolTold = 0;
    bool poolHasSprites = false, poolHasPlayer = false;
    std::size_t playerTexBase = 0, spriteTexBase = 0;
    // sprite id -> its slot within the pool's sprite section, or -1
    std::unordered_map<int, int> spriteSlot;   // absent = -1, as an unassigned slot was
    // The sprite ids the resident scene can actually name - what goes in the
    // pool, as opposed to everything that decoded.
    std::set<int> spriteWanted, spritePooled;
    bool poolOverflowTold = false;
    const bool stagedProbe = std::getenv("OMK_STAGE_PROBE") != nullptr;
    // `engine: camera obstruction` - one line per frame from `sub_417070`.
    const bool obstructProbe = std::getenv("OMK_CAM_OBSTRUCT_PROBE") != nullptr;
    // A DIAGNOSTIC for the sign of the scene call's Euler: CLAUDE.md 5's
    // rule is that leaving the game's space reflects one axis and so
    // reverses the sense of every rotation about it.
    const float progYawSign = std::getenv("OMK_PROGYAW_NEG") ? -1.0f : 1.0f;
    // One `.3DO`/`.3DT` per MODEL NAME, loaded once and shared by every actor
    // wearing it.
    const auto charModelFor = [&](const std::string& name) -> CharModel* {
        if (name.empty()) return nullptr;
        auto it = charModels.find(name);
        if (it != charModels.end()) return &it->second;
        // EVERY LOAD SAYS WHAT IT COST, and whether it is a RELOAD - a model
        // this run already had and the per-frame eviction below let go of. A
        // transition's stall is a load inside a frame (todo/vita-port.md
        // 2026-09-23), and a reload is the one that need not happen at all.
        static std::map<std::string, int> loadsOf;
        const int nth = ++loadsOf[name];
        const auto loadT0 = std::chrono::steady_clock::now();
        struct Said {
            const std::string& n; int k; std::chrono::steady_clock::time_point t0;
            ~Said() {
                const double ms = std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - t0).count();
                std::printf("model load: %s in %.1f ms%s\n", n.c_str(), ms,
                            k > 1 ? (" - a RELOAD, load " + std::to_string(k)).c_str() : "");
            }
        } said{name, nth, loadT0};
        CharModel m;
        if (const auto mo = fs.resolve("MESHES/PERSOS/" + name + ".3DO")) {
            const auto md = omk::DataFs::readPath(*mo);
            m.rest = omk::buildGeometry(md, omk::DrawFilter::Engine);
            if (const auto mh = omk::readHeader(md)) m.meshes = omk::readMeshes(md, *mh);
            if (const auto mt = fs.resolve("MESHES/PERSOS/" + name + ".3DT"))
                m.tex = omk::textures(md, omk::DataFs::readPath(*mt));
            m.face = omk::faceMeshOf(m.meshes);
            m.boneIdx.build(m.meshes, omk::MeshNameIndex::shadowNames());
            for (std::size_t i = 0; i < m.meshes.size() && m.root < 0; ++i) {
                bool hasParent = false;
                for (const auto& p : m.meshes)
                    if (p.id == m.meshes[i].parent) { hasParent = true; break; }
                if (!hasParent) m.root = static_cast<int>(i);
            }
            m.ready = !m.rest.corners.empty() && !m.meshes.empty();
        }
        ++poolComposition;      // the pool gains this model's textures
        return &charModels.emplace(name, std::move(m)).first->second;
    };
    const auto propModelFor = [&](const std::string& stem) -> PropModel* {
        if (stem.empty()) return nullptr;
        auto it = propModels.find(stem);
        if (it != propModels.end()) return &it->second;
        PropModel m;
        if (const auto mo = fs.resolve("MESHES/OBJETS/" + stem + ".3DO")) {
            const auto md = omk::DataFs::readPath(*mo);
            m.rest = omk::buildGeometry(md, omk::DrawFilter::Engine);
            if (const auto mt = fs.resolve("MESHES/OBJETS/" + stem + ".3DT"))
                m.tex = omk::textures(md, omk::DataFs::readPath(*mt));
            // The HIERARCHY ROOT's position, the way a character model's
            // pelvis is found: the mesh whose parent resolves to nothing.
            if (const auto mh = omk::readHeader(md)) {
                const auto ms = omk::readMeshes(md, *mh);
                int root = -1;
                for (std::size_t i = 0; i < ms.size() && root < 0; ++i) {
                    bool hasParent = false;
                    for (const auto& q : ms)
                        if (q.id == ms[i].parent) { hasParent = true; break; }
                    if (!hasParent) root = static_cast<int>(i);
                }
                if (root >= 0)
                    for (int k = 0; k < 3; ++k)
                        m.origin[k] = ms[static_cast<std::size_t>(root)].pos[k];
                    for (int k = 0; k < 3; ++k)
                        m.localOff[k] = ms[static_cast<std::size_t>(root)].local[k];
            }
            m.ready = !m.rest.corners.empty();
            std::printf("prop model %s: %zu corners, %zu batches, %zu textures\n",
                        stem.c_str(), m.rest.corners.size(), m.rest.batches.size(),
                        m.tex.size());
        }
        ++poolComposition;
        return &propModels.emplace(stem, std::move(m)).first->second;
    };
    // One `.CTL` per BANK NAME, and the entry `Actor_LoadBankList` leaves the
    // channel on. `PlayerController`'s constructor is the rule quoted:
    // `rt_.loadModel()` then `SetPersoBankGroup(channel, Cef_DefaultGroup)`,
    // and `clipOwner()`'s chain - an entry whose flags carry 0x8002 is an
    // alias and hands the clip on through its GoTo.
    const auto charBankFor = [&](const std::string& name) -> CharBank* {
        if (name.empty()) return nullptr;
        auto it = charBanks.find(name);
        if (it != charBanks.end()) return &it->second;
        CharBank b;
        if (const auto cp = fs.resolve("ANIMS/" + name + ".CTL")) {
            b.data = omk::DataFs::readPath(*cp);
            b.ctl = omk::readCtl(b.data);
            b.ready = b.ctl.valid;
            int g = -1;
            for (std::size_t k = 0; k < b.ctl.groupList.size(); ++k)
                if (b.ctl.groupList[k].flags & 1u) { g = static_cast<int>(k); break; }
            int s = g >= 0 ? b.ctl.groupList[static_cast<std::size_t>(g)].defaultEntry : -1;
            for (int guard = 0; guard < 64; ++guard) {
                if (s < 0 || s >= static_cast<int>(b.ctl.states.size())) { s = -1; break; }
                if (!(b.ctl.states[static_cast<std::size_t>(s)].flags & 0x8002u)) break;
                s = b.ctl.states[static_cast<std::size_t>(s)].gotoIdx;
            }
            if (s >= 0 && s < static_cast<int>(b.ctl.states.size())) {
                const int c = b.ctl.states[static_cast<std::size_t>(s)].clip;
                if (c >= 0 && c < static_cast<int>(b.ctl.clips.size())) b.idleClip = c;
            }
        }
        return &charBanks.emplace(name, std::move(b)).first->second;
    };
    // ...and that clip as a pose, at FRAME 0. The recipe is
    OMK_HEAPCHECK("before melee");
    // (struct FightRun: `backends/sdl/playtypes.h`, todo/play-split.md)
    FightRun fightRun;
    bool fightCamTold = false;        // one line when mode 14 takes the view
    const auto playerRecordSpan = [&]() {
        return state.raw().subspan(
            static_cast<std::size_t>(omk::GameState::kPlayerRecord),
            static_cast<std::size_t>(omk::GameState::kPlayerRecordSize));
    };
    const auto beginMelee = [&](int opponentId, int level) -> bool {
        if (!player || fightRun.active) return false;
        Staged* s = nullptr;
        for (auto& up : staged) if (up->actor == opponentId) { s = up.get(); break; }
        if (!s || !s->mo || s->mo->meshes.empty()) {
            std::printf("fight.begin %d: no staged body for that character, "
                        "the script runs on\n", opponentId);
            return false;
        }
        // Both fighters' `.CTL` slot 2. The opponent's comes off his record;
        // the player's off the DB player record's own slot 2 (+90), with the
        // same LABELLED fallback shape his adventure bank has.
        const std::string foeBankName = session.ctlSlotOfActor(opponentId, 2);
        std::string myBankName;
        {
            const auto raw = state.raw();
            const std::size_t off =
                static_cast<std::size_t>(omk::GameState::kPlayerRecord) + 90u;
            for (std::size_t k = 0; k < 9 && off + k < raw.size(); ++k) {
                const char c = static_cast<char>(raw[off + k]);
                if (!c) break;
                myBankName.push_back(c);
            }
            if (myBankName.empty())
                myBankName = (!playerModel.empty() && playerModel[0] == 'F')
                                 ? "F1CMBT" : "H1CMBT";   // LABELLED fallback
        }
        CharBank* fb = charBankFor(foeBankName);
        CharBank* pb = charBankFor(myBankName);
        if (!fb || !fb->ready || !pb || !pb->ready) {
            std::printf("fight.begin %d: combat bank missing (player '%s' %s, "
                        "opponent '%s' %s), the script runs on\n", opponentId,
                        myBankName.c_str(), (pb && pb->ready) ? "ok" : "MISSING",
                        foeBankName.c_str(), (fb && fb->ready) ? "ok" : "MISSING");
            return false;
        }
        // The stats, `Fight_Begin`'s six event-44 reads. The player's live in
        // the DB record - `Hooks::getActorProperty` refuses an actor with no
        // chunk record, and Kay'l has none in a city chunk.
        omk::FightStats ps{}, os{};
        {
            const auto rec = playerRecordSpan();
            std::int32_t v = 0;
            // `Hud_Refresh`'s six reads, in `dword_530CB0`'s order
            {
                static const int kCardProps[6] = {16, 19, 17, 3, 18, 2};
                for (int k = 0; k < 6; ++k) {
                    std::int32_t c = 0;
                    fightRun.cardProps[k] = omk::readActorProperty(rec, kCardProps[k], c) ? c : 0;
                }
            }
            if (omk::readActorProperty(rec, 1, v))  ps.vie = v;
            // THE HARNESS, and it is one: the engine reads property 1 as it is.
            if (fightHealth >= 0) {
                std::printf("fight.begin: harness --fight-health gives the player Vie %d "
                            "(his record says %d)\n", fightHealth, ps.vie);
                ps.vie = fightHealth;
            }
            if (omk::readActorProperty(rec, 16, v)) ps.attack = v;
            if (omk::readActorProperty(rec, 18, v)) ps.dodge = v;
            if (omk::readActorProperty(rec, 19, v)) ps.experience = v;
            std::int32_t w = 0;
            if (session.actorProperty(opponentId, 1, w))  os.vie = w;
            if (session.actorProperty(opponentId, 16, w)) os.attack = w;
            if (session.actorProperty(opponentId, 18, w)) os.dodge = w;
            if (session.actorProperty(opponentId, 19, w)) os.experience = w;
        }
        fightRun.foeChannel = std::make_unique<omk::CefChannel>(fb->ctl);
        {
            const int g = fightRun.foeChannel->defaultGroup();
            if (g >= 0) fightRun.foeChannel->setBankGroup(g);
        }
        player->setBank(pb->ctl, pb->data);
        player->setActorState(omk::ActorState::Melee, "Fight_Engage");

        // THE SEPARATION RADIUS. `Fight_Begin` takes `f32(node, 88)` for each
        // fighter and keeps the larger - the NODE's own bounding radius, and
        // this tree has no node, so the model's LARGEST MESH bound stands in
        // and is labelled as a stand-in. It is not `meshes.front()`: that is
        // one mesh (7.1 for HO1_FN, 18 cm) and the first version of this used
        // it, which would have let two fighters stand inside each other. The
        // whole-body mesh gives 42.5, a little over a metre.
        const auto bodyRadius = [](const std::vector<omk::Mesh>& ms) {
            float r = 0.0f;
            for (const auto& m : ms) r = std::max(r, m.radius);
            return r;
        };
        fightRun.player = omk::FightBody{};
        // ONE ORIGIN FOR BOTH FIGHTERS. The engine's two actors each store a
        // single position at `+244/+248/+252`, and `Fight_KeepSeparation` and
        // the camera both read it; this tree had the player on his WALKER's
        // origin (the feet) and the opponent on his staged placement (the
        // PELVIS), about 41 units apart. `measureSeparation` is a 3D distance,
        // so that constant gap satisfied the 43.4 separation radius on its own
        // and the two stood inside each other - a reader: *"some camera issues
        // are when Kay'l and the opponent are at the same place at the same
        // time"*. The camera then took its heading from a two-unit
        // `atan2(b - a)`, which is noise (`todo/fight-mode.md` 15.8d).
        //
        // The opponent's placement is the convention to meet, because it is
        // the one the engine's actor record uses, so the player is lifted to
        // his pelvis on the way in and dropped back to his feet on the way out.
        fightRun.player.x = player->pos()[0];
        fightRun.player.y = player->pos()[1] - player->cameraLift();
        fightRun.player.z = player->pos()[2];
        fightRun.player.yaw = player->facing();
        fightRun.player.radius = bodyRadius(playerMeshes);
        fightRun.player.channel = &player->channel();
        fightRun.player.isPlayer = true;
        // His channel is ticked by the CONTROLLER, because melee's row runs
        // `Actor_ApplyMotion` after `Cef_TickChannel` and the controller is
        // both halves. Without this the fight ticks the channel alone and he
        // stands still while his clips play - which is what the first run of
        // this harness did, 79 units apart for 245 frames.
        fightRun.player.externallyTicked = true;
        fightRun.foe = omk::FightBody{};
        // WHERE HIS PROGRAM LEFT HIM, not where it began. `fight.begin` runs
        // inside the script pump, BEFORE this frame's staged pass notices the
        // program has ended and carries `drawAt` over into `at` - so `at` is
        // still the program's placement (the path START) and `drawAt` is the
        // body the engine's actor record holds: `Anim_RootDelta` has summed
        // the clip into +244 all along. Reading `at` put the supermarket's
        // robber at 'D1BassinP2''s start, 11.9 m away with `SMbox45` between
        // them, where the approach had left him 1.5 m from the player
        // (`todo/fight-mode.md` 15.8e). The same rule `npcBody` uses.
        const float* foeAt = s->progRan ? s->drawAt : s->at;
        fightRun.foe.x = foeAt[0]; fightRun.foe.y = foeAt[1]; fightRun.foe.z = foeAt[2];
        // THE HARNESS, and it is one: the engine starts him where the script
        // left him. This moves him so a check can put a wall in his way - the
        // supermarket's real fight is short and never reaches one.
        if (foeAtSet) {
            fightRun.foe.x = foeAtXZ[0]; fightRun.foe.z = foeAtXZ[1];
            std::printf("fight: harness --fight-foe-at starts the opponent at %.0f %.0f "
                        "(the script left him at %.0f %.0f)\n", double(foeAtXZ[0]),
                        double(foeAtXZ[1]), double(foeAt[0]), double(foeAt[2]));
        }
        fightRun.foe.yaw = s->facing;
        fightRun.foe.radius = bodyRadius(s->mo->meshes);
        fightRun.foe.channel = fightRun.foeChannel.get();
        // HIS WALKER. The lift is the player's recipe (`PlayerController`'s
        // `camLift_`) with the opponent's model: the hierarchy root above the
        // model's lowest extent. The swept body is his own sphere list
        // (descriptor +244/+248), centres made relative to the feet.
        fightRun.foeWalker.reset();
        fightRun.foeBlocked = fightRun.foeSlid = fightRun.foeSteps = 0;
        if (!noFoeCollision && !playerSoup.empty() && s->mo->root >= 0) {
            float feet = -1e30f;
            for (const auto& m : s->mo->meshes) feet = std::max(feet, m.pos[1] + m.boxMax[1]);
            const float lift = feet - s->mo->meshes[static_cast<std::size_t>(s->mo->root)].pos[1];
            fightRun.foeLift = (lift > 0.0f && lift < 200.0f) ? lift : 0.0f;
            double fy = double(fightRun.foe.y) + fightRun.foeLift;
            auto w = std::make_unique<omk::Walker>(playerSoup, fightRun.foe.x, fy, fightRun.foe.z);
            if (const auto g = w->ground(fightRun.foe.x, fy, fightRun.foe.z)) {
                w->moveTo(fightRun.foe.x, *g, fightRun.foe.z);
                fy = *g;
            }
            w->setGrid(&playerGrid);
            w->setSteep(&playerSteep);
            std::vector<omk::CollisionSphere> sph;
            if (const auto mp = fs.resolve("MESHES/PERSOS/" + s->model + ".3DO")) {
                const auto md = omk::DataFs::readPath(*mp);
                sph = omk::modelSweepSpheres(md);
            }
            float r = 0.0f;
            std::vector<std::array<double, 3>> centres;
            for (const auto& c : sph) {
                r = std::max(r, c.radius);
                centres.push_back({double(c.pos[0]),
                                   double(c.pos[1]) - double(fightRun.foeLift),
                                   double(c.pos[2])});
            }
            w->setBlockers(&playerSteep, r > 0.0f ? r : 12.0f, std::move(centres));
            w->setBlockerGrid(&playerSteepGrid);
            std::printf("fight: the opponent's walker - feet %.0f (pelvis %.0f, lift %.1f), "
                        "%zu sweep spheres of radius %.1f against %zu wall faces\n",
                        fy, double(fightRun.foe.y), double(fightRun.foeLift), sph.size(),
                        double(r > 0.0f ? r : 12.0f), playerSteep.size() / 9);
            fightRun.foeWalker = std::move(w);
        }
        fightRun.foeBank = fb;
        fightRun.foeClip = -1;
        fightRun.foeRootRef = -1e30f;
        s->inertAfterFight = false;
        fightRun.foeRoot.clear();
        fightRun.foeFrame = 1.0f;

        fightRun.ms = 0.0;
        fightRun.fight = std::make_unique<omk::Fight>(
            // THE CRT's GENERATOR, NOT THE HOST'S (2026-09-23). This was
            // `std::rand()`: on macOS a different generator from the engine's
            // MSVC one, and one the SYSTEM LIBRARIES share - an audio or
            // input thread drawing once before the fight shifted every roll
            // by one, and a headless fight came out two ways, about one run
            // in four. Same stream as the gunmen's (the engine has one).
            [&gunRandSeed] {
                gunRandSeed = gunRandSeed * 214013u + 2531011u;
                return static_cast<int>((gunRandSeed >> 16) & 0x7FFFu);
            },
            [&fightRun] { return static_cast<long>(fightRun.ms); });
        fightRun.fight->setBodyTick([&](float dt, std::uint32_t word) {
            if (player) player->tick(dt, word);
        });
        fightRun.fight->begin(fightRun.player, ps, fightRun.foe, os, level,
                              settings.v.fightDifficulty,
                              settings.v.combatCamera);
        in.installScheme(3);          // `Input_InstallScheme(3)`, group Combat
        fightRun.active = true;
        fightRun.camRaySet = false;
        fightRun.hudRefresh = true;
        fightRun.koSeen = 0;
        fightRun.opponent = opponentId;
        fightRun.body = s;
        fightRun.startedAt = session.frameNo();
        // read back from the CHANNEL, not from what `Fight` meant to write
        std::printf("frame %ld: FIGHT GATE - the player's channel honours priority <= %d "
                    "(experience %d; flag 0x400 %s), the opponent's %s\n",
                    session.frameNo(), player->channel().priorityThreshold(), ps.experience,
                    player->channel().priorityGated() ? "set" : "CLEAR",
                    (fightRun.foeChannel && fightRun.foeChannel->priorityGated()) ? "SET" : "none");
        std::printf("frame %ld: FIGHT BEGINS against CHARACTERS %d - banks '%s' "
                    "vs '%s', AI level %d (profile %d), difficulty %d, "
                    "radius %.1f, scheme 3\n"
                    "  the player: Vie %d, attack %d, dodge %d, experience %d "
                    "(the DB player record)\n"
                    "  the opponent: Vie %d, attack %d, dodge %d, experience %d "
                    "(his chunk record)\n"
                    "  they stand %.0f apart, %.2f m\n",
                    session.frameNo(), opponentId, myBankName.c_str(),
                    foeBankName.c_str(), level, level + 1,
                    settings.v.fightDifficulty,
                    double(fightRun.fight->radius()),
                    ps.vie, ps.attack, ps.dodge, ps.experience,
                    os.vie, os.attack, os.dodge, os.experience,
                    double(fightRun.fight->separation()),
                    double(fightRun.fight->separation()) * 0.0254);
        return true;
    };
    session.setFightHook([&](int opponentId, int level) {
        return beginMelee(opponentId, level);
    });

    // `PlayerController::poseTracks`'s, transcribed rather than reinvented: a
    // `.CTL` clip is an `.ani` DESCRIPTOR with no "3.0V" wrapper, its tracks
    // resolve to meshes by name, and KEY 0 IS THE REST SENTINEL so frame `f`
    // reads key `f + 1`. Only frame 0 is built, because nothing here ticks a
    // channel per actor.
    const auto idleTracksFor = [&](const CharBank& b,
                                   const std::vector<omk::Mesh>& meshes) {
        omk::NodeTracks t;
        if (b.idleClip < 0 || meshes.empty()) return t;
        const auto d = omk::animDescriptor(
            b.data, b.ctl.clips[static_cast<std::size_t>(b.idleClip)].offset);
        if (!d || d->frames <= 0 || d->tracks.empty()) return t;
        const auto lower = [](std::string v) {
            for (auto& c : v) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            return v;
        };
        t.count = static_cast<int>(d->tracks.size());
        t.frames = 1;
        t.rootTrack = -1;
        for (const auto& tr : d->tracks) {
            std::int32_t mi = -1;
            const std::string want = lower(tr.name);
            for (const auto& m : meshes)
                if (lower(m.name) == want) { mi = m.index; break; }
            t.ids.push_back(mi);
        }
        t.quats.assign(1, {});
        t.trans.assign(1, {0.0f, 0.0f, 0.0f});
        t.quats[0].resize(d->tracks.size());
        for (std::size_t i = 0; i < d->tracks.size(); ++i) {
            const omk::AnimTrack& tr = d->tracks[i];
            if (!tr.rotOffset || tr.rotKeys <= 0) continue;
            const int key = tr.rotKeys > 1 ? 1 : 0;      // frame 0 reads key 1
            const std::size_t o = tr.rotOffset + 16u * static_cast<std::size_t>(key);
            if (o + 16 > b.data.size()) continue;
            float q[4];
            std::memcpy(q, b.data.data() + o, 16);
            t.quats[0][i] = {q[0], q[1], q[2], q[3]};
        }
        return t;
    };
    std::vector<omk::Texture> pool;
    std::size_t poolSize = 0;

    // The 3D renderer, behind `PORTING` A2's boundary: the GPU one when the
    // machine has it, the software reference otherwise. Everything below
    // submits DECISIONS and never touches an API, which is what lets the two
    // be swapped by assigning a pointer.
    OMK_HEAPCHECK("before effect sprites");
    // (struct SpriteTable: `backends/sdl/playtypes.h`, todo/play-split.md)
    SpriteTable spriteTab;
    const omk::SpriteLookup spriteLookup = [&spriteTab](int id) { return spriteTab.framesOf(id); };
    const auto loadSpritesInto = [&](SpriteTable& spriteTab, const std::string& scx) {
        const auto ap = fs.resolve("SCPTDATA/" + scx);
        if (!ap) return 0;
        const auto ad = omk::DataFs::readPath(*ap);
        const auto st = omk::readScxStream(ad);
        int n = 0;
        for (const auto& sp : st.sprites) {
            if (sp.id < 0) continue;
            const auto slot = static_cast<std::size_t>(sp.id);
            if (spriteTab.idCount <= slot) spriteTab.idCount = slot + 1;
            if (!sp.model || !sp.texture ||
                sp.offset + sp.model + sp.texture > ad.size()) continue;
            const std::span<const std::byte> mo(ad.data() + sp.offset, sp.model);
            const std::span<const std::byte> te(ad.data() + sp.offset + sp.model,
                                                sp.texture);
            auto t = omk::textures(mo, te);
            if (!t.empty()) spriteTab.tex[sp.id] = t.front();
            spriteTab.frames[sp.id] = omk::spriteFrames(mo);
            ++n;
        }
        return n;
    };
    const auto loadSprites = [&](const std::string& scx) { return loadSpritesInto(spriteTab, scx); };
    // THE TWO LIBRARIES' SPRITES, decoded ONCE (2026-09-23). They never change,
    // and every scene change read `aventure.SCX` and `fight.SCX` again - 4 MB,
    // which a console's memory card takes most of a second over, on the frame
    // of the hand-over. The table a scene change starts from is this one; the
    // order global, fight, scene - and so every collision - is unchanged.
    SpriteTable spriteBase;
    int spriteBaseGlobal = 0, spriteBaseFight = 0;
    // WHICH scene's sprites `spriteTex` currently holds. An effect names its
    // sprite by ID and `Sfx_TickAmbient` resolves that id through the SCENE
    // (`sub_4A5800`), and the ids are scene-local: `Grid.sfx` wants 9..12 and
    // `Grid.SCX` registers exactly those, `anekbah.sfx` wants 49589..49591 and
    // `anekbah.SCX` registers exactly those. Loading one scene's sprites ONCE
    // at boot and then changing scene leaves every later effect resolving
    // against the wrong table - Anekbah's three ids fall outside it entirely
    // and draw nothing, which is fire and smoke not working, while the
    // Impasse's 13/14/114... collide with the GLOBAL library's and draw the
    // wrong picture, which is `todo/omk-play.md` 48.
    // THE GLOBAL LIBRARY IS THE EFFECTS SCENE. `Game_Start` loads
    // `SCPTDATA\aventure.scx` into `stru_930780` (04_sys.c 4425), and
    // `Effects_SetScene(.., &stru_930780)` (05_sys.c 2177/2208) makes THAT the
    // scene `Cef_TickEffects` resolves a `.CTL` record's sound id in through
    // `Scene_FindSoundIndex`, and `Cef_SpawnEffect` its sprite id through
    // `sub_4A5800`. Not the resident scene: the Impasse's 20 sounds have no
    // 194 (the grab, POBJ01.WAV), 199/203 (the footsteps STPR/STPL) or 180,
    // and its 187 is a DEMON footstep where the library's 187 is SNEAKIN.WAV.
    // Searching the resident scene - what this did until 2026-09-05 - made
    // the grab silent and played a demon's step on the confirm.
    std::unique_ptr<omk::ScxRuntime> globalRt;
    if (const auto gp = fs.resolve("SCPTDATA/aventure.SCX")) {
        globalRt = std::make_unique<omk::ScxRuntime>(omk::DataFs::readPath(*gp));
        if (!globalRt->valid()) globalRt.reset();
    }
    // ...AND THE FIGHT'S OWN LIBRARY. `Fight_Begin` (0x004455B0) ends with
    // `Game_Start("fight.scx")` - a second `Game_Start`, exactly like the one
    // that installs `aventure.scx` at boot - and `gamedata/SCPTDATA/fight.SCX`
    // holds **21 sounds and 16 sprites** that ship nowhere else: the punches
    // (`CPOING02`, `PUNCHD`, `PUNCHG`), the kicks (`CPIED03`), the head hit
    // (`COUPTETE03`), the block (`ARRETCOUP01`), the fall (`CHUTEF04`), the
    // cries and `STEPSH1`, at ids 402..423.
    //
    // Without it every `.CTL` effect record of a fight fired with its timing,
    // attach point and scale all computed correctly and then failed its
    // lookup: 68 `ctl-effect` lines in a played fight, every one of them
    // `sound id 408 / 417 / 419 is not in the global library` (those three are
    // `ELECMB02`, `MVT02` and `ELECMB03`). A reader: *"no sound fx, no visual
    // effect"* (`todo/fight-mode.md` 15.2).
    //
    // Loaded once and kept, rather than at `fight.begin`: the engine's
    // `Game_Start` is a load, and doing it here costs one read instead of one
    // per fight.
    std::unique_ptr<omk::ScxRuntime> fightRt;
    if (const auto fp = fs.resolve("SCPTDATA/fight.SCX")) {
        fightRt = std::make_unique<omk::ScxRuntime>(omk::DataFs::readPath(*fp));
        if (!fightRt->valid()) fightRt.reset();
        std::printf("fight library: SCPTDATA/fight.SCX %s\n",
                    fightRt ? "loaded (Fight_Begin's Game_Start)" : "INVALID");
    }
    std::string spriteScx;
    {
        const int glob = loadSpritesInto(spriteBase, "aventure.SCX");
        // ...then the FIGHT's, which `Fight_Begin`'s own `Game_Start` installs
        // (see `fightRt` above). Its 16 sprites are the blow effects the
        // `.CTL` combat states spawn - ids 8..14, 32..35, 40, 43, 44, 192,
        // 193 - and without them every one reported `sprite N is not
        // registered by the library or the scene`.
        //
        // Loaded BEFORE the scene rather than after, although the engine's
        // `Game_Start` comes last, so that no existing scene's ids change
        // meaning: measured, the fight's sixteen collide with NONE of
        // `aventure.SCX`'s twenty, and the fight's own set `ASm49res.SCX`
        // registers no sprites at all, so the order is unobservable here and
        // the safe one is chosen deliberately. A scene that did register one
        // of those ids would keep its own, which is the behaviour this tree
        // already has everywhere else.
        const int fightSp = loadSpritesInto(spriteBase, "fight.SCX");
        spriteBaseGlobal = glob;
        spriteBaseFight = fightSp;
        spriteTab = spriteBase;
        // ...then the SCENE's own, which is what `Sfx_TickAmbient` resolves
        // against and which wins where the ids collide.
        const int local = session.scene().file().empty()
                            ? 0 : loadSprites(session.scene().file());
        int okTex = 0; std::size_t frames = 0;
        for (const auto& kv : spriteTab.tex) if (kv.second.width) ++okTex;
        for (const auto& kv : spriteTab.frames) frames += kv.second.frames.size();
        std::printf("sprites: %d global + %d fight + %d from %s, %d decoded over ids "
                    "0..%zu, %zu frames in all\n", glob, fightSp, local,
                    session.scene().file().c_str(), okTex,
                    spriteTab.idCount == 0 ? 0 : spriteTab.idCount - 1, frames);
        spriteScx = session.scene().file();
    }
    // Re-run that whenever the resident scene changes: the two libraries first
    // (`spriteBase`, decoded once), then the scene's, because the scene's ids
    // WIN where they collide, which is the order `Sfx_TickAmbient` resolves in.
    const auto refreshSprites = [&]() {
        if (session.scene().file() == spriteScx) return;
        spriteScx = session.scene().file();
        const auto sp0 = std::chrono::steady_clock::now();
        spriteTab = spriteBase;                          // the two libraries, as above
        const int glob = spriteBaseGlobal;
        const int fightSp = spriteBaseFight;
        const int local = spriteScx.empty() ? 0 : loadSprites(spriteScx);
        int okTex = 0;
        for (const auto& kv : spriteTab.tex) if (kv.second.width) ++okTex;
        std::printf("sprites: reloaded for %s - %d global + %d fight + %d local, "
                    "%d decoded, %.1f ms\n",
                    spriteScx.empty() ? "<none>" : spriteScx.c_str(), glob,
                    fightSp, local, okTex,
                    std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - sp0).count());
        ++poolComposition;          // the sprite section of the pool changed
    };

    omk::SoftwareRenderer worldSw;
    omk::Renderer& world = vkRen ? *vkRen
                         : glRen ? *glRen
                         : worldVk ? *worldVk
                                   : static_cast<omk::Renderer&>(worldSw);
    // THE ENHANCEMENTS THIS RENDERER CANNOT DRAW, refused here rather than
    // prepared every frame for nothing: each one also turns off what it
    // replaces, so an undrawn per-pixel light left the crowd UNLIT and an
    // undrawn shadow map left every body SHADOWLESS - the Vita, 2026-09-29,
    // whose shared config asked for both. Mapped falls back to fitted, the
    // best of the shadows every backend draws (they are geometry).
    if (lighting > 0 && !world.drawsPixelLights()) {
        std::printf("lighting: per pixel REFUSED - the %s renderer does not draw it; "
                    "per vertex, as the engine lights\n", world.name());
        lighting = 0;
    }
    if (shadowQuality >= 2 && !world.drawsShadowMap()) {
        std::printf("shadows: mapped REFUSED - the %s renderer has no shadow map; fitted\n",
                    world.name());
        shadowQuality = 1;
    }
    bool worldReady = false;
    // (struct WorldSlot: `backends/sdl/playtypes.h`, todo/play-split.md)
    std::array<WorldSlot, 2> worldSlots;
    std::string worldSet;            // the ACTIVE slot's stem - the set under his feet
    std::size_t worldTexBase[2] = {0, 0};   // each slot's first index in `worldTex`
    unsigned worldGen = 0;                  // bumped by every `rebuildWorld`: re-bake the tie

    // (struct Sky: `backends/sdl/playtypes.h`, todo/play-split.md)
    Sky sky;
    // ---------------------------------------------------- THE SHADOWS
    //
    // Option row 5 *Affichage des ombres*. `sub_419060` loads
    // `MESHES\MISC\shadows.3DO` once at game start, `Actor_LoadModel` clones
    // it under every character, and `Actors_TickAll` emits blobs under a
    // fixed set of BONES each frame - see `o3de/shadow.h` for the whole
    // mechanism. Loaded once here for the same reason the engine loads it
    // once: it is not a set's asset and no area change touches it.
    const omk::ShadowModel shadowModel = omk::loadShadowModel(fs);
    // Rebuilt every frame into this one object, outside the loop so the
    // revision accumulates - the same rule `fxGeo` above states, and for the
    // same backend-side reason.
    omk::Geometry shadowGeo;
    std::size_t shadowTexBase = 0;
    // The worst distance from a walker's foot-pair midpoint to his own body
    // point this frame - the orphan-shadow detector (see the ped loop).
    float shadowFootOffMax = 0.0f, pedFootOffMax = 0.0f;
    // The worst vertical spread inside ONE blob - 0 for every classic blob by
    // construction, nonzero for a fitted one wherever the ground is not flat.
    float shadowSpreadMax = 0.0f, shadowSpreadPlayer = 0.0f;
    long shadowBlobsDrawn = 0;
    bool shadowTold = false, shadowLightTold = false, lightsTold = false;
    float shimmerClock = 0.0f;   // `dword_907310`, wrapped at 256
    std::vector<omk::DecorSoup> worldDecors; // every LOADED slot's soup, for decorUnder
    // A set's geometry is REBUILT on every area change, and a fresh
    // Geometry's revision is 0 - the same value the previous set was cached
    // under by the Vulkan backend, which keys its vertex buffer on the
    // POINTER and the revision. So the Impasse drew GRID's tunnel through the
    // Impasse's batch ranges: black with a few stray triangles where the
    // software renderer, which reads the Geometry directly, drew the alley.
    // The particle geometry hit the identical fault on 2026-09-02; the cure
    // is the same - a revision that only ever climbs.
    std::uint64_t worldGeoRev = 0;
    std::vector<omk::Texture> worldTex;     // the shown slots' textures, slot 0 first
    long worldFrames = 0;
    // The particles' quads, REBUILT every frame into this one object. It lives
    // outside the frame loop so `Geometry::revision` accumulates: the Vulkan
    // backend caches a vertex buffer by pointer and revision, and a
    // block-local Geometry had the same address and a revision of 1 on every
    // frame, so the GPU drew the first frame's particles for ever while the
    // software path animated - the posed-character bug over again, one level
    // out, because this geometry is rebuilt rather than mutated.
    omk::Geometry fxGeo;
    // Where the scene's character is this frame - his model origin, the
    // pelvis - for set pieces linked to him. `sub_450FC0` case 2 finds an
    // actor by the first THREE letters of its name, uppercased ('HO1' for
    // HO1_FNM, Kay'l); the intro's arrival piece is linked that way and
    // plays `kaylarr` and `kay arr` at his position as he lands. His frame
    // is the identity here because the facing is not applied (see
    // `SceneRunner::Started::euler`), and the position is LAST frame's -
    // the runner ticks before the pose is composed. Type 3 (the PLAYER,
    // `unk_8F5EA0`) has no counterpart in this viewer and is left to the
    // runner's absolute fallback, which setpiece.h labels.
    float actorAt[3] = {0, 0, 0};
    bool  actorKnown = false;
    session.sceneMutable().setPieceLinks(
        [&](int type, std::uint32_t id, omk::PieceLink& L) -> bool {
            if (type != 2) return false;
            const char tag[4] = {static_cast<char>((id >> 16) & 0xFF),
                                 static_cast<char>((id >> 8) & 0xFF),
                                 static_cast<char>(id & 0xFF), 0};
            const auto tagged = [&](const std::string& m) {
                if (m.size() < 3) return false;
                for (int k = 0; k < 3; ++k) {
                    char c = m[static_cast<std::size_t>(k)];
                    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
                    if (c != tag[k]) return false;
                }
                return true;
            };
            // EVERY body on screen is a candidate now, not just the one this
            // file used to draw - the piece is linked to the actor whose model
            // carries the tag.
            for (const auto& up : staged)
                if (up->drawn && tagged(up->model)) {
                    for (int k = 0; k < 3; ++k) L.pos[k] = up->drawAt[k];
                    L.hasMatrix = true;   // identity: the facing is unported
                    return true;
                }
            if (!actorKnown || !tagged(playerModel)) return false;
            for (int k = 0; k < 3; ++k) L.pos[k] = actorAt[k];
            L.hasMatrix = true;   // identity: the facing is unported
            return true;
        });
    int  lastCamera = -2;
    // (struct SetLoad: `backends/sdl/playtypes.h`, todo/play-split.md)
    std::shared_ptr<SetLoad> setLoads[2];
    // what each slot was last ASKED for - not `WorldSlot::stem`, which is what
    // is IN it: a set that does not resolve is asked for once, not every frame
    std::string slotAsked[2];
    int slotAskedArea[2] = {-1, -1};
    const bool syncSets = omk::envSet("OMK_SYNC_SETS");   // A/B only
    // THE LOAD HELD FOR THE SET (`Session::setLoadGate`). A console's card and
    // A9 took 1.3 s over Anekbah where the slices give 0.57, and the frame
    // that brought the set in waited 727 ms for the rest (2026-09-30). With
    // the gate the Session's load waits instead, as `Area_TickLoad` waits on
    // its reader, and the game draws on. The frame the set arrives on is then
    // the machine's, so it is ON only where that is wanted: a Vita, or
    // `OMK_LOAD_GATE=1`; every check runs by the count alone.
#if defined(__vita__)
    const bool loadGate = !syncSets && !omk::envSet("OMK_NO_LOAD_GATE");
#else
    const bool loadGate = !syncSets && omk::envSet("OMK_LOAD_GATE");
#endif
    // (a test's slow card: the job sleeps this long first - desktop only)
    const int loadDelayMs = std::getenv("OMK_LOAD_DELAY_MS") ? std::atoi(std::getenv("OMK_LOAD_DELAY_MS")) : 0;
    session.setVoiceToDevice([](const std::vector<std::int16_t>& pcm, int channels) {
        return resampleToDevice(pcm, channels, 22050, kDeviceRate);
    });
    if (loadGate)
        session.setLoadGate([&setLoads](int slot) {
            const auto& L = setLoads[slot & 1];
            return !L || !L->job || L->job->ready();
        });
    const auto prepareSet = [](SetLoad& L) {
        using clk = std::chrono::steady_clock;
        const auto since = [](clk::time_point a) {
            return std::chrono::duration<double, std::milli>(clk::now() - a).count();
        };
        WorldSlot& w = L.out;
#if !defined(__vita__)
        if (L.delayMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(L.delayMs));
#endif
        auto t = clk::now();
        const auto d = omk::DataFs::readPath(L.path3do);
        std::vector<std::byte> td;
        if (!L.path3dt.empty()) td = omk::DataFs::readPath(L.path3dt);
        L.bytes = d.size() + td.size();
        L.ms[0] = since(t);
        if (d.empty()) return;
        w.stem = L.stem;
        w.area = L.area;
        t = clk::now();
        w.geo = omk::buildGeometry(d, omk::DrawFilter::Engine);
        // The runs, in submission order: batch by batch, and inside a batch
        // split wherever `cornerMesh` changes. A set whose corners carry no
        // mesh index leaves this empty, and the draw path then submits whole
        // batches exactly as it did before.
        w.runs.clear();
        if (w.geo.cornerMesh.size() == w.geo.corners.size()) {
            for (std::size_t bi = 0; bi < w.geo.batches.size(); ++bi) {
                const auto& b = w.geo.batches[bi];
                std::uint32_t c = 0;
                while (c < b.count) {
                    const std::size_t at = static_cast<std::size_t>(b.start) + c;
                    const std::int32_t mi = w.geo.cornerMesh[at];
                    std::uint32_t n = 0;
                    while (c + n < b.count &&
                           w.geo.cornerMesh[static_cast<std::size_t>(b.start) + c + n] == mi) ++n;
                    w.runs.push_back({static_cast<std::uint32_t>(b.start + c), n, mi, bi});
                    c += n;
                }
            }
        }
        L.ms[1] = since(t);
        t = clk::now();
        if (!L.path3dt.empty()) w.tex = omk::textures(d, td);
        L.ms[2] = since(t);
        t = clk::now();
        w.soup = omk::collisionSoup(d, omk::SoupKind::Walkable, &w.soupMesh);
        w.steep = omk::collisionSoup(d, omk::SoupKind::Steep, &w.steepMesh);
        w.shotSoup = omk::collisionSoup(d, omk::SoupKind::Shot);
        w.sightSoup = omk::collisionSoup(d, omk::SoupKind::Sight);
        w.baseSoup.clear(); w.baseSteep.clear();
        L.ms[3] = since(t);
        t = clk::now();
        w.mirror = omk::mirrorPlane(d);
        if (const auto mh = omk::readHeader(d)) {
            w.lights = omk::readLights(d, *mh);
            w.meshes = omk::readMeshes(d, *mh);
        }
        // THE SET'S OWN EMITTERS - `Sfx_BindAmbientEffects`, the environment
        // family. Every mesh flagged 0x40000000 whose first four name bytes
        // match a section-D tag registers that binding's effect at the mesh's
        // position: the neon, the steam, the smoke. They come up with the SET,
        // not with any object, which is why nothing started them and why they
        // had never appeared here. 319 across the 12 sets that have any.
        // BOUND IN THE FRAME LOOP, not here: walking in, the set loads while
        // the OUTGOING scene is resident, and binding here put ANEKBAH's
        // meshes against the Impasse's `.sfx` (102, the neon alone) in a pool
        // that was about to be dropped, and the city's own `.SCX` then came up
        // with none - the Bowie sequence's fire among them (2026-09-29).
        w.emitters = omk::SceneRunner::setEmitterMeshes(d);
        L.ms[4] = since(t);
        L.found = true;
    };
    // The slot emptied and the new set's preparation started. -> whether the
    // world lost a set by it (the rebuild is then owed at once).
    const auto askSet = [&](int slot, const std::string& stem, int area, long frame) {
        slot &= 1;
        WorldSlot& w = worldSlots[static_cast<std::size_t>(slot)];
        const bool had = !w.stem.empty();
        w = WorldSlot{};
        mergedValid = false;
        w.geo.revision = ++worldGeoRev;
        setLoads[slot].reset();          // a job still running finishes into its own state
        slotAsked[slot] = stem;
        slotAskedArea[slot] = area;
        if (stem.empty()) return had;
        const auto o = fs.resolve("MESHES/DECORS/" + stem + ".3DO");
        if (!o) { std::printf("world: no set %s.3DO\n", stem.c_str()); return had; }
        auto L = std::make_shared<SetLoad>();
        L->slot = slot;
        L->stem = stem;
        L->area = area;
        L->askedFrame = frame;
        L->path3do = *o;
        if (const auto t = fs.resolve("MESHES/DECORS/" + stem + ".3DT")) L->path3dt = *t;
        // on a thread only when there are frames to spend on it: a load that
        // comes in on this same frame is simply done here
        const bool streams = !syncSets && session.loading() && session.loadingSlot() == slot &&
                             session.loadSlicesLeft() > 1;
        if (!streams) prepareSet(*L);
        else {
            L->delayMs = loadDelayMs;
            L->job = std::make_unique<omk::BackgroundJob>([L, prepareSet] { prepareSet(*L); });
        }
        setLoads[slot] = L;
        return had;
    };
    // The prepared set into its slot, on the frame's thread. -> whether it came.
    const auto integrateSet = [&](SetLoad& L, long frame) {
        double waited = 0.0;
        if (L.job) {
            const double w0 = static_cast<double>(SDL_GetPerformanceCounter());
            L.job->wait();
            waited = (static_cast<double>(SDL_GetPerformanceCounter()) - w0) * 1000.0 /
                     static_cast<double>(SDL_GetPerformanceFrequency());
        }
        if (!L.found) return false;
        WorldSlot& w = worldSlots[static_cast<std::size_t>(L.slot & 1)];
        w = std::move(L.out);
        mergedValid = false;
        w.geo.revision = ++worldGeoRev;
        // The Vulkan one is already initialised - its swapchain had to exist
        // before the window could be presented to at all.
        if (!worldReady) {
            if (!vkRen && !glRen && !worldVk) worldSw.init(dispW, dispH);
            worldReady = true;
        }
        std::printf("world: slot %d set %s (AREA %d) - %zu corners, %zu batches, "
                    "%zu textures, %zu walkable triangles%s\n",
                    L.slot, L.stem.c_str(), L.area, w.geo.corners.size(), w.geo.batches.size(),
                    w.tex.size(), w.soup.size() / 9, w.mirror.found ? ", mirror" : "");
        std::printf("set load: %s - %zu KB read in %.1f ms, geometry %.1f, textures %.1f, "
                    "soups %.1f, the rest %.1f; asked at frame %ld, in at frame %ld - %s, "
                    "the frame waited %.1f ms for it, the Session's load %d frame(s)\n",
                    L.stem.c_str(), L.bytes / 1024, L.ms[0], L.ms[1], L.ms[2], L.ms[3], L.ms[4],
                    L.askedFrame, frame,
                    !L.job ? "prepared on the frame" :
                    L.job->threaded() ? "prepared on its own thread" : "no thread, prepared on the frame",
                    waited, L.heldFrames);
        return true;
    };
    // After any slot changed: the texture pool (slot 0's first, then slot
    // 1's - a batch's material is offset by its slot's base), the decor list
    // the feet are probed against, and the WALKER'S soup. `playerSoup` is
    // refilled IN PLACE because the controller's walker holds a reference to
    // it: the player walks off one set onto the other without being rebuilt,
    // and his `.CTL` state, position and facing survive the transition the
    // way the engine's actor does (it is one record; only the decor changes).
    const auto rebuildWorld = [&]() {
        const double rebuild0 = static_cast<double>(SDL_GetPerformanceCounter());
        ++worldGen;
        worldTex.clear();
        worldDecors.clear();
        playerSoup.clear();
        playerSteep.clear();   // rebuilt with the soup, or it accumulates
        for (int slot = 0; slot < 2; ++slot) {
            WorldSlot& w = worldSlots[static_cast<std::size_t>(slot)];
            worldTexBase[slot] = worldTex.size();
            if (w.stem.empty()) continue;
            worldTex.insert(worldTex.end(), w.tex.begin(), w.tex.end());
            worldDecors.push_back({w.area, &w.soup});
            playerSoup.insert(playerSoup.end(), w.soup.begin(), w.soup.end());
            playerSteep.insert(playerSteep.end(), w.steep.begin(), w.steep.end());
        }
        const double rebuildA = static_cast<double>(SDL_GetPerformanceCounter());
        playerMovingTri.assign(playerSoup.size() / 9, 0);   // new sets: nothing has moved yet
        playerMovingIds.clear();
        rebuildFixedGrid();
        rebuildMovingGrid();
        const double rebuildB = static_cast<double>(SDL_GetPerformanceCounter());
        steepMovingTri.assign(playerSteep.size() / 9, 0);
        steepMovingIds.clear();
        rebuildSteepFixedGrid();
        rebuildSteepMovingGrid();
        mergedValid = true;
        const double rebuild1 = static_cast<double>(SDL_GetPerformanceCounter());
        // NOT handed to the renderer here. The frame's pool - the sets', then
        // the characters', the player's and the sprites' - is composed and set
        // before anything is drawn, and this bumps `poolComposition`, so it
        // will be. Setting the sets' textures ALONE first made a backend that
        // keeps what it has uploaded drop every character and sprite texture
        // and send them again a moment later: on a console's walk into
        // Anekbah, `27 dropped` then `18 uploaded, 57 ms`, and again `18
        // dropped` / `18 uploaded, 52 ms` on the arrival (2026-09-30).
        // `OMK_POOL_TWICE=1` is the old order, for the comparison.
        static const bool poolTwice = omk::envSet("OMK_POOL_TWICE");
        if (poolTwice) world.setTextures(worldTex);
        poolSize = worldTex.size();
        ++poolComposition;   // the character and sprite sections re-append over this
        const double hz = static_cast<double>(SDL_GetPerformanceFrequency());
        std::printf("world: rebuild - soups and grids %.1f ms (the merge %.1f, the walkable grid "
                    "%.1f over %zu triangles, the steep one %.1f over %zu), the texture pool %.1f ms\n",
                    (rebuild1 - rebuild0) * 1000.0 / hz, (rebuildA - rebuild0) * 1000.0 / hz,
                    (rebuildB - rebuildA) * 1000.0 / hz, playerSoup.size() / 9,
                    (rebuild1 - rebuildB) * 1000.0 / hz, playerSteep.size() / 9,
                    (static_cast<double>(SDL_GetPerformanceCounter()) - rebuild1) * 1000.0 / hz);
    };

    // ---- and now they PLAY -------------------------------------------
    //
    // Three MPEG-1 program streams at 320x240, doubled to the framebuffer.
    // `movies` is the boot chain's own order. LMENU skips them, which is the
    // engine's key and not this frontend's invention.
    // A frame budget is for the headless checks, and there the budget must
    // reach the SCREEN - otherwise the dump is a movie frame and the
    // live-vs-reference comparison compares the wrong thing. `--nofmv` is the
    // engine's own switch and is what the checks pass; this only guards the
    // case where someone bounds the frames and forgets.
    if (playMovies && !frames) {
        const char* movies[3] = {"FLIS/EIDOS.mpg", "FLIS/QUANTIC.mpg",
                                 "FLIS/GAME.mpg"};
        omk::Surface mv(dispW, dispH, 0);
        bool skipAll = false, bounded = false;
        for (const char* name : movies) {
            if (skipAll) break;
#if defined(__vita__)
            // THE HARDWARE PATH (`backends/vita/avmovie.h`): the film converted
            // to H.264 by `scripts/vita-movies.sh` and copied to
            // ux0:data/omk/movies/<NAME>.mp4 plays on the Vita's decoder -
            // same skip rules, same audio queue, same present. Without the
            // file, the software decoder below, as before.
            {
                std::string stem = name;
                if (const auto sl = stem.find_last_of('/'); sl != std::string::npos) stem = stem.substr(sl + 1);
                if (const auto dt = stem.rfind('.'); dt != std::string::npos) stem = stem.substr(0, dt);
                for (auto& c : stem) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                std::string where;
                const std::string mp4 = omk::vita::avFind(stem, fr, where);
                if (omk::vita::AvFilm* av = mp4.empty() ? nullptr : omk::vita::avOpen(mp4)) {
                    std::printf("  %s: hardware decoder, %s\n", name, mp4.c_str());
                    front.openAudio(44100, 2);
                    omk::Surface film(320, 240, 0);
                    std::vector<float> pcm;
                    int rate = 0;
                    long shownAv = 0;
                    // NOT ACTIVE YET is not FINISHED: on a console the player
                    // buffers after `sceAvPlayerAddSource` and only then
                    // reports active, so testing `avActive` first ended every
                    // film at once - "0 frames shown, sound at 0 Hz" for all
                    // three (console log 2026-09-18). Vita3K is active at once,
                    // which is why it played there. Wait up to 3 s for the
                    // start; after that, inactive means the end.
                    const Uint32 avStart = SDL_GetTicks();
                    bool avStarted = false;
                    const char* avEnd = "the film ended";
                    for (;;) {
                        if (omk::vita::avActive(av)) avStarted = true;
                        else if (avStarted || SDL_GetTicks() - avStart > 3000) {
                            if (!avStarted) avEnd = "the player never became active (3 s)";
                            break;
                        }
                        omk::HostInput h;
                        if (!front.pump(h)) { skipAll = true; avEnd = "the window closed"; break; }
                        if (!h.held.empty() || h.pad.buttons != 0) {
                            if (h.held.count(0x38)) skipAll = true;      // DIK_LMENU
                            avEnd = h.pad.buttons != 0 ? "skipped by a pad button" : "skipped by a key";
                            break;
                        }
                        // THE SOUND IS THE PLAYER'S CLOCK. SceAvPlayer times
                        // its pictures by the sound taken from it, and this
                        // took up to sixteen chunks a pass as fast as they
                        // came - so the clock ran ahead, the picture with it,
                        // and the sound queued here and played at its own
                        // speed: "the video is played accelerated while the
                        // audio is played at normal speed" (the reader,
                        // 2026-09-23, on the first console run that played a
                        // film). Taken only while less than a quarter second
                        // waits in the device, it is taken as fast as it plays.
                        pcm.clear();
                        if (front.queuedSeconds() < 0.25 &&
                            omk::vita::avAudio(av, pcm, rate) && !pcm.empty())
                            front.queueAudio(pcm);
                        if (omk::vita::avVideo(av, film)) { present(film); ++shownAv; }
                        else SDL_Delay(2);
                    }
                    omk::vita::avClose(av);
                    front.flushAudio();
                    for (int guard = 0; guard < 300; ++guard) {
                        omk::HostInput h;
                        if (!front.pump(h)) break;
                        if (h.held.empty() && h.pad.buttons == 0) break;
                        SDL_Delay(10);
                    }
                    std::printf("  %s: %ld frames shown, sound at %d Hz - %s\n", name, shownAv, rate, avEnd);
                    continue;
                }
                // SAID: a console log that showed only the software path gave
                // no way to tell a missing copy from a player that refused it
                // (avOpen names its own failures)
                if (mp4.empty())
                    std::printf("  %s: no %s.mp4 found - decoding the MPEG-1 in software "
                                "(scripts/vita-movies.sh makes one). Looked in:%s\n",
                                name, stem.c_str(), where.c_str());
            }
#endif
            const auto real = fs.resolve(name);
            omk::Movie mov;
            if (!real || !mov.open(*real)) {
                std::printf("  %s: not decodable, skipped\n", name);
                continue;
            }
            std::printf("  %s %dx%d %.2f s\n", name, mov.info().width,
                        mov.info().height, mov.info().duration);
            // 44100 stereo, the stream's own rate - NOT the engine's 22050
            // primary, which these never went through (the world reopens the
            // device at that rate after them).
            const bool audioOk =
                front.openAudio(mov.info().sampleRate ? mov.info().sampleRate : 44100, 2);
            const double fps = mov.info().framerate > 0 ? mov.info().framerate : 30.0;
            // THE CLOCK THE PICTURE FOLLOWS: what the audio device has
            // PLAYED - decoded minus still queued - when there is a device,
            // and the wall clock when there is none. Without that second
            // half a run with no audio device measures "heard" as the
            // DECODER's position (nothing is queued, so nothing is
            // subtracted), which races ahead of real time, and the
            // frame-dropping below would discard nearly every frame.
            const Uint32 movieStart = SDL_GetTicks();
            const auto heardSeconds = [&] {
                return audioOk ? mov.audioSeconds() - front.queuedSeconds()
                               : (SDL_GetTicks() - movieStart) / 1000.0;
            };
            long shown = 0;
            // THE FILM AT ITS OWN SIZE on a GPU present: the GLES pass fits a
            // surface to the window itself, so scaling 320x240 up to the
            // display on the CPU first was pure cost - on a Vita, most of a
            // frame (`todo/vita-port.md`). The SDL upload path keeps the
            // display-sized surface: its texture is the window's size.
            omk::Surface film(mov.info().width, mov.info().height, 0);
            omk::Surface& target = glRen ? film : mv;
            // A FRAME THAT IS LATE IS DECODED AND DROPPED. The sound runs at
            // its own rate on the audio device; the loop used to wait when the
            // picture was early and never catch up when it was late, so on a
            // slow CPU the picture crawled behind a sound at normal speed.
            const auto late = [&] {
                if (frames) return false;
                return heardSeconds() - (shown + 1) / fps > 1.0 / fps;
            };
            long dropped = 0;
            int droppedInARow = 0;
            for (;;) {
                // At most THREE drops in a row. When decoding one frame costs
                // more than a frame lasts - a Vita, where only the SOUND of the
                // films came through - "late" never clears, and dropping every
                // late frame dropped them all. This shows one frame in four at
                // worst: a slower picture, still in step with the sound.
                const bool behind = droppedInARow < 3 && late();
                if (behind ? !mov.skipFrame() : !mov.nextFrame(target)) break;
                omk::HostInput h;
                if (!front.pump(h)) { skipAll = true; break; }
                // `docs/BOOT.md` 2: ANY key ends the movie playing, and left
                // ALT latches and ends all three. Accepting only ALT and ESC -
                // which is what this did - means a player pressing space or
                // return sits through the whole thing.
                // ...and ANY PAD BUTTON too: a pad's buttons are the game's
                // joystick, not keys in `held` (only START is), so on a Vita
                // nothing but START could skip - and a film drawing slowly
                // polls it seldom.
                if (!h.held.empty() || h.pad.buttons != 0) {
                    if (h.held.count(0x38)) skipAll = true;      // DIK_LMENU
                    break;
                }
                // THE SOUND IS DECODED HALF A SECOND AHEAD OF THE PICTURE, not
                // to the end of the film (2026-09-23). This drained
                // `nextAudio` until it came back empty, and pl_mpeg's audio
                // decoder reads on through the whole stream - so the FIRST
                // frame decoded all 107 s of `GAME.mpg`'s sound into the
                // device queue, and the demuxer buffered every video packet it
                // stepped over on the way. A desktop has the memory to hide
                // that; the Vita, on its MPEG-1 fallback (no H.264 copy), asked
                // for 72 MB in one piece and died of `bad_alloc` in Vita3K.
                // `heardSeconds` is decoded minus still queued, so the pacing
                // reads the same clock either way.
                constexpr double kAudioAhead = 0.5;
                while (mov.audioSeconds() < (shown + 1) / fps + kAudioAhead) {
                    const auto blk = mov.nextAudio();
                    if (blk.empty()) break;
                    front.queueAudio(blk);
                }
                ++shown;
                if (behind) { ++dropped; ++droppedInARow; continue; }
                droppedInARow = 0;
                present(target);

                // PACE BY THE AUDIO, not by a fixed delay. Sleeping 1000/fps
                // after each frame adds the DECODE time to every frame, so the
                // picture falls steadily behind a soundtrack that plays at its
                // own rate. The audio device is the only clock running at the
                // rate a person hears: what has been decoded, minus what is
                // still queued, is the moment being heard now. Wait only while
                // the picture is ahead of it - and when it is behind, `late`
                // above drops frames until it is not.
                if (!frames) {
                    const double ahead = shown / fps - heardSeconds();
                    if (ahead > 0.001 && ahead < 1.0)
                        SDL_Delay(static_cast<Uint32>(ahead * 1000.0));
                }
                if (frames && mov.framesDecoded() >= frames) {
                    skipAll = bounded = true; break;
                }
            }
            // Whatever the decoder ran ahead into is still in the device, and
            // a skipped movie must not go on playing under what follows.
            front.flushAudio();
            // the button that skipped this film must come UP before the next
            // one starts, or one press skips them all
            for (int guard = 0; guard < 300; ++guard) {
                omk::HostInput h;
                if (!front.pump(h)) break;
                if (h.held.empty() && h.pad.buttons == 0) break;
                SDL_Delay(10);
            }
            if (dropped)
                std::printf("  %s: %ld of %ld frames dropped to keep up with the sound\n",
                            name, dropped, shown);
        }
        std::printf("movies %s\n",
                    !skipAll        ? "played (any key skips one, ALT skips all)"
                    : bounded       ? "cut short by --frames"
                                    : "skipped (ALT)");
    }
    // THE AUDIO DEVICE IS THE WORLD'S TOO. `openAudio` was called in one
    // place - the movie player, at the movie's own rate - so a run with
    // `--nofmv` (or a street start, which skips the movies) never opened it
    // and every world sound was dropped without a word: no music, no
    // effects, no voices. A reader on 2026-09-04: "there is absolutely no
    // sound at all". The world plays at the primary's 22050 (`kDeviceRate`),
    // and a device the films opened at their 44100 is REOPENED at it.
    if (!frames) {
        if (front.reopenAudio(kDeviceRate, 2))
            std::printf("audio: device open at %d Hz stereo for the world\n", kDeviceRate);
        else
            std::printf("audio: NO DEVICE (%s) - the world will be silent\n", SDL_GetError());
    }


    // ---- THE SPLASH SCREEN ------------------------------------------
    //
    // `Game_Main` shows it between `Game_Start("aventure.scx")` and
    // `Game_RunLoop`, which is exactly here:
    //
    //     push offset aAventureScx_3 ; "aventure.scx"
    //     call sub_41B5A0            ; Game_Start
    //     push offset aImagesOmikronB ; "IMAGES\OMIKRON.BMP"
    //     call sub_420A20            ; <- the splash
    //     call sub_439310            ; Game_RunLoop
    //
    // and `sub_420A20` is: load the bitmap, blit it over the whole screen,
    // present, free it, and **`Sleep(0x1388)` - five seconds**. It is a
    // blocking sleep, so unlike the movies it is NOT skippable, and that is
    // reproduced rather than improved on. Events are pumped through the wait
    // only so the host can close the window; no key shortens it.
    if (!frames) {
        const omk::Surface splash =
            omk::surfaceFromBmp(fs.read("IMAGES/OMIKRON.BMP"));
        if (splash.valid()) {
            // The splash is a 640x480 file and `Blt` stretches, which is
            // how the original puts it on a bigger display too.
            omk::Surface sf(dispW, dispH, 0);
            omk::blt(sf, {0, 0, dispW, dispH}, splash,
                     {0, 0, splash.w, splash.h}, omk::kBltWait);
            present(sf);
            std::printf("splash: IMAGES/OMIKRON.BMP, 5 s (Sleep(0x1388) - "
                        "not skippable in the original either)\n");
            const Uint32 until = SDL_GetTicks() + 5000;
            while (SDL_GetTicks() < until) {
                omk::HostInput h;
                if (!front.pump(h)) break;      // the window closed
                present(sf);
                SDL_Delay(16);
            }
        } else {
            std::printf("no IMAGES/OMIKRON.BMP - no splash\n");
        }
    }

    std::printf("screen %d. arrows move, ENTER confirms, TAB closes, "
                "ESC opens the pause screen.\n", screenId);

    omk::HostInput host;
    omk::Surface fb(dispW, dispH, 0);
    long n = 0;
    // WAIT FOR THE KEY THAT SKIPPED THE MOVIE TO COME UP. Any key now ends a
    // movie and ESC also quits, so one press did both: skipped the last movie
    // and closed the menu behind it - "0 frames presented", with the window
    // gone before it drew. A game edge-triggers; this is the frame-zero half.
    for (int guard = 0; guard < 300; ++guard) {
        if (!front.pump(host)) break;
        if (host.held.empty() && host.pad.buttons == 0) break;   // pad buttons too
        SDL_Delay(10);
    }
    Uint32 lastMs = SDL_GetTicks();
    // the interface's clock: the wall's, or a headless run's frames at 30 a second
    const auto uiClockMs = [&]() -> long {
        if (frames > 0) return n * 1000 / 30;
        return static_cast<long>(SDL_GetTicks());
    };
    Uint32 fpsSince = lastMs, fpsLastMs = lastMs, fpsWorst = 0;
    int    fpsFrames = 0;
    // Where `Script_Display3DSprite` puts a sprite: the active camera's
    // TARGET, read by the handler on every tick it runs (program.h has the
    // trace; the XYZ table it would prefer is never written). The runner is
    // handed the camera the LAST frame drew with - one frame behind, since
    // the script ticks before this frame's camera is settled.
    float spriteAnchor[3] = {0.0f, 0.0f, 0.0f};
    bool  spriteAnchorSet = false;
    std::map<int, long> spriteLogged;     // row -> the link tick already reported
    // ---- THE PLAYER'S DAMAGE, from the HIT onward ----
    // `sub_4240E0`'s (a bolt) and `sub_423B10`'s (a strike - the dogs' bite,
    // `todo/released-spectres.md` step 5) player arms are the SAME from the
    // damage on: the death `sub_423FC0` before the gauge, or the hurt shove,
    // the gauge, property 1 and message 0. `what` names the source in the log.
    const auto applyPlayerDamage = [&](long n, int owner, const char* what, int dmgIn,
                                       int shield, const omk::HitOut& ho) {
                        const std::size_t recAt = static_cast<std::size_t>(omk::GameState::kPlayerRecord);
                        const std::size_t recLen = static_cast<std::size_t>(omk::GameState::kPlayerRecordSize);
                        if (ho.killed) {
                            // `if (+92 <= 0) { sub_423FC0(him); return v30; }` - BEFORE
                            // the gauge, the property and the message, so the killing
                            // hit leaves the gauge at its last value and tells no one.
                            // ---- THE DEATH, `sub_423FC0` (05_sys.c 4606, ported
                            // 2026-09-11 - a reader: "continue with the player's death") -
                            //  1. every live gunman (+160 0x40 up, 0x4002 clear, +92 > 0)
                            //     STANDS DOWN: action 0, or on script step 8 his 0x20
                            //     cleared - applied on his own next tick here, where the
                            //     engine does it inside the hit;
                            //  2. ACTOR_STATE 15; `sub_436D20` shows his body (this port
                            //     draws it throughout - NOT PORTED as a switch); then
                            //     `sub_47CE70` (a global actor's pitch and roll zeroed,
                            //     its writer not traced - NOT PORTED); MESSAGE 9;
                            //  3. .CTL group 201 on his channel, and the countdown
                            //     `dword_4E975C` = its default entry's clip length
                            int stood = 0;
                            for (const auto& [ga, gr] : shootBrains)
                                if ((gr.flags & 0x40u) && !(gr.flags & 0x4002u) && gr.health > 0) {
                                    gunStandDown.insert(ga);
                                    ++stood;
                                }
                            playerDeathCountdown = 0.0f;
                            if (player) player->setActorState(omk::ActorState::Shoot15, "sub_423FC0");
                            const bool ran9 = session.postMessage(9, session.playerActor());
                            const bool g201 = player && player->enterGroupById(201);
                            if (g201) playerDeathCountdown = static_cast<float>(player->clipFrames());
                            std::printf("frame %ld: PLAYER HIT by actor %d's %s - damage %d, Body "
                                        "Shield %d -> %d; health %d -> %d - KILLED (sub_423FC0): "
                                        "ACTOR_STATE 15, message 9 %s, .CTL group 201 %s, %.0f "
                                        "frames to count down, %d gunmen stand down; the gauge "
                                        "stays at %d\n", n, owner, what, dmgIn, shield,
                                        ho.damage, ho.healthWas, ho.health,
                                        ran9 ? "to its handler" : "- NO handler subscribes",
                                        g201 ? "on" : "NOT FOUND", double(playerDeathCountdown),
                                        stood, hudHealth);
                            return;
                        }
                        // ---- THE HURT REACTION, `sub_47D1F0` (ported 2026-09-12)
                        // The SOUND first - `word_657A14` resolved in the
                        // resident library, which in shoot mode is shoot2.scx,
                        // and played FLAT: `Sound_Play3D`'s `a3` is 0, so it
                        // takes the `v6[12] = 4` arm and carries no position.
                        // Then the shove itself, pointed by the hit's own band
                        // and spent over four frames by the mover above.
                        if (shootRt) {
                            const int w = shootRt->wavBydId(shootMover.hurtSound);
                            if (w < 0)
                                std::printf("  the hurt sound %d is not in shoot2.scx\n",
                                            shootMover.hurtSound);
                            else {
                                const SfxSample& sm = sfxPcm(shootRt->wavData(w));
                                if (!sm.pcm->empty()) front.playSound(sm.pcm, false, fxGain());
                            }
                        }
                        const bool shoved = player &&
                            omk::shootHurt(shootMover, ho.band, player->eulerPitch(),
                                           player->eulerRoll());
                        // then the gauge, property 1 and message 0
                        hudHealth = playerShootRec.health;                // `dword_90E100`
                        omk::writeActorProperty(state.rawMutable().subspan(recAt, recLen), 1,
                                                playerShootRec.health);
                        std::int32_t stored = 0;
                        omk::readActorProperty(state.raw().subspan(recAt, recLen), 1, stored);
                        const bool ran = session.postMessage(0, session.playerActor());
                        std::printf("frame %ld: PLAYER HIT by actor %d's %s - damage %d, Body "
                                    "Shield %d -> %d; health %d -> %d, gauge %d (property 1 stored "
                                    "%d); message 0 %s; the shove (sub_47D1F0) band %d %s\n",
                                    n, owner, what, dmgIn, shield,
                                    ho.damage, ho.healthWas, ho.health, hudHealth, int(stored),
                                    ran ? "to the hurt handler" : "- NO handler subscribes",
                                    ho.band,
                                    !shoved ? "- NO shooter installed"
                                            : ho.band < 2
                                                ? "rolls him - and the first-person preset "
                                                  "CANNOT SHOW a roll"
                                                : "TIPS the view 2 degrees");
    };
    std::set<std::string> motionLogged;   // mesh/pool pairs already reported
    // The 30 Hz pacer's next deadline, in seconds on the performance counter
    // (see the cap at the bottom of this loop).
    double paceNext = 0.0;
    // THE FRAME'S PHASES, timed - an instrument for the Vita (2026-09-18: the
    // city "unplayable", and the SLOW FRAME line below gives only the total).
    // Four spans on the performance counter: the simulation and the draw
    // submission up to the readback, the readback itself (on GLES the wait for
    // the GPU), the CPU compose after it, and the present with its swap.
    // Averaged over 60 frames and printed as one `frame phases:` line.
    const double phaseHz = static_cast<double>(SDL_GetPerformanceFrequency());
    const auto phaseNow = [&] { return static_cast<double>(SDL_GetPerformanceCounter()) / phaseHz; };
    double phTop = 0.0, phRb0 = -1.0, phRb1 = -1.0;
    // ...and a few NAMED spans inside them, summed the same way, so a slow
    // phase says which call it is (the Vita's start menu: ~600 ms of
    // "sim+draw" with no world drawn).
    // ...and MARKS down the frame, so a slow one names its SECTION: each
    // mark is the end of the section before it, the gaps are printed on a
    // frame over `OMK_MARKS_MS` (default 150) from the largest down. The named
    // spans covered 6 of a console's 200 ms city frame (2026-09-22).
    std::vector<std::pair<const char*, double>> phMarks;
    const auto mark = [&](const char* name) { phMarks.emplace_back(name, phaseNow()); };
    std::map<std::string, double> phSpan;
    // ...and every SECTION between two marks, summed over the 60-frame window
    // and printed with the spans: the per-frame breakdown prints only past
    // `OMK_MARKS_MS` and only when paced, so a console frame of 65 ms - over
    // budget and under 150 - left ~50 ms of it attributed to nothing
    // (2026-09-30, the Bowie sequence).
    std::map<std::string, double> phSection;
    const auto spanned = [&](const char* name, auto&& fn) {
        const double a = phaseNow();
        fn();
        phSpan[name] += phaseNow() - a;
    };
    double phSum[4] = {0, 0, 0, 0};
    long phN = 0;
    long phGpu = 0;                          // frames presented from the GPU
    std::map<std::string, long> phKept;      // ...and why the others were not
    // THE FRAME LOOP - `PlayFrame::step` in `playframe.cpp` (todo/play-split.md).
    PlayFrame frame{
        .fr = fr,
        .frames = frames,
        .dump = dump,
        .typeText = typeText,
        .dispW = dispW,
        .dispH = dispH,
        .scripted = scripted,
        .keyEvery = keyEvery,
        .snapsDir = snapsDir,
        .flickerDir = flickerDir,
        .waterCamPreset = waterCamPreset,
        .snapEvery = snapEvery,
        .savesPath = savesPath,
        .density = density,
        .fightArg = fightArg,
        .noFightCamRay = noFightCamRay,
        .fightLevelArg = fightLevelArg,
        .boardArg = boardArg,
        .densityFlag = densityFlag,
        .clipFlag = clipFlag,
        .skyFlag = skyFlag,
        .shadowFlag = shadowFlag,
        .cpuBodiesFlag = cpuBodiesFlag,
        .detailFlag = detailFlag,
        .dither = dither,
        .mouseInvertX = mouseInvertX,
        .shootEyeLift = shootEyeLift,
        .shootEyeSet = shootEyeSet,
        .drawFog = drawFog,
        .lightCrowd = lightCrowd,
        .fogRGB = fogRGB,
        .noScriptSprites = noScriptSprites,
        .scxPlay = scxPlay,
        .openSneak = openSneak,
        .startShoot = startShoot,
        .shootHealth = shootHealth,
        .shootEndAt = shootEndAt,
        .standAt = standAt,
        .haveStand = haveStand,
        .eyeA = eyeA,
        .atA = atA,
        .fovA = fovA,
        .callDialog = callDialog,
        .animHoldHarness = animHoldHarness,
        .haveEye = haveEye,
        .haveAt = haveAt,
        .speed = speed,
        .showFps = showFps,
        .boardPress = boardPress,
        .mountSpent = mountSpent,
        .calledOpenTold = calledOpenTold,
        .boarded = boarded,
        .boarding = boarding,
        .doorOff = doorOff,
        .doorOffState = doorOffState,
        .boardCam = boardCam,
        .leaving = leaving,
        .exitOff = exitOff,
        .exitOffState = exitOffState,
        .doorIn = doorIn,
        .doorOut = doorOut,
        .doorClipsRead = doorClipsRead,
        .journeyTo = journeyTo,
        .calledDestination = calledDestination,
        .ride = ride,
        .scxPlayed = scxPlayed,
        .savedAt = savedAt,
        .savedYaw = savedYaw,
        .haveSavedPlacement = haveSavedPlacement,
        .fs = fs,
        .w = w,
        .lay = lay,
        .comp = comp,
        .optMenu = optMenu,
        .view3dPic = view3dPic,
        .cloud = cloud,
        .in = in,
        .state = state,
        .loadedName = loadedName,
        .saveSettings = saveSettings,
        .settings = settings,
        .fxGain = fxGain,
        .dialogueGain = dialogueGain,
        .highScores = highScores,
        .unlimitedClip = unlimitedClip,
        .clipInches = clipInches,
        .clipReport = clipReport,
        .flickRing = flickRing,
        .flickNote = flickNote,
        .flickLit = flickLit,
        .frameNote = frameNote,
        .flickAfter = flickAfter,
        .flickEvent = flickEvent,
        .flickQuietUntil = flickQuietUntil,
        .drawSky = drawSky,
        .drawShadows = drawShadows,
        .threadBodies = threadBodies,
        .shadowQuality = shadowQuality,
        .shadowDetail = shadowDetail,
        .lighting = lighting,
        .radarAlways = radarAlways,
        .frameRate = frameRate,
        .applyTextScale = applyTextScale,
        .forceAdventure = forceAdventure,
        .objectRecords = objectRecords,
        .globalFile = globalFile,
        .inv = inv,
        .destinations = destinations,
        .sliderTold = sliderTold,
        .examineTold = examineTold,
        .examineText = examineText,
        .memoBodyPending = memoBodyPending,
        .session = session,
        .walk = walk,
        .openScreen = openScreen,
        .conversations = conversations,
        .lastArea = lastArea,
        .loadPanelState = loadPanelState,
        .pendingLoadSlot = pendingLoadSlot,
        .quitRequested = quitRequested,
        .exitProgram = exitProgram,
        .videophoneCall = videophoneCall,
        .videophoneSpoke = videophoneSpoke,
        .callHarness = callHarness,
        .callPending = callPending,
        .uiCursor = uiCursor,
        .uiLists = uiLists,
        .uiModels = uiModels,
        .screenFromScript = screenFromScript,
        .screenOpenBits = screenOpenBits,
        .playerScreen = playerScreen,
        .actionSpent = actionSpent,
        .sneakRows = sneakRows,
        .addrSeen = addrSeen,
        .sneakHidden = sneakHidden,
        .sneakTold = sneakTold,
        .replySel = replySel,
        .actionTold = actionTold,
        .prevBits = prevBits,
        .specialMoves = specialMoves,
        .cityMaps = cityMaps,
        .shootWeapons = shootWeapons,
        .sceneVoices = sceneVoices,
        .sfxLog = sfxLog,
        .takeCandidate = takeCandidate,
        .takeWasLow = takeWasLow,
        .heldInHand = heldInHand,
        .lineScroll = lineScroll,
        .lineOverflow = lineOverflow,
        .menuShown = menuShown,
        .lastDlgCam = lastDlgCam,
        .worldRays = worldRays,
        .lastRayCam = lastRayCam,
        .editingShown = editingShown,
        .haveLastDrawn = haveLastDrawn,
        .lastEye = lastEye,
        .lastAt = lastAt,
        .lastFov = lastFov,
        .ctlSprites = ctlSprites,
        .ctlFxState = ctlFxState,
        .ctlFxFrame = ctlFxFrame,
        .foeSprites = foeSprites,
        .foeFxState = foeFxState,
        .foeFxFrame = foeFxFrame,
        .foeSpritesDrawn = foeSpritesDrawn,
        .foeSpritesPooled = foeSpritesPooled,
        .ctlField = ctlField,
        .ctlGeo = ctlGeo,
        .takeCam = takeCam,
        .takeCamPhase = takeCamPhase,
        .takeCamClock = takeCamClock,
        .takeCamFromEye = takeCamFromEye,
        .takeCamFromAt = takeCamFromAt,
        .takeCamFromFov = takeCamFromFov,
        .takeCamEye = takeCamEye,
        .takeCamAt = takeCamAt,
        .takeCamFov = takeCamFov,
        .takeCamTravel = takeCamTravel,
        .takeCamRequest = takeCamRequest,
        .playerCamRequest = playerCamRequest,
        .fallBanded = fallBanded,
        .fallCamRequest = fallCamRequest,
        .lastRoll = lastRoll,
        .musicPaused = musicPaused,
        .holdEditCam = holdEditCam,
        .heldUnderRequests = heldUnderRequests,
        .editFromKnown = editFromKnown,
        .editFromEye = editFromEye,
        .editFromAt = editFromAt,
        .editFromFov = editFromFov,
        .editFromRoll = editFromRoll,
        .rollTold = rollTold,
        .player = player,
        .playerCtl = playerCtl,
        .moveWaitCtx = moveWaitCtx,
        .moveWaitGroup = moveWaitGroup,
        .playerCtlData = playerCtlData,
        .playerMeshes = playerMeshes,
        .playerBoneIdx = playerBoneIdx,
        .playerTex = playerTex,
        .playerRest = playerRest,
        .playerPosed = playerPosed,
        .playerAffine = playerAffine,
        .playerPosedFrame = playerPosedFrame,
        .playerSpheres = playerSpheres,
        .playerReach = playerReach,
        .playerSoup = playerSoup,
        .playerSoupFlags = playerSoupFlags,
        .soupFlagsFor = soupFlagsFor,
        .soupFlagsSize = soupFlagsSize,
        .playerGrid = playerGrid,
        .playerMovingTri = playerMovingTri,
        .playerMovingIds = playerMovingIds,
        .rebuildFixedGrid = rebuildFixedGrid,
        .rebuildMovingGrid = rebuildMovingGrid,
        .mergedValid = mergedValid,
        .playerSteep = playerSteep,
        .playerSteepGrid = playerSteepGrid,
        .steepMovingTri = steepMovingTri,
        .steepMovingIds = steepMovingIds,
        .rebuildSteepFixedGrid = rebuildSteepFixedGrid,
        .rebuildSteepMovingGrid = rebuildSteepMovingGrid,
        .playerModel = playerModel,
        .playerCtlName = playerCtlName,
        .adventure = adventure,
        .playerReady = playerReady,
        .followCam = followCam,
        .playerProgram = playerProgram,
        .playerProgramWas = playerProgramWas,
        .mirrorLive = mirrorLive,
        .mirrorSeen = mirrorSeen,
        .playerHeadAt = playerHeadAt,
        .playerHeadRise = playerHeadRise,
        .playerHeadKnown = playerHeadKnown,
        .playerMeshAt = playerMeshAt,
        .playerMeshRot = playerMeshRot,
        .playerMeshAtKnown = playerMeshAtKnown,
        .placementSeen = placementSeen,
        .heldFrames = heldFrames,
        .mediaText = mediaText,
        .mediaTextFrames = mediaTextFrames,
        .mediaBmp = mediaBmp,
        .playerFeet = playerFeet,
        .playerFeetKnown = playerFeetKnown,
        .playerRootRef = playerRootRef,
        .playerStandLatched = playerStandLatched,
        .playerRootXZ = playerRootXZ,
        .lastRootDrop = lastRootDrop,
        .playerCamId = playerCamId,
        .handoverFacing = handoverFacing,
        .handoverFacingKnown = handoverFacingKnown,
        .playerDrivenSeen = playerDrivenSeen,
        .playerDrivenArea = playerDrivenArea,
        .frameSec = frameSec,
        .gameClock = gameClock,
        .holds = holds,
        .handoverFrame = handoverFrame,
        .sndMove = sndMove,
        .sndConfirm = sndConfirm,
        .sndBack = sndBack,
        .optSndMove = optSndMove,
        .optSndConfirm = optSndConfirm,
        .optSndBack = optSndBack,
        .optWalkSeen = optWalkSeen,
        .optEntered = optEntered,
        .pendingDisplay = pendingDisplay,
        .loadSlot = loadSlot,
        .front = front,
        .vkRen = vkRen,
        .worldVk = worldVk,
        .glWin = glWin,
        .glRen = glRen,
        .glSwapMs = glSwapMs,
        .present = present,
        .blip = blip,
        .adpcmTables = adpcmTables,
        .music = music,
        .playingTrack = playingTrack,
        .voiceLib = voiceLib,
        .voices = voices,
        .voiceOverShot = voiceOverShot,
        .speakerMeshes = speakerMeshes,
        .speakerTracks = speakerTracks,
        .lineIdleFrame = lineIdleFrame,
        .sceneFrameLast = sceneFrameLast,
        .speakerMorph = speakerMorph,
        .speakerModel = speakerModel,
        .speakerVoice = speakerVoice,
        .voiceShot = voiceShot,
        .speakerAt = speakerAt,
        .speakerSolved = speakerSolved,
        .speakerReady = speakerReady,
        .speakerConv = speakerConv,
        .dialogMode = dialogMode,
        .shootMode = shootMode,
        .charModels = charModels,
        .propModels = propModels,
        .propGeo = propGeo,
        .propBatchOwner = propBatchOwner,
        .propsTold = propsTold,
        .staged = staged,
        .pedJobs = pedJobs,
        .pedStaged = pedStaged,
        .pedLodTracks = pedLodTracks,
        .skeletonRootOf = skeletonRootOf,
        .hasSeveralSkeletons = hasSeveralSkeletons,
        .lodChainOf = lodChainOf,
        .lodTracksFor = lodTracksFor,
        .lodRestFor = lodRestFor,
        .vehStaged = vehStaged,
        .vehDrawn = vehDrawn,
        .vehLive = vehLive,
        .vehStopped = vehStopped,
        .vehTold = vehTold,
        .heaviestRootOf = heaviestRootOf,
        .pedTracks = pedTracks,
        .pedCacheGen = pedCacheGen,
        .pedAni = pedAni,
        .pedAniName = pedAniName,
        .pedDrawn = pedDrawn,
        .pedLive = pedLive,
        .pedInAction = pedInAction,
        .pedIdle = pedIdle,
        .pedOffView = pedOffView,
        .pedLit = pedLit,
        .pedTold = pedTold,
        .shootClips = shootClips,
        .shootClipFor = shootClipFor,
        .shootClipOfType = shootClipOfType,
        .shootClipExact = shootClipExact,
        .shootClipBySlot = shootClipBySlot,
        .pedTracksFor = pedTracksFor,
        .shootCameraLive = shootCameraLive,
        .shootPitch = shootPitch,
        .shootBrains = shootBrains,
        .gunShots = gunShots,
        .gunTold = gunTold,
        .gunClips = gunClips,
        .gunAnims = gunAnims,
        .gunCurType = gunCurType,
        .gunCurSlot = gunCurSlot,
        .playerDeathCountdown = playerDeathCountdown,
        .gunStandDown = gunStandDown,
        .gunLooped = gunLooped,
        .gunAims = gunAims,
        .gunAimTold = gunAimTold,
        .gunDrawnTold = gunDrawnTold,
        .gunBarrelTold = gunBarrelTold,
        .hudHealth = hudHealth,
        .gunRandSeed = gunRandSeed,
        .playerShootRec = playerShootRec,
        .shotLatch = shotLatch,
        .shootAim = shootAim,
        .shootMover = shootMover,
        .hudWalk = hudWalk,
        .hudBar = hudBar,
        .radar = radar,
        .shootMap = shootMap,
        .shootField = shootField,
        .gunCellSeeded = gunCellSeeded,
        .gunEntryPending = gunEntryPending,
        .shootNoise = shootNoise,
        .hudRows = hudRows,
        .hudAmmo = hudAmmo,
        .hudTold = hudTold,
        .shootMoveFrames = shootMoveFrames,
        .shootMoveDist = shootMoveDist,
        .shootMoveFrom = shootMoveFrom,
        .mouseSensX = mouseSensX,
        .mouseSensY = mouseSensY,
        .mouseInverted = mouseInverted,
        .shootPitchDirty = shootPitchDirty,
        .playerPoseNow = playerPoseNow,
        .gunmanPoseNow = gunmanPoseNow,
        .projectiles = projectiles,
        .shotsFired = shotsFired,
        .shootSfx = shootSfx,
        .shootRt = shootRt,
        .sfxCache = sfxCache,
        .sfxSceneWas = sfxSceneWas,
        .sfxPcm = sfxPcm,
        .shotSound = shotSound,
        .shotGunStem = shotGunStem,
        .gunFactsFor = gunFactsFor,
        .stagedEver = stagedEver,
        .stagedIds = stagedIds,
        .poolComposition = poolComposition,
        .poolBuiltFor = poolBuiltFor,
        .poolTold = poolTold,
        .poolHasSprites = poolHasSprites,
        .poolHasPlayer = poolHasPlayer,
        .playerTexBase = playerTexBase,
        .spriteTexBase = spriteTexBase,
        .spriteSlot = spriteSlot,
        .spriteWanted = spriteWanted,
        .spritePooled = spritePooled,
        .poolOverflowTold = poolOverflowTold,
        .stagedProbe = stagedProbe,
        .obstructProbe = obstructProbe,
        .progYawSign = progYawSign,
        .charModelFor = charModelFor,
        .propModelFor = propModelFor,
        .charBankFor = charBankFor,
        .fightRun = fightRun,
        .fightCamTold = fightCamTold,
        .beginMelee = beginMelee,
        .idleTracksFor = idleTracksFor,
        .pool = pool,
        .poolSize = poolSize,
        .spriteTab = spriteTab,
        .spriteLookup = spriteLookup,
        .globalRt = globalRt,
        .fightRt = fightRt,
        .refreshSprites = refreshSprites,
        .worldSw = worldSw,
        .world = world,
        .worldReady = worldReady,
        .worldSlots = worldSlots,
        .worldSet = worldSet,
        .worldTexBase = worldTexBase,
        .worldGen = worldGen,
        .sky = sky,
        .shadowModel = shadowModel,
        .shadowGeo = shadowGeo,
        .shadowTexBase = shadowTexBase,
        .pedFootOffMax = pedFootOffMax,
        .shadowFootOffMax = shadowFootOffMax,
        .shadowSpreadMax = shadowSpreadMax,
        .shadowSpreadPlayer = shadowSpreadPlayer,
        .shadowBlobsDrawn = shadowBlobsDrawn,
        .lightsTold = lightsTold,
        .shadowLightTold = shadowLightTold,
        .shadowTold = shadowTold,
        .shimmerClock = shimmerClock,
        .worldDecors = worldDecors,
        .worldGeoRev = worldGeoRev,
        .worldTex = worldTex,
        .worldFrames = worldFrames,
        .fxGeo = fxGeo,
        .actorAt = actorAt,
        .actorKnown = actorKnown,
        .lastCamera = lastCamera,
        .setLoads = setLoads,
        .slotAsked = slotAsked,
        .slotAskedArea = slotAskedArea,
        .syncSets = syncSets,
        .loadGate = loadGate,
        .askSet = askSet,
        .integrateSet = integrateSet,
        .rebuildWorld = rebuildWorld,
        .host = host,
        .fb = fb,
        .n = n,
        .lastMs = lastMs,
        .uiClockMs = uiClockMs,
        .fpsLastMs = fpsLastMs,
        .fpsWorst = fpsWorst,
        .fpsSince = fpsSince,
        .fpsFrames = fpsFrames,
        .spriteAnchor = spriteAnchor,
        .spriteAnchorSet = spriteAnchorSet,
        .spriteLogged = spriteLogged,
        .applyPlayerDamage = applyPlayerDamage,
        .motionLogged = motionLogged,
        .paceNext = paceNext,
        .phaseNow = phaseNow,
        .phTop = phTop,
        .phRb0 = phRb0,
        .phRb1 = phRb1,
        .phMarks = phMarks,
        .mark = mark,
        .phSpan = phSpan,
        .phSection = phSection,
        .spanned = spanned,
        .phSum = phSum,
        .phN = phN,
        .phGpu = phGpu,
        .phKept = phKept,
    };
    for (;;) {
        const int stepped = frame.step();
        if (stepped == -2) break;
        if (stepped >= 0) return stepped;
    }
    std::printf("%ld frames presented\n", n);
#if defined(OMK_GLES)
    // the buffers of geometries that are gone (todo/optimization.md step 26)
    if (glRen) {
        long gs[3];
        omk::glesGeometryStats(glRen, gs);
        std::printf("gles: %ld geometries released over the run, %ld vertex buffers and %ld "
                    "posed buffers held at the end\n", gs[0], gs[1], gs[2]);
    }
#endif
    if (!dump.empty()) {
        // The framebuffer the WINDOW was shown, as raw LE RGB565 - so the
        // live half of PORTING A1's pair can be diffed against the reference
        // half, which is the property rule 3 is about: the frontend uploads
        // the pixels and must not have touched them.
        std::ofstream o(dump, std::ios::binary);
        for (auto v : fb.px) {
            const char b2[2] = {static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)};
            o.write(b2, 2);
        }
        // SAY THE SIZE. This wrote the bytes and no dimensions, and a reader
        // of the file has nothing to go on but its length - 960000 bytes is
        // 800x600, not the 640x480 that a `.bin` from this viewer is assumed
        // to be, and one shot was decoded at the wrong stride and read as a
        // broken renderer.
        std::printf("wrote %s (%dx%d RGB565, %zu bytes)\n", dump.c_str(),
                    fb.w, fb.h, fb.px.size() * 2);
    }
    // ------------------------------------------------- WRITING A SAVE
    //
    // `Game_WriteSave` (0x00408EF0), with `State_Save`'s snapshot half in
    // front of it (GAME_STATE 5a, 8b).  The order is the engine's: take the
    // snapshot the block does not otherwise keep, then the four copies into
    // the slot, then the whole file back.
    //
    // WHERE the scene comes from matters.  `State_Save` reads the LIVE
    // resident slot (`dword_69BC4C[4 * dword_69BC60]`), not the scene-per-area
    // table - and `State_Apply` copies the header back INTO that table before
    // `Area_Load` reads it, so the header is what a load believes.  Taking it
    // from the table here would make the port unable to express a divergence
    // the engine can.
    if (saveSlotArg >= 0) {
        state.setCurrentArea(static_cast<std::int16_t>(session.activeArea()));
        state.setCurrentScene(static_cast<std::int16_t>(
            session.residentSlot(session.activeSlot()).scene));
        state.setPlacement(session.playerPos(), session.playerYaw());

        omk::SaveSlot out;
        out.name = !saveNameArg.empty() ? saveNameArg
                 : (!loadedName.empty() ? loadedName : std::string("omk-play"));
        out.day  = state.clockDay();
        out.time = state.clock();
        out.state = state;

        // The picture the load panel draws beside the selected row: the back
        // buffer scaled into a 128 x 96 rect and repacked to X1R5G5B5
        // (GAME_STATE 8b).  `fb` is what the window was shown, so this is the
        // frame the player was looking at - which is what the engine's blit
        // takes too.
        const auto thumb = omk::thumbFromRgb565(fb.px, fb.w, fb.h);

        auto file = omk::readSaveFile(savesPath, fr + "/IAM/GAMES");
        if (file.size() < omk::kSaveFileSize) {
            // `sub_4092A0`'s create arm - the settings, then 256 empty slots.
            file = omk::blankSaveFile(saveSettings ? *saveSettings
                                                   : omk::defaultSettingsBlock());
            std::printf("save: %s did not exist - created, %zu bytes\n",
                        savesPath.c_str(), file.size());
        }
        // ...and `Game_WriteSave`'s first copy: the settings over the head, on
        // EVERY slot save.  Saving a game saves the options (GAME_STATE 8a).
        if (saveSettings) omk::putSettings(file, *saveSettings);
        if (!omk::writeSaveSlot(file, saveSlotArg, out, thumb))
            std::fprintf(stderr, "save: slot %d is out of range\n", saveSlotArg);
        else if (omk::writeSaveFile(savesPath, file))
            std::printf("save: slot %d written to %s - '%s', %s %s, area %d "
                        "scene %d, standing at %.0f %.0f %.0f facing %.0f, "
                        "with a %dx%d picture\n",
                        saveSlotArg, savesPath.c_str(), out.name.c_str(),
                        omk::formatDate(out.day).c_str(),
                        omk::formatTime(out.time).c_str(),
                        state.currentArea(), state.currentScene(),
                        session.playerPos()[0], session.playerPos()[1],
                        session.playerPos()[2], session.playerYaw(),
                        omk::kThumbW, omk::kThumbH);
    }
    {
        const auto& ps = session.scene().effects().particles();
        float lo[3] = {1e9f,1e9f,1e9f}, hi[3] = {-1e9f,-1e9f,-1e9f}, sc = 0;
        for (const auto& p : ps) {
            for (int k = 0; k < 3; ++k) {
                if (p.pos[k] < lo[k]) lo[k] = p.pos[k];
                if (p.pos[k] > hi[k]) hi[k] = p.pos[k];
            }
            if (p.scale > sc) sc = p.scale;
        }
        if (!ps.empty())
            std::printf("particles: sprite %d, scale up to %.2f, box "
                        "%.0f %.0f %.0f .. %.0f %.0f %.0f\n",
                        ps.front().sprite, sc, lo[0], lo[1], lo[2],
                        hi[0], hi[1], hi[2]);
    }
    if (session.dialogOpen()) {
        const auto& dlg = session.dialogue();
        std::printf("dialogue: line '%s' at %.2f of %.2f s - frame %d of %d, blend %g\n",
                    dlg.voice().c_str(), dlg.elapsed(), dlg.lineSeconds(),
                    static_cast<int>(dlg.elapsed() * 30.0), speakerTracks.frames,
                    omk::morphBlendFrames(speakerTracks.frames));
    }
    std::printf("effects: %d set pieces shown so far, %d shown now, "
                "%d emitters registered on the last frame, %zu particles alive\n",
                session.scene().piecesFired(), session.scene().pieces().shownCount(),
                session.scene().pieces().registered(),
                session.scene().effects().count());
    std::printf("effects: the melee opponent's .CTL sprites placed on his bones %ld times, "
                "%ld with a texture slot to draw with\n", foeSpritesDrawn, foeSpritesPooled);
    std::printf("world: %ld frames drawn, last set %s (%d shown), last camera %d, "
                "%ld frames under player.anim.hold\n",
                worldFrames, worldSet.empty() ? "(none)" : worldSet.c_str(),
                session.shownCount(), session.cameraId(), heldFrames);
    if (player)
        std::printf("player: ends at %.1f %.1f %.1f facing %.0f, channel entry %d group %d\n",
                    player->pos()[0], player->pos()[1], player->pos()[2],
                    player->facing(), player->ctlState(), player->ctlGroup());
    {
        std::string ids;
        for (std::size_t k = 0; k < stagedIds.size(); ++k)
            ids += (k ? ", " : "") + std::to_string(stagedIds[k]);
        std::printf("staged %ld characters (ids %s), %zu on screen at the end, "
                    "%zu models and %zu banks resident\n",
                    stagedEver, ids.empty() ? "none" : ids.c_str(),
                    staged.size(), charModels.size(), charBanks.size());
        for (const auto& up : staged)
            std::printf("  actor %d %s (bank %s) at %.0f %.0f %.0f facing %.0f - %s%s\n",
                        up->actor, up->model.c_str(),
                        up->bank.empty() ? "none" : up->bank.c_str(),
                        up->drawAt[0], up->drawAt[1], up->drawAt[2], up->facing,
                        up->src, up->drawn ? "" : "  [not drawn]");
    }
    if (player)
        std::printf("player: %s/%s at %.1f %.1f %.1f facing %.1f, ACTOR_STATE %d, "
                    ".CTL state %d '%s' clip %s frame %.1f, walked %.1f over %ld ticks, "
                    "pose tracks %s\n",
                    playerModel.c_str(), playerCtlName.c_str(), player->pos()[0],
                    player->pos()[1], player->pos()[2], player->facing(),
                    static_cast<int>(player->state()), player->ctlState(),
                    player->ctlStateName().c_str(), player->clipName().c_str(),
                    player->clipFrame(), player->distanceWalked(), player->ticks(),
                    player->poseTracks() ? "valid" : "NONE (drawn at rest - a T-pose)");
    {
        std::int32_t vie = -1;
        omk::readActorProperty(playerRecordSpan(), 1, vie);
        std::printf("player: Vie %d (the DB player record's property 1); %ld run-overs posted\n",
                    vie, session.runOvers());
    }
    std::printf("session: %d areas entered, %d ui answers, %d zones turned away by "
                "the height band\n",
                session.areasEntered(),
                static_cast<int>(session.uiAnswers().size()),
                session.zones().heightSkips());
    front.close();
    return 0;
}
