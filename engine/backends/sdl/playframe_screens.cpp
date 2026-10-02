// SPDX-License-Identifier: GPL-3.0-or-later
// THE FRAME'S SCREENS PHASE - the screens' rows and the HUDs.
// Part of `main`'s loop body, moved byte for byte by `todo/play-split.md`
// (2026-10-02); see `playframe.h` for what the names below refer to.
#include "playframe.h"

namespace {

// POSITIONED TEXT - a string that carries `{X}` moves.
//
// `{X<xxx><yyy>}` is "move to (xxx, yyy) as percentages of the screen", and a
// string may carry several: each opens a new block at its own spot, with the
// `{f}` face and `{C}/{D}/{F}/{G}` alignment that follow it. That is the whole
// of the Bowie title sequence's credits - `AREA 0` record 78 fires twenty
// `media.play` calls and each object's `+280` description is a block like
//
//     {X090058}{f1}{D}Direction programmation
//     {X080065}{f3}{D}Olivier NALLET
//
// so there is no credits system to write; the port simply threw the moves
// away (`if (d == 'X' ...) { i += 7; continue; }`) and every credit landed at
// the bottom like an ordinary subtitle. Lines inside one block stack by the
// font's own height. -> true when the string was positioned and drawn here.
bool drawPositioned(omk::Surface& fb, const omk::TextLayout& lay,
                    const omk::ParsedText& pt, int dispW, int dispH) {
    if (pt.moves.empty()) return false;
    for (std::size_t m = 0; m < pt.moves.size(); ++m) {
        const auto& mv = pt.moves[m];
        const std::size_t from = mv.at;
        const std::size_t to = m + 1 < pt.moves.size() ? pt.moves[m + 1].at : pt.run.size();
        if (from >= to) continue;
        // The block's own rows, split on the newlines the text carries.
        std::vector<std::vector<omk::StyledChar>> rows(1);
        for (std::size_t k = from; k < to; ++k) {
            if (pt.run[k].ch == '\n') { rows.emplace_back(); continue; }
            if (pt.run[k].ch == '\r') continue;
            rows.back().push_back(pt.run[k]);
        }
        int y = dispH * mv.yPct / 100;
        const int x = dispW * mv.xPct / 100;
        for (auto& row : rows) {
            if (row.empty()) { y += lay.height(row) + 2; continue; }
            const int w = lay.measure(row);
            int rx = x;
            if (mv.align == omk::kAlignRight)       rx = x - w;
            else if (mv.align == omk::kAlignCentre) rx = x - w / 2;
            lay.drawRun(fb, rx, y, row);
            y += lay.height(row) + 2;
        }
    }
    return true;
}

void drawSubtitle(omk::Surface& fb, const omk::TextLayout& lay,
                  const std::string& line,
                  const std::vector<std::string>& menu, int selected,
                  int dispW, int dispH, int inset640 = 32,
                  SubBox box = SubBox::None, char face = 'J',
                  int scroll = 0, int* overflowOut = nullptr,
                  bool mediaLine = false) {
    // 32 is `Dialog_TickUI`'s block; `Subtitle_Show` (0x0041E040) lays a
    // `media.play` line out inset 16 - the caller says which.
    const int inset = inset640 * dispW / 640;
    const int left = inset, right = dispW - inset, width = right - left;
    if (width <= 0) return;

    // PARSE THE MARKUP ONCE, THEN WRAP THE RUN - not the other way round.
    //
    // The shipped strings carry `{f...}` face markup: `media.play 142` is
    // literally `{fD}Te voil...`. Wrapping the STRING first and parsing each
    // row separately loses the run's state at every break, so a `{fD}` at the
    // head applied to the first row and every row after it fell back to the
    // block's default - two faces in one paragraph, which is what a reader
    // photographed in the Impasse (`todo/omk-play.md` 58).
    //
    // ...and since 2026-09-07 the wrap itself is the ENGINE'S, not this
    // file's: `TextLayout::layOutBlock` is `Text_LayOutBlock` (0x0043F3E0),
    // and it parses the markup, wraps and breaks in one pass, so the two
    // paragraphs above describe what it already does rather than what this
    // lambda has to arrange. What is still this file's is the STACK - which
    // row is the line and which are replies, and the tone each takes - because
    // the engine issues a separate `Text_DrawBlock` per string and colours it
    // at the call, so the rows have to come back rather than be drawn.
    const auto wrapRun = [&](const std::string& text,
                             std::vector<std::vector<omk::StyledChar>>& out) {
        omk::TextBlock blk;
        blk.left = left; blk.right = right;
        blk.top = 0;     blk.bottom = dispH;
        blk.font = face;
        blk.measureOnly = true;         // the caller draws
        blk.screenW = dispW; blk.screenH = dispH;
        omk::BlockResult res;
        lay.layOutBlock(nullptr, text, blk, &res);
        for (auto& r : res.rows)
            if (!r.empty()) out.push_back(std::move(r));
    };
    std::vector<std::vector<omk::StyledChar>> rows;
    std::vector<std::uint8_t> tone;
    if (!line.empty()) {
        std::vector<std::vector<omk::StyledChar>> tmp;
        wrapRun(line, tmp);
        for (auto& r : tmp) { rows.push_back(std::move(r)); tone.push_back(255); }
    }
    for (std::size_t k = 0; k < menu.size(); ++k) {
        std::vector<std::vector<omk::StyledChar>> tmp;
        wrapRun(menu[k], tmp);
        for (auto& r : tmp) {
            rows.push_back(std::move(r));
            tone.push_back(static_cast<int>(k) == selected ? 255 : 128);
        }
    }
    if (rows.empty()) return;

    // THE LINE PITCH is the engine's `120 * i16i(font, 6) / 100` - 120% of the
    // face's own line height - not this file's old `height + 2`.
    const auto probe = omk::parseMarkup("Ag", face);
    const int lineH = 120 * lay.height(probe.run) / 100;
    // WHERE THE BLOCK SITS. `Dialog_TickUI` places it with
    //
    //     v3 = height << 6
    //     dword_6A52C4 = height - v3 / 480
    //
    // so the block's TOP is `height - height*64/480` - 80 rows above the
    // bottom at 600 - and `Text_LayOutBlock` fills it DOWNWARD from there.
    // This used to anchor the text's BOTTOM at `height - inset/2` and grow it
    // upward by the row count, which put a single line ~46 px lower and left
    // the box standing empty above it (`todo/omk-play.md` 58). The reply
    // stack is anchored the same way in the engine - `dword_6A52C4 = v18 -
    // dword_907975`, the bottom less the stack's own height - so a block
    // taller than the 64 grows upward from the same edge.
    const int blockH = dispH * 64 / 480;
    const int stackH = static_cast<int>(rows.size()) * lineH;
    // THE BLOCK HAS A MAX SIZE, AND PAST IT THE TEXT SCROLLS.
    //
    // `Dialog_TickUI` keeps the overflow itself:
    //
    //     dword_53AE24 = Text_DrawBlock(32, 0, ..., v3 / 480, ...) - v3 / 480
    //
    // the laid-out height LESS the block's - so a line that fits leaves it <=
    // 0 and a long one leaves the number of pixels hidden. The scroll is then
    // one pixel a tick, clamped to it:
    //
    //     if ((a2 & 8) && dword_6A52C0 < dword_53AE24) ++dword_6A52C0;  // down
    //     if ((a2 & 4) && v14 > 0)                     --dword_6A52C0;  // up
    //     if (dword_53AE24 <= 0) return 1;
    //     dword_6A50E8 = v14 ? (dword_53AE24 != v14 ? 3 : 1) : 2;
    //
    // and `Game_Tick` hands both to the renderer,
    // `sub_4400D0(0, dword_6A52C4, dword_6A52C0, height - 1, dword_6A50E8)`.
    // `dword_6A50E8` is the ARROW state - `sub_4400D0` draws a quad under
    // `a5 & 1` and another under `a5 & 2`, ~7px at the bottom edge - so 2 is
    // "more below", 1 "more above" and 3 both. The arrows are NOT drawn here.
    // The two blocks are anchored DIFFERENTLY, and the engine says so.
    //
    // A spoken LINE gets the fixed block: `v3 / 480` is the BLOCK's height,
    // not the text's, so the block stands at `height - height*64/480`
    // whatever the text does and a long line overflows BELOW it, hidden until
    // scrolled. The REPLY stack is anchored by its own height instead -
    // `dword_6A52C4 = v18 - dword_907975` - and each row gets its own
    // `Text_DrawBlock(v40, v35, v42, v35 + v36, ...)`, so it grows upward and
    // is not clipped.
    //
    // A `media.play` LINE is neither: `Subtitle_Show` (0x0041E040) lays it out
    // over the whole screen height and parks it by its OWN height,
    //
    //     g_SubtitleY = SCREEN_H - Text_DrawBlock(16, 0, W - 16, H, text) - 16
    //
    // so a long one grows upward and is never clipped, scrolled or arrowed.
    // Drawn through the dialogue's fixed block instead, the robot's 17-second
    // notice in the alley ("Vous avez ete victime d'une agression...") lost
    // its tail behind a red arrow nothing could scroll - it is a cutscene
    // (a reader, 2026-09-29).
    const bool fixedBlock = menu.empty() && !mediaLine;
    const int overflow = (fixedBlock && stackH > blockH) ? stackH - blockH : 0;
    if (overflowOut) *overflowOut = overflow;
    if (scroll < 0) scroll = 0;
    if (scroll > overflow) scroll = overflow;
    if (blockH <= 0) return;
    // The reply stack is EXACTLY its own height: `dword_6A52C4 = v18 -
    // dword_907975`, the bottom less the stack's own measured height, with a
    // `Text_DrawBlock` per row. Flooring it at the 64-scaled block made every
    // menu as tall as the longest possible one, where the game's grows with
    // the number and length of the answers.
    // The reply stack ENDS ON THE BOX'S BOTTOM EDGE, not the screen's. The
    // box runs to `height - 18` (`v25 = HIWORD(g_ScreenSize) - 18`), and
    // `dword_6A52C4 = v18 - dword_907975` puts the text's top a stack-height
    // above that same edge - so the rows sit inside the box. Anchoring them
    // to `dispH` instead left the text BELOW its own box, which is what a
    // reader photographed with a single reply.
    // The line's text top is `a2 - 4` (`v6 -= 4`), with `a2 = height -
    // height*64/480`; the reply stack ends on the box's bottom edge.
    const int clipTop = fixedBlock ? dispH - blockH - 4
                      : mediaLine  ? dispH - 16 - stackH : dispH - 18 - stackH;
    const int clipBot = fixedBlock ? clipTop + blockH : mediaLine ? dispH - 16 : dispH - 18;
    int y = clipTop - scroll;
    drawSubtitleBox(fb, box, clipTop, dispW, dispH);
    // THE SCROLL ARROWS - red, flashing, at the right edge. `sub_4400D0`
    // draws them under `a5 & 1` (more above) and `a5 & 2` (more below):
    //
    //     v23 = ((v15 / 0x3E7) << 24) + 16711680      0xFF0000, pulsing alpha
    //     up   (w-32, y+7) (w-25, y+7) (w-29, y)      apex at the top
    //     down (w-32, a4-7)(w-25, a4-7)(w-29, a4)     apex at the bottom
    //
    // with `a4 = height - 1`. `dword_6A50E8` is 2 at the top of the text, 1
    // at the bottom and 3 in between, so the pair says which way there is
    // more to see.
    if (overflow > 0) {
        const int pulse = 128 + static_cast<int>(127.0 * std::sin(
                              static_cast<double>(SDL_GetTicks()) * 0.006));
        const auto tri = [&](int apexY, int baseY) {
            const int xa = dispW - 32, xb = dispW - 25, xm = dispW - 29;
            const int lo = apexY < baseY ? apexY : baseY;
            const int hi = apexY < baseY ? baseY : apexY;
            for (int y = lo; y <= hi; ++y) {
                if (y < 0 || y >= fb.h) continue;
                const double t = hi == lo ? 0.0
                    : static_cast<double>(y - apexY) / static_cast<double>(baseY - apexY);
                const int x0t = static_cast<int>(xm + (xa - xm) * t);
                const int x1t = static_cast<int>(xm + (xb - xm) * t);
                for (int x = x0t; x <= x1t; ++x) {
                    if (x < 0 || x >= fb.w) continue;
                    std::uint16_t& px = fb.px[static_cast<std::size_t>(y) *
                                              static_cast<std::size_t>(fb.w) +
                                              static_cast<std::size_t>(x)];
                    int r = ((px >> 11) & 31) << 3, g = ((px >> 5) & 63) << 2,
                        b = (px & 31) << 3;
                    r = (0xFF * pulse + r * (255 - pulse)) / 255;
                    g = (g * (255 - pulse)) / 255;
                    b = (b * (255 - pulse)) / 255;
                    px = static_cast<std::uint16_t>(((r >> 3) << 11) |
                                                    ((g >> 2) << 5) | (b >> 3));
                }
            }
        };
        if (scroll > 0)        tri(clipTop, clipTop + 7);       // more ABOVE
        if (scroll < overflow) tri(dispH - 1, dispH - 8);       // more BELOW
    }
    for (std::size_t k = 0; k < rows.size(); ++k) {
        omk::ParsedText pt;
        pt.run = rows[k];
        for (auto& sc : pt.run) sc.rgb[0] = sc.rgb[1] = sc.rgb[2] = tone[k];
        // LEFT-ALIGNED, which is the engine's default and not a choice.
        // `Text_DrawBlock` initialises `style = 2` and the dialogue's params
        // carry only TEXTP_FLAG_A (`v56[0] = 64`), so no TEXTP_ALIGN_* bit is
        // ever set and the style stays 2. `Text_LayOutBlock` switches on
        // `dword_907A00 & 0x1E`:
        //
        //     case 4   x = right - w                         right
        //     case 8   x = left + (right - w - left) / 2      centred
        //     default  x unchanged                            LEFT   <- 2
        //
        // This centred every row, which is visible on any line short enough
        // not to fill the block (`todo/omk-play.md` 58).
        // clipped to the block - a row scrolled out of it is not drawn
        // A ROW IS DRAWN ONLY IF IT FITS WHOLE. `Text_LayOutBlock` stops at
        // the block's bottom; drawing a row that straddles the edge left the
        // last line sliced in half against the screen.
        if (y >= clipTop && y + lineH <= clipBot) lay.drawRun(fb, left, y, pt.run);
        y += lineH;
    }
}

}  // namespace

