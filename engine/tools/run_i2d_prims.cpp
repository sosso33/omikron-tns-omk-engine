// SPDX-License-Identifier: GPL-3.0-or-later
// The I2D primitives other than the blits, run through the software back end.
//
//     run_i2d_prims <out.bin>
//
// **Tier 6, read and explained — deliberately not 4.** The blits reach tier 4
// because a `Blt` is a memory copy and the captured frames can be diffed
// against it. These cannot: `sub_4822F0`, `sub_4806C0` and `sub_480BD0` each
// choose between Direct3D and the engine's own software rasterizer on
// `sub_45EF50() == 2`, the captures were taken in whatever mode CrossOver
// selected, and that is not mode 2. So what is asserted here is transcription
// invariants — properties the port could violate — and NOT agreement with the
// original. `PORTING` B3 requires that be stated rather than implied, and a
// capture taken in mode 2 (the driver index makes it reachable) is what would
// raise it.
//
// The invariants, each of which the code could fail:
//
//   * **nothing is written outside the clip rectangle.** The rasterizer clips
//     analytically and then plots; if the two disagreed, pixels would land
//     outside. Over a sweep of lines from far outside the surface to far
//     outside it, 0 may escape;
//   * **a line with both endpoints outside one side writes nothing** — the
//     reject arm of each of the eight clip steps;
//   * **an UNCLIPPED edge is the same set of pixels drawn either way round.**
//     The original swaps so x0 <= x1 before Bresenham, so reversing the
//     endpoints must not move a pixel — and it does not, 1500 of 1500. This is
//     the test that catches an error-term slip. It is scoped to unclipped
//     lines on purpose: the CLIP runs before the swap and processes endpoint 0
//     first using running values, so it is asymmetric by construction and a
//     clipped line reverses identically only 1255 times in 1459. Asserting
//     both is what distinguishes "the original is asymmetric here" from "the
//     port has a bug", which the first version of this sweep could not tell
//     apart. It does NOT pin the error term: an error initialised to 0
//     instead of -dx>>1 shifts a line half a pixel symmetrically and still
//     reverses identically, which the first version of this docstring claimed
//     otherwise;
//   * **every plotted pixel is the nearest one to the ideal line** —
//     |2*(dy*Δx − dx*Δy)| ≤ max(|dx|,|dy|), in doubled integers so it is
//     exact. This is the test that pins the error term, and the zero-init
//     mutation moves it off 0;
//   * **a wireframe triangle writes exactly the union of its three edges** —
//     `sub_4806C0`'s mode-2 branch is three line calls, so the triangle can
//     have no pixel its edges do not;
//   * **the quad's four blend modes behave as blends must.** Opaque leaves the
//     colour; the 50% blend of a colour with ITSELF is that colour again (the
//     half-mask must not drift); the saturating add never decreases a channel
//     and saturates to the mask; the saturating subtract never increases one
//     and floors at zero. Those are properties a wrong mask or a missing
//     clamp breaks, and they are checked per channel rather than on the
//     packed word, because a packed comparison hides a channel that borrowed
//     into its neighbour.
#include "platform/datafs.h"
#include "ui/surface.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <vector>

namespace {
struct Rng {
    std::uint32_t s = 0x9E3779B9u;
    std::uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    int range(int lo, int hi) {
        return lo + static_cast<int>(next() % static_cast<std::uint32_t>(hi - lo + 1));
    }
};
std::set<std::pair<int, int>> painted(const omk::Surface& s, std::uint16_t c) {
    std::set<std::pair<int, int>> out;
    for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < s.w; ++x)
            if (s.at(x, y) == c) out.insert({x, y});
    return out;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: run_i2d_prims <out.bin>\n"); return 2; }
    const int W = 640, H = 480;
    const std::uint16_t C = omk::rgb565(255, 255, 255);
    const int L = 0, R = W - 1, T = 0, B = H - 1;

