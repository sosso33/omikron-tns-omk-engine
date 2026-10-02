// SPDX-License-Identifier: GPL-3.0-or-later
// THE GLES2 BACKEND'S ENTRY POINTS, for the files that talk to it -
// `playgpu_gles.cpp`, built only into the GLES window's variant.
#pragma once

#include "playshared.h"

// The GLES2 backend (`backends/gles/glesrender.cpp`, `todo/vita-port.md` F1),
// declared the same way as Vulkan's below: this file includes no GL header.
namespace omk {
Renderer* makeGlesRenderer();
bool glesPresentSurface(Renderer*, const Surface&, int winW, int winH);
bool glesPresentWorld(Renderer*, int vy, int vh, int frameW, int frameH, int winW, int winH);
void glesTakeTimings(double out[4]);
std::string glesFrameReport();
long glesTakePatches();
long glesTakeTiePatches();
void glesTakeWindow(double out[7]);
void glesTakeStateCalls(long out[3]);
void glesGeometryStats(Renderer*, long out[3]);
long glesTakeOverlayRows(Renderer*);
void glesSetDepthTie(Renderer*, bool);
bool glesPresentOverlay(Renderer*, const Surface&, const unsigned char* mask, const unsigned char* maskRows,
                        const float fade[4], int vy, int vh, int winW, int winH);
void glesWindowPicture(Renderer*, int w, int h, std::vector<unsigned char>& out);
}
