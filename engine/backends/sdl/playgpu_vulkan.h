// SPDX-License-Identifier: GPL-3.0-or-later
// THE VULKAN BACKEND'S ENTRY POINTS and SDL's Vulkan header, for the files
// that talk to it - `playgpu_vulkan.cpp`, built only into the Vulkan
// variant, and the set viewer's own window (`playscene.cpp`).
#pragma once

#include "playshared.h"

#if defined(OMK_SDL3)
#  include <SDL3/SDL_vulkan.h>
#else
#  include <SDL_vulkan.h>
#endif

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
bool  vulkanWorldPicture(Renderer*, int vy, int vh, std::vector<unsigned char>&);
bool  vulkanResize(Renderer*, int w, int h);
}
