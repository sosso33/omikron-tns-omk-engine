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

namespace omk {
Renderer* makeDekoRenderer();
bool dekoPresentWorld(Renderer*, int vy, int vh);
bool dekoPresentSurface(Renderer*, const Surface&);
const char* dekoDeviceName(Renderer*);
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
        std::printf("renderer: DEKO3D - %s\n", omk::dekoDeviceName(r));
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
void PlayState::gpuReportTimings() {}
void PlayState::gpuSlowFrameReport() {}
void PlayState::gpuFinishReport() {}
void PlayState::gpuVerifyWorldPicture() {}
