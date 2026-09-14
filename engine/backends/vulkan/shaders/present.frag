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
//
// `engine/tools/present_probe.cpp` checks it against the tables over every
// colour at every matrix cell, and `OMK_VERIFY_GPU_PRESENT` against the real
// framebuffer in play (`verify.py: engine: gpu present`).
layout(set = 0, binding = 0) uniform sampler2D world;
layout(push_constant) uniform Present {
    int vy;       // the picture's first row in the window
    int vh;       // its height; rows outside are the black bands
    int w;        // unused by the arithmetic, kept for the probe's sanity
    int dither;   // the View's DITHERENABLE
} pc;
layout(location = 0) out vec4 o;

const int kBayer4[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);

int divTrunc(int a, int b) { return a >= 0 ? a / b : -((-a) / b); }
int clampByte(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    int wy = p.y - pc.vy;
    if (wy < 0 || wy >= pc.vh) {
        o = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    vec4 s = texelFetch(world, ivec2(p.x, wy), 0);
    int r = int(round(s.r * 255.0));
    int g = int(round(s.g * 255.0));
    int b = int(round(s.b * 255.0));
    int r5, g6, b5;
    if (pc.dither != 0) {
        int t = kBayer4[(wy & 3) * 4 + (p.x & 3)] - 8;
        int d5 = divTrunc(t * 8, 16);
        int d6 = divTrunc(t * 4, 16);
        r5 = (clampByte(r + d5) * 31 + 127) / 255;
        g6 = (clampByte(g + d6) * 63 + 127) / 255;
        b5 = (clampByte(b + d5) * 31 + 127) / 255;
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
