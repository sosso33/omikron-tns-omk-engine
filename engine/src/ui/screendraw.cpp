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
// The lit colour when bank C's `0x80000001` forces white. The DIM one is no
// longer a constant: `Ui_ItemTextStyle` halves whatever colour the item
// carries, so `0x7F` was only ever the halving of 255.
constexpr std::uint8_t kLit = 255;

// `Ui_DrawItemFill`'s quad, with the blend `sub_480AC0`'s mode-4 arm sets:
// SRCBLEND = INVSRCALPHA (6) and DESTBLEND = SRCALPHA (5), the INVERSE of the
// usual source-over, so
//
//     result = src * (1 - a) + dst * a
//
// and a large alpha makes the source FAINT rather than solid. The plain
// `0x40000010` arm passes alpha 200, so a fill contributes 0.216 of its own
// colour over whatever is beneath.
//
// **Confirmed against the original.** The LIFT's description panel is a fill
// at (15, 360) 475x105 whose record colour is (80, 122, 118), over artwork
// that is black there. This predicts (17.3, 26.3, 25.5); a player's
// screenshot of the running game measures **(15, 25, 25)**. That is the rule
// checked against the game rather than against this repo, and it is what
// took the fill from refused to ported.
void fillQuad(Surface& fb, int x0, int y0, int x1, int y1,
              int r, int g, int b, int alpha) {
    if (!fb.valid()) return;
    x0 = std::max(0, x0); y0 = std::max(0, y0);
    x1 = std::min(fb.w, x1); y1 = std::min(fb.h, y1);
    const int keep = alpha;              // dst weight
    const int add  = 255 - alpha;        // src weight
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            std::uint16_t& d = fb.px[static_cast<std::size_t>(y) * fb.w + x];
            const int dr = ((d >> 11) & 31) * 255 / 31;
            const int dg = ((d >> 5) & 63) * 255 / 63;
            const int db = (d & 31) * 255 / 31;
            const int nr = (r * add + dr * keep) / 255;
            const int ng = (g * add + dg * keep) / 255;
            const int nb = (b * add + db * keep) / 255;
            d = static_cast<std::uint16_t>(((nr * 31 / 255) << 11) |
                                           ((ng * 63 / 255) << 5) |
                                            (nb * 31 / 255));
        }
    }
}

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
            // The item's own draw gate, the same `0x40000001` the list has -
            // from the record, and from the RUNTIME for the lists whose
            // widgets are a window onto something longer (`sub_42AAE0`).
            if (eff0[1] & 1) continue;
            if (hidden_ && hidden_->count(it.addr)) continue;

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

            // ---- THE TWO LIT LADDERS, and they are NOT the same ladder --
            //
            // `docs/UI.md` says `Ui_DrawItemSprite` "repeats the same ladder"
            // as `Ui_ItemTextStyle`. Read side by side they agree on the
            // first two rungs and differ on the other two, so a drawer that
            // shares one ladder lights the wrong things:
            //
            //   both      0x40000008 -> lit
            //             0x40000004 -> Ui_Oscillator(1)          (pulse)
            //   SPRITE    0x40000002 -> !sel: unlit
            //                           sel & !focus: lit
            //                           sel &  focus: oscillator  <- FLASH
            //             otherwise  -> lit = SELECTED
            //   TEXT      0x40000002 -> !sel: unlit
            //                           sel: oscillator
            //             otherwise  -> lit = SELECTED **and** FOCUSED
            //
            // (`sub_476E60` LABEL_4/10/11 against `sub_4769A0` LABEL_6/12/13.)
            //
            // Oscillator 1 ships period 500, flags 3, and its completion
            // `sub_42B7B0` does `osc[6] = (osc[6] == 0)` - a square wave
            // toggling every 500 ms. That is the user's "flashing icon to
            // indicate the selection", read rather than invented.
            //
            // The sneak's own text carries none of the three bits, so it
            // takes the TEXT default: the current list's selected row is
            // white and every other row is dimmed. Its tab icons carry
            // `0x2`, so the focused tab pulses.
            const bool blink = ((clockMs_ / 500) & 1) != 0;
            bool litSprite, litText;
            if (eff0[1] & 8)      { litSprite = litText = true; }
            else if (eff0[1] & 4) { litSprite = litText = blink; }
            else if (eff0[1] & 2) {
                litSprite = !isSel ? false : (isFocus ? blink : true);
                litText   = isSel && blink;
            } else {
                litSprite = isSel;
                litText   = isSel && isFocus;
            }
            const bool lit = litSprite;

            // THE ITEM'S COLOUR, and the record is not always it. A page
            // builder writes `+8/+9/+10` at run time through `sub_4296D0`,
            // which is why 209 of the 222 fill items ship the (255, 0, 0)
            // placeholder: `UiWalk::itemColour` returns what the builder
            // wrote, and null where nothing did.
            const int* over = walk.itemColour(it.addr);
            const int rgb[3] = {over ? over[0] : it.rgb[0],
                                over ? over[1] : it.rgb[1],
                                over ? over[2] : it.rgb[2]};

            // ---- THE FILL: `Ui_DrawItemFill` (0x00476FE0) --------------
            //
            // A quad over the item's own scaled rect. The colour is the
            // item's `+8/+9/+10` - or (255, 50, 50) on the `0x42000000`
            // arm - and the alpha comes from which arm runs: **200** for the
            // plain `0x40000010`, 100 for `0x44000000`, `+9` otherwise.
            // `fillQuad` carries the blend and the evidence for it.
            //
            // **Two things about it were wrong for three attempts.** The
            // blend is the INVERSE of source-over, so a big alpha makes the
            // quad faint, not solid - drawn the usual way round the sneak's
            // rows came out bright red. And the per-row gate is real:
            // `sub_42AAE0` sets `0x40000001` on every widget past the object
            // count, so the engine fills only the rows that hold something,
            // which is why a capture shows two bars of nine.
            //
            // AND THE PLACEHOLDER IS ANSWERED, 2026-09-04. 209 of the
            // 222 fill items ship (255, 0, 0) because a page BUILDER writes
            // the real colour at run time - `sub_4296D0` over a whole list,
            // handed the page's own tab-icon colour. `rgb` above is what it
            // wrote; `UiWalk::buildPage` and `docs/UI.md` carry the rule and
            // the five captures that confirm it.
            if (eff0[1] & 0x10) {
                int fr = rgb[0], fg = rgb[1], fb2 = rgb[2];
                if (eff0[1] & 0x02000000) { fr = 255; fg = 50; fb2 = 50; }
                const int alpha = (eff0[1] & 0x10) ? 200
                                : (eff0[1] & 0x04000000) ? 100 : rgb[1];
                const int x0 = scaleX(it.x + q->offsetX);
                const int y0 = scaleY(it.y + q->offsetY);
                const int x1 = scaleX(it.x + q->offsetX + it.w);
                const int y1 = scaleY(it.y + q->offsetY + it.h);
                fillQuad(fb, x0, y0, x1, y1, fr, fg, fb2, alpha);
                ++out.fillsDrawn;
            }

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

            // ---- THE TEXT COLOUR IS THE ITEM'S OWN --------------------
            //
            // `Ui_ItemTextStyle` (0x004769A0) takes `+8/+9/+10` as the
            // colour - unless bank C carries `0x80000001`, which forces
            // white - and **halves all three when the row is not lit**.
            // That halving is the `>>= 1` this file already had as `kDim`;
            // what it did not have is the colour it halves.
            //
            // 233 of the 275 text items carry the white flag, the sneak's
            // among them - so white-and-grey was right there by luck. The
            // other 42 use their own, and NINE of those are actually
            // coloured: eight at (254, 68, 20) and one at (255, 100, 70),
            // on the terminal and SURV screens. Those nine have been drawn
            // white since the composer existed.
            std::uint8_t cr = 255, cg = 255, cb = 255;
            if (!(eff0[2] & 1)) {
                cr = static_cast<std::uint8_t>(rgb[0]);
                cg = static_cast<std::uint8_t>(rgb[1]);
                cb = static_cast<std::uint8_t>(rgb[2]);
            }
            if (!litText) { cr >>= 1; cg >>= 1; cb >>= 1; }
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
            const auto run = parseMarkup(s, it.face('J'), cr, cg, cb).run;

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
