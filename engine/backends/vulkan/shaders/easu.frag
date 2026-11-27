#version 450
// THE UPSCALE's first pass (`todo/remaster.md` A6) - AMD FSR 1's EASU, the
// edge-adaptive spatial upsampler, from the vendored `third_party/ffx_fsr1.h`
// (MIT; `glslc -I third_party`). `colour_`'s drawn picture, smaller than the
// output by the upscale factor, comes up to the OUTPUT's size here; RCAS
// sharpens it in `present.frag`, before the original's 565 dither, so the
// dither stays the output pixel's as the game's was.
//
// The 32-bit path (`FSR_EASU_F`). The 16-bit one (`FSR_EASU_H`, with
// `shaderFloat16`) was MEASURED on the M1 at 4K (2026-11-13): 3.07 -> ~2.7 ms,
// for 4.5% of the frame's pixels moved, some by 36/255 on edges - not taken.
// `src` is sampled LINEAR, CLAMPED - textureGather takes the four texels
// round a point.
#extension GL_GOOGLE_include_directive : require
#define A_GPU 1
#define A_GLSL 1
#include "ffx_a.h"

layout(set = 0, binding = 0) uniform sampler2D src;
layout(push_constant) uniform Easu {
    uvec4 con0, con1, con2, con3;   // `omk::fsrEasuCon`
} pc;
layout(location = 0) out vec4 o;

#define FSR_EASU_F 1
#ifdef OMK_NO_GATHER
// THE FOUR TEXELS FETCHED, not gathered (-DOMK_NO_GATHER, deko3d's module -
// `todo/deko3d.md` D4b): on the Switch `textureGather` returned the wrong
// components - the picture came out CYAN, its red lost (the reader's photo,
// and a frame captured each way: fetched, the street as it is). The same
// four texels in `textureGather`'s order - (i0,j1), (i1,j1), (i1,j0),
// (i0,j0) round the corner `p` falls on. FSR asks red, green and blue of a
// point in that order (`ffx_fsr1.h` FsrEasuF), so the red call FETCHES and
// keeps the four, and green and blue read what it kept: 16 fetches a pixel,
// not 48 (the three calls' own fetches were not shared - 12.4 ms against 2.1).
// Vulkan never defines it: its module is as it was.
vec4 gT0, gT1, gT2, gT3;
AF4 FsrEasuRF(AF2 p) {
    ivec2 sz = textureSize(src, 0);
    ivec2 i0 = ivec2(floor(p * vec2(sz) - 0.5));
    ivec2 mx = sz - 1;
    gT0 = texelFetch(src, clamp(i0 + ivec2(0, 1), ivec2(0), mx), 0);
    gT1 = texelFetch(src, clamp(i0 + ivec2(1, 1), ivec2(0), mx), 0);
    gT2 = texelFetch(src, clamp(i0 + ivec2(1, 0), ivec2(0), mx), 0);
    gT3 = texelFetch(src, clamp(i0, ivec2(0), mx), 0);
    return AF4(gT0.r, gT1.r, gT2.r, gT3.r);
}
AF4 FsrEasuGF(AF2 p) { return AF4(gT0.g, gT1.g, gT2.g, gT3.g); }
AF4 FsrEasuBF(AF2 p) { return AF4(gT0.b, gT1.b, gT2.b, gT3.b); }
#else
AF4 FsrEasuRF(AF2 p) { return textureGather(src, p, 0); }
AF4 FsrEasuGF(AF2 p) { return textureGather(src, p, 1); }
AF4 FsrEasuBF(AF2 p) { return textureGather(src, p, 2); }
#endif
#include "ffx_fsr1.h"

void main() {
    AF3 c;
    FsrEasuF(c, AU2(gl_FragCoord.xy), pc.con0, pc.con1, pc.con2, pc.con3);
    o = vec4(c, 1.0);
}
