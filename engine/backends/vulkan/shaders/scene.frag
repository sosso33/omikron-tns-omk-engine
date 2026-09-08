#version 450
// The scene fragment shader, and it is a transcription of `raster.cpp`'s inner
// loop rather than a rendering choice:
//
//   * the texel is the material's own pixel units WRAPPED, which is what lets
//     one atlas tile across a wall - so the UVs arrive unnormalised and are
//     divided by textureSize here, with the sampler set to REPEAT;
//   * the baked vertex value is a COLOUR and multiplies the texel (ASSETS 4c);
//     38.9% of set vertices are not grey, and reading one byte as a brightness
//     renders every set in monochrome, which was this repo's own bug;
//   * `cutout` is flag 0x800 - a COLOUR KEY on black, the engine's
//     SetRenderState(27, 1) arm - and never alpha;
//   * the fog is linear over the clip distance's range, the CPU having
//     already applied the bucket key's exclusions (`renderer.h`, the View).
layout(push_constant) uniform Push {
    mat4  mvp;
    int   cutout;      // flag 0x800: a colour key on black, never alpha
    float fogStart;    // 0 = no fog for this batch; see renderer.h's View
    float fogEnd;
    vec3  fogColour;
} pc;

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vCol;
layout(location = 2) in float vDepth;
layout(location = 0) out vec4 outColour;

void main() {
    // Alpha is the KEY, written at upload: 0 on a black texel, 1 elsewhere.
    // Under the nearest sampler (the original's) this is exactly the old
    // `rgb == 0` test on the same texel. Under the bilinear ENHANCEMENT the
    // key texels are (0,0,0,0), so the sample is premultiplied: a fragment
    // more than half key is discarded and the rest is un-premultiplied,
    // which is what keeps the cutout edge free of a dark fringe.
    vec4 t = texture(tex, vUV / vec2(textureSize(tex, 0)));
    if (pc.cutout != 0) {
        if (t.a < 0.5) discard;
        t.rgb /= t.a;
    }
    vec3 c = clamp(t.rgb * vCol, 0.0, 1.0);
    // THE FOG, and it is raster.cpp's line transcribed. Linear -
    // `FOGTABLEMODE` 3 at density 1.0, the only mode the engine sets - over
    // the range the clip distance sizes. The CPU side has already applied the
    // bucket key's two exclusions and any doubling, so a zero end means this
    // batch is not fogged.
    if (pc.fogEnd > pc.fogStart && vDepth > pc.fogStart) {
        float f = clamp((pc.fogEnd - vDepth) / (pc.fogEnd - pc.fogStart), 0.0, 1.0);
        c = mix(pc.fogColour, c, f);
    }
    outColour = vec4(c, 1.0);
}
