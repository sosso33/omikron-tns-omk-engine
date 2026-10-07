// SPDX-License-Identifier: GPL-3.0-or-later
// THE INSTRUMENT PANEL on the 3DS's BOTTOM screen (`todo/3ds-port.md` step
// 2b, the reader's ask of 2026-10-05: "use the bottom screen to show
// available stats (like fps, used ram, cpu usage, ...), some debugging
// options (button to capture a frame for the debugger for example)").
//
// An INSTRUMENT, not the game: the original had one screen, and nothing the
// game draws goes here (the interface stays on the top screen until the
// per-screen survey, step 10). Drawn on the CPU into a 320x240 RGB565
// surface with the ENGINE'S OWN text renderer and the game's SMALL face
// (`FONTS/SMALL.FNT`, 12 px - the smallest of the 13), so the frontend still
// draws no text of its own (A8 rule 3), and redrawn a few times a second -
// the numbers move slowly, and a panel redrawn every frame would cost the
// frame it measures.
//
//   * the console line - model, clock;
//   * the frame - fps, mean and worst frame time over the last second, and
//     the CPU's BUSY share: the frame interval less the time the viewer's
//     pacer slept (`Frontend::delayMs`), over the interval;
//   * the memory - the newlib heap in use of what libctru gave it, the linear
//     heap and VRAM free, and in a profiling build the counting allocator's
//     live and peak (`prof::memTotals`);
//   * the profiler - whether a capture is open (`--profile <path>` in
//     args.txt) and the state the game last wrote to `<path>.state`;
//   * the log's last lines;
//   * four TOUCH buttons: CAPTURE (the frame on show written to
//     `sdmc:/omk/captures/`, raw LE RGB565 as `--dump` writes, and a profiler
//     snapshot asked for when a capture is open), and PAUSE / STEP / RESUME,
//     the profiler's own commands written to `<path>.ctl` exactly as
//     `tools/omkprof.py --ctl` writes them - so they need a capture open.
//
// A RELEASE build (`OMK_PROFILE 0`) keeps the console line and the frame
// line, and no buttons.
//
// NOT YET, and why: the game's own state (area, camera mode, bodies staged)
// and the ENHANCEMENT toggles need a way from the frontend into the game,
// which is shared code (`platform/frontend.h`) left alone while other
// sessions work there; and the toggles wait for the citro3d backend (step
// 3), since the software reference draws none of them.
#pragma once

#include "ui/surface.h"
#include "ui/text.h"

#include <optional>
#include <string>
#include <vector>

namespace omk::n3ds {

struct PanelStats {
    bool newModel = false;       // a New 3DS (the 804 MHz mode)
    double fps = 0.0;            // presents in the last second
    double frameMs = 0.0;        // their mean interval...
    double worstMs = 0.0;        // ...and the longest
    double busyPct = 0.0;        // the interval less the pacer's sleep, over the interval
    long frames = 0;             // presents since the start
    std::string note;            // the last action's result, shown until replaced
};

class Panel {
public:
    // The font table out of the ROMFS tables and the faces out of the card's
    // FONTS/. -> false when either is missing; the panel then draws nothing
    // but a plain background, and says so in the log.
    bool open();

    enum class Action { None, Capture, Pause, Step, Resume };
    // A touch at (x, y) in the bottom screen's 320x240 -> the button under it.
    Action hit(int x, int y) const;

    // Compose the panel into `s` (320x240).
    void draw(Surface& s, const PanelStats& st) const;

    // `s` onto the bottom screen's framebuffer (turned a quarter, as the
    // top's is).
    static void toScreen(const Surface& s);

private:
    void text(Surface& s, int x, int y, const std::string& t,
              std::uint8_t r, std::uint8_t g, std::uint8_t b) const;
    // A line of the panel: where, what, in which colour.
    struct Line {
        int y = 0;
        std::string t;
        std::uint8_t r = 0, g = 0, b = 0;
        bool operator==(const Line& o) const { return y == o.y && t == o.t && r == o.r && g == o.g && b == o.b; }
    };
    // What the panel wants this redraw: the lines above the log, the log's
    // first row and its lines.
    void wanted(const PanelStats& st, std::vector<Line>& head, int& logY, std::vector<Line>& log) const;
    // The whole panel from scratch into `s`.
    void drawAll(Surface& s, const std::vector<Line>& head, int logY, const std::vector<Line>& log) const;
    // WHAT `s` HOLDS (`todo/3ds-port.md` 6.5): the lines last drawn into it,
    // so a redraw draws only the lines that changed and SCROLLS the log's
    // pixel rows when its tail only moved - ~13.5 ms twice a second on the
    // console redrawn whole.
    mutable const Surface* held_ = nullptr;
    mutable std::vector<Line> heldHead_, heldLog_;
    mutable int heldLogY_ = -1;
    mutable Surface check_;           // OMK_PANEL_CHECK: the whole redraw, to compare
    FontTable fonts_;
    std::optional<TextLayout> layout_;
    bool ok_ = false;
};

}  // namespace omk::n3ds
