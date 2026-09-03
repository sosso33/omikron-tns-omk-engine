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
//     SetRenderState(27, 1) arm - and never alpha.
layout(push_constant) uniform Push {
    mat4 mvp;
    int  cutout;
    int  pad0, pad1, pad2;
} pc;

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vCol;
layout(location = 0) out vec4 outColour;

void main() {
    vec3 t = texture(tex, vUV / vec2(textureSize(tex, 0))).rgb;
    if (pc.cutout != 0 && t.r == 0.0 && t.g == 0.0 && t.b == 0.0) discard;
    outColour = vec4(clamp(t * vCol, 0.0, 1.0), 1.0);
}
