// SPDX-License-Identifier: GPL-3.0-or-later
// COMPOSE A SCREEN - the shipped assets through the ported drawers into one
// RGB565 framebuffer.
//
// Everything here already existed in pieces and had never been put together:
// `surfaceFromBmp` + `blt` (tier 4, the menu title 66560/66560), `drawRun`
// (tier 4, 6132/6132 glyph pixels), the widget tree's walk, and - new on
// 2026-09-01 - the item COORDINATES and the panel tile map, without which
// nothing could be placed at all.
//
// It is deliberately on THIS side of the frontend boundary: composing a frame
// needs no window, so it can be checked headless and compared against a
// captured framebuffer exactly the way the two slices above are.
//
// TIER: the parts are tier 4 individually; the COMPOSITION is not checked
// against a capture yet, because the menu's background animates
// (`PORTING` B5: a check may assert the text and must not assert the scene).
// So this file's own claim is **tier 6, read and explained**, and the numbers
// that are asserted are the ones the assets could fail - see
// `verify.py: engine screen`.
#pragma once

#include "platform/datafs.h"
#include "ui/cloud.h"
#include "ui/surface.h"
#include "ui/text.h"
#include "ui/widgets.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace omk {

// `sub_476860` - the generic text callback, the one that draws the item's own
// `+28` string (with `+30` as a printf argument when it is not -1). An item
// whose `+32` is anything else has native text this port does not produce,
// and an item with no `+24` and no `+32` has NO TEXT AT ALL.
inline constexpr std::uint32_t kTextFnString = 0x00476860u;

// What a composed frame reports about itself, so a headless check can assert
// it without a window or a PNG decoder.
struct ScreenFrame {
    int  tilesDrawn = 0;      // background cells blitted
    bool fullSheet = false;   // the 0x40004000 path instead
    int  itemsDrawn = 0;      // rows whose text was rasterised
    // Sprite items blitted - `Ui_DrawItemSprite`, bank B `0x100`. 233 items
    // in the tree carry it and none of them was drawn before 2026-09-04;
    // the sneak's five left-hand icons are five of them.
    int  spritesDrawn = 0;
    int  textAdvance = 0;     // summed pen advance of every row drawn
    int  centred = 0;         // rows the alignment ladder centred
    // FNV-1a of the whole framebuffer. The counts above say what was drawn;
    // this says WHERE, and it is the only field that moves when a glyph
    // shifts by a pixel - without it the check reported "4 centred" whether
    // or not the ladder had actually moved anything.
    std::uint32_t hash = 0;
    long painted = 0;         // non-zero pixels in the whole frame
    // How many panels deep the walk was. 1 is the screen's own; more means it
    // has descended into a child - the confirm dialog, the name field - and
    // those are drawn OVER the parent, not instead of it.
    int  panelsDrawn = 0;
    // Whether the animated background was drawn under the sheet.
    bool cloudDrawn = false;
};

class ScreenComposer {
public:
    // The menu's animated background, drawn UNDER the screen's own sheet -
    // which is colour-keyed over it. Optional: a composer with none draws the
    // sheet on black, which is what this did before the cloud was found.
    void attachCloud(const MenuCloud* c) { cloud_ = c; }
    void setFrame(long f) { frame_ = f; }
    // THE OSCILLATOR CLOCK, in MILLISECONDS, and it is not the frame counter.
    // The eight oscillator records carry periods of 500, 1000 and 5000, and
    // `Ui_TickScreens` advances each by the frame DELTA - so the unit is ms,
    // where `frame_` above is the cloud's frame index. Feeding one to the
    // other makes the selection blink at a rate that depends on the frame
    // rate, which is the class of bug CLAUDE.md 5 records for the effect
    // emitters ("ticking it in seconds ran the city 30x too slow").
    void setClockMs(long ms) { clockMs_ = ms; }

    // WHAT A ROW SHOWS WHEN IT IS NOT A STRING ID.
    //
    // An item's `+28` indexes the screen's own `IAM\<name>`, and for most
    // rows that is the whole story. The sneak's inventory page is the case
    // where it is not: its nine rows (list 0x004DE6F0) ship `-1` and are
    // never bound, because what they show is the CARRIED OBJECT LIST, which
    // the interface asks for through `Game_HandleEvent` 29 and 33 rather than
    // storing anywhere. An inventory slot is 56 bytes of which the first 32
    // are the display name `case 33` wrote.
    //
    // So this is the same shape as the start menu's name field, which the
    // composer already draws from `UiWalk::name()` for the same reason: the
    // row's text is runtime state and the drawer has to be given it. Keyed by
    // ITEM ADDRESS, so it cannot land on the wrong row. Rows with no entry
    // fall back on the string id, and a screen with no map set behaves
    // exactly as before.
    void setRowText(const std::map<std::uint32_t, std::string>* t) { rows_ = t; }

    // THE DISPLAY SIZE, and the interface is not redesigned for it.
    //
    // `I2D_ScaleX` (0x00429700) and `I2D_ScaleY` (0x00429730) are
    // `v * width / 640` and `v * height / 480`: the whole interface is
    // AUTHORED at 640x480 and its coordinates are scaled to whatever the
    // display is, while the glyphs are drawn at their native size. So a
    // higher resolution spreads the same layout over more pixels and does not
    // enlarge the text - which is what a reader's 800x600 screenshot shows.
    //
    // Full-screen artwork is STRETCHED rather than scaled in coordinates: the
    // sheets are 640x480 files and `Blt` stretches, and the cloud's own
    // surface is hard-coded 0x280 x 0x1E0 in `sub_4B19C0` and blitted out.
    void setDisplay(int w, int h) { dw_ = w; dh_ = h; }
    int  scaleX(int v) const { return v * dw_ / 640; }
    int  scaleY(int v) const { return v * dh_ / 480; }

    ScreenComposer(const DataFs& fs, const UiWidgets& w, const TextLayout& lay)
        : fs_(&fs), w_(&w), lay_(&lay) {}

    // Draw `screenId` as `walk` currently has it - the focused row is the one
    // the walk says, and it is drawn white against 0x7F7F7F for the rest,
    // which is what the captures show (`engine: text draw`).
    ScreenFrame draw(Surface& fb, int screenId, const UiWalk& walk) const;

private:
    // `Ui_DrawPanelBack` (0x00476040). With the panel's 0x40004000 flag the
    // whole 640x480 sheet goes over the whole screen; without it the panel's
    // 80 tile ids each pick a 64x64 source cell as (id % 10, id / 10), and
    // row 7 is drawn at HALF height from source y 448..480 - which is what
    // makes 10x64 = 640 and 7x64 + 32 = 480 come out exactly.
    int background(Surface& fb, const UiPanel& p, const Surface& sheet) const;

    const MenuCloud* cloud_ = nullptr;
    const std::map<std::uint32_t, std::string>* rows_ = nullptr;
    long             frame_ = 0;
    long             clockMs_ = 0;
    int              dw_ = 640, dh_ = 480;

    const DataFs*    fs_;
    const UiWidgets* w_;
    const TextLayout* lay_;
};

}  // namespace omk
