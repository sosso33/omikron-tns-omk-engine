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
                        const std::vector<StyledChar>& run) const {
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

}  // namespace omk