    long lines = 0, drawnPx = 0, outside = 0, rejected = 0;
    long inPairs = 0, inSame = 0, clipPairs = 0, clipSame = 0;
    // The geometric test: every plotted pixel must be the NEAREST one to the
    // ideal line on its minor axis. This is what actually pins the error term
    // - the reversal test does not, because an error initialised to 0 instead
    // of -dx>>1 shifts a line by half a pixel SYMMETRICALLY and so reverses
    // identically. Measured in doubled integers to stay exact:
    // |2*(dy*(x-x0) - dx*(y-y0))| must not exceed the major delta.
    long probed = 0, offLine = 0;
    Rng rng;
    for (int i = 0; i < 3000; ++i) {
        // half wholly inside, half ranging well beyond the surface so every
        // clip arm is hit
        const bool inside = (i % 2) == 0;
        const int x0 = inside ? rng.range(0, W - 1) : rng.range(-400, W + 400);
        const int y0 = inside ? rng.range(0, H - 1) : rng.range(-400, H + 400);
        const int x1 = inside ? rng.range(0, W - 1) : rng.range(-400, W + 400);
        const int y1 = inside ? rng.range(0, H - 1) : rng.range(-400, H + 400);
        const bool clipped = !(x0 >= 0 && x0 < W && y0 >= 0 && y0 < H &&
                               x1 >= 0 && x1 < W && y1 >= 0 && y1 < H);
        omk::Surface a(W, H, 0);
        const long n = omk::drawLine(a, x0, y0, x1, y1, C, L, R, T, B);
        ++lines;
        drawnPx += n;
        if (n == 0) ++rejected;
        const auto pa = painted(a, C);
        for (const auto& [px, py] : pa)
            if (px < L || px > R || py < T || py > B) ++outside;

        if (!clipped) {
            const int ddx = x1 - x0, ddy = y1 - y0;
            const long major = std::labs(ddx) >= std::labs(ddy)
                             ? std::labs(ddx) : std::labs(ddy);
            for (const auto& [px, py] : pa) {
                ++probed;
                const long cross = 2L * (static_cast<long>(ddy) * (px - x0) -
                                         static_cast<long>(ddx) * (py - y0));
                if (std::labs(cross) > major) ++offLine;
            }
        }
        omk::Surface b(W, H, 0);
        omk::drawLine(b, x1, y1, x0, y0, C, L, R, T, B);
        const bool same = pa == painted(b, C);
        if (clipped) { ++clipPairs; clipSame += same; }
        else         { ++inPairs;   inSame   += same; }
    }

    // a line wholly off one side must write nothing - the reject arms
    long rejectArms = 0;
    {
        const int cases[4][4] = {{-50, 100, -10, 300},    // both left
                                 {700, 100, 900, 300},    // both right
                                 {100, -50, 300, -10},    // both above
                                 {100, 600, 300, 900}};   // both below
        for (const auto& c : cases) {
            omk::Surface s(W, H, 0);
            if (omk::drawLine(s, c[0], c[1], c[2], c[3], C, L, R, T, B) == 0)
                ++rejectArms;
        }
    }

    // the wireframe triangle: exactly the union of its edges, no fill
    long tris = 0, triExact = 0;
    for (int i = 0; i < 400; ++i) {
        int xy[6];
        for (int k = 0; k < 6; ++k) xy[k] = rng.range(-100, (k & 1) ? H + 100 : W + 100);
        omk::Surface t(W, H, 0);
        omk::drawTriangleWire(t, xy, C, L, R, T, B);
        omk::Surface e(W, H, 0);
        omk::drawLine(e, xy[0], xy[1], xy[2], xy[3], C, L, R, T, B);
        omk::drawLine(e, xy[2], xy[3], xy[4], xy[5], C, L, R, T, B);
        omk::drawLine(e, xy[4], xy[5], xy[0], xy[1], C, L, R, T, B);
        ++tris;
        if (painted(t, C) == painted(e, C)) ++triExact;
    }

