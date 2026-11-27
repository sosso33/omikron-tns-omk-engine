#version 450
// THE PRESENT PASS (todo/optimization.md step 4b) - the adventure frame's last
// CPU work, done where the frame already is.
//
// On a frame with nothing drawn over the 3D, the CPU path was: read the colour
// attachment back, `quantise888DitherRow` it into the RGB565 framebuffer at the
// letterbox's row offset (bands black), expand every 565 value to RGBA8 by bit
// replication (`expand565Rgba`) and upload it again. This computes the same
// bytes per pixel, from the same attachment, in integer arithmetic:
//
//   * `quantise888Dither` (ui/surface.h): the 4x4 Bayer offset t - 8, scaled to
//     one output step per channel with C++'s TRUNCATING division - written out
//     below rather than trusting a signed `/` - clamped, then
//     `(v * 31 + 127) / 255` (and 63 for green), all non-negative;
//   * the dither's cell is the WORLD picture's (x, y), not the window's: the
//     readback dithers the picture before it is placed `vy` rows down;
//   * with the dither off, `rgb565`'s truncation;
//   * then `(r5 << 3) | (r5 >> 2)`, `(g6 << 2) | (g6 >> 4)`, `(b5 << 3) | (b5 >> 3)`,
//     alpha 255, written as k / 255 into a UNORM8 target - which stores k.
//   * SUPERSAMPLED (`ss` > 1, todo/optimization.md "The 4K benchmark"): the
//     attachment is `ss` times the picture each way, and the pixel is first
//     the ROUNDED integer mean of its `ss x ss` block, `(sum + n/2) / n` on the
//     8-bit values - `readback`'s resolve - and only then dithered ONCE, at the
//     PICTURE's (x, y). That loop used to run on the CPU after reading the whole
//     oversized attachment back: 530 MB a frame at 4K with 4x4.
//
// `engine/tools/present_probe.cpp` checks it against the tables over every
// colour at every matrix cell, and `OMK_VERIFY_GPU_PRESENT` against the real
// framebuffer in play (`verify.py: engine: gpu present`).
//
//   * UPSCALED (`rcas` 1, `todo/remaster.md` A6): `world` is then EASU's
//     output, already at the picture's size (`easu.frag`), and the pixel is
//     AMD FSR 1's RCAS sharpening of it (`third_party/ffx_fsr1.h`, MIT) -
//     then dithered as any other. `raw` 1 writes the 8-bit value undithered
//     and unquantised: `readback`'s, whose CPU path quantises it itself.
#extension GL_GOOGLE_include_directive : require
layout(set = 0, binding = 0) uniform sampler2D world;
layout(push_constant) uniform Present {
    int vy;       // the picture's first row in the window
    int vh;       // its height; rows outside are the black bands
    int w;        // the picture's width (RCAS's clamp)
    int dither;   // the View's DITHERENABLE
    int ss;       // the supersample factor, 1 off
    int rcas;     // 1: the upscale's sharpening, `world` EASU's
    uint rcasCon; // `omk::fsrRcasCon`
    int raw;      // 1: the 8-bit value, for `readback`
} pc;
layout(location = 0) out vec4 o;

#define A_GPU 1
#define A_GLSL 1
#include "ffx_a.h"
#define FSR_RCAS_F 1
AF4 FsrRcasLoadF(ASU2 p) { return texelFetch(world, clamp(p, ASU2(0), ASU2(pc.w - 1, pc.vh - 1)), 0); }
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}
#include "ffx_fsr1.h"

