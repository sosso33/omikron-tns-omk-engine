// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screendraw.h"

#include "ui/iamtext.h"

#include <algorithm>

namespace omk {
namespace {

// `Ui_ItemTextStyle`: an unselected row is dimmed by HALVING all three colour
// channels - one shift, and it is the dimming of every unselected row in the
// game. The captures give white for the focused row and 0x7F7F7F for the
// rest, which is that shift (`engine: text draw`).
constexpr std::uint8_t kLit = 255, kDim = 0x7F;

}  // namespace

int ScreenComposer::background(Surface& fb, const UiPanel& p,
                              const Surface& sheet) const {
    if (p.tiles.empty()) {                       // the 0x40004000 full-sheet arm
        // COLOUR-KEYED. `gfxint.bmp`'s whole background is one palette index -
        // 255, rgb(4,4,4) - which is the I2D key, so the sheet is transparent
        // there and the layer beneath shows through. Blitted opaquely the menu
        // comes out on black, which is how it looked until the cloud was
        // found. The key is taken from the sheet's own bottom-left pixel
        // rather than named here: it is the corner every one of these full
        // sheets fills with its background.
        const std::uint16_t key =
            sheet.valid() ? sheet.px[static_cast<std::size_t>(sheet.h - 1) * sheet.w]
                          : std::uint16_t(0);
        blt(fb, {0, 0, fb.w, fb.h}, sheet, {0, 0, sheet.w, sheet.h},
            kBltWait | kBltKeySrc, key);
        return 0;
    }
    int drawn = 0;
    for (int cell = 0; cell < static_cast<int>(p.tiles.size()); ++cell) {
        const int id = p.tiles[cell];
        if (id < 0) continue;                    // ids are SIGNED; negative skips
        const int dx = (cell % 10) * 64, dy = (cell / 10) * 64;
        const int sx = (id % 10) * 64,   sy = (id / 10) * 64;
        // Row 7 is the case the drawer hard-codes: seven rows of 64 leave 32,
        // so it draws at half height from source y 448..480.
        const int hgt = (cell / 10 == 7) ? 32 : 64;
        blt(fb, {dx, dy, dx + 64, dy + hgt},
            sheet, {sx, sy, sx + 64, sy + hgt}, false, 0);
        ++drawn;
    }
    return drawn;
}

ScreenFrame ScreenComposer::draw(Surface& fb, int screenId,
                                 const UiWalk& walk) const {
    ScreenFrame out;
    const UiPanel* p = w_->screen(screenId);
    if (!p) return out;

    // The animated background goes down FIRST, and the screen's sheet is
    // colour-keyed over it.
    if (cloud_ && cloud_->valid()) {
        if (fb.w == 640 && fb.h == 480) {
            cloud_->draw(fb, frame_);
        } else {
            // `sub_4B19C0` makes its surface 0x280 x 0x1E0 whatever the
            // display is, so the effect is always computed at 640x480 and
            // blitted out. Its two warp tables are 640 and 480 entries in the
            // image, which is the same statement from the other side.
            Surface c(640, 480, 0);
            cloud_->draw(c, frame_);
            blt(fb, {0, 0, fb.w, fb.h}, c, {0, 0, 640, 480}, kBltWait);
        }
        out.cloudDrawn = true;
    }

    const std::string& art = w_->bitmap(screenId);
    if (!art.empty()) {
        const Surface sheet = surfaceFromBmp(fs_->read("I2D/bitmaps/" + art));
        if (sheet.valid()) {
            out.tilesDrawn = background(fb, *p, sheet);
            out.fullSheet  = p->tiles.empty();
        }
    }

    // The screen's own strings. `Ui_DrawItem` takes the item's `+28` as an
    // index into `IAM\<name>`, and the open callback is what wrote that index
    // - the record ships -1 (docs/UI.md 3d), which is why the bindings had to
    // be lifted before any of this could show a word.
    // `iamStrings` reads its argument as a path relative to the DataFs it is
    // handed, and the reference callers root theirs at `gamedata/IAM`. This one is
    // rooted at the game directory, so the archive name has to be prefixed -
    // getting that wrong returns an empty list and draws a screen with no
    // labels, which looks exactly like the missing-bindings symptom.
    const std::string& tf = w_->textFile(screenId);
    const std::vector<std::string> text =
        tf.empty() ? std::vector<std::string>{} : iamStrings(*fs_, "IAM/" + tf);

    // The panels to draw, outermost first. The walk may have descended into a
    // child - the confirm dialog, the name field - and the engine draws those
    // OVER what they came from rather than instead of it, so the chain is
    // walked up to the screen's own panel and then drawn in reverse.
    // Only the panel the walk is ON. Drawing the chain from the screen down -
    // the first guess - puts the dialog's rows on top of the menu's at the
    // same coordinates and both come out as overlapping glyphs, which is
    // worse than not drawing it. A child panel REPLACES its parent's rows
    // here; whether the engine dims or keeps the parent behind is not
    // established, and no capture of this dialog exists to settle it.
    std::vector<const UiPanel*> chain{walk.panel() ? walk.panel() : p};
    out.panelsDrawn = 1;

    const UiItem* focused = walk.selected();
    for (const UiPanel* q : chain)
    for (const auto& l : q->lists) {
        if (l.hidden()) continue;
        // THE NAME FIELD. Its item carries no string - the record's label is
        // -1 - because what it shows is what the PERSON typed, not a line out
        // of `IAM\<screen>`. The list is identified by its hook, which is the
        // widget table's own `nameHook`, so this is not a guess about which
        // box it is. Without it the field is invisible: a player types and
        // nothing appears, which is most of why the start menu could not be
        // got past.
        if (l.hook == w_->nameHook() && !walk.name().empty() && !l.items.empty()) {
            const UiItem& f = l.items.front();
            const auto run = parseMarkup(walk.name(), 'I', kLit, kLit, kLit).run;
            lay_->drawRun(fb, scaleX(f.x + q->offsetX),
                          scaleY(f.y + q->offsetY), run);
            ++out.itemsDrawn;
        }
        for (const auto& it : l.items) {
            // A row the RUNTIME filled beats the record's string id. The
            // sneak's nine inventory rows are the case: their `+28` is -1 and
            // stays -1, and what they show is the carried object list.
            const std::string* run_ = nullptr;
            if (rows_) {
                const auto r = rows_->find(it.addr);
                if (r != rows_->end() && !r->second.empty()) run_ = &r->second;
            }
            const int id = it.label();
            if (!run_ && (id < 0 || id >= static_cast<int>(text.size()))) continue;
            const std::string& s =
                run_ ? *run_ : text[static_cast<std::size_t>(id)];
            if (s.empty()) continue;
            const bool lit = focused && focused->addr == it.addr;
            const std::uint8_t v = lit ? kLit : kDim;
            // Face `I` is MENUINTR, the row the compiled font table names for
            // the menus - and finding that was what took `uitext.py` from
            // Latin letters against alien glyphs to IoU 1.000.
            const auto run = parseMarkup(s, 'I', v, v, v).run;

            // `Ui_ItemTextStyle` (0x004769A0) maps the item's BANK 2 bits to
            // `Text_DrawBlock`'s alignment, and the mapping is NOT the
            // identity - the ladder is
            //
            //     0x80000004 -> 2 (left)    0x80000010 -> 8  (CENTRED)
            //     0x80000008 -> 4 (right)   0x80000020 -> 0x10
            //
            // and it is the EFFECTIVE flags that matter, not the record: the
            // start menu's four buttons store bank 2 as ZERO and are centred
            // by one broadcast of `0x80000010` over the list. Drawn from the
            // record alone they sit hard against the left edge of a 640-wide
            // row, which is exactly what this composed before the ladder was
            // read (docs/UI.md 5).
            std::uint32_t eff[3];
            it.effective(l.broadcast, eff);
            const int width = lay_->measure(run);
            // `Ui_DrawItem` scales the item's BOX and hands that to
            // `Text_DrawBlock`, which aligns inside it:
            //
            //     v6  = I2D_ScaleX(x);      v16 = I2D_ScaleX(x + w);
            //     v14 = I2D_ScaleY(y);      v18 = I2D_ScaleY(y + h);
            //     Text_DrawBlock(v6, v14, v16, v18, text, style)
            //
            // so the alignment is worked out against the SCALED width with a
            // native-size glyph run. Centring in the 640-wide design space and
            // scaling the result instead pulls every centred row left of
            // centre as the display grows.
            const int x0 = scaleX(it.x + q->offsetX);
            const int x1 = scaleX(it.x + q->offsetX + it.w);
            int x = x0;
            if (eff[2] & 0x10)      x += (x1 - x0 - width) / 2;   // centred
            else if (eff[2] & 0x08) x += (x1 - x0) - width;       // right
            const int y = scaleY(it.y + q->offsetY);
            // `drawRun` returns the pen ADVANCE, not a pixel count - the
            // width the row occupies. Named for what it is.
            out.textAdvance += lay_->drawRun(fb, x, y, run);
            out.centred += (eff[2] & 0x10) ? 1 : 0;
            ++out.itemsDrawn;
        }
    }
    for (auto px : fb.px) if (px) ++out.painted;
    std::uint32_t h = 2166136261u;
    for (auto px : fb.px) {
        h = (h ^ (px & 0xFF)) * 16777619u;
        h = (h ^ (px >> 8)) * 16777619u;
    }
    out.hash = h;
    return out;
}

}  // namespace omk
