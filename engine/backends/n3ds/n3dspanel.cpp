// SPDX-License-Identifier: GPL-3.0-or-later
// See `n3dspanel.h`.
#include "n3dspanel.h"

#include "n3dshost.h"
#include "platform/profile.h"

#include <3ds.h>
#include <malloc.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace omk::n3ds {

namespace {

constexpr int kW = 320, kH = 240;
constexpr char kFace = 'L';          // SMALL, 12 px
constexpr int kLine = 13;            // its height and one row between lines

// The buttons, along the bottom edge.
struct Button { const char* label; Panel::Action action; };
constexpr Button kButtons[] = {
    {"CAPTURE", Panel::Action::Capture}, {"PAUSE", Panel::Action::Pause},
    {"STEP", Panel::Action::Step},       {"RESUME", Panel::Action::Resume},
};
constexpr int kButtonTop = kH - 26, kButtonW = kW / 4;

void fillRect(Surface& s, int x0, int y0, int x1, int y1, std::uint16_t c) {
    x0 = std::max(x0, 0); y0 = std::max(y0, 0);
    x1 = std::min(x1, s.w); y1 = std::min(y1, s.h);
    for (int y = y0; y < y1; ++y)
        std::fill(s.px.begin() + static_cast<std::ptrdiff_t>(y) * s.w + x0,
                  s.px.begin() + static_cast<std::ptrdiff_t>(y) * s.w + x1, c);
}

// What the game wrote last to `<capture>.state` ("paused <frame> <snap>" or
// "running <frame> <snap>"), or "".
std::string profilerState() {
    const std::string& base = capturePath();
    if (base.empty()) return {};
    std::FILE* f = std::fopen((base + ".state").c_str(), "r");
    if (!f) return {};
    char buf[64] = {};
    const std::size_t n = std::fread(buf, 1, sizeof buf - 1, f);
    std::fclose(f);
    std::string s(buf, n);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

}  // namespace

bool Panel::open() {
    fonts_ = FontTable::loadJson(std::string(kTables) + "/ui.json");
    if (!fonts_.byLetter(kFace)) {
        std::printf("panel: no font table in %s/ui.json - the bottom screen stays blank\n", kTables);
        return false;
    }
    layout_.emplace(fonts_, std::string(kRoot) + "/FONTS");
    if (!layout_->face(kFace)) {
        std::printf("panel: no SMALL face in %s/FONTS - the bottom screen stays blank\n", kRoot);
        return false;
    }
    ok_ = true;
    return true;
}

Panel::Action Panel::hit(int x, int y) const {
#if OMK_PROFILE
    if (y >= kButtonTop && y < kH && x >= 0 && x < kW) return kButtons[x / kButtonW].action;
#else
    (void)x; (void)y;
#endif
    return Action::None;
}

// One line in the SMALL face. The log can carry anything: `{` would open the
// markup and a byte past ASCII is not in the face, so both are replaced.
void Panel::text(Surface& s, int x, int y, const std::string& t,
                 std::uint8_t r, std::uint8_t g, std::uint8_t b) const {
    if (!ok_) return;
    std::string clean;
    clean.reserve(t.size());
    for (char c : t) {
        const unsigned char u = static_cast<unsigned char>(c);
        clean += (c == '{' || c == '}') ? '|' : (u < 32 || u > 126 ? '?' : c);
    }
    layout_->drawRun(s, x, y, parseMarkup(clean, kFace, r, g, b).run, 0, kH);
}

void Panel::wanted(const PanelStats& st, std::vector<Line>& head, int& logY, std::vector<Line>& log) const {
    head.clear();
    log.clear();
    logY = -1;
    char line[160];
    int y = 2;
    const auto add = [&](std::vector<Line>& to, std::string t, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        to.push_back(Line{y, std::move(t), r, g, b});
        y += kLine;
    };
    std::snprintf(line, sizeof line, "OMK - %s 3DS, %s", st.newModel ? "New" : "Old",
                  st.newModel ? "804 MHz" : "268 MHz");
    add(head, line, 255, 255, 255);
    std::snprintf(line, sizeof line, "%.1f fps   frame %.1f ms (worst %.1f)   busy %.0f%%",
                  st.fps, st.frameMs, st.worstMs, st.busyPct);
    add(head, line, 255, 255, 160);
#if OMK_PROFILE
    const struct mallinfo mi = mallinfo();
    std::snprintf(line, sizeof line, "heap %.1f/%.1f MB  linear %.1f free  vram %.1f free",
                  mi.uordblks / 1048576.0, heapBytes() / 1048576.0,
                  linearSpaceFree() / 1048576.0, vramSpaceFree() / 1048576.0);
    add(head, line, 160, 220, 255);
    const prof::MemTotals mt = prof::memTotals();
    std::snprintf(line, sizeof line, "counted live %.1f MB, peak %.1f MB, %ld blocks",
                  mt.live / 1048576.0, mt.peak / 1048576.0, mt.blocks);
    add(head, line, 160, 220, 255);
    if (capturePath().empty()) {
        add(head, "profiler: off - --profile <path> in args.txt for the buttons", 200, 160, 160);
    } else {
        const std::string state = profilerState();
        std::snprintf(line, sizeof line, "profiler: %s  %s", capturePath().c_str(),
                      state.empty() ? "(no state yet)" : state.c_str());
        add(head, line, 160, 255, 160);
    }
    if (!st.note.empty()) add(head, st.note, 255, 200, 120);
    // the log, as much of it as fits above the buttons
    y += 3;
    logY = y;
    const int rows = (kButtonTop - 2 - y) / kLine;
    if (rows > 0)
        for (const std::string& l : logTail(static_cast<std::size_t>(rows))) add(log, l, 150, 150, 150);
#endif
}

void Panel::drawAll(Surface& s, const std::vector<Line>& head, int logY, const std::vector<Line>& log) const {
    fillRect(s, 0, 0, kW, kH, 0);
    for (const Line& l : head) text(s, 4, l.y, l.t, l.r, l.g, l.b);
#if OMK_PROFILE
    fillRect(s, 0, logY - 2, kW, logY - 1, 0x4208);
    for (const Line& l : log) text(s, 4, l.y, l.t, l.r, l.g, l.b);
    // the buttons
    for (int i = 0; i < 4; ++i) {
        const int x0 = i * kButtonW;
        fillRect(s, x0 + 2, kButtonTop, x0 + kButtonW - 2, kH - 2, 0x2945);
        fillRect(s, x0 + 2, kButtonTop, x0 + kButtonW - 2, kButtonTop + 1, 0x8410);
        const int w = layout_ ? layout_->measure(kButtons[i].label, kFace) : 0;
        text(s, x0 + (kButtonW - w) / 2, kButtonTop + 7, kButtons[i].label, 255, 255, 255);
    }
#else
    (void)logY; (void)log;
#endif
}

// A redraw draws only what changed (6.5). A line sits in the band of kLine
// rows from its `y`, black around its glyphs, so a changed line is its band
// cleared and drawn again; a log whose tail only MOVED up k lines has its
// pixel rows moved up k bands and its last k lines drawn. Anything else - the
// first draw, another surface, the log's first row moving (a note line came
// or went), a log of another length - is the whole redraw. `OMK_PANEL_CHECK`
// lays the whole redraw beside every incremental one and counts the pixels
// that differ.
void Panel::draw(Surface& s, const PanelStats& st) const {
    std::vector<Line> head, log;
    int logY = -1;
    wanted(st, head, logY, log);
    const bool whole = held_ != &s || s.w != kW || s.h != kH || logY != heldLogY_ ||
                       head.size() != heldHead_.size() || log.size() != heldLog_.size();
    if (whole) {
        drawAll(s, head, logY, log);
    } else {
        const auto band = [&](int y) {
            const int y1 = std::min(y + kLine, kButtonTop);
            if (y1 > y) std::fill(s.px.begin() + static_cast<std::ptrdiff_t>(y) * kW,
                                  s.px.begin() + static_cast<std::ptrdiff_t>(y1) * kW, std::uint16_t(0));
        };
        for (std::size_t i = 0; i < head.size(); ++i) {
            if (head[i] == heldHead_[i]) continue;
            band(head[i].y);
            text(s, 4, head[i].y, head[i].t, head[i].r, head[i].g, head[i].b);
        }
        const std::size_t n = log.size();
        std::size_t k = 0;                      // how far the tail moved
        const auto movedBy = [&](std::size_t m) {
            for (std::size_t i = 0; i + m < n; ++i)
                if (log[i].t != heldLog_[i + m].t) return false;
            return true;
        };
        while (k < n && !movedBy(k)) ++k;
        if (k > 0 && k < n) {
            std::memmove(s.px.data() + static_cast<std::size_t>(logY) * kW,
                         s.px.data() + static_cast<std::size_t>(logY + static_cast<int>(k) * kLine) * kW,
                         static_cast<std::size_t>(n - k) * kLine * kW * sizeof(std::uint16_t));
        }
        for (std::size_t i = (k < n ? n - k : 0); i < n; ++i) {
            band(log[i].y);
            text(s, 4, log[i].y, log[i].t, log[i].r, log[i].g, log[i].b);
        }
    }
    held_ = &s;
    heldHead_ = std::move(head);
    heldLog_ = std::move(log);
    heldLogY_ = logY;
    static const bool check = std::getenv("OMK_PANEL_CHECK") != nullptr;
    if (check && !whole) {
        if (check_.w != kW || check_.h != kH) check_ = Surface(kW, kH, 0);
        drawAll(check_, heldHead_, heldLogY_, heldLog_);
        long differ = 0;
        for (std::size_t i = 0; i < s.px.size(); ++i) differ += s.px[i] != check_.px[i];
        static long redraws = 0, bad = 0;
        ++redraws;
        if (differ) ++bad;
        if (differ || redraws % 10 == 0)
            std::printf("panel check: %ld pixels differ from the whole redraw (%ld of %ld incremental "
                        "redraws differed)\n", differ, bad, redraws);
    }
}

// The bottom framebuffer is 240 pixels a column, 320 columns, a column
// running bottom to top - screen pixel (x, y) is word `x * 240 + (239 - y)`.
void Panel::toScreen(const Surface& s) {
    u16 fw = 0, fh = 0;
    auto* dst = reinterpret_cast<std::uint16_t*>(gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &fw, &fh));
    if (!dst || fw != 240 || fh != 320 || s.w != kW || s.h != kH) return;
    // in 8x8 tiles, as the top screen's copy (`n3dsfront.cpp`): a column of
    // the screen is one pixel of every row of the surface
    for (int tx = 0; tx < kW; tx += 8)
        for (int ty = 0; ty < kH; ty += 8)
            for (int x = tx; x < tx + 8; ++x) {
                std::uint16_t* o = dst + x * 240 + 239 - ty;
                const std::uint16_t* r = s.px.data() + static_cast<std::size_t>(ty) * kW + x;
                for (int j = 0; j < 8; ++j, r += kW) *o-- = *r;
            }
}

}  // namespace omk::n3ds