int PlayState::phaseScreens() {
    const auto& fs = *fs_;
    auto& lay = *lay_;
    auto& comp = *comp_;
    auto& optMenu = *optMenu_;
    auto& inv = *inv_;
    auto& session = *session_;
    omk::Renderer& world = *world_;
    bool done = false;
    do {
        // ---- THE SNEAK'S INVENTORY ROWS ------------------------------
        //
        // The nine row widgets of list 0x004DE6F0 belong to the DEVICE, not
        // to one page: several pages carry the same list, and what a row
        // shows is whatever that page's own code wrote into it -
        // `sub_42AA00` reads the row's `+60` tag and asks the channel for a
        // name with `Game_RaiseEvent(33, ...)`. So the text has to follow the
        // PANEL the walk is on and not the screen. Filled once at the open,
        // it showed the carried items on the "Memoire" tab as well, which is
        // a different list - caught by looking at the tab, not by a number.
        //
        // Only the inventory page is filled. The other pages' rows are the
        // player's bio, his statistics and the memos, and which list each one
        // asks for has not been read: an empty row says so, where the carried
        // list would be a plausible-looking wrong answer.
        //
        // The channel, in the order the interface asks it: case 29 for the
        // count and case 33 for each name, both refusing (result 3) while no
        // list is open - which is why the open raised event 25 first.
        if (walk && openScreen == omk::kScreenSneak) {
            // `sub_49BEA0` (Utiliser) writes the screen slot's state word to
            // **3** and returns 1 when `sub_42B470` returned 1 - and
            // docs/UI.md's state machine says 3 is CLOSING. So a successful
            // use CLOSES THE SNEAK. Acted on at the end of this block, where
            // nothing else still holds `walk` or its panel.
            bool useClosedSneak = false;
            sneakRows.clear();
            sneakHidden.clear();
            const omk::UiPanel* pn = walk->panel();
            // ---- THE SLIDER PAGE, and WHICH SOURCE fills the rows -----
            //
            // One global picks it: `dword_670CB8`, written by each page's
            // `panel+4` builder - 0 inventory, 2 memory, **4 slider** - and
            // `sub_42ADD0` branches on it. For 0 and 2 it raises the
            // inventory channel's event 25 with that number as the list id;
            // for 4 it raises NOTHING and instead sets flag `0x1000` on every
            // row widget, which is the flag `sub_42AA00` tests to take its
            // text from `sub_40E540(tag)` rather than from case 33.
            //
            // `sub_40E540`, and `sub_40E8E0` for the count, walk
            // `GLOBAL +16` (36-byte records, count `+28`) and keep only the
            // entries whose bit is set in the DB's `+24` array - which is
            // `StateArray::AddressEnabled`, what VM ops 87/88 write. So the
            // page lists the places the game has given the player, and a
            // capture of the original shows exactly that: four rows.
            // WHICH SOURCE, not which panel. `dword_670CB8` is what the
            // engine dispatches on, and the row bindings are a static record
            // that survives a descent - so the names stay put when an object
            // is chosen and the walk moves to the verb panel.
            const int rowKind = walk->rowKind();
            if (rowKind == 4) {
                std::vector<std::string> known;
                std::vector<const omk::Destination*> knownRecs;   // the same rows, for the listing
                for (const auto& d : destinations)
                    if (state.bit(omk::StateArray::AddressEnabled, d.bit)) {
                        known.push_back(d.name); knownRecs.push_back(&d);
                    }
                const omk::UiPanel* rp = w.at(omk::kPanelSneakSlider);
                for (const auto& l : (rp ? rp->lists : pn->lists)) {
                    if (l.addr != omk::kListSneakRows) continue;
                    for (std::size_t k = 0; k < l.items.size(); ++k) {
                        if (k >= known.size()) {
                            sneakHidden.insert(l.items[k].addr);   // sub_42AAE0
                            continue;
                        }
                        sneakRows[l.items[k].addr] = known[k];
                    }
                }
                // `sub_42AAE0`'s OTHER half: the rows past the end are not
                // selectable either, so the walk must not put the highlight
                // on one. Without this the selection walks off the end of
                // the live destinations and the cursor goes with it.
                // CARRYING the window, not resetting it. This runs every
                // frame, so passing 0 would scroll the list back to the top
                // between the keypress and the next draw.
                walk->bindRows(omk::kListSneakRows,
                               static_cast<int>(known.size()),
                               walk->rowWindow(omk::kListSneakRows));
                if (!sliderTold) {
                    sliderTold = true;
                    std::printf("sneak: slider page - %zu of %zu destinations "
                                "enabled (GLOBAL +16, DB +24)\n",
                                known.size(), destinations.size());
                    for (std::size_t r = 0; r < known.size(); ++r)
                        std::printf("   row %zu: '%s' (area %d, address bit %d)\n", r,
                                    knownRecs[r]->name.c_str(), knownRecs[r]->area, knownRecs[r]->bit);
                }
            }
            // ---- THE EXAMINE PAGE'S CONTENT -------------------------
            //
            // Which object is being examined is the ROW the walk was on when
            // "Examiner" was confirmed, and the row list keeps its selection
            // (it is a static record), so it is still there. `Game_HandleEvent`
            // case 40 then dispatches on that object's own kind.
            // ---- A VERB WAS CONFIRMED -------------------------------
            //
            // `sub_42B470`'s decision is the record's own `+4 & 1`, which is
            // `usable()`: yes and the engine runs
            // `Object_ApplyEffect(rec, Actor_IdBySlot(Actor_Player()))`, no
            // and it plays interface sound 13 and does nothing else. The
            // REFUSAL is ported whole; the apply is announced and not run,
            // because `Object_ApplyEffect`'s body is `named` and not read and
            // `sub_409780`'s context gate - whether the object may be used
            // HERE - has not been read at all.
            // ---- THE COMBINE, once both slots are full ----------------
            //
            // `Game_HandleEvent` case 37's SECOND arm:
            //
            //     recipe = sub_409650(second, first)
            //     if (!recipe || dword_4E6C70 != recipe+6) { result 2; }
            //     else { ObjectList_RemoveById(list, first);
            //            ObjectList_RemoveById(list, second);
            //            ObjectList_InsertFront(list, recipe+4, 0, 0);
            //            dword_4E6C70 = -1; }
            //
            // and `sub_49BC60`'s tail plays interface sound 12 on the success
            // and shows text 35 on the failure, then reinstalls the inventory
            // page either way. The gate is the one `beginCombine` set.
            // THE INTERFACE SOUNDS BY THEIR OWN ID. `sub_482D90(n)` takes an
            // index into the 45-row table (`tables/ui.json` sounds), not a
            // screen's slot - so 12 is `SNK009` and 13 `SNK012`, where
            // `sndConfirm` / `sndBack` are the screen's slots 1 and 2 (`SNK003`
            // and `SNK001` on the sneak). A reader: *"there should be a sound
            // effect when a new object is created"*.
            const auto uiSound = [&](int id) -> const std::vector<float>& {
                static std::map<int, std::vector<float>> cache;
                auto it = cache.find(id);
                if (it == cache.end()) {
                    std::vector<float> v;
                    const std::string& nm = w.soundNameById(id);
                    if (!nm.empty())
                        if (const auto path = fs.resolve("I2D/sounds/" + nm + ".wav"))
                            v = wavToDevice(omk::DataFs::readPath(*path), kDeviceRate);
                    it = cache.emplace(id, std::move(v)).first;
                }
                return it->second;
            };
            if (int ra = -1, rb = -1; walk->takeCombine(ra, rb)) {
                const auto bag = omk::objectList(state, omk::ObjectList::Carried);
                const auto at = [&](int r) {
                    return r >= 0 && static_cast<std::size_t>(r) < bag.size()
                         ? bag[static_cast<std::size_t>(r)] : -1;
                };
                const int a = at(ra), b = at(rb);
                const int gate = (a == omk::globalSpellItem(globalFile) ||
                                  b == omk::globalSpellItem(globalFile)) ? 1 : 0;
                const int made = (a >= 0 && b >= 0) ? inv.combine(a, b, gate) : -1;
                if (made > 0) {
                    state.listRemove(0, a);
                    state.listRemove(0, b);
                    state.listAdd(0, made);          // InsertFront
                    blip(uiSound(12));               // `push 0Ch; call sub_482D90`
                    std::printf("sneak: combine %d '%s' + %d '%s' (gate %d) -> "
                                "%d '%s'\n", a, session.objectName(a).c_str(),
                                b, session.objectName(b).c_str(), gate,
                                made, session.objectName(made).c_str());
                } else {
                    blip(sndBack);
                    std::printf("sneak: combine %d '%s' + %d '%s' (gate %d) -> "
                                "nothing - no recipe, or its gate is not %d "
                                "(interface text 35)\n",
                                a, session.objectName(a).c_str(),
                                b, session.objectName(b).c_str(), gate, gate);
                }
                walk->endCombine();
                // read back from the walk, after the combine closed: is the
                // verb still flashing, and which list holds the focus
                std::printf("sneak: combine closed - `Utiliser sur` %s, focus on list %d\n",
                            (walk->itemFlagsOn(omk::kItemSneakUseOn) & 0x2u) ? "STILL FLASHING"
                                                                             : "no longer lit",
                            walk->currentList());
            }
            if (const int verb = walk->takeVerb(); verb >= 0) {
                const auto carried =
                    omk::objectList(state, omk::ObjectList::Carried);
                // THE ROW TAG, not the widget index. All three verb
                // callbacks read `selected_widget[+0x3C]`, which is
                // `widget + window`; the two agree only while the rows are
                // unscrolled, and reading the selection applied the verb to
                // whatever had been under the cursor before the scroll.
                const int row = walk->selectedRow(omk::kListSneakRows);
                const omk::ObjectRecord* rec = nullptr;
                if (row >= 0 && static_cast<std::size_t>(row) < carried.size()) {
                    const int idx = carried[static_cast<std::size_t>(row)];
                    if (idx >= 0 &&
                        static_cast<std::size_t>(idx) < objectRecords.size())
                        rec = &objectRecords[static_cast<std::size_t>(idx)];
                }
                if (!rec) {
                    std::printf("sneak: %s with no object selected\n",
                                verb ? "Utiliser sur" : "Utiliser");
                } else {
                  // the OBJECTS id of the selected row - both halves need it
                  const int objIdx = carried.empty() ? -1
                      : carried[static_cast<std::size_t>(row)];
                  if (verb == 1) {
                    // ---- `Utiliser sur` IS A MODE, not a use --------------
                    //
                    // `sub_49BF30` does not touch case 35 at all: it opens a
                    // COMBINE, puts the object in one of two slots, disables
                    // the verb list and sends the player back to the rows for
                    // a second object. Running `Utiliser`'s arm here - which
                    // this did until 2026-09-04 - took the object IN HAND
                    // under the other verb's name.
                    //
                    // Which slot is `sub_42B520`'s answer: event 37's first
                    // arm compares the object with `u16(GLOBAL, 64)`, the
                    // spell item, and sets the recipe gate to 1 for it and 0
                    // for anything else.
                    const int spellItem = omk::globalSpellItem(globalFile);
                    const bool isSpell = (objIdx == spellItem);
                    // The slot takes the ROW, not the object id: `sub_49BF30`
                    // stores the selected widget's row tag (`[+3Ch]`), the
                    // second pick stores its row the same way, and the
                    // resolver below maps BOTH through the carried list.
                    // Handing it `objIdx` made the first slot "row 18" - a
                    // row that does not exist - so every combine came back
                    // `-1 ''` and nothing could ever be made. The spell test
                    // above still reads the object id, which is what event
                    // 37 compares.
                    walk->beginCombine(row, isSpell);
                    std::printf("sneak: Utiliser sur '%s' -> combine opened, "
                                "gate %d%s. Pick a second object\n",
                                rec->name.c_str(), isSpell ? 1 : 0,
                                isSpell ? " (the spell item - and NO shipped "
                                          "recipe carries gate 1, so this arm "
                                          "cannot produce anything)" : "");
                  } else if (verb == 0) {
                    // ---- WHAT REACHES THE WORLD -------------------------
                    //
                    // `sub_42B420(tag, 20)` announces, and its second event
                    // is 43 - whose block starts at the ACTION, so case 43
                    // runs `Message_RunHandlers(20, ..., object, ...)`. That
                    // walks the resident SCENE's subscription table, then the
                    // AREA's, then GLOBAL's, first match wins; and this
                    // header already recorded that a message's sender is an
                    // OBJECT id for 4, 20 and 25.
                    //
                    // So `Utiliser` posts message 20 with the object, and
                    // whichever resident chunk subscribes to it decides -
                    // which is why a player says the key "is automatically
                    // used when you are near the location where you should
                    // use it": proximity is which SCENE is resident, and the
                    // handler is its own. Nothing in the Session posted a
                    // message before this.
                    const bool ran = session.postMessage(20, objIdx);
                    const auto& m = session.messagesRun();
                    std::printf("sneak: Utiliser '%s' -> message 20, sender "
                                "object %d - %s\n", rec->name.c_str(), objIdx,
                                ran && !m.empty()
                                  ? (m.back().table + " table handles it").c_str()
                                  : "no resident chunk subscribes to it");
                  }
                  // ...and THEN the decision. `sub_49BEA0` calls
                  // `sub_42B420` (the announce, above) and `sub_42B470` (this)
                  // in that order, so both happen on one confirm.
                  //
                  // `Utiliser`'s ONLY. `Utiliser sur` is `sub_49BF30`, a
                  // different callback that opens the combine and returns -
                  // it never calls `sub_42B470`. This block used to run for
                  // BOTH verbs, so `Utiliser sur` opened the combine and then,
                  // on the same press, took the object IN HAND and closed the
                  // device. A reader: *"Utiliser sur does not work correctly
                  // (it means using on another object in the sneak, not
                  // interacting with the environment)"*.
                  if (verb != 0) {
                    // the combine is open; the rows are waiting for object two
                  } else if (!rec->usable()) {
                    // THE ARM THAT WORKS, and it is the one WITHOUT the
                    // usable bit. Case 35's `loc_407314` loads the object's
                    // own model from its stem and returns result **1**, and
                    // `sub_42B470` then runs
                    // `sub_41C490(dword_930724, tag)`, which writes
                    // `player[+0xA4] = &unk_4E7EA0[tag * 96]` and attaches
                    // the model to him. So "Utiliser" on a key TAKES IT IN
                    // HAND - which is what a player then carries to a door.
                    // ...and it IS the hand now. `Session::useObject` is
                    // case 35's arm: allocate a `word_4E6CA0` slot for the
                    // id, drop the item from list 0, and hold that slot -
                    // which is exactly what `var.set.used_object` (75) reads
                    // back. The MODEL attach (`sub_437400`/`sub_4374E0`
                    // inside `sub_41C490`) is the renderer's half and is
                    // still not done, so nothing appears in his hand.
                    const int slot = session.useObject(objIdx);
                    std::printf("sneak: '%s' -> IN HAND, slot %d (case 35 "
                                "result 1, sub_41C490 sets player+0xA4). A "
                                "zone whose activate script reaches opcode 75 "
                                "will now see object %d; the model attach is "
                                "not ported, so it is invisible\n",
                                rec->name.c_str(), slot, objIdx);
                    // `sub_42B470` returns 1 on this arm alone; `sub_49BEA0`
                    // turns that into `[slot+8] = 3`.
                    useClosedSneak = true;
                } else {
                    // ---- THE CONSUMABLE ARM, and it now CONSUMES ----------
                    //
                    // `Game_HandleEvent` case 35, the `rec+4 & 1` branch:
                    //
                    //     Object_ApplyEffect(rec, Actor_IdBySlot(Actor_Player()));
                    //     ObjectList_RemoveAt(0, row);        // result 2
                    //
                    // `Object_ApplyEffect` (0x00409780), the consumable half
                    // (`rec+4 & 0x20` clear): `rec+6` picks the property
                    // (`omk::effectProperty` - 6 is *Vie*), `rec+8` is added
                    // to the current value through `Actor_GetProperty` /
                    // `Actor_SetProperty`, and a sum past 0xFFFF becomes
                    // 0xFFFF (`v7 = -1`). The setter applies its own per-
                    // property clamp, which `writeActorProperty` carries.
                    //
                    // This used to ANNOUNCE the apply and run nothing - "the
                    // apply is announced and not run" - so a medkit did
                    // nothing and stayed in the bag. A reader: *"impossible to
                    // use health items"*.
                    //
                    // NOT ported, labelled: the `rec+4 & 0x20` arm (weapons,
                    // ammunition, seteks, rings - the valuables), which writes
                    // property 35's ammunition slots and 4/5; nothing usable
                    // AND valuable reaches it through this verb in practice,
                    // and it is left announcing rather than guessed.
                    const int prop = omk::effectProperty(rec->effect);
                    std::int32_t before = -1, after = -1;
                    if (prop > 0 && !rec->valuable()) {
                        const auto pr = state.rawMutable().subspan(
                            static_cast<std::size_t>(omk::GameState::kPlayerRecord),
                            static_cast<std::size_t>(omk::GameState::kPlayerRecordSize));
                        omk::readActorProperty(pr, prop, before);
                        std::int32_t next = before + rec->amount;
                        if (next > 0xFFFF) next = 0xFFFF;             // `v7 = -1`
                        omk::writeActorProperty(pr, prop, next);
                        omk::readActorProperty(pr, prop, after);      // the setter's clamp
                        state.listRemove(0, objIdx);                  // ObjectList_RemoveAt(0, row)
                    }
                    blip(uiSound(13));               // `sub_42B470`: interface sound 13
                    if (after >= 0)
                        std::printf("sneak: '%s' USED - property %d %d -> %d "
                                    "(+%d, effect %d), and it leaves the bag "
                                    "(case 35 result 2)\n",
                                    rec->name.c_str(), prop, before, after,
                                    rec->amount, rec->effect);
                    else
                        std::printf("sneak: '%s' is a consumable on the VALUABLES "
                                    "arm of Object_ApplyEffect (effect %d), which "
                                    "is not ported - announced, not run\n",
                                    rec->name.c_str(), rec->effect);
                    // `sub_42B470` returned 0, so `sub_49BEA0` takes
                    // `loc_49BEF8`: reset the row list and
                    // `sub_42A370(screen, unk_4DEE50)` - back to the
                    // INVENTORY page, screen still open.
                    walk->installPanel(omk::kPanelSneakInventory);
                  }
                }
            }
            comp.setExamineText(nullptr);
            // ---- THE CITY MAP (`ui/citymap.h`) ---------------------------
            //
            // `Lire plan` installs panel 0x004DF190, whose open hook
            // `sub_49D9E0` loads `Images\\<resident set>.bmp` and matches the
            // uppercased stem against the compiled four-row table. Everything
            // the two draw hooks then need is resolved HERE, because none of
            // it is a property of the widget tree: the bitmap, the city row,
            // where the player stands and which way he faces, and the markers.
            //
            // THE MARKERS are the ENABLED slider destinations whose names
            // begin with the city's - the same `GLOBAL +16` list the slider
            // page shows, filtered by the DB's AddressEnabled bits. The
            // engine positions each through the 15-row OVERRIDE table first
            // and falls back on `sub_40E630`, which is the TRANSPORT and
            // would `Area_Load` from a draw hook; the port instead resolves
            // only against the RESIDENT chunk's own address table and counts
            // what it had to drop (`ui/citymap.h` says why that costs nothing
            // in the shipped data: every destination of a city carries that
            // city's area id, so the engine takes its same-area fast path).
            static omk::ScreenComposer::CityMapView cityView;
            comp.setCityMap(nullptr);
            if (pn && pn->addr == omk::kPanelSneakMap) {
                static std::string cityBmpStem;
                static omk::Surface cityBmp;
                const std::string stem = session.setName();
                if (stem != cityBmpStem) {
                    cityBmpStem = stem;
                    // `sprintf("Images\\%s.bmp")` then `fopen`. DataFs
                    // resolves case-insensitively, which is what the shipped
                    // lower-case `anekbah.bmp` against an upper-case set name
                    // needs.
                    cityBmp = omk::surfaceFromBmp(fs.read("IMAGES/" + stem + ".bmp"));
                }
                cityView = omk::ScreenComposer::CityMapView{};
                cityView.sheet = cityBmp.valid() ? &cityBmp : nullptr;
                cityView.row = cityMaps.findCity(stem);
                cityView.playerX = session.playerPos()[0];
                cityView.playerZ = session.playerPos()[2];
                cityView.playerFacing = session.playerYaw();
                {
                    const auto raw = state.raw();
                    const std::size_t rec =
                        static_cast<std::size_t>(omk::GameState::kPlayerRecord);
                    for (std::size_t i = rec + 8;
                         i < raw.size() && raw[i] != std::byte{0}; ++i)
                        cityView.playerName.push_back(static_cast<char>(raw[i]));
                }
                int unplaced = 0;
                if (cityView.row) {
                    const auto& rs = session.residentSlot(session.activeSlot());
                    for (const auto& d : destinations) {
                        if (!state.bit(omk::StateArray::AddressEnabled, d.bit)) continue;
                        const std::string place =
                            omk::cityMapPlaceName(d.name, cityView.row->name);
                        if (place.empty()) continue;
                        omk::ScreenComposer::CityMapView::Marker mk;
                        mk.name = place;
                        if (const omk::CityPlaceRow* pr = cityMaps.findPlace(place)) {
                            mk.x = pr->pos[0];
                            mk.z = pr->pos[2];
                        } else {
                            const omk::Address* ad = nullptr;
                            for (const auto& x : rs.addresses)
                                if (x.id == d.bit) ad = &x;
                            if (!ad) { ++unplaced; continue; }
                            mk.x = ad->pos[0];
                            mk.z = ad->pos[2];
                        }
                        cityView.markers.push_back(mk);
                    }
                }
                comp.setCityMap(&cityView);
                static std::string mapTold;
                const std::string said =
                    "set '" + stem + "' -> " +
                    (cityBmp.valid() ? "Images/" + stem + ".bmp " +
                         std::to_string(cityBmp.w) + "x" + std::to_string(cityBmp.h)
                                     : std::string("no bitmap")) +
                    ", city " + (cityView.row ? cityView.row->name + " id " +
                                 std::to_string(cityView.row->id)
                                              : std::string("none (tag -1)")) +
                    ", " + std::to_string(cityView.markers.size()) +
                    " markers, " + std::to_string(unplaced) + " unplaced";
                if (said != mapTold) {
                    mapTold = said;
                    std::printf("sneak map: %s\n", said.c_str());
                }
            }
            // ---- THE IDENTITY PAGE'S SHEET (todo/sneak.md §5e step 2) -------
            //
            // What `Actor_GetProperty` (event 44) hands the identity hooks for
            // the player, read where it reads it - the character record, which
            // for the player is the DB's at `+60`. Cases 0/6/9..13 return a
            // POINTER into the record (`+108`, `+8`, `+128`, `+136`, `+40`,
            // `+116`, `+124`), 14 and 15 the two pointers the record itself
            // holds at `+4` and `+0` - the bio strings `State_Apply` plants.
            // All are C strings: read up to their NUL.
            static omk::ScreenComposer::PlayerSheet playerSheet;
            comp.setPlayerSheet(nullptr);
            if (pn && pn->addr == omk::kPanelSneakIdentity) {
                static const auto sneakText = omk::iamStrings(fs, "IAM/Sneak");
                playerSheet.text = sneakText;
                const auto raw = state.raw();
                const auto cstr = [&](std::size_t at) {
                    std::string s;
                    for (std::size_t i = at; i < raw.size() && raw[i] != std::byte{0}; ++i)
                        s.push_back(static_cast<char>(raw[i]));
                    return s;
                };
                const std::size_t rec = static_cast<std::size_t>(omk::GameState::kPlayerRecord);
                const auto u32at = [&](std::size_t at) {
                    std::uint32_t v = 0;
                    for (int k = 0; k < 4; ++k)
                        v |= static_cast<std::uint32_t>(raw[at + k]) << (8 * k);
                    return v;
                };
                playerSheet.str = {
                    {0, cstr(rec + 108)}, {6, cstr(rec + 8)},   {9, cstr(rec + 128)},
                    {10, cstr(rec + 136)}, {11, cstr(rec + 40)}, {12, cstr(rec + 116)},
                    {13, cstr(rec + 124)},
                    // 14 and 15 dereference the record's `+4` and `+0`, which
                    // `State_Apply` plants as the two bio blocks at image 592
                    // and 336 (`GameState::relocate`). The viewer's copy is not
                    // relocated - its `+0`/`+4` still hold what the save
                    // stored - so the blocks those pointers always designate
                    // are read directly.
                    {14, cstr(static_cast<std::size_t>(omk::GameState::kBio[1]))},
                    {15, cstr(static_cast<std::size_t>(omk::GameState::kBio[0]))},
                };
                static bool bioTold = false;
                if (!bioTold) {
                    bioTold = true;
                    std::printf("sneak: identity bio pointers as stored +0 %#x +4 %#x "
                                "(State_Apply plants %d / %d)\n", u32at(rec + 0), u32at(rec + 4),
                                omk::GameState::kBio[0], omk::GameState::kBio[1]);
                }
                const auto i16at = [&](std::size_t at) {
                    return static_cast<int>(static_cast<std::int16_t>(
                        static_cast<std::uint16_t>(raw[at]) |
                        static_cast<std::uint16_t>(static_cast<std::uint16_t>(raw[at + 1]) << 8)));
                };
                // The integers event 44 answers from the same record, for both
                // contents: 8 the age, and the Caracteristiques - 1 Energie
                // `+170`, 16 Attaque `+160`, 19 Maitrise `+166`, 17 Resistance
                // `+162`, 3 Vitesse `+158`, 18 Esquive `+164`, 2 Mana `+156`.
                playerSheet.num = {{8, i16at(rec + 154)}, {1, i16at(rec + 170)},
                                   {16, i16at(rec + 160)}, {19, i16at(rec + 166)},
                                   {17, i16at(rec + 162)}, {3, i16at(rec + 158)},
                                   {18, i16at(rec + 164)}, {2, i16at(rec + 156)}};
                static std::string characteristicsTold;
                std::string cs;
                for (int p : {1, 16, 19, 17, 3, 18, 2})
                    cs += " | " + std::to_string(p) + "=" + std::to_string(playerSheet.num[p]);
                if (cs != characteristicsTold) {
                    characteristicsTold = cs;
                    std::printf("sneak: characteristics sheet%s (rank string %d)\n", cs.c_str(),
                                playerSheet.num[19] / 41 <= 4 ? 36 + playerSheet.num[19] / 41 : -1);
                }
                if (const omk::UiList* tabs = w.listAt(omk::kListSneakTabs))
                    if (!tabs->items.empty())
                        for (int c = 0; c < 3; ++c)
                            playerSheet.icon[c] = static_cast<std::uint8_t>(tabs->items.front().rgb[c]);
                comp.setPlayerSheet(&playerSheet);
                // ---- THE CHARACTER VIEW (`sub_4778E0`, step 4) ---------------
                //
                // Built once per player model - the engine builds it in the
                // sneak's open; only this page shows it. The bank is the
                // LITERAL "F1AVNT.CTL" the open pushes (0x004DF5EC), whatever
                // the player's own; `Cef_DefaultGroup` is the first group with
                // flag 1 and `Cef_DefaultClip` that group's flag-0x20 entry's
                // clip, NOT followed through a goto; `Anim_SetFrame(node, clip,
                // 0.0, 1.0)` applies frame 1 - key 2, key 0 being the rest
                // sentinel - to every node, the root's rotation included.
                // `Anim_RootDelta`'s 0 -> 1 step moves the node, which the
                // camera does not follow (it targets the model's centre), so
                // it is not applied here.
                static std::string characterBuiltFor;
                if (characterBuiltFor != playerModel) {
                    characterBuiltFor = playerModel;
                    CharModel* cm = charModelFor(playerModel);
                    const std::string characterBank = "F1AVNT";   // 0x004DF5EC, literal
                    CharBank* cb = charBankFor(characterBank);
                    int entry = -1, clip = -1;
                    int clipFrames = 0;
                    std::string firstTrack;
                    if (cb && cb->ready)
                        for (const auto& g : cb->ctl.groupList)
                            if (g.flags & 1u) { entry = g.defaultEntry; break; }
                    if (cb && entry >= 0 && entry < static_cast<int>(cb->ctl.states.size()))
                        clip = cb->ctl.states[static_cast<std::size_t>(entry)].clip;
                    int bound = 0;
                    if (cm && cm->ready && clip >= 0 &&
                        clip < static_cast<int>(cb->ctl.clips.size())) {
                        omk::NodeTracks t;
                        const auto d = omk::animDescriptor(
                            cb->data, cb->ctl.clips[static_cast<std::size_t>(clip)].offset);
                        if (d && d->frames > 0 && !d->tracks.empty()) {
                            clipFrames = d->frames;
                            firstTrack = d->tracks.front().name;
                            const auto lower = [](std::string v) {
                                for (auto& c : v) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
                                return v;
                            };
                            t.count = static_cast<int>(d->tracks.size());
                            t.frames = 1;
                            t.rootTrack = -1;
                            // THE BINDING IS BY BONE, NOT BY NAME. F1AVNT's
                            // tracks are `Sh`-prefixed (`ShBassin`) and Kay'l's
                            // meshes `U`-prefixed (`UBassin`), so an equality
                            // test binds 0 of 19 and he stands in his T-pose -
                            // which is what this drew first. The engine finds a
                            // bone by `strstr` on the node names
                            // (`docs/ASSETS.md`, "the bones are found by
                            // strstr"), and the port's rule for the same
                            // pairing is the crowd's and the gunmen's: the bone
                            // begins at the SECOND UPPERCASE LETTER
                            // (`verify.py: bone names`). Exact first, as there.
                            const auto boneOf = [&](const std::string& n) {
                                for (std::size_t i = 1; i < n.size(); ++i)
                                    if (n[i] >= 'A' && n[i] <= 'Z') return lower(n.substr(i));
                                return n.size() > 2 ? lower(n.substr(2)) : lower(n);
                            };
                            for (const auto& tr : d->tracks) {
                                std::int32_t mi = -1;
                                const std::string want = lower(tr.name);
                                for (const auto& m : cm->meshes)
                                    if (lower(m.name) == want) { mi = m.index; break; }
                                if (mi < 0) {
                                    const std::string bone = boneOf(tr.name);
                                    for (const auto& m : cm->meshes)
                                        if (boneOf(m.name) == bone) { mi = m.index; break; }
                                }
                                if (mi >= 0) ++bound;
                                t.ids.push_back(mi);
                            }
                            t.quats.assign(1, {});
                            t.trans.assign(1, {0.0f, 0.0f, 0.0f});
                            t.quats[0].resize(d->tracks.size());
                            for (std::size_t i = 0; i < d->tracks.size(); ++i) {
                                const omk::AnimTrack& tr = d->tracks[i];
                                if (!tr.rotOffset || tr.rotKeys <= 0) continue;
                                const int key = tr.rotKeys > 2 ? 2 : tr.rotKeys - 1;   // frame 1
                                const std::size_t o = tr.rotOffset + 16u * static_cast<std::size_t>(key);
                                if (o + 16 > cb->data.size()) continue;
                                float qv[4];
                                std::memcpy(qv, cb->data.data() + o, 16);
                                t.quats[0][i] = {qv[0], qv[1], qv[2], qv[3]};
                            }
                            const auto pose = omk::composePose(cm->meshes, t, 0, false);
                            omk::Geometry posed = cm->rest;
                            omk::applyPose(posed, cm->rest, cm->meshes, pose);
                            uiModels.setCharacter(std::move(posed), cm->tex, playerModel);
                        }
                    }
                    // The bank from the variable it was loaded under, and the
                    // clip's own length and first track: H1AVNT also opens on
                    // entry 0 'H_STAND', clip 0, and binds 19 by exact name, so
                    // only these tell the literal F1AVNT from the player's bank.
                    std::printf("sneak: identity character '%s' - bank %s, default entry %d "
                                "'%s', clip %d (%d frames, first track '%s'), %d tracks bound, "
                                "frame 1\n", playerModel.c_str(), characterBank.c_str(),
                                entry,
                                cb && entry >= 0 && entry < static_cast<int>(cb->ctl.states.size())
                                    ? cb->ctl.states[static_cast<std::size_t>(entry)].name.c_str() : "",
                                clip, clipFrames, firstTrack.c_str(), bound);
                }
                static std::string identityTold;
                std::string said;
                for (int p : {6, 8, 0, 13, 9, 10, 12, 11, 15, 14})
                    said += " | " + std::to_string(p) + "=" +
                            (p == 8 ? std::to_string(playerSheet.num[8]) : playerSheet.str[p]);
                if (said != identityTold) {
                    identityTold = said;
                    std::printf("sneak: identity sheet%s\n", said.c_str());
                }
            }
            if (pn && pn->addr == omk::kPanelSneakExamine) {
                const auto carried =
                    omk::objectList(state, omk::ObjectList::Carried);
                // BY ADDRESS: the examine page carries no row list of its
                // own, and the selections are a static record keyed by list,
                // so the row chosen two panels ago is still there.
                // ...and the TAG here too: `sub_49BFF0` latches
                // `dword_4DE74C = selected_widget[+0x3C]` when Examiner is
                // confirmed, and the page draws whatever that names.
                const int row = walk->selectedRow(omk::kListSneakRows);
                // `sub_49BFF0`'s last call, `sub_42B420(tag, 4)`: event 30
                // answers the object's slot and event 43 runs
                // `Message_RunHandlers(4, player, slot)`, whose sender is
                // `ObjectSlot_Id(slot)` - the object. The port never posted
                // it, so the GLOBAL table's handler never ran from here.
                if (walk->takeExamineMessage()) {
                    const int eid = row >= 0 && static_cast<std::size_t>(row) < carried.size()
                        ? carried[static_cast<std::size_t>(row)] : -1;
                    const bool ran = eid >= 0 && session.postMessage(4, eid);
                    const auto& runs = session.messagesRun();
                    std::printf("sneak: Examiner row %d id %d -> message 4 %s\n", row, eid,
                                ran && !runs.empty()
                                    ? ("handled by the " + runs.back().table + " table").c_str()
                                    : "(no resident chunk subscribes)");
                }
                if (row >= 0 && static_cast<std::size_t>(row) < carried.size()) {
                    const int idx = carried[static_cast<std::size_t>(row)];
                    if (idx >= 0 &&
                        static_cast<std::size_t>(idx) < objectRecords.size()) {
                        const auto& rec = objectRecords[static_cast<std::size_t>(idx)];
                        // Case 30 loads the model for whatever is SELECTED,
                        // whatever its kind, and case 40 hands the
                        // description back on every arm - so the page gets
                        // both: the object's own prop and its text.
                        // WHICH CONTENT. Two captures of the original
                        // settle it between them: Kay'l's apartment key is
                        // kind 0 and shows a 3D model with a one-line label,
                        // and the MK400 notice is kind 15 and shows TEXT
                        // ONLY - no prop behind it, though its record does
                        // name one (PAPIER). So the document kinds 15 and 16
                        // suppress the model, and everything else shows it.
                        const auto k = uiModels.examine(
                            fs, (rec.kind == 15 || rec.kind == 16) ? rec.kind : 15,
                            (rec.kind == 15) ? std::string() : rec.stem);
                        examineText = rec.description;
                        comp.setExamineText(&examineText);
                        // ...and where it is scrolled to. The composer
                        // CLAMPS this against the laid-out height, the way
                        // `Ui_ItemTextStyle` clamps `dword_6A5090`, so it
                        // takes the walk's own field rather than a copy.
                        comp.setTextScroll(&walk->textScroll());
                        if (rec.stem != examineTold) {
                            examineTold = rec.stem;
                            std::printf("sneak: examine '%s' kind %d -> %s\n",
                                        rec.name.c_str(), rec.kind,
                                        k == omk::UiModels::Examine::Model ? "3D model"
                                        : k == omk::UiModels::Examine::Document
                                          ? "document bitmap" : "nothing (case 40 result 2)");
                        }
                    }
                }
            }
            // ---- THE MEMORY PAGE'S ROWS ARE THE MEMO JOURNAL ---------------
            //
            // Kind 2 is the memory page, and it binds OBJECT LIST 2:
            // `sub_42ADD0(rows, 0, 2)` raises the channel's event 25 on that
            // list and takes the count from event 29. This port drew that page
            // empty and `todo/sneak.md` §2c called it correct, on the grounds
            // that the builder's count `dword_4DE708` has no absolute store
            // anywhere in the image - but `0x004DE708` IS the row list's own
            // `+0x18` (`0x004DE6F0 + 24`), which `sub_42ADD0` writes through
            // the list pointer at `0x0042AF70`, exactly as it writes the shops'
            // and MULTIPLAN's counts. A reader playing it settled which was
            // right: *"info page contains important info you may have heard in
            // dialog"*. 76 `inventory.add` sites fill list 2 with `Memo NNN ...`
            // objects, whose record NAME is the memo's heading ("Moi :") and
            // whose description is its body.
            if (rowKind == 2 && inv.openedList() != 2) inv.openList(2);
            if ((rowKind == 0 || rowKind == 2) && inv.openedList() >= 0) {
                const omk::ObjectList rowSource = rowKind == 2
                    ? omk::ObjectList::Memos : omk::ObjectList::Carried;
                const auto carried = omk::objectList(state, rowSource);
                // ---- `sub_42AAE0`, THE ROW BINDER --------------------
                //
                // The nine widgets are a WINDOW onto the list, and which of
                // them are live is decided per row rather than by drawing
                // whatever has text:
                //
                //     for each widget k of the list:
                //       if (k + window >= list+24)     // past the end
                //           item+60 = -1;              // tag: empty
                //           set item 0x40000001;       // and NOT DRAWN
                //           set item 0x20000004;       // and unselectable
                //       else
                //           item+60 = k + window;      // the row it shows
                //           clear those two;
                //
                // so the engine draws only the rows that HOLD something -
                // two of nine in the user's capture - and that is the gate
                // `Ui_DrawItemFill`'s bars are behind. `list+24` is the
                // count the channel reports (case 29).
                //
                // THE WINDOW is `sub_42AFF0`'s, kept in widget 0's `+0x3C`
                // and moved by the mover - so the text follows the scroll
                // rather than always starting at row 0. Hardcoded 0 until
                // 2026-09-04, which truncated any list longer than the nine
                // widgets: a tenth carried object could not be reached.
                const omk::UiPanel* rp = w.at(omk::kPanelSneakInventory);
                for (const auto& l : (rp ? rp->lists : pn->lists)) {
                    if (l.addr != omk::kListSneakRows) continue;
                    const std::size_t window = static_cast<std::size_t>(
                        std::max(0, walk->rowWindow(omk::kListSneakRows)));
                    for (std::size_t k = 0; k < l.items.size(); ++k) {
                        const std::size_t row = k + window;
                        if (row >= carried.size()) {
                            // `sub_42AAE0`: past the end, so tag -1 and
                            // `0x40000001` - not drawn and not selectable.
                            sneakHidden.insert(l.items[k].addr);
                            continue;
                        }
                        // `playerCount` is case 33's other half and is NOT
                        // read: for kinds 2..6 the quantity lives in the
                        // player record and which field it is has not been
                        // established, so those rows show the name without
                        // its " - N". Kinds 7..11 take the item's own `+12`
                        // and are complete.
                        sneakRows[l.items[k].addr] =
                            inv.displayName(carried[row], 0);
                    }
                }
                walk->bindRows(omk::kListSneakRows,
                               static_cast<int>(carried.size()),
                               walk->rowWindow(omk::kListSneakRows));
                // One line per change, so a headless run can be held to the
                // rows the page bound - and to WHICH list they came from.
                // ---- THE MEMO'S BODY, in the page's own box ----------------
                //
                // Item 0x004DEA98's draw hook `0x00477F60` raises event 40 on
                // the item's `+0x3C` and lays out what it answers - and that
                // tag field IS `dword_4DEAD4`, which the row hook writes with
                // the SELECTED row's tag whenever the row kind is 2. Case 40
                // hands back the object record's description on every arm, so
                // the box shows the selected memo's text. The composer draws it
                // through the same path as the examine page's.
                // ...and only while the BOX IS SHOWN. `0x0049D750` hides it
                // when the page is built and `0x0049D8B0` lights it once the
                // rows are the current list, so a memo body on an opened page
                // nobody has moved into is this port's invention, not the
                // game's.
                if (rowKind == 2 && walk->memoBodyShown()) {
                    const int sel = walk->selectedRow(omk::kListSneakRows);
                    const int memo = sel >= 0 && sel < static_cast<int>(carried.size())
                        ? carried[static_cast<std::size_t>(sel)] : -1;
                    if (const omk::ObjectRecord* mr = memo > 0 ? inv.record(memo) : nullptr) {
                        examineText = mr->description;
                        comp.setExamineText(&examineText);
                        comp.setTextScroll(&walk->textScroll());
                        // The body, named from the memo the rows selected and
                        // measured rather than described, so a check can hold
                        // it: the id, its heading, and how long the text is.
                        memoBodyPending = "row " + std::to_string(sel) + " id " +
                                          std::to_string(memo) + " '" + mr->name + "'";
                    }
                }
                if (rowKind == 2) {
                    // The list is named from the SOURCE the rows were read
                    // from, not from a literal: a mutation that made this page
                    // read list 0 still printed "object list 2" and only the
                    // ids gave it away.
                    static std::string memosTold;
                    const int from = static_cast<int>(rowSource);
                    std::string said = "object list " + std::to_string(from) + ", " +
                                       std::to_string(carried.size()) + " rows:";
                    for (int id : carried) said += " " + std::to_string(id);
                    if (said != memosTold) {
                        memosTold = said;
                        std::printf("sneak: memory page - %s\n", said.c_str());
                    }
                }
                // ---- THE CLOCK, and WHAT THE ECHO BAR NEEDS ---------
                //
                // Two of the device's rows are filled by callbacks of its
                // own, and both are readable - `sub_0049DC20` and
                // `sub_0049E090` carry no `proc` label (nothing calls them;
                // they are dwords in the widget table, CLAUDE.md 1's trap),
                // so `asmfn.py` returns a neighbour and the range has to be
                // dumped by hand.
                //
                // THE ECHO BAR IS NO LONGER COMPOSED HERE. It was: this
                // block used to put the selected item's own label into
                // `sneakRows` for it, which is right for three of the
                // function's seven arms and silent for the other four - and
                // the four include the one the page spends its time in, the
                // TAB LABEL with `  (n / 18)` after it, so the bar was blank
                // whenever the selection sat in the row list, which is
                // whenever the player is looking at his inventory.
                // `ScreenComposer::echoBarText` runs the whole function now,
                // and reports what it drew in `ScreenFrame::echoBar` - a
                // line composed where the drawing happens can be asserted;
                // one composed here could only report the intention.
                //
                // What the bar cannot get for itself is the two counts:
                // `sub_42B1C0(4)` and `(5)` raise `Game_HandleEvent(44)` on
                // the player, whose cases 4 and 5 are the player record's
                // `+172` and `+174` - the seteks and the anneaux.
                comp.setPlayerCounts(state.money(), state.rings());
                {
                    for (const auto& l : pn->lists) {
                        for (const auto& e : l.items) {
                            if (e.textFn == 0x0049E090u) {
                                // The clock. Both halves are the engine's own
                                // formatters, already ported and checked
                                // (`sub_0041E690`'s integer division); the
                                // " - " joining them is read off the user's
                                // screenshot - "12 Nadim 7216 - 13:01:15" -
                                // and is the one part of this line that is
                                // not from the code.
                                sneakRows[e.addr] =
                                    omk::formatDate(state.clockDay()) + " - " +
                                    omk::formatTime(state.clock());
                            }
                        }
                    }
                }
                if (!sneakTold++) {
                    // Count the ROWS, not the map: since the echo bar and
                    // the clock share `sneakRows` this reported "2 rows
                    // shown" for a list holding one object.
                    std::size_t rows = 0;
                    for (const auto& l : pn->lists)
                        if (l.addr == omk::kListSneakRows)
                            for (const auto& e : l.items)
                                rows += sneakRows.count(e.addr);
                    std::printf("sneak: object list %d holds %zu, %zu rows "
                                "shown, window at %d\n", inv.openedList(),
                                carried.size(), rows,
                                walk->rowWindow(omk::kListSneakRows));
                }
            }
            if (useClosedSneak) {
                std::printf("screen %d closed by the use - `sub_49BEA0` wrote "
                            "state 3 because `sub_42B470` returned 1 (event "
                            "%d, object list %d)\n", openScreen,
                            omk::kEventSneakClose, inv.openedList());
                inv.closeList();          // Game_RaiseEvent(26, 0)
                walk.reset();
                openScreen = -1;
                screenFromScript = true;
            }
        }
        // ---- THE SHOP'S STOCK ROWS (todo/shops.md step 2) ----------------
        //
        // `Ui_OpenShop` binds the nine rows with `sub_42ADD0(rows, 0, arm)`,
        // where the arm is 0 at the bank and -1 in the other nine. A list
        // argument that is not -1 raises event 25 on it, so the BANK opens
        // list 0 - the player's own inventory, which is what a bank buys -
        // and the nine keep the list `ui.open` named: 3, the stock, which is
        // the active area block's `+8` (`omk::shopStock`). Then case 29 for
        // the count and case 33 for each name, through the same window and
        // past-the-end rule as the sneak's rows (`sub_42AAE0`).
        // All THREE of the shop's panels share the header, so all three are
        // served here: the shop, the Vente confirm and the Examiner page.
        const std::uint32_t shopPanel =
            walk && walk->panel() ? walk->panel()->addr : 0u;
        if (shopPanel == omk::kPanelShop || shopPanel == omk::kPanelShopSellConfirm ||
            shopPanel == omk::kPanelShopExamine) {
            sneakRows.clear();
            sneakHidden.clear();
            if (w.screenParam(openScreen) == 0 && inv.openedList() != 0)
                inv.openList(0);                        // Game_RaiseEvent(25, 0)
            std::vector<int> ids;
            const auto readIds = [&]() {
                ids.clear();
                if (inv.openedList() == 3)
                    ids = omk::shopStock(session.residentSlot(session.activeSlot()).areaChunk);
                else if (inv.openedList() == 0)
                    ids = omk::objectList(state, omk::ObjectList::Carried);
            };
            readIds();
            // ---- STEP 4: THE PURCHASE AND THE SALE ------------------------
            //
            // The walk recorded which (`takeShop`); the channel carries it
            // out, as `Game_HandleEvent` does in the engine. A message goes
            // through `sub_42B820(0, -1, text)` into `byte_6A4CA0` and runs for
            // oscillator 0's 5000 ms, shown in place of line two (below).
            static std::string shopMessage;
            static long shopMessageMs = -1000000;
            const long shopNowMs = static_cast<long>(SDL_GetTicks());
            const auto buyText = omk::iamStrings(fs, "IAM/Buy");
            const auto str = [&](int id) {
                return id >= 0 && id < static_cast<int>(buyText.size())
                    ? buyText[static_cast<std::size_t>(id)] : std::string();
            };
            static std::map<int, std::vector<float>> shopSounds;
            const auto playId = [&](int id) {       // `Ui_PlaySound` (0x00482D90)
                auto it = shopSounds.find(id);
                if (it == shopSounds.end()) {
                    std::vector<float> pcm;
                    const std::string& nm = w.soundNameById(id);
                    if (!nm.empty())
                        if (const auto path = fs.resolve("I2D/sounds/" + nm + ".wav"))
                            pcm = wavToDevice(omk::DataFs::readPath(*path), kDeviceRate);
                    it = shopSounds.emplace(id, std::move(pcm)).first;
                }
                if (!it->second.empty()) blip(it->second);
            };
            const auto post = [&](int stringId) {
                shopMessage = str(stringId);
                shopMessageMs = shopNowMs;
            };
            if (int kind = -1, row = -1; walk->takeShop(kind, row)) {
                const int id = row >= 0 && row < static_cast<int>(ids.size())
                    ? ids[static_cast<std::size_t>(row)] : -1;
                const auto carried = omk::objectList(state, omk::ObjectList::Carried);
                const int money = state.money();
                const int price = id > 0 ? inv.price(id) : 0;
                if (kind == 0 && id > 0) {
                    // the row's Acheter arm: `sub_42B3E0(tag)` - event 41 -
                    // refused is sound 18 and nothing else
                    if (inv.shopAllows(id, carried) != omk::InvResult::Ok) {
                        playId(18);
                        std::printf("shop: buy %d refused by event 41 (a gun already held)\n", id);
                    } else if (static_cast<int>(carried.size()) >=
                                   omk::GameState::kListCapacity[0] || price > money) {
                        // case 38's result 2: `sub_42B300(tag) > sub_42B1C0(4)`
                        // picks 9 "Pas assez de seteks ! !", else 8 "Sneak plein !"
                        post(price > money ? 9 : 8);
                        playId(18);
                        std::printf("shop: buy %d refused by event 38 (price %d, money %d, "
                                    "carried %zu) - message %d\n", id, price, money,
                                    carried.size(), price > money ? 9 : 8);
                    } else {
                        // case 38: `Inventory_Insert` + `ObjectList_InsertFront(0, ...)`,
                        // then `u16(+172) -= price`. Inventory_Insert's merge of
                        // ammunition and money into an existing slot is not
                        // modelled (`GameState::listAdd` says so).
                        state.listAdd(0, id);
                        state.setMoney(money - price);
                        post(21);                        // "Objet achete !"
                        playId(16);
                        std::printf("shop: bought %d for %d, money %d -> %d\n",
                                    id, price, money, state.money());
                    }
                } else if (kind == 1 && id > 0) {
                    // `Oui` (0x004AEC00): event 36 request 10 - the record's flag
                    // 0x2 - and `sub_42B300(tag) > 0`, then event 39
                    const auto* rec = inv.record(id);
                    const bool sellable = rec && (rec->flags & 0x2) != 0 && price > 0;
                    if (!sellable) {
                        post(7);                         // "Vente non autorisee !"
                        std::printf("shop: sale of %d refused (flags %#x, price %d) - message 7\n",
                                    id, rec ? rec->flags : 0, price);
                    } else {
                        state.setMoney(std::min(money + price / 2, 0xFFFF));
                        state.listRemove(0, id);         // `ObjectList_RemoveAt(0, tag)`
                        playId(17);
                        std::printf("shop: sold %d for %d, money %d -> %d\n",
                                    id, price / 2, money, state.money());
                    }
                }
                readIds();
                // `if (!dword_4E3658) dword_4E3988 = 0` - an emptied list hands
                // the focus back to the buttons
                if (ids.empty()) walk->focusList(omk::kListShopButtons);
            }
            // ---- THE ANALYSER PAGE'S MODEL: event 30 on the chosen row -----
            //
            // The Examiner arm stores the row's tag and installs 0x004E39D8,
            // whose box draws `sub_42B2C0(tag)` - case 30, the preview load,
            // which loads the SELECTED object's model whatever its kind and
            // refuses list 2 (no models). The rows' selection is a static
            // record, so the row chosen on the shop panel is still selected.
            if (shopPanel == omk::kPanelShopExamine && inv.openedList() != 2) {
                const int row = walk->selectedRow(omk::kListShopRows);
                const int id = row >= 0 && row < static_cast<int>(ids.size())
                    ? ids[static_cast<std::size_t>(row)] : -1;
                static int shopExamined = -1;
                if (id > 0 && id != shopExamined &&
                    static_cast<std::size_t>(id) < objectRecords.size()) {
                    shopExamined = id;
                    const auto& rec = objectRecords[static_cast<std::size_t>(id)];
                    const auto k = uiModels.examine(fs, 15, rec.stem);
                    std::printf("shop: Analyser - object %d '%s', model %s\n", id,
                                rec.stem.c_str(),
                                k == omk::UiModels::Examine::Model ? "loaded" : "none");
                }
            }
            const int window = walk->rowWindow(omk::kListShopRows);
            for (const auto& l : walk->panel()->lists) {
                if (l.addr != omk::kListShopRows) continue;
                for (std::size_t k = 0; k < l.items.size(); ++k) {
                    const std::size_t row = k + static_cast<std::size_t>(std::max(0, window));
                    if (row >= ids.size()) {
                        sneakHidden.insert(l.items[k].addr);   // not drawn, not selectable
                        continue;
                    }
                    sneakRows[l.items[k].addr] = inv.displayName(ids[row], 0);
                }
            }
            walk->bindRows(omk::kListShopRows, static_cast<int>(ids.size()), window);
            // ---- THE HEADER's three text hooks (step 3) ------------------
            //
            //   0x004AEE30  the SELECTED BUTTON's label - `sub_476860` on
            //               `off_4E337C[word_4E3372]`, printed "%s". It also
            //               fetches the selected row's name into a buffer
            //               nothing reads (dead in the original), and while
            //               oscillator 0's flag runs it shows the flashing
            //               message `byte_6A4CA0` instead - step 4's, not
            //               modelled here.
            //   0x004AEF10  string 4 and `sub_42B1C0(4)`, the player's money,
            //               "%s %d".
            //   0x004AEF60  string 5 and `sub_42B300(tag)` - case 34, the
            //               price of the selected row's object - HALVED when
            //               the screen's parameter is 0, the bank quoting what
            //               it pays; "%s %d". A row with no object is tag -1
            //               and `sub_42B300` returns 0 for it.
            {
                // The hooks name the lists by ADDRESS (`0x004E3640`,
                // `off_4E337C`), not through the installed panel - which on the
                // Vente confirm and the Examiner page carries no stock rows.
                const omk::UiList* buttons = w.listAt(omk::kListShopButtons);
                const omk::UiList* rows = w.listAt(omk::kListShopRows);
                int price = 0;
                int rowId = -1;                 // the selected row's object
                if (rows) {
                    const int sel = walk->selectionOf(*rows);
                    const int row = sel >= 0 ? sel + std::max(0, window) : -1;
                    if (row >= 0 && row < static_cast<int>(ids.size())) {
                        rowId = ids[static_cast<std::size_t>(row)];
                        price = inv.price(rowId);
                        if (w.screenParam(openScreen) == 0) price /= 2;
                    }
                }
                // oscillator 0: `sub_42B6D0` adds the frame's ms to `+4` and at
                // `+8` = 5000 `sub_42B7B0` clears the flag line two tests
                const bool messageUp = shopNowMs - shopMessageMs < 5000;
                for (const auto& l : walk->panel()->lists) {
                    for (const auto& e : l.items) {
                        if (e.textFn == 0x004AEE30u && messageUp) {
                            sneakRows[e.addr] = shopMessage;     // `sub_478D60`
                        } else if (e.textFn == 0x004AEE30u && buttons) {
                            const int b = walk->selectionOf(*buttons);
                            if (b >= 0 && b < static_cast<int>(buttons->items.size()))
                                sneakRows[e.addr] =
                                    str(buttons->items[static_cast<std::size_t>(b)].label());
                        } else if (e.textFn == 0x004AEFD0u) {
                            // the Vente confirm's second line: `sub_42AA00` on
                            // the selected stock row - case 33, its name
                            if (rowId > 0) sneakRows[e.addr] = inv.displayName(rowId, 0);
                        } else if (e.textFn == 0x004AEF10u) {
                            sneakRows[e.addr] = str(e.label()) + " " +
                                                std::to_string(state.money());
                        } else if (e.textFn == 0x004AEF60u) {
                            sneakRows[e.addr] = str(e.label()) + " " +
                                                std::to_string(price);
                        }
                    }
                }
                // One line whenever the header changes, so a headless run can
                // be held to what the three hooks composed.
                std::string said;
                for (const auto& l : walk->panel()->lists) {
                    if (l.addr != omk::kListShopHeader) continue;
                    for (const auto& e : l.items) {
                        const auto t = sneakRows.find(e.addr);
                        if (t != sneakRows.end()) said += " | '" + t->second + "'";
                    }
                }
                static std::string headerTold;
                if (said != headerTold) {
                    headerTold = said;
                    std::printf("shop header: screen %d%s\n", openScreen, said.c_str());
                }
            }
            static int shopTold = -1;
            if (shopTold != openScreen * 100 + static_cast<int>(ids.size())) {
                shopTold = openScreen * 100 + static_cast<int>(ids.size());
                std::printf("shop: screen %d, object list %d, %zu rows:", openScreen,
                            inv.openedList(), ids.size());
                for (int id : ids) std::printf(" %d", id);
                std::printf("\n");
            }
        }
        // ---- MULTIPLAN's ROWS and HEADER (todo/multiplan.md step 2) --------
        //
        // The rows list the SOURCE (`dword_68A610`, the walk's
        // `multiplanSource`): 0 the sneak, 1 the storage every terminal
        // shares. `sub_42ADD0(rows, 0, src)` raises event 25 on it, then case
        // 29 for the count and case 33 for each name, through the window.
        // The header's hook `0x004B0B60` is the shops' `0x004AEE30` again:
        // the posted message while oscillator 0 runs, else the SELECTED
        // BUTTON's label (`IAM\Multip` 0..3).
        // ---- THE GANDHAR DOOR's cursor, where its hook put it ------------
        //
        // `sub_4AFE90` rewrites the cursor item's x/y on every step - `col * 63
        // + 135`, `row * 63 + 61` - and the tree only carries the authored
        // place, so the composer is told where the walk moved it.
        static std::map<std::uint32_t, std::pair<int, int>> itemMoved;
        itemMoved.clear();
        // ---- `Indices`, THE HINT SHOP (screen 30's second page) -----------
        //
        // Five ROW widgets bound to OBJECT LIST 2 - the memo journal - a
        // 440x120 body box showing one section of the selected memo's
        // description, a footer counting the player's anneaux, and, on the
        // confirm, `Cet indice te coutera : 3` over `Acheter` / `Annuler`.
        //
        // Everything the widget tree cannot carry comes from here: the row
        // NAMES (the channel's case 33), the body's TEXT (case 40, the
        // object record's description), the two native `textFn`s, and the
        // Y each builder writes over the two items the pages SHARE.
        static std::set<std::uint32_t> hintReport;
        static std::map<std::uint32_t, int> itemSection;
        static std::string hintTitleTold;
        hintReport.clear();
        itemSection.clear();
        const std::uint32_t hintPanel =
            walk && walk->panel() ? walk->panel()->addr : 0u;
        if (hintPanel == omk::kPanelHints || hintPanel == omk::kPanelHintBuy) {
            sneakRows.clear();
            sneakHidden.clear();
            comp.setExamineText(nullptr);
            const auto saveText = omk::iamStrings(fs, "IAM/Save");
            const auto str = [&](int id) {
                return id >= 0 && id < static_cast<int>(saveText.size())
                    ? saveText[static_cast<std::size_t>(id)] : std::string();
            };
            const auto memos = omk::objectList(state, omk::ObjectList::Memos);
            // `sub_42ADD0` raises event 25 on list 2 before it asks for the
            // count, so the channel is opened here too - case 33 refuses
            // (result 3) while no list is open.
            if (inv.openedList() != 2) inv.openList(2);
            // The five widgets, through `sub_42AAE0`'s window rule. The walk
            // already bound them in `buildPage`; this supplies the NAMES,
            // which are `sub_42AA00` -> event 33 on the widget's own tag.
            if (const omk::UiList* rl = w.listAt(omk::kListHintRows))
                for (std::size_t k = 0; k < rl->items.size(); ++k)
                    if (k < memos.size())
                        sneakRows[rl->items[k].addr] =
                            inv.displayName(memos[k], 0);
            // THE BODY BOX. `sub_477F60` raises event 40 on the item's own
            // `+0x3C` and lays out what it answers; `dword_4E2B4C` is that
            // field, and the builders write it from the row list's selection
            // - which they have just reset to 0. So it is the FIRST memo
            // whatever row is highlighted, and `word_4E2B2E` picks which
            // bracketed section of it: 0 the memo, 1 the clue `Acheter` buys.
            const int bodyRow = walk->hintBodyRow();
            const int bodyId = bodyRow >= 0 && bodyRow < static_cast<int>(memos.size())
                ? memos[static_cast<std::size_t>(bodyRow)] : -1;
            if (const omk::ObjectRecord* br = bodyId > 0 ? inv.record(bodyId) : nullptr) {
                examineText = br->description;
                comp.setExamineText(&examineText);
                comp.setTextScroll(&walk->textScroll());
            }
            itemSection[omk::kItemHintBody] = walk->hintBodySection();
            // THE FOOTER, `sub_4AE290`. Three arms, and the item's `+28` -
            // which the row callback rewrites to 8 on a refusal - picks
            // between the last two.
            if (walk->hintRows() <= 0)
                sneakRows[omk::kItemHintFoot] = str(5);
            else if (walk->hintFooterString() == 6)
                sneakRows[omk::kItemHintFoot] =
                    "{C}" + str(6) + " " + std::to_string(walk->rings());
            else
                sneakRows[omk::kItemHintFoot] = str(walk->hintFooterString());
            // THE PRICE LINE, `sub_4AE340`, and it is transcribed WITH ITS
            // ODDITY. The hook calls `Ui_ItemStringDefault` (0x00476860) on
            // an item whose bank C carries no `0x200` and whose `+30` is 0,
            // so the string goes through `sub_43FEA0(0, ...)` - the SECTION
            // extractor - and `Cet indice te coutera :` has no brackets at
            // all. The engine's own answer is therefore `{TEXT ERROR!}`
            // followed by the whole string, and the layout swallows the
            // brace as an unknown directive, so the line reads correctly on
            // screen. Reproduced rather than tidied: the `{TEXT ERROR!}` is
            // what the function returns.
            if (walk->hintRows() > 0 && saveText.size() > 2)
                sneakRows[omk::kItemHintPrice] =
                    "{C}" + omk::extractTextSection(&saveText[2], 0) + " " +
                    std::to_string(walk->hintPrice());
            // `word_4E2B12` and `word_4E2CF2`: the two shared items move
            // between the pages, 250/400 on the shop and 180/290 on the
            // confirm.
            for (const auto& l : walk->panel()->lists)
                for (const auto& e : l.items) {
                    if (e.addr == omk::kItemHintBody)
                        itemMoved[e.addr] = {e.x, walk->hintBodyY()};
                    if (e.addr == omk::kItemHintFoot)
                        itemMoved[e.addr] = {e.x, walk->hintFooterY()};
                }
            hintReport.insert(omk::kItemHintBody);
            hintReport.insert(omk::kItemHintFoot);
            hintReport.insert(omk::kItemHintPrice);
            hintReport.insert(omk::kItemHintDone);
        }
        // ---- DEN'S LOCKER: the wheels show their digits -----------------
        //
        // `sub_4AFBE0` spins a wheel by writing `digit * 46` into its UNLIT
        // SOURCE's y, which cuts that figure out of the artwork's strip at
        // x = 0. The tree carries (0, 0) - digit zero - so the mover says which
        // row of the strip each wheel is on.
        static std::map<std::uint32_t, std::pair<int, int>> itemSource;
        static std::map<std::uint32_t, std::pair<int, int>> itemLitSource;
        static std::vector<std::uint32_t> denWheelItems;
        static std::vector<std::uint32_t> xachenItems;
        itemSource.clear();
        itemLitSource.clear();
        denWheelItems.clear();
        xachenItems.clear();
        // ---- GANDHAR'S DOOR: the cursor and the markers where the hooks put
        // them - the position AND the unlit source, which the hook and the
        // press write to the same cell (`UiListState::itemPlace`). Moving only
        // the position left the cursor's unlit sprite sampling the artwork at
        // (135, 61), pasted over whichever cell it stood on.
        if (walk && openScreen == 12 && walk->panel()) {
            for (const auto& l : walk->panel()->lists) {
                if (l.hook != omk::kHookGandharGrid || l.items.empty()) continue;
                for (const auto& it : l.items)
                    if (const auto* pl = walk->itemPlace(it.addr)) {
                        itemMoved[it.addr]  = {(*pl)[0], (*pl)[1]};
                        itemSource[it.addr] = {(*pl)[2], (*pl)[3]};
                    }
                static int gandTold = -1;
                const int key = walk->gandharRow() * 10 + walk->gandharCol();
                if (key != gandTold) {
                    gandTold = key;
                    std::printf("gandhar door: the cursor is on row %d col %d; %d of the four "
                                "symbols in (%d presses)\n", walk->gandharRow(),
                                walk->gandharCol(), __builtin_popcount(walk->gandharMask()),
                                walk->gandharPresses());
                }
            }
        }
        // ---- XACHEN'S CARTRIDGES: the four symbols above the buttons ------
        //
        // `sub_4AF9D0` writes the symbol's 51x23 cell into the widget's LIT
        // source, and the widget carries bank B `0x8` so that is what it
        // draws. The symbols are the list the mover cannot reach (flags
        // `0x20000004`); the buttons are the one it can, and on the code all
        // four of THOSE take lit source (0, 23) with `0x40000008` set, which
        // is the lamp coming on.
        if (walk && openScreen == 14 && walk->panel()) {
            const omk::UiList* syms = nullptr;
            const omk::UiList* btns = nullptr;
            for (const auto& l : walk->panel()->lists) {
                if (l.hook == 0x0042A930u) btns = &l;
                else if (l.items.size() == 4) syms = &l;
            }
            if (syms)
                for (std::size_t k = 0; k < 4 && k < syms->items.size(); ++k) {
                    int xy[2];
                    omk::UiWalk::xachenSprite(walk->xachen(static_cast<int>(k)), xy);
                    itemLitSource[syms->items[k].addr] = {xy[0], xy[1]};
                    xachenItems.push_back(syms->items[k].addr);
                }
            if (btns && walk->xachenSolved())
                for (const auto& e : btns->items) itemLitSource[e.addr] = {0, 23};
        }
        if (walk && openScreen == 13 && walk->panel()) {
            for (const auto& l : walk->panel()->lists) {
                if (l.hook != omk::kHookDenDial) continue;
                for (std::size_t k = 0; k < 4 && k < l.items.size(); ++k) {
                    itemSource[l.items[k].addr] = {0, walk->denDigit(static_cast<int>(k)) * 46};
                    denWheelItems.push_back(l.items[k].addr);
                }
            }
        }
        const bool liftHandled = walk && openScreen == 4;
        // ---- THE TERMINAL FAMILY'S DISPLAY (`todo/missing-ui.md` 3) ------
        //
        // The same gap as the lift's box, over seven screens: panel 0x004E4108's
        // body is a 430x320 item at (40, 80) in font 74 whose text comes from a
        // native `textFn`, 0x004AF5D0, so the composer drew nothing and a reader
        // found Kay'l's terminal blank - the artwork's keypad and screen with no
        // dossiers on it.
        //
        // `sub_4AF5D0` read from the image: two state queries first
        // (`sub_42B5E0(6)` then `sub_42B5F0`, and the same pair on 5), and
        // failing both a SEVEN-CASE jump table on the screen's fixed parameter,
        // each arm calling `sub_4767E0(panel, out, <string>, -1)`:
        //
        //     case 0 TERMINAL   string 12     case 4 SURV ERROR  string 1
        //     case 1 FIGHT SIM  string 5      case 5 SURV NO KIT string 3
        //     case 2 MORGUE     string 6      case 6 SURV KIT    string 2
        //     case 3 ARCHIVES   string 5
        //
        // The two query branches - string 11 for the TERMINAL and 4 for the
        // FIGHT SIM when the first is true, 6 for the FIGHT SIM when the second
        // is - are NOT ported: `sub_42B5E0`/`sub_42B5F0` are an object lookup
        // this has not read, so the default arm is what draws. Labelled, and it
        // is the arm the shipped state takes on a first visit.
        if (walk && walk->panel() && !liftHandled) {
            struct Fam { int screen; const char* file; int str; };
            static constexpr Fam kFam[] = {
                {5,  "IAM/Term", 12}, {11, "IAM/Fsim", 5}, {19, "IAM/Morg", 6},
                {18, "IAM/Arch", 5},  {15, "IAM/Surv", 1}, {16, "IAM/Surv", 3},
                {17, "IAM/Surv", 2},
            };
            for (const auto& f : kFam) {
                if (openScreen != f.screen) continue;
                sneakRows.clear();
                const auto txt = omk::iamStrings(fs, f.file);
                if (f.str >= static_cast<int>(txt.size())) break;
                for (const auto& l : walk->panel()->lists)
                    for (const auto& e : l.items)
                        if (e.textFn == 0x004AF5D0u)
                            sneakRows[e.addr] = txt[static_cast<std::size_t>(f.str)];
                // ---- AND THE HEADER BAR, `textFn` 0x004AF5A0 -------------
                //
                // Eleven bytes of code, and the whole of what was left of this
                // family. `unk_4E3FE0` is the KEYPAD'S OWN LIST - `db 0Bh` at
                // +0 is its eleven items, `word_4E3FE2` at +2 its selection,
                // `sub_4AF300` at +4 its hook and `off_4E3FEC` at +0x0C its
                // item array - so
                //
                //     movsx ecx, word_4E3FE2 ; mov edx, off_4E3FEC
                //     ... sub_476860(screen, [edx+ecx*4], out)
                //
                // hands the GENERIC string callback the keypad item the cursor
                // is on, in place of the header's own. The bar at (40, 28) is
                // therefore the LABEL OF THE HIGHLIGHTED CELL, and it is the
                // same shape as the lift's description box: a widget whose
                // text belongs to another widget.
                //
                // The labels are the ones each screen's OPEN callback binds
                // (`item+28`, lifted as `bind.string`): TERMINAL 5..9 on the
                // first five cells and 10 on the big button, FIGHT SIM 0..2,
                // ARCHIVES 0..3 with tags 6..9, MORGUE 0..4, and the three
                // SURV screens bind none at all - so on those the bar is
                // rightly empty, which is what the record says and not a gap.
                {
                    const omk::UiList* pad = nullptr;
                    for (const auto& l : walk->panel()->lists)
                        if (l.hook == omk::kHookTerminalPad) pad = &l;
                    const int cell = pad ? walk->selectionOf(*pad) : -1;
                    const int id = pad && cell >= 0 &&
                                   cell < static_cast<int>(pad->items.size())
                                 ? pad->items[static_cast<std::size_t>(cell)].label() : -1;
                    if (id >= 0 && id < static_cast<int>(txt.size()))
                        for (const auto& l : walk->panel()->lists)
                            for (const auto& e : l.items)
                                if (e.textFn == 0x004AF5A0u)
                                    sneakRows[e.addr] = txt[static_cast<std::size_t>(id)];
                    // printed from what the BAR will draw, and from the cell
                    // the WALK is on - not from the id, which is the value
                    // handed over rather than the one used
                    static std::string padTold;
                    std::string bar;
                    for (const auto& l : walk->panel()->lists)
                        for (const auto& e : l.items)
                            if (e.textFn == 0x004AF5A0u) {
                                const auto r = sneakRows.find(e.addr);
                                if (r != sneakRows.end()) bar = r->second;
                            }
                    std::string one;
                    for (char c : bar.substr(0, 60)) one += (c == '\r' || c == '\n') ? ' ' : c;
                    const std::string said = std::to_string(openScreen) + "/" +
                                             std::to_string(cell) + ": " + one;
                    if (said != padTold) {
                        padTold = said;
                        std::printf("terminal family: screen %d cell %d - the bar says "
                                    "'%s'\n", openScreen, cell, one.c_str());
                    }
                }
                static int famTold = -1;
                if (famTold != openScreen) {
                    famTold = openScreen;
                    // the BODY item's own row, named rather than "the last
                    // thing in the map" - the map gained the header bar above
                    std::string shown;
                    for (const auto& l : walk->panel()->lists)
                        for (const auto& e : l.items)
                            if (e.textFn == 0x004AF5D0u) {
                                const auto r = sneakRows.find(e.addr);
                                if (r != sneakRows.end()) shown = r->second;
                            }
                    std::string one;
                    for (char c : shown.substr(0, 80)) one += (c == '\r' || c == '\n') ? ' ' : c;
                    std::printf("terminal family: screen %d - the display says '%s'\n",
                                openScreen, one.c_str());
                }
                break;
            }
        }
        // ---- THE LIFT'S DESCRIPTION BOX (`todo/missing-ui.md` 2b) --------
        //
        // Screen 4's list 1 is one 475x105 text item at (15, 360) in font 67
        // whose text comes from a NATIVE callback, 0x004B01C0 - so the composer,
        // which draws an item's own string or nothing, drew nothing and the box
        // sat empty. A reader: *"when a level is hovered, the names of the
        // people's office and other sections of the level should be displayed"*.
        //
        // What it says is `IAM\Lift`, whose seven strings are the seven slots
        // IN ORDER - the grid's items carry string ids 0..6 and the file reads
        // "Niveau 1 : Bureau du commandant Gandhar", "Niveau 0 : Entree
        // principale", then -1 to -5 with the agents' names in their own
        // colours (`{I045175045}Tarek 511` and the rest; Kay'l 669 is on -2).
        // So the box is the SELECTED slot's string, and the port already has
        // both halves: the walk knows the selection and `iamStrings` reads the
        // file the composer itself would have used.
        //
        // LABELLED: 0x004B01C0's own body is not transcribed. It builds its
        // string through the inventory channel (`sub_4083F0` with event 0x24
        // and a {slot, 7} block, then `sub_4767E0` into a buffer) and this
        // takes the file's string directly, which is what that produces for
        // every one of the seven shipped slots.
        if (walk && openScreen == 4 && walk->panel()) {
            sneakRows.clear();
            const auto liftText = omk::iamStrings(fs, "IAM/Lift");
            const omk::UiPanel* pn = walk->panel();
            int slot = -1;
            for (const auto& l : pn->lists)
                if (l.items.size() == 7) { slot = walk->selectionOf(l); break; }
            for (const auto& l : pn->lists)
                for (const auto& e : l.items)
                    if (e.textFn == 0x004B01C0u && slot >= 0 &&
                        slot < static_cast<int>(liftText.size()))
                        sneakRows[e.addr] = liftText[static_cast<std::size_t>(slot)];
            static int liftTold = -2;
            if (slot != liftTold) {
                liftTold = slot;
                // printed from what the BOX will draw, not from the selection
                std::string shown;
                for (const auto& r : sneakRows) shown = r.second;
                const auto nl = shown.find('\r');
                std::printf("lift: slot %d - the panel says '%s'\n", slot,
                            nl == std::string::npos ? shown.c_str()
                                                    : shown.substr(0, nl).c_str());
            }
        }
        if (walk && walk->panel() &&
            (walk->panel()->addr == omk::kPanelMultiplan ||
             walk->panel()->addr == omk::kPanelMultiplanExamine ||
             walk->panel()->addr == omk::kPanelMultiplanDestroy)) {
            sneakRows.clear();
            sneakHidden.clear();
            const int src = walk->multiplanSource();
            if (inv.openedList() != src) inv.openList(src);   // Game_RaiseEvent(25, src)
            const auto mpText = omk::iamStrings(fs, "IAM/Multip");
            // ---- THE TRANSFERS (step 3): `Game_HandleEvent` case 36 ---------
            //
            // The walk recorded the row callback's request (`takeMultiplan`).
            // Case 36 reads the OPEN list's slot at the tag, then:
            //   7: kind 1 (a hand weapon) -> 2; `ObjectList_IsFull(1)` -> 2;
            //      else `ObjectList_InsertFront(1, ...)`, `RemoveAt(0, tag)`, 1
            //   8: `ObjectList_IsFull(0)` -> 2; else `Inventory_Insert(rec, 0,
            //      player)` and, when it answers 1, `InsertFront(0, ...)`;
            //      then `RemoveAt(1, tag)` whatever it answered, 1
            // So a kind 12/13 object (seteks, rings) leaves the storage as a
            // COUNT on the player and takes no sneak slot - the session's
            // `insertArm` / `applyObjectEffect` are that ladder. The callback
            // then posts message 8 on 1 and 7 / 6 otherwise through
            // `sub_42B820(0, -1, text)`, oscillator 0, 5000 ms, and rebinds
            // the rows (`sub_42ADD0(rows, -1, -1)`), handing the focus back
            // to the buttons when they are left empty.
            static std::string mpMessage;
            static long mpMessageMs = -1000000;
            const long mpNowMs = static_cast<long>(SDL_GetTicks());
            if (int request = -1, row = -1; walk->takeMultiplan(request, row)) {
                const auto from = omk::objectList(state, src == 0 ? omk::ObjectList::Carried
                                                                   : omk::ObjectList::Second);
                const int id = row >= 0 && row < static_cast<int>(from.size())
                    ? from[static_cast<std::size_t>(row)] : -1;
                int result = request;          // case 36 leaves +4 alone past the count
                std::string arm;
                const auto postText = [&](int m) {
                    mpMessage = m < static_cast<int>(mpText.size())
                        ? mpText[static_cast<std::size_t>(m)] : std::string();
                    mpMessageMs = mpNowMs;
                };
                if (request == 4) {
                    // `sub_42B420(tag, 4)`: event 30 loads the preview and
                    // answers the object's SLOT, event 43 runs
                    // `Message_RunHandlers(4, player, slot)`, whose block
                    // carries `ObjectSlot_Id(slot)` - the object id.
                    const bool ran = id >= 0 && session.postMessage(4, id);
                    const auto& runs = session.messagesRun();
                    std::printf("multiplan: examine row %d id %d -> message 4 %s\n", row, id,
                                ran && !runs.empty()
                                    ? ("handled by the " + runs.back().table + " table").c_str()
                                    : "(no resident chunk subscribes)");
                } else if (request == 6) {
                    // Oui: the SOURCE test comes before the tag's; case 36
                    // request 6 is `(flags & 2) ? RemoveAt(1, tag), 1 : 2`,
                    // and success posts nothing.
                    int msg = -1;
                    if (src != 1) { msg = 4; arm = "not the kiosk's list"; }
                    else if (id >= 0) {
                        const auto* rec = inv.record(id);
                        if (rec && (rec->flags & 0x2)) {
                            state.listRemove(1, id); result = 1; arm = "destroyed";
                        } else { result = 2; msg = 4; arm = "flag 0x2 clear"; }
                    } else { arm = "no row"; }
                    if (msg >= 0) postText(msg);
                    std::printf("multiplan: destroy row %d id %d -> %d (%s)%s\n", row, id,
                                result, arm.c_str(), msg >= 0 ? ", message 4" : "");
                    const auto after = omk::objectList(state, omk::ObjectList::Second);
                    if (result == 1 && after.empty()) walk->focusList(omk::kListMultiplanButtons);
                } else if (id >= 0 && request == 7) {
                    const auto* rec = inv.record(id);
                    if (rec && rec->kind == 1) { result = 2; arm = "kind 1"; }
                    else if (!state.listAdd(1, id)) { result = 2; arm = "storage full"; }
                    else { state.listRemove(0, id); result = 1; arm = "row"; }
                } else if (id >= 0 && request == 8) {
                    const auto carried = omk::objectList(state, omk::ObjectList::Carried);
                    if (static_cast<int>(carried.size()) >= omk::GameState::kListCapacity[0]) {
                        result = 2; arm = "sneak full";
                    } else {
                        const auto a = session.insertArm(id);
                        if (a == omk::Session::Banked::Row) { state.listAdd(0, id); arm = "row"; }
                        else { session.applyObjectEffect(id);
                               arm = a == omk::Session::Banked::Consumed ? "consumed" : "merged"; }
                        state.listRemove(1, id);
                        result = 1;
                    }
                }
                if (request == 7 || request == 8) {
                    const int msg = result == 1 ? 8 : request == 7 ? 7 : 6;
                    postText(msg);
                    std::printf("multiplan: request %d row %d id %d -> %d (%s), message %d, "
                                "rings %d\n", request, row, id, result, arm.c_str(), msg,
                                state.rings());
                    const auto after = omk::objectList(state, src == 0 ? omk::ObjectList::Carried
                                                                        : omk::ObjectList::Second);
                    if (result == 1 && after.empty()) walk->focusList(omk::kListMultiplanButtons);
                }
            }
            const auto ids = omk::objectList(state, src == 0 ? omk::ObjectList::Carried
                                                             : omk::ObjectList::Second);
            const int window = walk->rowWindow(omk::kListMultiplanRows);
            if (const omk::UiList* rows = w.listAt(omk::kListMultiplanRows)) {
                for (std::size_t k = 0; k < rows->items.size(); ++k) {
                    const std::size_t row = k + static_cast<std::size_t>(std::max(0, window));
                    if (row >= ids.size()) {
                        sneakHidden.insert(rows->items[k].addr);   // sub_42AAE0
                        continue;
                    }
                    sneakRows[rows->items[k].addr] = inv.displayName(ids[row], 0);
                }
            }
            walk->bindRows(omk::kListMultiplanRows, static_cast<int>(ids.size()), window);
            // ---- THE EXAMINER PAGE and THE DESTROY CONFIRM (step 4) ---------
            //
            // Both name the object through the rows' SELECTION, a static
            // record, so the row chosen on the kiosk is still there. The page's
            // box is draw hook `0x004780A0`, the sneak's examine box again:
            // case 40 answers 4 for kind 15 (text only), 5 for kind 16 (the
            // `IMAGES` bitmap the builder `0x004B0510` loads, then the text),
            // 2 otherwise (case 30's model on oscillator 4, then the text) -
            // `uiModels.examine` as the sneak page calls it. The confirm's
            // second line `0x004B0C30` is `sub_42AA00` on the selected row.
            {
                const int sel = walk->selectedRow(omk::kListMultiplanRows);
                const int selId = sel >= 0 && sel < static_cast<int>(ids.size())
                    ? ids[static_cast<std::size_t>(sel)] : -1;
                const std::uint32_t pa = walk->panel()->addr;
                if (pa == omk::kPanelMultiplanExamine && selId >= 0 &&
                    static_cast<std::size_t>(selId) < objectRecords.size()) {
                    const auto& rec = objectRecords[static_cast<std::size_t>(selId)];
                    static int mpExamined = -1;
                    const auto k = uiModels.examine(
                        fs, (rec.kind == 15 || rec.kind == 16) ? rec.kind : 15,
                        (rec.kind == 15) ? std::string() : rec.stem);
                    examineText = rec.description;
                    comp.setExamineText(&examineText);
                    comp.setTextScroll(&walk->textScroll());
                    if (selId != mpExamined) {
                        mpExamined = selId;
                        std::printf("multiplan: Examiner '%s' kind %d -> %s\n", rec.name.c_str(),
                                    rec.kind,
                                    k == omk::UiModels::Examine::Model ? "3D model"
                                    : k == omk::UiModels::Examine::Document ? "document bitmap"
                                    : "text only");
                    }
                }
                if (pa == omk::kPanelMultiplanDestroy && selId >= 0)
                    if (const omk::UiList* c = w.listAt(omk::kListMultiplanDestroy))
                        for (const auto& e : c->items)
                            if (e.textFn == omk::kTextMultiplanRowName)
                                sneakRows[e.addr] = inv.displayName(selId, 0);
            }
            // `0x004B0B60`: the posted message while oscillator 0 runs
            // (`sub_478D60`); otherwise `sprintf(out, "%s", label)` of the
            // selected button (`sub_476860`). On the Examiner page, or with
            // the rows focused, it ALSO fetches the selected row's name into a
            // second buffer (`sub_42AA00`) - and never prints it: the format
            // at 0x004E5AF8 is "%s" with the label as its only argument.
            const bool mpMessageUp = mpNowMs - mpMessageMs < 5000;
            if (const omk::UiList* buttons = w.listAt(omk::kListMultiplanButtons)) {
                const int b = walk->selectionOf(*buttons);
                if (b >= 0 && b < static_cast<int>(buttons->items.size())) {
                    const int id = buttons->items[static_cast<std::size_t>(b)].label();
                    if (const omk::UiList* head = w.listAt(omk::kListMultiplanHeader))
                        for (const auto& e : head->items) {
                            if (e.textFn != 0x004B0B60u) continue;
                            if (mpMessageUp)
                                sneakRows[e.addr] = mpMessage;
                            else if (id >= 0 && id < static_cast<int>(mpText.size()))
                                sneakRows[e.addr] = mpText[static_cast<std::size_t>(id)];
                        }
                }
            }
            static std::string multiplanTold;
            std::string said = "source " + std::to_string(src) + ", " +
                               std::to_string(ids.size()) + " rows:";
            for (int id : ids) said += " " + std::to_string(id);
            if (said != multiplanTold) {
                multiplanTold = said;
                std::printf("multiplan: %s\n", said.c_str());
            }
        }
        // ---- `sub_423A40`, the tail of `Actor_SetProperty` (`script/hooks.h`)
        //
        // A script's property write in a shoot phase reaches the shoot
        // records here: property 1 is the record's `+92` (for the player, the
        // gauge's value too), property 35 the HUD's ammo when the held
        // weapon's row matches - its TYPE at least 6 (the `>= 2 && >= 6`
        // pair, transcribed as read) and equal to the magazine slot + 2. A
        // SHOOT medikit is a zone script that does exactly this:
        // `var.set.actor_stat(player, 1)`, `var.add`, `actor.stat.set` - so
        // the kit is used the moment it is taken, not stored.
        for (const auto& w : session.takeShootStatWrites()) {
            if (!shootMode) continue;
            if (w.property == 1) {
                if (w.actor == -1) {
                    playerShootRec.health = static_cast<int>(w.value);
                    hudHealth = playerShootRec.health;   // `dword_90E100 = value`
                    std::printf("frame %ld: SHOOT STAT (sub_423A40) - the player's health "
                                "-> %d, and the gauge's\n", n, int(w.value));
                } else if (const auto it = shootBrains.find(w.actor); it != shootBrains.end()) {
                    it->second.health = static_cast<int>(w.value);
                    std::printf("frame %ld: SHOOT STAT (sub_423A40) - actor %d's health -> %d\n",
                                n, w.actor, int(w.value));
                }
            } else if (w.property == 35 && w.actor == -1) {
                if (const omk::ShootWeaponRow* row = playerShootRec.weapon)
                    if (row->key >= 2 && row->key >= 6 && row->key == (w.value >> 16) + 2)
                        hudAmmo = static_cast<int>(w.value & 0xFFFF);
            }
        }
        // ---- THE FIGHT HUD (`todo/fight-mode.md` step 5) -----------------
        //
        // `Actors_TickAll`'s melee row ends with `Fight_UpdateHealthBars`
        // (0x00445160) while the KO counter is 0: property 1 of each fighter
        // through `Hud_DrawBar(player, 200, 0, 2)` and `(opponent, 200, 1, 0)`
        // - the two gauges down the screen's two edges, and mode 2's STAT CARD
        // for the four seconds after `Fight_Begin`'s `Hud_Refresh`
        // (`ui/hudbar.h`). Hidden through the KO replay, as the engine's gate.
        if (fightRun.active && fightRun.fight && !omk::envSet("OMK_NOUI")) {
            if (!hudBar.loaded()) hudBar.load(fs);
            if (fightRun.hudRefresh) {
                fightRun.hudRefresh = false;
                hudBar.refresh(0, 22, fb.w);           // `sub_446C40(0, 22)`
                hudBar.refresh(1, 22, fb.w);
                hudBar.refreshCard(fightRun.cardProps, fightRun.ms);
                std::printf("frame %ld: FIGHT HUD (Hud_Refresh) - stat card", n);
                for (int k = 0; k < 6; ++k)
                    std::printf(" %s=%d", hudBar.cardLabel(k).c_str(), hudBar.cardValue(k));
                // non-ASCII bytes ESCAPED: the rank is raw Latin-1 ("Initi\xe9"),
                // and one invalid UTF-8 byte in this log makes `grep` go quiet
                // and a strict decode throw (`engine: fight letterbox`)
                std::string rank = "(none)";
                if (hudBar.cardValue(1) >= 0 && hudBar.cardValue(1) < 5) {
                    rank.clear();
                    for (const char ch : hudBar.rankName(hudBar.cardValue(1))) {
                        const auto u = static_cast<unsigned char>(ch);
                        if (u < 0x80) rank.push_back(ch);
                        else { char e[8]; std::snprintf(e, sizeof e, "\\x%02x", u); rank += e; }
                    }
                }
                std::printf(", rank '%s'\n", rank.c_str());
            }
            if (fightRun.fight->koCounter() == 0) {
                const omk::HudBarFrame a = hudBar.draw(fb, fightRun.fight->player().hp, 200, 0, 2,
                                                       &lay, fightRun.ms);
                const omk::HudBarFrame b = hudBar.draw(fb, fightRun.fight->opponent().hp, 200, 1, 0);
                if ((n - fightRun.startedAt) % 30 == 0)
                    std::printf("    fight HUD: player gauge %d%% (top %d), opponent %d%% (top %d), "
                                "card rows %d text %d\n", a.percent, a.top, b.percent, b.top,
                                a.cardRows, a.cardText);
            }
        }
        // ---- THE BREATH GAUGE (`todo/swimming.md` step 4) ----------------
        //
        // `sub_4A8F30`'s underwater arm, every tick until the 40 seconds are
        // spent: `Hud_DrawBar(1000 * (start + 40000 - now) / 40000, 1000, 0, 1)`
        // - mode 1, the horizontal bar (`ui/hudbar.h`). The engine's
        // `Hud_Refresh` on the first tick seeds the sparks and the stat card's
        // clock, neither of which mode 1 draws, so nothing is done for it here.
        if (player && player->breathLeftMs() >= 0.0 && !omk::envSet("OMK_NOUI")) {
            if (!hudBar.loaded()) hudBar.load(fs);
            const int v = static_cast<int>(1000.0 * player->breathLeftMs() / 40000.0);
            const omk::HudBarFrame br = hudBar.draw(fb, v, 1000, 0, 1);
            if (n % 30 == 0)
                std::printf("    breath gauge (Hud_DrawBar mode 1): %d%%, %d ms left, "
                            "right edge %d, %d quads %d blits\n", br.percent,
                            int(player->breathLeftMs()), br.top, br.quads, br.blits);
        }
        // ---- THE SHOOT HUD, screen 34 (`todo/shoot-mode.md` 8.3) --------
        //
        // `Shoot_Enter` opens it and it runs under the mode as any screen
        // does, but it takes no input - so it is composed here from a walk of
        // its own rather than opened as the interactive screen. Its items'
        // callbacks are native (0x42E870.. - no `proc` label; read from the
        // image with objdump) and three of them produce TEXT, supplied as row
        // text by item address:
        //
        //   0x4C4418  `sub_42B1C0(5)` - player property 5, ANNEAUX - into
        //             "{C}" + "%d", centred under the turning ring
        //   0x4C44A8  `dword_90E11C`, "%d"; the -1 arm prints a .bss string
        //             nothing writes by address, so it is taken as EMPTY
        //   0x4C4460  the held object's NAME, `Game_RaiseEvent(46)`
        //
        // and the full-screen item 0x4C44F0 draws the CROSSHAIR: the four
        // quads at 0x4C4680, offset by half the display (native pixels,
        // not scaled), then `Hud_DrawBar(health, 200, 0, 0)` - the health
        // GAUGE (`ui/hudbar.h`). The two turning models go into their boxes
        // below. NOT drawn yet, labelled: the top-right minimap
        // `RADAR\<level>.WRE`.
        if (shootMode && hudWalk && !walk && !omk::envSet("OMK_NOUI")) {
            hudRows.clear();
            std::int32_t rings = 0;
            omk::readActorProperty(state.raw().subspan(
                                       static_cast<std::size_t>(omk::GameState::kPlayerRecord),
                                       static_cast<std::size_t>(omk::GameState::kPlayerRecordSize)),
                                   5, rings);
            hudRows[0x4C4418u] = "{C}" + std::to_string(rings);
            if (hudAmmo != -1) hudRows[0x4C44A8u] = std::to_string(hudAmmo);
            std::string weaponName;
            {
                const int obj = session.shootMode().weaponObject();
                const auto& objs = voiceLib.objects();
                if (obj >= 0 && static_cast<std::size_t>(obj) < objs.size())
                    weaponName = objs[static_cast<std::size_t>(obj)].name;
                if (!weaponName.empty()) hudRows[0x4C4460u] = weaponName;
            }
            comp.attachCursor(nullptr);
            comp.attachModels(nullptr);
            comp.attachCloud(nullptr);
            comp.setRowText(&hudRows);
            comp.setHidden(nullptr);
            const omk::ScreenFrame hf = comp.draw(fb, session.shootMode().hudScreen(), *hudWalk);
            // THE TWO TURNING MODELS, into their items' boxes: the ring
            // (item 0x4C43D0 at 48,0, `sub_478DE0(node, 10.0)`) and the held
            // weapon (0x4C4460 at 48,380, 25.0), both turned by oscillator 4
            // (`sub_478EC0`), the sneak previews' turntable.
            bool ringDrawn = false, weaponDrawn = false;
            {
                const float spin = omk::UiModels::spinDegrees(uiClockMs());
                const auto box = [&](int x, int y, int bw, int bh, int out[4]) {
                    out[0] = comp.scaleX(x); out[1] = comp.scaleY(y);
                    out[2] = comp.scaleX(x + bw) - out[0]; out[3] = comp.scaleY(y + bh) - out[1];
                };
                int r[4];
                for (int k = 0; k < uiModels.count(); ++k)
                    if (uiModels.name(k).find("anneau") != std::string::npos) {
                        box(48, 0, 100, 100, r);
                        ringDrawn = uiModels.draw(fb, k, r[0], r[1], r[2], r[3], spin, 10.0f);
                    }
                const int obj = session.shootMode().weaponObject();
                const auto& objs = voiceLib.objects();
                if (obj >= 0 && static_cast<std::size_t>(obj) < objs.size() &&
                    uiModels.loadWeapon(fs, objs[static_cast<std::size_t>(obj)].stem)) {
                    box(48, 380, 100, 100, r);
                    weaponDrawn = uiModels.drawWeapon(fb, r[0], r[1], r[2], r[3], spin, 25.0f);
                }
            }
            // the crosshair: {x0, y0, x1, y1} from the four records, relative
            // to the centre, filled white (0xFFFFFF -> RGB565 0xFFFF)
            const int cx = fb.w / 2, cy = fb.h / 2;
            static const int kCross[4][4] = {{-1, 4, 1, 12}, {4, -1, 12, 1},
                                             {-12, -1, -4, 1}, {-1, -12, 1, -4}};
            for (const auto& q : kCross)
                for (int y = cy + q[1]; y < cy + q[3]; ++y)
                    for (int x = cx + q[0]; x < cx + q[2]; ++x)
                        if (x >= 0 && y >= 0 && x < fb.w && y < fb.h)
                            fb.px[static_cast<std::size_t>(y) * static_cast<std::size_t>(fb.w) +
                                  static_cast<std::size_t>(x)] = 0xFFFF;
            // THE HEALTH GAUGE: the same callback's last call,
            // `Hud_DrawBar(dword_90E100, 200, 0, 0)` (`ui/hudbar.h`), the
            // value the player's record +92
            if (!hudBar.loaded()) hudBar.load(fs);
            // (`dword_90E100`, `hudHealth` - not +92 itself, which a killing
            // hit takes below 0 while the gauge keeps its last value)
            const omk::HudBarFrame bar = hudBar.draw(fb, hudHealth, 200, 0, 0);
            // THE RADAR: item 0x4C4388's own callback 0x42F000 (`ui/radar.h`),
            // in the box its HUD's open callback set, through the shoot camera's fov
            // (preset row 4's 75). The gunmen go in with the position their
            // brain takes as `self`; ACTOR_STATE 3 while the brain has health
            // (`Shoot_ActorEnter` wrote it), 0 once it has none (the brain's
            // `health <= 0` arm), and anything without a brain is neither.
            // Drawn only while the SWITCH is on - ops 146/147, or the robot
            // HUD's open - or with the `radar = always` enhancement.
            const bool radarShown = radar.loaded() && player &&
                                    (session.radarOn() || radarAlways);
            {
                static int radarShownTold = -1;
                if (radar.loaded() && int(radarShown) != radarShownTold) {
                    radarShownTold = int(radarShown);
                    std::printf("frame %ld: RADAR %s - the game's switch (ops 146/147) is %s%s\n",
                                n, radarShown ? "SHOWN" : "hidden",
                                session.radarOn() ? "ON" : "off",
                                radarAlways ? ", radar = always" : "");
                }
            }
            if (radarShown) {
                omk::RadarView rv;
                const int* bx = radar.box();
                rv.left = fb.w * bx[0] / 640;
                rv.top = fb.h * bx[1] / 480;
                rv.right = rv.left + fb.w * bx[2] / 640;
                rv.bottom = rv.top + fb.h * bx[3] / 480;
                rv.screenW = fb.w;
                rv.screenH = fb.h;
                rv.fovDeg = 75.0f;
                for (int k = 0; k < 3; ++k) rv.player[k] = player->pos()[k];
                rv.facingDeg = player->facing();
                std::vector<omk::RadarActor> ra;
                for (const auto& sp : staged) {
                    if (!sp) continue;
                    omk::RadarActor a;
                    a.id = sp->actor;
                    for (int k = 0; k < 3; ++k) a.pos[k] = sp->drawAt[k];
                    const auto br = shootBrains.find(sp->actor);
                    a.state = br == shootBrains.end() ? 1 : (br->second.health > 0 ? 3 : 0);
                    ra.push_back(a);
                }
                const omk::RadarFrame rf = radar.draw(fb, rv, ra);
                static int radarBlipsTold = -1;
                if (rf.blips != radarBlipsTold) {
                    radarBlipsTold = rf.blips;
                    const bool inFb = rf.playerX >= 0 && rf.playerY >= 0 &&
                                      rf.playerX < fb.w && rf.playerY < fb.h;
                    std::printf("frame %ld: RADAR drawn - %d of %d edges in front, %d lines "
                                "in the box %d,%d-%d,%d, %d gunmen, player square %d at "
                                "%d,%d pixel 0x%04x\n", n,
                                rf.edgesInFront, radar.wire().edges(), rf.lines, rv.left,
                                rv.top, rv.right, rv.bottom, rf.blips, int(rf.player),
                                rf.playerX, rf.playerY,
                                inFb ? unsigned(fb.px[static_cast<std::size_t>(rf.playerY) *
                                                          static_cast<std::size_t>(fb.w) +
                                                      static_cast<std::size_t>(rf.playerX)])
                                     : 0u);
                }
            }
            const auto pixel = [&](int x, int y) {
                return unsigned(fb.px[static_cast<std::size_t>(y) * static_cast<std::size_t>(fb.w) +
                                      static_cast<std::size_t>(x)]);
            };
            // the name is the game's cp1252 and the log is UTF-8: "Bâton de
            // pouvoir" printed raw put a lone 0xE2 in the output, and a check
            // reading it strictly stopped with a decode error. For letters
            // cp1252 is Latin-1, so each high byte is two UTF-8 bytes.
            std::string weaponLog;
            for (const unsigned char ch : weaponName) {
                if (ch < 0x80) { weaponLog.push_back(static_cast<char>(ch)); continue; }
                weaponLog.push_back(static_cast<char>(0xC0 | (ch >> 6)));
                weaponLog.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
            }
            char line[384];
            std::snprintf(line, sizeof line,
                          "rings %d, ammo %d, weapon '%s' - items drawn %d, fills %d; "
                          "models ring %d weapon %d; "
                          "crosshair at %d %d, pixel below centre 0x%04x; "
                          "gauge %d/200 = %d%%, top %d, quads %d, blits %d, "
                          "frame pixel 0x%04x, empty-part pixel 0x%04x",
                          int(rings), hudAmmo, weaponLog.c_str(), hf.itemsDrawn,
                          hf.fillsDrawn, int(ringDrawn), int(weaponDrawn), cx, cy,
                          pixel(cx, cy + 8), hudHealth, bar.percent, bar.top,
                          bar.quads, bar.blits,
                          pixel(fb.w * 24 / 640 - fb.w * 5 / 640, fb.h * 300 / 480),
                          pixel(fb.w * 24 / 640, fb.h * 100 / 480));
            // told when the line CHANGES - but not for the two pixel probes:
            // once the gauge is full enough its fill covers the empty-part
            // probe and the fill's column SCROLLS a frame, so that pixel
            // changes every frame and the line was printed every frame (a
            // reader's session at 176 of 200). They are printed, not compared.
            const std::string told(line);
            const std::string key = told.substr(0, told.find(", frame pixel"));
            if (hudTold != key) {
                hudTold = key;
                std::printf("frame %ld: shoot HUD (screen %d) - %s\n", n,
                            session.shootMode().hudScreen(), line);
            }
        }
        if (walk) {
            comp.setFrame(n);
            // The oscillators run on a MILLISECOND clock, not on the frame
            // index - their periods are 500, 1000 and 5000 and
            // `Ui_TickScreens` advances them by the frame delta.
            //
            // ...and in a HEADLESS run (`--frames`, no pacing) on the frame
            // clock, 1000/30 ms a frame - the paced run's own - so a run is the
            // same run however busy the machine is. On the wall clock, `engine:
            // den locker` went red under a sweep's load: its frames came slower,
            // the hand's wheel spent the few frames that showed its 2 in the
            // blink's blank half, and the display's union read 7210 for 7212.
            comp.setClockMs(uiClockMs());
            // THE HIGHLIGHT. `Ui_DrawItemCursor` eases sixteen elements
            // between frames, so it needs a delta and somewhere to live; it
            // is attached rather than owned by the composer so that
            // `run_screen`'s hashes stay a pure function of the screen.
            {
                static long uiLastMs = 0;
                const long nowMs = uiClockMs();
                comp.setDeltaMs(uiLastMs ? nowMs - uiLastMs : 33);
                uiLastMs = nowMs;
            }
            comp.attachCursor(&uiCursor);
            comp.attachModels(&uiModels);
            comp.setRowText(sneakRows.empty() ? nullptr : &sneakRows);
            comp.setHidden(sneakHidden.empty() ? nullptr : &sneakHidden);
            comp.setItemMove(itemMoved.empty() ? nullptr : &itemMoved);
            comp.setItemSource(itemSource.empty() ? nullptr : &itemSource);
            comp.setItemLitSource(itemLitSource.empty() ? nullptr : &itemLitSource);
            comp.setItemSection(itemSection.empty() ? nullptr : &itemSection);
            comp.setReportText(hintReport.empty() ? nullptr : &hintReport);
            comp.setHighScores(openScreen == 36 ? &highScores : nullptr,
                               walk ? walk->highScorePage() : 0);
            // THE CLOUD IS THE MENU'S BACKGROUND, NOT EVERY SCREEN'S.
            //
            // A reader's screenshots of the original settle it from both
            // sides: the load panel (screen 29, at the menu) draws over the
            // animated cloud, and the SAVE screen (30, opened from a save
            // point) draws over the LIVE 3D SCENE - Kay'l is visible standing
            // on the rings behind the menu text.
            //
            // What gates it in the engine is not read: `sub_4B19C0` only
            // creates and frees the cloud's surface around a resolution
            // change, and the per-frame drawer has no `proc` label, so it is
            // in the decompilation's blind spot (CLAUDE.md 1). The rule here
            // is taken from those screenshots - no cloud once the world is
            // live - and is labelled a RECONSTRUCTION for that reason.
            //
            // "THE WORLD IS LIVE" IS `player`, NOT `adventure`, and the two
            // differ exactly where this matters. `adventure` is a per-frame
            // MODE and a screen over the world takes it false in the same
            // breath - so the rule as written drew the cloud over every
            // world-side screen, which is the one case the screenshots
            // decide against. It went unseen because until the pause screen
            // no screen was ever opened from inside the world; the SAVE
            // screen the reader photographed would have had it too.
            comp.attachCloud(player ? nullptr : &cloud);
            // `OMK_NOUI=1` draws the frame WITHOUT the interface layer. An
            // instrument, and the one that found the keyed-tile fault: with
            // the device off, the caller was there all along, so the world
            // was never the problem.
            if (!omk::envSet("OMK_NOUI")) {
                omk::ScreenFrame sf;
                spanned("screen draw", [&] { sf = comp.draw(fb, openScreen, *walk); });
                // screen 35 over the menu while it has the focus - its panel
                // paints no background, so the menu's sheet shows through
                const bool optShown = (openScreen == 29 && optMenu.focused()) ||
                    (openScreen == omk::kScreenSneak && optMenu.isOpen() && walk->panel() &&
                     walk->panel()->addr == omk::kPanelSneakOptions);
                if (optShown) {
                    const omk::OptionsDrawn od = comp.drawOptions(fb, optMenu);
                    static std::string optTold;
                    std::string line = "options: page " + std::to_string(optMenu.page()) + ":";
                    for (const auto& l : od.lines) line += " [" + l + "]";
                    if (line != optTold) {
                        optTold = line;
                        std::printf("frame %ld: %s - %d drawn, %d lit, %d slider%s\n", n,
                                    line.c_str(), od.widgets, od.lit, od.sliders,
                                    od.sliders == 1 ? "" : "s");
                    }
                }
                // ---- THE HINT SHOP, REPORTED FROM THE DRAW -----------
                //
                // Every field on this line comes out of `ScreenFrame`, and
                // the four texts are the strings the COMPOSER laid out, not
                // the map it was handed: an item the builders hid has no
                // entry at all, which is the half a handed-over map cannot
                // say (CLAUDE.md 1, the log-line rule). `-` marks one that
                // drew nothing this frame - and on a correct page three of
                // the four always do, because the shop hides `Indice achete
                // !` and the confirm hides the body until you have paid.
                if (!hintReport.empty()) {
                    const auto txt = [&](std::uint32_t a) {
                        const auto t = sf.itemText.find(a);
                        return t == sf.itemText.end() ? std::string("-")
                                                      : t->second;
                    };
                    char page[512];
                    std::snprintf(page, sizeof page,
                                  "indices: %s, %d row%s, list %d, %d items drawn; "
                                  "body '%s' | price '%s' | foot '%s' | done '%s'",
                                  hintPanel == omk::kPanelHintBuy ? "the purchase confirm"
                                                                  : "the shop",
                                  walk->hintRows(), walk->hintRows() == 1 ? "" : "s",
                                  walk->currentList(), sf.itemsDrawn,
                                  txt(omk::kItemHintBody).c_str(),
                                  txt(omk::kItemHintPrice).c_str(),
                                  txt(omk::kItemHintFoot).c_str(),
                                  txt(omk::kItemHintDone).c_str());
                    if (hintTitleTold != page) {
                        hintTitleTold = page;
                        std::printf("%s\n", page);
                    }
                }
                // ---- DEN'S LOCKER, REPORTED FROM THE DRAW ------------
                //
                // Not from `denDigit()`: the hook hands the composer a source
                // row per wheel and the composer decides whether to use it,
                // because the wheel the hand is on draws its LIT source -
                // its own empty place in the artwork - while oscillator 1
                // blinks it. A line printed from what was HANDED OVER said
                // "7 2 1 2" through a display that showed three figures and a
                // gap, which is the fault this is written against
                // (CLAUDE.md 1). `-` is a wheel drawn blank this frame.
                if (openScreen == 13 && !denWheelItems.empty()) {
                    std::string read;
                    for (const std::uint32_t a : denWheelItems) {
                        const auto sr = sf.spriteSrc.find(a);
                        const int y = sr == sf.spriteSrc.end() ? -1 : sr->second.second;
                        // the strip is at x = 0 and 46 to a figure; anything
                        // else is the item's own lit place, so: blank
                        const bool digit = sr != sf.spriteSrc.end() &&
                                           sr->second.first == 0 && y >= 0 && (y % 46) == 0;
                        read += digit ? std::to_string(y / 46) : std::string("-");
                        read += ' ';
                    }
                    static std::string denTold;
                    if (read != denTold) {
                        denTold = read;
                        std::printf("den locker: the display reads %s(the hand is on %d, "
                                    "and that wheel blinks)\n", read.c_str(), walk->denWheel());
                    }
                }
                // ---- THE HIGH-SCORE TABLE, reported from the draw --------
                //
                // `scoreBlocks` is what the hook actually laid out: the title,
                // the page heading and two per row - so a page whose rows are
                // blank says so by its count, and a heading that resolved to
                // nothing cannot be claimed.
                if (openScreen == 36) {
                    static std::string hsTold;
                    std::string rows;
                    for (const auto& r : sf.scoreRows) rows += " | " + r;
                    const std::string said = "page " + std::to_string(walk->highScorePage()) +
                                             ", " + std::to_string(sf.scoreBlocks) +
                                             " blocks drawn:" + rows;
                    if (said != hsTold) {
                        hsTold = said;
                        std::printf("high score: %s\n", said.c_str());
                    }
                }
                // ---- THE CITY MAP, reported from the DRAW ----------------
                //
                // The line above says what the viewer RESOLVED; this one says
                // what the two hooks put on the frame - `mapSheet` is set
                // inside the blit's own `if (item->tag)` arm, and every point
                // is the projection's own output. A run that matched the city
                // and then drew nothing would read differently here, which is
                // the half a line printed at the hand-over cannot see.
                if (openScreen == omk::kScreenSneak && sf.mapPin) {
                    static std::string mapDrawTold;
                    std::string said = std::string("the sheet ") +
                        (sf.mapSheet ? "blitted" : "MISSING") + ", the pin at " +
                        std::to_string(sf.mapPinAt[0]) + "," +
                        std::to_string(sf.mapPinAt[1]) + ", " +
                        std::to_string(sf.mapMarkers.size()) + " markers:";
                    for (const auto& m : sf.mapMarkers)
                        said += " " + m.first + " at " +
                                std::to_string(m.second.first) + "," +
                                std::to_string(m.second.second) + " |";
                    if (!said.empty() && said.back() == '|') said.resize(said.size() - 2);
                    if (said != mapDrawTold) {
                        mapDrawTold = said;
                        std::printf("sneak map: %s\n", said.c_str());
                    }
                }
                // ---- XACHEN, reported from the draw for the same reason ---
                //
                // The symbol is a CELL of the artwork, so what a check can see
                // is the rect the composer sampled; turning it back into the
                // value through the same table the hook used says the symbol
                // reached the screen rather than only the walk.
                if (openScreen == 14 && !xachenItems.empty()) {
                    std::string read;
                    for (const std::uint32_t a : xachenItems) {
                        const auto sr = sf.spriteSrc.find(a);
                        int v = -1;
                        if (sr != sf.spriteSrc.end())
                            for (int k = 1; k <= 14; ++k) {
                                int xy[2];
                                omk::UiWalk::xachenSprite(k, xy);
                                if (xy[0] == sr->second.first && xy[1] == sr->second.second)
                                    { v = k; break; }
                            }
                        read += (v < 0 ? std::string("-") : std::to_string(v)) + " ";
                    }
                    static std::string xaTold;
                    if (read != xaTold) {
                        xaTold = read;
                        std::printf("xachen: the cartridges show %s(%s)\n", read.c_str(),
                                    walk->xachenSolved() ? "10 14 7 9 - the door opens"
                                                         : "not the code");
                    }
                }
                // ---- THE SNEAK'S ECHO BAR, reported from the DRAW ------
                //
                // `sf.echoBar` is the string the composer's transcription of
                // `sub_0049DC20` handed the LAYOUT, taken after it ran, and
                // `sf.echoArm` is which of the function's seven branches
                // produced it - so an empty bar (the examine box's arm, or a
                // panel with nothing selected) reads differently from a bar
                // the walk never reached at all, and no part of the line can
                // be satisfied by something this file computed.
                //
                // `sf.rowMarks` is the row hook `0x0049C090`'s own fills, and
                // `rowMarked` the row tags it marked - the second mark being
                // what a `Utiliser sur` shows and nothing else in the device
                // does.
                if (openScreen == 9) {
                    static std::string echoTold;
                    std::string marks;
                    for (int t : sf.rowMarked) marks += " " + std::to_string(t);
                    const std::string said =
                        "arm " + std::to_string(sf.echoArm) + " '" + sf.echoBar +
                        "', " + std::to_string(sf.rowMarks) + " row marks" +
                        (marks.empty() ? std::string() : " (tags" + marks + ")");
                    if (said != echoTold) {
                        echoTold = said;
                        std::printf("sneak: echo bar - %s\n", said.c_str());
                    }
                }
                // ...and the memo body is reported from the DRAW: `textLines`
                // counts what the body/examine block laid out, so a box fed
                // nothing says 0 lines instead of repeating what it was handed.
                if (!memoBodyPending.empty()) {
                    static std::string memoBodyTold;
                    // ...and WHEN, because the content alone stopped being
                    // able to say it. The box is lit as soon as the selection
                    // enters the list, and the selection starts on row 0 - so
                    // a body wrongly drawn while the page merely SITS OPEN
                    // prints the same `row 0 id 913` as the right one does.
                    // The frame separates them: the legitimate line cannot
                    // appear before the press that selects the line. It is a
                    // property of this harness (fixed `--frames` and
                    // `--keydelay`), not of the engine.
                    const std::string said = memoBodyPending + ", " +
                                             std::to_string(sf.textChars) + " chars, " +
                                             std::to_string(sf.textLines) + " lines drawn";
                    if (said != memoBodyTold) {
                        memoBodyTold = said;
                        // The frame is stamped on the line but kept OUT of the
                        // change test: inside it, every frame differed and the
                        // same body printed on all 360 of them.
                        std::printf("sneak: memo body - %s at frame %ld\n",
                                    said.c_str(),
                                    static_cast<long>(session.frameNo()));
                    }
                    memoBodyPending.clear();
                }
                // ---- THE MEMO READER ---------------------------------
                //
                // The page a memo row's confirm installs (`sub_49BC60`'s
                // kind-2 arm). It ships `+24 = 2`, so it comes up standing in
                // the body box's own list, whose hook is the scroller - which
                // is what makes UP and DOWN move the text rather than the
                // selection. Reported as the list it stands in and the scroll
                // offset, beside what the box actually drew.
                if (const omk::UiPanel* rp = walk->panel();
                    rp && rp->addr == omk::kPanelSneakReader) {
                    static std::string readerTold;
                    const std::string said =
                        "list " + std::to_string(walk->currentList()) +
                        ", scroll " + std::to_string(walk->textScroll()) +
                        ", " + std::to_string(sf.textChars) + " chars, " +
                        std::to_string(sf.textLines) + " lines drawn";
                    if (said != readerTold) {
                        readerTold = said;
                        std::printf("sneak: memo reader - %s\n", said.c_str());
                    }
                }
            }
        }

        // A `media.play` line, while `Subtitle_Show`'s timer runs: inset 16,
        // against the bottom, white. A conversation's own text takes over.
        // The document bitmap sits over the frame until the next media.play
        // replaces it. Black is the key - 284581 of `ZVOG001`'s 307200 pixels
        // are it - so only the logo lands on the scene.
        if (mediaBmp.w > 0 && mediaBmp.h > 0) {
            // SCALED TO THE DISPLAY, like every other interface bitmap: the
            // interface is authored at 640x480 and `ScreenDraw` maps it with
            // `v * width / 640` and `v * height / 480`. Blitting 1:1 from the
            // origin put the logo in the top-left corner at native size.
            // Nearest-neighbour, because the port's rule for the 2D layer is
            // an exact copy with no filtering (`ui/surface.h`).
            // The source COLUMN per display column, once for the two widths:
            // `x * w / fb.w` per pixel was a library divide on the Vita's A9,
            // 522K a frame - 83 ms of every frame the title logo was up in the
            // Bowie sequence (2026-09-30), where the original's is one
            // DirectDraw colour-keyed blit.
            static std::vector<int> mediaCol;
            static int mediaColFor[2] = {-1, -1};
            if (mediaColFor[0] != mediaBmp.w || mediaColFor[1] != fb.w) {
                mediaColFor[0] = mediaBmp.w; mediaColFor[1] = fb.w;
                mediaCol.resize(static_cast<std::size_t>(fb.w));
                for (int x = 0; x < fb.w; ++x) mediaCol[static_cast<std::size_t>(x)] = x * mediaBmp.w / fb.w;
            }
            for (int y = 0; y < fb.h; ++y) {
                const int sy = y * mediaBmp.h / fb.h;
                if (sy < 0 || sy >= mediaBmp.h) continue;
                for (int x = 0; x < fb.w; ++x) {
                    const int sx = mediaCol[static_cast<std::size_t>(x)];
                    if (sx < 0 || sx >= mediaBmp.w) continue;
                    const std::uint16_t src =
                        mediaBmp.px[static_cast<std::size_t>(sy) *
                                    static_cast<std::size_t>(mediaBmp.w) +
                                    static_cast<std::size_t>(sx)];
                    if (!src) continue;                       // the colour key
                    fb.px[static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(fb.w) +
                          static_cast<std::size_t>(x)] = src;
                }
            }
        }
        // ...and it DOES draw over the videophone: two of the ten sneak
        // calls are a `media.play` and nothing else, so a subtitle suppressed
        // by "a screen is up" would lose the whole of those two.
        if (!session.dialogOpen() && (!walk || openScreen == kScreenVideophone) &&
            mediaTextFrames > 0) {
            // A DIFFERENT FACE, and it is the engine's choice. The
            // dialogue's params are TEXTP_FLAG_A alone, so its font stays the
            // `Text_DrawBlock` default 74 = 'J'; `Subtitle_Show` (0x0041E040)
            // passes `params[0] = 0x20 | 0x40` and `params[2] = 86`, and
            // TEXTP_SLOT2 writes `dword_907A10 = params[2]` - the font global
            // whose default is that 74. So the adventure-mode interaction
            // line, the one that always comes with a sound, is face 86 = 'V'.
            // A credit block positions itself; anything else is the
            // ordinary bottom-anchored subtitle.
            const auto ptMedia = omk::parseMarkup(mediaText, 'V');
            if (!drawPositioned(fb, lay, ptMedia, dispW, dispH))
                drawSubtitle(fb, lay, mediaText, {}, -1, dispW, dispH, 16,
                             SubBox::None, 'V', 0, nullptr, /*mediaLine*/ true);
            mediaTextFrames -= frameSec * 30.0;
        }
        // The subtitle goes over whatever the frame already holds - which
        // during a conversation is the dialogue camera's view of the set.
        if (session.dialogOpen()) {
            const auto& dlg = session.dialogue();
            std::vector<std::string> menu;
            int sel = -1;
            if (dlg.phase() == omk::DialogPhase::Menu) {
                for (const auto& r : dlg.replies()) {
                    if (!r.available) continue;
                    if (r.branch == (replySel >= 0 &&
                                     replySel < static_cast<int>(dlg.replies().size())
                                         ? dlg.replies()[static_cast<std::size_t>(replySel)].branch
                                         : -1))
                        sel = static_cast<int>(menu.size());
                    menu.push_back(r.text);
                }
            }
            // "The game never shows the NPC line and the menu together" -
            // the rule `DialogPlayer`'s phases already carry, and it belongs
            // to the drawing too. In the menu phase the line is gone.
            const bool inMenu = dlg.phase() == omk::DialogPhase::Menu;
            // ONE PIXEL A TICK WHILE HELD, clamped to the overflow - the
            // engine's own rule. Only the spoken line scrolls; the reply
            // stack is anchored by its own height and never clipped.
            //
            // Read from the ENGINE'S INPUT WORD, slots 2 and 3 (Avancer /
            // Reculer - UP and DOWN on the keyboard), not from SDL's keyboard:
            // that was a host key read around the bindings, so on a Vita - no
            // keyboard - the long intro line could not be scrolled at all.
            // The word carries the keyboard arrows, the pad's stick and d-pad
            // (Input_Poll's hardwired axes) and any rebinding alike.
            if (!inMenu && lineOverflow > 0) {
                if ((heldBits & 0x8u) && lineScroll < lineOverflow) ++lineScroll;   // down
                if ((heldBits & 0x4u) && lineScroll > 0)            --lineScroll;   // up
            }
            spanned("dialogue text", [&] {
            drawSubtitle(fb, lay,
                         inMenu ? std::string() : dlg.lineText(),
                         menu, sel, dispW, dispH, 32,
                         inMenu ? SubBox::Replies : SubBox::Line, 'J',
                         lineScroll, &lineOverflow);
            });
        }
        mark("screens, hud");
        // ---- THE FPS COUNTER -------------------------------------------
        //
        // Measured over a WINDOW rather than per frame, because a per-frame
        // reciprocal is mostly noise: this loop sleeps to pace itself, so a
        // single frame's time says more about the sleep than about the work.
        // It reports the rate and the worst frame in the window, which is what
        // says whether a hitch is happening at all.
        // ...and ALWAYS in the window's title, once a second: the same window
        // measure, appended to the title the window was opened with (read
        // back once, so each backend keeps its own label). `--fps` adds the
        // terminal line with the worst frame.
        {
            ++fpsFrames;
            const Uint32 nowMs = SDL_GetTicks();
            const Uint32 dtMs = nowMs - fpsLastMs;
            if (dtMs > fpsWorst) fpsWorst = dtMs;
            fpsLastMs = nowMs;
            if (nowMs - fpsSince >= 1000) {
                const double secs = (nowMs - fpsSince) / 1000.0;
                const double rate = fpsFrames / secs;
                if (SDL_Window* tw = front.active()) {
                    static std::string baseTitle;
                    static SDL_Window* titled = nullptr;
                    if (titled != tw) {
                        const char* t = SDL_GetWindowTitle(tw);
                        baseTitle = t ? t : "OMK Engine";
                        titled = tw;
                    }
                    char buf[48];
                    std::snprintf(buf, sizeof buf, " - %.0f fps", rate);
                    SDL_SetWindowTitle(tw, (baseTitle + buf).c_str());
                }
                if (showFps) {
                    std::printf("fps %.1f  (%d frames, worst %u ms)  %s%s\n",
                                rate, fpsFrames, fpsWorst,
                                vkRen ? "vulkan" : glRen ? "gles" : "software",
                                drawWorld ? ", 3D" : ", 2D only");
                    std::fflush(stdout);
                }
                fpsSince = nowMs; fpsFrames = 0; fpsWorst = 0;
            }
        }

        // ---- THE SCREEN FADES, over everything --------------------------
        //
        // `Screen_StartColorFade` and `Screen_Fade` both end in a full-screen
        // quad the engine submits AFTER the scene, so this is the last thing
        // before the frame goes out. Two fades run independently and the
        // engine draws both, so both are applied in turn.
        //
        // The blend is a MODEL - see `Session::ScreenFade`. The engine picks
        // one of three I2D quad flags by colour and none of the three is
        // traced; mixing toward the colour matches the ramp's direction for
        // every mode and colour, which is what a viewer sees.
        {
            const omk::Session::ScreenFade& cf = session.colourFade();
            const float k = cf.weight();
            ovFade[3] = 0.0f;
            if (cf.running() && k > 0.0f && g_ov.on) {
                // on an overlay frame the mix is the present shader's: the
                // same law over every pixel, world and interface alike - the
                // CPU loop cost 200-300 ms a frame on a console
                ovFade[0] = static_cast<float>((cf.colour >> 16) & 0xFF) / 255.0f;
                ovFade[1] = static_cast<float>((cf.colour >> 8) & 0xFF) / 255.0f;
                ovFade[2] = static_cast<float>(cf.colour & 0xFF) / 255.0f;
                ovFade[3] = k;
            } else if (cf.running() && k > 0.0f) {
                const int cr = static_cast<int>((cf.colour >> 16) & 0xFF);
                const int cg = static_cast<int>((cf.colour >> 8) & 0xFF);
                const int cb = static_cast<int>(cf.colour & 0xFF);
                for (std::size_t ovI = 0; ovI < fb.px.size(); ++ovI) {
                    const bool ovKey = g_ov.on && fb.px[ovI] == kOverlayKey;
                    if (ovKey) { g_ov.row(ovI); g_ov.m[ovI] = static_cast<std::uint8_t>(g_ov.m[ovI] * (1.0f - k)); }
                    std::uint16_t& px = ovKey ? g_ov.c[ovI] : fb.px[ovI];
                    int r = ((px >> 11) & 31) << 3, g = ((px >> 5) & 63) << 2, b = (px & 31) << 3;
                    r += static_cast<int>((cr - r) * k);
                    g += static_cast<int>((cg - g) * k);
                    b += static_cast<int>((cb - b) * k);
                    px = static_cast<std::uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
                }
            }
        }
        // ...and the BLACK fade only over the two LETTERBOX BANDS. Both were
        // applied to every pixel here, on the stated premise that "both end in
        // a full-screen quad" - true of the colour half, false of this one.
        // The ticker submits two quads of `v3 = (height << 6) / 480` rows, at
        // the top and the bottom, shading from `v8`'s grey on the inner edge
        // to `v7`'s at the screen edge; the middle of the picture is never
        // touched. Applied full-screen it blacked out the whole frame at the
        // end of every cutscene and then snapped back when state 4 cleared,
        // which is `todo/omk-play.md` 56.
        {
            const omk::Session::ScreenFade& bf = session.blackFade();
            {
                // `OMK_FADELOG=1`: the black fade's mode, clock and both
                // band greys. It exists to tell the fade's own two bands apart
                // from the LETTERBOX, which occupies the same 64 rows at 480
                // and is what one report turned out to be.
                static const bool fadeLog = [] {
                    const char* e = std::getenv("OMK_FADELOG"); return e && *e == '1';
                }();
                static std::string told;
                if (fadeLog) {
                    char buf[160];
                    std::snprintf(buf, sizeof buf,
                        "mode %d clock %.1f/%.1f inner %d outer %d running %d",
                        bf.mode, (double)bf.clock, (double)bf.duration,
                        bf.bandGrey(false), bf.bandGrey(true), bf.running() ? 1 : 0);
                    if (told != buf) {
                        told = buf;
                        std::printf("frame %ld: blackFade %s\n", n, buf);
                    }
                }
            }
            if (bf.running() && fb.h > 0) {
                const int band = (fb.h * 64) / 480;
                const int inner = bf.bandGrey(false), outer = bf.bandGrey(true);
                if (band > 0 && (inner < 255 || outer < 255)) {
                    for (int y = 0; y < fb.h; ++y) {
                        // distance from the screen edge, 0 at the edge and
                        // `band` at the inner lip; outside the bands, nothing.
                        int d;
                        if (y < band) d = y;
                        else if (y >= fb.h - band) d = fb.h - 1 - y;
                        else continue;
                        const float t = band > 1 ? static_cast<float>(d) / static_cast<float>(band - 1)
                                                 : 1.0f;
                        const int grey = outer + static_cast<int>((inner - outer) * t);
                        // `v * grey / 255` for every byte, once a ROW: the
                        // same law, four lookups a pixel instead of four
                        // multiply-divides (the console's A9, 2026-09-29)
                        std::uint8_t scale[256];
                        for (int v = 0; v < 256; ++v) scale[v] = static_cast<std::uint8_t>(v * grey / 255);
                        const std::size_t row0 = static_cast<std::size_t>(y) * static_cast<std::size_t>(fb.w);
                        for (int x = 0; x < fb.w; ++x) {
                            const std::size_t ovI = row0 + static_cast<std::size_t>(x);
                            const bool ovKey = g_ov.on && fb.px[ovI] == kOverlayKey;
                            if (ovKey) { g_ov.row(ovI); g_ov.m[ovI] = scale[g_ov.m[ovI]]; }
                            std::uint16_t& px = ovKey ? g_ov.c[ovI] : fb.px[ovI];
                            const int r = scale[((px >> 11) & 31) << 3], g = scale[((px >> 5) & 63) << 2],
                                      b = scale[(px & 31) << 3];
                            px = static_cast<std::uint16_t>(((r >> 3) << 11) |
                                                            ((g >> 2) << 5) | (b >> 3));
                        }
                    }
                }
            }
        }

        // ...and what the finished frame actually CONTAINS, for hunting a
        // flicker: a body drawn on a set that is not there shows up as a lit
        // count far below its neighbours', which is what a reader reports as
        // "Kay'l with a black background" and what no still frame can be
        // chosen to catch.
        if (omk::envSet("OMK_CLIPLOG")) {
            // Lit pixels, and how many changed since the previous frame. The
            // second is what a POP looks like: a body or a camera moving a
            // long way in one frame repaints a large part of the picture, and
            // a hold repaints almost none. Neither is a claim about the
            // engine - this is a diagnostic and nothing draws differently
            // because of it.
            static std::vector<std::uint16_t> prevPx;
            long litpx = 0, diffpx = 0;
            for (std::uint16_t v : fb.px) if (v) ++litpx;
            if (prevPx.size() == fb.px.size())
                for (std::size_t k = 0; k < fb.px.size(); ++k)
                    if (fb.px[k] != prevPx[k]) ++diffpx;
            prevPx = fb.px;
            std::printf("  [lit] frame %ld  %ld of %zu  changed %ld\n",
                        n, litpx, fb.px.size(), diffpx);
        }
        // ---- THE FLICKER CATCHER -------------------------------------
        if (!flickerDir.empty()) {
            long lit = 0;
            for (std::uint16_t v : fb.px) if (v) ++lit;
            if (frameNote.empty()) frameNote = "2D only - the world was not drawn";
            auto write = [&](long fno, const std::vector<std::uint16_t>& px,
                             const std::string& note) {
                const std::string path = flickerDir + "/flick-" + std::to_string(flickEvent) +
                                         "-" + std::to_string(fno) + ".bin";
                if (!omk::safeOutputPath(path)) return;
                std::ofstream o(path, std::ios::binary);
                for (auto v : px) {
                    const char b2[2] = {static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)};
                    o.write(b2, 2);
                }
                std::ofstream t(flickerDir + "/flick-" + std::to_string(flickEvent) + ".txt",
                                std::ios::app);
                t << "frame " << fno << "  " << note << "\n";
            };
            // The baseline is the MEDIAN of a window, not the previous frame:
            // the fault lasts up to five frames, so its neighbours are inside
            // it and a neighbour test cannot see it.
            long base = 0;
            if (flickLit.size() >= static_cast<std::size_t>(kFlickWindow)) {
                std::vector<long> w(flickLit.end() - kFlickWindow, flickLit.end());
                std::nth_element(w.begin(), w.begin() + w.size() / 2, w.end());
                base = w[w.size() / 2];
            }
            if (flickAfter > 0) {                       // still writing an event
                write(n, fb.px, frameNote);
                --flickAfter;
            } else if (base > 0 && lit < base * 3 / 5 && n > flickQuietUntil) {
                flickEvent = n;
                flickQuietUntil = n + 60;               // one dump per two seconds
                std::printf("frame %ld: FLICKER - %ld lit against a median of %ld; "
                            "writing %d frames to %s/flick-%ld-*.bin\n",
                            n, lit, base, kFlickPre + 1 + kFlickPost,
                            flickerDir.c_str(), flickEvent);
                for (std::size_t k = 0; k < flickRing.size(); ++k)
                    write(n - static_cast<long>(flickRing.size() - k), flickRing[k],
                          k < flickNote.size() ? flickNote[k] : std::string());
                write(n, fb.px, frameNote);
                flickAfter = kFlickPost;
            }
            flickLit.push_back(lit);
            if (flickLit.size() > static_cast<std::size_t>(kFlickWindow)) flickLit.erase(flickLit.begin());
            flickRing.push_back(fb.px);
            flickNote.push_back(frameNote);
            if (flickRing.size() > static_cast<std::size_t>(kFlickPre)) {
                flickRing.erase(flickRing.begin());
                flickNote.erase(flickNote.begin());
            }
            frameNote.clear();
        }
#if defined(OMK_VULKAN)
        if (gpuFrame && verifyGpuPresent) {
            // the GPU's picture against the CPU frame, as the upload would
            // expand it - every byte of every pixel
            static std::vector<unsigned char> gpuPic;
            static long compared = 0, differing = 0, badPixels = 0;
            if (omk::vulkanWorldPicture(&world, gpuVy, gpuVh, gpuPic) &&
                gpuPic.size() == fb.px.size() * 4) {
                const unsigned char* lut = omk::expand565Rgba();
                long diff = 0;
                for (std::size_t i = 0; i < fb.px.size(); ++i)
                    if (std::memcmp(lut + 4 * static_cast<std::size_t>(fb.px[i]), &gpuPic[4 * i], 4) != 0) ++diff;
                ++compared;
                if (diff) {
                    ++differing; badPixels += diff;
                    // at once, so a run with only a few GPU frames still says so
                    std::printf("gpu present verify: frame %ld DIFFERS in %ld pixels\n", n, diff);
                }
            }
            if (compared > 0 && compared % 30 == 0)
                std::printf("gpu present verify: frame %ld, %ld frames compared, %ld differ (%ld pixels)\n",
                            n, compared, differing, badPixels);
        }
#endif
        {
            static long gpuPresented = 0, framesSeen = 0;
            static std::map<std::string, long> keptBy;
            ++framesSeen;
            if (gpuFrame) ++gpuPresented;
            else ++keptBy[gpuKeep];
            if (omk::envSet("OMK_GPU_PRESENT_STATS") && framesSeen % 30 == 0) {
                std::printf("gpu present: frame %ld, %ld of %ld frames stayed on the GPU; on the CPU:",
                            n, gpuPresented, framesSeen);
                for (const auto& [why, count] : keptBy) std::printf(" %s %ld,", why.c_str(), count);
                std::printf("\n");
            }
            if (gpuFrame) ++phGpu; else ++phKept[gpuKeep];
        }
        done = true;
    } while (false);
    return done ? -1 : -2;
}
