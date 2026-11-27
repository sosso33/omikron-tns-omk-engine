#version 450
// SPDX-License-Identifier: GPL-3.0-or-later
// THE UPSCALE's EASU for deko3d (`todo/deko3d.md` D4b) - GLES's transcription
// of AMD FSR 1's 32-bit EASU (`backends/gles/glesrender_remaster_shaders.inl`
// `kEasuFrag`; `third_party/ffx_fsr1.h`, MIT): plain floats, the twelve
// texels read one by one, AMD's bit-trick reciprocals exact ones - within
// their own error of the Vulkan pass, not bit for bit.
//
// Why not `easu.frag` itself: compiled by `uam` (Mesa's old TGSI path) AMD's
// header cost the present 10.4 ms a 720p frame on the Switch against 2.7 with
// no upscale - the upscale slower than drawing the whole frame natively - and
// its `textureGather` read the wrong components (the picture cyan). GLES runs
// this transcription on the same GPU inside an 8.5 ms frame.
//
// deko3d's origin is the top-left both ways (the image's row 0 and
// `gl_FragCoord`), as the present pass reads it: no flip, and the reads are
// exact texels (`texelFetch`), CLAMPED to the drawn picture.
layout(set = 0, binding = 0) uniform sampler2D src;
layout(push_constant) uniform Easu {
    vec2 inSize;    // the drawn picture, render pixels (`rs(w_) x rs(vh)`)
    vec2 outSize;   // the output picture (`w_ x vh`)
} pc;
layout(location = 0) out vec4 o;

vec3 T(vec2 p) {
    p = clamp(p, vec2(0.0), pc.inSize - 1.0);
    return texelFetch(src, ivec2(p), 0).rgb;
}
float L(vec3 c) { return c.b * 0.5 + (c.r * 0.5 + c.g); }
float rcpS(float x) { return 1.0 / max(x, 1.0e-20); }
void setF(inout vec2 dir, inout float len, float w, float lA, float lB, float lC, float lD, float lE) {
    float dc = lD - lC, cb = lC - lB;
    float lenX = rcpS(max(abs(dc), abs(cb)));
    float dirX = lD - lB;
    dir.x += dirX * w;
    lenX = clamp(abs(dirX) * lenX, 0.0, 1.0);
    len += lenX * lenX * w;
    float ec = lE - lC, ca = lC - lA;
    float lenY = rcpS(max(abs(ec), abs(ca)));
    float dirY = lE - lA;
    dir.y += dirY * w;
    lenY = clamp(abs(dirY) * lenY, 0.0, 1.0);
    len += lenY * lenY * w;
}
void tap(inout vec3 aC, inout float aW, vec2 off, vec2 dir, vec2 len, float lob, float clp, vec3 c) {
    vec2 v = vec2(off.x * dir.x + off.y * dir.y, off.x * -dir.y + off.y * dir.x) * len;
    float d2 = min(v.x * v.x + v.y * v.y, clp);
    float wB = 2.0 / 5.0 * d2 - 1.0;
    float wA = lob * d2 - 1.0;
    wB *= wB;
    wA *= wA;
    wB = 25.0 / 16.0 * wB - (25.0 / 16.0 - 1.0);
    float w = wB * wA;
    aC += c * w;
    aW += w;
}
void main() {
    vec2 ip = floor(gl_FragCoord.xy);
    vec2 pp = ip * (pc.inSize / pc.outSize) + (0.5 * pc.inSize / pc.outSize - 0.5);
    vec2 fp = floor(pp);
    pp -= fp;
    vec3 b = T(fp + vec2(0.0, -1.0)), c = T(fp + vec2(1.0, -1.0));
    vec3 e = T(fp + vec2(-1.0, 0.0)), f = T(fp), g = T(fp + vec2(1.0, 0.0)), h = T(fp + vec2(2.0, 0.0));
    vec3 i = T(fp + vec2(-1.0, 1.0)), j = T(fp + vec2(0.0, 1.0)), k = T(fp + vec2(1.0, 1.0)), l = T(fp + vec2(2.0, 1.0));
    vec3 n = T(fp + vec2(0.0, 2.0)), oo = T(fp + vec2(1.0, 2.0));
    float bL = L(b), cL = L(c), eL = L(e), fL = L(f), gL = L(g), hL = L(h);
    float iL = L(i), jL = L(j), kL = L(k), lL = L(l), nL = L(n), oL = L(oo);
    vec2 dir = vec2(0.0);
    float len = 0.0;
    setF(dir, len, (1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL);
    setF(dir, len, pp.x * (1.0 - pp.y), cL, fL, gL, hL, kL);
    setF(dir, len, (1.0 - pp.x) * pp.y, fL, iL, jL, kL, nL);
    setF(dir, len, pp.x * pp.y, gL, jL, kL, lL, oL);
    vec2 dir2 = dir * dir;
    float dirR = dir2.x + dir2.y;
    bool zro = dirR < 1.0 / 32768.0;
    dirR = zro ? 1.0 : inversesqrt(dirR);
    dir.x = zro ? 1.0 : dir.x;
    dir *= dirR;
    len = len * 0.5;
    len *= len;
    float stretch = (dir.x * dir.x + dir.y * dir.y) * rcpS(max(abs(dir.x), abs(dir.y)));
    vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
    float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
    float clp = rcpS(lob);
    vec3 min4 = min(min(f, g), min(j, k));
    vec3 max4 = max(max(f, g), max(j, k));
    vec3 aC = vec3(0.0);
    float aW = 0.0;
    tap(aC, aW, vec2(0.0, -1.0) - pp, dir, len2, lob, clp, b);
    tap(aC, aW, vec2(1.0, -1.0) - pp, dir, len2, lob, clp, c);
    tap(aC, aW, vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, i);
    tap(aC, aW, vec2(0.0, 1.0) - pp, dir, len2, lob, clp, j);
    tap(aC, aW, vec2(0.0, 0.0) - pp, dir, len2, lob, clp, f);
    tap(aC, aW, vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, e);
    tap(aC, aW, vec2(1.0, 1.0) - pp, dir, len2, lob, clp, k);
    tap(aC, aW, vec2(2.0, 1.0) - pp, dir, len2, lob, clp, l);
    tap(aC, aW, vec2(2.0, 0.0) - pp, dir, len2, lob, clp, h);
    tap(aC, aW, vec2(1.0, 0.0) - pp, dir, len2, lob, clp, g);
    tap(aC, aW, vec2(1.0, 2.0) - pp, dir, len2, lob, clp, oo);
    tap(aC, aW, vec2(0.0, 2.0) - pp, dir, len2, lob, clp, n);
    o = vec4(min(max4, max(min4, aC / aW)), 1.0);
}
