#version 450
// SPDX-License-Identifier: GPL-3.0-or-later
// THE CLOUD UNDER THE INTERFACE (the reader's "do the gpu rendering like the
// original", 2026-11-02). The menus' animated cloud is `sub_4B19C0`'s
// 640x480 surface, which DirectDraw's blitter stretched to the display on the
// card. The CPU used to do that stretch for every pixel of the window, every
// frame - a core and more at 3024x1964. Now the composer leaves the frame's
// cloud pixels as `kOverlayKey` and the passes that blend over them write the
// overlay planes (`ui/overlay.h`). The upload is the frame's own 565 words and
// the planes' M beside them (an untouched key: C = 0, M = 255; an opaque
// pixel: M = 0, its word expanded by replication), and this pass is
//
//     out = C + cloud * M / 255
//
// with the cloud sampled as the CPU blit samples it (`blt`, `ui/surface.cpp`),
// in its own integer arithmetic so the two agree to the bit: NEAREST at
// `x * uw / W` (the blit's column table), or the interface-scaling
// enhancement's BILINEAR, the blit's separable fixed point - then the frame
// quantised to 565 as every frame of this port is (PORTING A3), rounding as
// `quantise888` does.
layout(set = 0, binding = 0) uniform usampler2D ui;     // R16_UINT, the 565 words
layout(set = 0, binding = 1) uniform usampler2D under;  // the cloud, R16_UINT 565 words
layout(set = 0, binding = 2) uniform sampler2D mask;    // R8, the planes' M
layout(push_constant) uniform Under {
    int W, H;       // the frame
    int uw, uh;     // the cloud's surface, 640x480
    int linear;     // 0 nearest (the game's), 1 bilinear (`uiscaling linear`)
} pc;
layout(location = 0) out vec4 o;

// `kOverlayKey` (`ui/overlay.h`): magenta, the frame's "the cloud goes here"
const int kOverlayKey = 0xF81F;
// the blit's fixed point: a weight of 1.0 is 2^16
const int kOne = 1 << 16;

// TWO WIDENINGS of a 565 word, and which is which matters to the bit: the
// BLIT's (`blt`: blue `(b << 3) | (b >> 2)`), which its arithmetic runs on,
// and the PRESENT's (`expand565Rgba`, `present.frag`: blue `>> 3`), which is
// what the window shows of any 565 pixel
ivec3 blitWide(int w) {
    int r = (w >> 11) & 31, g = (w >> 5) & 63, b = w & 31;
    return ivec3((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2));
}
vec3 shown(int r5, int g6, int b5) {
    return vec3(float((r5 << 3) | (r5 >> 2)), float((g6 << 2) | (g6 >> 4)), float((b5 << 3) | (b5 >> 3)));
}
int word(int x, int y) {
    return int(texelFetch(under, ivec2(clamp(x, 0, pc.uw - 1), clamp(y, 0, pc.uh - 1)), 0).r);
}

// `blt`'s `tap`: the pixel centre `((2i + 1) s - d) / 2d`, floored, its
// fraction in 16 bits rounded, the two indices clamped
void tap(int i, int s, int d, out int i0, out int i1, out int w) {
    int num = (2 * i + 1) * s - d, den = 2 * d;
    int f = num >= 0 ? num / den : -((-num + den - 1) / den);
    w = ((num - f * den) * kOne + den / 2) / den;
    i0 = clamp(f, 0, s - 1);
    i1 = clamp(f + 1, 0, s - 1);
}

// one source row across: 8-bit channels weighted to 2^16, / 255 * (31, 63, 31),
// rounded to 8 fractional bits
ivec3 across(int y, int x0, int x1, int wx) {
    ivec3 a = blitWide(word(x0, y));
    ivec3 b = blitWide(word(x1, y));
    ivec3 sum = a * (kOne - wx) + b * wx;
    return ((sum / 255) * ivec3(31, 63, 31) + 128) >> 8;
}

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    int w = int(texelFetch(ui, p, 0).r);
    int m;
    vec3 c;
    if (w == kOverlayKey) {     // the cloud alone
        m = 255;
        c = vec3(0.0);
    } else {
        m = int(round(texelFetch(mask, p, 0).r * 255.0));
        c = shown((w >> 11) & 31, (w >> 5) & 63, w & 31);
    }
    if (m == 0) {
        o = vec4(c / 255.0, 1.0);
        return;
    }
    vec3 cloud;
    if (pc.linear == 0) {
        int u = word(p.x * pc.uw / pc.W, p.y * pc.uh / pc.H);
        cloud = shown((u >> 11) & 31, (u >> 5) & 63, u & 31);
    } else {
        // `blt`'s separable fixed point to the bit (`ui/surface.cpp`): the
        // taps at pixel centres with 16-bit weights, each source row
        // interpolated across into 565's ranges << 8, then the two rows
        // blended with the weight taken to 8 bits
        int x0, x1, wx, y0, y1, wy;
        tap(p.x, pc.uw, pc.W, x0, x1, wx);
        tap(p.y, pc.uh, pc.H, y0, y1, wy);
        ivec3 h0 = across(y0, x0, x1, wx), h1 = across(y1, x0, x1, wx);
        int v1 = (wy + 128) >> 8, v0 = 256 - v1;
        ivec3 q = (h0 * v0 + h1 * v1 + (1 << 15)) >> 16;
        cloud = shown(q.r, q.g, q.b);
    }
    // the planes' law, then the frame's 565
    ivec3 v = clamp(ivec3(round(c + cloud * float(m) / 255.0)), ivec3(0), ivec3(255));
    int r5 = (v.r * 31 + 127) / 255, g6 = (v.g * 63 + 127) / 255, b5 = (v.b * 31 + 127) / 255;
    o = vec4(shown(r5, g6, b5), 255.0) / 255.0;
}
