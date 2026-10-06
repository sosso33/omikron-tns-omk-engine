// SPDX-License-Identifier: GPL-3.0-or-later
// THE GREYSCALE RENDER BANK - ops 150 / 151, `render.grey.on` / `.off`, the
// game's BLACK-AND-WHITE cutscenes (todo/drift-audit.md S11).
//
// `sub_42FA00(bank)` (0x0042FA00) installs one of two five-pointer banks
// (`off_4C4918`..`off_4C4938`, two entries each) and, when the bank CHANGES,
// calls that bank's activate hook (`funcs_42FA4D`). The second bank is
// `C:\Omikron\Sources\libdirect3d\bw.c` by its own assert strings, and every
// one of its entries is its colour twin's code with the colour taken to LUMA,
// `(299 R + 587 G + 114 B) / 1000`, truncated:
//
//   * `dword_90E09C`, the bucket walk - `sub_42FF80` against
//     `Render_FlushBuckets` - writes every vertex's diffuse as that grey in all
//     three bytes, the ALPHA byte (the fog factor) left as it was, and the FOG
//     colour (the scene's `+336`) the same way;
//   * `dword_90E0AC` / `dword_90E0A0`, the screen clears (`sub_431410` /
//     `sub_431460`), take the clear colour through `word_4EB8D8`, a 64K table
//     from a SCREEN-format pixel to its grey (`greyClear565` below);
//   * `dword_90E0A8` / `dword_90E0A4`, the texture and palette uploads
//     (`sub_4311C0` / `sub_430D90`), upload greyed; and the ACTIVATE hook
//     `sub_42FE80` greys every RESIDENT texture in place - a 16-bit surface
//     through `word_50B8D8`, a palettised one by greying its palette
//     (`sub_42FD40`), the software device's shade tables by `sub_483EB0`.
//     Bank 0's hook `sub_42FC10` re-uploads them all from the source pages.
//
// Bank 0 is installed by `Game_Init` and again by `sub_4193E0`, which both the
// restart (`Script_Pump` case 3) and `Game_LoadSave` reach through
// `sub_40E260` - a restart or a load always comes back in colour. All 14
// shipped `render.grey.on` are closed by a `.off` (SCRIPT_VM 150/151).
//
// The 2D layer (I2D: the interface, the subtitles) is DirectDraw and is not
// touched. The fades inside the 14 brackets are all black or white, which the
// luma leaves as they are, so whether the fade quad is greyed cannot show.
//
// What the port takes and what it leaves, declared:
//   * the texture is greyed through its PALETTE (`sub_42FD40`'s path), not
//     through `word_50B8D8`, which the original takes only for a device that
//     converted the 8-bit page to a 16-bit surface. The two differ by the
//     565 quantisation before the luma (and the table maps black to grey 4,
//     a 565 green of 1); which one a 1999 card took is not in this tree;
//   * the software device's arm (`sub_483EB0`) greys each of the 16 SHADES
//     of a palette entry, after the shading; the port's software reference
//     greys the palette and shades after - the luma is linear, so the two
//     differ only in the truncation;
//   * a GPU backend greys the vertex colour per FRAGMENT, where its shimmer
//     and its lights are computed - the luma is linear, so this is the
//     per-vertex grey interpolated, up to the per-vertex truncation.
#pragma once

#include "formats/tex3dt.h"

#include <algorithm>
#include <cstdint>

namespace omk {

// The luma every arm of `bw.c` takes of an 8-bit colour, truncated.
inline int lumaGrey(int r, int g, int b) {
    return (299 * r + 587 * g + 114 * b) / 1000;
}

// A vertex colour as the port carries it, 0..1 a channel -> its grey, 0..1.
// The engine greys BYTES, so the channel is taken back to its byte first.
inline float lumaGreyUnit(float r, float g, float b) {
    const auto byte = [](float c) {
        return static_cast<int>(std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return static_cast<float>(lumaGrey(byte(r), byte(g), byte(b))) / 255.0f;
}

// `sub_42FD40`: every one of the 256 palette entries to its luma in all three
// bytes. A new buffer - the colour one stays as it is for the restore, which
// is bank 0's re-upload from the source.
inline PixelBuffer greyPalette(const PixelBuffer& pal) {
    PixelBuffer out;
    if (pal.size() != 768) return pal;
    out.assign(768, 0);
    std::uint8_t* o = out.mutableData();
    for (int i = 0; i < 256; ++i) {
        const int y = lumaGrey(pal[3 * i], pal[3 * i + 1], pal[3 * i + 2]);
        o[3 * i] = o[3 * i + 1] = o[3 * i + 2] = static_cast<std::uint8_t>(y);
    }
    return out;
}

// `word_4EB8D8`, built by `sub_431010` (0x00431010) for an RGB565 screen, and
// the clear colour taken through it. The builder walks 64 x 64 x 64 levels -
// R outermost, B innermost - with the accumulator `3000 + 1196 i + 2348 j +
// 456 k` (4 x the luma weights, from 3000), and writes
//     table[ R(4i+3) | G(4j+3) | B(4k+3) ] = grey(acc / 1000)
// where a channel's bits are the TOP bits of the 8-bit level (`sub_440A20`'s
// component tables). A 5-bit channel is hit by two `i`, and the later write
// wins: the level it is read at is `8 r5 + 7`, i.e. `i = 2 r5 + 1`.
// -> the grey 565 pixel, returned as the 8-bit triple that truncates back to
// it (what `View::clearColour` carries).
inline void greyClear565(const std::uint8_t in[3], std::uint8_t out[3]) {
    const int r5 = in[0] >> 3, g6 = in[1] >> 2, b5 = in[2] >> 3;
    const int y = (3000 + 4 * (299 * (2 * r5 + 1) + 587 * g6 + 114 * (2 * b5 + 1))) / 1000;
    out[0] = static_cast<std::uint8_t>(y & ~7);
    out[1] = static_cast<std::uint8_t>(y & ~3);
    out[2] = static_cast<std::uint8_t>(y & ~7);
}

}  // namespace omk
