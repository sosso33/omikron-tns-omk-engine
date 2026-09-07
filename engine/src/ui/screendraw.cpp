// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screendraw.h"

#include "script/savefile.h"

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

// THE I2D COLOUR KEY, and it is a constant of the loader rather than a
// property of any sheet. `I2D_CreateSurfaceFromBmp` (0x00428DB0) ends with
//
//     surface->SetColorKey(DDCKEY_SRCBLT, {low = 0, high = 0})
//
// (the vtable call at +116; DDCKEY_SRCBLT is the literal 8 it passes), so
// every one of the eleven interface bitmaps keys on **pure black** and on
// nothing else. `I2D_BlitBitmap`'s third argument, 1 on every interface
// blit, is what sets DDBLT_KEYSRC against it (`sub_4810D0`: `v3 & 1` ->
// 0x1008000 = DDBLT_WAIT | DDBLT_KEYSRC).
//
// This composer took the key from each sheet's own bottom-left pixel, which
// for `gfxint.bmp` is palette index 255 = rgb(4, 4, 4) - NOT black, and so
// not the key. The engine's own capture settles it: `traces/frames/menu-18`
// has the whole 640x150 title band at exactly (0, 4, 0), which is (4, 4, 4)
// through RGB565. Those pixels are DRAWN, opaquely, over the cloud. Read as
// the key they were skipped and the cloud showed through the band.
constexpr std::uint16_t kI2dColourKey = 0;

// `sub_47A510` - the start menu's NAME FIELD drawer, the item `+20` hook on
// item 0x004CE840 and the only one of the tree's 24 that is ported. It is
// named by address because that is what identifies it: the record carries no
// string, no text pointer and no text callback, so nothing else in the item
// says what the box shows.
constexpr std::uint32_t kDrawNameField = 0x0047A510;
// The LOAD PANEL's row list (item 0x004CEB70, 370 x 300 at 20,140). Its own
// `+20` draw hook - the table's `drawFn` - and what it composes is the
// `Joueur :` heading and the save rows, neither of which is a string in any
// record: they come from the save DIRECTORY.
constexpr std::uint32_t kDrawLoadRows = 0x0047B180;
// ...and the string it prefixes the typed name with, `push 0Dh` in that hook:
// index 13 of the screen's own `IAM\<name>` file, which for the start menu's
// `IAM\Menu` is its last line, "Entrez votre nom".
constexpr int kNameFieldLabel = 13;

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
    // WHICH ARM RUNS IS THE PANEL'S BANK-B FLAG, not whether it has tiles.
    //
    // `Ui_DrawPanelBack` (0x00476040) reads three bits of `panel+76` and
    // nothing else: `0x2000` draws no background at all, `0x4000` blits the
    // whole sheet over the display, and failing both it walks `panel+20`'s 80
    // tile ids - of which there are NONE when that pointer is 0.
    //
    // This tested `tiles.empty()` and called it "the full-sheet arm", which
    // is the one panel shape the engine never draws. The start menu's three
    // five panels ship `0x40002000`: no artwork whatsoever. The
    // title band a capture shows is not the sheet - it is a 640x150 SPRITE
    // ITEM (0x004CF1A8) on the screen's own panel, which is why the menu has
    // it and its confirm dialog, a CHILD panel that does not carry that item,
    // does not. Blitting the whole sheet here painted `gfxint.bmp` over the
    // animated cloud on both.
    //
    // NOT MODELLED, and deliberately: the fourth bit, `0x0800`, gates a
    // full-screen `I2D_SubmitQuad(rect, 0, 0)` that goes down when it is
    // CLEAR - which it is for every start-menu panel. Drawn here it would be
    // a black rectangle over the cloud, and the engine's own capture has the
    // cloud, so whatever layer that quad lands on is below the cloud's blit.
    // The composer clears its framebuffer to 0 and draws the cloud first,
    // which is the same picture; the quad is left out rather than guessed at.
    if (p.backNone()) return 0;
    if (p.backSheet()) {
        blt(fb, {0, 0, fb.w, fb.h}, sheet, {0, 0, sheet.w, sheet.h},
            kBltWait | kBltKeySrc, kI2dColourKey);
        return 0;
    }
    if (p.tiles.empty()) return 0;
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
        // ...AND THE TILE BLIT IS COLOUR-KEYED, exactly like the sheet arm
        // above. `Ui_DrawPanelBack`'s two calls are
        //
        //     I2D_BlitBitmap(&rect, u32(a1, 56), 1, 3)      // the whole sheet
        //     I2D_BlitBitmap(&rect, u32(a1, 56), 1, 3)      // each of the 80
        //
        // and that third argument is what turns DDBLT_KEYSRC on against the
        // flat **0** key `I2D_CreateSurfaceFromBmp` sets on every bitmap it
        // loads. This passed `false, 0` and painted the black cells solid.
        //
        // Invisible on every page that has no hole in it - the sneak's and
        // the slider's are opaque - and fatal on the one that does: the
        // VIDEOPHONE's viewport is a black rectangle in `sneak.bmp` meant to
        // be keyed out so the 3D view shows through. Painted solid it made
        // the caller's picture a flat grey (the item's own 21.6% white fill
        // over black, which is exactly the (48,52,48) a reader photographed),
        // and the world was rendering correctly the whole time.
        blt(fb, {dx0, dy0, dx1, dy1},
            sheet, {sx, sy0, sx + 64, sy0 + sh}, kBltWait | kBltKeySrc,
            kI2dColourKey);
        ++drawn;
    }
    return drawn;
}

