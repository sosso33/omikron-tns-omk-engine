// SPDX-License-Identifier: GPL-3.0-or-later
// THE VULKAN BACKEND'S ENTRY POINTS and SDL's Vulkan header, for the files
// that talk to it - `playgpu_vulkan.cpp`, built only into the Vulkan
// variant, and the set viewer's own window (`playscene.cpp`).
#pragma once

#include "playshared.h"
#include "sdlfront.h"

#if defined(OMK_SDL3)
#  include <SDL3/SDL_vulkan.h>
#else
#  include <SDL_vulkan.h>
#endif

#include <vector>

// A VULKAN WINDOW, ITS INSTANCE EXTENSIONS AND ITS SURFACE - the three calls
// SDL2 and SDL3 spell differently (SDL3: no position arguments, the
// extension list SDL's own, an allocator before the surface, `SDL_Init`
// true on success), for the viewer's window and the set viewer's
inline SDL_Window* vkSdlWindow(const char* title, int w, int h, std::uint64_t flags) {
#if defined(OMK_SDL3)
    if (!SDL_Init(SDL_INIT_VIDEO)) return nullptr;
    SDL_Window* win = SDL_CreateWindow(title, w, h, static_cast<SDL_WindowFlags>(flags) | SDL_WINDOW_VULKAN);
    if (win) SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    return win;
#else
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return nullptr;
    return SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
                            static_cast<Uint32>(flags) | SDL_WINDOW_VULKAN);
#endif
}
inline std::vector<const char*> vkSdlExtensions(SDL_Window* win) {
#if defined(OMK_SDL3)
    (void)win;
    Uint32 n = 0;
    const char* const* names = SDL_Vulkan_GetInstanceExtensions(&n);
    return names ? std::vector<const char*>(names, names + n) : std::vector<const char*>{};
#else
    unsigned n = 0;
    SDL_Vulkan_GetInstanceExtensions(win, &n, nullptr);
    std::vector<const char*> ext(n);
    SDL_Vulkan_GetInstanceExtensions(win, &n, ext.data());
    return ext;
#endif
}
inline bool vkSdlSurface(SDL_Window* win, VkInstance inst, VkSurfaceKHR* surf) {
#if defined(OMK_SDL3)
    return SDL_Vulkan_CreateSurface(win, inst, nullptr, surf);
#else
    return SDL_Vulkan_CreateSurface(win, inst, surf) == SDL_TRUE;
#endif
}

// The live renderer's factory. DECLARED rather than included: A8 rule 2 keeps
// `vulkan.h` inside `backends/vulkan/`, and this file must build and link with
// no Vulkan on the machine at all - which is what OMK_VULKAN guards.
namespace omk {
Renderer* makeVulkanRenderer();
const char* vulkanDeviceName(Renderer*);
void  vulkanNeedExtensions(Renderer*, const char* const*, unsigned);
void* vulkanCreateInstance(Renderer*);
bool  vulkanAttachSurface(Renderer*, unsigned long long);
bool  vulkanPresent(Renderer*);
bool  vulkanPresentSurface(Renderer*, const Surface&);
bool  vulkanPresentWorld(Renderer*, int vy, int vh);
// the menus' cloud stretched on the card under the interface (`under.frag`):
// `ui` the window-sized frame whose `kOverlayKey` pixels are the cloud's,
// `mask`/`maskRow` the overlay planes' M as `resolveOverlayPlanes` left them,
// `under` the 640x480 cloud, `filter` 0 nearest / 1 bilinear
bool  vulkanPresentUnder(Renderer*, const Surface& ui, const unsigned char* mask,
                         const unsigned char* maskRow, const Surface& under, int filter,
                         std::vector<unsigned char>* readback = nullptr);
bool  vulkanWorldPicture(Renderer*, int vy, int vh, std::vector<unsigned char>&);
bool  vulkanResize(Renderer*, int w, int h);
bool  vulkanRetarget(Renderer*, int samples, int ss, float up, float ao);   // OMK's options menu
}
