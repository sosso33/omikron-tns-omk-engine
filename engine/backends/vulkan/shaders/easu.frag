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
AF4 FsrEasuRF(AF2 p) { return textureGather(src, p, 0); }
AF4 FsrEasuGF(AF2 p) { return textureGather(src, p, 1); }
AF4 FsrEasuBF(AF2 p) { return textureGather(src, p, 2); }
#include "ffx_fsr1.h"

void main() {
    AF3 c;
    FsrEasuF(c, AU2(gl_FragCoord.xy), pc.con0, pc.con1, pc.con2, pc.con3);
    o = vec4(c, 1.0);
}
