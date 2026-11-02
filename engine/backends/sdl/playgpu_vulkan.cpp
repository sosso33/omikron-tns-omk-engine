// SPDX-License-Identifier: GPL-3.0-or-later
// THE GPU WINDOW, VULKAN - `PlayState::gpu...` for the Vulkan variant
// (`todo/play-split.md` S5). Each body is the `#if defined(OMK_VULKAN)` arm it
// replaces, moved unchanged; the game code calls these, and each build links
// exactly one of `playgpu_vulkan.cpp`, `playgpu_gles.cpp`, `playgpu_none.cpp`.
#include "playframe.h"
#include "playgpu_vulkan.h"
#include "platform/env.h"


// The Vulkan window - this glue's, not the game's (the gateway: game code
// holds no host handle). One viewer a process, so one window.
static SDL_Window* vkWin = nullptr;

bool PlayState::gpuWindowBuild() const { return true; }

// Vulkan when the machine has it (see THE RENDERER in `playsetup_devices.cpp`)
void PlayState::gpuOpenWindow() {
    if (!forceSoftware && !worldVulkan) {
        vkWin = vkSdlWindow("OMK Engine (vulkan)", dispW, dispH,
                            // hidden for a profile of the direct present
                            // (`OMK_HIDDEN_WINDOW`, playgpu_gles.cpp)
                            omk::envFlag("OMK_HIDDEN_WINDOW") ? SDL_WINDOW_HIDDEN
                                                              : omk::sdlFrontend(front).windowFlags());
        if (vkWin) {
            const std::vector<const char*> ext = vkSdlExtensions(vkWin);
            const auto nx = static_cast<unsigned>(ext.size());
            omk::Renderer* vkr = omk::makeVulkanRenderer();
            if (aaSamples > 1) vkr->setMultisample(aaSamples);   // the enhancements
            if (texFilter > 0) vkr->setTextureFilter(texFilter);
            if (texAniso > 1) vkr->setAnisotropy(texAniso);
            if (ssaa > 1) vkr->setSupersample(ssaa);
            remasterConfigure(*vkr);   // the remaster's (`backends/remaster/`), as GLES's
            if (aoStrength > 0.0f) vkr->setAmbientOcclusion(aoStrength);   // ...and the AO, Vulkan's alone
            omk::vulkanNeedExtensions(vkr, ext.data(), nx);
            void* inst = omk::vulkanCreateInstance(vkr);
            VkSurfaceKHR surf{};
            if (inst &&
                vkSdlSurface(vkWin, static_cast<VkInstance>(inst), &surf) &&
                omk::vulkanAttachSurface(vkr, reinterpret_cast<unsigned long long>(surf)) &&
                vkr->init(dispW, dispH)) {
                vkRen = vkr;
                // the menus' cloud stretched on the card (`under.frag`)
                cloudUnderGpu = !omk::envFlag("OMK_NO_GPU_CLOUD");
                omk::sdlFrontend(front).attachWindow(vkWin);   // F11 and row 2 act on it
                std::printf("renderer: VULKAN - %s\n", omk::vulkanDeviceName(vkr));
            } else {
                delete vkr;
                SDL_DestroyWindow(vkWin); vkWin = nullptr;
                std::printf("renderer: no Vulkan device - the software "
                            "reference\n");
            }
        }
    }
}

void PlayState::gpuOpenWorldHarness() {
    // ...and the harness: a Vulkan renderer with no surface, for the WORLD
    // alone. `run_vulkan` and `shadow_probe` already prove the backend comes
    // up offscreen; this is the same thing inside the viewer.
    if (!vkRen && worldVulkan) {
        omk::Renderer* wv = omk::makeVulkanRenderer();
        if (wv && aaSamples > 1) wv->setMultisample(aaSamples);
        if (wv && texFilter > 0) wv->setTextureFilter(texFilter);
        if (wv && texAniso > 1) wv->setAnisotropy(texAniso);
        if (wv && ssaa > 1) wv->setSupersample(ssaa);
        if (wv) remasterConfigure(*wv);
        if (wv && aoStrength > 0.0f) wv->setAmbientOcclusion(aoStrength);
        if (wv && wv->init(dispW, dispH)) {
            worldVk = wv;
            std::printf("renderer: the world through VULKAN offscreen - %s "
                        "(a harness; the frame is still presented on the CPU)\n",
                        omk::vulkanDeviceName(wv));
        } else { delete wv; std::printf("--world-vulkan: no offscreen device\n"); }
    }
}

