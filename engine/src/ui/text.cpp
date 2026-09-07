// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/text.h"

#include "platform/datafs.h"
#include "platform/json.h"

#include <algorithm>
#include <cctype>

namespace omk {
namespace {

bool digits(const std::string& s, std::size_t at, int n) {
    if (at + static_cast<std::size_t>(n) > s.size()) return false;
    for (int k = 0; k < n; ++k)
        if (!std::isdigit(static_cast<unsigned char>(s[at + static_cast<std::size_t>(k)])))
            return false;
    return true;
}

int num(const std::string& s, std::size_t at, int n) {
    int v = 0;
    for (int k = 0; k < n; ++k) v = v * 10 + (s[at + static_cast<std::size_t>(k)] - '0');
    return v;
}

}  // namespace

FontTable FontTable::loadJson(const std::string& uiTable) {
    FontTable t;
    const Json doc = Json::parseFile(uiTable);      // held: see options.cpp
    const Json& fs = doc["rows"]["fonts"];
    for (std::size_t i = 0; i < fs.size(); ++i) {
        FontFace f;
        f.index  = static_cast<int>(fs[i]["index"].i64());
        f.id     = static_cast<int>(fs[i]["id"].i64());
        const auto l = fs[i]["letter"].str();
        f.letter = l.empty() ? 0 : l[0];
        f.name   = fs[i]["name"].str();
        f.kern           = static_cast<std::int16_t>(fs[i]["kern"].i64());
        f.defaultAdvance = static_cast<std::int16_t>(fs[i]["default_advance"].i64());
        f.height         = static_cast<std::int16_t>(fs[i]["height"].i64());
        t.faces_.push_back(std::move(f));
    }
    return t;
}

const FontFace* FontTable::byLetter(char c) const {
    for (const auto& f : faces_) if (f.letter == c) return &f;
    return nullptr;
}

ParsedText parseMarkup(const std::string& text, char face,
                       std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    ParsedText out;
    char curF = face;
    std::uint8_t curC[3] = {r, g, b};
    bool curBlink = false;   // `{B}` until the string says otherwise
    std::size_t i = 0;
    const std::size_t n = text.size();
    while (i < n) {
        const char c = text[i];
        if (c == '{') {
            ++i;
            // Several directives chain inside ONE brace, so this loops to the
            // closing `}` rather than reading a single command.
            while (i < n && text[i] != '}') {
                const char d = text[i];
                if (d == 'f' && i + 1 < n) { curF = text[i + 1]; i += 2; continue; }
                if (d == 'I' && digits(text, i + 1, 9)) {
                    // THREE 3-DIGIT components, not a hex triple
                    curC[0] = static_cast<std::uint8_t>(num(text, i + 1, 3));
                    curC[1] = static_cast<std::uint8_t>(num(text, i + 4, 3));
                    curC[2] = static_cast<std::uint8_t>(num(text, i + 7, 3));
                    i += 10;
                    continue;
                }
                if (d == 'X' && digits(text, i + 1, 6)) {
                    // RECORDED, not skipped. Three digits of x then three of
                    // y, each a percentage of the screen.
                    ParsedText::Move mv;
                    mv.at   = out.run.size();
                    mv.xPct = num(text, i + 1, 3);
                    mv.yPct = num(text, i + 4, 3);
                    out.moves.push_back(mv);
                    i += 7;
                    continue;
                }
                // An alignment applies to the block the last move opened, and
                // arrives in a LATER brace than the move itself.
                const auto setAlign = [&](int a) {
                    out.align = a;
                    if (!out.moves.empty()) out.moves.back().align = a;
                };
                if (d == 'G') { setAlign(kAlignLeft);    ++i; continue; }
                if (d == 'D') { setAlign(kAlignRight);   ++i; continue; }
                if (d == 'C') { setAlign(kAlignCentre);  ++i; continue; }
                if (d == 'F') { setAlign(kAlignJustify); ++i; continue; }
                if (d == 'B') { curBlink = true; ++i; continue; }
                if (d == 'H' || d == 'L' || d == 'M' || d == 'g') {
                    ++i; continue;
                }
                if (d == 'E') { i += 2; continue; }
                ++i;                                   // anything else: ignored
            }
            ++i;                                       // the closing brace
            continue;
        }
        if (c == '[' || c == ']') { ++i; continue; }    // counted spans
        StyledChar sc;
        sc.ch = c;
        sc.face = curF;
        sc.blink = curBlink;
        for (int k = 0; k < 3; ++k) sc.rgb[k] = curC[k];
        out.run.push_back(sc);
        ++i;
    }
    return out;
}

const Font* TextLayout::face(char letter) const {
    const auto it = loaded_.find(letter);
    if (it != loaded_.end()) return it->second.valid ? &it->second : nullptr;
    const auto* f = table_->byLetter(letter);
    if (!f) return nullptr;
    const DataFs fs(dir_);
    auto fn = readFnt(fs.read(f->name + ".FNT"));
    const auto& stored = loaded_.emplace(letter, std::move(fn)).first->second;
    return stored.valid ? &stored : nullptr;
}

int TextLayout::measure(const std::vector<StyledChar>& run) const {
    int w = 0;
    for (const auto& sc : run) {
        const auto* rec = table_->byLetter(sc.face);
        if (!rec) continue;
        const Font* f = face(sc.face);
        const auto code = static_cast<unsigned char>(sc.ch);
        // the glyph's own width, or the FACE's default when the file has no
        // glyph for this code - then the face's kerning, on every advance
        int adv = rec->defaultAdvance;
        if (f && f->glyphs[code].present) adv = f->glyphs[code].width;
        w += adv + rec->kern;
    }
    return w;
}

int TextLayout::measure(const std::string& text, char f) const {
    return measure(parseMarkup(text, f).run);
}

int TextLayout::height(const std::vector<StyledChar>& run) const {
    int h = 0;
    for (const auto& sc : run)
        if (const auto* rec = table_->byLetter(sc.face))
            h = std::max<int>(h, rec->height);
    return h;
}


// ------------------------------------------------------- the rasteriser

void TextLayout::buildRamp(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                           std::uint16_t ramp[32]) {
    // One accumulator per channel, stepping by that channel's value and
    // divided by 31 - the engine does the division with the `0x08421085`
    // magic-multiply, which TRUNCATES, so plain integer division matches it.
    // Entry 31 is therefore the colour itself and entry 0 is black.
    unsigned ar = 0, ag = 0, ab = 0;
    for (int i = 0; i < 32; ++i) {
        ramp[i] = rgb565(static_cast<int>(ar / 31), static_cast<int>(ag / 31),
                         static_cast<int>(ab / 31));
        ar += r; ag += g; ab += b;
    }
}

int TextLayout::drawRun(Surface& dst, int x, int y,
                        const std::vector<StyledChar>& run,
                        int clipTop, int clipBottom) const {
    int pen = x;
    std::uint16_t ramp[32];
    std::uint8_t cur[3] = {0, 0, 0};
    bool haveRamp = false;
    for (const auto& sc : run) {
        const auto* rec = table_->byLetter(sc.face);
        if (!rec) continue;
        const Font* f = face(sc.face);
        const auto code = static_cast<unsigned char>(sc.ch);
        int adv = rec->defaultAdvance;
        if (f && f->glyphs[code].present) adv = f->glyphs[code].width;

        // the ramp is rebuilt only when the colour changes, as the engine's
        // `cmp word_4C6F54, ax` cache does
        if (!haveRamp || cur[0] != sc.rgb[0] || cur[1] != sc.rgb[1] ||
            cur[2] != sc.rgb[2]) {
            buildRamp(sc.rgb[0], sc.rgb[1], sc.rgb[2], ramp);
            cur[0] = sc.rgb[0]; cur[1] = sc.rgb[1]; cur[2] = sc.rgb[2];
            haveRamp = true;
        }

        if (f && f->glyphs[code].present) {
            const Glyph& gl = f->glyphs[code];
            const auto cov = f->coverage(code);
            if (!cov.empty()) {
                // `bottom` is the glyph's lower edge relative to the baseline,
                // so its top sits `height - bottom` below the line's top. The
                // face's own height is the line box.
                // the baseline is the line's top plus the FACE's height, and
                // `bottom` is the glyph's lower edge relative to it - so it
                // ADDS. Subtracting instead put every row eight pixels low and
                // stretched the block, which is what the first version did.
                const int top = y + rec->height + gl.bottom - gl.height;
                for (int gy = 0; gy < gl.height; ++gy) {
                    const int py = top + gy;
                    if (py < 0 || py >= dst.h) continue;
                    if (py < clipTop || py >= clipBottom) continue;
                    for (int gx = 0; gx < gl.width; ++gx) {
                        const auto c = static_cast<std::uint8_t>(
                            cov[static_cast<std::size_t>(gy) * gl.width + gx]);
                        if (!c) continue;              // zero is transparent
                        const int px = pen + gx;
                        if (px < 0 || px >= dst.w) continue;
                        dst.set(px, py, ramp[c & 31]);
                    }
                }
            }
        }
        pen += adv + rec->kern;
    }
    return pen - x;
}


// ------------------------------------------- `Text_LayOutBlock` (0x0043F3E0)
//
// 577 lines in the listing, one caller (`Text_DrawBlock`), and the last piece
// of the interface's text path this port had a GUESS in place of: a greedy
// break at spaces, a line advance of `height + 2` and a blank line of 12. The
// engine's own algorithm, transcribed:
//
// * the VERTICAL placement runs before a character is read - `0x800` bottom,
//   `0x1000` middle, neither top - against the CURRENT font's line height
//   (record `+12`), and the markup letters `H` / `L` / `M` redo the same three
//   at any point;
// * the WRAP accumulates `Text_GlyphAdvance` per character; a SPACE remembers
//   both the run position and the input position, and a character that no
//   longer fits cuts the line back to that space and rewinds the input to just
//   after it. With NO space seen the character is kept anyway, so a single
//   word wider than the box overflows rather than breaking;
// * the LINE ADVANCE is `120 * lineHeight / 100`, and a blank line advances by
//   the same rather than by a constant;
// * a line is FLUSHED when a newline is pending or when the alignment or
//   vertical bits changed (`(style ^ working) & 0x1C1E`). That is why `{F}`
//   breaks a line and `{C}` does not - and why `{P}`, which is not a directive
//   at all, works as a paragraph break: an unrecognised letter falls into the
//   same test;
// * `[` and `]` are COUNTED, and the span whose index equals `span` swaps in
//   the alternate colour, font, style and `E` value;
// * `{B}` TOGGLES blink (`v57 ^= 0x4000`), and a blinking character on a high
//   oscillator is written (255, 0, 0) - red, which this port had measured off
//   two captures before it could read it here.
//
// The engine emits one `Text_DrawRun` per span of constant style; this emits
// one `drawRun` per LINE, which is the same pixels because `drawRun` already
// carries the face and the colour per character and rebuilds its ramp on a
// change - the engine's own `cmp word_4C6F54, ax` cache.
int TextLayout::layOutBlock(Surface* dst, const std::string& text,
                            const TextBlock& b) const {
    // `if (!*a1) return dword_907A18` - the engine returns the box's TOP for
    // an empty string, not 0, where every other path returns a height. Kept:
    // its one consumer subtracts the box height and floors at 0.
    if (text.empty()) return b.top;

    const auto lineHeight = [&](char f) -> int {
        const auto* rec = table_->byLetter(f);
        return rec ? rec->height : 0;
    };
    const auto advance = [&](unsigned char ch, char f) -> int {
        const auto* rec = table_->byLetter(f);
        if (!rec) return 0;
        const Font* fn = face(f);
        int adv = rec->defaultAdvance;
        if (fn && fn->glyphs[ch].present) adv = fn->glyphs[ch].width;
        return adv + rec->kern;
    };

    int   left = b.left, top = b.top;               // 907A14 / 907A18, movable
    char  font = b.font;                            // 907A10
    int   style = b.style;                          // 907A00, the LINE's
    int   working = b.style;                        // v57, the pending one
    std::uint8_t rgb[3] = {b.rgb[0], b.rgb[1], b.rgb[2]};
    int   eValue = b.eValue;

    // The bracket save slots (v62/v60/v61, v76, v75, v77), seeded from the
    // entry state exactly as the engine seeds them.
    std::uint8_t saveRgb[3] = {b.rgb[0], b.rgb[1], b.rgb[2]};
    char  saveFont = b.font;
    int   saveStyle = b.style, saveE = b.eValue;
    int   spanIndex = -1;                           // v70

    const int width = b.right - b.left;             // v79
    int   penX = b.left;                            // v73
    int   y;                                        // v6 / v63
    if ((b.style & 0x800) != 0)        y = b.bottom - lineHeight(font);
    else if ((b.style & 0x1000) != 0)  y = top + ((b.bottom - lineHeight(font) - top) >> 1);
    else                               y = top;
    // `v72 = dword_907A18` - seeded from the box's TOP, not from the pen, and
    // only ever raised. A bottom- or middle-placed block therefore reports a
    // height measured from the top of its box.
    int   maxY = b.top;                             // v72

    std::vector<StyledChar> line;                   // the 6-byte run buffer
    bool  lineOpen = false;                         // v58 != 0
    int   lineW = 0;                                // v74
    int   wrapMark = -1;                            // v66: run length at the space
    std::size_t wrapInput = 0;                      // v64: input just after it
    bool  haveWrap = false;
    bool  inBrace = false;                          // v71
    bool  done = false;                             // v68

    std::size_t i = 0;
    const std::size_t n = text.size();
    // The engine walks a NUL-terminated buffer and reads the terminator as a
    // character; `i == n` stands in for that, so the last line flushes.
    while (!done) {
        bool newline = false;                       // v67
        const int c = (i < n) ? static_cast<unsigned char>(text[i]) : 0;
        ++i;

        if (c == '{' && !b.literal)      { inBrace = true;  goto tail; }
        if (c == '}' && !b.literal)      { inBrace = false; goto tail; }
        if (c == '[' && !b.literal) {
            // `if (++v70 == dword_907A24)` - SAVE, then install the alternates
            if (++spanIndex == b.span) {
                saveRgb[0] = rgb[0]; saveRgb[1] = rgb[1]; saveRgb[2] = rgb[2];
                saveFont = font; saveStyle = style; saveE = eValue;
                rgb[0] = b.altRgb[0]; rgb[1] = b.altRgb[1]; rgb[2] = b.altRgb[2];
                font = b.altFont; style = b.altStyle; eValue = b.altE;
            }
            goto tail;
        }
        if (c == ']' && !b.literal) {
            if (spanIndex == b.span) {
                rgb[0] = saveRgb[0]; rgb[1] = saveRgb[1]; rgb[2] = saveRgb[2];
                font = saveFont; style = saveStyle; eValue = saveE;
            }
            goto tail;
        }

        if (inBrace) {
            switch (c) {
            case 0:   done = true; goto test;           // ...and still FLUSH
            case 'B': working ^= 0x4000; goto tail;
            case 'C': working = (working & ~0x1E) | 8;  goto tail;
            case 'D': working = (working & ~0x1E) | 4;  goto tail;
            case 'E': if (i < n) eValue = static_cast<unsigned char>(text[i++]);
                      goto tail;
            case 'F': working = (working & ~0x1E) | 0x10; break;   // ...and TEST
            case 'G': working = (working & ~0x1E) | 2;  goto tail;
            case 'H': working = (working & ~0x1C00) | 0x400;
                      top = 0; y = 0; goto tail;
            case 'I': {
                // THREE 3-DIGIT DECIMALS, not a hex triple
                if (i + 9 <= n) {
                    const auto d3 = [&](std::size_t k) {
                        int v = 0;
                        for (std::size_t j = 0; j < 3 && k + j < n; ++j)
                            v = v * 10 + (text[k + j] - '0');
                        return static_cast<std::uint8_t>(v);
                    };
                    rgb[0] = d3(i); rgb[1] = d3(i + 3); rgb[2] = d3(i + 6);
                    i += 9;
                }
                goto tail;
            }
            case 'L': working = (working & ~0x1C00) | 0x800;
                      y = b.bottom - lineHeight(font); goto tail;
            case 'M': working = (working & ~0x1C00) | 0x1000;
                      y = top + ((b.bottom - lineHeight(font) - top) >> 1);
                      goto tail;
            case 'X': {
                // Six digits: three of x then three of y, each a PERCENTAGE
                // of the screen - and the box's own left/top are pulled back
                // if the move lands above or left of them.
                if (i + 6 <= n) {
                    const auto d3 = [&](std::size_t k) {
                        int v = 0;
                        for (std::size_t j = 0; j < 3; ++j)
                            v = v * 10 + (text[k + j] - '0');
                        return v;
                    };
                    penX = b.screenW * d3(i) / 100;
                    y    = b.screenH * d3(i + 3) / 100;
                    i += 6;
                    if (penX < left) left = penX;
                    if (y < top)     top = y;
                }
                break;                                   // ...and TEST
            }
            case 'f': if (i < n) font = text[i++]; goto tail;
            case 'g': goto tail;                         // read and ignored
            default:  break;                             // ...and TEST
            }
            goto test;
        }

        // ---- the TEXT path ------------------------------------------------
        if (!lineOpen) { lineW = 0; line.clear(); lineOpen = true;
                         haveWrap = false; wrapMark = -1; }
        if (c == 13) goto tail;                          // '\r' is dropped
        if (c == 10) { haveWrap = false; newline = true; goto test; }
        {
            bool keep = true;                            // v34
            const int w = lineW + advance(static_cast<unsigned char>(c), font);
            if (w <= width) {
                if (c == ' ') { wrapMark = static_cast<int>(line.size());
                                wrapInput = i; haveWrap = true; }
                else if (!c)  { keep = false; newline = true;
                                wrapInput = i - 1; haveWrap = true; }
            } else if (haveWrap || c == ' ' || !c) {
                keep = false;
                if (wrapMark >= 0)
                    line.resize(static_cast<std::size_t>(wrapMark));
                newline = true;
            }
            if (keep) {
                StyledChar sc;
                sc.ch = static_cast<char>(c);
                sc.face = font;
                sc.blink = (working & 0x4000) != 0;
                sc.rgb[0] = rgb[0]; sc.rgb[1] = rgb[1]; sc.rgb[2] = rgb[2];
                line.push_back(sc);
            }
            lineW = w;                    // ...whether or not it was kept
        }

    test:
        if (newline || ((style ^ working) & 0x1C1E) != 0) {
            if (lineOpen || wrapMark >= 0) {
                if (!b.measureOnly && !line.empty()) {
                    int x = penX - b.originX;
                    const int runW = measure(line);
                    switch (style & 0x1E) {
                    case 4: x = b.right - runW; break;
                    case 8: x = b.left + (b.right - runW - b.left) / 2; break;
                    default: break;
                    }
                    if (dst) {
                        auto copy = line;
                        if (b.blinkOn)
                            for (auto& sc : copy)
                                if (sc.blink) { sc.rgb[0] = 255; sc.rgb[1] = 0;
                                                sc.rgb[2] = 0; }
                        drawRun(*dst, x, y - b.originY, copy,
                                b.clipTop, b.clipBottom);
                    }
                }
                // The REWIND, and it is what makes the wrap work: the input
                // goes back to just after the space the line was cut at.
                if (haveWrap) {
                    i = wrapInput;
                    haveWrap = false;
                    if (i >= n) done = true;
                } else if (!c) {
                    done = true;
                }
            }
            lineOpen = false;
            line.clear();
            wrapMark = -1;
            style = working;
            if (newline) {
                y += 120 * lineHeight(font) / 100;
                if (y > maxY) maxY = y;
            }
        }
    tail:
        if (i > n) done = true;                 // past the terminator
    }
    // `v72 - dword_907A18`, and `dword_907A18` is MUTABLE: `{H}` zeroes it
    // and `{X}` pulls it back to a move above the box, so the height is
    // measured from wherever the top ended up.
    return maxY - top;
}


}  // namespace omk