const UiItem* ScreenComposer::viewportItem(const UiPanel* p) {
    if (!p) return nullptr;
    for (const auto& l : p->lists) {
        if (!l.drawn()) continue;
        for (const auto& it : l.items)
            if (it.drawFn == kDrawViewport) return &it;
    }
    return nullptr;
}

ScreenFrame ScreenComposer::draw(Surface& fb, int screenId,
                                 const UiWalk& walk) const {
    ScreenFrame out;
    const UiPanel* p = w_->screen(screenId);
    if (!p) return out;
    // The cursor's quads, held back to layer 8 (see the collect below).
    std::vector<UiCursor::Quad> cursorLate;

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
    // The I2D colour key is **0**, and it is not read off the sheet.
    // `I2D_CreateSurfaceFromBmp` (0x00428DB0) finishes every bitmap it loads
    // with `SetColorKey(DDCKEY_SRCBLT, {0, 0})` - vtable +116 - so the key is
    // pure black for all eleven of them, and `I2D_BlitBitmap`'s `a3 = 1` is
    // what turns DDBLT_KEYSRC on against it.
    const std::uint16_t artKey = kI2dColourKey;
    if (sheetOk) {
        out.tilesDrawn = background(fb, *p, art);
        // ...and it is the panel's own arm, not "has no tiles". A panel with
        // neither the sheet bit nor a tile pointer draws NO background, which
        // is what the start menu does.
        out.fullSheet  = p->backSheet();
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
    // ---- THE DISPLAY LIST IS SORTED BY LAYER -------------------------
    //
    // `I2D_Enqueue` files every primitive under the layer it is submitted at
    // and the flush walks the sixteen layers in order, so an item's `+11` is
    // its DEPTH and record order decides nothing between two items that
    // carry different ones. This composer drew in record order, and the start
    // menu is where that shows: its title band is a sprite item at layer 3
    // and its four buttons are text at layer 6, but the sprite's list is the
    // panel's THIRD, so the band was painted over "Nouvelle partie" and the
    // menu's first row vanished. The engine's own capture
    // (`traces/frames/menu-18`) has that row white on the band.
    //
    // So the walk runs once per distinct layer, ascending. The two primitives
    // that do NOT take the item's own layer keep their existing treatment:
    // `Ui_DrawItemFill` submits at `+11 - 2` and the cursor at a flat 8,
    // which is why the cursor is still collected and drawn last.
    //
    // WITHIN one layer this keeps submission order, and the engine does not:
    // the I2D per-layer cache is a HEAD cache, so the rest of a layer draws in
    // REVERSE submission order (`docs/UI.md` 1). Not modelled, because nothing
    // in the shipped tree puts two items of one layer over the same pixels -
    // the pairs that do overlap (the slider page's two headers at (187, 30))
    // are alternatives a builder chooses between, exactly one drawn. Left as
    // a known gap rather than implemented blind: reversing it would move
    // every screen and no capture separates the two.
    std::vector<int> layers;
    for (const UiPanel* q : chain)
        for (const auto& l : q->lists) {
            if (!l.drawn()) continue;
            for (const auto& it : l.items) layers.push_back(it.layer);
        }
    std::sort(layers.begin(), layers.end());
    layers.erase(std::unique(layers.begin(), layers.end()), layers.end());

    for (const int layer : layers)
    for (const UiPanel* q : chain)
    for (const auto& l : q->lists) {
        // THE DRAW GATE IS NOT THE WALK'S. `Ui_DrawPanel` skips a list on
        // bank B `0x40000001` (`sub_429080(list, 1073741825)`);
        // `Ui_MoveBetweenLists` skips it on `+16 & 4`. This used the walk's
        // flag, so it hid the sneak's bottom bar - which is `+16 = 0x20000004`
        // and `+20 = 0`: drawn, and deliberately not navigable.
        if (!l.drawn()) continue;
        // THE NAME FIELD, and it is a whole ITEM DRAW HOOK rather than a
        // special case of the text path. `Ui_DrawItem` runs the item's `+20`
        // first, before the text and before every decoration; item
        // 0x004CE840's is `sub_47A510`, and what it draws is
        //
        //     sprintf(buf, "%s : %s", Ui_ScreenString(screen, 13), typed)
        //     Text_DrawBlock(the item's own box, buf, Ui_ItemTextStyle(item))
        //     if (Ui_Oscillator(1)[24])                       // the 2 Hz blink
        //         Text_DrawBlock(x + width(buf[0 .. label+3+cursor]), y,
        //                        + 20, + 20, "_", the same style)
        //
        // so the box is not the typed name at all: it is the screen's own
        // string 13 - "Entrez votre nom", "Enter Name" in English - then
        // " : ", then what the player typed, then a caret that blinks on the
        // same square wave as everything else and sits after the CURSOR, not
        // after the last character. This drew the bare buffer in the menu's
        // face, so the dialog came up with a name and no label and no caret;
        // the capture the user supplied has all three.
        //
        // The face is the item's own, `+36 = 74` ('J', JOURNAL) - which is
        // why the label reads in Latin letters while the title and the two
        // buttons, font 73 (MENUINTR), are the game's own glyphs.
        // ---- THE LOAD PANEL'S ROWS AND HEADING -------------------------
        //
        // `sub_47B180`, the row list's own draw hook.  The box is divided
        // into **nine** slots - the hook's `dword_657960` (the row count plus
        // one) is compared against 9, and its height arithmetic is a divide
        // by the same - with the `Joueur :` heading in slot 0 and the rows
        // from slot 1.  Against a reader's screen grab of the original at
        // 640x480 that puts the heading at y=140 and the rows at 173, 206 and
        // 240; the grab measures 143, 175, 206 and 239.
        //
        // RECONSTRUCTION, and labelled as one: the slot arithmetic is read
        // from the hook and the CONTENT is exact (the directory's own fields,
        // checked against that grab character for character), but the pixel
        // offsets inside a slot are not - the hook's text layout has not been
        // transcribed.
        if (!l.items.empty() && l.items.front().drawFn == kDrawLoadRows &&
            l.items.front().layer == layer && walk.loadPanel()) {
            const UiItem& f = l.items.front();
            const LoadPanel& lp = *walk.loadPanel();
            const int slot = f.h / 9;
            const int x0 = scaleX(f.x + q->offsetX);
            const auto line = [&](int k, const std::string& t, bool lit) {
                const std::uint8_t v = lit ? kLit : static_cast<std::uint8_t>(kLit >> 1);
                const auto run = parseMarkup(t, f.face('J'), v, v, v).run;
                lay_->drawRun(fb, x0, scaleY(f.y + q->offsetY + k * slot), run);
                ++out.itemsDrawn;
            };
            if (!lp.profiles.empty() &&
                lp.profile < static_cast<int>(lp.profiles.size()))
                line(0, "Joueur : " + lp.profiles[static_cast<std::size_t>(lp.profile)], true);
            const auto rows = lp.rows();
            for (std::size_t i = 0; i < rows.size() && i < 8; ++i)
                line(static_cast<int>(i) + 1, lp.rowLabel(rows[i]),
                     static_cast<int>(i) == lp.row);
            // `Nouvelle sauvegarde` - the screen's string 15, one row past
            // the last slot, and only on the save screen.
            if (lp.hasNewRow()) {
                const int k = static_cast<int>(rows.size());
                const std::string t = 15 < static_cast<int>(text.size())
                    ? text[15] : std::string("Nouvelle sauvegarde");
                if (k < 8) line(k + 1, t, k == lp.row);
            }
            // ---- THE SELECTION BOX, and the connector, and the picture ----
            //
            // `I2D_DrawRectOutline` (0x004777A0): four `I2D_SubmitQuad`s with
            // flags 4, which is `quadMode` 1. Its rectangle was measured off
            // `traces/frames/loadpanel-mode2.png` and solved back through the
            // function's own arithmetic to **a1=20, a2=173, a3=370, a4=16**
            // for the first row - and 173 is this list's own second slot,
            // which is the two readings agreeing (`engine: I2D outline`).
            if (lp.row >= 0) {
                const int v7 = 1;                     // `I2D_ScaleX(1)` at 640
                const int a1 = f.x + q->offsetX, a3 = f.w;
                const int a2 = f.y + q->offsetY + slot * (lp.row + 1) + 33 - slot;
                const int a4 = 16;
                const std::uint16_t c = rgb565(255, 255, 255);
                const int quads[4][8] = {
                    {a1-v7, a2-v7, a1+a3+v7, a2-v7, a1+a3+v7, a2,       a1-v7, a2},
                    {a1-v7, a2+a4, a1+a3+v7, a2+a4, a1+a3+v7, a2+a4+v7, a1-v7, a2+a4+v7},
                    {a1-v7, a2,    a1,       a2,    a1,       a2+a4,    a1-v7, a2+a4},
                    {a1+a3, a2,    a1+a3+v7, a2,    a1+a3+v7, a2+a4,    a1+a3, a2+a4},
                };
                for (const auto& qd : quads) {
                    const int xs[4] = {scaleX(qd[0]), scaleX(qd[2]), scaleX(qd[4]), scaleX(qd[6])};
                    const int ys[4] = {scaleY(qd[1]), scaleY(qd[3]), scaleY(qd[5]), scaleY(qd[7])};
                    fillQuad(fb, xs, ys, c, quadMode(4));
                }
                // the CONNECTOR to the picture - not a line: `Ui_DrawItem`'s
                // vocabulary has no line at all, and the captured bar is two
                // rows by sixty-nine columns, a mode-1 quad's signature.
                const int cy = a2 + a4 / 2;
                const int cx[4] = {scaleX(a1+a3+v7), scaleX(460), scaleX(460), scaleX(a1+a3+v7)};
                const int cys[4] = {scaleY(cy), scaleY(cy), scaleY(cy+2), scaleY(cy+2)};
                fillQuad(fb, cx, cys, c, quadMode(4));
                // ...and the slot's own 128x96 picture, read on the MOVE the
                // way `sub_408D70` reads it.
                const int sl = lp.slotOfRow();
                if (sl >= 0 && !lp.path.empty()) {
                    const auto file = DataFs::readPath(lp.path);
                    const auto px = readSaveThumb(file, sl);
                    // WALK THE DESTINATION AND SAMPLE THE SOURCE, never the
                    // other way round. Scaling each of the 128x96 source
                    // pixels to ONE destination point leaves the gaps between
                    // them unwritten, and at 640x480 there are none - so it
                    // looks perfect here and comes out as a dark grid over
                    // the picture on any larger window, which is what a
                    // reader saw. `background`'s own comment says the same
                    // thing about the tile blit: "the destination is scaled
                    // and the source is not, and getting that backwards is
                    // invisible at 640x480 and ruins every other resolution."
                    if (!px.empty()) {
                        const int x0 = scaleX(460), x1 = scaleX(460 + omk::kThumbW);
                        const int y0 = scaleY(140), y1 = scaleY(140 + omk::kThumbH);
                        const int w = x1 - x0, h = y1 - y0;
                        for (int dy = y0; dy < y1; ++dy) {
                            if (dy < 0 || dy >= fb.h || h <= 0) continue;
                            const int sy = (dy - y0) * omk::kThumbH / h;
                            for (int dx = x0; dx < x1; ++dx) {
                                if (dx < 0 || dx >= fb.w || w <= 0) continue;
                                const int sx = (dx - x0) * omk::kThumbW / w;
                                fb.px[static_cast<std::size_t>(dy) * static_cast<std::size_t>(fb.w) +
                                      static_cast<std::size_t>(dx)] =
                                    px[static_cast<std::size_t>(sy) * omk::kThumbW +
                                       static_cast<std::size_t>(sx)];
                            }
                        }
                    }
                }
            }
            continue;
        }
        if (!l.items.empty() && l.items.front().drawFn == kDrawNameField &&
            l.items.front().layer == layer) {
            const UiItem& f = l.items.front();
            // The same lit ladder every other row takes: the item carries no
            // bank-B bit, so it is white while its list is the current one
            // and halved otherwise.
            const int selIdx = walk.selectionOf(l);
            const bool isSel = selIdx >= 0 &&
                static_cast<std::size_t>(selIdx) < l.items.size() &&
                l.items[static_cast<std::size_t>(selIdx)].addr == f.addr;
            std::uint8_t nr = kLit, ng = kLit, nb = kLit;
            std::uint32_t eff[3];
            f.effective(l.broadcast, eff);
            if (!(eff[2] & 1)) {
                nr = static_cast<std::uint8_t>(f.rgb[0]);
                ng = static_cast<std::uint8_t>(f.rgb[1]);
                nb = static_cast<std::uint8_t>(f.rgb[2]);
            }
            if (!(isSel && &l == curList)) { nr >>= 1; ng >>= 1; nb >>= 1; }

            const std::string label =
                kNameFieldLabel < static_cast<int>(text.size())
                    ? text[static_cast<std::size_t>(kNameFieldLabel)]
                    : std::string();
            const std::string shown = label + " : " + walk.name();
            const int x0 = scaleX(f.x + q->offsetX);
            const int y0 = scaleY(f.y + q->offsetY);
            const auto run = parseMarkup(shown, f.face('J'), nr, ng, nb).run;
            out.textAdvance += lay_->drawRun(fb, x0, y0, run);
            ++out.itemsDrawn;
            // ...and the caret, on oscillator 1 - the same 500 ms square wave
            // the flashing rows take. `strncpy(prefix, shown, strlen(label) +
            // cursor + 3)`: the 3 is `" : "`.
            if (((clockMs_ / 500) & 1) != 0) {
                const std::size_t cut = std::min(
                    shown.size(), label.size() + 3 +
                    static_cast<std::size_t>(walk.nameCursor()));
                const auto pre = parseMarkup(shown.substr(0, cut),
                                             f.face('J'), nr, ng, nb).run;
                const auto caret = parseMarkup("_", f.face('J'), nr, ng, nb).run;
                lay_->drawRun(fb, x0 + lay_->measure(pre), y0, caret);
                ++out.itemsDrawn;
            }
        }
        for (const auto& it : l.items) {
            if (it.layer != layer) continue;
            std::uint32_t eff0[3];
            it.effective(l.broadcast, eff0);
            // ...and the bank-B bits a builder SET at RUN TIME, which the
            // record cannot carry and which `Ui_ItemTextStyle` reads beside
            // the record's own. `sub_49B950` sets `0x40000002` on "Examiner",
            // which is what makes it flash while its page is up. This has to
            // land BEFORE the lit/unlit ladder below reads `eff0[1]`.
            eff0[1] |= walk.itemFlagsOn(it.addr) & 0x1FFFFFFFu;
            // The item's own draw gate, the same `0x40000001` the list has -
            // from the record, and from the RUNTIME for the lists whose
            // widgets are a window onto something longer (`sub_42AAE0`).
            if (eff0[1] & 1) continue;
            if (hidden_ && hidden_->count(it.addr)) continue;
            if (walk.itemOff(it.addr)) continue;   // a builder switched it off

            // THE 3D VIEWPORT (`sub_4782B0`): the node `sub_4812E0` clears
            // the rectangle's DEPTH, sets the D3D viewport to it
            // (`sub_440C40` -> `sub_42FF80` -> `sub_45FA20`, aspect w/h),
            // draws the ordinary scene through the camera snapshot the
            // submit took, and restores the full-screen viewport. The
            // frontend has done the rendering at that size; this is the
            // node's place in the layer walk, and nothing else of the item
            // is drawn - the callback IS its draw.
            if (it.drawFn == kDrawViewport) {
                if (view3d_ && view3d_->valid()) {
                    const int x0 = scaleX(it.x + q->offsetX);
                    const int y0 = scaleY(it.y + q->offsetY);
                    blt(fb, {x0, y0, scaleX(it.x + q->offsetX + it.w),
                             scaleY(it.y + q->offsetY + it.h)},
                        *view3d_, {0, 0, view3d_->w, view3d_->h}, kBltWait);
                    ++out.itemsDrawn;
                }
                continue;
            }

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
            // ---- THE 3D PREVIEWS: `I2D_Submit3DView` (0x00428900) -----
            //
            // Not an item flag - `Ui_DrawItem` never reaches this primitive,
            // so the flag table cannot name it. What identifies the three
            // slots is the LIST: 0x004DE420, whose items take the three
            // models in the order the sneak's open callback loads them.
            if (models_ && l.addr == kListSneakPreviews) {
                const int slot = static_cast<int>(&it - &l.items[0]);
                if (models_->draw(fb, slot,
                                  scaleX(it.x + q->offsetX),
                                  scaleY(it.y + q->offsetY),
                                  scaleX(it.w), scaleY(it.h),
                                  UiModels::spinDegrees(clockMs_)))
                    ++out.modelsDrawn;
            }

            // ---- THE EXAMINE PAGE'S TEXT ------------------------------
            //
            // The description, laid into the item's own box by
            // `TextLayout::layOutBlock` - `Text_LayOutBlock` (0x0043F3E0)
            // itself, since 2026-09-07. It replaced this port's own greedy
            // wrap, whose line advance was `height + 2` and whose blank line
            // was a flat 12; the engine's pitch is 120% of the current font's
            // `+12` and a blank line takes the same.
            //
            // AND IT SCROLLS. Bank C `0x2` on the item, four of which the
            // tree carries. `Ui_ItemTextStyle` lays the block out, takes
            // `laidOutHeight - boxHeight` floored at 0, clamps `dword_6A5090`
            // into [0, that] and hands the result to the run at `+0x10` -
            // which `Text_DrawBlock` unpacks into `dword_9079F8`, the layout's
            // ORIGIN Y. So the scroll is not a special case in the drawer at
            // all: it is the block's origin, subtracted from every line
            // (`v41 = v6 - dword_9079F8`). `sub_42A9A0`, the list hook, steps
            // the same global eight pixels per UP or DOWN and does no
            // clamping, so the bound is discovered HERE and written back.
            //
            // `{B}` is RED, (255, 0, 0), on the frames oscillator 1 is high -
            // `Text_LayOutBlock`'s run-emit is explicit about it, and
            // `docs/UI.md`'s markup table had said white. Two captures of the
            // original two seconds apart settled it before it could be read:
            // the MK400 notice's Khonsu line, `{fSI226198101B}`, is gold in
            // one and red in the other, (181, 180, 131) against (150, 33, 42).
            if (examine_ && !examine_->empty() &&
                l.addr == kListSneakExamineContent) {
                TextBlock blk;
                blk.left   = scaleX(it.x + q->offsetX);
                blk.top    = scaleY(it.y + q->offsetY);
                blk.right  = blk.left + scaleX(it.w);
                blk.bottom = blk.top + scaleY(it.h);
                blk.font   = it.face('J');
                blk.rgb[0] = static_cast<std::uint8_t>(rgb[0]);
                blk.rgb[1] = static_cast<std::uint8_t>(rgb[1]);
                blk.rgb[2] = static_cast<std::uint8_t>(rgb[2]);
                blk.blinkOn = blink;
                blk.screenW = fb.w;
                blk.screenH = fb.h;
                // Rows outside the box are not written, so the line
                // straddling each edge of a scrolled block is CUT rather than
                // dropped or spilled over the page art.
                blk.clipTop = blk.top;
                blk.clipBottom = blk.bottom;

                // Measure first, because the clamp needs the height and the
                // hook that moves the offset has no bound of its own. This is
                // `Ui_ItemTextStyle`'s own order.
                TextBlock probe = blk;
                probe.measureOnly = true;
                const int total = lay_->layOutBlock(nullptr, *examine_, probe);
                const int overflow = std::max(0, total - (blk.bottom - blk.top));
                if (scroll_) {
                    if (*scroll_ > overflow) *scroll_ = overflow;
                    else if (*scroll_ < 0)   *scroll_ = 0;
                }
                out.textOverflow = overflow;
                blk.originY = scroll_ ? *scroll_ : 0;
                BlockResult res;
                lay_->layOutBlock(&fb, *examine_, blk, &res);
                out.textLines += res.lines;
            }

            // ---- THE EXAMINE PAGE'S CONTENT ---------------------------
            //
            // Panel 0x004DEF20's own list 0x004DE760 holds one item, and what
            // it shows is whichever arm of `Game_HandleEvent` case 40 the
            // object's kind takes - a 3D model for kind 15, an
            // `IMAGES\<stem>.bmp` for kind 16, nothing otherwise.
            if (models_ && l.addr == kListSneakExamineContent) {
                if (models_->drawExamine(fb,
                                         scaleX(it.x + q->offsetX),
                                         scaleY(it.y + q->offsetY),
                                         scaleX(it.w), scaleY(it.h),
                                         UiModels::spinDegrees(clockMs_)))
                    ++out.modelsDrawn;
            }

            // ---- THE CURSOR: `Ui_DrawItemCursor` (`sub_479920`) -------
            //
            // Drawn BEFORE the fill and the sprite, because `sub_4795F0`
            // submits at layer 8 while a fill goes to the item's own
            // `+11 - 2`, and the I2D per-layer cache is a HEAD cache.
            //
            // THE GATE IS READ AT THE CALL SITE, not taken from the docs:
            // bank B `0x40000200` on the item, then bank A `0x20000001` -
            // FOCUSED - on the item AND on its list. That is the one row
            // which is both its list's selection and in the focused list,
            // which is the row the player is standing on. A player reported
            // "the hovering effect is absent so it is very difficult to know
            // where I am"; this is that effect.
            // DEFERRED, because the I2D display list is sorted by LAYER
            // and the cursor's is 8 while a sprite's is the item's own 6 and
            // a fill's is `+11 - 2` = 4. Submitting it here drew it UNDER the
            // icon, and a player saw exactly that: "you put the effect
            // between the background and the selected image so we can
            // clearly see that a cut image of the icon is there". Collected
            // now and drawn after the whole walk, which is what a sort by
            // layer does with one cursor on screen.
            if (cursor_ && (eff0[1] & 0x200) && isSel && isFocus) {
                const int cx = scaleX(it.x + q->offsetX + it.w / 2);
                const int cy = scaleY(it.y + q->offsetY + it.h / 2);
                int hr = rgb[0], hg = rgb[1], hb = rgb[2];
                if (eff0[1] & 0x02000000) { hr = 255; hg = 50; hb = 50; }
                cursorLate = cursor_->tick(it.addr, cx, cy,
                                           scaleX(it.w), scaleY(it.h),
                                           hr, hg, hb, deltaMs_);
                ++out.cursorQuads;
            }
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
            //
            // ...and since 2026-09-07 the row goes through `layOutBlock` -
            // `Text_LayOutBlock` itself - rather than through one unwrapped
            // `drawRun`, because that call above IS a `Text_DrawBlock` with
            // the item's box: the engine WRAPS inside it, aligns inside it and
            // steps 120% of the font's height between lines. `docs/UI.md`
            // named the missing wrap as a gap ("this composer draws one
            // unwrapped line") from the day the composer was written.
            //
            // The alignment goes in as the STYLE word the ladder above
            // produces, which is the same 2/4/8/0x10 `Ui_ItemTextStyle` hands
            // `Text_DrawBlock`, so the case-4/case-8 arms inside the layout
            // do the arithmetic these two lines used to.
            const int x0 = scaleX(it.x + q->offsetX);
            const int x1 = scaleX(it.x + q->offsetX + it.w);
            const int y  = scaleY(it.y + q->offsetY);
            TextBlock row;
            row.left   = x0;
            row.top    = y;
            row.right  = x1;
            row.bottom = scaleY(it.y + q->offsetY + it.h);
            row.font   = it.face('J');
            row.rgb[0] = static_cast<std::uint8_t>(cr);
            row.rgb[1] = static_cast<std::uint8_t>(cg);
            row.rgb[2] = static_cast<std::uint8_t>(cb);
            row.style  = (eff[2] & 0x10) ? 8 : (eff[2] & 0x08) ? 4 : 2;
            row.blinkOn = blink;
            row.screenW = fb.w;
            row.screenH = fb.h;
            BlockResult rr;
            lay_->layOutBlock(&fb, s, row, &rr);
            // `drawRun` returned the pen ADVANCE, not a pixel count - the
            // width the row occupies - and `BlockResult::advance` is the same
            // number summed over the lines a block wrapped to.
            out.textAdvance += rr.advance;
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
    // ...and now layer 8, over everything the walk drew at 4 and 6.
    for (const auto& c : cursorLate)
        fillQuad(fb, c.x0, c.y0, c.x1, c.y1, c.r, c.g, c.b, c.alpha);

    return out;
}

}  // namespace omk
