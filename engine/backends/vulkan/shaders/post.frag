#version 450
// THE REMASTER'S POST-PASS (`todo/remaster.md` step 2) - the BLOOM from the
// texture pack's LIGHT MAPS and the city's colour GRADE, both off unless
// asked. `colour_` is first copied to `src`; then, one shader in four modes:
//
//   mode 0  DOWN:  a quarter-size image of the light - `src.rgb * src.a`,
//           the alpha being the light map's value `scene.frag` wrote with
//           the bloom on - four bilinear taps covering each 4x4 block
//   mode 1  BLUR:  21 taps along `dir`, a Gaussian of sigma 4 quarter-texels
//   mode 3  HALO:  the blurred glow with the rain's HALOS added (`haloOf`),
//           at the glow's own quarter size, once - the final pass then reads
//           it as the glow (its `halo` 0), not 25 taps of it a frame's pixel
//   mode 2  FINAL: `src` with the glow SCREEN-blended over it (`1 - (1 - c)
//           (1 - g * strength)`), then the GRADE: a 3D LUT stored as an
//           N*N x N strip (blue slices side by side), looked up trilinearly;
//           and before both, the AMBIENT OCCLUSION (`ao.frag`, the remaster's
//           A4) multiplied in where `ao` says - not on a light map's texels
//
// The figures are the previews' the reader chose (2026-10-20).
layout(push_constant) uniform Push {
    vec2  dstInv;     // 1 / the size of the image being written
    vec2  srcInv;     // 1 / the size of the image being read
    vec2  dir;        // the blur's direction, in source texels
    float strength;   // the bloom's, 0 off
    int   mode;
    int   lutSize;    // the grade's N, 0 off
    int   ao;         // 1: binding 3 holds the half-size occlusion
    float halo;       // the HALOS in the rain (`View::halo`), 0 none
} pc;

layout(set = 0, binding = 0) uniform sampler2D src;
layout(set = 0, binding = 1) uniform sampler2D glow;
layout(set = 0, binding = 2) uniform sampler2D lut;
layout(set = 0, binding = 3) uniform sampler2D occlusion;
layout(location = 0) out vec4 outColour;

// the GLOW a texel's alpha carries (`scene.frag`): 0 on a body (alpha 0) and
// on the set without light (1/255), its light map's value above
float glowOf(float a) { return max(0.0, a * 255.0 - 1.0) / 254.0; }

// THE HALOS in the rain (`View::halo`, the reader's "go" on the weather
// list's 2, 2026-10-28): the wet air spreads each light's bloom into a wide
// soft glow round it - the glow image sampled on two rings about the pixel,
// added to the bloom, so a lamp keeps its core and gains a halo (a wider
// blur alone would have dimmed the core). GLES's twin in
// glesrender_remaster_shaders.inl.
const float kHaloGain = 1.5;      // the halo's light against the bloom's, at full rain
const float kHaloRadius = 12.0;   // its outer ring, in glow texels (a quarter of the frame's)
vec3 haloOf(sampler2D g, vec2 uv, vec2 texel) {
    vec3 h = texture(g, uv).rgb;
    for (int i = 0; i < 8; ++i) {
        float a = (float(i) + 0.5) * 0.7853982;
        h += texture(g, uv + vec2(cos(a), sin(a)) * (0.5 * kHaloRadius) * texel).rgb;
    }
    for (int i = 0; i < 16; ++i) {
        float a = float(i) * 0.3926991;
        h += texture(g, uv + vec2(cos(a), sin(a)) * kHaloRadius * texel).rgb;
    }
    return h / 25.0;
}

vec3 graded(vec3 c) {
    float n = float(pc.lutSize);
    c = clamp(c, 0.0, 1.0);
    float bs = c.b * (n - 1.0);
    float b0 = floor(bs);
    float b1 = min(b0 + 1.0, n - 1.0);
    float t = bs - b0;
    vec2 inv = vec2(1.0 / (n * n), 1.0 / n);
    vec2 rg = c.rg * (n - 1.0) + 0.5;
    vec3 a = texture(lut, (vec2(b0 * n, 0.0) + rg) * inv).rgb;
    vec3 b = texture(lut, (vec2(b1 * n, 0.0) + rg) * inv).rgb;
    return mix(a, b, t);
}

void main() {
    vec2 uv = gl_FragCoord.xy * pc.dstInv;
    if (pc.mode == 0) {
        vec3 acc = vec3(0.0);
        for (int j = -1; j <= 1; j += 2)
            for (int i = -1; i <= 1; i += 2) {
                vec4 s = texture(src, uv + vec2(i, j) * pc.srcInv);
                acc += s.rgb * glowOf(s.a);
            }
        outColour = vec4(acc * 0.25, 1.0);
    } else if (pc.mode == 1) {
        vec3 acc = vec3(0.0);
        float wsum = 0.0;
        for (int k = -10; k <= 10; ++k) {
            float w = exp(-float(k * k) / 32.0);
            acc += texture(src, uv + pc.dir * pc.srcInv * float(k)).rgb * w;
            wsum += w;
        }
        outColour = vec4(acc / wsum, 1.0);
    } else if (pc.mode == 3) {
        // `srcInv` is the FRAME's, as the final pass's was: the same texel
        outColour = vec4(texture(src, uv).rgb + haloOf(src, uv, 4.0 * pc.srcInv) * (kHaloGain * pc.halo), 1.0);
    } else {
        vec4 s0 = texture(src, uv);
        vec3 c = s0.rgb;
        // a glowing texel (its light map, the alpha with the bloom on) is not
        // occluded, and neither is a BODY (alpha 0): the occlusion is the set's
        if (pc.ao != 0) c *= s0.a < 0.5 / 255.0 ? 1.0 : mix(texture(occlusion, uv).r, 1.0, glowOf(s0.a));
        if (pc.strength > 0.0) {
            vec3 g = texture(glow, uv).rgb;
            if (pc.halo > 0.0) g += haloOf(glow, uv, 4.0 * pc.srcInv) * (kHaloGain * pc.halo);
            g = min(g * pc.strength, vec3(1.0));
            c = 1.0 - (1.0 - c) * (1.0 - g);
        }
        if (pc.lutSize > 1) c = graded(c);
        outColour = vec4(c, 1.0);
    }
}