// NO INTEGER DIVISION in this pass (2026-11-27, `todo/deko3d.md`): the Switch's
// GPU has none, and its compiler (`uam`) made each of the pass's thirteen `/`
// a long sequence a pixel - the present cost 12.05 ms of a 22.7 ms frame
// there, more than GLES's whole frame. Every divisor here is a constant or a
// power of two, so each is a shift or a multiply, EXACT over the values it
// meets (checked exhaustively): a / 255 for 0 <= a <= 65535 is
// (a * 257 + 257) >> 16; t * 8 / 16 and t * 4 / 16 truncated are shifts of
// the magnitude; the supersample's n = ss * ss is 1, 4 or 16. The 4x4 Bayer
// matrix (0 8 2 10 / 12 4 14 6 / 3 11 1 9 / 15 7 13 5, the table this was)
// is its bits: x0^y0, y0, x1^y1, y1, high to low.
int div255(int a) { return (a * 257 + 257) >> 16; }
int shrTrunc(int a, int s) { return a >= 0 ? a >> s : -((-a) >> s); }
int bayer4(int x, int y) {
    return (((x ^ y) & 1) << 3) | ((y & 1) << 2) | ((((x >> 1) ^ (y >> 1)) & 1) << 1) | ((y >> 1) & 1);
}
int clampByte(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    int wy = p.y - pc.vy;
    if (wy < 0 || wy >= pc.vh) {
        o = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    int r = 0, g = 0, b = 0;
    if (pc.rcas != 0) {
        AF1 fr, fg, fb;
#ifdef OMK_RCAS_LITE
        // RCAS in plain floats (-DOMK_RCAS_LITE, deko3d's module - GLES's
        // transcription, `kRcasFrag`): AMD's header through `uam` made the
        // Switch's upscaled present ~7 ms dearer than the native one. Within
        // its own error of AMD's, not bit for bit; Vulkan never defines it.
        {
            ivec2 lim = ivec2(pc.w - 1, pc.vh - 1);
            vec3 tb = texelFetch(world, clamp(ivec2(p.x, wy - 1), ivec2(0), lim), 0).rgb;
            vec3 td = texelFetch(world, clamp(ivec2(p.x - 1, wy), ivec2(0), lim), 0).rgb;
            vec3 te = texelFetch(world, clamp(ivec2(p.x, wy), ivec2(0), lim), 0).rgb;
            vec3 tf = texelFetch(world, clamp(ivec2(p.x + 1, wy), ivec2(0), lim), 0).rgb;
            vec3 th = texelFetch(world, clamp(ivec2(p.x, wy + 1), ivec2(0), lim), 0).rgb;
            vec3 mn4 = min(min(tb, td), min(tf, th));
            vec3 mx4 = max(max(tb, td), max(tf, th));
            vec3 hitMin = min(mn4, te) / max(4.0 * mx4, vec3(1.0e-20));
            vec3 hitMax = (1.0 - max(mx4, te)) / min(4.0 * mn4 - 4.0, vec3(-1.0e-20));
            vec3 lobeC = max(-hitMin, hitMax);
            float lobe = max(-(0.25 - 1.0 / 16.0), min(max(lobeC.r, max(lobeC.g, lobeC.b)), 0.0)) *
                         uintBitsToFloat(pc.rcasCon);
            vec3 rc = (lobe * tb + lobe * td + lobe * th + lobe * tf + te) / (4.0 * lobe + 1.0);
            fr = rc.r; fg = rc.g; fb = rc.b;
        }
#else
        FsrRcasF(fr, fg, fb, AU2(p.x, wy), AU4(pc.rcasCon, 0u, 0u, 0u));
#endif
        r = clampByte(int(round(fr * 255.0)));
        g = clampByte(int(round(fg * 255.0)));
        b = clampByte(int(round(fb * 255.0)));
    } else {
        for (int sy = 0; sy < pc.ss; ++sy)
            for (int sx = 0; sx < pc.ss; ++sx) {
                vec4 s = texelFetch(world, ivec2(p.x * pc.ss + sx, wy * pc.ss + sy), 0);
                r += int(round(s.r * 255.0));
                g += int(round(s.g * 255.0));
                b += int(round(s.b * 255.0));
            }
        // the rounded mean of the block: n = ss * ss, a power of two
        int halfN = (pc.ss * pc.ss) >> 1;
        int sh = pc.ss == 4 ? 4 : (pc.ss == 2 ? 2 : 0);
        r = (r + halfN) >> sh;
        g = (g + halfN) >> sh;
        b = (b + halfN) >> sh;
    }
    if (pc.raw != 0) {
        o = vec4(float(r), float(g), float(b), 255.0) / 255.0;
        return;
    }
    int r5, g6, b5;
    if (pc.dither != 0) {
        int t = bayer4(p.x, wy) - 8;
        int d5 = shrTrunc(t * 8, 4);
        int d6 = shrTrunc(t * 4, 4);
        r5 = div255(clampByte(r + d5) * 31 + 127);
        g6 = div255(clampByte(g + d6) * 63 + 127);
        b5 = div255(clampByte(b + d5) * 31 + 127);
    } else {
        r5 = r >> 3;
        g6 = g >> 2;
        b5 = b >> 3;
    }
    int R = (r5 << 3) | (r5 >> 2);
    int G = (g6 << 2) | (g6 >> 4);
    int B = (b5 << 3) | (b5 >> 3);
    o = vec4(float(R), float(G), float(B), 255.0) / 255.0;
}
