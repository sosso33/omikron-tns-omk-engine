// SPDX-License-Identifier: GPL-3.0-or-later
// THE SHADERS' BLOCKS - the uniform, push and vertex layouts `backends/vulkan/shaders/*`
// read, byte for byte. Shared by the two backends that run those shaders: Vulkan
// (`vkrender.cpp`) and deko3d (`backends/deko3d/dkrender.cpp`, whose push blocks are
// uniform buffers - `dkshaders.py` - laid out identically under std140). Moved here
// from `vkrender.cpp` unchanged, 2026-11-27 (`todo/deko3d.md`).
#pragma once

#include "o3de/renderer.h"

#include <cstddef>
#include <cstdint>

namespace omk::shaderblocks {

// the present pass (`present.frag`), the menus' cloud (`under.frag`), the
// post-pass (`post.frag`) and the ambient occlusion (`ao.frag`)
struct PresentPush { int32_t vy, vh, w, dither, ss, rcas; uint32_t rcasCon; int32_t raw; };
struct UnderPush { int32_t W, H, uw, uh, filter; };
struct PostPush { float dstInv[2], srcInv[2], dir[2]; float strength; int32_t mode; int32_t lutSize; int32_t ao; float halo; };
struct AoPush { float view[4], depth[4]; float radius, strength; int32_t mode, pad; float srcInv[2]; };

// Set 1, binding 0 - per FRAME, and it must match `scene.frag`'s block.
// Set 1, binding 2 - the LIGHTS, per frame. std140: an array of structs
// aligns every member to 16 bytes, which three vec4s already are.
struct GpuLightStd140 {
    float posA[4];      // xyz position, w outer radius
    float dirB[4];      // xyz direction, w inner radius
    float colourI[4];   // rgb colour 0..1, a intensity
};
struct LightUbo {
    GpuLightStd140 l[omk::View::kMaxGpuLights];   // 8 the set's, the rest the remaster's points
    int32_t count;
    float   base;      // View::litBase, in the std140 padding that was here
    int32_t grey;      // View::grey, the greyscale bank (ops 150/151) - padding too
    int32_t tiles;     // 1: the TILED point lights (`o3de/lighttiles.h`) are bound - padding once
    // THE WATER's Fresnel (`todo/remaster.md` A3): the eye, at 3088. Written by
    // every `begin`, and the frame's own pass is the last one - the only one
    // that reads it (the reflection pass draws no water)
    float   eye[4];
    // THE RELIEF (`View::relief`), at 3104: read by the TILED module alone
    float   fx[4];
    // THE SAND'S DUST (`View::dust`, the restored weather's, the remaster's), at
    // 3120: strength, the drift's x and z, the distance scale; its colour at 3136
    float   dust[4];
    float   dustColour[4];
    // THE RAIN'S VEILS (`View::veil`), at 3152: strength, the fall's offset,
    // the distance scale; their colour at 3168, and the gust's lean at 3180 (`View::veilGust`)
    float   veil[4];
    float   veilColour[4];
    // THE LIGHTS' STREAKS on the wet floors (`View::streakLights`), at 3184:
    // x how many, y their strength (`View::streaks`); the lights at 3200
    float   streakN[4];
    GpuLightStd140 streak[omk::View::kMaxStreakLights];
};
// the offsets `scene.frag`'s block reads them at (std140: 64 lights of 48
// bytes, four scalars, then the vec4s)
static_assert(offsetof(LightUbo, eye) == 3088 && offsetof(LightUbo, fx) == 3104 &&
              offsetof(LightUbo, dust) == 3120 && offsetof(LightUbo, dustColour) == 3136 &&
              offsetof(LightUbo, veil) == 3152 && offsetof(LightUbo, veilColour) == 3168 &&
              offsetof(LightUbo, streakN) == 3184 && offsetof(LightUbo, streak) == 3200,
              "the Lights block's layout is scene.frag's");

struct ShadowUbo {
    float lightMvp[16];
    float strength;    // 0 = no shadow this frame, which is the default
    float texel;       // 1 / kShadowSide
    float bias;
    // THE SHIMMER's clock (`o3de/shimmer.h`), which is per FRAME like the rest
    // of this block and had a spare float sitting here for the std140
    // alignment. Not the shadow's business, but this is the frame's uniform.
    float shimmer;
};

// how a baked tie loser's corner is marked; `scene.vert` decodes it
constexpr float kTieLoserPhase = 8192.0f;

// One vertex, matching `scene.vert`'s three inputs. The UVs stay in the
// material's own PIXEL units, as the shipped data stores them, and the shader
// divides by textureSize with the sampler set to REPEAT - which is the same
// wrap `raster.cpp`'s `sample()` does with `% t.width`.
struct GpuVert {
    float x, y, z;
    float u, v;
    float r, g, b;
    // THE NORMAL, for row 7's per-pixel lighting. It travels with the vertex
    // because a bone's rotation turns its normals with it - `applyPose` does
    // that already, for the crowd's per-vertex light.
    float nx, ny, nz;
    // THE SHIMMER's phase (`o3de/shimmer.h`), -1 on a mesh that does not.
    float phase;
    // THE CORNER'S MESH (`Geometry::cornerMesh`), -1 for none: which affine
    // a POSED draw places it by (`scene.vert`, `posesBodies`); every other
    // draw ignores it.
    float mesh;
};

// It MUST match `scene.frag`'s block byte for byte, and the ordering is what
// makes it match: a `vec3` in a push-constant block is 16-byte aligned, so
// `fogColour` sits at offset 80 and everything scalar has to come before it.
// Packed the obvious way - mvp, cutout, fogStart, fogEnd, fogColour - the
// colour arrived four bytes early and had done since the fog landed; it went
// unseen because the shipped fog colour is BLACK, so the misplaced bytes were
// zeros. See `scene.frag`.
struct Push {
    float mvp[16];     // 0
    int32_t cutout;    // 64  flag 0x800: a colour key on black, never alpha
    float fogStart;    // 68  0 = no fog for this batch; see renderer.h's View
    float fogEnd;      // 72
    int32_t caster;    // 76  this batch CASTS, so it must not RECEIVE
    float fogColour[3];// 80
    int32_t lit;       // 92  this batch is LIT per pixel (row 7)
    int32_t cull;      // 96  the BACK-FACE CULL: 0 both sides, +1 discard
                       //     where !gl_FrontFacing, -1 where gl_FrontFacing
    int32_t posed;     // 100 a body POSED HERE: set 2 holds its affines and lights
    // THE TEXTURE PACK (`o3de/texpack.h`, `todo/remaster.md` B4): the bound
    // texture is a replacement `texScaleU` x `texScaleV` the original's size,
    // and the UVs are still in the ORIGINAL's texels. 0 for every shipped
    // texture, and the shader then divides exactly as it did before.
    int32_t texScaled; // 104
    float texScaleU;   // 108
    float texScaleV;   // 112
    // THE BLOOM (`todo/remaster.md` step 2, `post.frag`): 0 off; 1 on - the
    // alpha written is the texel's LIGHT, from the pack's light map
    int32_t bloomMode; // 116
    // THE WATER REFLECTION (`todo/remaster.md` A3): a water draw adds the
    // reflection (binding 2) times the strength; 0 on every other draw. The
    // low four bits the mode; above them a WET floor's darkening, 0..255
    // (`Draw::wet`, `o3de/wetfloor.h`), and above that the rain's, 0..255 (its
    // ripples, `View::rain`)
    int32_t water;     // 120
    float waterStrength; // 124
};
// 128 is every device's guaranteed push-constant size: the block is full
static_assert(sizeof(Push) == 128, "the push block is 128 bytes");

}  // namespace omk::shaderblocks
