// SPDX-License-Identifier: GPL-3.0-or-later
// THE 3DS GPU BACKEND - citro3d on the PICA200 (`todo/3ds-port.md` step 3).
//
// The PICA200 is a FIXED-FUNCTION fragment pipeline behind a programmable
// vertex stage: per-vertex colour, texture combiners, an alpha test, a
// blender, a depth buffer - which is the shape of the original's Direct3D
// path, and so of the GL1 backend (`backends/gl1/`), which this mirrors law
// for law: the vertices transformed, near-clipped, culled and screen-clipped
// on the CPU and handed over in clip space (the original's `D3DTLVERTEX`); the
// colour law `texel x colour`, additive `src + dst`, multiply `dst * (1 -
// src)`, depth written by opaque draws only; the colour key on black as alpha
// 0 under the alpha test; the fog per vertex with the bucket key's two
// exclusions; the shimmer added to the vertex colour first; the 4x4 ordered
// dither applied at readback.
//
// What is the PICA's own: textures are RGBA5551 - 16 bits, as the original's
// D3D surfaces were, and its 1-bit alpha IS the colour key - tiled into the
// GPU's 8x8 Morton order at upload, from the kept palette indices (2534 of
// 2534 shipped textures are 256x256 or 64x64, inside the PICA's power-of-two
// 8..1024, measured 2026-10-06).
//
// THE FRAME: drawn offscreen at the frame's size. A frame with the interface
// over it is read back as RGB565 and composited and presented on the CPU
// exactly as the software reference's is (the SDL GL1 path's shape); a frame
// nothing is drawn over goes STRAIGHT (`c3dPresentHalf`, 3c).
//
// TIER: none (`PORTING` B6, like the Vulkan and GL1 backends). Its correctness
// is the software reference's, which it mirrors, and a frame from it is
// compared against that reference rather than against the original.
#pragma once

#include "o3de/renderer.h"

namespace omk {
// -> never null; `init()` is what fails, and the caller keeps the software
// reference then.
Renderer* makeC3dRenderer();

// THE STRAIGHT PRESENT (`todo/3ds-port.md` 3c): a frame nothing is drawn
// over leaves the GPU already HALVED - the display transfer's own 2x2
// average (`GX_TRANSFER_SCALE_XY`) - into `screen`, the 400x240 top-screen
// picture: the world's `vh` rows at row `vy` of the frame, halved and centred,
// dithered as the reference is, black elsewhere. A frame that already fits
// the screen (`--res 400x224`) goes over 1:1 instead, unhalved. No 640x480-class readback, no
// CPU composite, no CPU halving. -> false when the frame cannot go that way
// (its halves not multiples of 8, or larger than the screen), and the caller
// reads back and composites as before. `r` is `makeC3dRenderer`'s.
bool c3dPresentHalf(Renderer* r, int vy, int vh, Surface& screen);

// THE BACKEND'S OWN COUNTS over the frames since the last call - draws, posed
// draws on the GPU and on the CPU, the CPU's posing time, the time waited on
// the GPU at a transfer, the fullest the command buffer got and citro3d's GPU
// times - as one log line, then reset. The frame's "world submit" section
// holds all of the first three and can hold a GPU wait, which only these
// separate (the reader's fourth console run, 2026-10-06).
void c3dReport(Renderer* r, long frame);
}  // namespace omk
