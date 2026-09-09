// SPDX-License-Identifier: GPL-3.0-or-later
// THE ORDERED DITHER, measured two ways.
//
//     dither_probe
//
// `sub_4638C0` sets D3DRENDERSTATE 26 (`DITHERENABLE`) to 1 on both of its
// device arms, so dithering is what the shipped engine does and this is NOT an
// enhancement - it is on by default and `--no-dither` exists only so two
// frames can be laid side by side. What is ported is that DECISION; the 4x4
// Bayer matrix itself is a RECONSTRUCTION, because Direct3D dithers inside the
// driver's conversion to the framebuffer format and that belongs to whatever
// card the player had. See `src/ui/surface.h`.
//
// Two halves, because a quantiser that is right on its own proves nothing
// about a frame and a frame that changes proves nothing about the law:
//
//   * THE LAW, against an exact reference. Over all 256 input levels, the
//     16 tile phases of a 4x4 block are averaged and compared with the input.
//     Plain rounding is flat, so its block mean is the rounded value and the
//     error is the whole rounding error; the dither spreads the block across
//     the two neighbouring levels in the right proportion, so its block mean
//     tracks the input. The error must FALL, and the signed bias must stay
//     near zero - a threshold running 0..+7 instead of -8..+7 passes the first
//     test and fails the second, which is the version this replaced.
//
//   * THE WIRING, through `SoftwareRenderer`. One gradient quad drawn twice,
//     `View::dither` off then on. Without the flag reaching the blend sites
//     the two frames are identical, which no amount of correct arithmetic in
//     `surface.h` would show.
//
// `verify.py: engine: dither` drives it.
#include "o3de/renderer.h"
#include "ui/surface.h"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>

namespace {

// 5-bit back to 0..255, the same expansion `quantise888`'s rounding inverts.
int expand5(int v) { return (v * 255 + 15) / 31; }

}  // namespace

int main() {
    // ---------------------------------------------------------------- the table
    int seen[16] = {0};
    for (int i = 0; i < 16; ++i) {
        const int v = omk::kBayer4[i];
        if (v >= 0 && v < 16) ++seen[v];
    }
    int permutation = 1;
    for (int i = 0; i < 16; ++i) if (seen[i] != 1) permutation = 0;
    std::printf("bayer permutation %d\n", permutation);

    // ------------------------------------------------------------------ the law
    double plainErr = 0.0, ditherErr = 0.0, ditherBias = 0.0;
    for (int L = 0; L < 256; ++L) {
        const int flat = expand5((omk::quantise888(L, L, L) >> 11) & 0x1F);
        plainErr += std::abs(flat - L);
        double sum = 0.0;
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                sum += expand5((omk::quantise888Dither(L, L, L, x, y) >> 11) & 0x1F);
        const double mean = sum / 16.0;
        ditherErr  += std::abs(mean - L);
        ditherBias += mean - L;
    }
    std::printf("block error x1000: plain %d, dithered %d\n",
                static_cast<int>(plainErr  / 256.0 * 1000.0 + 0.5),
                static_cast<int>(ditherErr / 256.0 * 1000.0 + 0.5));
    std::printf("dither signed bias x1000: %d\n",
                static_cast<int>(ditherBias / 256.0 * 1000.0 +
                                 (ditherBias < 0 ? -0.5 : 0.5)));

    // ---------------------------------------------------------------- the frame
    constexpr int W = 192, H = 48;
    omk::SoftwareRenderer ren;
    if (!ren.init(W, H)) { std::fprintf(stderr, "no software renderer\n"); return 1; }

    omk::Texture t;
    t.name = "white"; t.width = 1; t.height = 1; t.bpp = 24; t.exact = true;
    t.rgb = {255, 255, 255};
    const std::vector<omk::Texture> pool = {t};
    ren.setTextures(pool);

    omk::View v;
    v.cam.eye[0] = 0; v.cam.eye[1] = 0; v.cam.eye[2] = 0;
    v.cam.at[0] = 0;  v.cam.at[1] = 0;  v.cam.at[2] = 1;
    v.cam.hfovDeg = 90.0f;
    v.cam.w = W; v.cam.h = H;

    // A quad filling the view at z = 100, shaded by a SLOW horizontal ramp -
    // 0.02 to 0.14 across the whole width, which crosses about four 5-bit
    // steps in 192 pixels. That is the case the dither is for: a large smooth
    // surface whose colour crosses a quantisation step, which in 565 is a
    // visible band and after dithering is noise the eye integrates.
    omk::Geometry g;
    {
        const float d = 100.0f, s = d;                       // 90 deg hfov
        const float sy = s * float(H) / float(W);
        const float xs[6] = {-s,  s,  s, -s,  s, -s};
        const float ys[6] = {-sy, -sy, sy, -sy, sy, sy};
        for (int i = 0; i < 6; ++i) {
            omk::Corner c;
            c.x = xs[i]; c.y = ys[i]; c.z = d;
            c.u = 0; c.v = 0; c.phase = -1.0f;
            const float k = 0.02f + 0.12f * (xs[i] + s) / (2.0f * s);
            c.r = k; c.g = k; c.b = k;
            g.corners.push_back(c);
        }
        omk::Batch b;
        b.material = 0; b.start = 0; b.count = 6;
        b.blend = omk::Blend::Opaque; b.cutout = false;
        g.batches.push_back(b);
    }

    const auto render = [&](bool dither) {
        v.dither = dither;
        ren.begin(v);
        omk::Draw d;
        d.bucketKey = 0; d.geo = &g; d.start = 0; d.count = 6;
        d.blend = omk::Blend::Opaque; d.cutout = false;
        ren.submit(d);
        ren.end();
        return ren.readback();
    };
    const omk::Surface off = render(false);
    const omk::Surface on  = render(true);

    int differ = 0;
    double lumaOff = 0.0, lumaOn = 0.0;
    std::set<std::uint16_t> colOff, colOn;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const std::uint16_t a = off.at(x, y), b = on.at(x, y);
            if (a != b) ++differ;
            colOff.insert(a); colOn.insert(b);
            const auto luma = [](std::uint16_t p) {
                return 0.299 * expand5((p >> 11) & 0x1F)
                     + 0.587 * (((p >> 5) & 0x3F) * 255 + 31) / 63
                     + 0.114 * expand5(p & 0x1F);
            };
            lumaOff += luma(a); lumaOn += luma(b);
        }
    const double n = double(W) * double(H);
    std::printf("frame: differ %d of %d, colours %zu -> %zu, luma drift x1000 %d\n",
                differ, W * H, colOff.size(), colOn.size(),
                static_cast<int>((lumaOn - lumaOff) / n * 1000.0
                                 + ((lumaOn < lumaOff) ? -0.5 : 0.5)));
    return 0;
}
