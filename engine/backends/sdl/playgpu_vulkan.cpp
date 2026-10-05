// SPDX-License-Identifier: GPL-3.0-or-later
// THE GPU WINDOW, VULKAN - `PlayState::gpu...` for the Vulkan variant
// (`todo/play-split.md` S5). Each body is the `#if defined(OMK_VULKAN)` arm it
// replaces, moved unchanged; the game code calls these, and each build links
// exactly one of `playgpu_vulkan.cpp`, `playgpu_gles.cpp`, `playgpu_none.cpp`.
#include "playframe.h"
#include "playgpu_vulkan.h"

// The Vulkan window - this glue's, not the game's (the gateway: game code
// holds no host handle). One viewer a process, so one window.
static SDL_Window* vkWin = nullptr;

bool PlayState::gpuWindowBuild() const { return true; }

// Vulkan when the machine has it (see THE RENDERER in `playsetup_devices.cpp`)
void PlayState::gpuOpenWindow() {
    if (!forceSoftware && !worldVulkan) {
        if (SDL_Init(SDL_INIT_VIDEO) == 0) {
            vkWin = SDL_CreateWindow("OMK Engine (vulkan)",
                                     SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     dispW, dispH, SDL_WINDOW_VULKAN |
                                     // hidden for a profile of the direct present
                                     // (`OMK_HIDDEN_WINDOW`, playgpu_gles.cpp)
                                     (omk::envSet("OMK_HIDDEN_WINDOW") ? SDL_WINDOW_HIDDEN
                                                                       : omk::sdlFrontend(front).windowFlags()));
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
                omk::sdlFrontend(front).attachWindow(vkWin);   // F11 and row 2 act on it
                std::printf("renderer: VULKAN - %s\n", omk::vulkanDeviceName(vr));
            } else {
                delete vr;
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
void PlayState::gpuPresentOverlay(bool&) {}

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
                std::printf("gpu present verify: frame %ld DIFFERS in %ld pixels\n", n, diff);
            }
        }
        if (compared > 0 && compared % 30 == 0)
            std::printf("gpu present verify: frame %ld, %ld frames compared, %ld differ (%ld pixels)\n",
                        n, compared, differing, badPixels);
    }
}