bool PlayState::gpuPresentSurface(const omk::Surface& pic) {
    if (vkRen) { omk::vulkanPresentSurface(vkRen, pic); return true; }
    return false;
}

void PlayState::gpuPresentVerify(bool&) {}
// THE CLOUD UNDER THE INTERFACE: the frame's key pixels are the cloud's, the
// planes hold what blended over them (`screensInterfaceLayer`), and the card
// stretches the 640x480 cloud under both - `sub_4B19C0`'s surface blitted out,
// as DirectDraw did it. A present the backend refuses (the window resizing)
// is composed here instead, NEAREST, and goes the CPU's way.
void PlayState::gpuPresentOverlay(bool& presentedWorld) {
    if (!cloudGpuFrame || !vkRen) return;
    static std::vector<std::uint8_t> mask, maskRow;
    static std::vector<std::uint64_t> maskBlocks;
    omk::resolveOverlayPlanes(fb.px.data(), fb.w, fb.h, g_ov, mask, maskRow, maskBlocks);
    const int filter = comp_ ? comp_->scaling() : 0;
    // OMK_VERIFY_GPU_CLOUD: the presented frame read back and laid against
    // the CPU's own composition of it - the cloud stretched by `blt` with the
    // same filter, the planes' law `C + cloud * M` per 8-bit channel, then 565
    static const bool verify = omk::envFlag("OMK_VERIFY_GPU_CLOUD");
    std::vector<unsigned char> got;
    if (omk::vulkanPresentUnder(vkRen, fb, mask.data(), maskRow.data(), cloudPic, filter,
                                verify ? &got : nullptr)) {
        presentedWorld = true;
        if (verify && got.size() == fb.px.size() * 4) {
            omk::Surface ref(fb.w, fb.h, 0);
            omk::blt(ref, {0, 0, fb.w, fb.h}, cloudPic, {0, 0, cloudPic.w, cloudPic.h}, 0, 0, 0, filter);
            long differ = 0, keyed = 0, blended = 0, differKey = 0;
            int worst = 0;
            for (std::size_t i = 0; i < fb.px.size(); ++i) {
                const std::uint16_t v = fb.px[i];
                const std::size_t y = i / static_cast<std::size_t>(fb.w);
                const int m = v == omk::kOverlayKey ? 255 : (maskRow[y] ? mask[i] : 0);
                keyed += v == omk::kOverlayKey;
                blended += (m && v != omk::kOverlayKey);
                std::uint16_t want = v;
                if (m) {
                    const std::uint16_t c = v == omk::kOverlayKey ? 0 : v;
                    const unsigned char* cc = omk::expand565Rgba() + 4 * static_cast<std::size_t>(c);
                    const unsigned char* uu = omk::expand565Rgba() + 4 * static_cast<std::size_t>(ref.px[i]);
                    int o[3];
                    for (int k = 0; k < 3; ++k)
                        o[k] = std::min(255, static_cast<int>(std::lround(cc[k] + uu[k] * m / 255.0)));
                    want = omk::quantise888(o[0], o[1], o[2]);
                }
                const unsigned char* g = &got[4 * i];
                const std::uint16_t have = omk::quantise888(g[0], g[1], g[2]);
                if (have != want) {
                    ++differ;
                    differKey += v == omk::kOverlayKey;
                    const int d = std::max({std::abs((have >> 11) - (want >> 11)),
                                            std::abs(((have >> 5) & 63) - ((want >> 5) & 63)),
                                            std::abs((have & 31) - (want & 31))});
                    worst = std::max(worst, d);
                }
            }
            std::printf("gpu cloud verify: frame %ld, %dx%d %s, %ld key pixels, %ld blended, "
                        "%ld differ, %ld of them key pixels (largest step %d)\n", frameNo, fb.w, fb.h,
                        filter ? "linear" : "nearest", keyed, blended, differ, differKey, worst);
        }
        return;
    }
    if (!cloudPic.valid()) return;
    for (int y = 0; y < fb.h; ++y)
        for (int x = 0; x < fb.w; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * fb.w + x;
            const std::uint16_t v = fb.px[i];
            const int m = v == omk::kOverlayKey ? 255 : (maskRow[static_cast<std::size_t>(y)] ? mask[i] : 0);
            if (!m) continue;
            const std::uint16_t c = v == omk::kOverlayKey ? 0 : v;
            const std::uint16_t u = cloudPic.at(x * cloudPic.w / fb.w, y * cloudPic.h / fb.h);
            const auto ch = [&](int a, int b, int bits) {   // C + cloud * M, per 565 channel
                const int k = (1 << bits) - 1;
                const int s = a + (b * m + 127) / 255;
                return s > k ? k : s;
            };
            fb.px[i] = static_cast<std::uint16_t>((ch(c >> 11, u >> 11, 5) << 11) |
                                                  (ch((c >> 5) & 63, (u >> 5) & 63, 6) << 5) |
                                                  ch(c & 31, u & 31, 5));
        }
}

