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
// THE FRAME, for now: drawn offscreen at the frame's size, read back as
// RGB565 and composited and presented on the CPU exactly as the software
// reference's is - the SDL GL1 path's shape, and the slot `--world-vulkan`
// uses. Presenting straight to the top screen comes after.
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
}  // namespace omk
