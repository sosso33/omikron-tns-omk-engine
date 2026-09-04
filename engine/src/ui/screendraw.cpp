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
    // THE DESTINATION IS SCALED AND THE SOURCE IS NOT, and getting that
    // backwards is invisible at 640x480 and ruins every other resolution.
    // `Ui_DrawPanelBack` (0x00476040):
    //
    //     dst = (col * I2D_ScaleX(64), row * I2D_ScaleY(64), ...)
    //     src = ((id % 10) << 6, (id / 10) << 6, ...)
    //
    // so ten columns of `ScaleX(64)` cover the display whatever its width,
    // while the source keeps reading 64-pixel cells out of a 640x480 sheet.
    // This used a literal 64 for BOTH: at the player's default 800x600 the
    // background covered the top-left 640x480 and the world showed through
    // on the right and bottom, while every widget - which does go through
    // `I2D_ScaleX/Y` - sat somewhere else entirely. Reported as "the sneak
    // interface is supposed to take all the screen".
    //
    // A 640x480 test cannot see this: there `ScaleX(64) == 64`. The check
    // has to compose at a second resolution, which `engine: screen scale`
    // now does.
    int drawn = 0;
    for (int cell = 0; cell < static_cast<int>(p.tiles.size()); ++cell) {
        const int id = p.tiles[cell];
        if (id < 0) continue;                    // ids are SIGNED; negative skips
        const int col = cell % 10, row = cell / 10;
        const int dx0 = col * scaleX(64), dx1 = (col + 1) * scaleX(64);
        // Row 7 is the case the drawer hard-codes: seven rows of 64 leave 32,
        // so it draws at half height from source y 448..480 - and the
        // engine writes that as `7 * ScaleY(64) + ScaleY(64) / 2`.
        const int dy0 = row * scaleY(64);
        const int dy1 = (row == 7) ? 7 * scaleY(64) + scaleY(64) / 2
                                   : (row + 1) * scaleY(64);
        const int sx = (id % 10) * 64, sy = (id / 10) * 64;
        const int sh = (row == 7 || id / 10 == 7) ? 32 : 64;
        const int sy0 = (id / 10 == 7) ? 448 : sy;
        blt(fb, {dx0, dy0, dx1, dy1},
            sheet, {sx, sy0, sx + 64, sy0 + sh}, false, 0);
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

    // THE ARTWORK, hoisted: the background AND every sprite item come out of
    // the same sheet. `Ui_DrawItemSprite` blits from `screen+56`, which is
    // the surface `UI_LoadScreen` put the bitmap in - the same one
    // `Ui_DrawPanelBack` tiles - so the icons are cut from `sneak.bmp`
    // itself rather than from any separate atlas.
    const std::string& artName = w_->bitmap(screenId);
    Surface art;
    if (!artName.empty()) art = surfaceFromBmp(fs_->read("I2D/bitmaps/" + artName));
    const bool sheetOk = art.valid();
    // The I2D colour key, taken from the sheet's own bottom-left pixel the
    // way the full-sheet path already does rather than named here.
    const std::uint16_t artKey =
        sheetOk ? art.px[static_cast<std::size_t>(art.h - 1) * art.w]
                : std::uint16_t(0);
    if (sheetOk) {
        out.tilesDrawn = background(fb, *p, art);
        out.fullSheet  = p->tiles.empty();
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

    const UiItem* sel = walk.selected();
    // The panel's CURRENT list - `Ui_DrawList` only marks a row FOCUSED when
    // its list is the one `panel+24` names.
    const UiPanel* wp = walk.panel() ? walk.panel() : p;
    const UiList* curList =
        (walk.currentList() >= 0 &&
         static_cast<std::size_t>(walk.currentList()) < wp->lists.size())
            ? &wp->lists[static_cast<std::size_t>(walk.currentList())] : nullptr;
    for (const UiPanel* q : chain)
    for (const auto& l : q->lists) {
        // THE DRAW GATE IS NOT THE WALK'S. `Ui_DrawPanel` skips a list on
        // bank B `0x40000001` (`sub_429080(list, 1073741825)`);
        // `Ui_MoveBetweenLists` skips it on `+16 & 4`. This used the walk's
        // flag, so it hid the sneak's bottom bar - which is `+16 = 0x20000004`
        // and `+20 = 0`: drawn, and deliberately not navigable.
        if (!l.drawn()) continue;
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
            std::uint32_t eff0[3];
            it.effective(l.broadcast, eff0);
            // The item's own draw gate, the same `0x40000001` the list has.
            if (eff0[1] & 1) continue;

            // ---- IS THIS ITEM SELECTED, AND IS IT FOCUSED ------------
            //
            // `Ui_DrawList` sets two flags before drawing each row:
            // `0x20000002` UIF_SELECTED on the row the list's `+2` names,
            // and `0x20000001` UIF_FOCUSED as well when that list is the
            // panel's CURRENT one - so exactly one item on screen is
            // focused, which is what `screen+40` caches for the input code.
            const int selIdx = walk.selectionOf(l);
            const bool isSel = selIdx >= 0 &&
                static_cast<std::size_t>(selIdx) < l.items.size() &&
                l.items[static_cast<std::size_t>(selIdx)].addr == it.addr;
            const bool isFocus = isSel && &l == curList;

            // ---- THE LIT LADDER, and it is where the FLASHING lives ----
            //
            // `Ui_DrawItemSprite` (0x00476E60) and `Ui_ItemTextStyle` share
            // it exactly:
            //
            //     0x40000008 -> lit
            //     0x40000004 -> lit = Ui_Oscillator(1).value      (pulse)
            //     0x40000002 -> not SELECTED          : unlit
            //                   SELECTED, not FOCUSED : lit
            //                   SELECTED and FOCUSED  : oscillator 1  <- FLASH
            //     otherwise  -> lit = SELECTED
            //
            // Oscillator 1 ships period 500, flags 3, and its completion
            // (`sub_42B7B0`) does `osc[6] = (osc[6] == 0)` - a square wave
            // toggling 0/1 every 500 ms. That is the user's "flashing icon
            // to indicate the selection", read rather than invented.
            const bool blink = ((clockMs_ / 500) & 1) != 0;
            bool lit;
            if (eff0[1] & 8)      lit = true;
            else if (eff0[1] & 4) lit = blink;
            else if (eff0[1] & 2) lit = isFocus ? blink : isSel;
            else                  lit = isSel;

            // ---- THE FILL: read, TRIED, and NOT DRAWN ------------------
            //
            // `Ui_DrawItemFill` (0x00476FE0) puts a quad over the item's own
            // scaled rect, and the amber bars behind the sneak's rows and its
            // two strips are where it goes. The colour is packed
            // `(alpha << 24) | (+8 << 16) | (+9 << 8) | +10` with alpha 200
            // on the plain `0x40000010` arm - all read, and lifted as
            // `rgb`/`layer`.
            //
            // **It was implemented, rendered, and refuted by the picture.**
            // Two things the reading does not have:
            //
            //  * the BLEND. `sub_4285E0`'s mode comes from
            //    `sub_464740() ? 4 : 0`, a runtime query into the I2D back
            //    end that is not traced. The sneak's rows carry (255, 0, 0),
            //    and source-over at alpha 200 paints them PURE RED where the
            //    original's bars are amber. So mode 4 is not source-over, and
            //    what it is has to be read before this can be drawn.
            //  * the per-row GATE. The original fills only the rows that
            //    HOLD an object - two of nine in the capture - while every
            //    one of the nine carries the flag statically. Something
            //    clears it per row at runtime, and that something is the
            //    sneak's own list code (`sub_0049C050`'s neighbourhood),
            //    which is the same unread function that owns the scrolling.
            //
            // Drawing it on the strength of the colour alone gives nine red
            // bars where the game shows two amber ones - which looks like a
            // rendering bug and is really an unread blend. Left out until
            // both halves are read; the data is in the table for whoever
            // reads them.

            // ---- THE SPRITE: `Ui_DrawItemSprite` -----------------------
            //
            // The lit source at `+12/+14` or the unlit one at `+16/+18`,
            // each `w x h`, blitted from the screen's own artwork - so the
            // sneak's five left-hand icons are cut out of `sneak.bmp`.
            // Source raw, destination scaled, exactly as the background.
            //
            // The sneak's tab column carries `0x40000302`: sprite, cursor,
            // and lit-on-selection. Drawing its `+28` as TEXT instead - what
            // this did - printed five labels the game has never shown.
            if ((eff0[1] & 0x100) && sheetOk) {
                const int* src = lit ? it.lit : it.unlit;
                const int x0 = it.x + q->offsetX, y0 = it.y + q->offsetY;
                blt(fb, {scaleX(x0), scaleY(y0),
                         scaleX(x0 + it.w), scaleY(y0 + it.h)},
                    art, {src[0], src[1], src[0] + it.w, src[1] + it.h},
                    kBltWait | kBltKeySrc, artKey);
                ++out.spritesDrawn;
            }

            // ---- AN ITEM SHOWS TEXT ONLY IF SOMETHING GIVES IT ANY -----
            //
            // `Ui_DrawItem` reads `+24` (a resolved `char *`) and, failing
            // that, calls `+32`. It NEVER reads `+28`. An item with both
            // zero draws nothing, whatever string id it carries - 111 of
            // the tree's 572 items are exactly that, and this drew every
            // one of them. The sneak's six tab icons and its three 50x50
            // buttons are in that set, which is the whole of the user's
            // "some of the texts are parts of sub-menu and should not be
            // displayed at anytime": they are not sub-menu texts, they are
            // strings belonging to a widget that does not draw text.
            //
            // Of the callbacks, two are modelled: `0x00476860` draws the
            // item's own `+28`, and `0x0042AA00` is the inventory row,
            // which asks the channel for a name and arrives here as
            // `rows_`. The other thirteen are native and draw nothing.
            const std::string* run_ = nullptr;
            if (rows_) {
                const auto r = rows_->find(it.addr);
                if (r != rows_->end() && !r->second.empty()) run_ = &r->second;
            }
            const bool ownString = it.textFn == kTextFnString;
            if (!run_ && !ownString) continue;
            const int id = it.label();
            if (!run_ && (id < 0 || id >= static_cast<int>(text.size()))) continue;
            const std::string& s =
                run_ ? *run_ : text[static_cast<std::size_t>(id)];
            if (s.empty()) continue;
            const std::uint8_t v = lit ? kLit : kDim;
            // THE FACE IS THE ITEM'S OWN, `+36`, not one hard-coded here.
            //
            // This drew everything in `I` (MENUINTR) because that is the
            // start menu's face and the start menu is what was measured. It
            // is right there for a reason the record states: screen 29's
            // four buttons carry font **73**, which IS 'I' - so the tier-4
            // agreement with the engine's own capture never depended on the
            // hard-coding and does not move now.
            //
            // The SNEAK is where it showed. Its verbs, rows and echo bar name
            // **74** ('J', JOURNAL) and its clock **67** ('C'), and JOURNAL
            // is two pixels shorter in the line than MENUINTR - so the port
            // drew the whole device in the menu's face at the menu's leading.
            // Reported as "the font used is not the one of the main menu",
            // which is the complaint from the other side: it should not be,
            // and it was.
            //
            // 255 means the record names none, and `Text_DrawBlock`'s own
            // global default is 74.
            const auto run = parseMarkup(s, it.face('J'), v, v, v).run;

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
