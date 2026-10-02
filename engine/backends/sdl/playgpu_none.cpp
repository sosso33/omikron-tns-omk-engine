// SPDX-License-Identifier: GPL-3.0-or-later
// THE GPU WINDOW, NONE - `PlayState::gpu...` for the software-only variant
// (no Vulkan, no GLES): there is no GPU window, the frame goes to the SDL
// texture (`todo/play-split.md` S5).
#include "playframe.h"

bool PlayState::gpuWindowBuild() const { return false; }
void PlayState::gpuOpenWindow() {}
void PlayState::gpuOpenWorldHarness() {}
bool PlayState::gpuPresentSurface(const omk::Surface&) { return false; }
void PlayState::gpuPresentVerify(bool&) {}
void PlayState::gpuPresentOverlay(bool&) {}
void PlayState::gpuPresentWorld(bool&) {}
bool PlayState::gpuWorldOnWindow() { return false; }
void PlayState::gpuOverlayDecision(const char*&) {}
void PlayState::gpuResize(int, int, bool&) {}
bool PlayState::gpuDriverRow(std::vector<std::string>&) { return false; }
void PlayState::gpuReportTimings() {}
void PlayState::gpuSlowFrameReport() {}
void PlayState::gpuFinishReport() {}
void PlayState::gpuVerifyWorldPicture() {}
