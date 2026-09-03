// SPDX-License-Identifier: GPL-3.0-or-later
// The I2D software back end: an RGB565 surface, and `Blt`.
//
// `docs/PORTING.md` A1/A4 makes this the REFERENCE implementation of the 2D
// path — the one `verify.py` checks — with a Vulkan backend to follow behind
// the same boundary. It is not an approximation of DirectDraw:
// `IDirectDrawSurface::Blt` is a memory copy with an optional colour key and
// an optional stretch, so a software transcription of it is the same
// operation, which is why `PORTING` B5 calls the 2D path exact.
//
// **RGB565, and compare in 565.** The game's framebuffer is 16-bit 565,
// established by measuring a capture (32 red levels, 63 green). A captured
// frame's 8-bit values are the HOST's 565->888 expansion, not the game's data,
// and the host does not expand the way you would guess: bit replication is off
// by one in green against what CrossOver actually produced (n<<2|n>>4 gives 56
// where the capture says 57; round(n*255/63) gives 57). So a comparison
// quantises the capture back to 565 rather than expanding the reference to
// 888, which removes the host from the question entirely. Done that way the
// menu's title region matches at **66560 of 66560 pixels**; done in 888 it
// matches 94.9% and every difference is a rounding artefact of the host.
//
// The flag values are the drawers' own, `sub_4810D0` and `sub_480F60`:
// `0x1000000` always (DDBLT_WAIT), `| 0x8000` when payload bit 0 is set
// (DDBLT_KEYSRC) and `| 0x2000` when bit 1 is (DDBLT_KEYDEST) — colour-key
// transparency, never alpha.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace omk {

inline constexpr std::uint32_t kBltWait    = 0x01000000u;  // always set
inline constexpr std::uint32_t kBltKeySrc  = 0x00008000u;  // payload bit 0
inline constexpr std::uint32_t kBltKeyDest = 0x00002000u;  // payload bit 1

