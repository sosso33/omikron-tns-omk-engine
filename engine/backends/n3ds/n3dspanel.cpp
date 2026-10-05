// SPDX-License-Identifier: GPL-3.0-or-later
// See `n3dspanel.h`.
#include "n3dspanel.h"

#include "n3dshost.h"
#include "platform/profile.h"

#include <3ds.h>
#include <malloc.h>

#include <cstdio>
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
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) s.set(x, y, c);
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

void Panel::draw(Surface& s, const PanelStats& st) const {
    fillRect(s, 0, 0, kW, kH, 0);
    char line[160];
    int y = 2;

    std::snprintf(line, sizeof line, "OMK - %s 3DS, %s", st.newModel ? "New" : "Old",
                  st.newModel ? "804 MHz" : "268 MHz");
    text(s, 4, y, line, 255, 255, 255);
    y += kLine;
    std::snprintf(line, sizeof line, "%.1f fps   frame %.1f ms (worst %.1f)   busy %.0f%%",
                  st.fps, st.frameMs, st.worstMs, st.busyPct);
    text(s, 4, y, line, 255, 255, 160);
    y += kLine;

#if OMK_PROFILE
    const struct mallinfo mi = mallinfo();
    std::snprintf(line, sizeof line, "heap %.1f/%.1f MB  linear %.1f free  vram %.1f free",
                  mi.uordblks / 1048576.0, heapBytes() / 1048576.0,
                  linearSpaceFree() / 1048576.0, vramSpaceFree() / 1048576.0);
    text(s, 4, y, line, 160, 220, 255);
    y += kLine;
    const prof::MemTotals mt = prof::memTotals();
    std::snprintf(line, sizeof line, "counted live %.1f MB, peak %.1f MB, %ld blocks",
                  mt.live / 1048576.0, mt.peak / 1048576.0, mt.blocks);
    text(s, 4, y, line, 160, 220, 255);
    y += kLine;
    if (capturePath().empty()) {
        text(s, 4, y, "profiler: off - --profile <path> in args.txt for the buttons", 200, 160, 160);
    } else {
        const std::string state = profilerState();
        std::snprintf(line, sizeof line, "profiler: %s  %s", capturePath().c_str(),
                      state.empty() ? "(no state yet)" : state.c_str());
        text(s, 4, y, line, 160, 255, 160);
    }
    y += kLine;
    if (!st.note.empty()) {
        text(s, 4, y, st.note, 255, 200, 120);
        y += kLine;
    }

    // the log, as much of it as fits above the buttons
    y += 3;
    fillRect(s, 0, y - 2, kW, y - 1, 0x4208);
    const int rows = (kButtonTop - 2 - y) / kLine;
    if (rows > 0) {
        for (const std::string& l : logTail(static_cast<std::size_t>(rows))) {
            text(s, 4, y, l, 150, 150, 150);
            y += kLine;
        }
    }

    // the buttons
    for (int i = 0; i < 4; ++i) {
        const int x0 = i * kButtonW;
        fillRect(s, x0 + 2, kButtonTop, x0 + kButtonW - 2, kH - 2, 0x2945);
        fillRect(s, x0 + 2, kButtonTop, x0 + kButtonW - 2, kButtonTop + 1, 0x8410);
        const int w = layout_ ? layout_->measure(kButtons[i].label, kFace) : 0;
        text(s, x0 + (kButtonW - w) / 2, kButtonTop + 7, kButtons[i].label, 255, 255, 255);
    }
#else
    (void)y;
#endif
}

// The bottom framebuffer is 240 pixels a column, 320 columns, a column
// running bottom to top - screen pixel (x, y) is word `x * 240 + (239 - y)`.
void Panel::toScreen(const Surface& s) {
    u16 fw = 0, fh = 0;
    auto* dst = reinterpret_cast<std::uint16_t*>(gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &fw, &fh));
    if (!dst || fw != 240 || fh != 320 || s.w != kW || s.h != kH) return;
    for (int x = 0; x < kW; ++x) {
        std::uint16_t* col = dst + x * 240;
        for (int y = 0; y < kH; ++y) col[239 - y] = s.px[static_cast<std::size_t>(y) * kW + x];
    }
}

}  // namespace omk::n3ds
