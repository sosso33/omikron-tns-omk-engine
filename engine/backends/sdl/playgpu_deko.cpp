// SPDX-License-Identifier: GPL-3.0-or-later
// THE GPU WINDOW, DEKO3D - `PlayState::gpu...` for the Switch's deko3d build
// (`todo/deko3d.md`, `make switch-deko`). Vulkan's glue in shape: the renderer
// sits where Vulkan's does (`vkRen`), the world is presented straight from
// the GPU when nothing is drawn over it, and a frame the interface covers is
// read back, composed on the CPU and presented as a surface.
//
// **NO SDL WINDOW.** devkitPro's SDL2 makes every window an EGL surface on
// libnx's default `NWindow` - the one deko3d's swapchain must own
// (`todo/deko3d.md` "SDL without a window", read from `libSDL2.a`'s object
// code). So SDL's video is started only for its input and its
// `appletMainLoop`, and the renderer takes the window itself.
#include "playframe.h"
#include "sdlfront.h"
#include "platform/env.h"

#include <chrono>

namespace omk {
Renderer* makeDekoRenderer();
bool dekoPresentWorld(Renderer*, int vy, int vh);
bool dekoPresentSurface(Renderer*, const Surface&);
const char* dekoDeviceName(Renderer*);
void dekoSetSwapInterval(Renderer*, int n);
void dekoTakeCounts(Renderer*, long out[5]);
void dekoTakeCpu(Renderer*, double out[5]);
std::string dekoTakeLadder(Renderer*);
}

// THE DEVICE CLOSED AT THE PROGRAM'S END. hbloader loads the next program -
// hbmenu, the netloader's next NRO, a sweep's next run - into this SAME
// process, and a deko3d device never destroyed keeps nvservices' transfer
// memory: its pages, inside the heap the next program is given, stay
// locked to it. The reader's console (2026-11-27): the launch after a
// `--bench` run that returned crashed in `dkDeviceCreate` -> `nvInitialize`
// -> `tmemCreate`'s memset (a data abort at 0x752f3f7000, in the heap).
// GLES needs none of this - `SDL_Quit` closes its EGL. The frontend never
// deletes its renderer, so the end of the program does: a static owner,
// destroyed after `main` returns.
namespace {
struct DekoOwner {
    omk::Renderer* r = nullptr;
    ~DekoOwner() {
        if (!r) return;
        delete r;   // `dkDeviceDestroy` -> `nvExit`: the transfer memory back
        r = nullptr;
    }
};
DekoOwner g_deko;
}

bool PlayState::gpuWindowBuild() const { return true; }

void PlayState::gpuOpenWindow() {
    if (forceSoftware) return;
    // the pads, the applet's events (HOME, sleep, the dock) - no window
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
        std::printf("deko3d: SDL video (input only) did not start: %s\n", SDL_GetError());
    omk::Renderer* r = omk::makeDekoRenderer();
    if (texFilter > 0) r->setTextureFilter(texFilter);
    if (r->init(dispW, dispH)) {
        vkRen = r;
        g_deko.r = r;
        std::printf("renderer: DEKO3D - %s\n", omk::dekoDeviceName(r));
        // v-sync off under the performance sweep, as the GLES window's rule
        // (`playgpu_gles.cpp`): a frame's work then ends when the GPU takes it
        if (!opt.benchPath.empty()) {
            omk::dekoSetSwapInterval(r, 0);
            std::printf("deko3d: v-sync off - the performance sweep\n");
        }
    } else {
        delete r;
        std::printf("renderer: deko3d did not come up - the software reference\n");
    }
}

void PlayState::gpuOpenWorldHarness() {}

bool PlayState::gpuPresentSurface(const omk::Surface& pic) {
    return vkRen && omk::dekoPresentSurface(vkRen, pic);
}

void PlayState::gpuPresentVerify(bool&) {}
void PlayState::gpuPresentOverlay(bool&) {}   // the overlay planes: D2

void PlayState::gpuPresentWorld(bool& presentedWorld) {
    if (vkRen && !presentedWorld) presentedWorld = omk::dekoPresentWorld(vkRen, gpuVy, gpuVh);
}

bool PlayState::gpuWorldOnWindow() {
    return vkRen && world_ == vkRen;
}

void PlayState::gpuOverlayDecision(const char*&) {}
void PlayState::gpuResize(int, int, bool&) {}   // the dock's two sizes: D3

bool PlayState::gpuDriverRow(std::vector<std::string>& drivers) {
    if (vkRen) { drivers.push_back(std::string("deko3d - ") + omk::dekoDeviceName(vkRen)); return true; }
    return false;
}
bool PlayState::gpuRelinkLighting(bool, bool) { return false; }
bool PlayState::gpuRetarget(int, int, float, float) { return false; }   // no MSAA / supersample / upscale yet
void PlayState::gpuReportTimings() {
    if (!vkRen) return;
    long c[5];
    omk::dekoTakeCounts(vkRen, c);
    if (c[3] > 0)
        std::printf("frame %ld deko3d (a frame, mean of %ld): %.0f draws (%.0f by the original's module), "
                    "%.0f runs of the cull, %.0f vertices; GPU %.2f ms\n", frameNo, c[3],
                    static_cast<double>(c[0]) / c[3], static_cast<double>(c[4]) / c[3],
                    static_cast<double>(c[1]) / c[3], static_cast<double>(c[2]) / c[3], vkRen->gpuFrameMs());
    double cpu[5];
    omk::dekoTakeCpu(vkRen, cpu);
    if (c[3] > 0)
        std::printf("frame %ld deko3d CPU (a frame, mean of %ld): the slot's fence waited %.2f ms; submit %.2f ms, "
                    "of it %.1f vertex uploads (%.0f KB) %.2f ms\n", frameNo, c[3], cpu[4] / c[3], cpu[3] / c[3],
                    cpu[0] / c[3], cpu[1] / 1024.0 / c[3], cpu[2] / c[3]);
    const std::string ladder = omk::dekoTakeLadder(vkRen);
    if (!ladder.empty()) std::printf("frame %ld deko3d ladder (GPU ms, mean): %s\n", frameNo, ladder.c_str());
}
void PlayState::gpuSlowFrameReport() {}
// ...and closed HERE, at the run's end, while the output still reaches the
// Mac: the static owner alone was not enough (the relaunch after a returning
// `--bench` run crashed the same way), and its destructor ran past the point
// where anything it said could be read. Nothing draws after `finish` asks
// for this report (`playsetup_finish.cpp`); the static owner stays for the
// run that ends another way.
void PlayState::gpuFinishReport() {
    if (!g_deko.r) return;
    const auto t0 = std::chrono::steady_clock::now();
    delete g_deko.r;
    if (world_ == vkRen) world_ = nullptr;
    vkRen = nullptr;
    g_deko.r = nullptr;
    std::printf("deko3d: the device closed at the run's end (%.1f ms) - nvservices' memory given back "
                "before the next program loads into this process\n",
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
}
void PlayState::gpuVerifyWorldPicture() {}
