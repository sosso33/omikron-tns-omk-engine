// SPDX-License-Identifier: GPL-3.0-or-later
// THE HUDS AND THE OVERLAY: the fight HUD, the breath gauge, the shoot HUD, the fps counter, the fades, the flicker catcher.
// Parts of `PlayState::phaseScreens`, moved byte for byte by `todo/play-split.md`
// (2026-10-02); the phase calls them in this order.
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
                  bool mediaLine = false, std::uint32_t nowMs = 0) {
    // `nowMs`: the host's tick (`Frontend::ticksMs`), which pulses the scroll
    // arrows below - handed in, as a free function has no frontend.
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
                              static_cast<double>(nowMs) * 0.006));
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

// The fight HUD, the breath gauge, the shoot HUD
void PlayState::screensHuds() {
    OMK_ZONE("screens: huds");   // the profiler (todo/debug-tools.md 6)
    const auto& fs = *fs_;
    auto& lay = *lay_;
    auto& comp = *comp_;
    auto& optMenu = *optMenu_;
    auto& session = *session_;
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
    // ---- THE SCRIPT TIMER'S READOUT (`todo/drift-audit.md` S1) --------
    //
    // `sub_41E480`'s head, every `Game_Tick`: nothing while the timer is
    // halted (bit 0); under bit 3 the time, through `Text_DrawBlock(0, 20,
    // width, 50, text, {0x28, -1, 67})` - TEXTP_SLOT2 | TEXTP_ALIGN_8, so
    // face 67 `'C'`, CENTRED, the default white, and below 640x480 the size
    // step to 76 `'L'` that `Text_DrawBlock` makes over any face. The text
    // is `%2d:%02d:%02d` of MINUTES, SECONDS and HUNDREDTHS (`v2 % 1000 /
    // 10`), not hours. Expired (bit 4) it shows the VALUE itself, whatever
    // the countdown bit says; otherwise `clock - start`, and `value -` that
    // under bit 2 - the Tetra raids' mode 12. The `flags == 1` arm inside is
    // unreachable behind the bit-0 return and is left out.
    {
        using GS = omk::GameState;
        const int f = state.timerFlags();
        if (!(f & GS::kTimerStopped) && (f & GS::kTimerVisible) && !omk::envSet("OMK_NOUI")) {
            int v = 0;
            if (f & GS::kTimerExpired) {
                v = state.timerValue();
            } else {
                v = state.clock() - state.timerBase();
                if (f & GS::kTimerCountdown) v = state.timerValue() - v;
            }
            char text[48];
            std::snprintf(text, sizeof text, "%2d:%02d:%02d",
                          v / 1000 / 60 % 60, v / 1000 % 60, v % 1000 / 10);
            omk::TextBlock blk;
            blk.left = 0;  blk.top = 20;
            blk.right = fb.w;  blk.bottom = 50;
            blk.font = (fb.w < 640 || fb.h < 480) ? 'L' : 'C';
            blk.style = 8;
            blk.screenW = fb.w;  blk.screenH = fb.h;
            omk::BlockResult res;
            lay.layOutBlock(&fb, text, blk, &res);
            if (n % 30 == 0)
                std::printf("    script timer readout \"%s\": %d line(s), advance %d\n",
                            text, res.lines, res.advance);
        }
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
        // ...and ASTAROTH's, which his own tick draws every frame it runs:
        // `Hud_DrawBar(+92, 200, 1, 0)` (0x480147) - side 1, the gauge melee
        // gives the opponent (`todo/astaroth.md` 3)
        if (astarothBar != -1) {
            const omk::HudBarFrame ab = hudBar.draw(fb, astarothBar, 200, 1, 0);
            static int astBarTold = -2;
            if (astBarTold != ab.percent) {
                astBarTold = ab.percent;
                std::printf("frame %ld: Astaroth's gauge (Hud_DrawBar side 1): %d/200 = %d%%, "
                            "top %d\n", n, astarothBar, ab.percent, ab.top);
            }
            astarothBar = -1;
        }
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
                         SubBox::None, 'V', 0, nullptr, /*mediaLine*/ true, front.ticksMs());
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
                     lineScroll, &lineOverflow, false, front.ticksMs());
        });
    }
    mark("screens, hud");
}

// The fps counter
void PlayState::screensFps() {
    OMK_ZONE("screens: fps");   // the profiler (todo/debug-tools.md 6)
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
        const std::uint32_t nowMs = front.ticksMs();
        const std::uint32_t dtMs = nowMs - fpsLastMs;
        if (dtMs > fpsWorst) fpsWorst = dtMs;
        fpsLastMs = nowMs;
        if (nowMs - fpsSince >= 1000) {
            const double secs = (nowMs - fpsSince) / 1000.0;
            const double rate = fpsFrames / secs;
            if (const void* tw = front.windowId()) {
                static std::string baseTitle;
                static const void* titled = nullptr;
                if (titled != tw) {
                    baseTitle = front.windowTitle();
                    if (baseTitle.empty()) baseTitle = "OMK Engine";
                    titled = tw;
                }
                char buf[48];
                std::snprintf(buf, sizeof buf, " - %.0f fps", rate);
                front.setWindowTitle(baseTitle + buf);
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
}

// The screen fades, over everything
void PlayState::screensFades() {
    OMK_ZONE("screens: fades");   // the profiler (todo/debug-tools.md 6)
    auto& session = *session_;
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
}