    // the quad: the four modes, over every 565 colour pair that matters
    long quadPx = 0, blendIdempotent = 0, addMonotone = 0, subMonotone = 0,
         addSaturates = 0, subFloors = 0, channelBleed = 0;
    {
        const auto chan = [](std::uint16_t v, int i) {
            return i == 0 ? (v >> 11) & 31 : i == 1 ? (v >> 5) & 63 : v & 31;
        };
        Rng q;
        for (int i = 0; i < 4000; ++i) {
            const auto c = static_cast<std::uint16_t>(q.next() & 0xFFFF);
            const auto d = static_cast<std::uint16_t>(q.next() & 0xFFFF);
            const int px[4] = {10, 14, 14, 10}, py[4] = {10, 10, 12, 12};
            for (int m = 0; m < 4; ++m) {
                omk::Surface s2(32, 32, d);
                quadPx += omk::fillQuad(s2, px, py, c, m);
                const std::uint16_t got = s2.at(11, 11);
                for (int k = 0; k < 3; ++k) {
                    const int cc = chan(c, k), dd = chan(d, k), gg = chan(got, k);
                    const int mx = k == 1 ? 63 : 31;
                    if (m == 2) {
                        if (gg < dd) ++channelBleed;              // add lowered one
                        if (cc + dd >= mx && gg != mx) ++channelBleed;
                        else if (cc + dd < mx && gg != cc + dd) ++channelBleed;
                    } else if (m == 3) {
                        if (gg > dd) ++channelBleed;              // sub raised one
                        if (dd < cc && gg != 0) ++channelBleed;
                        else if (dd >= cc && gg != dd - cc) ++channelBleed;
                    }
                }
                if (m == 2 && got >= d) ++addMonotone;
                if (m == 3 && got <= d) ++subMonotone;
            }
            // the 50% blend of a colour with itself must be that colour, up to
            // the low bit the half-mask drops
            omk::Surface s3(32, 32, c);
            omk::fillQuad(s3, px, py, c, 1);
            if (s3.at(11, 11) == (c & 0xF7DE)) ++blendIdempotent;
            // saturation and flooring at the extremes
            omk::Surface s4(32, 32, 0xFFFF);
            omk::fillQuad(s4, px, py, 0xFFFF, 2);
            if (s4.at(11, 11) == 0xFFFF) ++addSaturates;
            omk::Surface s5(32, 32, 0x0000);
            omk::fillQuad(s5, px, py, 0xFFFF, 3);
            if (s5.at(11, 11) == 0x0000) ++subFloors;
        }
    }

    std::vector<std::uint8_t> o;
    const auto put = [&o](long v) {
        const auto u = static_cast<std::uint32_t>(static_cast<std::int32_t>(v));
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    for (long v : {lines, drawnPx, outside, rejected, inPairs, inSame,
                   clipPairs, clipSame, rejectArms, tris, triExact,
                   probed, offLine, quadPx, blendIdempotent, addMonotone,
                   subMonotone, addSaturates, subFloors, channelBleed})
        put(v);
    if (!omk::safeOutputPath(argv[1])) return 2;
    std::ofstream out(argv[1], std::ios::binary);
    out.write(reinterpret_cast<const char*>(o.data()),
              static_cast<std::streamsize>(o.size()));

    std::printf("lines: %ld drawn, %ld pixels, %ld OUTSIDE the clip rect, "
                "%ld wholly rejected\n", lines, drawnPx, outside, rejected);
    std::printf("reversal, UNCLIPPED: %ld pairs, %ld identical; CLIPPED: "
                "%ld pairs, %ld identical - the clip is asymmetric by "
                "construction\n", inPairs, inSame, clipPairs, clipSame);
    std::printf("nearest-pixel: %ld probed, %ld off the ideal line\n",
                probed, offLine);
    std::printf("reject arms hit: %ld of 4\n", rejectArms);
    std::printf("triangles: %ld, %ld exactly the union of their three edges\n",
                tris, triExact);
    std::printf("quad: %ld pixels over 4 modes; blend idempotent %ld/4000; "
                "add monotone %ld/4000, saturates %ld/4000; sub monotone "
                "%ld/4000, floors %ld/4000; per-channel errors %ld\n",
                quadPx, blendIdempotent, addMonotone, subMonotone,
                addSaturates, subFloors, channelBleed);
    return 0;
}
