#version 450
// SPDX-License-Identifier: GPL-3.0-or-later
// THE INTERFACE AS AN OVERLAY (`todo/deko3d.md` D2) - GLES's `kOverlayFrag`
// (`backends/gles/glesrender.cpp`) for deko3d: the frame the CPU composed,
// its world rows left as the KEY, laid over the world the present pass just
// drew - so a frame with a subtitle, a HUD or a conversation needs no
// readback. `play.cpp` resolves the key into two planes (`resolveOverlayPlanes`):
// C, the 565 frame, and M, how much of the world shows through; the blend
// (ONE, SRC_ALPHA) makes `C + world * M`.
//
// C is read as its 565 WORDS and widened by bit replication - `expand565Rgba`'s
// bytes - not through a UNORM read, which rounds (`c * 255 / 31`) and differs
// for 4 of the 32 five-bit values (Vulkan's `presentSurface` note).
layout(set = 0, binding = 0) uniform usampler2D pic;   // R16_UINT, the 565 words
layout(set = 0, binding = 1) uniform sampler2D mask;   // R8, the planes' M
layout(push_constant) uniform Overlay {
    vec4 fade;    // the colour fade: rgb, and its weight in a
} pc;
layout(location = 0) out vec4 o;

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    uint v = texelFetch(pic, p, 0).r;
    vec4 c;
    if (v == 0xF81Fu) {
        c = vec4(0.0, 0.0, 0.0, 1.0);   // the KEY: the world shows here, untouched by any pass
    } else {
        uint r5 = v >> 11, g6 = (v >> 5) & 63u, b5 = v & 31u;
        c = vec4(float((r5 << 3) | (r5 >> 2)), float((g6 << 2) | (g6 >> 4)), float((b5 << 3) | (b5 >> 3)),
                 0.0) / 255.0;
        c.a = texelFetch(mask, p, 0).r;
    }
    // the colour fade, over everything: new = old * (1 - f) + colour * f
    o = vec4(c.rgb * (1.0 - pc.fade.a) + pc.fade.rgb * pc.fade.a, c.a * (1.0 - pc.fade.a));
}
