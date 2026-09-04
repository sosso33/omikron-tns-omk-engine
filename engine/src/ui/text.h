// SPDX-License-Identifier: GPL-3.0-or-later
// Laying out a line of interface text - `Text_LayOutBlock` and `Text_DrawRun`.
//
// A `.FNT` alone cannot lay text out. The advance of a glyph is its own width
// plus the FACE's kerning, and a character the file does not carry falls back
// to the face's default advance - both of which live in the 13-record font
// table at 0x004C7090 (`Font_Find`), not in the glyph file. That table is
// lifted into `tables/ui.json` as `fonts`, keyed by an ASCII LETTER, which is
// what an item's `+36` has been all along: 74 is `J` JOURNAL, the engine's
// default, 83 is `S` SNEAK for a heading, 76 is `L` SMALL below 640x480.
//
// The MARKUP is read during layout, after the parameter block has been
// applied, so a directive in the string wins over the item's own flags:
//
//     {f<letter>}          select a face
//     {I<9 digits>}        an ink colour as THREE 3-DIGIT components - not a
//                          hex triple, which is the correction FILE_FORMATS
//                          5b4 needed
//     {X<6 digits>}        a move; the caller owns the box, so layout skips it
//     {G} {D} {C} {F}      align left / right / centre / justify
//     {H} {L} {M}          vertical align
//     {B} {E<n>} {g}       consumed and ignored here
//
// Several chain inside one brace (`{fCI255120045}`), and `[` `]` delimit
// counted spans that carry no styling.
#pragma once

#include "formats/fnt.h"
#include "ui/surface.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace omk {

// One row of the font table.
struct FontFace {
    int          index = 0;
    int          id = 0;          // the ASCII code an item's +36 carries
    char         letter = 0;
    std::string  name;            // the FONTS/<name>.FNT stem
    std::int16_t kern = 0;        // added to EVERY advance
    std::int16_t defaultAdvance = 0;   // for a glyph the file does not carry
    std::int16_t height = 0;
};

class FontTable {
public:
    static FontTable loadJson(const std::string& uiTable);
    const FontFace* byLetter(char c) const;
    std::size_t size() const { return faces_.size(); }
    const std::vector<FontFace>& all() const { return faces_; }

private:
    std::vector<FontFace> faces_;
};

// One styled character, as the parser hands it on.
struct StyledChar {
    char         ch = 0;
    char         face = 0;
    std::uint8_t rgb[3] = {255, 255, 255};
    // `{B}` - BLINK, and the colour it flashes to is RED.
    //
    // `Text_LayOutBlock`'s run-emit: when the blink bit is set and
    // oscillator 1 is high it writes `run[2] = 0xFF, run[3] = 0, run[4] = 0`
    // and otherwise the current colour. `docs/UI.md`'s markup table said
    // "white", and that was wrong - two captures of the original two seconds
    // apart show the MK400 notice's Khonsu line (`{fSI226198101B}`: font S,
    // colour (226, 198, 101), blink) gold in one and RED in the other.
    //
    // Parsed and then DROPPED until 2026-09-04, so a string that asked to
    // flash simply did not.
    bool         blink = false;
};

inline constexpr int kAlignLeft = 2, kAlignRight = 4, kAlignCentre = 8,
                     kAlignJustify = 16;

struct ParsedText {
    std::vector<StyledChar> run;
    int  align = -1;              // -1: the string set none, so the item's wins
    // `{X<xxx><yyy>}` - "move to (xxx, yyy) as PERCENTAGES of the screen"
    // (docs/UI.md 5). One string can carry several, and each starts a new
    // positioned block: that is how the Bowie title sequence's credits are
    // scattered around the frame. `AREA 0` record 78 fires twenty
    // `media.play` calls and each object's `+280` description is a block like
    //
    //     {X090058}{f1}{D}Direction programmation
    //     {X080065}{f3}{D}Olivier NALLET
    //
    // so the credits are ORDINARY SUBTITLES, not a system of their own. `at`
    // is the index into `run` where the move takes effect, and `align` is the
    // alignment in force for that block - the directives arrive in separate
    // braces AFTER the move, so it is filled in as they are read.
    struct Move { std::size_t at = 0; int xPct = 0, yPct = 0; int align = -1; };
    std::vector<Move> moves;
};

ParsedText parseMarkup(const std::string& text, char face = 'J',
                       std::uint8_t r = 255, std::uint8_t g = 255,
                       std::uint8_t b = 255);

// The advance of a run in pixels: per character, the glyph's own width or the
// face's default, plus the face's kerning.
class TextLayout {
public:
    TextLayout(const FontTable& t, const std::string& fontsDir)
        : table_(&t), dir_(fontsDir) {}

    int measure(const std::vector<StyledChar>& run) const;
    int measure(const std::string& text, char face = 'J') const;
    // How tall the tallest face used by a run is.
    int height(const std::vector<StyledChar>& run) const;

    const Font* face(char letter) const;

    // ---------------------------------------------------- the RASTERISER
    //
    // `Text_DrawRun` (0x0043EA10) is the half that was missing: layout says
    // where a glyph goes, this says what colour its pixels are.
    //
    // **The colour model is a 32-entry RAMP, and it is the whole of it.** A
    // glyph byte is a COVERAGE level 0..31 - not an intensity and not a
    // palette index - and the engine rebuilds `word_52F5B8[32]` whenever the
    // requested colour changes so that entry `i` is `i/31` of it. Zero is
    // transparent. Read from the asm rather than the docstring: the builder
    // runs 32 iterations from `word_52F5B8` to `dword_52F5F8` (0x40 bytes),
    // carries one accumulator per channel stepping by that channel's value,
    // and divides each by 31 with the canonical `0x08421085` magic-multiply -
    // so the division TRUNCATES, which is visible in the shipped frames.
    //
    // Confirmed against the original's framebuffer: the start menu's
    // unfocused rows come out (123,125,123), which is 565 `0x7BEF` and is
    // colour `0x7F7F7F` through this ramp, and the focused row comes out
    // white. The two differ only in the colour handed in - the glyphs are
    // identical - which is what makes the focus highlight a brightness.
    static void buildRamp(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                          std::uint16_t ramp[32]);

    // Draw one run at a pen position, returning the advance. `y` is the TOP of
    // the line; a glyph sits `height - bottom` below it, which is how the
    // record's `bottom` (relative to the baseline) is consumed.
    //
    // The engine draws into a scratch surface cleared to black and blits that
    // with `I2D_BlitSurface`, so a zero-coverage pixel is transparent through
    // the colour key rather than black. Writing straight into the destination
    // and skipping zero is the same result with one buffer fewer, and it is
    // what makes the glyph pixels independent of whatever is behind them -
    // measured: identical across three captures of an animated screen.
    int drawRun(Surface& dst, int x, int y,
                const std::vector<StyledChar>& run) const;

private:
    const FontTable* table_;
    std::string dir_;
    mutable std::map<char, Font> loaded_;
};

}  // namespace omk