void PlayState::gpuPresentWorld(bool& presentedWorld) {
    if (vkRen) presentedWorld = omk::vulkanPresentWorld(vkRen, gpuVy, gpuVh);
}

bool PlayState::gpuWorldOnWindow() {
    omk::Renderer& world = *world_;
    bool onVulkan = false;
    onVulkan = (vkRen && &world == vkRen) ||
               (verifyGpuPresent && worldVk && &world == worldVk);
    return onVulkan;
}

void PlayState::gpuOverlayDecision(const char*&) {}

void PlayState::gpuResize(int nw, int nh, bool& ok) {
    if (vkRen) ok = omk::vulkanResize(vkRen, nw, nh);
    if (ok && worldVk) ok = omk::vulkanResize(worldVk, nw, nh);
}

bool PlayState::gpuDriverRow(std::vector<std::string>& drivers) {
    if (vkRen) { drivers.push_back(std::string("Vulkan - ") + omk::vulkanDeviceName(vkRen)); return true; }
    return false;
}
bool PlayState::gpuRelinkLighting(bool, bool) { return false; }   // Vulkan builds both always

// OMK's options menu: the supersample, the upscale, the ambient occlusion -
// the target remade on both Vulkan renderers there are (`retarget`); not MSAA
bool PlayState::gpuRetarget(int samples, int ss, float up, float ao) {
    bool ok = vkRen || worldVk;
    if (vkRen) ok = omk::vulkanRetarget(vkRen, samples, ss, up, ao) && ok;
    if (worldVk) ok = omk::vulkanRetarget(worldVk, samples, ss, up, ao) && ok;
    return ok;
}

void PlayState::gpuReportTimings() {}
void PlayState::gpuSlowFrameReport() {}
void PlayState::gpuFinishReport() {}

void PlayState::gpuVerifyWorldPicture() {
    omk::Renderer& world = *world_;
    if (gpuFrame && verifyGpuPresent) {
        // the GPU's picture against the CPU frame, as the upload would
        // expand it - every byte of every pixel
        static std::vector<unsigned char> gpuPic;
        static long compared = 0, differing = 0, badPixels = 0;
        if (omk::vulkanWorldPicture(&world, gpuVy, gpuVh, gpuPic) &&
            gpuPic.size() == fb.px.size() * 4) {
            const unsigned char* lut = omk::expand565Rgba();
            long diff = 0;
            for (std::size_t i = 0; i < fb.px.size(); ++i)
                if (std::memcmp(lut + 4 * static_cast<std::size_t>(fb.px[i]), &gpuPic[4 * i], 4) != 0) ++diff;
            ++compared;
            if (diff) {
                ++differing; badPixels += diff;
                // at once, so a run with only a few GPU frames still says so
                std::printf("gpu present verify: frame %ld DIFFERS in %ld pixels\n", frameNo, diff);
            }
        }
        if (compared > 0 && compared % 30 == 0)
            std::printf("gpu present verify: frame %ld, %ld frames compared, %ld differ (%ld pixels)\n",
                        frameNo, compared, differing, badPixels);
    }
}