inline std::uint16_t rgb565(int r, int g, int b) {
    return static_cast<std::uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

// 888 -> 565, which is how a captured frame is brought back into the
// framebuffer's own space. Rounding is the correct inverse in general;
// truncation (`>>3`, `>>2`) happens to invert every level the host actually
// produces, so a test over a capture cannot tell the two apart - measured, not
// assumed, and recorded so nobody reads a passing check as evidence for the
// choice. What DOES matter is comparing in 565 at all: expanding the reference
// to 888 instead needs the host's rule, and bit replication is not it.
inline std::uint16_t quantise888(int r, int g, int b) {
    const int r5 = (r * 31 + 127) / 255, g6 = (g * 63 + 127) / 255,
              b5 = (b * 31 + 127) / 255;
    return static_cast<std::uint16_t>((r5 << 11) | (g6 << 5) | b5);
}

struct Rect { int left = 0, top = 0, right = 0, bottom = 0; };

struct Surface {
    int w = 0, h = 0;
    std::vector<std::uint16_t> px;

    Surface() = default;
    Surface(int width, int height, std::uint16_t fill = 0)
        : w(width), h(height), px(static_cast<std::size_t>(width) * height, fill) {}

    std::uint16_t at(int x, int y) const {
        return px[static_cast<std::size_t>(y) * w + x];
    }
    void set(int x, int y, std::uint16_t v) {
        px[static_cast<std::size_t>(y) * w + x] = v;
    }
    bool valid() const { return w > 0 && h > 0; }
};

// `I2D_CreateSurfaceFromBmp` (0x00428DB0) does `LoadImageA` then a blit into a
// DirectDraw surface. The shipped interface bitmaps are Windows BMPs, 8-bit
// palettised, uncompressed, BOTTOM-UP (a positive height), with a 40-byte
// BITMAPINFOHEADER and a 256-entry BGRA palette - `gfxint.BMP` is 640x480 with
// its pixels at offset 1078, which is 54 + 256*4 exactly.
//
// Rows are padded to a multiple of four bytes. Never throws: an unreadable
// file comes back invalid, because these are user-supplied.
Surface surfaceFromBmp(std::span<const std::byte> d);

// `IDirectDrawSurface::Blt`, at vtable +20 - which is what settles that the
// payload's FIRST rectangle is the source and its second the destination.
//
// Stretches when the rectangles differ in size (nearest, as a 1999 blitter
// does), skips source pixels equal to `srcKey` under DDBLT_KEYSRC, and writes
// only where the destination equals `dstKey` under DDBLT_KEYDEST.
//
// -> false when either rectangle is degenerate or lands outside its surface,
// which is the drawer's own `if (v4 < 0)` error path rather than a crash.
bool blt(Surface& dst, Rect dr, const Surface& src, Rect sr,
         std::uint32_t flags, std::uint16_t srcKey = 0, std::uint16_t dstKey = 0);

// ------------------------------------------------- the software rasterizer
//
// **Which back end this is.** The I2D primitives other than the blits do not
// end in a `Blt`: `sub_4822F0` (line), `sub_4806C0` (triangle) and
// `sub_480BD0` (quad) each open with `if (sub_45EF50() == 2)` and take one of
// TWO paths - a Direct3D one, and the engine's own software rasterizer. The
// selector is a display-driver index: `sub_43A6D0` hands a real DirectDraw
// GUID to mode 0, and the last two synthetic entries of the device list to
// modes 1 and 2. **Mode 2 is the software back end**, and it is what this is a
// port of.
//
// So the tier here is **6, read and explained** - not the 4 the blits reach.
// The captured frames were taken in whatever mode CrossOver selected, which is
// not mode 2, so a line drawn by this cannot be diffed against them. It would
// become tier 4 with a capture taken in mode 2, which the driver index makes
// reachable; until then what is asserted is transcription invariants only, and
// `PORTING` B3 requires that be said rather than implied.
//
// **A triangle in mode 2 is a WIREFRAME.** `sub_4806C0`'s software branch is
// three calls to the line rasterizer over the three edges - there is no fill.
// The quad's is not: it is a separate float-based routine of ~189 lines and is
// not ported here.
//
// **And the software path takes its colour from a different place than the
// D3D one.** `I2D_DrawLine` stores the colour argument at payload dword 6, but
// `sub_48C4C0` is handed `word_907540[byte2] | word_907340[byte1] |
// word_907740[byte0]` of dword **2** - the third component of the FIRST point.
// Recorded rather than reconciled: both are what the code does.
//
// The three tables are built by `sub_440A20` from the surface's own channel
// MASKS, so the conversion follows the pixel format rather than assuming one;
// 565 is the instance the shipped path produced, and truncating into the field
// is what `rgb565` above does.

// `sub_48C4C0` (0x004890C0's family): clip to [left,right] x [top,bottom],
// then integer Bresenham. -> the number of pixels written.
//
// The clip is eight steps - each endpoint against each of the four sides -
// rejecting when both ends are outside the same side and otherwise moving the
// endpoint onto the boundary with an integer interpolation of the other axis.
// Transcribed including its asymmetries: the x0-against-right step measures
// against the ORIGINAL x1 where its neighbours use the running one.
long drawLine(Surface& dst, int x0, int y0, int x1, int y1, std::uint16_t colour,
              int left, int right, int top, int bottom);

// The mode-2 triangle: three edges, no fill.
long drawTriangleWire(Surface& dst, const int xy[6], std::uint16_t colour,
                      int left, int right, int top, int bottom);

// ------------------------------------------------------------- the quad
//
// **A mode-2 quad is an axis-aligned RECTANGLE FILL of the four points'
// bounding box** - `sub_48C060` takes their min/max, clamps to the screen and
// runs one of four blend loops. There is no general quad rasterisation and no
// gouraud: the colour comes from **vertex 0 alone**, even though the D3D path
// sets SHADEMODE from the flags.
//
// **The payload's third component per point is the COLOUR, not a z**, which
// is what `docs/UI.md`'s "{x, y, z}" had wrong. The mode-2 path builds four
// `D3DTLVERTEX`-shaped records - `{x, y, 1.0, 1.0, colour, 0, 0, 0}` - taking
// slot 4 from each point's third dword, and `sub_48C060` then reads vertex 0's.
// The submitter's third ARGUMENT is not a colour either: it lands one dword
// past the points (6, 9, 12 for line, triangle, quad) and both back ends use
// it as FLAGS - D3D sets SHADEMODE from bit 3, FILLMODE from bit 4 and
// ALPHABLENDENABLE from bits 0..2.
//
//     mode = 0;  if (flags & 7) { mode = 1; if (flags & 1) mode = 2;
//                                           if (flags & 2) mode = 3; }
//
// | mode | what the loop does |
// |---|---|
// | 0 | opaque fill |
// | 1 | 50% blend: `((half & c) >> 1) + ((half & dst) >> 1)` |
// | 2 | per-channel saturating ADD, clamped to the channel mask |
// | 3 | per-channel saturating SUBTRACT, clamped to zero |
//
// `half` is 0xF7DE for 565 - each channel's mask with its low bit cleared -
// and the engine derives it, like the masks, from the tables `sub_440A20`
// builds out of the surface's pixel format rather than assuming one.
//
// **One asymmetry, transcribed rather than tidied - and since CONFIRMED
// against the engine's own framebuffer**: modes 0 and 1 fill the half-open
// span `[left, right)` while modes 2 and 3 fill `[left, right]`, one pixel
// wider. Rows are inclusive in all four.
//
// It looked like a wart and is load-bearing. `I2D_DrawRectOutline` draws the
// load panel's selection box as four mode-1 quads of thickness
// `I2D_ScaleX(1)` = 1, and the half-open x span turns that single 1 into
// **2-pixel horizontal bars and 1-pixel vertical ones**. That is exactly what
// a mode-2 capture shows: 1518 covered pixels, all 1518 bright in the
// original's frame. Close the span and the port covers 1552, of which 20 are
// pixels the engine never lit, and the verticals come out 2 wide
// (`verify.py: engine I2D outline`).
//
// Modes 2 and 3 are still read through the decompiler's frame arithmetic
// (`*(v2 - 2)`, `*(v2 - 1)`) and have no such confirmation - nothing drawn
// with them has been captured.
int  quadMode(std::uint32_t flags);
long fillQuad(Surface& dst, const int x[4], const int y[4],
              std::uint16_t colour, int mode);

}  // namespace omk
