// SPDX-License-Identifier: GPL-3.0-or-later
// THE LIVE FRONTEND - a window, a keyboard, and the ported framebuffer.
//
//     omk-play <gamedata> <tables/>                        the interface, live
//     omk-play <gamedata> <tables/> --scene Aapkayl        the SCENE VIEWER
//
// **This is the only file in the tree that includes an SDL header**, which is
// `docs/PORTING.md` A8 rule 2. (`backends/vulkan/vkrender.cpp` is the only one
// that includes Vulkan's, on the same terms and for the same reason; the rule
// is one dependency per backend file, not one backend file in total.) Rule 3 is the one that is not hygiene, and
// it decides what this file may do: SDL creates a window, reports keys and
// uploads a texture. It does **not** blit, scale, blend, lay out or draw text.
// Every pixel it shows was put there by `blt`, `fillQuad` and `drawRun` - the
// ported drawers, each already checked against the engine's own framebuffer -
// and letting SDL do any of that would make the checks test SDL while staying
// exactly as green.
//
// A8 names SDL3. Only SDL2 is installed on the machine this was written on, and
// the surface used here is a dozen calls that both versions have, so it builds
// against either and the Makefile prefers 3. That divergence is recorded in A8
// rather than left for someone to find.
#if defined(OMK_SDL3)
#  include <SDL3/SDL.h>
#  if defined(OMK_VULKAN)
#    include <SDL3/SDL_vulkan.h>
#  endif
#else
#  include <SDL.h>
#  if defined(OMK_VULKAN)
#    include <SDL_vulkan.h>
#  endif
#endif

#include "formats/anim.h"
#include "formats/ctl.h"
#include "formats/mesh3do.h"
#include "formats/scx.h"
#include "formats/tex3dt.h"
#include "input/bindings.h"
#include "actor/moves.h"
#include "actor/pose.h"
#include "actor/speaker.h"
#include "actor/player.h"
#include "actor/moves.h"
#include "actor/walk.h"
#include "o3de/collision.h"
#include "o3de/geom3do.h"
#include "o3de/particles.h"
#include "audio/mixer.h"
#include "audio/music.h"
#include "audio/voiceover.h"
#include "formats/adpcm.h"
#include "script/area.h"
#include "script/savefile.h"
#include "script/gamestate.h"
#include "script/inventory.h"
#include "o3de/raster.h"
#include "o3de/renderer.h"
#include "platform/boot.h"
#include "platform/movie.h"
#include "platform/datafs.h"
#include "platform/frontend.h"
#include "ui/iamtext.h"
#include "ui/screendraw.h"
#include "ui/text.h"
#include "ui/widgets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>

// The live renderer's factory. DECLARED rather than included: A8 rule 2 keeps
// `vulkan.h` inside `backends/vulkan/`, and this file must build and link with
// no Vulkan on the machine at all - which is what OMK_VULKAN guards.
#if defined(OMK_VULKAN)
namespace omk {
Renderer* makeVulkanRenderer();
const char* vulkanDeviceName(Renderer*);
void  vulkanNeedExtensions(Renderer*, const char* const*, unsigned);
void* vulkanCreateInstance(Renderer*);
bool  vulkanAttachSurface(Renderer*, unsigned long long);
bool  vulkanPresent(Renderer*);
bool  vulkanPresentSurface(Renderer*, const Surface&);
}
#endif

namespace {

// A scripted-key entry meaning "type the --type string here", not a scan code.
constexpr int kTypeMarker = -1;

// ------------------------------------------------------ THE INTERFACE SOUNDS
//
// `docs/UI.md`: every screen names up to twelve sounds and the slots are
// POSITIONAL - **slot 0 is the selection move, slot 1 the confirm, slot 2 the
// screen opening**. The start menu's are 1, 2, 0, which the table resolves to
// `men002`, `men003`, `men001`. Both halves were already lifted; nothing was
// playing them, which is what a player reported.
//
// The samples are the engine's own 22050/16-bit mono or stereo and the device
// is running at the movie's 44100 float stereo, so they are converted here.
// That conversion is the FRONTEND's: `PORTING` A8 rule 3 forbids a dependency
// doing work a reference implementation ports, and there is no ported mixer to
// step on - `Sound_Init` hands a DirectSound primary buffer to the driver and
// DirectSound sums into it, so the summing never had a portable half
// (`src/audio/mixer.h`).
// A case-insensitive name compare - a scene function names a set mesh and the
// two spellings need not match in case.
bool sameName(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

// An angle difference on the SHORT arc, wrapped to (-180, 180]. A camera roll
// is stored 4096-per-turn and a small negative one reads as ~+359; the two are
// the same rotation standing still and a full turn apart once interpolated
// (CLAUDE.md 1, and `verify.py: camera roll`).
float shortArc(float deg) {
    while (deg > 180.0f)  deg -= 360.0f;
    while (deg <= -180.0f) deg += 360.0f;
    return deg;
}

std::vector<float> wavToDevice(std::span<const std::byte> file, int deviceRate) {
    const omk::audio::WavLoad w = omk::audio::loadWav(file);
    if (w.reject != omk::audio::WavReject::Ok || w.fmt.bits != 16 || !w.fmt.rate) return {};
    const auto* pcm = reinterpret_cast<const std::int16_t*>(file.data() + w.dataOffset);
    const std::size_t frames = w.dataBytes / (2u * (w.fmt.channels ? w.fmt.channels : 1));
    const double step = static_cast<double>(w.fmt.rate) / deviceRate;
    const std::size_t out = static_cast<std::size_t>(frames / step);
    std::vector<float> o;
    o.reserve(out * 2);
    for (std::size_t i = 0; i < out; ++i) {
        // Nearest-neighbour, deliberately: the alternative is a resampler,
        // and `PORTING` B5's argument applies - what a menu blip sounds like
        // through DirectSound's own resampler is the driver's, has no
        // reachable tier, and is not something to imitate precisely.
        const std::size_t src = static_cast<std::size_t>(i * step);
        if (src >= frames) break;
        const std::int16_t l = pcm[src * w.fmt.channels];
        const std::int16_t r = w.fmt.channels > 1 ? pcm[src * w.fmt.channels + 1] : l;
        o.push_back(l / 32768.0f);
        o.push_back(r / 32768.0f);
    }
    return o;
}

// THE DIALOGUE SUBTITLE.
//
// The BOX is the engine's, read out of `Dialog_TickUI` (0x0046A200) rather
// than chosen:
//
//     v3 = height << 6
//     Text_DrawBlock(32, 0, (width - 64) + 32, v3 / 480, text, params)
//     dword_6A52C4 = height - v3 / 480        <- where it is placed
//     dword_907961 = 0xFFFFFF                 <- the ink
//     off_4C71A8   = &unk_808080              <- the second colour
//
// `Text_DrawBlock(left, top, right, bottom, ...)`, so at 640x480 that is a
// block inset **32 pixels from each side** and **64 tall**, placed against the
// BOTTOM of the screen - which `Text_DrawBlock`'s own docstring says in as
// many words ("0x0041E040 measures once to place a subtitle against the bottom
// of the screen"). Both numbers scale with the display: `width - 64` and
// `height * 64 / 480`.
//
// **What is NOT recovered**: `Text_LayOutBlock` is not ported, so the WRAPPING
// here is this file's - break on spaces at the block's width - and the FACE is
// the port's default rather than whatever `dword_907969 = 32` selects. The
// reply menu has no recovered placement at all and is stacked under the line,
// with the selected row in the ink and the rest in the 0x808080 the engine
// also loads. So the BOX is read from the engine and the layout inside it is a
// reconstruction; they are labelled differently on purpose.
void wrapInto(const omk::TextLayout& lay, const std::string& t, int width,
              std::vector<std::string>& out, char face = 'J') {
    std::string ln, w;
    for (std::size_t k = 0; k <= t.size(); ++k) {
        const char c = k < t.size() ? t[k] : ' ';
        const bool brk = (c == '\n');
        if (c != ' ' && c != '\n' && c != '\r') { w.push_back(c); continue; }
        if (!w.empty()) {
            const std::string cand = ln.empty() ? w : ln + " " + w;
            if (lay.measure(cand) > width && !ln.empty()) { out.push_back(ln); ln = w; }
            else ln = cand;
            w.clear();
        }
        if (brk && !ln.empty()) { out.push_back(ln); ln.clear(); }
    }
    if (!ln.empty()) out.push_back(ln);
}

// THE SUBTITLE BOX, and both of its blends.
//
// `Dialog_TickUI` draws no box - it makes no call but `Text_DrawBlock`. The
// box is the TEXT RENDERER's: `sub_4400D0` (0x004400D0), called from
// `Game_Tick` as `sub_4400D0(0, dword_6A52C4, dword_6A52C0, height - 1, ...)`,
// submits one quad before the glyphs and switches on `off_4C71A8`, the second
// colour `Dialog_TickUI` sets:
//
//     off_4C71A8 == 0x80002040  ->  flags 4, top = a2 - 32      the REPLIES
//     off_4C71A8 == 0x00808080  ->  flags 2, top = a2 - 4       the LINE
//     x 18 .. width-18,  y (top - 8) .. height - 18
//     I2D_SubmitQuad(&v21, flags, 10)
//
// `I2D_SubmitQuad` copies 48 bytes - four vertices of (x, y, colour) plus the
// flag word - and `sub_480BD0` fills all four corners from the FIRST one
// unless flag 8 is set, which neither of these sets: both are a flat fill.
//
// The flags are the BLEND, through `sub_480AC0`, which sets D3D render states
// 19 (SRCBLEND) and 20 (DESTBLEND):
//
//     & 1   src 2 ONE,          dst 2 ONE            additive
//     & 2   src 1 ZERO,         dst 4 INVSRCCOLOR    dst *= (1 - src)
//     & 4   src 6 INVSRCALPHA,  dst 5 SRCALPHA       src*(1-a) + dst*a
//
// So the LINE's box is `dst * (1 - 0x808080/255)`, a 50% DARKENING - which
// over the black letterbox band is invisible, and is why a reader watching
// the original could not say whether a plain subtitle had a box. The REPLY
// box is 50% of the navy 0x002040. Each is gated on a driver-capability test
// (`sub_464730/40/50`) that can clear the bit and blank the colour; both are
// supported here.
enum class SubBox { None, Line, Replies };

void drawSubtitleBox(omk::Surface& fb, SubBox kind, int top, int dispW, int dispH) {
    if (kind == SubBox::None) return;
    // The 18, 8, 4 and 32 are LITERAL pixels in `sub_4400D0` - `v21 = 18`,
    // `(uint16_t)g_ScreenSize - 18`, `v6 - 8`, `a2 - 32` - not scaled by the
    // display, unlike the block's own `height * 64 / 480`. Scaling them put
    // the box a few rows lower than the engine does.
    // `top` is the TEXT's top, and the box is always `v6 - 8` from it: the
    // -4 (line) and -32 (replies) in `sub_4400D0` are how `v6` is derived
    // from `a2`, not a second offset on the box. Applying both put the reply
    // box 32 rows too high above its own text.
    const int x0 = 18, x1 = dispW - 18;
    const int y0 = top - 8;
    const int y1 = dispH - 18;
    (void)kind;
    if (x1 <= x0 || y1 <= y0) return;
    for (int y = y0 < 0 ? 0 : y0; y < y1 && y < fb.h; ++y) {
        for (int x = x0 < 0 ? 0 : x0; x < x1 && x < fb.w; ++x) {
            std::uint16_t& px = fb.px[static_cast<std::size_t>(y) *
                                      static_cast<std::size_t>(fb.w) +
                                      static_cast<std::size_t>(x)];
            int r = ((px >> 11) & 31) << 3, g = ((px >> 5) & 63) << 2, b = (px & 31) << 3;
            if (kind == SubBox::Line) {
                // dst *= (1 - src), src = 0x808080
                r = r * (255 - 0x80) / 255;
                g = g * (255 - 0x80) / 255;
                b = b * (255 - 0x80) / 255;
            } else {
                // src*(1-a) + dst*a, src = 0x002040, a = 0x80
                const int a = 0x80;
                r = (0x00 * (255 - a) + r * a) / 255;
                g = (0x20 * (255 - a) + g * a) / 255;
                b = (0x40 * (255 - a) + b * a) / 255;
            }
            px = static_cast<std::uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
}

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
                  int scroll = 0, int* overflowOut = nullptr) {
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
    const auto wrapRun = [&](const std::vector<omk::StyledChar>& run,
                             std::vector<std::vector<omk::StyledChar>>& out) {
        std::vector<omk::StyledChar> ln, word;
        for (std::size_t k = 0; k <= run.size(); ++k) {
            const bool end = k == run.size();
            const char c = end ? ' ' : run[k].ch;
            if (!end && c != ' ' && c != '\n' && c != '\r') { word.push_back(run[k]); continue; }
            if (!word.empty()) {
                std::vector<omk::StyledChar> cand = ln;
                if (!cand.empty()) { omk::StyledChar sp = word.front(); sp.ch = ' '; cand.push_back(sp); }
                cand.insert(cand.end(), word.begin(), word.end());
                if (!ln.empty() && lay.measure(cand) > width) { out.push_back(ln); ln = word; }
                else ln = cand;
                word.clear();
            }
            if (!end && c == '\n') { out.push_back(ln); ln.clear(); }
        }
        if (!ln.empty()) out.push_back(ln);
    };
    std::vector<std::vector<omk::StyledChar>> rows;
    std::vector<std::uint8_t> tone;
    if (!line.empty()) {
        std::vector<std::vector<omk::StyledChar>> tmp;
        wrapRun(omk::parseMarkup(line, face).run, tmp);
        for (auto& r : tmp) { rows.push_back(std::move(r)); tone.push_back(255); }
    }
    for (std::size_t k = 0; k < menu.size(); ++k) {
        std::vector<std::vector<omk::StyledChar>> tmp;
        wrapRun(omk::parseMarkup(menu[k], face).run, tmp);
        for (auto& r : tmp) {
            rows.push_back(std::move(r));
            tone.push_back(static_cast<int>(k) == selected ? 255 : 128);
        }
    }
    if (rows.empty()) return;

    const auto probe = omk::parseMarkup("Ag", face);
    const int lineH = lay.height(probe.run) + 2;
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
    const bool fixedBlock = menu.empty();
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
    const int clipTop = fixedBlock ? dispH - blockH - 4 : dispH - 18 - stackH;
    const int clipBot = fixedBlock ? clipTop + blockH : dispH - 18;
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

// cp1252 to UTF-8, for the CONSOLE only. The dialogue pool is cp1252 - which
// is what `FONTS/*.FNT` is indexed by, so the strings stay cp1252 everywhere
// that matters and this converts a copy on its way to a terminal.
std::string cp1252ToUtf8(const std::string& in) {
    static const unsigned short kHigh[32] = {   // 0x80..0x9F, the only holes
        0x20AC,0,0x201A,0x0192,0x201E,0x2026,0x2020,0x2021,0x02C6,0x2030,
        0x0160,0x2039,0x0152,0,0x017D,0,0,0x2018,0x2019,0x201C,0x201D,0x2022,
        0x2013,0x2014,0x02DC,0x2122,0x0161,0x203A,0x0153,0,0x017E,0x0178};
    std::string o;
    for (unsigned char c : in) {
        unsigned cp = c;
        if (c >= 0x80 && c <= 0x9F) { cp = kHigh[c - 0x80]; if (!cp) continue; }
        if (cp < 0x80) { o.push_back(static_cast<char>(cp)); }
        else if (cp < 0x800) {
            o.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            o.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return o;
}

// Raw int16 PCM to the device's interleaved float, the same nearest-neighbour
// step `wavToDevice` uses and for the same reason (B5: a resampler's sound is
// the driver's and has no reachable tier). This one exists because a dialogue
// line arrives already DECODED - out of a `.3DM`'s ADPCM block - rather than
// as a `.wav` file, so there is no header to read.
std::vector<float> resampleToDevice(const std::vector<std::int16_t>& pcm,
                                    int channels, int rate, int deviceRate) {
    if (pcm.empty() || channels <= 0 || rate <= 0) return {};
    const std::size_t frames = pcm.size() / static_cast<std::size_t>(channels);
    const double step = static_cast<double>(rate) / deviceRate;
    std::vector<float> o;
    o.reserve(static_cast<std::size_t>(frames / step) * 2);
    for (std::size_t i = 0;; ++i) {
        const std::size_t src = static_cast<std::size_t>(i * step);
        if (src >= frames) break;
        const std::int16_t l = pcm[src * static_cast<std::size_t>(channels)];
        const std::int16_t r = channels > 1
            ? pcm[src * static_cast<std::size_t>(channels) + 1] : l;
        o.push_back(l / 32768.0f);
        o.push_back(r / 32768.0f);
    }
    return o;
}

// SDL scancode -> the engine's own DIK code. The interface's own keys come
// first, because `Input::poll` matches these against the live binding tables
// (docs/UI.md 3c: the four arrows, ENTER, SPACE and TAB are the whole of the
// menu's vocabulary). The letters and brackets after them are for the SCENE
// VIEWER below and reach no binding table at all - an unbound code produces no
// bit, so adding them cannot change what the interface does. They are the real
// set-1 scan codes rather than invented ones, so this map stays one thing.
const std::map<int, int>& keymap() {
    static const std::map<int, int> m = {
        {SDL_SCANCODE_UP,     0xC8}, {SDL_SCANCODE_DOWN,  0xD0},
        {SDL_SCANCODE_LEFT,   0xCB}, {SDL_SCANCODE_RIGHT, 0xCD},
        {SDL_SCANCODE_RETURN, 0x1C}, {SDL_SCANCODE_SPACE, 0x39},
        {SDL_SCANCODE_TAB,    0x0F}, {SDL_SCANCODE_ESCAPE,0x01},
        // `Input_Poll` compares against scan code 56 - DIK_LMENU, the key
        // that skips ALL three movies (docs/BOOT.md 2, asserted by
        // `verify.py: boot sequence`). ALT is the engine's own choice, not
        // this frontend's.
        {SDL_SCANCODE_LALT,   0x38},
        // the viewer's: WASD to fly, QE up and down, [ ] to step the set's own
        // cameras, L to cycle the baked light, P to print the camera
        {SDL_SCANCODE_W, 0x11}, {SDL_SCANCODE_A, 0x1E}, {SDL_SCANCODE_S, 0x1F},
        {SDL_SCANCODE_D, 0x20}, {SDL_SCANCODE_Q, 0x10}, {SDL_SCANCODE_E, 0x12},
        {SDL_SCANCODE_L, 0x26}, {SDL_SCANCODE_P, 0x19},
        {SDL_SCANCODE_LEFTBRACKET, 0x1A}, {SDL_SCANCODE_RIGHTBRACKET, 0x1B},
        {SDL_SCANCODE_LSHIFT, 0x2A}, {SDL_SCANCODE_V, 0x2F},
        {SDL_SCANCODE_M, 0x32},
        // ...and the two ADVENTURE bindings that had no key at all, which is
        // why neither could be pressed. `tables/key_bindings.json` group 0:
        // "Courir" is action 11, bit 0x800, keyboard **54** = DIK_RSHIFT -
        // the RIGHT shift, not the left, which was mapped (0x2A) and reaches
        // no binding; and "Pas de cote / Demi-tour" is action 10, bit 0x400,
        // keyboard **157** = DIK_RCONTROL. Both are the engine's own defaults.
        {SDL_SCANCODE_RSHIFT, 0x36}, {SDL_SCANCODE_RCTRL, 0x9D},
    };
    return m;
}

class SdlFrontend : public omk::Frontend {
public:
    bool open(int w, int h, const std::string& title) override {
        w_ = w; h_ = h;
#if defined(OMK_SDL3)
        if (!SDL_Init(SDL_INIT_VIDEO)) return false;
        win_ = SDL_CreateWindow(title.c_str(), w, h, 0);
        if (!win_) return false;
        ren_ = SDL_CreateRenderer(win_, nullptr);
#else
        if (SDL_Init(SDL_INIT_VIDEO) != 0) return false;
        win_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED, w, h, 0);
        if (!win_) return false;
        ren_ = SDL_CreateRenderer(win_, -1, 0);
#endif
        if (!ren_) return false;
        // RGB565 all the way to the window: A3 fixes the reference framebuffer
        // at 16 bits, and uploading 565 directly is what keeps what is shown
        // identical to what a capture is diffed against. Asking SDL for 888
        // here would expand every pixel by the HOST's rule, which A3 measured
        // as not being bit replication.
        tex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGB565,
                                 SDL_TEXTUREACCESS_STREAMING, w, h);
        return tex_ != nullptr;
    }

    bool pump(omk::HostInput& out) override {
        out.text.clear();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
#if defined(OMK_SDL3)
            if (e.type == SDL_EVENT_QUIT) out.quit = true;
            else if (e.type == SDL_EVENT_TEXT_INPUT) out.text += e.text.text;
#else
            if (e.type == SDL_QUIT) out.quit = true;
            else if (e.type == SDL_TEXTINPUT) out.text += e.text.text;
#endif
        }
        out.held.clear();
        int n = 0;
#if defined(OMK_SDL3)
        const bool* ks = SDL_GetKeyboardState(&n);
#else
        const Uint8* ks = SDL_GetKeyboardState(&n);
#endif
        for (const auto& [sc, dik] : keymap())
            if (sc < n && ks[sc]) out.held.insert(dik);
        return !out.quit;
    }

    void present(const omk::Surface& fb) override {
        // One upload of the finished framebuffer. No filtering, no scaling:
        // the window is the framebuffer's own size.
        SDL_UpdateTexture(tex_, nullptr, fb.px.data(),
                          static_cast<int>(fb.w * sizeof(std::uint16_t)));
        SDL_RenderClear(ren_);
#if defined(OMK_SDL3)
        SDL_RenderTexture(ren_, tex_, nullptr, nullptr);
#else
        SDL_RenderCopy(ren_, tex_, nullptr, nullptr);
#endif
        SDL_RenderPresent(ren_);
    }

    // ---- THE MIXER -------------------------------------------------
    //
    // SDL's queue is a FIFO with no mixing, so a queued blip plays AFTER
    // whatever is already there - which is why the first version flushed the
    // device before each one. That is fine while a blip is the only sound and
    // wrong the moment music plays underneath: the first keypress would cut
    // the track. So the device is driven by a CALLBACK that sums two things:
    //
    //   * the STREAM - the movie soundtrack, or the music - a ring buffer fed
    //     as it is decoded;
    //   * a few ONE-SHOT voices for the interface sounds.
    //
    // `PORTING` A8 rule 3 is satisfied because there is no ported mixer to
    // step on: `Sound_Init` hands DirectSound a 22050/16/stereo primary buffer
    // and DirectSound does the summing, so that half never had a portable
    // counterpart (`src/audio/mixer.h`). This is the device's job, done here.
    static void feed(void* user, Uint8* out, int len) {
        auto* self = static_cast<SdlFrontend*>(user);
        auto* dst = reinterpret_cast<float*>(out);
        const std::size_t n = static_cast<std::size_t>(len) / sizeof(float);
        std::lock_guard<std::mutex> lk(self->amx_);
        for (std::size_t i = 0; i < n; ++i) {
            float v = 0.0f;
            if (self->sHead_ < self->stream_.size()) v += self->stream_[self->sHead_++];
            for (auto& one : self->shots_) {
                if (one.pos >= one.pcm.size()) {
                    if (!one.loop || one.pcm.empty()) continue;
                    one.pos = 0;                  // a looping shot wraps
                }
                v += one.pcm[one.pos++] * one.gain;
            }
            dst[i] = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
        }
        // Reclaim the consumed head rather than growing for ever.
        if (self->sHead_ > (1u << 20)) {
            self->stream_.erase(self->stream_.begin(),
                                self->stream_.begin() + static_cast<std::ptrdiff_t>(self->sHead_));
            self->sHead_ = 0;
        }
        std::erase_if(self->shots_, [](const Shot& o) {
            return !o.loop && o.pos >= o.pcm.size();   // a loop ends only on stopSound
        });
    }

    bool openAudio(int rate, int channels) override {
        // One device, not one per movie: reopening it per file was how the
        // previous movie's audio kept playing under the next.
#if defined(OMK_SDL3)
        if (astream_) return true;
#else
        if (adev_) return true;
#endif
        arate_ = rate; achan_ = channels;
        SDL_AudioSpec want{};
        want.freq = rate;
        want.format = AUDIO_F32;
        want.channels = static_cast<Uint8>(channels);
        want.samples = 1024;
        want.callback = &SdlFrontend::feed;   // MIXED, not queued
        want.userdata = this;
#if defined(OMK_SDL3)
        astream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                             &want, nullptr, nullptr);
        if (astream_) SDL_ResumeAudioStreamDevice(astream_);
        return astream_ != nullptr;
#else
        if (SDL_Init(SDL_INIT_AUDIO) != 0) return false;
        adev_ = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
        if (adev_) SDL_PauseAudioDevice(adev_, 0);
        return adev_ != 0;
#endif
    }

    void queueAudio(std::span<const float> s) override {
        if (s.empty()) return;
        std::lock_guard<std::mutex> lk(amx_);
        stream_.insert(stream_.end(), s.begin(), s.end());
    }

    int playSound(std::span<const float> s, bool loop = false,
                  float gain = 1.0f) override {
        if (s.empty()) return -1;
        std::lock_guard<std::mutex> lk(amx_);
        // A cap, because a held key would otherwise stack voices without end.
        if (shots_.size() >= 8) shots_.erase(shots_.begin());
        const int id = nextShot_++;
        shots_.push_back({std::vector<float>(s.begin(), s.end()), 0, id, loop, gain});
        return id;
    }

    void stopSound(int handle) override {
        if (handle < 0) return;
        std::lock_guard<std::mutex> lk(amx_);
        std::erase_if(shots_, [handle](const Shot& o) { return o.id == handle; });
    }

    void flushAudio() override {
        std::lock_guard<std::mutex> lk(amx_);
        stream_.clear(); sHead_ = 0;
    }

    double queuedSeconds() override {
        std::lock_guard<std::mutex> lk(amx_);
        const double per = arate_ > 0 ? 1.0 / (arate_ * achan_) : 0.0;
        return static_cast<double>(stream_.size() - sHead_) * per;
    }

    void close() override {
#if defined(OMK_SDL3)
        if (astream_) SDL_DestroyAudioStream(astream_);
#else
        if (adev_) SDL_CloseAudioDevice(adev_);
#endif
        if (tex_) SDL_DestroyTexture(tex_);
        if (ren_) SDL_DestroyRenderer(ren_);
        if (win_) SDL_DestroyWindow(win_);
        SDL_Quit();
    }

private:
    SDL_Window*   win_ = nullptr;
    SDL_Renderer* ren_ = nullptr;
    SDL_Texture*  tex_ = nullptr;
#if defined(OMK_SDL3)
    SDL_AudioStream* astream_ = nullptr;
#else
    SDL_AudioDeviceID adev_ = 0;
#endif
    int w_ = 0, h_ = 0;
    int arate_ = 0, achan_ = 2;

    // omk-play 72: a LOOPING shot wraps instead of ending. `Script_PlaySound`
    // carries a loop flag the port recorded and never honoured, so an ambience
    // was re-fired by its program every cycle - wav 23 started 33 times in 521
    // frames, a 1.76 s sample overlapping itself three deep. That restart is
    // what a reader heard as "the loop feels unnatural".
    struct Shot { std::vector<float> pcm; std::size_t pos; int id; bool loop = false;
                  float gain = 1.0f; };
    int                 nextShot_ = 1;
    std::mutex          amx_;
    std::vector<float>  stream_;
    std::size_t         sHead_ = 0;
    std::vector<Shot>   shots_;
};


// ----------------------------------------------------- THE SCENE VIEWER
//
//     omk-play <gamedata> <tables> --scene Aapkayl [--cam N] [--eye x,y,z]
//                            [--at x,y,z] [--fov F] [--letterbox] [--vulkan]
//
// **This is an INSTRUMENT, not a slice of the port**, and the distinction is
// the one `docs/PORTING.md` B6 makes about the rasterizer it drives: the
// engine has no software 3D rasterizer, so there is nothing here to
// transcribe. What this adds is not a ported behaviour but the ability to
// LOOK, which the tree did not have - `src/o3de/raster.*` only ever wrote
// `.bin` files for `verify.py`, so every claim about the 3D path was a number
// only a checker could judge.
//
// That is a real gap and it is the reason this exists. CLAUDE.md 1 has a rule
// about it - *a suite that only compares this repo to itself cannot see a
// wrong reading applied consistently* - and the two worked examples in that
// section, the dialogue staging and the Anekbah panels, were both caught by
// somebody watching rather than by anything here. A frame on screen is the
// cheapest instrument in the tree for that class of error, and it costs five
// seconds instead of a capture rig.
//
// What it draws is exactly what `verify.py: engine silhouette` measures: the
// same `drawGeometry`, the same batch order, the same blend modes, into the
// same RGB565 surface the window uploads unmodified. So a fault you can see
// here is a fault in the thing the checks check, not in a second renderer
// written to look at.
//
// The letterbox is the default because the game's camera mode is letterboxed -
// 640x352 inside 480, 1.818:1, which `traces/frames/dlg402-*.png` show and
// which the vertical fov follows from (`tanv = tanh / (W/H)`). `--full` opens
// it to the whole framebuffer, which is a different vertical fov and therefore
// a different picture; it is for looking around, not for comparing.
struct ViewCam {
    float eye[3] = {0, 0, 0};
    float yaw = 0, pitch = 0;      // radians; yaw about the world Y
    float fov = 60.0f;

    // The game's Y points DOWN, so a positive pitch must LOWER the forward
    // vector's y to look up. Getting that backwards is invisible standing
    // still and inverts the mouse the moment anything moves, which is the
    // shape of error CLAUDE.md 1 calls "invisible at rest".
    void forward(float f[3]) const {
        f[0] = std::sin(yaw) * std::cos(pitch);
        f[1] = -std::sin(pitch);
        f[2] = std::cos(yaw) * std::cos(pitch);
    }
    // The strafe axis is taken the way `basisOf` takes it - s = f x (0,-1,0) -
    // rather than re-derived, so flying sideways moves the way the picture
    // says it should.
    void right(float r[3]) const {
        float f[3]; forward(f);
        const float up[3] = {0.0f, -1.0f, 0.0f};
        r[0] = f[1] * up[2] - f[2] * up[1];
        r[1] = f[2] * up[0] - f[0] * up[2];
        r[2] = f[0] * up[1] - f[1] * up[0];
        const float m = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
        if (m > 0) { r[0] /= m; r[1] /= m; r[2] /= m; }
    }
    void aim(float eye_at[3]) const {
        float f[3]; forward(f);
        for (int k = 0; k < 3; ++k) eye_at[k] = eye[k] + f[k] * 100.0f;
    }
    void lookAt(const float e[3], const float t[3]) {
        for (int k = 0; k < 3; ++k) eye[k] = e[k];
        float d[3] = {t[0] - e[0], t[1] - e[1], t[2] - e[2]};
        const float m = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (m <= 0) return;
        for (int k = 0; k < 3; ++k) d[k] /= m;
        yaw = std::atan2(d[0], d[2]);
        pitch = std::asin(std::clamp(-d[1], -1.0f, 1.0f));
    }
};

// The baked per-vertex light, cycled the way both web viewers' `lights` button
// cycles it (CLAUDE.md 5). COLOUR is what the game draws and the only mode to
// compare against a screenshot; GREY is THIS REPO'S OWN BUG before 2026-08-29,
// the green byte read as a brightness, kept so the two can be seen on one
// frame; OFF is full bright, for looking at the textures alone.
enum class Light { Colour, Grey, Off };

omk::Geometry relight(const omk::Geometry& src, Light mode) {
    if (mode == Light::Colour) return src;
    omk::Geometry g = src;
    for (auto& c : g.corners) {
        if (mode == Light::Off) { c.r = c.g = c.b = 1.0f; continue; }
        const float y = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
        c.r = c.g = c.b = y;
    }
    return g;
}

int sceneViewer(const std::string& fr, const std::string& setName,
                int camIndex, const float* eyeArg, const float* atArg,
                float fovArg, bool letterbox, int frameBudget,
                const std::string& dump, bool startVulkan, bool noDelay) {
    // The set. A bare name is looked up in MESHES/DECORS, which is where the
    // decor sets live; anything with a slash is taken as given, so a character
    // model or another folder can be opened without a special case.
    std::string path = setName;
    if (path.find('/') == std::string::npos) {
        if (path.size() < 4 || path.substr(path.size() - 4) != ".3DO")
            path += ".3DO";
        path = fr + "/MESHES/DECORS/" + path;
    }
    const auto d = omk::DataFs::readPath(path);
    if (d.empty()) { std::fprintf(stderr, "cannot read %s\n", path.c_str()); return 1; }
    std::string tpath = path.substr(0, path.rfind('.')) + ".3DT";
    const auto tres = omk::DataFs::readPath(tpath);
    const auto geo = omk::buildGeometry(d, omk::DrawFilter::Engine);
    const auto tex = tres.empty() ? std::vector<omk::Texture>{}
                                  : omk::textures(d, tres);
    if (geo.corners.empty()) {
        std::fprintf(stderr, "%s has no drawable geometry\n", path.c_str());
        return 1;
    }

    // The set's OWN cameras, out of the .3DO's 52-byte records - name, eye,
    // target and fov. Stepping these with [ and ] is what makes the viewer a
    // check rather than a toy: they are the framings the authors chose, so a
    // set that looks right from all of them is right where it was meant to be
    // looked at.
    std::vector<omk::Camera> cams;
    if (const auto h = omk::readHeader(d)) cams = omk::readCameras(d, *h);

    ViewCam vc;
    vc.fov = fovArg > 0 ? fovArg : 60.0f;
    int ci = camIndex;
    if (eyeArg && atArg) {
        vc.lookAt(eyeArg, atArg);
        ci = -1;
    } else if (!cams.empty()) {
        if (ci < 0 || ci >= static_cast<int>(cams.size())) ci = 0;
        vc.lookAt(cams[ci].pos, cams[ci].target);
        if (fovArg <= 0 && cams[ci].fov > 0) vc.fov = cams[ci].fov;
    } else {
        // No camera anywhere: stand off the geometry's own centroid, the way
        // `run_anekbah` derives its framing, so the set is at least in shot.
        double c[3] = {0, 0, 0};
        for (const auto& k : geo.corners) { c[0] += k.x; c[1] += k.y; c[2] += k.z; }
        const double n = static_cast<double>(geo.corners.size());
        const float t[3] = {static_cast<float>(c[0] / n), static_cast<float>(c[1] / n),
                            static_cast<float>(c[2] / n)};
        const float e[3] = {t[0], t[1] - 100.0f, t[2] - 400.0f};
        vc.lookAt(e, t);
    }

    std::printf("%s: %zu corners, %zu batches, %zu textures, %zu cameras\n",
                path.c_str(), geo.corners.size(), geo.batches.size(),
                tex.size(), cams.size());
    for (std::size_t i = 0; i < cams.size(); ++i)
        std::printf("  cam %2zu  %-20s fov %.1f\n", i, cams[i].name, cams[i].fov);
    std::printf(
        "\nW/S fly, A/D strafe, Q/E up-down, SHIFT faster, arrows look,\n"
        "[ ] step the set's cameras, L cycles the light "
        "(colour = what the game draws, grey = this repo's old bug, off),\n"
        "V swaps the software reference for the Vulkan backend, "
        "P prints the camera, ESC quits.\n\n");

    SdlFrontend front;
    SDL_Window* vkWin = nullptr;
    bool direct = false;
    omk::Renderer* vkRen = nullptr;   // owns the swapchain, whoever is drawing

    const int PW = 640, PH = letterbox ? 352 : 480;
    const int PY = (480 - PH) / 2;

    // ---- the renderers, both behind `PORTING` A2's boundary.
    //
    // The viewer draws through `Renderer` rather than calling `drawGeometry`,
    // so what is on screen is the same path `verify.py` measures AND the same
    // path the GPU backend implements. `V` swaps them in place, which is the
    // most useful thing this window can do: the reference and the live one,
    // same camera, same frame, a keypress apart.
    omk::SoftwareRenderer sw;
    sw.init(PW, PH);
    omk::Renderer* live = nullptr;
#if defined(OMK_VULKAN)
    live = omk::makeVulkanRenderer();
    if (live && !live->init(PW, PH)) { delete live; live = nullptr; }
    if (live) {
        live->setTextures(tex);
        std::printf("vulkan: %s  (V swaps the backend)\n",
                    omk::vulkanDeviceName(live));
    } else {
        std::printf("vulkan: no device - software only\n");
    }
#else
    std::printf("built without Vulkan - software only "
                "(`make vulkan` needs pkg-config vulkan + glslc)\n");
#endif
    sw.setTextures(tex);
    omk::Renderer* ren = (startVulkan && live) ? live
                                              : static_cast<omk::Renderer*>(&sw);
    std::printf("backend: %s\n", ren->name());

    // ---- DIRECT PRESENTATION.
    //
    // A window carrying a Vulkan surface cannot also carry an `SDL_Renderer`,
    // so this is a WINDOW MODE rather than a runtime toggle: `--vulkan` opens
    // a Vulkan window and the frame never touches the CPU, while without it the
    // window keeps its texture-upload path and `V` can still swap backends
    // through `readback()` for comparison.
    //
    // The order below is Vulkan's, not a preference: the instance needs the
    // window system's extensions, the surface needs the instance, and the
    // device's queue choice needs the surface.
#if defined(OMK_VULKAN)
    if (startVulkan) {
        if (SDL_Init(SDL_INIT_VIDEO) == 0) {
            vkWin = SDL_CreateWindow("OMK Engine - scene viewer (vulkan)",
                                     SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     640, 480, SDL_WINDOW_VULKAN);
        }
        if (vkWin) {
            unsigned n = 0;
            SDL_Vulkan_GetInstanceExtensions(vkWin, &n, nullptr);
            std::vector<const char*> ext(n);
            SDL_Vulkan_GetInstanceExtensions(vkWin, &n, ext.data());
            omk::Renderer* vr = omk::makeVulkanRenderer();
            omk::vulkanNeedExtensions(vr, ext.data(), n);
            void* inst = omk::vulkanCreateInstance(vr);
            // Value-initialised rather than VK_NULL_HANDLE: this file
            // includes SDL_vulkan.h, which declares the handle types but not
            // Vulkan's constants - and A8 rule 2 keeps vulkan.h out of here.
            VkSurfaceKHR surf{};
            if (inst && SDL_Vulkan_CreateSurface(vkWin, static_cast<VkInstance>(inst), &surf)
                && omk::vulkanAttachSurface(vr, reinterpret_cast<unsigned long long>(surf))
                && vr->init(PW, PH)) {
                delete live;
                live = vr;
                live->setTextures(tex);
                direct = true;
                vkRen = vr;   // the one that owns the swapchain
                std::printf("vulkan: PRESENTING DIRECTLY - no readback, "
                            "no texture upload\n");
            } else {
                delete vr;
                SDL_DestroyWindow(vkWin); vkWin = nullptr;
                std::printf("vulkan: direct presentation unavailable, "
                            "falling back to the upload path\n");
            }
        }
    }
#endif
    if (!direct && !front.open(640, 480, "OMK Engine - scene viewer (software)")) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1;
    }
    if (direct) ren = live;

    // The mirror, if this set has one (mesh flag 0x100000 - ASSETS 4c).
    const omk::MirrorPlane mp = omk::mirrorPlane(d);
    bool mirrorOn = true, mirrorWas = false;
    if (mp.found)
        std::printf("mirror: mesh %d, plane point (%.0f %.0f %.0f) "
                    "normal (%.2f %.2f %.2f) - M toggles the pass\n",
                    mp.mesh, mp.point[0], mp.point[1], mp.point[2],
                    mp.normal[0], mp.normal[1], mp.normal[2]);
    else
        std::printf("this set has no mirror mesh\n");

    Light light = Light::Colour;
    omk::Geometry lit = geo;
    omk::Surface fb(640, 480, 0);
    std::set<int> was;
    long frames = 0;

    for (;;) {
        omk::HostInput host;
        if (!front.pump(host)) break;
        if (host.held.count(0x01)) break;                       // ESC

        // Held keys fly; the ones that CHANGE something are edge-triggered, so
        // holding [ does not run through every camera in one frame. This is
        // the viewer's own filter and not `Game_Frame`'s - nothing here goes
        // near the binding tables.
        const auto down = [&](int dik) { return host.held.count(dik) != 0; };
        const auto hit  = [&](int dik) { return down(dik) && !was.count(dik); };

        const float step = down(0x2A) ? 40.0f : 8.0f;           // LSHIFT
        float f[3], r[3];
        vc.forward(f); vc.right(r);
        for (int k = 0; k < 3; ++k) {
            if (down(0x11)) vc.eye[k] += f[k] * step;           // W
            if (down(0x1F)) vc.eye[k] -= f[k] * step;           // S
            if (down(0x20)) vc.eye[k] += r[k] * step;           // D
            if (down(0x1E)) vc.eye[k] -= r[k] * step;           // A
        }
        if (down(0x10)) vc.eye[1] -= step;                      // Q: up (Y down)
        if (down(0x12)) vc.eye[1] += step;                      // E: down
        if (down(0xCB)) vc.yaw   -= 0.03f;                      // left
        if (down(0xCD)) vc.yaw   += 0.03f;                      // right
        if (down(0xC8)) vc.pitch += 0.02f;                      // up
        if (down(0xD0)) vc.pitch -= 0.02f;                      // down
        vc.pitch = std::clamp(vc.pitch, -1.5f, 1.5f);

        if (!cams.empty() && (hit(0x1A) || hit(0x1B))) {        // [ ]
            const int n = static_cast<int>(cams.size());
            ci = ((ci < 0 ? 0 : ci) + (hit(0x1B) ? 1 : n - 1)) % n;
            vc.lookAt(cams[ci].pos, cams[ci].target);
            if (cams[ci].fov > 0) vc.fov = cams[ci].fov;
            std::printf("cam %d  %s  fov %.1f\n", ci, cams[ci].name, vc.fov);
        }
        if (hit(0x32)) {                                        // M
            mirrorOn = !mirrorOn;
            std::printf("mirror pass %s\n", mirrorOn ? "on" : "off");
        }
        if (hit(0x2F) && live) {                                // V
            ren = (ren == &sw) ? live : static_cast<omk::Renderer*>(&sw);
            std::printf("backend: %s\n", ren->name());
        }
        if (hit(0x26)) {                                        // L
            light = light == Light::Colour ? Light::Grey
                  : light == Light::Grey   ? Light::Off : Light::Colour;
            lit = relight(geo, light);
            std::printf("light: %s\n",
                        light == Light::Colour ? "colour (what the game draws)"
                      : light == Light::Grey   ? "grey (this repo's own pre-2026-08-29 bug)"
                                               : "off (full bright)");
        }
        if (hit(0x19)) {                                        // P
            float at[3]; vc.aim(at);
            std::printf("--eye %.0f,%.0f,%.0f --at %.0f,%.0f,%.0f --fov %.1f\n",
                        vc.eye[0], vc.eye[1], vc.eye[2], at[0], at[1], at[2], vc.fov);
        }
        was = host.held;

        omk::RCamera cam;
        for (int k = 0; k < 3; ++k) cam.eye[k] = vc.eye[k];
        vc.aim(cam.at);
        cam.hfovDeg = vc.fov;
        cam.w = PW; cam.h = PH;

        // The submissions, in `buildGeometry`'s order - which is the engine's
        // own (`Render_FlushBuckets` ascending), not this file's. A backend
        // receives them and never reorders.
        omk::View view; view.cam = cam;
        // `drawWithMirror` submits in `buildGeometry`'s order - the engine's
        // own - and adds the reflection pass when the set has a mirror mesh
        // and the camera is in front of it. With no mirror it is one pass and
        // one readback, exactly as before.
        const auto ms = omk::drawWithMirror(*ren, lit, tex, view,
                                            mirrorOn ? mp : omk::MirrorPlane{});
        if (ms.active != mirrorWas) {
            std::printf("mirror %s%s (%ld px, camera %.0f in front)\n",
                        ms.active ? "REFLECTING" : "not in view",
                        ms.native ? " [gpu stencil]" : "",
                        ms.maskPixels, ms.distance);
            mirrorWas = ms.active;
        }
        // The readback is the whole cost direct presentation removes, so it
        // must not happen when nothing needs it. It IS needed when the mirror
        // pass composited (that frame lives only on the CPU) and always in the
        // upload path.
        static const omk::Surface kNone(1, 1, 0);
        // A frame that stayed on the GPU needs no readback at all - which is
        // the whole point of the native mirror pass, and of direct
        // presentation. The readback is for the upload path, for a software
        // frame, and for a mirror the CPU had to composite.
        const bool onGpu = direct && ren == vkRen && (!ms.active || ms.native);
        const omk::Surface& pic = onGpu ? kNone : ren->readback();

        // The letterbox is a PLACEMENT, not a blit: the picture is copied into
        // the framebuffer's middle rows and the bands stay black, which is what
        // the captures show. Nothing is scaled, filtered or blended, so what
        // the window uploads is what `drawGeometry` produced.
#if defined(OMK_VULKAN)
        if (direct) {
            // The finished frame is already in the GPU's colour attachment,
            // so it goes straight to the swapchain - no readback, no upload,
            // no SDL texture. The exception is a frame the MIRROR pass
            // composited, which exists only as a CPU `Surface` and has to be
            // uploaded; that is the one frame that still round-trips, and a
            // GPU composite is what would remove it.
            // The SWAPCHAIN belongs to the Vulkan renderer, but the frame
            // may have come from the software one - `V` still swaps them here.
            // A frame it did not draw itself (software, or one the mirror pass
            // composited on the CPU) is uploaded; its own is presented with no
            // CPU involvement at all. Presenting through `ren` instead froze
            // the window the moment `V` was pressed, because the cast to the
            // Vulkan renderer simply failed and nothing was presented.
            if (onGpu) omk::vulkanPresent(vkRen);
            else       omk::vulkanPresentSurface(vkRen, pic);
        } else
#endif
        {
            std::fill(fb.px.begin(), fb.px.end(), std::uint16_t(0));
            for (int y = 0; y < PH; ++y)
                std::copy_n(pic.px.begin() + static_cast<std::size_t>(y) * PW, PW,
                            fb.px.begin() + static_cast<std::size_t>(y + PY) * 640);
            front.present(fb);
        }
        std::fflush(stdout);   // so a crash keeps its log; one cost nothing else
        ++frames;
        if (frameBudget && frames >= frameBudget) break;
        if (!noDelay) SDL_Delay(16);   // --nodelay: for measuring, not for playing
    }
    std::printf("%ld frames presented (%s)%s\n", frames, ren->name(),
                direct ? ", presented directly" : "");
    // The framebuffer the WINDOW was shown, raw LE RGB565 - the same dump the
    // interface path writes, so a shot from here can be read by the same
    // Python and laid beside a capture. `--frames 1 --dump` is also how this
    // is smoke-tested without a person pressing ESC.
    if (!dump.empty()) {
        // Direct presentation never fills `fb` - that is the point - so a dump
        // asks the renderer for the frame explicitly. It reads the colour
        // attachment rather than the swapchain, so it verifies everything up
        // to the blit and not the blit itself; the blit is a person's job.
        if (direct) {
            const omk::Surface& last = ren->readback();
            std::fill(fb.px.begin(), fb.px.end(), std::uint16_t(0));
            for (int y = 0; y < PH && y < last.h; ++y)
                std::copy_n(last.px.begin() + static_cast<std::size_t>(y) * PW, PW,
                            fb.px.begin() + static_cast<std::size_t>(y + PY) * 640);
        }
        std::ofstream o(dump, std::ios::binary);
        for (auto v : fb.px) {
            const char b2[2] = {static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)};
            o.write(b2, 2);
        }
        std::printf("wrote %s (%dx%d RGB565)\n", dump.c_str(), 640, 480);
    }
    if (!direct) front.close();
    // The teardown comes LAST, after the dump: `ren` points at `live`, and
    // freeing the renderer before reading a frame out of it is a
    // use-after-free that crashed 4 runs of 4. The renderer owns the Vulkan
    // surface, so it must still go before the window that surface was made
    // from.
    delete live;
    live = nullptr;
    if (vkWin) { SDL_DestroyWindow(vkWin); SDL_Quit(); }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    // `--help` anywhere on the line, and the same text the argument check
    // prints. Kept in one place so a flag cannot be added without a line here.
    const auto usage = [](std::FILE* to) {
        std::fprintf(to,
"omk-play - the OMK engine's viewer\n"
"\n"
"  omk-play <gamedata> [tables dir] [screen] [options]      play\n"
"  omk-play <gamedata> [tables dir] --save F --area N       start in a street\n"
"  omk-play <gamedata> [tables dir] --scene <set> [options] look at one set\n"
"\n"
"<gamedata> is the shipped tree. <tables dir> is OPTIONAL - the lifted\n"
"tables (tables/); without them the run still works but tells you less, and\n"
"the interface screens cannot be drawn. `tables/vm_opcodes.json` is the one\n"
"exception: the world scripts cannot be decoded without it.\n"
"[screen] is the interface screen to open, default 29, the start menu.\n"
"\n"
"RUNNING\n"
"  --speed X        scale the frame delta - the engine's own trick. Its\n"
"                   `dword_4E972C` has 1.0, 0.5, 0.1 and 2.0, so 2 is the\n"
"                   game's double speed and 0.5 its slow motion. Capped at 3.\n"
"  --nofmv          skip the three FLIS movies (~143 s of them)\n"
"  --frames N       run N frames and exit - headless, no pacing\n"
"  --dump out.bin   write the last framebuffer as RGB565\n"
"  --snaps <dir>    write a framebuffer every 30 frames after the hand-over\n"
"  --fps            report the rate and the worst frame once a second\n"
"\n"
"STARTING IN A STREET (street life)\n"
"  --save FILE      load a save instead of IAM/START, so there is no intro\n"
"  --area N         the area to stand in\n"
"  --address A      the ADDRESSES record to stand on\n"
"  --stand x,y,z[,facing]   an explicit spot instead of an address\n"
"  --density 0..4   how much crowd - the options menu\'s own row 6\n"
"  --no-crowd       no pedestrians at all\n"
"  --scene-chunk N  run SCENE chunk N's startup script over the area, the\n"
"                   way `scene.load` does. A street start jumps straight to\n"
"                   an area, so the chunk that would have been loaded on the\n"
"                   way in never is - and with it go the beats that SHOW the\n"
"                   area's props. AREA 222 wants SCENE 55, whose script ends\n"
"                   in `object.show 162`, the Impasse's rings: without it\n"
"                   there is nothing in the world to take\n"
"  --bank-reject    DEBUG: every bank is REFUSED as a full list refuses it,\n"
"                   the object staying in his hand. Without it, the original's\n"
"                   Inventory_Insert: a row, a merge, or for money and rings\n"
"                   Object_ApplyEffect - the count goes up, no row\n"
"  --give a,b,c     object ids into the carried list - a HARNESS write, not\n"
"                   `inventory.add`. A list, because a new game ships two\n"
"                   objects and the flows worth driving want a bagful: row\n"
"                   scrolling needs more than the nine row widgets, and\n"
"                   `Utiliser sur` needs a recipe pair (18,7 -> 33)\n"
"  --sneak          open the SNEAK as soon as he is on his feet, through\n"
"                   the same path TAB takes - for testing that screen\n"
"                   without walking to it\n"
"  TAB              once he is on his feet, opens the SNEAK - his handheld\n"
"                   device. It is the adventure scheme\'s own \"Ouvrir\n"
"                   sneak\" binding, and the .CTL has to play H_SNKON first,\n"
"                   so it is not instant. LEFT/RIGHT move between the\n"
"                   device\'s columns, UP/DOWN within one, ENTER opens a tab\n"
"\n"
"DISPLAY\n"
"  --res WxH        default 800x600; the interface is authored at 640x480\n"
"                   and scaled, so a bigger display spreads the same layout\n"
"  --vulkan         force the Vulkan backend (V toggles it live)\n"
"  --software       force the software rasteriser\n"
"  --letterbox      the 1.818:1 camera-mode bars, for laying a shot beside\n"
"                   a capture; --full is the old spelling of the opposite\n"
"\n"
"INPUT, scripted\n"
"  --keys D,D,...   DIK scancodes fed in order; `T` types --type there\n"
"  --type <text>    what `T` types - the start menu refuses an empty name\n"
"  --keydelay N     frames between scripted keys, default 2\n"
"  --hold <stream>  after the hand-over, DIK codes HELD: `k200*120,k203*30`,\n"
"                   `+` joins several, `0*n` holds nothing - player_probe's\n"
"                   own syntax, so a walk can be replayed\n"
"\n"
"THE SET VIEWER (--scene)\n"
"  --cam N          step the set's own cameras\n"
"  --eye x,y,z      place the eye        --at x,y,z   aim it\n"
"  --fov F          horizontal field of view, degrees\n"
"  --nodelay        drop the 16 ms sleep. THE SET VIEWER ONLY - it does\n"
"                   nothing to a game run; use --frames for that\n"
"\n"
"  --help           this\n");
    };
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--help") { usage(stdout); return 0; }
    if (argc < 2) {
        usage(stderr);
        return 2;
    }
    const std::string fr = argv[1];
    // THE TABLES ARE OPTIONAL, and the argument for them is too.
    //
    // `tables/` holds what a replica cannot read out of the shipped tree - the
    // VM opcode table, the widget tree, the fonts and key bindings, the ADPCM
    // coefficients. Most of it only makes the run RICHER, so its absence is a
    // warning and not a refusal; each loader below says what is lost.
    //
    // The second positional argument is taken as the directory when it is not
    // a flag; otherwise a few obvious places are tried, so `omk-play <tree>`
    // works from the repo root or from `engine/`.
    std::string tb;
    if (argc >= 3 && argv[2][0] != '-') tb = argv[2];
    else {
        for (const char* cand : {"tables", "../tables", "../../tables"}) {
            std::ifstream probe(std::string(cand) + "/vm_opcodes.json");
            if (probe) { tb = cand; break; }
        }
        if (!tb.empty())
            std::printf("tables: none given, using %s\n", tb.c_str());
    }
    int screenId = 29, frames = 0;
    bool playMovies = true;
    std::string dump, typeText;
    // THE DISPLAY. The interface is authored at 640x480 and its coordinates
    // are scaled by `I2D_ScaleX/Y` (`v * w / 640`, `v * h / 480`), so a bigger
    // display spreads the same layout without enlarging the glyphs. 800x600 is
    // the mode a reader's own screenshot of the original is in.
    int dispW = 800, dispH = 600;
    std::vector<int> scripted;
    bool bankReject = false;          // --bank-reject, a DEBUG switch
    int keyEvery = 2;
    // ADVENTURE MODE, headless: `--hold k200*120,k203*30` is a replayable
    // input stream fed AFTER the hand-over - DIK scancodes HELD for that many
    // frames (`+` joins several), `0*n` holds nothing - the same syntax
    // `tools/player_probe.cpp` takes, so a walk can be reproduced without a
    // person at the keys. `--snaps DIR` writes the framebuffer as raw LE
    // RGB565 every 30 frames from the hand-over on (`snap-<frame>.bin`,
    // 640x480 after the display size), which is how the walk was LOOKED at.
    std::string holdStream, snapsDir;
    // A STREET START (docs/STREET_LIFE.md, step 4): `--save FILE` takes the
    // game DB from a save's slot 0 - the player record lives there, and
    // Kay'l's actor record is in no city chunk - `--area N` loads that area
    // instead of the save's own, `--address A` puts him down on one of its
    // ADDRESSES (listed at start), and adventure mode begins at once, with
    // no intro to replay. `--density` is options row 6 for the crowd
    // (default the engine's 3), `--no-crowd` leaves the pedestrians out.
    std::string saveFile;
    int areaArg = -1, addressArg = -1, density = omk::kDefaultStreetActivity;
    // --give: object ids for the carried list, comma-separated. A LIST
    // rather than one id because the flows worth driving need a bagful - row
    // scrolling wants more than the nine row widgets, and `Utiliser sur`
    // wants a recipe PAIR, and a new game ships exactly two objects.
    std::string giveList;
    bool newWorld = false; // --newgame-world: START's world, the save's player
    // --scene-chunk N: run a SCENE chunk's startup script over the area, the
    // way `scene.load` does. A street start jumps straight to an area, so the
    // chunk that would have been loaded on the way in never is - and with it
    // go the beats that SHOW the area's props. For AREA 222 that is SCENE 55,
    // whose script ends in `object.show 162`, the Impasse's rings: without it
    // there is nothing in the world to take (`tools/prop_probe.cpp` does the
    // same call, and is where this shape comes from).
    int sceneChunk = -1;
    bool noCrowd = false;
    // `--sneak` opens the device as soon as the player is on his feet,
    // through the SAME path TAB takes - `MDSNEAK0`'s handler, event 25 and
    // screen 9 - rather than a second way in. A testing convenience for a
    // screen that otherwise costs a walk to reach; it sets the same
    // `playerScreen` the special move sets and nothing else.
    bool openSneak = false;
    float standAt[4] = {0, 0, 0, 0};
    bool haveStand = false;      // `--stand x,y,z,yaw`: put the player down there after the hand-over
    // the scene viewer's
    std::string scene;
    int camIndex = -1;
    float eyeA[3], atA[3], fovA = 0;
    // NOT letterboxed by default, and that was a mistake worth naming. The
    // 1.818:1 letterbox is measured off DIALOGUE captures (ASSETS: dialog 402,
    // an 800x600 frame carrying an 800x440 strip), so it is established for
    // CAMERA MODE - conversations and cutscenes. Nothing establishes it for
    // free roaming, and this is a free-look tool; imposing it here was
    // generalising a camera-mode property to all rendering. `--letterbox`
    // brings it back for comparing against those captures, which is the one
    // job it is evidence for.
    bool haveEye = false, haveAt = false, letterbox = false, startVulkan = false, noDelay = false;
    double speed = 1.0;                 // --speed: the frame delta's multiplier
    bool forceSoftware = false, showFps = false;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--scene" && i + 1 < argc) scene = argv[++i];
        else if (a == "--cam" && i + 1 < argc) camIndex = std::atoi(argv[++i]);
        else if (a == "--fov" && i + 1 < argc) fovA = static_cast<float>(std::atof(argv[++i]));
        else if (a == "--letterbox") letterbox = true;   // camera mode, for comparing with captures
        else if (a == "--full") letterbox = false;       // kept: it was the old spelling
        else if (a == "--vulkan") startVulkan = true;
        // The game uses Vulkan when the machine has it; this forces the
        // software reference, which is what every check is written against.
        else if (a == "--software") forceSoftware = true;
        // The frame rate, reported once a second on stdout. Off by default:
        // it is a diagnostic, and a game that prints every second when nobody
        // asked is a game with a line of noise under it.
        else if (a == "--fps") showFps = true;
        else if (a == "--nodelay") noDelay = true;
        // THE SPEED MULTIPLIER, and the engine has one of its own.
        // `Game_Frame` switches on `dword_4E972C` to set the frame delta:
        //
        //     case 0   flt_4C30D8 = 30.0 / fps, capped at 3.0   the normal rate
        //     case 1   1.0        case 2   0.5
        //     case 3   0.1        case 4   2.0
        //
        // so the game ships with a double-speed and two slow-motions, driven
        // by scaling the DELTA rather than by running the loop faster. This
        // does the same to `frameSec`, so 2 is the engine's case 4 and 0.5 its
        // case 2. Clamped at the engine's own ceiling of 3.0 - case 0 caps
        // there, and past it a single frame steps further than any of the
        // runtime's own clamps expect.
        else if (a == "--speed" && i + 1 < argc) {
            speed = std::atof(argv[++i]);
            if (!(speed > 0.0)) speed = 1.0;
            if (speed > 3.0) speed = 3.0;
        }
        // How many frames apart the scripted keys are fed. The default 2 is
        // one press and one release, which is the minimum that produces an
        // EDGE; a walk that has to wait for a line to play needs more.
        else if (a == "--keydelay" && i + 1 < argc) keyEvery = std::atoi(argv[++i]);
        else if (a == "--hold" && i + 1 < argc) holdStream = argv[++i];
        else if (a == "--snaps" && i + 1 < argc) snapsDir = argv[++i];
        else if (a == "--res" && i + 1 < argc)
            std::sscanf(argv[++i], "%dx%d", &dispW, &dispH);
        else if (a == "--eye" && i + 1 < argc)
            haveEye = std::sscanf(argv[++i], "%f,%f,%f", &eyeA[0], &eyeA[1], &eyeA[2]) == 3;
        else if (a == "--at" && i + 1 < argc)
            haveAt = std::sscanf(argv[++i], "%f,%f,%f", &atA[0], &atA[1], &atA[2]) == 3;
        else if (a == "--frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (a == "--dump" && i + 1 < argc) dump = argv[++i];
        else if (a == "--nofmv") playMovies = false;   // the engine's own switch
        else if (a == "--save" && i + 1 < argc) saveFile = argv[++i];
        else if (a == "--area" && i + 1 < argc) areaArg = std::atoi(argv[++i]);
        else if (a == "--address" && i + 1 < argc) addressArg = std::atoi(argv[++i]);
        // A HARNESS FLAG, not a port: put an object into the carried list so
        // a flow can be exercised from a save that does not carry it. VM
        // opcode 50 `inventory.add` is what the game uses; this writes the
        // slot and runs none of its bookkeeping.
        else if (a == "--give" && i + 1 < argc) giveList = argv[++i];
        else if (a == "--bank-reject") bankReject = true;
        // ...and its companion: keep the save's PLAYER but take the world
        // from `IAM\START`, so a flow can be tried against a new game's
        // state without the intro. Also a harness flag, not a port.
        else if (a == "--newgame-world") newWorld = true;
        else if (a == "--scene-chunk" && i + 1 < argc) sceneChunk = std::atoi(argv[++i]);
        else if (a == "--density" && i + 1 < argc) density = std::atoi(argv[++i]);
        else if (a == "--no-crowd") noCrowd = true;
        else if (a == "--sneak") openSneak = true;
        else if (a == "--stand" && i + 1 < argc)
            haveStand = std::sscanf(argv[++i], "%f,%f,%f,%f", &standAt[0], &standAt[1], &standAt[2], &standAt[3]) >= 3;
        else if (a == "--type" && i + 1 < argc) typeText = argv[++i];
        else if (a == "--keys" && i + 1 < argc) {
            // A `T` in the list means "type `--type` here" - the start menu's
            // confirm is gated on a non-empty name field, so a scripted walk
            // has to type between two presses, not before them.
            std::string t = argv[++i], cur;
            for (char c : t + ",") {
                if (c == ',') {
                    if (cur == "T") scripted.push_back(kTypeMarker);
                    else if (!cur.empty()) scripted.push_back(std::stoi(cur, nullptr, 0));
                    cur.clear();
                }
                else cur.push_back(c);
            }
        } else if (a[0] != '-') screenId = std::atoi(a.c_str());
    }

    // The viewer takes the whole program: it wants no boot chain, no widget
    // tree and no movies, and mixing it into the interface loop would make
    // both harder to read than either is worth.
    if (!scene.empty())
        return sceneViewer(fr, scene, camIndex, haveEye ? eyeA : nullptr,
                           haveAt ? atA : nullptr, fovA, letterbox, frames, dump,
                           startVulkan, noDelay);

    const omk::DataFs fs(fr);
    auto w = omk::UiWidgets::loadJson(tb + "/ui_widgets.json");
    if (!w.valid())
        std::printf("tables: no widget tree (ui_widgets.json) - the interface "
                    "screens cannot be drawn or walked, so the Session answers "
                    "them itself and the start menu is skipped\n");
    w.loadScreens(tb + "/ui.json");
    const auto fonts = omk::FontTable::loadJson(tb + "/ui.json");
    const omk::TextLayout lay(fonts, fr + "/FONTS");
    omk::ScreenComposer comp(fs, w, lay);
    // The menu's animated background - `IMAGES/cloud.bmp` embossed by a
    // rotating light and warped by two cosine tables (`ui/cloud.h`). The
    // screen's own sheet is colour-keyed over it.
    omk::MenuCloud cloud;
    if (cloud.load(fs)) comp.attachCloud(&cloud);
    else std::printf("no IMAGES/cloud.bmp - the menu draws on black\n");

    // The real input path: scancodes in, the live binding tables and
    // `Game_Frame`'s edge filter in the middle, one 14-bit word out. Nothing
    // here hands the walk a word directly, which is the whole point of
    // `verify.py: engine input`.
    const auto schemes = omk::ControlSchemes::loadJson(tb + "/key_bindings.json");
    omk::Input in(schemes);
    in.installScheme(0);
    in.setRepeatMask(0x203F);          // `Ui_BeginScreen`

    // ---- the boot chain, because the app does not start at a menu --------
    //
    // `docs/BOOT.md`: launch -> the three FLIS movies -> Game_Start
    // ("aventure.scx", which is the global sprite and sound library and NOT a
    // menu) -> the start area out of `IAM\START +1414` -> whose startup script
    // reaches `ui.open`. Running it here rather than opening screen 29
    // directly means the menu appears because the SCRIPT asked for it, which
    // is what `engine: boot` reproduces 42 of 42 from a cold start.
    //
    // **THE MOVIES ARE STEPPED, NOT DECODED.** `gamedata/FLIS/` holds three MPEG-1
    // program streams and `PORTING` A8 names pl_mpeg as the vendored decoder
    // for them; it is not integrated, so the boot finds the three files,
    // reports them, and moves on. The first thing a player sees is therefore
    // missing here, and saying so is the point - a window that opens on the
    // menu would imply the app starts there.
    omk::BootOptions bo;
    bo.root = fr; bo.tables = tb; bo.frames = 1;
    const omk::BootReport br = omk::boot(bo);
    std::printf("boot: %d FLIS movie%s found; %s -> %d sprites, %d sounds; "
                "start area %d\n",
                br.moviesFound, br.moviesFound == 1 ? "" : "s",
                br.bootScene.c_str(), br.bootSprites, br.bootSounds,
                br.startArea);

    // ---- THE LIVE SESSION -----------------------------------------------
    //
    // `boot()` runs the chain and reports; this RUNS it, frame by frame, with
    // a person at the keyboard. The difference is where the menu's answer
    // comes from: `attachUi` DERIVES one by walking the tree, which is right
    // for a headless check and wrong for a game. `answerUiFromPerson` parks
    // the script instead - `Game_HandleEvent` case 5 - and nothing releases it
    // until somebody presses a key.
    //
    // So the menu is not opened by this file. AREA 118's own startup script
    // reaches `ui.open(29, -1, -> variable 19)` and parks; the frontend asks
    // the Session which screen is waiting and opens THAT. A build that opened
    // screen 29 itself would still show a menu and would be a different
    // program.
    const auto opcodes = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    // The ONE that cannot be worked around: without the operand lengths the
    // VM cannot even step an instruction, and a hand-written table is not an
    // option - CLAUDE.md records one being wrong three ways in an hour. It is
    // in the repo as `tables/vm_opcodes.json`.
    if (!opcodes.valid()) {
        std::fprintf(stderr,
            "no VM opcode table: looked for %s/vm_opcodes.json\n"
            "  this one is required - the world scripts cannot be decoded "
            "without the operand lengths.\n"
            "  pass the directory as the second argument, e.g. "
            "omk-play <gamedata> tables\n",
            tb.empty() ? "<no tables dir>" : tb.c_str());
        return 1;
    }
    omk::GameState state = omk::GameState::fromFile(fr + "/IAM/START");
    if (!saveFile.empty()) {
        const auto slot = omk::readSaveSlot(omk::DataFs::readPath(saveFile), 0);
        if (!slot) { std::fprintf(stderr, "%s: not a save file\n", saveFile.c_str()); return 1; }
        state = slot->state;
        // THE CLOCK COMES FROM THE SLOT, and this used only to print it.
        //
        // `gamestate.h`: the clock and the timer are engine globals and NOT
        // part of the 8192-byte image, so restoring the DB restores
        // everything EXCEPT the date and time - and the slot header is the
        // only place they exist. Loading a save therefore left the game at
        // day 0, 00:00:00 while the loader printed the save's real date one
        // line above.
        //
        // Nothing noticed because nothing DREW the clock. The sneak's own
        // clock row (`sub_0049E090`) is the first thing in this port to show
        // it, and it showed "1 Aqed 7216 - 0:00:00" against a save the same
        // function had just printed as a different date.
        if (newWorld) {
            // The save brought its own world - doors opened, addresses
            // enabled. Put a new game's back, keeping the player record the
            // save is loaded FOR.
            if (state.debugCopyWorldFrom(omk::GameState::fromFile(fr + "/IAM/START")))
                std::printf("--newgame-world: the six state arrays and the "
                            "three object lists reset to IAM/START (a harness "
                            "write, not `Game_NewGame`)\n");
        }
        state.setClockDay(slot->day);
        state.setClock(slot->time);
        std::printf("save: slot 0 '%s', %s %s, area %d\n", slot->name.c_str(),
                    omk::formatDate(slot->day).c_str(), omk::formatTime(slot->time).c_str(),
                    state.currentArea());
    }
    const bool forceAdventure = areaArg >= 0;
    // THE INVENTORY, out of the game data: `IAM\OBJECT`'s 1002 records and
    // `IAM\GLOBAL +12`'s eleven combination recipes. `script/inventory.h` was
    // written, checked and never consumed by anything that runs - the sneak
    // is what the channel exists for, so this is where it is loaded.
    if (!giveList.empty()) {
        int placed = 0, refused = 0;
        std::string cur;
        for (char ch : giveList + ",") {
            if (ch != ',') { cur.push_back(ch); continue; }
            if (cur.empty()) continue;
            const int id = std::atoi(cur.c_str());
            cur.clear();
            if (id <= 0) continue;
            // `debugPutObject` fills the FIRST free slot, so the ids land in
            // the order they are given - which is the reverse of what the
            // game's own `ObjectList_InsertFront` would do, and is fine for a
            // harness whose point is to have a bag at all.
            if (state.debugPutObject(0, id)) ++placed;
            else { ++refused; std::printf("--give: no free slot for object %d\n", id); }
        }
        std::printf("--give: %d object%s put in the carried list, %d refused "
                    "(a harness write, not `inventory.add`)\n",
                    placed, placed == 1 ? "" : "s", refused);
    }
    const auto objectRecords = omk::loadObjects(fs);
    const auto globalFile = fs.read("IAM/GLOBAL");
    const auto recipes = omk::globalRecipes(globalFile);
    omk::Inventory inv(objectRecords, recipes);
    // `GLOBAL +16` - the sneak's slider destinations, 39 of them.
    const auto destinations = omk::globalDestinations(globalFile);
    bool sliderTold = false;
    std::string examineTold;
    std::string examineText;
    if (objectRecords.empty())
        std::printf("no IAM/OBJECT - the sneak's inventory page will be "
                    "empty\n");
    omk::Session session(fr + "/IAM", state, opcodes);
    if (bankReject) {
        session.setBankReject(true);
        std::printf("DEBUG --bank-reject: every bank refused, the object stays in hand - "
                    "NOT the original's rule\n");
    }
    if (!session.loadAnnounceMap(tb + "/vm_announce.json"))
        std::printf("tables: no vm_announce.json - the log will name fewer "
                    "operands\n");
    // A person answers the screens only when there ARE screens to draw.
    session.answerUiFromPerson(w.valid());
    // There is a frame clock here, so `camera.set.wait` can do what the
    // handler does: hold the script for the length of the move it started.
    // Without it AREA 118's six intro cameras and its `area.goto` all happen
    // in one frame and the introduction is never seen.
    session.setCameraWait(true);
    // And the waiting `scx.play*` variants, which is the beat before the
    // intro's conversation: AREA 118 shows Kay'l and starts his animation with
    // `scx.play.actor.wait`, holding the script while the camera travels.
    session.setObjectWait(true);
    // And a conversation takes as long as its own voice does. Without this the
    // frontend closed each one on the next frame - a labelled stand-in that
    // got the intro's SHAPE wrong, because the grid tunnel AREA 118 cuts to
    // AFTER `dialog.start 272` then arrived two seconds after the menu instead
    // of three minutes. `src/script/dialogue.h` carries the reasoning; the
    // decision is on the ported side and this file only plays the audio.
    session.attachDialogue(fr + "/MORPH");
    // STREET LIFE: the pedestrians of an area naming a circuit spawn at its
    // load, at the density the options menu will one day hand in.
    session.setStreetActivity(density);
    if (!noCrowd) session.loadTraffic(fr);
    // A movie chain is the intro's; a street start skips it.
    if (forceAdventure) playMovies = false;
    const int startArea = areaArg >= 0 ? areaArg : state.currentArea();
    if (startArea < 0) { std::fprintf(stderr, "IAM/START names no area\n"); return 1; }
    session.loadArea(startArea);
    std::printf("session: area %d loaded, waiting for its script\n", startArea);
    if (sceneChunk >= 0) {
        session.sceneLoad(startArea, sceneChunk);
        std::printf("--scene-chunk: SCENE %d over AREA %d - its startup script "
                    "runs, which is what SHOWS an area's props\n",
                    sceneChunk, startArea);
    }
    if (forceAdventure) {
        const auto& rs = session.residentSlot(session.activeSlot());
        for (const auto& ad : rs.addresses)
            std::printf("address %d at %.0f %.0f %.0f yaw %.0f\n", ad.id, ad.pos[0], ad.pos[1], ad.pos[2], ad.yaw);
        if (addressArg < 0 && !rs.addresses.empty()) addressArg = rs.addresses.front().id;
        if (addressArg >= 0) {
            if (session.placeActorAt(addressArg))
                std::printf("street start: the player at address %d, %.0f %.0f %.0f facing %.0f\n",
                            addressArg, session.playerPos()[0], session.playerPos()[1],
                            session.playerPos()[2], session.playerYaw());
            else
                std::printf("street start: address %d is not in area %d\n", addressArg, startArea);
        }
        // ...and the camera a hand-over ends on: `Camera Player` (0), the
        // follow preset, which the intro's scripts request and this has to.
        session.requestCamera(0, 0);
        const auto& pd = session.sliders();
        std::string models;
        for (const auto& m : pd.models()) { if (!models.empty()) models += ","; models += m.name; }
        std::printf("street life: circuit %s, %d walkers at density %d (%s)\n",
                    rs.opt.empty() ? "none" : rs.opt.c_str(), pd.liveCount(), pd.streetActivity(),
                    models.empty() ? "no models" : models.c_str());
    }
    // The area's own `.SCX`, so `scx.play*` has objects to start - and so the
    // WAITING variants (46, 58, 60) have something to wait ON. Without it
    // AREA 118's `character.show 310` + `scx.play.actor.wait 310, 1` starts
    // nothing and the script runs straight into `dialog.start`, which is the
    // ~5 second beat before the conversation going missing.
    // The scene's `.SFX` too - `AREA +97` names both, and starting an object
    // fires the set pieces keyed to it.
    if (session.loadScene(fr + "/SCPTDATA", omk::ChunkKind::Area, startArea))
    {
        std::string sfxName = session.scene().file();
        const auto dot = sfxName.rfind('.');
        if (dot != std::string::npos) sfxName = sfxName.substr(0, dot) + ".sfx";
        session.sceneMutable().attachSfx(fr + "/SCPTDATA", sfxName);
        const auto& sf = session.scene().sfx();
        std::printf("scene: %s resident, %zu objects; %s: %zu effects, "
                    "%zu set pieces (%d of them keyed so this trigger cannot "
                    "reach them)\n",
                    session.scene().file().c_str(),
                    session.scene().scene().scene().objects.size(),
                    sfxName.c_str(), sf.effects.size(), sf.pieces.size(),
                    session.scene().standingPieces());
    }
    else
        std::printf("scene: area %d names no .SCX\n", startArea);


    // The screen currently on the player's hands, if any. `walk` is only
    // constructed once a script has actually asked for a screen - or, since
    // the sneak, once the PLAYER has.
    std::unique_ptr<omk::UiWalk> walk;
    int openScreen = -1, conversations = 0, lastArea = -1;
    constexpr int kScreenPause = 31;   // PAUSE GAME - the only screen that
                                       // sets dword_4E9728, the pause flag
    omk::UiCursor uiCursor;   // Ui_DrawItemCursor's one pool (dword_6A4D20)
    omk::UiListState uiLists; // every list's `+2`, for as long as we run
    // The sneak's three turning previews. Loaded once - the engine loads them
    // in the sneak's OPEN callback and frees them in its close, which for a
    // viewer that opens the device repeatedly is the same three files each
    // time.
    omk::UiModels uiModels;
    if (const int n = uiModels.load(fs))
        std::printf("sneak: %d of 3 preview models loaded (%s...)\n",
                    n, uiModels.name(0).c_str());
    // WHO asked for the open screen, because the two ends differ. `ui.open`
    // parks its caller at status 6 and every close path posts event 5 with
    // the answer, so leaving IS an answer and the script resumes. The sneak
    // has no caller at all: `sub_0046ADF0` calls `UI_OpenScreen(9, -1, ...)`
    // and the `-1` is the waiting-context argument, so `dword_930744` is
    // never written and there is nothing to resume. Answering the Session for
    // it would release whatever script happened to be parked.
    bool screenFromScript = true;
    // A screen the PLAYER asked for this frame, before it is opened below -
    // so the open, its sounds and its bookkeeping stay in one place.
    int playerScreen = -1;
    // The sneak's inventory ROWS - item address -> what that row shows. The
    // nine slots of list 0x004DE6F0 ship `+28` as -1 and are never bound,
    // because their text is the carried object list read through the channel
    // (`Game_HandleEvent` 29 and 33), not a string in `IAM\Sneak`.
    std::map<std::uint32_t, std::string> sneakRows;
    // The row widgets `sub_42AAE0` switches off - past the object count, so
    // tag -1 and `0x40000001` set. Without it every one of the nine rows
    // draws its fill and the page is striped.
    std::set<std::uint32_t> sneakHidden;
    int sneakTold = 0;             // one line per run, not one per frame
    int replySel = 0;            // which reply the player is on
    int actionTold = 0;          // one line for a press that reaches nothing
    // THE ACTION BUTTON IS AN EDGE (omk-play 74). Bit 0x10 arrives as a LEVEL
    // - the world's repeat mask is 0, so a held key is set every frame - and
    // this remembers the previous frame's so the press fires once. See the
    // long note at the dispatch for why the engine needs no such variable.
    // THE PRESS BITS ARE EDGES (omk-play 74). In the world the repeat mask is
    // 0, so every bit arrives as a LEVEL and a held key is set every frame -
    // right for a walk, wrong for anything that COUNTS presses. This is the
    // previous frame's word; `edgeBits` below is the difference.
    std::uint32_t prevBits = 0;
    // `tab_special_move[]` as a TABLE rather than a string compare: a fired
    // move resolves to its ROW - the index the engine dispatches on and the
    // handler address it calls - so an unknown name comes back nullptr
    // instead of falling off the end of an if-chain. The shipped `.CTL` files
    // use 54 distinct names against the table's 66 rows.
    omk::SpecialMoves specialMoves =
        omk::SpecialMoves::loadJson(tb.empty() ? std::string() : tb + "/special_moves.json");
    if (!specialMoves.valid())
        std::printf("special moves: tables/special_moves.json not read - the take will "
                    "still work, but a fired move cannot name its row\n");
    // the LOOPING scene voices, keyed the way `Script_StopSound` matches them:
    // by (wav, node). Only loops are kept - a one-shot ends by itself.
    std::map<std::pair<int,int>, int> sceneVoices;
    // omk-play 72: WHICH audio source is the one that will not stop. Every
    // start is labelled with its length, and `flushAudio` says how many
    // one-shots it does NOT clear - the streamed music is flushed on a switch
    // and `shots_` are not, so anything long started as a one-shot outlives
    // an area change, a music switch and a cutscene.
    const auto sfxLog = [&](const char* what, std::size_t samples, int a, int b,
                            float gain = 1.0f) {
        const double secs = samples / 44100.0 / 2.0;
        if (secs >= 0.75)                 // footsteps and blips are not the story
            std::printf("audio: %-14s %6.2f s  (%d, %d)  gain %.2f\n",
                        what, secs, a, b, gain);
    };
    int         takeCandidate = -1;      // `dword_53AF6C`, MDACTION's pick
    // Which HEIGHT the take was, kept from MDACTION so the put-back can match
    // it. The engine keeps the same thing in `dword_53AE5C` - `(ret == 2) ? 3
    // : 0`, which `sub_46B530` turns back into a group (omk-play 69).
    bool        takeWasLow = true;
    int         heldInHand = -1;         // the object drawn on the left hand: from MDGETOBJ to the release
    // The spoken line's SCROLL, in pixels, and the overflow it is clamped to -
    // `dword_6A52C0` and `dword_53AE24`. One pixel a tick while held, which is
    // what `Dialog_TickUI` does with input bits 4 (up) and 8 (down).
    int lineScroll = 0, lineOverflow = 0;
    bool menuShown = false;
    int  lastDlgCam = -2;
    // The absolute world cameras the script has set, as rays: during the
    // beat before a conversation they are what aims at the character.
    std::vector<omk::CameraRay> worldRays;
    int lastRayCam = -2;
    // THE CAMERA EDITING - camera mode 13. Every `scx.play*` handler ends by
    // asking `ScriptObject_HasCamEditing` and, when the object has a chunk-10
    // editing linked, requests mode 13 with the call's last field as the
    // travel (`SceneRunner::ActiveEditing` quotes the assembly). Mode 13 is
    // "follow the scene's active camera": the camera tick copies eye, target,
    // fov and roll out of `dword_9103D4` every frame, which is what
    // `Script_PlayScript` sampled from the editing at the OBJECT'S clock. It
    // supersedes the mode-12 world camera `camera.set` chose for as long as
    // the editing runs; when nothing sets an active camera and the mode is
    // still 13, the frame loop requests mode 0 with travel 0 (05_sys.c 2140)
    // - a cut back. The Session knows which editing is driving and what it
    // says; the travel FROM the previous camera is here, because this is
    // where the previous camera is: the one last drawn.
    int   editingShown = -1;                  // the announced editing's program
    bool  haveLastDrawn = false;              // a 3D camera has been drawn
    float lastEye[3] = {0, 0, 0}, lastAt[3] = {0, 0, 0}, lastFov = 75.0f;
    // omk-play 69: THE TAKE CAMERA. `MDGETOBJ` (0x0046B380, no proc label -
    // read from the raw listing) fills the request block with the player as
    // both subjects, a 30-frame travel (`dword_930818 = 30.0`) and calls
    // `Camera_Request(1)`; mode 1 loads preset 1 of the table at 0x004C20C8
    // (tables/camera_presets.json, `verify.py: camera presets`). `MDPUTSNK`
    // (0x0046B4B0) and `MDLETOBJ` (0x0046B460) call mode 16 - the swap back
    // with nothing loaded, 30 frames too - gated on `C+12 == 1`, the live
    // block being the mode-1 camera. `MDNOTAKE` swaps nothing: a cancel goes
    // through the put-back to MDLETOBJ. So the side view holds from the
    // hand-over until the object is banked or set down, on either path.
    // THE `.CTL` EFFECT SPRITES (omk-play 69, point 3). `Cef_UpdateStateEffects`
    // (0x0045B260) spawns every record of a state on ENTRY (or when the clock
    // wraps) unless the record's flag bit 2 marks it a per-frame emitter;
    // `Cef_SpawnEffect` (0x0045B3B0) takes one sprite instance from the scene
    // registry by the record's +20 id, places it at `Actor_AttachPoint(code)`,
    // scale +28, mode 4 (ADDITIVE); `Cef_TickEffects` (0x0045ADF0) keeps it
    // alive while `from <= clock <= from + duration` (and `<= to` when +8 is
    // set), sets its frame to `(clock - from) / duration * frames`, and with
    // flag 1 moves it to the bone every tick. The confirm's H_GETOBJ carries
    // two - sprites 127 and 130 on the LEFT HAND (attach 10 -> actor+44,
    // "Maing"), which is the "particle effect on the arm" a reader saw.
    // Attach codes map through Actor_AttachPoint's switch onto the loader's
    // bone table (04_sys.c 5497..5513), by NAME below.
    struct CtlSpriteInst { int sprite = 0; float duration = 0, from = 0, to = 0, scale = 1;
                           std::uint8_t flags = 0, attach = 0; int state = -1; };
    std::vector<CtlSpriteInst> ctlSprites;
    int   ctlFxState = -1; float ctlFxFrame = -1.0f;
    omk::ParticleField ctlField; omk::Geometry ctlGeo;
    static constexpr const char* kAttachName[18] = {
        "Buste", "Tete", "Buste", "Buste", "Buste", "Bassin", "Brasg", "Brasd",
        "Avantg", "Avantd", "Maing", "Maind", "Cuisseg", "Cuissed", "Jambeg",
        "Jambed", "Piedg", "Piedd"};   // 0/2/3/4 fall to the default arm, a1[5] = Buste
    bool  takeCam = false;            // `C+12 == 1`: the mode-1 camera is live
    int   takeCamPhase = 0;           // 1 travelling in, 2 holding, 3 travelling back
    float takeCamClock = 0.0f;        // frames since the request
    float takeCamFromEye[3] = {0, 0, 0}, takeCamFromAt[3] = {0, 0, 0}, takeCamFromFov = 75.0f;
    // preset 1: 62 cm to his side, 75 cm above the pelvis, 12 cm back, looking
    // 12 cm up and 50 cm ahead of him. In the mode-0 convention the follow
    // camera uses (`point = subject - R(yaw) * offset`, mode 0 = 3 m behind).
    constexpr float kTakeCamEye[3] = {24.4094f, 29.5276f, -4.7244f};
    constexpr float kTakeCamAt[3]  = {0.0f, 4.7244f, 19.685f};
    constexpr float kTakeCamFov    = 75.0f;
    constexpr float kTakeCamTravel = 30.0f;
    auto takeCamRequest = [&](int phase) {
        takeCamPhase = phase;
        takeCamClock = 0.0f;
        for (int k = 0; k < 3; ++k) { takeCamFromEye[k] = lastEye[k]; takeCamFromAt[k] = lastAt[k]; }
        takeCamFromFov = lastFov;
    };
    float lastRoll = 0.0f;              // the camera ROLL, blended like the fov
    bool  editFromKnown = false;              // ...and it was captured for the travel
    float editFromEye[3] = {0, 0, 0}, editFromAt[3] = {0, 0, 0}, editFromFov = 75.0f;
    float editFromRoll = 0.0f;
    bool  rollTold = false;
    int fxSpriteWas = -2;

    // ---- ADVENTURE MODE ------------------------------------------------
    //
    // The Impasse's cutscene ends `camera.set 0,0,2` + `scene.load 237,57` +
    // `player.anim.release`: the hand-over to the person at the keyboard.
    // Until 2026-09-02 nothing here moved him after it. The controller
    // (`actor/player.h`) is the engine's own chain - the input word into the
    // `.CTL` channel, the clip's root motion out of it, the walker under it,
    // the follow camera behind - and this file only builds it, feeds it the
    // word `Input::frame` makes, and draws where it says.
    //
    // WHEN: the Session's camera is a world camera whose two subjects are
    // actor 0 (SCENE 55's camera 0 - an eye 119 behind and 26 above him,
    // the follow shape `worldcam.h` describes), no scene program is playing
    // the player, he has been placed, and no conversation, screen or
    // editing owns the frame. `player.anim.release` (op 105) is a no-op in
    // the Session, so the program ending is the signal it leaves - which is
    // also what `Actor_TickScxDriven` keys on (the program's `IsBusy`).
    std::unique_ptr<omk::PlayerController> player;
    omk::CtlFile playerCtl;
    std::vector<std::byte> playerCtlData;
    std::vector<omk::Mesh> playerMeshes;
    std::vector<omk::Texture> playerTex;
    omk::Geometry playerRest, playerPosed;
    std::vector<omk::CollisionSphere> playerSpheres;   // the crowd push tests these
    float playerReach = 0.0f;                           // his model's +88
    omk::TriangleSoup playerSoup;
    // the same merge for the STEEP faces, so the controller can stand on a
    // slope and slide off it instead of finding no floor (omk-play 67)
    omk::TriangleSoup playerSteep;
    std::string playerModel, playerCtlName;
    bool  playerReady = false, adventure = false, followCam = false;
    int   placementSeen = 0;      // Session::placementSeq() as last consumed
    long  heldFrames = 0;         // frames under player.anim.hold
    // The `media.play` SUBTITLE: `Subtitle_Show(unk_4E6268)` is step 13 of
    // the handler (todo/pending/E1.md 1) - the ZVO record's +280 description,
    // `{C}`-prefixed when the player is in ACTOR_STATE 3 or 15, on screen for
    // 80 ms a character and never less than two seconds (`Subtitle_Show`,
    // readable/src/05_sys.c). The airlock's line 410 is the first the port
    // shows: its voice is a JINGOFF3 substitute, and the TEXT is what the
    // player reads.
    std::string mediaText;
    long  mediaTextFrames = 0;
    // THE MEDIA BITMAP - `media.play` on a kind-16 DOCUMENT.
    //
    // `if (rec[+2] == 16)` takes the other arm entirely: build
    // `IMAGES\<stem>.BMP`, `I2D_LoadBitmap` it, put the player in ACTOR_STATE
    // **10** (`ImageScreen`, "a full-screen bitmap holds it") and play NO
    // audio. It stays up until the NEXT `media.play`, which frees it (step 7,
    // `I2D_FreeBitmap` then ACTOR_STATE 1).
    //
    // That is the game's TITLE CARD: object 715 `ZVO G001 TITRE` is kind 16
    // with stem `ZVOG001`, so its `+280` description is `{X030040}{f3}` and
    // nothing else - the words are in `IMAGES/ZVOG001.BMP`, 640x480 with the
    // logo on black. Nothing here drew it, which is why the Bowie opening
    // came up without its title (`todo/omk-play.md` 59).
    omk::Surface mediaBmp;
    float playerFeet = 0.0f;
    bool  playerFeetKnown = false;
    // The model-space x/z of the hierarchy root - the PELVIS - which is what
    // a turn must pivot about. `HO1_FN`'s is (2.87, 17.94); rotating about
    // (0,0) instead swings him around a point half a metre away.
    float playerRootXZ[2] = {0.0f, 0.0f};
    float lastRootDrop = 0.0f;         // the crouch's root drop as drawn last frame (the held prop rides it)
    int   playerCamId = -2;
    // The facing at the hand-over. `Actor_TickScxDriven` sets +1308 when
    // the player's program ends and `Actor_TickNpc` then derives the facing
    // from the node's matrix - which after a scene clip is the clip's root
    // rotation at the frame reached. Tracked while the program runs, since
    // `scene.load` replaces the runner and its clips with it.
    float handoverFacing = 0.0f;
    bool  handoverFacingKnown = false;
    // A player program has RUN in this area: the hand-over is its ending,
    // not its absence. SCENE 55's startup script opens with `camera.set 0`
    // before any beat starts, so a camera-only signal fired a frame into
    // the Impasse with GRID's floor and the DB record from before
    // `player.become 49` - the first headless run showed exactly that.
    // Reset on every area change; an area whose scene has no programs at
    // all (a plain arrival) needs no beat to end.
    bool  playerDrivenSeen = false;
    int   playerDrivenArea = -1;
    double frameSec = 1.0 / 30.0;
    // the `--hold` stream, parsed into (keys, frames) runs
    struct HoldRun { std::vector<int> keys; int frames = 0; };
    std::vector<HoldRun> holds;
    {
        std::string cur;
        for (char c : holdStream + ",") {
            if (c != ',') { cur.push_back(c); continue; }
            if (cur.empty()) continue;
            HoldRun r;
            const auto star = cur.find('*');
            const std::string head = star == std::string::npos ? cur : cur.substr(0, star);
            r.frames = star == std::string::npos ? 1 : std::atoi(cur.c_str() + star + 1);
            if (!head.empty() && head[0] == 'k') {
                std::string k;
                for (char h : head.substr(1) + "+") {
                    if (h == '+') { if (!k.empty()) r.keys.push_back(std::atoi(k.c_str())); k.clear(); }
                    else k.push_back(h);
                }
            }
            holds.push_back(r);
            cur.clear();
        }
    }
    long handoverFrame = -1;

    // ---- the INTERFACE SOUNDS.
    //
    // `docs/UI.md`: every screen names up to twelve, and the slots are
    // POSITIONAL - 0 is the selection move, 1 the confirm, 2 the screen
    // opening. The start menu's are 1, 2, 0, which the table resolves to
    // `men002`, `men003`, `men001`. Both halves were already lifted and
    // nothing was playing them.
    std::vector<float> sndMove, sndConfirm, sndBack;
    const auto loadSlot = [&](int screen, int slot) {
        const std::string& nm = w.soundName(screen, slot);
        if (nm.empty()) return std::vector<float>{};
        const auto path = fs.resolve("I2D/sounds/" + nm + ".wav");
        if (!path) return std::vector<float>{};
        return wavToDevice(omk::DataFs::readPath(*path), 44100);
    };

    SdlFrontend front;
    comp.setDisplay(dispW, dispH);
    std::printf("display %dx%d (the interface is authored at 640x480 and "
                "scaled by I2D_ScaleX/Y)\n", dispW, dispH);

    // ---- THE RENDERER ---------------------------------------------------
    //
    // Vulkan when the machine has it, the software reference otherwise - and
    // the fallback is not a courtesy, it is `PORTING` A1's closing rule: a bare
    // checkout with no SDK must build and pass the whole suite. `--software`
    // forces the reference, which is the half every check is written against.
    //
    // The window mode is the awkward part and it is Vulkan's, not a choice
    // here: a window carrying a Vulkan surface cannot also carry an
    // `SDL_Renderer`, so the texture-upload path and the swapchain are
    // mutually exclusive and the decision has to be made before the window
    // exists. The order below is likewise Vulkan's - the instance needs the
    // window system's extensions, the surface needs the instance, and the
    // device's queue choice needs the surface.
    //
    // **What the GPU draws here is the 3D only.** The interface, the subtitle
    // and the menu are the ported I2D layer and stay on the CPU (`renderer.h`
    // says why: a Blt is a memory copy and there is nothing a GPU would make
    // more correct), so a frame is composed as before - the 3D read back into
    // the framebuffer, the 2D drawn over it - and `presentSurface` uploads the
    // finished picture through the swapchain. That readback is the cost the
    // scene viewer's `--vulkan` avoids by presenting the attachment directly,
    // and it cannot be avoided while anything is composited on the CPU.
    SDL_Window* vkWin = nullptr;
    omk::Renderer* vkRen = nullptr;
#if defined(OMK_VULKAN)
    if (!forceSoftware) {
        if (SDL_Init(SDL_INIT_VIDEO) == 0) {
            vkWin = SDL_CreateWindow("OMK Engine (vulkan)",
                                     SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     dispW, dispH, SDL_WINDOW_VULKAN);
        }
        if (vkWin) {
            unsigned nx = 0;
            SDL_Vulkan_GetInstanceExtensions(vkWin, &nx, nullptr);
            std::vector<const char*> ext(nx);
            SDL_Vulkan_GetInstanceExtensions(vkWin, &nx, ext.data());
            omk::Renderer* vr = omk::makeVulkanRenderer();
            omk::vulkanNeedExtensions(vr, ext.data(), nx);
            void* inst = omk::vulkanCreateInstance(vr);
            VkSurfaceKHR surf{};
            if (inst &&
                SDL_Vulkan_CreateSurface(vkWin, static_cast<VkInstance>(inst), &surf) &&
                omk::vulkanAttachSurface(vr, reinterpret_cast<unsigned long long>(surf)) &&
                vr->init(dispW, dispH)) {
                vkRen = vr;
                std::printf("renderer: VULKAN - %s\n", omk::vulkanDeviceName(vr));
            } else {
                delete vr;
                SDL_DestroyWindow(vkWin); vkWin = nullptr;
                std::printf("renderer: no Vulkan device - the software "
                            "reference\n");
            }
        }
    }
#endif
    if (!vkRen && !front.open(dispW, dispH, "OMK Engine (software)")) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    if (!vkRen) std::printf("renderer: the software reference\n");
    // One place that decides where a finished framebuffer goes, so the movies,
    // the splash and the frame loop cannot drift apart about it.
    const auto present = [&](const omk::Surface& pic) {
#if defined(OMK_VULKAN)
        if (vkRen) { omk::vulkanPresentSurface(vkRen, pic); return; }
#endif
        front.present(pic);
    };
    // The name field is real typing, so ask the host for characters.
    SDL_StartTextInput();
    // Queued, not mixed: a menu plays one blip at a time and the device is a
    // FIFO. Flushing first keeps them prompt - a blip that waits behind the
    // previous one arrives after the selection has already moved on.
    // Mixed OVER whatever is streaming, not queued behind it and not flushing
    // it - which is what lets a blip and the music coexist.
    const auto blip = [&](const std::vector<float>& v) { front.playSound(v); };

    // ---- THE MUSIC.
    //
    // Which track and whether it loops are the SCRIPT's decisions: AREA 118's
    // startup script reaches `music.play 109, loop` before the `ui.open` that
    // raises the menu, so the music arrives the same way the menu does. The
    // area header's own default (`AREA +142`) is 0 here - silent - so nothing
    // but the script names it.
    //
    // The streaming and the LOOP live in `src/audio/music.h`, not here. A
    // frontend is a device; it must not be deciding when a track restarts.
    const auto adpcmTables = omk::AdpcmTables::loadJson(tb + "/adpcm.json");
    omk::MusicPlayer music(44100);
    int playingTrack = -1;
    // The cutscene VOICES - `media.play` (op 92). `sub_41B200` plays one
    // through the morph streamer after a `Morph_Stop()`, so ONE at a time and
    // a second cuts the first (src/audio/voiceover.h).
    omk::VoiceOverLibrary voiceLib;
    voiceLib.load(fs);
    omk::VoiceOverPlayer voices(voiceLib);
    int voiceOverShot = -1;

    // ---- THE WORLD ------------------------------------------------------
    //
    // What is on screen after the menu is the SCRIPT's answer, not this
    // file's, and it arrives in two halves the Session already resolves:
    //
    //   * the DECOR SET is the resident area header's `+88` - the `.3DO` stem
    //     `Area_LoadSet` builds `MESHES\DECORS\%s.3DO` from. AREA 118, the
    //     one `IAM\START` starts in, names `GRID`; its startup script ends
    //     with `area.goto 222`, which is `AIMPASSE`, so the set changes
    //     underneath without this loop asking for it.
    //   * the CAMERA is whichever id the script's last `camera.set` /
    //     `camera.set.wait` named, resolved through `Camera_FindWorld`'s three
    //     tables and moved over the frames its second field asks for
    //     (`src/o3de/worldcam.h`).
    //
    // So there is nothing here to choose. This loads what the Session names
    // and draws through the camera the Session hands it, at whatever the
    // display size is - a horizontal fov and `tanv = tanh / (W/H)`, so the
    // aspect is the window's and no resolution is baked in.
    //
    // **The letterbox is NOT applied**, and that is deliberate: the 1.818:1
    // strip is measured off DIALOGUE captures and a reader watching the
    // original confirmed it belongs to conversations and cutscenes, not to
    // free play. Imposing it here would be generalising a camera-mode property
    // to all rendering, which is the mistake `--letterbox` exists to avoid in
    // the scene viewer.
    //
    // **Known gaps, stated rather than hidden**: `RCamera` carries no ROLL, so
    // the 1155 world cameras with a non-zero one are drawn upright (4226 of
    // 5381 have roll 0, AREA 118's six among them); there are no characters,
    // no props and no `.SCX` scene objects in the picture, only the decor set;
    // and the frame is drawn by the software reference rasterizer, which
    // `PORTING` B6 carries as a reference implementation and not as a port.
    // ---- THE SPEAKER --------------------------------------------------
    //
    // A conversation's cameras all aim at the character speaking it, so
    // without him they aim at nothing and the frame is black - which is what
    // the replica drew. Three things resolve him, and all three are the
    // engine's own:
    //
    //   * WHICH model: the DIALOG chunk's word 0 is the speaker's actor id,
    //     and the 276-byte actor record carrying that id at +272 names the
    //     model at +144 (`sub_40B190` is the scan);
    //   * WHERE he stands: the least-squares convergence of the line cameras'
    //     rays, dropped onto the walkable floor (`actor/speaker.h`);
    //   * HOW he is posed: the line's own `.3DM`, whose per-frame node
    //     quaternions compose down the mesh hierarchy (`actor/pose.h`).
    //
    // The pose advances with the VOICE - frame = elapsed * 30 - because that
    // is the clock the line runs on.
    // What is left here belongs to the LINE, not to a body: the model name,
    // the `.3DM` and its face vertices, the voice, and the camera solve that
    // says where a speaker no scene object drives is standing. The GEOMETRY
    // moved into `Staged`/`CharModel` above, one per actor (issue 41).
    std::vector<omk::Mesh> speakerMeshes;
    omk::NodeTracks speakerTracks;
    // The scene clip's frame the last time it was drawn, and the frame it was
    // on when the current line began - `Morph_Play` hands the morph player
    // the actor's clip AND its frame (rec[47]), and the blend-in eases from
    // that frame into the line (pose.h, BLENDING TWO POSES).
    float sceneFrameLast = 0.0f, lineIdleFrame = 0.0f;
    std::vector<std::byte> speakerMorph;   // the line's .3DM, for the FACE
    std::string speakerModel, speakerVoice;
    int voiceShot = -1;   // the line's voice in the mixer, for the press that cuts it
    float speakerAt[3] = {0, 0, 0};      // the camera solve, a GROUND point
    bool  speakerSolved = false;
    bool  speakerReady = false;
    int   speakerConv = -1;

    // ---- EVERY BODY THE SESSION SAYS IS ON SCREEN (issue 41) -----------
    //
    // `Actor_Attach` (0x0041CCA0) puts an actor in the o3de tree and
    // `Render_Scene` draws them ALL. This file used to keep exactly one
    // `speaker*` set - the model of `session.shown().front()`, posed by the
    // LAST running `"actor"` program whichever actor that program drives - so
    // the Impasse drew one passer-by performing Kay'l's arrival. What follows
    // is one `Staged` per shown actor, its pose resolved per actor, in the
    // engine's own precedence:
    //
    //   (a) a RUNNING scene program whose `Started.actor` is this actor -
    //       `ScriptObject_StartOnActor` (0x0041BA80) drives ITS actor - and
    //       then its clip on its authored `.3DP` path (`Path_Sample`);
    //   (b) else, this actor being the conversation's speaker, the line's own
    //       `.3DM` and its face, exactly as before;
    //   (c) else the IDLE. `Actor_LoadModel` -> `Actor_LoadBankList` leaves
    //       the channel on the default group's default entry
    //       (`Cef_DefaultGroup` = the group whose flags bit 0 is set,
    //       `Cef_DefaultEntry` = its 0x20 entry), and that entry's clip is
    //       what he stands in. This takes FRAME 0 of it and does not tick a
    //       channel per actor - a still idle, labelled, not a claim.
    //
    // The model and the bank are shared by NAME - three passers-by are one
    // `PA1_FN` - because the pool is only 64 slots wide (a bucket key's low
    // six bits, ASSETS 4b) and because the engine's own texture cache matches
    // on the name alone.
    struct CharModel {
        omk::Geometry rest;
        std::vector<omk::Mesh> meshes;
        std::vector<omk::Texture> tex;
        omk::FaceMesh face;
        // The HIERARCHY ROOT - the pelvis in all 181 character models, the
        // mesh whose parent id resolves to nothing. `composePose` leaves a
        // root at its AUTHORED position, and the models are not authored
        // about the origin: `UBassin` is at (2.9, -2.4, 17.9) but `D1Bassin`
        // is at (507.1, -168.1, 39.2), so a body placed by its model origin
        // is 507 units up the alley. The placement names the PELVIS, so the
        // pelvis is what is moved onto it.
        int root = -1;
        std::size_t texBase = 0;      // its first slot in the pool
        bool ready = false;
    };
    struct CharBank {
        omk::CtlFile ctl;
        std::vector<std::byte> data;
        int  idleClip = -1;           // the default group's default entry's
        bool ready = false;
    };
    // `std::map` is node-based, so a `Staged`'s pointer into these survives
    // every later insert.
    std::map<std::string, CharModel> charModels;
    // THE WORLD'S PROPS. One model per OBJECTS stem, from
    // `MESHES/OBJETS/<stem>.3DO` - `Object_ModelPath` (0x0040BAF0) copies the
    // record's `+14` and appends ".3DO". A prop is static: no pose, just the
    // placement `Object_SetPlacement` gives its node, so the rest geometry is
    // transformed into world space per frame and submitted like any batch.
    struct PropModel {
        omk::Geometry rest;
        std::vector<omk::Texture> tex;
        std::size_t texBase = 0;
        // THE MODEL'S OWN ORIGIN. `buildGeometry` bakes each mesh's authored
        // `pos` into its corners, exactly as it does for a decor set, so a
        // model is NOT centred on nothing: `ANNEAU` is a 4.6-unit ring whose
        // corners run x 503.4..508.0, y -177.4..-173.1, z 13.2..15.0 about a
        // mesh position of (505.7, -175.3, 14.1). Adding the placement on top
        // of that put the rings ~500 units up the alley. The placement names
        // where the object's ROOT goes, so the root is what is moved onto it -
        // the same correction the scripted crates needed.
        float origin[3] = {0, 0, 0};
        bool ready = false;
    };
    std::map<std::string, PropModel> propModels;
    omk::Geometry propGeo;                 // the shown props, in world space
    // Which model each of `propGeo`'s batches came from: the geometry is
    // built before the pool assigns the sections their bases, so a batch's
    // slot is resolved at submission through its owner.
    std::vector<const PropModel*> propBatchOwner;
    std::set<int> propsTold;               // one line per prop, not per frame
    std::map<std::string, CharBank>  charBanks;
    struct Staged {
        int actor = -1;
        std::string model, bank;
        CharModel* mo = nullptr;
        CharBank*  bk = nullptr;
        omk::Geometry posed;
        float at[3] = {0, 0, 0};
        float facing = 0.0f;
        bool  placed = false;   // something authored says where he stands
        bool  pelvis = false;   // ...and it names his PELVIS, not his feet
        bool  seen = false;
        bool  drawn = false;
        int   sceneClipWas = -2;      // the program's clip, cached
        omk::NodeTracks sceneTracks;
        omk::NodeTracks idle;         // the bank's default clip, frame 0
        bool  idleBuilt = false;
        float drawAt[3] = {0, 0, 0};   // where he was actually put, for a set piece
        // The placement a running scene PROGRAM gives him (a path sample or
        // the clip's root key 0), cached so it can be re-asserted every
        // frame - a `fromTable` body's per-frame reset to its 20-byte record
        // would otherwise win on every frame but the clip-change one, and he
        // would stand at the placement plus the whole root motion.
        bool  progPlaced = false;      // a program placed him this clip
        float progYaw = 0.0f;          // the call's Euler y (`Actor_SetEuler(node, p4, p5, p6)` every tick)
        bool  progYawKnown = false;
        float progBase[3] = {0, 0, 0};
        bool  progPelvis = false;
        const char* src = "none";
        omk::HeadLook look;            // `character.look_at_player`'s head aim, eased
        bool  lookSnap = true;
        // SEATED ONCE, the way the engine seats an actor once and then
        // `Actor_MoveBy`s him: the feet go on the floor when the pose SOURCE
        // or the clip changes, and the clip's root motion moves him from
        // there. Re-seating every frame would cancel every vertical the clip
        // has - a jump would slide along the ground instead of leaving it.
        float seatFeet = 0.0f;
        int   seatClip = -3;
        const char* seatSrc = "";
        bool  seatKnown = false;
        bool  placeTold = false, groundTold = false;
    };
    // OWNING POINTERS, not a vector of values: the Vulkan backend caches a
    // vertex buffer by (pointer, revision), so a `Staged` may never be moved
    // by a reallocation. Dropping one frees its address, which a later one
    // could reuse - `posed.revision` is taken from the global `worldGeoRev`
    // every frame it is drawn, so a reused address can never carry a
    // revision the backend has already seen.
    std::vector<std::unique_ptr<Staged>> staged;
    // THE PROCEDURAL PEDESTRIANS (docs/STREET_LIFE.md 2): one body per
    // walker of `session.sliders()`, its model shared through
    // `charModels`, posed from the crowd library's clip at the walker's own
    // clock, stood with its feet on the walker's body point and turned to its
    // heading. The engine draws a pedestrian through four LOD objects out to
    // `kLodDistances[3]` (40 m) and nothing beyond; this draws the full model
    // inside that distance and nothing beyond, so a street at density 3 in
    // Anekbah is 200 walkers of which a camera sees a few dozen.
    struct PedStaged {
        CharModel* mo = nullptr;
        // A crowd model carries FOUR skeletons - `PhBassin`, `PiBassin`, ...,
        // the LOD sub-objects `sub_453A70` splits it into (76 meshes in
        // PSH_FN, 19 a skeleton) - and the library's tracks name the first.
        // Drawing the whole model posed the first and left the other three at
        // rest, a T-pose inside every walker; so the rest geometry is cut to
        // the meshes under the root the tracks name, once per model.
        std::map<int, omk::Geometry>* lodRest = nullptr;
        omk::Geometry posed;
        const omk::NodeTracks* tracks = nullptr;
        const omk::PedClip* clipWas = nullptr;
        float feet = 0.0f;
        bool  feetKnown = false;
        bool  drawn = false;
    };
    std::vector<std::unique_ptr<PedStaged>> pedStaged;
    std::map<std::string, std::map<int, omk::Geometry>> pedLodRest;   // model -> root mesh -> its subtree's rest
    // The skeleton a set of tracks poses: the first track's mesh followed up
    // to its root. A model with one skeleton answers its only root.
    const auto skeletonRootOf = [](const CharModel& mo, const omk::NodeTracks& t) -> int {
        int m = -1;
        if (!t.ids.empty() && t.ids[0] >= 0)
            for (std::size_t j = 0; j < mo.meshes.size(); ++j)
                if (mo.meshes[j].index == t.ids[0]) { m = static_cast<int>(j); break; }
        if (m < 0) return mo.root;
        for (int guard = 0; guard < 64; ++guard) {
            const std::int32_t pid = mo.meshes[static_cast<std::size_t>(m)].parent;
            int next = -1;
            for (std::size_t j = 0; j < mo.meshes.size(); ++j)
                if (mo.meshes[j].id == pid) { next = static_cast<int>(j); break; }
            if (next < 0) return m;
            m = next;
        }
        return m;
    };
    const auto hasSeveralSkeletons = [](const CharModel& mo) {
        int roots = 0;
        for (const auto& m : mo.meshes) {
            bool hasParent = false;
            for (const auto& p : mo.meshes) if (p.id == m.parent) { hasParent = true; break; }
            if (!hasParent) ++roots;
        }
        return roots > 1;
    };
    const auto lodRestFor = [&](const std::string& model, const CharModel& mo, int rootMesh) -> const omk::Geometry& {
        auto& per = pedLodRest[model];
        auto it = per.find(rootMesh);
        if (it != per.end()) return it->second;
        // the meshes whose ancestor chain ends at `rootMesh`
        std::vector<bool> keep(mo.meshes.size(), false);
        for (std::size_t i = 0; i < mo.meshes.size(); ++i) {
            int m = static_cast<int>(i);
            for (int guard = 0; guard < 64 && m >= 0; ++guard) {
                if (m == rootMesh) { keep[i] = true; break; }
                const std::int32_t pid = mo.meshes[static_cast<std::size_t>(m)].parent;
                int next = -1;
                for (std::size_t j = 0; j < mo.meshes.size(); ++j)
                    if (mo.meshes[j].id == pid) { next = static_cast<int>(j); break; }
                m = next;
            }
        }
        omk::Geometry g;
        g.batches.clear();
        for (const auto& b : mo.rest.batches) {
            omk::Batch nb = b;
            nb.start = g.corners.size(); nb.count = 0;
            for (std::size_t c = b.start; c + 3 <= b.start + b.count; c += 3) {
                const auto mi = mo.rest.cornerMesh[c];
                if (mi < 0 || static_cast<std::size_t>(mi) >= keep.size() || !keep[static_cast<std::size_t>(mi)]) continue;
                for (int k = 0; k < 3; ++k) {
                    g.corners.push_back(mo.rest.corners[c + static_cast<std::size_t>(k)]);
                    g.cornerMesh.push_back(mo.rest.cornerMesh[c + static_cast<std::size_t>(k)]);
                    if (!mo.rest.cornerVertex.empty()) g.cornerVertex.push_back(mo.rest.cornerVertex[c + static_cast<std::size_t>(k)]);
                    if (!mo.rest.cornerDeclared.empty()) g.cornerDeclared.push_back(mo.rest.cornerDeclared[c + static_cast<std::size_t>(k)]);
                }
                nb.count += 3;
            }
            if (nb.count) g.batches.push_back(nb);
        }
        return per.emplace(rootMesh, std::move(g)).first->second;
    };
    // THE ROAD TRAFFIC (docs/STREET_LIFE.md 2b, actor/vehicles.cpp). A vehicle
    // is far simpler to stage than a walker: it has no clip and no skeleton -
    // `sub_456C70` moves a POINT and `sub_437F80` puts the instance on it - so
    // the geometry is composed once at rest and only transformed per frame.
    struct VehStaged {
        CharModel* mo = nullptr;
        omk::Geometry atRest;      // the chosen sub-object, composed, in model space
        omk::Geometry posed;       // ...that, placed in the world this frame
        int   lodRoot = -1;
        float origin[3] = {0, 0, 0};
        bool  built = false;
        bool  drawn = false;
    };
    std::vector<std::unique_ptr<VehStaged>> vehStaged;
    int vehDrawn = 0, vehLive = 0, vehStopped = 0;
    long vehTold = -1;
    // `sub_453A70`: the model's root sub-objects sorted by vertex+face count
    // DESCENDING - the LOD ladder. Sub-object 0 is what `sub_4544B0` hands
    // ambient traffic (`v16[1]`); the reserved slider takes sub-object 1
    // (`v16[2]`), which is read from the call sites, NOT judged by eye, and
    // not drawn here because the player's ride is not ported.
    const auto heaviestRootOf = [](const CharModel& mo) -> int {
        int best = mo.root;
        std::size_t bestWeight = 0;
        for (std::size_t i = 0; i < mo.meshes.size(); ++i) {
            bool hasParent = false;
            for (const auto& q : mo.meshes) if (q.id == mo.meshes[i].parent) { hasParent = true; break; }
            if (hasParent) continue;
            // the subtree's weight, the counts `sub_453A70` adds (+44 and +48)
            std::size_t w = 0;
            for (std::size_t j = 0; j < mo.meshes.size(); ++j) {
                int m = static_cast<int>(j);
                for (int guard = 0; guard < 64 && m >= 0; ++guard) {
                    if (static_cast<std::size_t>(m) == i) {
                        w += static_cast<std::size_t>(mo.meshes[j].vertices) +
                             static_cast<std::size_t>(mo.meshes[j].triangles) +
                             static_cast<std::size_t>(mo.meshes[j].quads);
                        break;
                    }
                    const std::int32_t pid = mo.meshes[static_cast<std::size_t>(m)].parent;
                    int next = -1;
                    for (std::size_t k = 0; k < mo.meshes.size(); ++k)
                        if (mo.meshes[k].id == pid) { next = static_cast<int>(k); break; }
                    m = next;
                }
            }
            if (w > bestWeight) { bestWeight = w; best = static_cast<int>(i); }
        }
        return best;
    };
    std::map<std::pair<int, int>, omk::NodeTracks> pedTracks;   // (sex, clip slot) -> its tracks
    std::vector<std::byte> pedAni;
    std::string pedAniName;
    int pedDrawn = 0, pedLive = 0, pedInAction = 0, pedIdle = 0;
    long pedTold = -1;
    const auto pedTracksFor = [&](int sex, const omk::PedClip& c, const std::vector<omk::Mesh>& meshes)
        -> const omk::NodeTracks* {
        // `PlayerController::poseTracks`'s recipe over the crowd library: the
        // descriptor's tracks resolve to meshes by name, key 0 is the rest
        // sentinel so frame f reads key f + 1
        const auto key = std::make_pair(sex, c.slot);
        auto it = pedTracks.find(key);
        if (it != pedTracks.end()) return it->second.valid() ? &it->second : nullptr;
        omk::NodeTracks t;
        const auto d = omk::animDescriptor(pedAni, c.descriptor);
        if (d && d->frames > 0 && !meshes.empty()) {
            const auto lower = [](std::string v) {
                for (auto& ch : v) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
                return v;
            };
            t.count = static_cast<int>(d->tracks.size());
            t.frames = d->frames;
            t.rootTrack = -1;
            // THE BONE NAMES CARRY A TWO-LETTER SKELETON PREFIX and the library
            // does not share it with every model: the men's clips say
            // `PhBassin`, the women's idle `ShBassin`, Jaunpur's men are
            // `KhBassin` and their women `FhBassin`. Matched by the whole
            // name, a woman idled and every Jaunpur man walked in a T-pose
            // (a reader's frame, 2026-09-03). The bone is the name after the
            // prefix, resolved inside the FIRST skeleton - the exact name is
            // tried first, for the one model whose prefix does agree.
            const auto suffix = [&](const std::string& n) { return n.size() > 2 ? lower(n.substr(2)) : lower(n); };
            int firstRoot = -1;
            for (std::size_t j = 0; j < meshes.size() && firstRoot < 0; ++j) {
                bool hasParent = false;
                for (const auto& p : meshes) if (p.id == meshes[j].parent) { hasParent = true; break; }
                if (!hasParent) firstRoot = static_cast<int>(j);
            }
            const auto underFirst = [&](std::size_t j) {
                int m = static_cast<int>(j);
                for (int guard = 0; guard < 64 && m >= 0; ++guard) {
                    if (m == firstRoot) return true;
                    const std::int32_t pid = meshes[static_cast<std::size_t>(m)].parent;
                    int next = -1;
                    for (std::size_t q = 0; q < meshes.size(); ++q) if (meshes[q].id == pid) { next = static_cast<int>(q); break; }
                    m = next;
                }
                return false;
            };
            for (const auto& tr : d->tracks) {
                std::int32_t mi = -1;
                const std::string want = lower(tr.name);
                for (const auto& m : meshes) if (lower(m.name) == want) { mi = m.index; break; }
                if (mi < 0) {
                    const std::string bone = suffix(tr.name);
                    for (std::size_t j = 0; j < meshes.size(); ++j)
                        if (suffix(meshes[j].name) == bone && underFirst(j)) { mi = meshes[j].index; break; }
                }
                t.ids.push_back(mi);
            }
            t.quats.assign(static_cast<std::size_t>(d->frames), {});
            t.trans.assign(static_cast<std::size_t>(d->frames), {0.0f, 0.0f, 0.0f});
            for (int f = 0; f < d->frames; ++f) {
                auto& row = t.quats[static_cast<std::size_t>(f)];
                row.resize(d->tracks.size());
                for (std::size_t i = 0; i < d->tracks.size(); ++i) {
                    const omk::AnimTrack& tr = d->tracks[i];
                    if (!tr.rotOffset || tr.rotKeys <= 0) continue;
                    int k = f + 1;
                    if (k >= tr.rotKeys) k = tr.rotKeys - 1;
                    const std::size_t o = tr.rotOffset + 16u * static_cast<std::size_t>(k);
                    if (o + 16 > pedAni.size()) continue;
                    float q[4];
                    std::memcpy(q, pedAni.data() + o, 16);
                    row[i] = {q[0], q[1], q[2], q[3]};
                }
            }
        }
        it = pedTracks.emplace(key, std::move(t)).first;
        return it->second.valid() ? &it->second : nullptr;
    };
    long stagedEver = 0;                 // for the summary line
    std::vector<int> stagedIds;
    // The pool is rebuilt on a COMPOSITION change, not on a size change: two
    // models with the same texture count swapping is exactly what a size test
    // cannot see.
    std::uint64_t poolComposition = 1, poolBuiltFor = 0, poolTold = 0;
    bool poolHasSprites = false, poolHasPlayer = false;
    std::size_t playerTexBase = 0, spriteTexBase = 0;
    // sprite id -> its slot within the pool's sprite section, or -1
    std::vector<int> spriteSlot;
    // The sprite ids the resident scene can actually name - what goes in the
    // pool, as opposed to everything that decoded.
    std::set<int> spriteWanted, spritePooled;
    bool poolOverflowTold = false;
    const bool stagedProbe = std::getenv("OMK_STAGE_PROBE") != nullptr;
    // One `.3DO`/`.3DT` per MODEL NAME, loaded once and shared by every actor
    // wearing it.
    const auto charModelFor = [&](const std::string& name) -> CharModel* {
        if (name.empty()) return nullptr;
        auto it = charModels.find(name);
        if (it != charModels.end()) return &it->second;
        CharModel m;
        if (const auto mo = fs.resolve("MESHES/PERSOS/" + name + ".3DO")) {
            const auto md = omk::DataFs::readPath(*mo);
            m.rest = omk::buildGeometry(md, omk::DrawFilter::Engine);
            if (const auto mh = omk::readHeader(md)) m.meshes = omk::readMeshes(md, *mh);
            if (const auto mt = fs.resolve("MESHES/PERSOS/" + name + ".3DT"))
                m.tex = omk::textures(md, omk::DataFs::readPath(*mt));
            m.face = omk::faceMeshOf(m.meshes);
            for (std::size_t i = 0; i < m.meshes.size() && m.root < 0; ++i) {
                bool hasParent = false;
                for (const auto& p : m.meshes)
                    if (p.id == m.meshes[i].parent) { hasParent = true; break; }
                if (!hasParent) m.root = static_cast<int>(i);
            }
            m.ready = !m.rest.corners.empty() && !m.meshes.empty();
        }
        ++poolComposition;      // the pool gains this model's textures
        return &charModels.emplace(name, std::move(m)).first->second;
    };
    const auto propModelFor = [&](const std::string& stem) -> PropModel* {
        if (stem.empty()) return nullptr;
        auto it = propModels.find(stem);
        if (it != propModels.end()) return &it->second;
        PropModel m;
        if (const auto mo = fs.resolve("MESHES/OBJETS/" + stem + ".3DO")) {
            const auto md = omk::DataFs::readPath(*mo);
            m.rest = omk::buildGeometry(md, omk::DrawFilter::Engine);
            if (const auto mt = fs.resolve("MESHES/OBJETS/" + stem + ".3DT"))
                m.tex = omk::textures(md, omk::DataFs::readPath(*mt));
            // The HIERARCHY ROOT's position, the way a character model's
            // pelvis is found: the mesh whose parent resolves to nothing.
            if (const auto mh = omk::readHeader(md)) {
                const auto ms = omk::readMeshes(md, *mh);
                int root = -1;
                for (std::size_t i = 0; i < ms.size() && root < 0; ++i) {
                    bool hasParent = false;
                    for (const auto& q : ms)
                        if (q.id == ms[i].parent) { hasParent = true; break; }
                    if (!hasParent) root = static_cast<int>(i);
                }
                if (root >= 0)
                    for (int k = 0; k < 3; ++k)
                        m.origin[k] = ms[static_cast<std::size_t>(root)].pos[k];
            }
            m.ready = !m.rest.corners.empty();
            std::printf("prop model %s: %zu corners, %zu batches, %zu textures\n",
                        stem.c_str(), m.rest.corners.size(), m.rest.batches.size(),
                        m.tex.size());
        }
        ++poolComposition;
        return &propModels.emplace(stem, std::move(m)).first->second;
    };
    // One `.CTL` per BANK NAME, and the entry `Actor_LoadBankList` leaves the
    // channel on. `PlayerController`'s constructor is the rule quoted:
    // `rt_.loadModel()` then `SetPersoBankGroup(channel, Cef_DefaultGroup)`,
    // and `clipOwner()`'s chain - an entry whose flags carry 0x8002 is an
    // alias and hands the clip on through its GoTo.
    const auto charBankFor = [&](const std::string& name) -> CharBank* {
        if (name.empty()) return nullptr;
        auto it = charBanks.find(name);
        if (it != charBanks.end()) return &it->second;
        CharBank b;
        if (const auto cp = fs.resolve("ANIMS/" + name + ".CTL")) {
            b.data = omk::DataFs::readPath(*cp);
            b.ctl = omk::readCtl(b.data);
            b.ready = b.ctl.valid;
            int g = -1;
            for (std::size_t k = 0; k < b.ctl.groupList.size(); ++k)
                if (b.ctl.groupList[k].flags & 1u) { g = static_cast<int>(k); break; }
            int s = g >= 0 ? b.ctl.groupList[static_cast<std::size_t>(g)].defaultEntry : -1;
            for (int guard = 0; guard < 64; ++guard) {
                if (s < 0 || s >= static_cast<int>(b.ctl.states.size())) { s = -1; break; }
                if (!(b.ctl.states[static_cast<std::size_t>(s)].flags & 0x8002u)) break;
                s = b.ctl.states[static_cast<std::size_t>(s)].gotoIdx;
            }
            if (s >= 0 && s < static_cast<int>(b.ctl.states.size())) {
                const int c = b.ctl.states[static_cast<std::size_t>(s)].clip;
                if (c >= 0 && c < static_cast<int>(b.ctl.clips.size())) b.idleClip = c;
            }
        }
        return &charBanks.emplace(name, std::move(b)).first->second;
    };
    // ...and that clip as a pose, at FRAME 0. The recipe is
    // `PlayerController::poseTracks`'s, transcribed rather than reinvented: a
    // `.CTL` clip is an `.ani` DESCRIPTOR with no "3.0V" wrapper, its tracks
    // resolve to meshes by name, and KEY 0 IS THE REST SENTINEL so frame `f`
    // reads key `f + 1`. Only frame 0 is built, because nothing here ticks a
    // channel per actor.
    const auto idleTracksFor = [&](const CharBank& b,
                                   const std::vector<omk::Mesh>& meshes) {
        omk::NodeTracks t;
        if (b.idleClip < 0 || meshes.empty()) return t;
        const auto d = omk::animDescriptor(
            b.data, b.ctl.clips[static_cast<std::size_t>(b.idleClip)].offset);
        if (!d || d->frames <= 0 || d->tracks.empty()) return t;
        const auto lower = [](std::string v) {
            for (auto& c : v) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            return v;
        };
        t.count = static_cast<int>(d->tracks.size());
        t.frames = 1;
        t.rootTrack = -1;
        for (const auto& tr : d->tracks) {
            std::int32_t mi = -1;
            const std::string want = lower(tr.name);
            for (const auto& m : meshes)
                if (lower(m.name) == want) { mi = m.index; break; }
            t.ids.push_back(mi);
        }
        t.quats.assign(1, {});
        t.trans.assign(1, {0.0f, 0.0f, 0.0f});
        t.quats[0].resize(d->tracks.size());
        for (std::size_t i = 0; i < d->tracks.size(); ++i) {
            const omk::AnimTrack& tr = d->tracks[i];
            if (!tr.rotOffset || tr.rotKeys <= 0) continue;
            const int key = tr.rotKeys > 1 ? 1 : 0;      // frame 0 reads key 1
            const std::size_t o = tr.rotOffset + 16u * static_cast<std::size_t>(key);
            if (o + 16 > b.data.size()) continue;
            float q[4];
            std::memcpy(q, b.data.data() + o, 16);
            t.quats[0][i] = {q[0], q[1], q[2], q[3]};
        }
        return t;
    };
    std::vector<omk::Texture> pool;
    std::size_t poolSize = 0;

    // The 3D renderer, behind `PORTING` A2's boundary: the GPU one when the
    // machine has it, the software reference otherwise. Everything below
    // submits DECISIONS and never touches an API, which is what lets the two
    // be swapped by assigning a pointer.
    // ---- THE EFFECT SPRITES -------------------------------------------
    //
    // A section C effect names its sprite by an index into the GLOBAL library
    // `aventure.scx` registers - the 20 the boot already reports. GRID's
    // effects use 9..12, which land on EFFECTS2_SMOKE1, EFFECTS1_IMPACT1,
    // EFFECTS1_IMPACT2 and EFFECTS1_M16D; the scene's own chunk 4 re-registers
    // copies of the same files, so the global one is what the index means.
    //
    // Each sprite is a whole `.3DO` in the stream immediately followed by its
    // `.3DT`, so the pair is read straight out of the same buffer.
    // Keyed by the sprite's ID, because that is what an effect names -
    // `sub_4A5800(scene + 8, id)` walks the scene's 36-byte registry rows
    // matching `+32`. Indexing a library instead lands on the wrong sprite:
    // for GRID it swaps IMPACT1 and IMPACT2 and turns `burn`'s smoke into a
    // muzzle flash, which is why the portal came out fire-orange.
    //
    // The GLOBAL library loads first and the SCENE's over it, since the tick
    // resolves through the scene when there is one.
    std::vector<omk::Texture> spriteTex;
    std::vector<omk::SpriteFrames> spriteFr;
    const auto loadSprites = [&](const std::string& scx) {
        const auto ap = fs.resolve("SCPTDATA/" + scx);
        if (!ap) return 0;
        const auto ad = omk::DataFs::readPath(*ap);
        const auto st = omk::readScxStream(ad);
        int n = 0;
        for (const auto& sp : st.sprites) {
            if (sp.id < 0) continue;
            const auto slot = static_cast<std::size_t>(sp.id);
            if (spriteTex.size() <= slot) {
                spriteTex.resize(slot + 1);
                spriteFr.resize(slot + 1);
            }
            if (!sp.model || !sp.texture ||
                sp.offset + sp.model + sp.texture > ad.size()) continue;
            const std::span<const std::byte> mo(ad.data() + sp.offset, sp.model);
            const std::span<const std::byte> te(ad.data() + sp.offset + sp.model,
                                                sp.texture);
            auto t = omk::textures(mo, te);
            if (!t.empty()) spriteTex[slot] = t.front();
            spriteFr[slot] = omk::spriteFrames(mo);
            ++n;
        }
        return n;
    };
    // WHICH scene's sprites `spriteTex` currently holds. An effect names its
    // sprite by ID and `Sfx_TickAmbient` resolves that id through the SCENE
    // (`sub_4A5800`), and the ids are scene-local: `Grid.sfx` wants 9..12 and
    // `Grid.SCX` registers exactly those, `anekbah.sfx` wants 49589..49591 and
    // `anekbah.SCX` registers exactly those. Loading one scene's sprites ONCE
    // at boot and then changing scene leaves every later effect resolving
    // against the wrong table - Anekbah's three ids fall outside it entirely
    // and draw nothing, which is fire and smoke not working, while the
    // Impasse's 13/14/114... collide with the GLOBAL library's and draw the
    // wrong picture, which is `todo/omk-play.md` 48.
    std::string spriteScx;
    {
        const int glob = loadSprites("aventure.SCX");
        // ...then the SCENE's own, which is what `Sfx_TickAmbient` resolves
        // against and which wins where the ids collide.
        const int local = session.scene().file().empty()
                            ? 0 : loadSprites(session.scene().file());
        int okTex = 0; std::size_t frames = 0;
        for (const auto& t : spriteTex) if (t.width) ++okTex;
        for (const auto& fr : spriteFr) frames += fr.frames.size();
        std::printf("sprites: %d global + %d from %s, %d decoded over ids "
                    "0..%zu, %zu frames in all\n", glob, local,
                    session.scene().file().c_str(), okTex,
                    spriteTex.empty() ? 0 : spriteTex.size() - 1, frames);
        spriteScx = session.scene().file();
    }
    // Re-run that whenever the resident scene changes. The global library is
    // reloaded first because the scene's ids WIN where the two collide, which
    // is the order `Sfx_TickAmbient` resolves in.
    const auto refreshSprites = [&]() {
        if (session.scene().file() == spriteScx) return;
        spriteScx = session.scene().file();
        spriteTex.clear();
        spriteFr.clear();
        const int glob = loadSprites("aventure.SCX");
        const int local = spriteScx.empty() ? 0 : loadSprites(spriteScx);
        int okTex = 0;
        for (const auto& t : spriteTex) if (t.width) ++okTex;
        std::printf("sprites: reloaded for %s - %d global + %d local, %d decoded\n",
                    spriteScx.empty() ? "<none>" : spriteScx.c_str(), glob, local, okTex);
        ++poolComposition;          // the sprite section of the pool changed
    };

    omk::SoftwareRenderer worldSw;
    omk::Renderer& world = vkRen ? *vkRen : static_cast<omk::Renderer&>(worldSw);
    bool worldReady = false;
    // THE SHOWN DECORS - one per resident slot, because TWO are drawn while
    // the player walks between areas. `Area_Transition`'s completion arm puts
    // the destination in state 2 and nothing hides the origin until
    // `area.arrive` (area.h, `Session::slotShown`); until 2026-09-03 this
    // file drew `session.setName()` alone, so the frame after the airlock
    // transition was black: the airlock set at x 7415..8149 through a follow
    // camera on a player still standing in the alley at x 6705. Keyed by
    // SLOT at stable addresses, since the Vulkan backend caches a vertex
    // buffer by pointer and revision.
    struct WorldSlot {
        std::string stem;
        int area = -1;
        omk::Geometry geo;
        std::vector<omk::Texture> tex;
        omk::MirrorPlane mirror;
        omk::TriangleSoup soup;      // its WALKABLE soup: the feet's decor probe
        // and the faces PAST the slope limit: `Walk_GroundResponse` stands the
        // actor on a steep face and slides him off it, so dropping them made a
        // slope a hole with no floor in any direction (omk-play 67)
        omk::TriangleSoup steep;
        // The set's own meshes, kept so a scripted motion can name one, and
        // the corners as BUILT, so a motion patches the original rather than
        // accumulating on the last frame's patch.
        std::vector<omk::Mesh>   meshes;
        std::vector<omk::Corner> baseCorners;
        // ...and the collision soups the same way: the mesh each triangle
        // came from, and the soups as BUILT, so a moved crate's collision
        // follows the crate (a reader, 2026-09-04: "some crates fall at a
        // moment. It looks like their colliders stay at their initial
        // position" - the sweep made rest-baked collision visible).
        std::vector<int>  soupMesh, steepMesh;
        omk::TriangleSoup baseSoup, baseSteep;
    };
    std::array<WorldSlot, 2> worldSlots;
    std::string worldSet;            // the ACTIVE slot's stem - the set under his feet
    std::size_t worldTexBase[2] = {0, 0};   // each slot's first index in `worldTex`
    std::vector<omk::DecorSoup> worldDecors; // the shown slots' soups, for decorUnder
    // A set's geometry is REBUILT on every area change, and a fresh
    // Geometry's revision is 0 - the same value the previous set was cached
    // under by the Vulkan backend, which keys its vertex buffer on the
    // POINTER and the revision. So the Impasse drew GRID's tunnel through the
    // Impasse's batch ranges: black with a few stray triangles where the
    // software renderer, which reads the Geometry directly, drew the alley.
    // The particle geometry hit the identical fault on 2026-09-02; the cure
    // is the same - a revision that only ever climbs.
    std::uint64_t worldGeoRev = 0;
    std::vector<omk::Texture> worldTex;     // the shown slots' textures, slot 0 first
    long worldFrames = 0;
    // The particles' quads, REBUILT every frame into this one object. It lives
    // outside the frame loop so `Geometry::revision` accumulates: the Vulkan
    // backend caches a vertex buffer by pointer and revision, and a
    // block-local Geometry had the same address and a revision of 1 on every
    // frame, so the GPU drew the first frame's particles for ever while the
    // software path animated - the posed-character bug over again, one level
    // out, because this geometry is rebuilt rather than mutated.
    omk::Geometry fxGeo;
    // Where the scene's character is this frame - his model origin, the
    // pelvis - for set pieces linked to him. `sub_450FC0` case 2 finds an
    // actor by the first THREE letters of its name, uppercased ('HO1' for
    // HO1_FNM, Kay'l); the intro's arrival piece is linked that way and
    // plays `kaylarr` and `kay arr` at his position as he lands. His frame
    // is the identity here because the facing is not applied (see
    // `SceneRunner::Started::euler`), and the position is LAST frame's -
    // the runner ticks before the pose is composed. Type 3 (the PLAYER,
    // `unk_8F5EA0`) has no counterpart in this viewer and is left to the
    // runner's absolute fallback, which setpiece.h labels.
    float actorAt[3] = {0, 0, 0};
    bool  actorKnown = false;
    session.sceneMutable().setPieceLinks(
        [&](int type, std::uint32_t id, omk::PieceLink& L) -> bool {
            if (type != 2) return false;
            const char tag[4] = {static_cast<char>((id >> 16) & 0xFF),
                                 static_cast<char>((id >> 8) & 0xFF),
                                 static_cast<char>(id & 0xFF), 0};
            const auto tagged = [&](const std::string& m) {
                if (m.size() < 3) return false;
                for (int k = 0; k < 3; ++k) {
                    char c = m[static_cast<std::size_t>(k)];
                    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
                    if (c != tag[k]) return false;
                }
                return true;
            };
            // EVERY body on screen is a candidate now, not just the one this
            // file used to draw - the piece is linked to the actor whose model
            // carries the tag.
            for (const auto& up : staged)
                if (up->drawn && tagged(up->model)) {
                    for (int k = 0; k < 3; ++k) L.pos[k] = up->drawAt[k];
                    L.hasMatrix = true;   // identity: the facing is unported
                    return true;
                }
            if (!actorKnown || !tagged(playerModel)) return false;
            for (int k = 0; k < 3; ++k) L.pos[k] = actorAt[k];
            L.hasMatrix = true;   // identity: the facing is unported
            return true;
        });
    int  lastCamera = -2;
    // One slot's decor in or out. The Geometry object stays where it is and
    // its revision climbs, so the backend refills rather than mistakes it.
    const auto loadWorldSlot = [&](int slot, const std::string& stem, int area) {
        WorldSlot& w = worldSlots[static_cast<std::size_t>(slot & 1)];
        w = WorldSlot{};
        w.geo.revision = ++worldGeoRev;
        if (stem.empty()) return;
        const auto o = fs.resolve("MESHES/DECORS/" + stem + ".3DO");
        if (!o) { std::printf("world: no set %s.3DO\n", stem.c_str()); return; }
        const auto d = omk::DataFs::readPath(*o);
        if (d.empty()) return;
        w.stem = stem;
        w.area = area;
        w.geo = omk::buildGeometry(d, omk::DrawFilter::Engine);
        w.geo.revision = ++worldGeoRev;
        const auto t = fs.resolve("MESHES/DECORS/" + stem + ".3DT");
        if (t) w.tex = omk::textures(d, omk::DataFs::readPath(*t));
        w.mirror = omk::mirrorPlane(d);
        w.soup = omk::collisionSoup(d, omk::SoupKind::Walkable, &w.soupMesh);
        w.steep = omk::collisionSoup(d, omk::SoupKind::Steep, &w.steepMesh);
        w.baseSoup.clear(); w.baseSteep.clear();
        if (const auto mh = omk::readHeader(d)) w.meshes = omk::readMeshes(d, *mh);
        // THE SET'S OWN EMITTERS - `Sfx_BindAmbientEffects`, the environment
        // family. Every mesh flagged 0x40000000 whose first four name bytes
        // match a section-D tag registers that binding's effect at the mesh's
        // position: the neon, the steam, the smoke. They come up with the SET,
        // not with any object, which is why nothing started them and why they
        // had never appeared here. 319 across the 12 sets that have any.
        if (const int n = session.sceneMutable().bindSetEmitters(d))
            std::printf("world: slot %d %s binds %d ambient emitters\n",
                        slot, stem.c_str(), n);
        // The Vulkan one is already initialised - its swapchain had to exist
        // before the window could be presented to at all.
        if (!worldReady) {
            if (!vkRen) worldSw.init(dispW, dispH);
            worldReady = true;
        }
        std::printf("world: slot %d set %s (AREA %d) - %zu corners, %zu batches, "
                    "%zu textures, %zu walkable triangles%s\n",
                    slot, stem.c_str(), area, w.geo.corners.size(), w.geo.batches.size(),
                    w.tex.size(), w.soup.size() / 9, w.mirror.found ? ", mirror" : "");
    };
    // After any slot changed: the texture pool (slot 0's first, then slot
    // 1's - a batch's material is offset by its slot's base), the decor list
    // the feet are probed against, and the WALKER'S soup. `playerSoup` is
    // refilled IN PLACE because the controller's walker holds a reference to
    // it: the player walks off one set onto the other without being rebuilt,
    // and his `.CTL` state, position and facing survive the transition the
    // way the engine's actor does (it is one record; only the decor changes).
    const auto rebuildWorld = [&]() {
        worldTex.clear();
        worldDecors.clear();
        playerSoup.clear();
        playerSteep.clear();   // rebuilt with the soup, or it accumulates
        for (int slot = 0; slot < 2; ++slot) {
            WorldSlot& w = worldSlots[static_cast<std::size_t>(slot)];
            worldTexBase[slot] = worldTex.size();
            if (w.stem.empty()) continue;
            worldTex.insert(worldTex.end(), w.tex.begin(), w.tex.end());
            worldDecors.push_back({w.area, &w.soup});
            playerSoup.insert(playerSoup.end(), w.soup.begin(), w.soup.end());
            playerSteep.insert(playerSteep.end(), w.steep.begin(), w.steep.end());
        }
        world.setTextures(worldTex);
        poolSize = worldTex.size();
        ++poolComposition;   // the character and sprite sections re-append over this
    };

    // ---- and now they PLAY -------------------------------------------
    //
    // Three MPEG-1 program streams at 320x240, doubled to the framebuffer.
    // `movies` is the boot chain's own order. LMENU skips them, which is the
    // engine's key and not this frontend's invention.
    // A frame budget is for the headless checks, and there the budget must
    // reach the SCREEN - otherwise the dump is a movie frame and the
    // live-vs-reference comparison compares the wrong thing. `--nofmv` is the
    // engine's own switch and is what the checks pass; this only guards the
    // case where someone bounds the frames and forgets.
    if (playMovies && !frames) {
        const char* movies[3] = {"FLIS/EIDOS.mpg", "FLIS/QUANTIC.mpg",
                                 "FLIS/GAME.mpg"};
        omk::Surface mv(dispW, dispH, 0);
        bool skipAll = false, bounded = false;
        for (const char* name : movies) {
            if (skipAll) break;
            const auto real = fs.resolve(name);
            omk::Movie mov;
            if (!real || !mov.open(*real)) {
                std::printf("  %s: not decodable, skipped\n", name);
                continue;
            }
            std::printf("  %s %dx%d %.2f s\n", name, mov.info().width,
                        mov.info().height, mov.info().duration);
            // 44100 stereo, the stream's own rate - NOT the engine's 22050
            // primary, which these never went through.
            front.openAudio(mov.info().sampleRate ? mov.info().sampleRate : 44100, 2);
            const double fps = mov.info().framerate > 0 ? mov.info().framerate : 30.0;
            long shown = 0;
            while (mov.nextFrame(mv)) {
                omk::HostInput h;
                if (!front.pump(h)) { skipAll = true; break; }
                // `docs/BOOT.md` 2: ANY key ends the movie playing, and left
                // ALT latches and ends all three. Accepting only ALT and ESC -
                // which is what this did - means a player pressing space or
                // return sits through the whole thing.
                if (!h.held.empty()) {
                    if (h.held.count(0x38)) skipAll = true;      // DIK_LMENU
                    break;
                }
                for (auto blk = mov.nextAudio(); !blk.empty(); blk = mov.nextAudio())
                    front.queueAudio(blk);
                present(mv);
                ++shown;

                // PACE BY THE AUDIO, not by a fixed delay. Sleeping 1000/fps
                // after each frame adds the DECODE time to every frame, so the
                // picture falls steadily behind a soundtrack that plays at its
                // own rate. The audio device is the only clock running at the
                // rate a person hears: what has been decoded, minus what is
                // still queued, is the moment being heard now. Wait only while
                // the picture is ahead of it, and never when it is behind.
                if (!frames) {
                    const double heard = mov.audioSeconds() - front.queuedSeconds();
                    const double ahead = shown / fps - heard;
                    if (ahead > 0.001 && ahead < 1.0)
                        SDL_Delay(static_cast<Uint32>(ahead * 1000.0));
                }
                if (frames && mov.framesDecoded() >= frames) {
                    skipAll = bounded = true; break;
                }
            }
            // Whatever the decoder ran ahead into is still in the device, and
            // a skipped movie must not go on playing under what follows.
            front.flushAudio();
        }
        std::printf("movies %s\n",
                    !skipAll        ? "played (any key skips one, ALT skips all)"
                    : bounded       ? "cut short by --frames"
                                    : "skipped (ALT)");
    }
    // THE AUDIO DEVICE IS THE WORLD'S TOO. `openAudio` was called in one
    // place - the movie player, at the movie's own rate - so a run with
    // `--nofmv` (or a street start, which skips the movies) never opened it
    // and every world sound was dropped without a word: no music, no
    // effects, no voices. A reader on 2026-09-04: "there is absolutely no
    // sound at all". The world converts everything to 44100 (`wavToDevice`),
    // so that is the rate it opens at; a device the movies already opened is
    // kept as it is (openAudio returns early).
    if (!frames) {
        if (front.openAudio(44100, 2))
            std::printf("audio: device open at 44100 Hz stereo for the world\n");
        else
            std::printf("audio: NO DEVICE (%s) - the world will be silent\n", SDL_GetError());
    }


    // ---- THE SPLASH SCREEN ------------------------------------------
    //
    // `Game_Main` shows it between `Game_Start("aventure.scx")` and
    // `Game_RunLoop`, which is exactly here:
    //
    //     push offset aAventureScx_3 ; "aventure.scx"
    //     call sub_41B5A0            ; Game_Start
    //     push offset aImagesOmikronB ; "IMAGES\OMIKRON.BMP"
    //     call sub_420A20            ; <- the splash
    //     call sub_439310            ; Game_RunLoop
    //
    // and `sub_420A20` is: load the bitmap, blit it over the whole screen,
    // present, free it, and **`Sleep(0x1388)` - five seconds**. It is a
    // blocking sleep, so unlike the movies it is NOT skippable, and that is
    // reproduced rather than improved on. Events are pumped through the wait
    // only so the host can close the window; no key shortens it.
    if (!frames) {
        const omk::Surface splash =
            omk::surfaceFromBmp(fs.read("IMAGES/OMIKRON.BMP"));
        if (splash.valid()) {
            // The splash is a 640x480 file and `Blt` stretches, which is
            // how the original puts it on a bigger display too.
            omk::Surface sf(dispW, dispH, 0);
            omk::blt(sf, {0, 0, dispW, dispH}, splash,
                     {0, 0, splash.w, splash.h}, omk::kBltWait);
            present(sf);
            std::printf("splash: IMAGES/OMIKRON.BMP, 5 s (Sleep(0x1388) - "
                        "not skippable in the original either)\n");
            const Uint32 until = SDL_GetTicks() + 5000;
            while (SDL_GetTicks() < until) {
                omk::HostInput h;
                if (!front.pump(h)) break;      // the window closed
                present(sf);
                SDL_Delay(16);
            }
        } else {
            std::printf("no IMAGES/OMIKRON.BMP - no splash\n");
        }
    }

    std::printf("screen %d. arrows move, ENTER confirms, ESC quits.\n", screenId);

    omk::HostInput host;
    omk::Surface fb(dispW, dispH, 0);
    long n = 0;
    // WAIT FOR THE KEY THAT SKIPPED THE MOVIE TO COME UP. Any key now ends a
    // movie and ESC also quits, so one press did both: skipped the last movie
    // and closed the menu behind it - "0 frames presented", with the window
    // gone before it drew. A game edge-triggers; this is the frame-zero half.
    for (int guard = 0; guard < 300; ++guard) {
        if (!front.pump(host)) break;
        if (host.held.empty()) break;
        SDL_Delay(10);
    }
    bool escWas = false;
    Uint32 lastMs = SDL_GetTicks();
    Uint32 fpsSince = lastMs, fpsLastMs = lastMs, fpsWorst = 0;
    int    fpsFrames = 0;
    for (;;) {
        const Uint32 frameStartMs = SDL_GetTicks();
        if (!front.pump(host)) break;
        // ESC on its EDGE, not while held, for the same reason.
        const bool esc = host.held.count(0x01) != 0;
        if (esc && !escWas) break;
        escWas = esc;

        // A scripted key is fed on its own frame and then RELEASED, which is
        // the only way it produces an edge - `Game_Frame`'s filter is why
        // holding a direction does not scroll (docs/UI.md 3c), and a driver
        // that held them would send one word and see one move.
        omk::DeviceState st;
        for (int dik : host.held) st.keyboard.push_back(dik);
        // the `--hold` stream: held, not tapped, and only once he can walk
        // ...and it keeps feeding while a SCREEN is up. Gated on `adventure`
        // alone it stopped the moment the sneak opened - opening a screen is
        // exactly what takes `adventure` false - so a stream could press TAB
        // and then never press anything again, and the screen could not be
        // driven headless at all.
        if ((adventure || walk) && !holds.empty()) {
            for (int k : holds.front().keys) st.keyboard.push_back(k);
            if (--holds.front().frames <= 0) holds.erase(holds.begin());
        }
        if (!scripted.empty() && (n % keyEvery) == 0) {
            const int k = scripted.front();
            scripted.erase(scripted.begin());
            if (k == kTypeMarker) host.text += typeText;   // as if it were typed
            else st.keyboard.push_back(k);
        }
        // The world's repeat mask is 0 - closing the last screen sets it
        // back, so the `.CTL` channel sees HELD keys and a walk is a walk
        // rather than a step per press. A screen over the world restores
        // `Ui_BeginScreen`'s 0x203F.
        // Keyed on the SCREEN, not on adventure mode. `Ui_BeginScreen` sets
        // 0x203F when a screen opens and the last close puts it back to 0, and
        // this used to do that only while `adventure` was true - which is
        // exactly when it is NOT: a screen over the world takes `adventure`
        // false in the same breath, so the mask stayed at the world's 0 and
        // every held key repeated every frame. Harmless while the only
        // screens came from `ui.open` during the boot, where `adventure` is
        // false and the mask is still the 0x203F set at start-up; the sneak
        // is the first screen opened from inside the world.
        if (walk) in.setRepeatMask(omk::kUiRepeatMask);
        else if (adventure) in.setRepeatMask(0);
        const std::uint32_t bits = in.frame(st);
        // The EDGES, taken here rather than at each consumer so a frame that
        // never reaches one - a dialogue, a cutscene, a screen - cannot leave
        // the latch stale and manufacture a press on the way back. This is
        // `Game_Frame`'s `dword_4E971C` with every bit masked: the engine's
        // own `held & (held ^ (mask & last))` at mask = all ones.
        const std::uint32_t edgeBits = bits & ~prevBits;
        prevBits = bits;
        // THE WORLD'S ACTION BUTTON IS NOT READ HERE. `Game_RaiseEvent(6, 4)`
        // is raised from the ACTOR tick (21_d3d.c:3460, :3513, :3962 - the
        // `.CTL` state handlers), so what stands for it in this loop is
        // `MDACTION` firing; see `actionFromMove` below. Reading it off this
        // edge instead was wrong twice over - it fired while a SCREEN had the
        // input, and it was spent by the time the sneak's own confirm let go.

        // THE DIALOGUE CLOCK IS REAL TIME, not a frame count.
        //
        // A line is timed by its AUDIO (CLAUDE.md 5), and the audio device
        // plays at its own rate whatever this loop does. Advancing the line by
        // 1/30 s per FRAME assumes the loop runs at exactly 30 - and it does
        // not, because a software-rasterised character costs more than the
        // 33 ms budget - so the pose and the voice drift apart, which is what
        // a reader saw. The web viewer makes the same choice for the camera
        // move and says why: wall clock, so losing or pausing the audio cannot
        // rewind it.
        //
        // A frame-bounded run keeps the fixed 1/30 so the headless checks stay
        // deterministic. The clamp is for a hitch: a long stall must not jump
        // the pose forward by however long the window was dragged.
        if (!frames) {
            const Uint32 nowMs = SDL_GetTicks();
            double dt = (nowMs - lastMs) / 1000.0;
            lastMs = nowMs;
            if (dt < 0.0 || dt > 0.25) dt = 1.0 / 30.0;
            dt *= speed;                     // --speed, the engine's own trick
            session.setFrameSeconds(dt);
            frameSec = dt;
        } else if (speed != 1.0) {
            // A frame-bounded run keeps the fixed 1/30 so the headless checks
            // stay deterministic; asking for a speed scales that too, and at
            // the default 1.0 nothing moves.
            frameSec = speed / 30.0;
            session.setFrameSeconds(frameSec);
        }

        // ---- one frame of the GAME -------------------------------------
        //
        // The script runs unless a screen is up. That is the engine: a script
        // parked at `ui.open` is waiting on a person, and `Game_HandleEvent`
        // case 5 is the only thing that releases it.
        if (!walk) session.frame();

        // ---- SCRIPTED OBJECT MOTION - the crates, the doors, the lifts ---
        //
        // `Script_MoveObjectOnPath` ends in `o3de_SetNodePos(node, x, y, z)`
        // with the path sample OUTRIGHT, so a moving object is a set MESH
        // placed at a world position, named through the object's own first
        // string table. 4841 sites - the most-used script function there is -
        // and nothing moved until 2026-09-03.
        //
        // The mesh's corners are offset by (target - the mesh's authored
        // position), which is what moving its origin means; `cornerMesh` says
        // which corners belong to it, and the base positions are kept so the
        // patch is applied to the ORIGINAL each frame rather than accumulated.
        // Bumping `revision` is what tells a caching backend the buffer moved.
        bool soupsMoved = false;
        if (session.scene().loaded() && !session.scene().motions().empty()) {
            for (int sl = 0; sl < 2; ++sl) {
                WorldSlot& w = worldSlots[static_cast<std::size_t>(sl)];
                if (w.geo.corners.empty() || w.meshes.empty()) continue;
                bool moved = false;
                for (const auto& mo : session.scene().motions()) {
                    if (!mo.placed) continue;
                    int mi = -1;
                    for (std::size_t k = 0; k < w.meshes.size(); ++k)
                        if (sameName(w.meshes[k].name, mo.name)) { mi = static_cast<int>(k); break; }
                    if (mi < 0) continue;
                    if (w.baseCorners.empty()) w.baseCorners = w.geo.corners;
                    if (w.baseSoup.empty()) w.baseSoup = w.soup;
                    if (w.baseSteep.empty()) w.baseSteep = w.steep;
                    const float* mp = w.meshes[static_cast<std::size_t>(mi)].pos;
                    // ...AND ITS ORIENTATION. The handler sets both - the path
                    // sample's 3x3 goes to node `+56` through `sub_437160`
                    // beside the `o3de_SetNodePos` - and an object that turns
                    // in place has ONLY the rotation: the Impasse's fan,
                    // `Ventilo`, samples the same point every frame and is
                    // animated entirely by the quaternion. Position-only, it
                    // stood still (`todo/omk-play.md` 53).
                    //
                    // A `Mesh` record carries no orientation of its own, only
                    // `pos`, so the node's matrix starts as identity and the
                    // sample's applies directly: a corner is rotated about the
                    // mesh's authored origin and then placed at the sample.
                    // With an identity quaternion this is exactly the offset
                    // the position-only version applied.
                    const omk::Quatf q{mo.quat[0], mo.quat[1], mo.quat[2], mo.quat[3]};
                    for (std::size_t c = 0; c < w.geo.corners.size(); ++c) {
                        if (c >= w.geo.cornerMesh.size() || w.geo.cornerMesh[c] != mi) continue;
                        const float local[3] = {w.baseCorners[c].x - mp[0],
                                                w.baseCorners[c].y - mp[1],
                                                w.baseCorners[c].z - mp[2]};
                        float r[3] = {local[0], local[1], local[2]};
                        if (mo.rotated) omk::qrot(q, local, r);
                        w.geo.corners[c].x = r[0] + mo.pos[0];
                        w.geo.corners[c].y = r[1] + mo.pos[1];
                        w.geo.corners[c].z = r[2] + mo.pos[2];
                    }
                    // the collision soups follow the mesh exactly as the
                    // render corners above (`Sweep_MeshTest` collides
                    // against the mesh's CURRENT matrix)
                    const auto patchSoup = [&](omk::TriangleSoup& soup, const omk::TriangleSoup& base,
                                               const std::vector<int>& meshOf) {
                        for (std::size_t t = 0; t < meshOf.size() && 9 * t + 9 <= base.size(); ++t) {
                            if (meshOf[t] != mi) continue;
                            for (int v = 0; v < 3; ++v) {
                                const std::size_t o = 9 * t + 3 * static_cast<std::size_t>(v);
                                const float local[3] = {base[o] - mp[0], base[o + 1] - mp[1], base[o + 2] - mp[2]};
                                float r[3] = {local[0], local[1], local[2]};
                                if (mo.rotated) omk::qrot(q, local, r);
                                soup[o] = r[0] + mo.pos[0]; soup[o + 1] = r[1] + mo.pos[1]; soup[o + 2] = r[2] + mo.pos[2];
                            }
                        }
                    };
                    patchSoup(w.soup, w.baseSoup, w.soupMesh);
                    patchSoup(w.steep, w.baseSteep, w.steepMesh);
                    soupsMoved = true;
                    moved = true;
                }
                if (moved) w.geo.revision = ++worldGeoRev;
            }
        }
        if (soupsMoved) {
            // the walker holds REFERENCES to the merged copies, so they are
            // refilled in place rather than rebuilt (rebuildWorld's merge)
            playerSoup.clear(); playerSteep.clear();
            for (int sl = 0; sl < 2; ++sl) {
                const WorldSlot& w = worldSlots[static_cast<std::size_t>(sl)];
                if (w.stem.empty()) continue;
                playerSoup.insert(playerSoup.end(), w.soup.begin(), w.soup.end());
                playerSteep.insert(playerSteep.end(), w.steep.begin(), w.steep.end());
            }
        }

        // ---- ADVENTURE MODE'S SOUND EFFECTS -----------------------------
        //
        // A cutscene's sound rides on a scene object's program; the player's
        // rides on the `.CTL` state machine, and the two use OPPOSITE lookups.
        // `Cef_TickEffects` resolves its `+22` with `Scene_FindSoundIndex` -
        // a search of the resident scene's chunk-3 records for a matching
        // `+24` ID - where a scene program's param 0 is a bounds-checked
        // INDEX. So `H_WALK`'s 203/199 name `STPR`/`STPL` in the Impasse and
        // may name nothing at all in a scene that does not carry them, which
        // is the engine's behaviour and not a gap here.
        if (player) {
            // the sprite half of the records, spawned as Cef_UpdateStateEffects does
            const int   st = player->ctlState();
            const float fr = player->channelFrame();
            if (st != ctlFxState || fr < ctlFxFrame) {
                // A NEW STATE (or a wrap) kills what the old one spawned - a
                // PORT SIMPLIFICATION, labelled: the engine keeps an instance
                // across states unless flag 0x10 is set, on a clock that keeps
                // running; every shipped record here dies inside its window.
                ctlSprites.clear();
                ctlFxState = st;
                for (const auto& e : player->stateEffects()) {
                    if (!e.sprite || (e.flags & 2)) continue;
                    ctlSprites.push_back({e.sprite, e.duration, e.from, e.to, e.scale,
                                          e.flags, e.attach, st});
                    std::printf("ctl-effect: state %d '%s' spawns sprite %d on attach %d "
                                "('%s') for %.0f frames from %.0f, scale %.2f, flags 0x%02x\n",
                                st, player->clipName().c_str(), e.sprite, e.attach,
                                e.attach < 18 ? kAttachName[e.attach] : "Buste",
                                e.duration, e.from, e.scale, e.flags);
                }
            }
            ctlFxFrame = fr;
            for (std::size_t i = 0; i < ctlSprites.size();) {
                const auto& c = ctlSprites[i];
                const float to = c.to == 0.0f ? 10000.0f : c.to;
                if (fr > c.from + c.duration || fr > to) ctlSprites.erase(ctlSprites.begin() + static_cast<long>(i));
                else ++i;
            }
        }
        if (player && session.scene().loaded()) {
            const auto& rt = session.scene().scene();
            for (const auto& es : player->sounds()) {
                const int i = rt.wavBydId(es.id);
                if (i < 0) continue;            // this scene carries no such id
                const auto pcm = wavToDevice(rt.wavData(i), 44100);
                if (!pcm.empty()) { sfxLog("ctl-effect", pcm.size(), es.id, i);
                                    front.playSound(pcm); }
            }
        }

        // ---- THE SCENE'S OWN SOUND EFFECTS ------------------------------
        //
        // An object's animation carries its sound: `Script_PlaySound` and
        // `Script_PlaySyncSound` hang off the body animation through the
        // `+12` sync link and run in the same chain walk, which is why the
        // Impasse's arrival clip fires STPR/STPL at frames 170, 200, 210 and
        // 280 - Kay'l's footsteps. `SceneRunner` reports what each frame
        // started; the payload is a whole RIFF sitting in the `.SCX` stream.
        //
        // POSITION IS NOT APPLIED. `Sound_Play3D` takes the node's world
        // position and a pair of distances, and the attenuation and pan law
        // beyond that point is DirectSound's - `PORTING` B5: it has no
        // reachable tier and imitating it precisely would be invention. The
        // cue, its timing and its loop flag are the decisions, and those are
        // what this plays.
        {
            const auto& sc = session.scene();
            for (const auto& fs : sc.sounds()) {
                // `Script_StopSound` (omk-play 71): the voice playing this
                // wav on this node is silenced, not every voice of the wav -
                // the handler matches on BOTH, so the key is the pair.
                const auto key = std::make_pair(fs.cue.wav, fs.cue.node);
                if (fs.cue.stop) {
                    const auto it = sceneVoices.find(key);
                    if (it != sceneVoices.end()) {
                        front.stopSound(it->second);
                        sceneVoices.erase(it);
                        std::printf("scene sound: STOP wav %d node %d - %zu still looping\n",
                                    fs.cue.wav, fs.cue.node, sceneVoices.size());
                    } else {
                        std::printf("scene sound: stop asked for wav %d node %d, "
                                    "which is not looping (%zu are)\n",
                                    fs.cue.wav, fs.cue.node, sceneVoices.size());
                    }
                    continue;
                }
                // A LOOPING cue is started ONCE. Its program re-reaches the
                // function every cycle - wav 23 came round 33 times in 521
                // frames, a 1.76 s sample overlapping itself three deep and
                // thrashing the 8-slot pool - and the engine does not restart
                // a sound that is already looping: the loop lives in the
                // mixer, which is what the flag is FOR.
                if (fs.cue.loop && sceneVoices.count(key)) continue;
                const auto raw = sc.scene().wavData(fs.cue.wav);
                if (raw.empty()) continue;      // 186 of 5425 name a sound
                                                // their scene does not carry;
                                                // `sub_48CB30` returns -1 and
                                                // the engine plays nothing
                const auto pcm = wavToDevice(raw, 44100);
                if (pcm.empty()) continue;
                // ---- POSITIONAL, because `Script_PlaySound` is 3D ------
                //
                // The handler (0x004A12D0) ends in three calls to
                // `Sound_Play3D` (0x0046CDC0), so a scene sound is placed and
                // attenuated by distance from the listener. Played flat, an
                // ambience is as loud across the city as beside it and goes on
                // through a cutscene whose camera is nowhere near it - which
                // is exactly what a reader reported (omk-play 73).
                //
                // **RECONSTRUCTION, and labelled in three places** (this, the
                // frontend, and the entry): WHERE the sound is comes from the
                // program's own motions this frame - the cue names a NODE and
                // the port cannot resolve a scene node to a world point, so
                // the object being animated stands in for it. And the CURVE is
                // this port's own: DirectSound owned the attenuation law and
                // `PORTING`'s audio row records that it has NO reachable tier,
                // so a plausible inverse-distance is the honest most that can
                // be done. What IS the engine's is the unit - the listener is
                // told the world unit is an INCH.
                float gain = 1.0f;
                {
                    const auto mo = sc.motionsOf(fs.program);
                    if (!mo.empty() && player) {
                        const float* L = player->pos();
                        float best = 1e30f;
                        for (const auto& m : mo) {
                            const float dx = m.pos[0] - L[0], dy = m.pos[1] - L[1],
                                        dz = m.pos[2] - L[2];
                            best = std::min(best, dx * dx + dy * dy + dz * dz);
                        }
                        const float d = std::sqrt(best);
                        // full within a room's width, then 1/d out to silence
                        constexpr float kNear = 120.0f;    // inches: ~3 m
                        constexpr float kFar  = 4000.0f;   // ~100 m
                        gain = d <= kNear ? 1.0f
                             : d >= kFar  ? 0.0f
                             : kNear / d;
                    }
                }
                sfxLog(fs.cue.loop ? "scene-LOOP" : "scene-sound",
                       pcm.size(), fs.cue.wav, fs.object, gain);
                if (gain <= 0.01f) continue;      // too far to hear at all
                const int h = front.playSound(pcm, fs.cue.loop, gain);
                // only a LOOPING cue needs remembering - a one-shot ends by
                // itself and the handle would go stale
                if (h >= 0 && fs.cue.loop) {
                    sceneVoices[key] = h;
                    // A stop can only be judged against what is PLAYING, so
                    // say what is looping and keep the list current. One-shots
                    // are not listed: they end on their own and nothing can
                    // stop them.
                    std::printf("scene sound: LOOP wav %d node %d (object %d, "
                                "program %d) - %zu looping now:",
                                fs.cue.wav, fs.cue.node, fs.object, fs.program,
                                sceneVoices.size());
                    for (const auto& v : sceneVoices)
                        std::printf(" [wav %d node %d]", v.first.first, v.first.second);
                    std::printf("\n");
                }
            }
        }

        // ---- the hand-over, and the controller's frame ------------------
        {
            const auto& sc = session.scene();
            bool playerDriven = false;
            if (sc.loaded()) {
                const auto& started = sc.started();
                for (std::size_t k = 0; k < started.size(); ++k) {
                    const auto& stt = started[k];
                    if (stt.how != "player" || !sc.programRunning(static_cast<int>(k)))
                        continue;
                    playerDriven = true;
                    if (stt.clip < 0) continue;
                    const float t = sc.programClock(static_cast<int>(k));
                    // `Actor_SetEuler(param 4/5/6)` composes over the clip's
                    // root; the Impasse authors 0 there (Started::euler), so
                    // the sum is the only reading exercised.
                    handoverFacing = omk::headingFromClipRoot(
                        sc.scene().clipData(stt.clip), t < 0 ? 0 : static_cast<int>(t))
                        + stt.euler[1];
                    handoverFacingKnown = true;
                }
            }
            if (session.areasEntered() != playerDrivenArea) {
                playerDrivenArea = session.areasEntered();
                if (!player) {
                    // nobody at the keys yet: the new area's beats decide
                    playerDrivenSeen = false;
                    playerReady = false; adventure = false;
                } else {
                    // He IS at the keys. The engine's actor is one record
                    // that walks from one decor onto the other; the walker's
                    // soup follows the shown slots (`rebuildWorld`) and
                    // nothing here rebuilds him. Until 2026-09-03 this reset
                    // the controller, and the gate below never rebuilt it -
                    // SCENE 55 stayed resident with its programs - so the
                    // walk into the airlock froze him in the alley.
                    std::printf("frame %ld: area transition %d with the player at the keys - "
                                "he keeps walking\n", n, playerDrivenArea);
                }
            }
            if (playerDriven) playerDrivenSeen = true;
            const omk::WorldCamera* hc = session.cameraTarget();
            // A HELD PLAYER MEANS THE SCRIPT OWNS THE CAMERA.
            //
            // `followCam` used to be a test on the camera's SHAPE alone, and
            // that cannot tell the area's follow camera from a scripted shot:
            // AREA 222's tutorial names 4290/4291/4292, and all three are
            // eyeSubject 0 / atSubject 0 exactly like camera 0. So each shot
            // was fed to `setCameraOffsets` and RE-AIMED THE FOLLOW CAMERA -
            // it kept trailing the player with the follow lag instead of
            // standing as a staged shot, which reads as "the camera never
            // left adventure mode".
            //
            // The engine's own discriminator is the hold: `sub_415D10` (the
            // follow camera) opens `if ((u32(a1,356) & 0x81) != 0) { v2 = 0;
            // v13 = 0; }` - while the player's channel is held its camera mode
            // is forced to 0. And the scripts bracket every staged sequence
            // with 104/105 (SCRIPT_VM 104/105; AREA 222 holds at pc 2360 and
            // releases at 2414, after its last `camera.set`). So: held means
            // the follow controller stands down and the requested camera is
            // resolved as a fixed shot.
            followCam = hc && !hc->absolute() && hc->eyeSubject == 0 &&
                        hc->atSubject == 0 && !session.playerAnimHeld();
            const bool beatsOver = playerDrivenSeen || !sc.loaded() || sc.programCount() == 0;
            const bool feetSetLoaded =
                !worldSlots[static_cast<std::size_t>(session.activeSlot() & 1)].geo.corners.empty();
            if (forceAdventure && !hc) followCam = true;   // a street start: no camera asked, the follow one
            const bool wantAdventure = forceAdventure
                ? (session.playerPlaced() && !session.dialogOpen() && !walk && feetSetLoaded)
                : (followCam && !playerDriven && beatsOver &&
                   session.playerPlaced() && !session.dialogOpen() && !walk &&
                   !sc.activeEditing() && feetSetLoaded);
            if (wantAdventure && !player && !worldSet.empty()) {
                // WHO he is: the DB's player record (+60; `player.become`
                // copies the actor record into it and a save carries it),
                // +144 the model and +72 the bank - then the resident actor
                // record, then the character on screen.
                const auto nameAt = [&](std::size_t off) {
                    std::string out;
                    const auto raw = state.raw();
                    for (std::size_t k = 0; k < 20 && off + k < raw.size(); ++k) {
                        const char c = static_cast<char>(raw[off + k]);
                        if (!c) break;
                        out.push_back(c);
                    }
                    return out;
                };
                const auto rec = static_cast<std::size_t>(omk::GameState::kPlayerRecord);
                playerModel = nameAt(rec + 144);
                playerCtlName = nameAt(rec + 72);
                std::string modelSrc = "the DB player record";
                if (playerModel.empty()) {
                    playerModel = session.modelOfActor(session.playerActor());
                    modelSrc = "the resident actor record";
                }
                if (playerModel.empty() && !session.shown().empty()) {
                    playerModel = session.shown().front().model;
                    modelSrc = "the character on screen";
                }
                std::string ctlSrc = "the DB player record";
                if (playerCtlName.empty()) {
                    // RECONSTRUCTION: AREA 118's record for Kay'l (310) names
                    // no bank, and the resident record is not reachable from
                    // here. The two adventure banks are H1Avnt / F1Avnt.
                    playerCtlName = (!playerModel.empty() && playerModel[0] == 'F') ? "F1AVNT" : "H1AVNT";
                    ctlSrc = "a LABELLED FALLBACK (the record names none)";
                }
                const auto mo = fs.resolve("MESHES/PERSOS/" + playerModel + ".3DO");
                const auto mt = fs.resolve("MESHES/PERSOS/" + playerModel + ".3DT");
                const auto cp = fs.resolve("ANIMS/" + playerCtlName + ".CTL");
                playerReady = false;
                if (mo && cp && !playerSoup.empty()) {
                    const auto md = omk::DataFs::readPath(*mo);
                    playerRest = omk::buildGeometry(md, omk::DrawFilter::Engine);
                    playerRest.revision = ++worldGeoRev;
                    playerMeshes.clear();
                    if (const auto mh = omk::readHeader(md)) playerMeshes = omk::readMeshes(md, *mh);
                    playerSpheres = omk::collisionSpheresOf(playerMeshes);
                    playerReach = playerMeshes.empty() ? 0.0f : playerMeshes.front().radius;
                    playerTex = mt ? omk::textures(md, omk::DataFs::readPath(*mt))
                                   : std::vector<omk::Texture>{};
                    playerCtlData = omk::DataFs::readPath(*cp);
                    playerCtl = omk::readCtl(playerCtlData);
                    // `playerSoup` is the shown slots' walkable soups, merged
                    // by `rebuildWorld` - the floor of BOTH decors.
                    if (playerCtl.valid && playerCtl.exact && !playerMeshes.empty() &&
                        !playerSoup.empty()) {
                        omk::PlayerController::Setup su;
                        su.ctl = &playerCtl; su.ctlData = playerCtlData;
                        su.meshes = &playerMeshes; su.soup = &playerSoup;
                        su.steep = &playerSteep;
                        // THE NARROW PHASE (issues 68/75, HANDOFF "the walker
                        // does not block on walls"): the same steep faces,
                        // swept; the radius is his largest collision sphere
                        // (`Actor_Move`: max r over the model's spheres, x
                        // dword_910358 = 1.0), and 12 - the sim's stand-in -
                        // when the model carries none.
                        su.blockers = &playerSteep;
                        {
                            // the model's OWN list (descriptor +244/+248), not
                            // the per-mesh crowd-push spheres: HO1_FN's four
                            // are 10.9 each, the crowd list's largest is 42.5
                            const auto sph = omk::modelSweepSpheres(md);
                            float r = 0.0f;
                            for (const auto& c : sph) r = std::max(r, c.radius);
                            su.sweepRadius = r > 0.0f ? r : 12.0f;
                            std::printf("adventure: the walker sweeps a sphere of radius %.1f "
                                        "(the model's %zu sweep spheres%s) against %zu wall faces\n",
                                        su.sweepRadius, sph.size(),
                                        sph.empty() ? " - none, the sim's 12 stands in" : "",
                                        playerSteep.size() / 9);
                        }
                        for (int k = 0; k < 3; ++k) su.pos[k] = session.playerPos()[k];
                        su.facing = handoverFacingKnown ? handoverFacing : session.playerYaw();
                        player = std::make_unique<omk::PlayerController>(su);
                        // `Walk_ProbeGround` raises event 9 when the decor
                        // under his feet changes to a slot in state 2 - and
                        // from here on the feet are probed every tick
                        // (`decorUnder`, actor/walk.h), so the ACTIVE row
                        // follows him across a transition. The teleport
                        // raise in `placeActorAt` stays the other source.
                        {
                            const float* pp = player->pos();
                            const int under = omk::decorUnder(worldDecors, pp[0], pp[1], pp[2]);
                            if (under >= 0) session.playerOnArea(under);
                        }
                        if (haveStand) {
                            player->placeAt(standAt, standAt[3]);
                            std::printf("street start: --stand puts the player at %.0f %.0f %.0f facing %.0f\n",
                                        standAt[0], standAt[1], standAt[2], standAt[3]);
                        }
                        placementSeen = session.placementSeq();
                        playerReady = !playerRest.corners.empty();
                        playerFeetKnown = false;
                        playerCamId = -2;
                        handoverFrame = n;
                        std::printf("frame %ld: ADVENTURE MODE - the player is %s (%s) on "
                                    "%s (%s); %d/%d tracks resolve; %zu walkable "
                                    "triangles of %s; standing at %.0f %.0f %.0f facing "
                                    "%.0f (%s); arrows walk and turn, RSHIFT runs\n",
                                    n, playerModel.c_str(), modelSrc.c_str(),
                                    playerCtlName.c_str(), ctlSrc.c_str(),
                                    player->tracksMatched(), player->tracksTotal(),
                                    playerSoup.size() / 9, worldSet.c_str(),
                                    player->pos()[0], player->pos()[1], player->pos()[2],
                                    player->facing(),
                                    handoverFacingKnown ? "from the last player clip's root"
                                                        : "the Session's yaw");
                    } else {
                        std::printf("adventure: cannot build the player (%s / %s / %s)\n",
                                    playerModel.c_str(), playerCtlName.c_str(), worldSet.c_str());
                    }
                } else {
                    std::printf("adventure: no model/bank/set for %s / %s / %s\n",
                                playerModel.c_str(), playerCtlName.c_str(), worldSet.c_str());
                }
            }
            // WHICH SCREENS STOP THE WORLD, and it is not "any screen".
            //
            // This used to read `!walk` - a screen open, of any kind - and a
            // player found it by opening the sneak in Anekbah and watching
            // the city freeze: 19 world frames in 1924. The engine does not
            // do that. `Game_Tick` (0x004200F0) runs `Script_SetFrameTime`,
            // the per-slot `Script_PlayAllScripts` loop, `Projectiles_Tick`,
            // `Sliders_Tick` and `Slider_TickRide` with NO test for an open
            // screen anywhere in it - the block at its head that looks like
            // one is the start menu's ATTRACT-MODE timeout (screen 29 idle
            // past 1800 units -> `FLIS\GAME.mpg` -> `Game_Start`).
            //
            // The only thing that stops the world is the pause flag
            // `dword_4E9728`, and it has exactly TWO writes in the whole
            // image: screen 31 PAUSE GAME's open callback (0x004ADDB0) sets
            // it and its close (0x004ADEB0) clears it. It works by forcing
            // the frame delta to 0.0, which is why `Slider_TickRide` sits
            // behind `flt_4C30D8 != 0.0` two lines below it in `Game_Tick`.
            //
            // This also REFUTES a banner: `readable/src/05_sys.c` says of
            // `Game_Tick` that "a playing FLIS or interface screen
            // short-circuits the world tick". It does not, and that reading
            // was `NAMED` - read and named, never tested.
            const bool uiPause = walk && openScreen == kScreenPause;
            adventure = player && !playerDriven && !session.dialogOpen() &&
                        !uiPause && !sc.activeEditing();
            if (adventure) {
                // The follow camera is the world camera the script named -
                // SCENE 55's camera 0 carries its own offsets and travels
                // the same resolve as the mode-0 preset. Its three trailing
                // shorts (the smoothing divisors) are not lifted into
                // `WorldCamera`; the preset's 3/8/8 stand in, labelled.
                if (followCam && hc && hc->id != playerCamId) {
                    playerCamId = hc->id;
                    player->setCameraOffsets(hc->eye, hc->at, hc->fov);
                    std::printf("frame %ld: follow camera %d - eye offset %.0f %.0f %.0f, "
                                "target offset %.0f %.0f %.0f, fov %.0f (smoothing 3/8/8 "
                                "from the mode-0 preset)\n", n, hc->id,
                                hc->eye[0], hc->eye[1], hc->eye[2],
                                hc->at[0], hc->at[1], hc->at[2], hc->fov);
                }
                // A TELEPORT under him: `actor.goto_address` wrote the
                // Session's position outright (the airlock beat's 653,
                // 'Tutorial'). Without this the next line writes the walker's
                // old position straight back over it.
                if (session.placementSeq() != placementSeen) {
                    placementSeen = session.placementSeq();
                    player->placeAt(session.playerPos(), session.playerYaw());
                    std::printf("frame %ld: actor.goto_address %d - the player put down at "
                                "%.0f %.0f %.0f facing %.0f\n", n, session.playerAddress(),
                                player->pos()[0], player->pos()[1], player->pos()[2],
                                player->facing());
                }
                // THE CROWD PUSH - `Actor_TickNpc`, before `Actor_ApplyMotion`:
                // the spatial index's answer for his spheres, added to his
                // position outright (docs/STREET_LIFE.md 3). The Session
                // posts the bump message when a walker was touched.
                {
                    float push[3];
                    if (!playerSpheres.empty() &&
                        session.crowdPush(playerSpheres, playerReach, player->pos(), player->facing(), push))
                        player->nudge(push);
                }
                // A SCREEN HAS THE INPUT, and the world still runs.
                //
                // Removing the old `!walk` gate (which froze all of Anekbah
                // behind the sneak) also handed the player the arrow keys
                // while the menu had them - a session log shows MDWALK,
                // MDROT000 and MDACTION firing after "screen 9 opened",
                // so moving the selection walked him down the street.
                //
                // The two are separate: `Game_Tick` runs `Actors_TickAll`
                // whatever is on screen, so the channel must keep ticking -
                // it is what carries a gait to its stand state - but the
                // INPUT WORD is the interface's while a screen is up.
                // `Ui_BeginScreen` installs its own repeat mask over the
                // device for exactly that reason. So this is the same shape
                // as `player.anim.hold` right below: tick with nothing
                // pressed rather than not ticking.
                if (walk) {
                    player->tick(static_cast<float>(frameSec * 30.0), 0);
                } else if (session.playerAnimHeld()) {
                    // `Actor_HoldAnimation(player, 1)` does NOT stop the
                    // channel - it feeds it a lone IDLE word every tick and
                    // cuts the device off, and the channel then keeps running
                    // with no input at all.
                    //
                    // `sub_45A870` writes `queue[0] = 0x40000000; n = 1`, and
                    // those two arrays are the input QUEUE and its LENGTH -
                    // `Perso_InjectInput` (0x0045A9F0) is the proof, it fills
                    // exactly them from its `a3`/`a2`. `Cef_TickChannel`
                    // re-asserts both every tick while `flags & 0x81`, and the
                    // channel's own rule then DROPS it: `if (n == 1 &&
                    // (queue[0] & 0x40000000)) { queue[0] = 0; n = 0; }`
                    // (ASSETS "a lone idle word is DROPPED").
                    //
                    // So the state machine ticks on with nothing pressed, which
                    // is what carries a GAIT to its stand state. Not ticking at
                    // all - what this did - leaves him mid-stride with one leg
                    // forward for the whole held sequence (omk-play 43).
                    ++heldFrames;
                    player->tick(static_cast<float>(frameSec * 30.0), 0);
                } else {
                    player->tick(static_cast<float>(frameSec * 30.0), bits);
                }
                // omk-play 69: WATCH THE WHOLE TAKE, INCLUDING THE WAIT.
                //
                // The take is not one animation, it is a CONVERSATION with the
                // player - the reader's own account: "grabbing an object means
                // taking it in the hand, waiting for the user confirmation or
                // cancellation, and triggering the right anim for each case".
                // H1Avnt says the same thing:
                //
                //   group 41   H_TAKL12 -> MDGETOBJ -> H_TAKL22 -> goto ...
                //   group 4    H_WAITOB   (the group's DEFAULT entry - the hold)
                //                +- MDPUTSNK  flags 80000013 -> H_GETOBJ -> H_STAND
                //                +- MDNOTAKE  flags 80000013 -> H_GETOBJ -> H_STAND
                //
                // Neither child of `H_WAITOB` carries the default bit `0x20`
                // and both carry `0x80000000`, so the machine is meant to SIT
                // in H_WAITOB until an input picks a branch. A channel that
                // falls through to a child when nothing matches would play the
                // confirm or the cancel immediately - "it plays all the
                // animations", which is the report.
                //
                // The old window was 120 TICKS, and a tick advances the clip by
                // `frameSeconds * 30`, so under `--speed 3` it expired just as
                // H_WAITOB began and the interesting half was never logged.
                // This runs until the machine is back in the idle, so the wait
                // and whatever leaves it are both on the record - and prints
                // the INPUT WORD, because "did a transition fire with no input"
                // is the whole question.
                {
                    static int  watch = 0;
                    static bool leftIdle = false;
                    static std::string lastClip;
                    for (const auto& mv : player->specialMoves())
                        if (mv == "MDACTION") {
                            watch = 1200; leftIdle = false; lastClip.clear();
                        }
                    if (watch > 0) {
                        --watch;
                        const std::string c = player->clipName();
                        if (c != lastClip) {
                            lastClip = c;
                            std::printf("  anim: frame %ld  .CTL state %d '%s'  clip '%s' "
                                        "f %.1f  input %04x%s\n", n, player->ctlState(),
                                        player->ctlStateName().c_str(), c.c_str(),
                                        player->clipFrame(), bits,
                                        bits ? "" : "   <- NO INPUT");
                            // Stop only once the machine has LEFT the idle and
                            // come back. Testing for H_STAND alone ended the
                            // watch on its first tick every time, because the
                            // press is seen while the idle clip is still up -
                            // six takes logged one line each and none of the
                            // interesting half.
                            if (c != "H_STAND") leftIdle = true;
                            else if (leftIdle) watch = 0;
                        }
                    }
                }
                // omk-play 67: does the new fall/slide path actually engage
                // while someone plays? Before the fix every drop past the step
                // limit came back Refused and the actor stood on it, so a
                // count of Fell/Slid against Refused is the whole question.
                {
                    static long nFell = 0, nSlid = 0, nRefused = 0, nBlocked = 0;
                    static long toldAt = -1;
                    const auto r = player->last().step;
                    if (r == omk::StepResult::Fell)         ++nFell;
                    else if (r == omk::StepResult::Slid)    ++nSlid;
                    else if (r == omk::StepResult::Refused) ++nRefused;
                    else if (r == omk::StepResult::Blocked) ++nBlocked;
                    const long tot = nFell + nSlid + nRefused;
                    if (tot > 0 && tot != toldAt && (tot % 25) == 0) {
                        toldAt = tot;
                        std::printf("walk: %ld fell, %ld slid, %ld refused (a drop past "
                                    "the no-damage tier), %ld blocked - at %.0f %.0f %.0f\n",
                                    nFell, nSlid, nRefused, nBlocked,
                                    player->pos()[0], player->pos()[1], player->pos()[2]);
                    }
                }
                // ---- THE WORLD TAKE: tab_special_move[] 3..7 -------------
                //
                // omk-play 66. Pressing action fires MDACTION (H1AVNT entry
                // 24, group 0, input 0x10) and that entry has NO CHILDREN:
                // the HANDLER carries the machine on, by finding group id 45
                // and installing it (`loc_46AFD0`: `Cef_FindGroupById(actor+
                // 180, 0x2D)` -> `SetPersoBankGroup`). Group 45's entry is
                // MDGETOBJ, so the take follows from the group switch alone.
                //
                //   MDACTION  scan for an object within 150 cm; found ->
                //             group 45, else nothing (the press falls through
                //             to Script_Pump's "nothing here")
                //   MDGETOBJ  `sub_41C490(player, slot)` - the node is linked
                //             to the hand and actor+164 points at the object's
                //             96-byte record - then the object's NAME through
                //             `Subtitle_Show`
                //   MDPUTSNK  entry 55, group 4, input 0x10: press action
                //             AGAIN and it goes in the sack - `sub_41C720`
                //             raises event 10 with the slot and clears +164
                //   MDLETOBJ  reached from entry 56 (input 0x20, the CANCEL
                //             bit): `sub_41C540(player, 0)` - released with
                //             remove=0, so the prop returns to its placement
                //
                // The three-press shape a reader described - take and see the
                // name, press again to bank it, another button to put it back
                // - is these four rows and nothing else.
                // `--sneak`: the same request the special move makes, once.
                if (openSneak && !walk && playerScreen < 0) {
                    openSneak = false;
                    inv.openList(0);
                    playerScreen = omk::kScreenSneak;
                    std::printf("--sneak: event %d opens object list 0, "
                                "screen %d\n", omk::kEventSneakOpen,
                                omk::kScreenSneak);
                }
                // WHERE THE ACTION RAISE COMES FROM, corrected 2026-09-04.
                //
                // `Game_RaiseEvent(6, 4)` has three sites in the image
                // (21_d3d.c:3460, :3513, :3962) and every one of them is an
                // ACTOR STATE HANDLER - the `.CTL` machine reaching the action
                // state, which is `MDACTION`. It is not read off the input
                // word at all. Taking it from the input EDGE instead put the
                // press one release out of step with the game: the ENTER that
                // confirms `Utiliser` is still held when the sneak closes, so
                // the .CTL enters the action state and fires MDACTION while
                // the edge has already been spent - the player pressed at the
                // lift with the key in his hand and the world never heard it.
                // A player hit exactly that and had to use a SECOND key.
                //
                // Entering a state is once per press by construction (the
                // state persists while the button is held), which is also why
                // the engine can raise from here without the six-presses-per
                // -press problem the raw LEVEL had.
                bool actionFromMove = false;
                for (const auto& mv : player->specialMoves()) {
                    if (mv == "MDACTION") actionFromMove = true;
                    const omk::SpecialMoves::Row* row = specialMoves.find(mv);
                    if (row)
                        std::printf("special move: %s (tab_special_move[%d] = 0x%08x)\n",
                                    row->name.c_str(), row->index, row->handler);
                    if (mv == "MDACTION" || mv == "MDADJSTP") {
                        // ---- `sub_465D30(actor, obj, fromAdjust)` ----------
                        //
                        // ONE function decides both stages of the take, and
                        // until 2026-09-04 this ported only its last twenty
                        // lines (which group) and guessed the rest. Read whole:
                        //
                        //   dx, dy, dz  = object node pos - actor node pos.
                        //                 The actor's node is the PELVIS (the
                        //                 same +244..+252 the follow camera
                        //                 targets), so dy here is the port's
                        //                 feet-relative dy plus the pelvis
                        //                 height - and it counts DOWN, like
                        //                 every y in this world. An object on
                        //                 the floor is +40 below the pelvis;
                        //                 `dy <= 27.47` (less than 70 cm below
                        //                 it) is the HIGH take, group 143.
                        //   D           = hypot(dx, dz); angle = the signed
                        //                 bearing off his facing.
                        //   LOW arm:      angle -= 10 (a bias, authored into
                        //                 the clips); target = 40 cm / cos;
                        //                 second = dy - 27.47 (scaled by
                        //                 1/29.53 inside sub_466390).
                        //   HIGH arm:     target = 60 cm / cos; second = the
                        //                 PITCH of the object seen from the
                        //                 target point, asin(-dy / hypot(
                        //                 target, dy)) in degrees - which
                        //                 answers what reaches +0x1C8 there.
                        //   refuse if     |angle| > 50 and (fromAdjust or
                        //                 D < target), or |D - target| > 120cm.
                        //   from MDACTION: if D/target is within 10%, no step:
                        //                 take at once. Else the step SCALE
                        //                 `dword_6A5380 = |D - target|/19.69`.
                        //   target point = object - target * dir; the move
                        //                 to it is `Actor_Move`d OUTRIGHT
                        //                 before a take (both from MDADJSTP
                        //                 and in the no-step case), and only
                        //                 PROBED before a step: if the probe
                        //                 moves under 25 cm the step is
                        //                 dropped and it takes at once (and
                        //                 fails if still > 125 cm off).
                        //   angle stored  flipped by 180 when D < target and a
                        //                 step is coming: he steps BACK.
                        //
                        // The step's DIRECTION and DISTANCE were the two
                        // faults a reader saw across 16 presses: he stepped
                        // 50 cm whatever the distance and walked through the
                        // rings twice. Both are this function.
                        const bool fromAdjust = mv == "MDADJSTP";
                        constexpr float kTakeHigh  = 27.472441f;   // flt_4BC7E0, 70 cm
                        constexpr float kReachLow  = 15.748032f;   // 40 cm
                        constexpr float kReachHigh = 23.622047f;   // 60 cm
                        constexpr float kStepLen   = 19.685039f;   // 1 / 0.0508, the 50 cm step
                        constexpr float kMaxError  = 47.244096f;   // 120 cm
                        constexpr float kNoStep    = 9.8425198f;   // 25 cm
                        constexpr float kMaxProbe  = 49.212598f;   // 125 cm
                        player->setAdjustStep(false);
                        float dyFeet = 0.0f;
                        const int obj = session.scanTakeable(player->pos(),
                                                             player->facing(), &dyFeet);
                        float op[3] = {0, 0, 0};
                        bool  ok = obj >= 0 && session.propPos(obj, op);
                        float D = 0.0f, angle = 0.0f, target = 0.0f, second = 0.0f;
                        float dyP = 0.0f, mx = 0.0f, mz = 0.0f, scale = 1.0f;
                        bool  low = true, takeNow = fromAdjust, moved = false;
                        const char* why = "no object in reach";
                        if (ok) {
                            const float dx = op[0] - player->pos()[0];
                            const float dz = op[2] - player->pos()[2];
                            dyP = dyFeet + player->cameraLift();
                            D = std::sqrt(dx * dx + dz * dz);
                            if (!(D > 0.0f)) { ok = false; why = "standing on it"; }
                            else {
                                // the port's facing convention, as before:
                                // `headingFromClipRoot` ends `atan2(z, x) + 90`
                                const float bearing = std::atan2(dz, dx) *
                                                      57.29577951308232f + 90.0f;
                                float rel = bearing - player->facing();
                                while (rel < -180.0f) rel += 360.0f;
                                while (rel >  180.0f) rel -= 360.0f;
                                // THE ENGINE'S SIGN IS THE OPPOSITE OF `rel`.
                                // `sub_465D30`: `v41 = acos(cos); if (fx*dz -
                                // fz*dx > 0) v41 = -v41`, and with the heading
                                // recipe both facings use (`atan2(z, x) + 90`,
                                // so f = (sin F, -cos F)) that cross product is
                                // sin(bearing - facing) = sin(rel). Positive
                                // rel is a NEGATIVE engine angle. Measured
                                // before the flip (2026-09-04, run 4): the
                                // step's side cell pushed him AWAY from the
                                // object line - lateral offset 15.4 -> 26.8
                                // and 17.7 -> 20.5 across two steps - because
                                // the quadrant table was fed the mirrored
                                // sign.
                                angle = -rel;
                                const float cosA = std::cos(rel * 0.017453292f);
                                low = dyP > kTakeHigh;
                                if (low) { angle -= 10.0f; target = kReachLow / cosA;
                                           second = dyP - kTakeHigh; }
                                else     { target = kReachHigh / cosA; }
                                const float err = std::fabs(D - target);
                                if (std::fabs(angle) > 50.0f && (fromAdjust || D < target)) {
                                    ok = false; why = "outside the 50 degree cone";
                                } else if (err > kMaxError) {
                                    ok = false; why = "more than 120 cm off the target";
                                } else {
                                    if (!fromAdjust) {
                                        const float r = std::fabs(D / target);
                                        if (r > 0.9f && r < 1.1f) takeNow = true;
                                        else scale = err / kStepLen;
                                    }
                                    mx = dx - target * dx / D;
                                    mz = dz - target * dz / D;
                                    if (!low)
                                        second = std::asin(-dyP / std::sqrt(target * target + dyP * dyP)) *
                                                 57.29577951308232f;
                                    if (!takeNow) {
                                        // **PORT SHORTCUT, labelled**: the
                                        // engine PROBES the move (Actor_Move,
                                        // then puts him back) and measures
                                        // what the collision let through;
                                        // this takes the requested length.
                                        const float probe = std::sqrt(mx * mx + mz * mz);
                                        if (probe < kNoStep) {
                                            takeNow = true; scale = 1.0f;
                                            if (std::fabs(D - probe) > kMaxProbe) {
                                                ok = false; why = "cannot close on it";
                                            }
                                        }
                                    }
                                    if (ok && takeNow) moved = player->moveBy(mx, mz);
                                    if (!(D >= target || takeNow))
                                        angle = angle < 0.0f ? 180.0f - angle : angle - 180.0f;
                                }
                            }
                        }
                        if (!ok) {
                            if (obj >= 0)
                                std::printf("take: %s - object %d '%s' refused (%s): D %.1f "
                                            "target %.1f angle %+.1f dy(pelvis) %+.1f\n",
                                            mv.c_str(), obj, session.objectName(obj).c_str(),
                                            why, D, target, angle, dyP);
                            else if (fromAdjust)
                                std::printf("take: MDADJSTP - nothing in reach after the step\n");
                            // engine: dword_53AE1C = 0, nothing installed
                        } else if (takeNow) {
                            const int g = low ? 41 : 143;
                            player->setTakeGeometry(angle, low ? second * 0.033866666f : second);
                            takeCandidate = obj;
                            takeWasLow = low;
                            std::printf("take: %s - object %d '%s' D %.1f target %.1f -> moved %s "
                                        "%+.1f %+.1f; %s take group %d, angle %+.1f second %+.2f\n",
                                        mv.c_str(), obj, session.objectName(obj).c_str(), D, target,
                                        moved ? "to" : "BLOCKED toward", mx, mz,
                                        low ? "LOW H_TAKL" : "HIGH H_TAKH", g, angle, second);
                            if (!player->enterGroupById(g))
                                std::printf("take: the bank has no group %d\n", g);
                        } else {
                            // ---- STAGE ONE: THE ADJUST STEP ---------------
                            // `sub_466210`: group 600, H_ADJSTP, the cells
                            // picked by the angle's quadrant and sign, the
                            // root motion scaled by `dword_6A5380`.
                            player->setTakeGeometry(angle, low ? second * 0.033866666f : second);
                            player->setAdjustStep(true);
                            player->setStepScale(scale);
                            takeCandidate = obj;
                            takeWasLow = low;
                            std::printf("take: MDACTION - object %d '%s' D %.1f target %.1f: "
                                        "adjust step of %.1f (scale %.2f) at angle %+.1f, "
                                        "then the %s take\n",
                                        obj, session.objectName(obj).c_str(), D, target,
                                        std::fabs(D - target), scale, angle,
                                        low ? "LOW" : "HIGH");
                            if (!player->enterGroupById(600))
                                std::printf("take: the bank has no group 600\n");
                        }
                    } else if (mv == "MDGETOBJ") {
                        if (takeCandidate >= 0 && session.takeObject(takeCandidate)) {
                            // `sub_4083F0(46, ...)` then `Subtitle_Show`
                            // (0x0041E040): the object's NAME at the bottom of
                            // the screen, which is what a reader described
                            // seeing on the take. It rides the same subtitle
                            // the voice-overs use.
                            mediaText = session.objectName(takeCandidate);
                            mediaTextFrames = mediaText.empty() ? 0 : 90;
                            std::printf("take: MDGETOBJ - holding %d '%s'\n",
                                        takeCandidate, mediaText.c_str());
                            // `Camera_Request(1, dword_930800)`: the take
                            // camera, travelling 30 frames from what is on
                            // screen (`sub_414A90` keeps the outgoing block as
                            // g_CameraPrev, request+24 = 30 the frames).
                            heldInHand = takeCandidate;       // `sub_41C490`: it rides the hand from here
                            takeCam = true;
                            takeCamRequest(1);
                            std::printf("take: camera mode 1 requested - preset 1 over 30 frames\n");
                        }
                    } else if (mv == "MDPUTSNK") {
                        // `if (C+12 == 1) Camera_Request(16)`: back to the
                        // camera the take displaced, 30 frames.
                        if (takeCam) {
                            takeCamRequest(3);
                            std::printf("take: camera mode 16 requested - back to the follow camera over 30 frames\n");
                        }
                        const int was = static_cast<int>(
                            omk::objectList(state, omk::ObjectList::Carried).size());
                        const auto arm = session.bankHeldObject(takeCandidate);
                        const int now = static_cast<int>(
                            omk::objectList(state, omk::ObjectList::Carried).size());
                        // NAME THE ARM. `Inventory_Insert`'s four outcomes are
                        // not interchangeable and a count alone cannot tell
                        // them apart - a merge and a full list both leave it
                        // unchanged, and the first is correct where the second
                        // is a refusal. Saying "REFUSED (full, or a kind
                        // Inventory_Insert would merge)" made a reader guess,
                        // and it was wrong: the kind-13 rings were neither.
                        using B = omk::Session::Banked;
                        const char* what =
                            arm == B::Row      ? "a row at the front of list 0"
                          : arm == B::Consumed ? "kind 12/13: Object_ApplyEffect applied and "
                                                 "CONSUMED, no row - the count is the take"
                          : arm == B::Merged   ? "merged into an existing row of a related "
                                                 "kind, no new row - and the quantity is in "
                                                 "the 56-byte cache this port does not model, "
                                                 "so nothing counts up"
                                               : "REFUSED - list 0 is full, case 10 returns 0 "
                                                 "and it stays in his hand";
                        heldInHand = -1;
                        std::printf("take: MDPUTSNK - object %d '%s' (kind %d) -> "
                                    "carried list %d -> %d: %s%s%s\n",
                                    takeCandidate,
                                    session.objectName(takeCandidate).c_str(),
                                    session.objectKind(takeCandidate), was, now, what,
                                    session.lastObjectEffect().empty() ? "" : "; effect: ",
                                    session.lastObjectEffect().c_str());
                        takeCandidate = -1;
                    } else if (mv == "MDNOTAKE") {
                        // `sub_46B530` (0x0046B530): a four-case switch on
                        // `dword_53AE5C`, the code MDACTION kept - 0 -> group
                        // 0x8C = 140 (H_PUTL12 -> MDLETOBJ -> H_PUTL22), 3 ->
                        // group 9 (H_PUTH12/22); 1 and 2 are groups 6 and 7.
                        // The cancel PLAYS THE PUT-BACK, and MDLETOBJ inside
                        // that group is what releases the object and swaps
                        // the camera. Until 2026-09-04 the port had no
                        // MDNOTAKE handler at all, so a cancel went straight
                        // to standing with the object still held and the
                        // take camera still up (a reader: "if I cancel the
                        // grab, the camera doesn't return").
                        const int putGroup = takeWasLow ? 140 : 9;
                        const bool got = player->enterGroupById(putGroup);
                        std::printf("take: MDNOTAKE - cancel: the put-back plays (%s, group %d%s)\n",
                                    takeWasLow ? "LOW H_PUTL" : "HIGH H_PUTH", putGroup,
                                    got ? "" : " - not in this bank");
                    } else if (mv == "MDLETOBJ") {
                        if (takeCam) {                // the same mode-16 swap
                            takeCamRequest(3);
                            std::printf("take: camera mode 16 requested - back to the follow camera over 30 frames\n");
                        }
                        // The put-back matches the take's HEIGHT, the same way
                        // and from the same decision: the engine keeps
                        // `dword_53AE5C = (ret == 2) ? 3 : 0` at MDACTION and
                        // `sub_46B530` turns it back into a group - case 0 ->
                        // 140, case 3 -> 9. H1Avnt.CTL: 140 is H_PUTL12/22 and
                        // 9 is H_PUTH12/22, the mirrors of the two takes. The
                        // switch is best-effort; a bank without the group
                        // leaves the machine where it is, which is what
                        // `Cef_FindGroupById` returning nothing does.
                        // Inside the put group already (MDNOTAKE entered it):
                        // `sub_41C540(actor, 0)` - the object back to the world.
                        std::printf("take: MDLETOBJ - object %d put back where it was\n",
                                    takeCandidate);
                        session.putHeldObjectBack();
                        heldInHand = -1;
                        takeCandidate = -1;
                    } else if (mv == omk::kMoveOpenSneak) {
                        // ROW 0, and the other end of the same table. TAB is
                        // the Aventure scheme's "Ouvrir sneak" (bit 0x2000);
                        // H1Avnt/F1Avnt group 0 has an entry waiting on
                        // exactly that bit whose flag-2 alias redirects
                        // through its GoTo into group 6, and group 6's child
                        // names `MDSNEAK0` (`actor/moves.h` quotes the whole
                        // chain and `sub_0046ADF0` with it).
                        //
                        // The handler's own gate is `sub_41A350(actor)`: when
                        // the actor carries a pending target at `+164` it
                        // raises event 10 for that object and does NOT open
                        // the sneak - which is the SAME `+164` MDGETOBJ links
                        // an object to just above. Not modelled, so the sneak
                        // always opens; with nothing held that is what the
                        // engine does, and holding something is the case to
                        // come back to.
                        //
                        // What IS reproduced is the pair around it: event 25
                        // opening object list 0 on the way in, and event 26
                        // if the open fails or when the close comes.
                        if (walk || playerScreen >= 0) continue;   // already up
                        inv.openList(0);          // Game_RaiseEvent(25, 0)
                        playerScreen = omk::kScreenSneak;
                        std::printf("MDSNEAK0: event %d opens object list 0, "
                                    "screen %d\n", omk::kEventSneakOpen,
                                    omk::kScreenSneak);
                    }
                }
                // `Actor_TickNpc`: `Actor_ApplyMotion`, then `Actor_ScanZones`
                // at the position it left - the Session's scan reads this on
                // its next frame (wave B, T15). Facing in the +420 degrees.
                session.setPlayerPosition(player->pos(), player->facing());
                // ---- THE ACTION BUTTON -------------------------------
                //
                // `Game_RaiseEvent(6, 4)` from the input handler, which is
                // `Game_HandleEvent` case 6:
                //
                //     if (g_DialogState == 3 || a2 != 4 || !dword_4E6B24)
                //         return 0;
                //     dword_4E6C90 = 1;
                //
                // so a press with no prompt slot taken never reaches the pump,
                // and the pump clears the flag at the end of its slot loop.
                // `Session::pressAction` models all of that, including handing
                // the tracked position to `talkToPedestrian` for the crowd.
                //
                // **AND IT FIRES ON THE EDGE**, which is a correction: this
                // used to press on the LEVEL, reasoning that "the pump clears
                // the flag each frame, so a held button is one press per
                // frame". A reader measured what that does - a normal 0.2 s
                // press counted as **six** presses - and the reasoning was
                // wrong about where the raise comes from.
                //
                // `Game_RaiseEvent(6, 4)` is NOT raised by the input handler.
                // Its three sites are all ACTOR functions - `sub_466B60`,
                // `Actor_TickUiHeld` (the ACTOR_STATE 9/17 tick) and
                // `sub_467950` - and every one uses the RETURN as a VETO:
                // `if (!a1[41] || Game_RaiseEvent(6, 4)) goto ...`. So the
                // engine never reads an action BIT here at all: the press
                // reaches the actor through the `.CTL` channel, whose
                // transition matching fires once per press because the second
                // frame of a held button finds the actor already in the state.
                // The channel is the edge filter, and the port does not route
                // the action through it - so the viewer must supply the edge
                // itself or the world sees a press a frame.
                //
                // It is also what the take needs to be usable at all: the
                // mechanic is press -> the take animation and the title, press
                // AGAIN -> the inventory (omk-play 69), and six presses inside
                // one keystroke makes those two steps unreachable.
                //
                // Bit 0x10 is "Action / Utiliser" - group 0 action 4 of
                // `tables/key_bindings.json`, keyboard 28, DIK_RETURN. The
                // Session was modelling the press and the zone registry was
                // arming its slots, and nothing in the viewer ever pressed it,
                // so no object could be taken and no pedestrian talked to
                // (`todo/omk-play.md` 65).
                if (actionFromMove && !session.dialogOpen()) {
                    const int armed = session.zones().armedCount();
                    const std::int16_t z = session.zones().armedZone();
                    // omk-play 66: EVERY press is reported, with where he
                    // stood, because the question is whether anything arms at
                    // the anneaux (7288 -80 3015) at all. The rings are
                    // OBJECTS 162 and no zone with an activate script covers
                    // them, so a press there that arms NOTHING is the result
                    // that confirms a second, object-proximity scan.
                    const float* pp = player ? player->pos() : nullptr;
                    // ...and WHAT IS IN THE HAND, because that is what the
                    // pump's dry run and `var.set.used_object` both read, and
                    // a press that finds it empty takes a different arm of the
                    // zone's script entirely.
                    const int hs = session.heldSlotOf(-1);
                    if (session.pressAction())
                        std::printf("action: zone %d activated (%d slot%s armed) at %.0f %.0f %.0f"
                                    " - hand slot %d, object %d\n",
                                    z, armed, armed == 1 ? "" : "s",
                                    pp ? pp[0] : 0.0f, pp ? pp[1] : 0.0f, pp ? pp[2] : 0.0f,
                                    hs, hs >= 0 ? session.objectSlotId(hs) : -1);
                    else
                        std::printf("action: pressed at %.0f %.0f %.0f - %d slots armed, "
                                    "nothing interactable in reach\n",
                                    pp ? pp[0] : 0.0f, pp ? pp[1] : 0.0f, pp ? pp[2] : 0.0f,
                                    armed);
                    (void)actionTold;
                }
                // `Walk_ProbeGround` -> `Game_HandleEvent` case 9: the decor
                // under his feet, over every shown slot. The active row - the
                // zone tables, the area's music - follows the FEET, not the
                // load (T11 finding 2).
                {
                    const float* pp = player->pos();
                    const int under = omk::decorUnder(worldDecors, pp[0], pp[1], pp[2]);
                    if (under >= 0 && under != session.activeArea()) {
                        session.playerOnArea(under);
                        std::printf("frame %ld: event 9 - his feet are on AREA %d's decor; "
                                    "the active row follows (zones re-registered: %zu)\n",
                                    n, under, session.zones().registered().size());
                    }
                }
            }
        }

        // The script asked for a track. A LEVEL, not an event: the engine's
        // own handler skips `music.play` when the track is already going, so
        // this only acts on a change.
        // A LEVEL, not an event: the engine's own handler skips `music.play`
        // when the track is already going, so this acts only on a change.
        if (session.musicTrack() != playingTrack) {
            playingTrack = session.musicTrack();
            std::printf("audio: music switch to %d - the stream is flushed; "
                        "one-shots already playing are NOT\n", playingTrack);
            front.flushAudio();
            if (music.play(fs, adpcmTables, playingTrack, session.musicLoops()))
                std::printf("music: track %d, %.1f s%s\n", music.track(),
                            music.seconds(), music.looping() ? ", looping" : "");
        }
        // Keep about a second of it in the device. The player decides what
        // comes next - including whether the track wraps - so all this does
        // is ask for more and hand it over.
        if (music.playing() && front.queuedSeconds() < 1.0) {
            std::vector<float> chunk;
            music.pull(chunk, 44100);
            front.queueAudio(chunk);
        }
        // `media.play` is an EVENT, not a level like music: each announcement
        // is one play, and the handler's `Morph_Stop()` means a second one
        // CUTS the first. That is the whole of the engine's policy here.
        for (const int mediaId : voices.poll(session.announced())) {
            const auto vo  = voiceLib.resolve(fs, mediaId);
            const auto pcm = voiceLib.decode(fs, adpcmTables, vo);
            std::printf("media.play %d (%s) -> %s%s, %.2f s\n", mediaId,
                        vo.objectName.c_str(),
                        vo.file.empty() ? (vo.image ? "IMAGES\\" : "nothing")
                                        : vo.file.c_str(),
                        vo.substituted ? " [JINGOFF3 substitution]" : "",
                        static_cast<double>(pcm.size()) / omk::kAdpcmRate);
            front.stopSound(voiceOverShot);
            voiceOverShot = pcm.empty() ? -1
                : front.playSound(resampleToDevice(pcm, 1, omk::kAdpcmRate, 44100));
            // ...and the TEXT. Step 13 of the handler is `Subtitle_Show` of
            // the buffer step 6 built: the record's +280 description, with a
            // `{C}` (centre) prefix when the player's ACTOR_STATE is 3 or
            // 15. Shown whatever the voice was - the JINGOFF3 substitute is
            // still followed by the line the player reads. An IMAGE (kind
            // 16) takes the other arm and shows a bitmap instead, which this
            // file does not draw.
            // A new media.play frees whatever bitmap was up, then this one
            // either loads its own or speaks.
            mediaBmp = omk::Surface{};
            if (vo.image && !vo.stem.empty()) {
                if (const auto bp = fs.resolve("IMAGES/" + vo.stem + ".BMP")) {
                    mediaBmp = omk::surfaceFromBmp(omk::DataFs::readPath(*bp));
                    std::printf("media.play %d is a DOCUMENT: IMAGES/%s.BMP %dx%d\n",
                                mediaId, vo.stem.c_str(), mediaBmp.w, mediaBmp.h);
                }
            }
            const auto& objs = voiceLib.objects();
            if (!vo.image && mediaId >= 0 && static_cast<std::size_t>(mediaId) < objs.size()) {
                std::string text = objs[static_cast<std::size_t>(mediaId)].description;
                const int st = player ? static_cast<int>(player->state()) : -1;
                if (!text.empty() && (st == 3 || st == 15)) text = "{C}" + text;
                mediaText = text;
                const long ms = std::max<long>(2000L, 80L * static_cast<long>(text.size()));
                mediaTextFrames = text.empty() ? 0 : (ms * 30 + 999) / 1000;
                if (!text.empty())
                    std::printf("media.play %d subtitle for %ld frames: %s\n", mediaId,
                                mediaTextFrames, text.c_str());
            }
        }

        // A conversation started. The world stops around one - that is the
        // engine (`Dialog_TickUI` owns the frame) - and the frontend has NO
        // dialogue UI yet, so there is nothing here that can play it. Ending
        // it is what the headless boot does to keep the chain moving, and it
        // is a STAND-IN: the line is not spoken, the replies are not offered,
        // and saying so is the point.
        // A conversation is on screen. The world stops around one - that is
        // the engine, `Dialog_TickUI` owns the frame - and NOTHING here
        // advances it. The voice plays; the player presses ENTER for NEXT,
        // then picks a reply with the arrows and ENTER. That is the same rule
        // the start menu follows and the same one `tools/omkweb.html` plays a
        // conversation by: the game never shows the NPC line and the menu
        // together, and the click that ends the line is what reveals the menu.
        //
        // Still missing, and said rather than hidden: the line and the replies
        // are printed here, not DRAWN, and the dialogue cameras and the
        // speaker's pose the web viewer carries are not wired in - so what is
        // on screen during a conversation is the world camera the script last
        // set.
        if (session.dialogOpen()) {
            const auto& dlg = session.dialogue();
            // A new conversation: stage the speaker (the model itself is
            // loaded on `character.show`, which comes first).
            if (session.dialogue().conversation().id != speakerConv) {
                speakerConv = session.dialogue().conversation().id;
                speakerReady = false;
                speakerSolved = false;
                speakerModel = session.speakerModel();
                speakerMeshes.clear();
                // The BODY is a `Staged` like any other - one `CharModel` per
                // model name, shared with whoever else wears it. The meshes
                // are copied here only for `rootTrackOf` and the report.
                CharModel* cm = charModelFor(speakerModel);
                if (cm && cm->ready) {
                    speakerMeshes = cm->meshes;
                    // Where he stands: the line cameras' rays, dropped onto
                    // the set's own walkable floor.
                    omk::TriangleSoup soup;
                    if (const auto so = fs.resolve("MESHES/DECORS/" + worldSet + ".3DO"))
                        soup = omk::collisionSoup(omk::DataFs::readPath(*so),
                                                  omk::SoupKind::Walkable);
                    const auto st = omk::stageSpeaker(
                        session.dialogue().conversation().cams,
                        omk::lineCameraIds(session.dialogue().conversation()),
                        soup.empty() ? nullptr : &soup);
                    // The authored PATH wins when the scene gives one: it is
                    // where the object actually puts him, and a camera solve
                    // is the fallback for a conversation whose speaker no
                    // scene object drives. The staging below applies it only
                    // to a body nothing else has placed.
                    for (int k = 0; k < 3; ++k) speakerAt[k] = st.pos[k];
                    speakerSolved = st.valid;
                    speakerReady = st.valid;
                    std::printf("speaker: %s (%zu meshes, %zu corners), %d rays "
                                "converge scatter %.1f, stands at %.0f %.0f %.0f%s\n",
                                speakerModel.c_str(), speakerMeshes.size(),
                                cm->rest.corners.size(), st.rays, st.scatter,
                                st.pos[0], st.pos[1], st.pos[2],
                                st.onFloor ? " on the walkable floor" : "");
                } else {
                    std::printf("speaker: no model for the conversation - "
                                "its cameras will look at nothing\n");
                }
            }
            if (dlg.lineChanged()) {
                session.clearDialogLineChanged();
                // The line's own `.3DM` drives the pose.
                speakerVoice = dlg.voice();
                speakerTracks = omk::NodeTracks{};
                speakerMorph.clear();
                lineIdleFrame = sceneFrameLast;
                if (speakerReady && !speakerVoice.empty())
                    if (const auto ma = fs.resolve("MORPH/" + speakerVoice + ".3DM")) {
                        speakerMorph = omk::DataFs::readPath(*ma);
                        speakerTracks = omk::nodeTracks(
                            speakerMorph, omk::rootTrackOf(speakerMeshes));
                        const CharModel* fm = charModels.count(speakerModel)
                            ? &charModels.at(speakerModel) : nullptr;
                        std::printf("  face: mesh %d, %d verts; the line supplies %s\n",
                                    fm ? fm->face.mesh : -1, fm ? fm->face.count : 0,
                                    fm && fm->face.valid() ? "them" : "none");
                    }
                if (!conversations++)
                    std::printf("--- conversation: ENTER for next, "
                                "arrows + ENTER to reply ---\n");
                std::printf("[%s, %.1f s] %s\n",
                            dlg.voice().empty() ? "no voice" : dlg.voice().c_str(),
                            dlg.lineSeconds(), cp1252ToUtf8(dlg.lineText()).c_str());
                front.stopSound(voiceOverShot); voiceOverShot = -1;   // one streamer
                front.stopSound(voiceShot);
                voiceShot = dlg.pcm().empty() ? -1 : front.playSound(
                        resampleToDevice(dlg.pcm(), dlg.channels(), 22050, 44100));
                replySel = 0; menuShown = false; lineScroll = 0;
            }
            if (dlg.phase() == omk::DialogPhase::Menu && !menuShown) {
                menuShown = true;
                replySel = -1;
                for (std::size_t k = 0; k < dlg.replies().size(); ++k) {
                    const auto& r = dlg.replies()[k];
                    if (!r.available) continue;
                    if (replySel < 0) replySel = static_cast<int>(k);
                    std::printf("   %s %zu. %s\n",
                                static_cast<int>(k) == replySel ? "->" : "  ",
                                k + 1, cp1252ToUtf8(r.text).c_str());
                }
            }
            // THE DIALOGUE MENU IS SILENT. `Dialog_TickUI` (0x0046A200, 388
            // lines) makes no sound call, and neither does the dispatch that
            // drives it in `Actors_TickAll`; the per-screen move/confirm/open
            // slots belong to the interface SCREENS (UI 3), and a conversation
            // is not one. Whatever a reply plays is its branch ACTION's own
            // script. A first version borrowed the open screen's blips here,
            // which a reader heard as wrong.
            // THE MENU COUNTS PRESSES, so it reads the EDGES (omk-play 74).
            // A reader on a bench: one press of Enter answered several times
            // over and one tap of a direction ran the selection round the
            // list. `Game_Frame` hands the dialogue `dword_90E0E0` - a word
            // ZEROED EVERY FRAME and rebuilt by the per-action callbacks (the
            // label-less `or al, N` family at 0x42B8F0, table entries nothing
            // calls directly) - and the call site splices the RAW word in for
            // exactly two bits:
            //
            //     case 2: Dialog_TickUI(2, dword_90E0E0 | dword_4E9718 & 0xC);
            //
            // 0xC is bits 2 and 3, which elsewhere nudge a camera by 6.0 a
            // frame. Splicing the raw word in by name for those two is only
            // meaningful if `dword_90E0E0` is NOT raw - so the confirm and the
            // menu steps are edges, and the two camera bits are the exception
            // the engine had to write out.
            if (edgeBits & omk::kUiConfirm) {
                if (dlg.phase() == omk::DialogPhase::Menu) {
                    if (replySel >= 0 &&
                        replySel < static_cast<int>(dlg.replies().size())) {
                        session.dialogChoose(dlg.replies()[
                            static_cast<std::size_t>(replySel)].branch);
                    }
                } else {
                    // The press that leaves a line: `Dialog_TickUI` case
                    // 2/7/8 -> `Morph_Stop`, which stops the voice buffer.
                    front.stopSound(voiceShot);
                    voiceShot = -1;
                    session.dialogNext();
                }
            } else if (dlg.phase() == omk::DialogPhase::Menu &&
                       (edgeBits & (omk::kUiUp | omk::kUiDown))) {
                // Step to the next AVAILABLE reply, the way `Ui_MoveSelection`
                // steps over an unselectable row.
                const int n = static_cast<int>(dlg.replies().size());
                const int dir = (edgeBits & omk::kUiDown) ? 1 : -1;
                for (int step = 1; step <= n; ++step) {
                    const int c = ((replySel + dir * step) % n + n) % n;
                    if (dlg.replies()[static_cast<std::size_t>(c)].available) {
                        if (c != replySel) replySel = c;
                        break;
                    }
                }
            }
            if (!session.dialogOpen())
                std::printf("--- conversation over ---\n");
        }

        // A script asked for a screen, or the PLAYER did. Which screen is the
        // SESSION's answer or the special move's, never this file's.
        if (!walk && (session.pendingUiScreen() >= 0 || playerScreen >= 0)) {
            const bool fromScript = playerScreen < 0;
            const int want = fromScript ? session.pendingUiScreen() : playerScreen;
            playerScreen = -1;
            // ...with the interface's OWN selections, which outlive the
            // walk. `list+2` is a field of a static record in the engine's
            // data segment: seeded by the linker, written by
            // `Ui_MoveSelection`, overwritten only where an open callback
            // writes it, and never reset. So the device remembers the verb
            // you last used and the row you were on across closing and
            // reopening it, and a walk built fresh each open would forget.
            auto fresh = std::make_unique<omk::UiWalk>(w, uiLists);
            if (!fresh->open(want)) {
                // A script's screen must be in the tree - the boot depends on
                // it. The PLAYER's need not be fatal: `sub_0046ADF0`'s own
                // failure arm raises event 26, logs "cant start sneak" and
                // returns, and the game carries on.
                if (fromScript) {
                    std::fprintf(stderr, "screen %d has no panel in the tree\n", want);
                    return 1;
                }
                std::printf("cant start sneak: screen %d has no panel in the "
                            "tree - event %d\n", want, omk::kEventSneakClose);
                inv.closeList();
            } else {
                walk = std::move(fresh);
                openScreen = want;
                screenFromScript = fromScript;
                std::printf("screen %d %s - arrows move, ENTER confirms, "
                            "TAB closes\n", openScreen,
                            fromScript ? "is asking" : "opened by the player");
                // The screen's own sounds, by slot. Which slot is which is
                // `sub_482FE0`'s answer - it dispatches on the INPUT BIT - not
                // a guess from the file names. Nothing plays when a screen
                // opens: the engine has no such slot, and the one this used to
                // play was the MOVE sound fired at the wrong moment.
                sndMove    = loadSlot(openScreen, omk::UiWidgets::kSoundMove);
                sndConfirm = loadSlot(openScreen, omk::UiWidgets::kSoundConfirm);
                sndBack    = loadSlot(openScreen, omk::UiWidgets::kSoundBack);
            }
        }

        if (walk) {
            // What the person typed goes to the field before the navigation
            // bits, because `Confirmer` opens by testing the field's cursor
            // and writes nothing when it is empty (docs/UI.md) - so a confirm
            // in the same frame as the last letter must see the letter.
            if (!host.text.empty()) walk->typeName(host.text);
            if (bits) {
                const int wasSel = walk->selection(), wasList = walk->currentList();
                walk->press(bits);
                // The MOVE sound fires when the selection actually MOVED, not
                // on every press: `Ui_MoveSelection` steps over unselectable
                // rows and a pinned list stops at its ends, so a key that
                // changes nothing must make no sound either.
                // One sound per press, chosen by the BIT, which is what
                // `sub_482FE0` does. The move is still gated on the selection
                // having actually moved - `Ui_MoveSelection` steps over
                // unselectable rows and a pinned list stops at its ends.
                if (bits & omk::kUiConfirm)      blip(sndConfirm);
                else if (bits & omk::kUiBack)    blip(sndBack);
                else if (walk->selection() != wasSel ||
                         walk->currentList() != wasList) blip(sndMove);
            }
            // CLOSING THE SNEAK is not closing a script's screen, and the
            // difference is the whole reason `screenFromScript` exists.
            // `Ui_CloseSneakFamily`'s parameter-0 arm - the one that serves
            // SNEAK - closes screen 35, frees the three `.3DO` previews its
            // open loaded (`setek`, `anneau`, `imager`), raises event 26 and
            // falls into the generic close. No answer is posted anywhere,
            // because `sub_0046ADF0` opened it with a waiting context of -1:
            // nothing is parked on it. Handing `session.answerUi(-1)` to a
            // sneak close would release whatever script happened to be
            // suspended elsewhere.
            //
            // (`docs/UI.md` attributes that arm and the closing animation the
            // other way round - it reads the scene-freeing arm as VIDEOPHONE
            // and the oscillator refusal as SNEAK. The branch decides it:
            // parameter 0 is SNEAK and takes `loc_49B6A5`, the scene-freeing
            // one; the refusal is parameter 2's. Corrected there too.)
            const bool leaving = walk->answer() >= 0 || walk->closed();
            if (leaving && !screenFromScript) {
                std::printf("screen %d closed by the player - event %d, object "
                            "list %d\n", openScreen, omk::kEventSneakClose,
                            inv.openedList());
                inv.closeList();          // Game_RaiseEvent(26, 0)
                walk.reset();
                openScreen = -1;
                screenFromScript = true;
            } else if (walk->answer() >= 0) {
                std::printf("screen %d answered %d -> the script resumes\n",
                            openScreen, walk->answer());
                session.answerUi(walk->answer());
                walk.reset();
                openScreen = -1;
            } else if (walk->closed()) {
                // The player LEFT the screen without choosing. `UI_OpenScreen`
                // parks the caller and presets the answer `dword_930750` to
                // -1; every close path - `UI_SendAnswer` and the two ESC/TAB
                // closes in 21_d3d.c (0x4034xx, 0x4035xx) - posts event 5
                // with whatever it holds, and `Game_HandleEvent` case 5 does
                // `Var_Set(var, answer)` and writes status 1 regardless. So
                // leaving IS an answer, -1, and the script resumes with it -
                // AREA 118's takes that branch as the unseeded opening. This
                // used to `break` out of the loop, a quit the engine has no
                // counterpart for.
                std::printf("screen %d closed without an answer -> -1, "
                            "the script resumes\n", openScreen);
                session.answerUi(-1);
                walk.reset();
                openScreen = -1;
            }
        }

        if (session.areasEntered() != lastArea) {
            lastArea = session.areasEntered();
            if (lastArea > 0) std::printf("area transition %d\n", lastArea);
        }

        // The absolute world cameras the script has set, as rays. Collected
        // BEFORE the character is staged, because `character.show` and the two
        // `camera.set`s before it land in the same VM run - AREA 118 sets 2172
        // at pc 1170, 2148 at 1177 and shows Kay'l at 1184 - so a solve that
        // ran first would have nothing to solve over.
        if (const omk::WorldCamera* tc = session.cameraTarget()) {
            if (tc->absolute() && tc->id != lastRayCam) {
                lastRayCam = tc->id;
                omk::CameraRay r;
                for (int k = 0; k < 3; ++k) { r.eye[k] = tc->eye[k]; r.at[k] = tc->at[k]; }
                worldRays.push_back(r);
            }
        }

        // ---- WHOEVER IS ON SCREEN --------------------------------------
        //
        // `character.show` is what puts a character in the world, and AREA 118
        // does it about six seconds before `dialog.start` - so loading the
        // model when a conversation opens drew the arrival as a black screen.
        // `Session::shown()` is now EVERY attached actor of both resident
        // slots (the alley's passers-by among them) followed by whatever a
        // script showed, so this stages them all rather than `front()`.
        {
            for (auto& up : staged) up->seen = false;
            const int playerId = session.playerActor();
            for (const auto& sh : session.shown()) {
                // In adventure mode the CONTROLLER owns the player's body; a
                // second one here would draw him twice.
                if (adventure && player && sh.actor == playerId) continue;
                Staged* s = nullptr;
                for (auto& up : staged) if (up->actor == sh.actor) { s = up.get(); break; }
                if (!s) {
                    staged.push_back(std::make_unique<Staged>());
                    s = staged.back().get();
                    s->actor = sh.actor;
                    s->model = sh.model;
                    s->bank  = sh.bank;
                    s->mo = charModelFor(sh.model);
                    s->bk = charBankFor(sh.bank);
                    ++stagedEver;
                    stagedIds.push_back(sh.actor);
                    std::printf("frame %ld: staged actor %d %s (bank %s, %zu meshes, "
                                "%zu textures) at %.0f %.0f %.0f facing %.0f - %s\n",
                                n, sh.actor,
                                sh.model.empty() ? "(no model)" : sh.model.c_str(),
                                sh.bank.empty() ? "none" : sh.bank.c_str(),
                                s->mo ? s->mo->meshes.size() : 0u,
                                s->mo ? s->mo->tex.size() : 0u,
                                sh.pos[0], sh.pos[1], sh.pos[2], sh.facing,
                                sh.fromTable ? "a placement record puts him here"
                                             : "shown by a script, no placement of his own");
                }
                s->seen = true;
                // A placement record names a spot on the GROUND; a script
                // show carries none (`Session::showCharacter` pushes a bare
                // record), so such a body waits for a program or a camera
                // solve to say where he is.
                if (sh.fromTable) {
                    for (int k = 0; k < 3; ++k) s->at[k] = sh.pos[k];
                    s->facing = sh.facing;
                    if (!s->placed) { s->placed = true; s->pelvis = false; }
                }
            }
            // A conversation's speaker need not be in `shown()` at all -
            // AREA 118's is, but the camera solve is the only thing that says
            // where a speaker no table places and no script shows is standing.
            if (session.dialogOpen() && speakerReady && !speakerModel.empty()) {
                const int sp = session.dialogue().conversation().speaker;
                Staged* s = nullptr;
                for (auto& up : staged) if (up->actor == sp) { s = up.get(); break; }
                if (!s)
                    for (auto& up : staged)
                        if (up->model == speakerModel) { s = up.get(); break; }
                if (!s) {
                    staged.push_back(std::make_unique<Staged>());
                    s = staged.back().get();
                    s->actor = sp;
                    s->model = speakerModel;
                    s->mo = charModelFor(speakerModel);
                    ++stagedEver;
                    stagedIds.push_back(sp);
                    std::printf("frame %ld: staged actor %d %s for conversation %d - "
                                "no table places him and no script shows him\n",
                                n, sp, speakerModel.c_str(), speakerConv);
                }
                s->seen = true;
                if (!s->placed && speakerSolved) {
                    for (int k = 0; k < 3; ++k) s->at[k] = speakerAt[k];
                    s->placed = true;
                    s->pelvis = false;   // the solve names a spot on the GROUND
                }
            }
            for (std::size_t k = 0; k < staged.size(); ) {
                if (staged[k]->seen) { ++k; continue; }
                std::printf("frame %ld: dropped actor %d %s\n", n,
                            staged[k]->actor, staged[k]->model.c_str());
                staged.erase(staged.begin() + static_cast<long>(k));
                ++poolComposition;
            }
            // A model no staged body wears any more leaves the pool, so the
            // 64 slots a bucket key can address are not spent on the last
            // area's cast.
            for (auto it = charModels.begin(); it != charModels.end(); ) {
                bool used = (it->first == playerModel && playerReady) ||
                            it->first == speakerModel;
                for (const auto& up : staged) if (up->model == it->first) used = true;
                // ...AND the models the traffic circuit's own bodies wear.
                // The crowd's walkers and the road traffic's two vehicles are
                // not `staged` actors, so this loop was erasing their models
                // every frame while `PedStaged::mo` and `VehStaged::mo` went
                // on pointing at the freed node. The crowd never showed it
                // because a city's authored extras wear the SAME PERSOS
                // models and kept them resident by accident; `sli_fn` and
                // `moto` are worn by nothing else, so the traffic staged
                // itself once and then vanished - which is how this was
                // found (2026-09-04).
                if (!used) {
                    const auto& circuit = session.sliders();
                    for (const auto& w : circuit.movers())
                        if (w.live && w.model == it->first) { used = true; break; }
                    if (!used)
                        for (const auto& v : circuit.vehicles())
                            if (v.live && v.model == it->first) { used = true; break; }
                }
                if (used) { ++it; continue; }
                it = charModels.erase(it);
                ++poolComposition;
            }
        }

        // ---- the world, when no screen is over it -----------------------
        //
        // The sets follow the resident slots' STATE: a slot whose decor the
        // Session has in state 2 is loaded, one it took back to state 1 is
        // dropped. During a transition that is two sets at once.
        {
            bool changed = false;
            for (int slot = 0; slot < 2; ++slot) {
                const auto& rs = session.residentSlot(slot);
                const bool shown = session.slotShown(slot) && rs.area != -1;
                const std::string want = shown ? rs.set : std::string();
                const int wantArea = shown ? rs.area : -1;
                const WorldSlot& w = worldSlots[static_cast<std::size_t>(slot)];
                if (want != w.stem || wantArea != w.area) {
                    loadWorldSlot(slot, want, wantArea);
                    changed = true;
                }
            }
            if (changed) rebuildWorld();
            const WorldSlot& act = worldSlots[static_cast<std::size_t>(session.activeSlot() & 1)];
            const WorldSlot& oth = worldSlots[static_cast<std::size_t>(1 - (session.activeSlot() & 1))];
            worldSet = !act.stem.empty() ? act.stem : oth.stem;
        }

        std::fill(fb.px.begin(), fb.px.end(), std::uint16_t(0));
        const omk::WorldCamera* wc = session.camera();

        // THE DIALOGUE CAMERAS. A line uses two and travels between them:
        // `sub_4013B0` issues camera command 12 twice, the first with duration
        // -1.0 (a snap) and the second with 160.0, so the view cuts to the
        // node's first camera and moves to its second over 160 frames - 5.3 s
        // at 30, which is why the move stops well before a long line ends. The
        // menu has its own pair and its own cut.
        //
        // The clock is `DialogPlayer`'s and belongs to the LINE, not to any
        // animation - CLAUDE.md 5 has why that matters.
        omk::View dlgView;
        bool haveDlgCam = false;
        if (session.dialogOpen()) {
            const auto& dlg = session.dialogue();
            const omk::DialogCamera* ca = dlg.cameraA();
            const omk::DialogCamera* cb = dlg.cameraB();
            if (!ca) ca = cb;
            if (ca) {
                const omk::DialogCamera* cbb = cb ? cb : ca;
                const float u = dlg.cameraProgress();
                for (int k = 0; k < 3; ++k) {
                    dlgView.cam.eye[k] = ca->eye[k] + (cbb->eye[k] - ca->eye[k]) * u;
                    dlgView.cam.at[k]  = ca->at[k]  + (cbb->at[k]  - ca->at[k])  * u;
                }
                // The viewer takes the fov from whichever camera the move is
                // nearer, rather than blending it.
                dlgView.cam.hfovDeg = (u > 0.5f ? cbb : ca)->fov;
                dlgView.cam.w = dispW; dlgView.cam.h = dispH;
                // NOT ported: `RCamera` carries no ROLL, and dialogue cameras
                // use one - conversation 272's first is -15 degrees. The shot
                // is drawn upright.
                haveDlgCam = ca->absolute() && cbb->absolute();
                if (ca->id != lastDlgCam) {
                    lastDlgCam = ca->id;
                    std::printf("  dialogue camera %d -> %d (%s), fov %.0f, "
                                "roll %.0f%s\n", ca->id, cbb->id,
                                dlg.phase() == omk::DialogPhase::Menu
                                    ? "reply pair" : "line pair",
                                ca->fov, ca->roll,
                                haveDlgCam ? "" : "  [relative - not drawn]");
                }
            }
        }
        // Report the camera the script asked for, not the interpolated one -
        // mid-travel the position is still near the OUTGOING camera and
        // printing that beside the incoming id reads as a decode fault.
        if (const omk::WorldCamera* tc = session.cameraTarget()) {
            if (tc->id != lastCamera) {
                lastCamera = tc->id;
                std::printf("camera %d: eye %.0f %.0f %.0f  at %.0f %.0f %.0f  "
                            "fov %.1f roll %.1f  %s%s\n", tc->id,
                            tc->eye[0], tc->eye[1], tc->eye[2],
                            tc->at[0], tc->at[1], tc->at[2], tc->fov, tc->roll,
                            session.cameraTravel() > 0
                                ? "travelling" : "cut",
                            tc->absolute() ? ""
                              : session.playerPlaced()
                                  ? "  [relative - resolved against the player]"
                                  : "  [RELATIVE to an actor - not drawn]");
            }
        }
        // A camera whose eye or target is an OFFSET from an actor needs to
        // know where that actor IS, and until 2026-09-02 nothing here did -
        // so 1443 of the 5384 world cameras were skipped and the screen went
        // black. `actor.goto_address` now places the player (see
        // `formats/addresses.h`), so a relative camera resolves as long as he
        // has been placed. He still does not MOVE - the actor runtime is not
        // driven by this loop - so what this buys is a correct static camera,
        // not a following one.
        const bool haveRelCam = wc && !wc->absolute() && session.playerPlaced();
        // THE EDITING, if one is driving. `activeEditing` is the object whose
        // `Script_PlayScript` set the scene's active camera this frame, and
        // `editingCamera` is `Cam_PlayEditing` at its clock.
        const omk::SceneRunner::ActiveEditing* edit = session.scene().activeEditing();
        omk::CamSample editCam;
        const bool haveEdit = edit && session.scene().editingCamera(editCam);
        if (edit && edit->program != editingShown) {
            editingShown = edit->program;
            // The camera the travel starts FROM is whatever was on screen -
            // `Camera_Request` swaps the live block into `g_CameraPrev` and
            // the move interpolates away from it.
            editFromKnown = haveLastDrawn;
            for (int k = 0; k < 3; ++k) { editFromEye[k] = lastEye[k]; editFromAt[k] = lastAt[k]; }
            editFromFov = lastFov;
            editFromRoll = lastRoll;
            std::printf("frame %ld: editing %d '%s' takes the camera (mode 13): object %d '%s', "
                        "%u frames, travel %.0f%s\n",
                        n, edit->editing, edit->editingName.c_str(), edit->object,
                        edit->objectName.c_str(), edit->duration, edit->travel,
                        editFromKnown ? "" : " (nothing on screen to travel from: a cut)");
        } else if (!edit && editingShown >= 0) {
            editingShown = -1;
            std::printf("frame %ld: editing over - camera falls back to world camera %d "
                        "(a cut, Camera_Request(0) with travel 0)\n", n, session.cameraId());
        }
        const bool anyWorld = !worldSlots[0].geo.corners.empty() ||
                              !worldSlots[1].geo.corners.empty();
        // ...and the same for DRAWING it: the screen composes over the
        // world afterwards, and every sneak page's background is an opaque
        // tile map, so nothing shows through that should not.
        const bool drawWorld = !(walk && openScreen == kScreenPause) &&
                               worldReady && anyWorld &&
                               (haveDlgCam || haveEdit ||
                                (wc && (wc->absolute() || haveRelCam)));
        if (drawWorld) {
            omk::View view = dlgView;
            // THE LETTERBOX. Camera mode - conversations and cutscenes - is
            // 1.818:1, which the dialogue captures measure and which a reader
            // confirmed does NOT belong to free roaming. Everything the
            // replica draws in 3D so far is camera mode: a scripted world
            // camera or a dialogue camera, never a player-controlled one. The
            // strip's height follows from the display's WIDTH, so no
            // resolution is baked in, and the bands are simply what the
            // framebuffer was cleared to.
            view.vw = dispW;
            view.vh = static_cast<int>(dispW / 1.8181818 + 0.5);
            // ...and adventure mode is NOT camera mode: a reader confirmed
            // the strip belongs to conversations and cutscenes, so the
            // walk is drawn full-frame.
            if (adventure && followCam) view.vh = dispH;
            if (view.vh > dispH) view.vh = dispH;
            view.vx = 0;
            view.vy = (dispH - view.vh) / 2;
            if (!haveDlgCam && haveEdit) {
                // MODE 13: the editing's camera, at the object's own clock -
                // so the shot and the animation cannot drift apart, they are
                // one clock. The travel is the request's +24, `max(field, 0)`
                // frames, blended linearly from the camera last on screen the
                // way `sub_414A90` sets up the move; with no previous camera
                // (nothing drawn yet) it is a cut, which is what the engine
                // does with travel 0. ROLL is sampled and NOT applied -
                // `RCamera` carries none - and the fov is the editing's own
                // (Aapkayl's `sdb` opens at 37 and the Impasse's `intro` at
                // 90, so it is not a constant to leave alone).
                float u = 1.0f;
                if (editFromKnown && edit->travel > 0.0f)
                    u = std::min(1.0f, session.scene().editingClock() / edit->travel);
                for (int k = 0; k < 3; ++k) {
                    view.cam.eye[k] = editFromKnown
                        ? editFromEye[k] + (editCam.eye[k] - editFromEye[k]) * u
                        : editCam.eye[k];
                    view.cam.at[k]  = editFromKnown
                        ? editFromAt[k] + (editCam.at[k] - editFromAt[k]) * u
                        : editCam.at[k];
                }
                const float efov = editCam.fov > 1.0f ? editCam.fov : 75.0f;
                view.cam.hfovDeg = editFromKnown ? editFromFov + (efov - editFromFov) * u : efov;
                // THE ROLL, blended on the SHORT ARC. An angle that wraps is
                // the class of error CLAUDE.md 1 keeps: +359 and 0 are the
                // same rotation standing still and a whole turn apart once
                // interpolated, and the title sequence span its camera
                // through them.
                view.cam.rollDeg = editFromKnown
                    ? editFromRoll + shortArc(editCam.roll - editFromRoll) * u
                    : editCam.roll;
                // A rolled shot is worth one line, once: the roll was DROPPED
                // by the renderer until 2026-09-03 and a still frame cannot
                // show it, so seeing the number is how a reader knows it is
                // being applied at all.
                if (std::fabs(view.cam.rollDeg) > 0.5f && !rollTold) {
                    rollTold = true;
                    std::printf("  camera ROLL %.1f degrees is being applied "
                                "(224 of the 1073 editing cameras carry one)\n",
                                static_cast<double>(view.cam.rollDeg));
                }
                view.cam.w = dispW; view.cam.h = dispH;
            } else if (!haveDlgCam && takeCam && player) {
                // THE TAKE CAMERA (omk-play 69): mode 1's preset resolved
                // against him every frame, travelled linearly over 30 frames
                // from the camera that was on screen at the request - the
                // same blend the editings use, `sub_414A90`'s setup being one
                // mechanism for both - then held; and mode 16 travels the
                // same 30 frames back to the follow camera and hands over.
                // Full-frame, not letterboxed: nothing read ties the strip
                // to this mode, and the walk it interrupts is full-frame.
                const omk::FollowCamera tc = player->resolveOffsets(kTakeCamEye, kTakeCamAt, kTakeCamFov);
                const omk::FollowCamera& fc = player->followCamera();
                const omk::FollowCamera& to = takeCamPhase == 3 ? fc : tc;
                float u = 1.0f;
                if (takeCamPhase == 1 || takeCamPhase == 3) {
                    takeCamClock += static_cast<float>(frameSec * 30.0);
                    u = haveLastDrawn ? std::min(1.0f, takeCamClock / kTakeCamTravel) : 1.0f;
                }
                for (int k = 0; k < 3; ++k) {
                    view.cam.eye[k] = takeCamFromEye[k] + (to.eye[k] - takeCamFromEye[k]) * u;
                    view.cam.at[k]  = takeCamFromAt[k]  + (to.at[k]  - takeCamFromAt[k])  * u;
                }
                view.cam.hfovDeg = takeCamFromFov + (to.fov - takeCamFromFov) * u;
                view.cam.rollDeg = 0.0f;
                view.cam.w = dispW; view.cam.h = dispH;
                if (u >= 1.0f) {
                    if (takeCamPhase == 1) takeCamPhase = 2;
                    else if (takeCamPhase == 3) { takeCam = false; takeCamPhase = 0; }
                }
            } else if (!haveDlgCam && adventure && followCam) {
                // The controller's follow camera: the world camera's offsets
                // resolved against HIS position and facing every frame, with
                // the engine's lag (player.h quotes sub_415D10/sub_415E60).
                const omk::FollowCamera& fc = player->followCamera();
                for (int k = 0; k < 3; ++k) {
                    view.cam.eye[k] = fc.eye[k];
                    view.cam.at[k]  = fc.at[k];
                }
                view.cam.hfovDeg = fc.fov;
                view.cam.rollDeg = 0.0f;      // the follow camera carries none
                view.cam.w = dispW; view.cam.h = dispH;
            } else if (!haveDlgCam) {
                // A relative point is `subjectPos - R(yaw) * offset`, which is
                // what `sub_415D10`/`sub_415E60` do; an absolute one is passed
                // through. `resolveCamera` handles both per point, because the
                // engine decides per point and 959 of the 1443 relative
                // cameras are relative in ONE of their two.
                //
                // ...AGAINST THE SAME SUBJECT POINT THE FOLLOW CAMERA USES.
                // `session.playerPos()` is his FEET - the ground point the
                // walker keeps - and a relative camera's offset is measured
                // from the pelvis, which is why `resolveSteady` subtracts
                // `camLift_` (Y points down, so subtracting RAISES). The
                // follow path did that from issue 49 and this one did not, so
                // every scripted shot naming a subject sat a whole lift too
                // low - about 42 units for HO1_FNM, and visibly so on AREA
                // 222's tutorial shots 4290/4291/4292
                // (`todo/omk-play.md` 57).
                const float lift = player ? player->cameraLift() : 0.0f;
                const float* pp0 = session.playerPos();
                const float subj[3] = {pp0[0], pp0[1] - lift, pp0[2]};
                const omk::ResolvedCamera rc = omk::resolveCamera(
                    *wc, subj, session.playerYaw());
                for (int k = 0; k < 3; ++k) {
                    view.cam.eye[k] = rc.eye[k];
                    view.cam.at[k]  = rc.at[k];
                }
                view.cam.hfovDeg = wc->fov > 1.0f ? wc->fov : 75.0f;
                view.cam.rollDeg = wc->roll;   // already wrapped to (-180,180]
                view.cam.w = dispW; view.cam.h = dispH;
            }
            // AN INSTRUMENT OVERRIDE, and nothing the engine does: `--eye`
            // and `--at` (and `--fov`) replace whatever camera the frame
            // chose, so a shot can be framed on a body the game's own camera
            // is not looking at. `--scene` has taken the same three since it
            // was written; this makes them work with the Session running.
            if (haveEye && haveAt) {
                for (int k = 0; k < 3; ++k) {
                    view.cam.eye[k] = eyeA[k];
                    view.cam.at[k]  = atA[k];
                }
                if (fovA > 1.0f) view.cam.hfovDeg = fovA;
                view.cam.w = dispW; view.cam.h = dispH;
            }
            // What is on screen this frame, for the next editing to travel
            // from.
            for (int k = 0; k < 3; ++k) { lastEye[k] = view.cam.eye[k]; lastAt[k] = view.cam.at[k]; }
            lastFov = view.cam.hfovDeg;
            lastRoll = view.cam.rollDeg;
            haveLastDrawn = true;
            // ---- THE TEXTURE POOL ------------------------------------
            //
            // The set's textures, then one section per staged MODEL, then the
            // player's, then the sprites'; a batch's slot is its material plus
            // its owner's base, which is the engine's own indexing (the bucket
            // key's low six bits, ASSETS 4b) and not a second mechanism.
            // Rebuilt on a COMPOSITION change rather than a size change: two
            // models with the same texture count swapping is exactly what a
            // size test cannot see.
            const bool drawPlayer = adventure && playerReady;
            // ---- THE WORLD'S PROPS -----------------------------------
            //
            // Every prop of the resident chunks whose DB state has bit 1 -
            // `object.show` sets it, `object.hide` clears it - drawn at the
            // placement `Area_Load` converted: position in inches, rotation
            // in degrees off a 4096-per-turn integer. `Object_SetPlacement`
            // gives the node `o3de_SetNodePos(pos)` and
            // `Matrix3x3_FromEulerAngles(rot)`, so a corner is `M * local +
            // pos` with M applied as a ROW vector, the convention
            // `rotateYaw` and `resolveCamera` already use.
            propGeo.corners.clear();
            propGeo.batches.clear();
            propGeo.cornerMesh.clear();
            propBatchOwner.clear();
            {
                const auto shown = session.props();
                for (const auto& pr : shown) {
                    // THE OBJECT IN HIS HAND (omk-play 69). `sub_41C490`, the
                    // MDGETOBJ hand-over, unlinks the prop's node from the
                    // world and re-links it under the actor's node at +44
                    // with its local transform zeroed - and +44 is
                    // `o3de_FindMeshByName(model, "Maing")` (04_sys.c 5506,
                    // a strstr: the model's `UMaing`), the LEFT hand. So from
                    // the grab until `sub_41C540` puts it back or the bank
                    // hides it, the object is drawn riding the left hand's
                    // composed pose, through the same model-to-world the
                    // player's own corners take. A reader described the
                    // original: "the camera movement shows the object in the
                    // hand of the player, when they have to confirm the grab
                    // or not" - which is what the mode-1 camera frames.
                    // `shown` is object-state bit 2, which the hold leaves
                    // set (the bank clears bit 0 later), so a held prop is
                    // still "shown" - the engine simply draws its node where
                    // the hierarchy now puts it, the hand, and so does this.
                    const bool held = player && heldInHand >= 0 && pr.id == heldInHand;
                    if (!pr.shown && !held) continue;
                    const auto& objs = voiceLib.objects();
                    if (pr.id < 0 || static_cast<std::size_t>(pr.id) >= objs.size()) continue;
                    const PropModel* pm = propModelFor(objs[static_cast<std::size_t>(pr.id)].stem);
                    if (!pm || !pm->ready) continue;
                    if (held) {
                        const omk::NodeTracks* pt = player->poseTracks();
                        const std::vector<omk::MeshPose> pose = pt
                            ? omk::composePose(playerMeshes, *pt, player->poseFrame(), false)
                            : omk::composePose(playerMeshes, omk::NodeTracks{}, 0, false);
                        int hand = -1;      // the LAST strstr hit, as o3de_Traverse leaves it
                        for (std::size_t i = 0; i < playerMeshes.size(); ++i)
                            if (std::strstr(playerMeshes[i].name, "Maing")) hand = static_cast<int>(i);
                        if (hand < 0 || static_cast<std::size_t>(hand) >= pose.size()) continue;
                        const omk::MeshPose& hp = pose[static_cast<std::size_t>(hand)];
                        const float* pp = player->pos();
                        const float yaw = player->facing();
                        const std::size_t base = propGeo.corners.size();
                        for (const auto& c : pm->rest.corners) {
                            omk::Corner w = c;
                            const float local[3] = {c.x - pm->origin[0], c.y - pm->origin[1],
                                                    c.z - pm->origin[2]};
                            float r[3];
                            omk::qrot(hp.q, local, r);              // the hand's rotation
                            const float in[3] = {hp.pos[0] + r[0] - playerRootXZ[0], hp.pos[1] + r[1],
                                                 hp.pos[2] + r[2] - playerRootXZ[1]};
                            float o[3];
                            omk::rotateYaw(yaw, in, o);             // the player's model-to-world
                            w.x = o[0] + pp[0];
                            w.y = o[1] + pp[1] - playerFeet + lastRootDrop;
                            w.z = o[2] + pp[2];
                            propGeo.corners.push_back(w);
                        }
                        for (const auto& b : pm->rest.batches) {
                            omk::Batch nb = b;
                            nb.start += static_cast<int>(base);
                            propGeo.batches.push_back(nb);
                            propBatchOwner.push_back(pm);
                        }
                        static bool heldTold = false;
                        if (!heldTold) {
                            heldTold = true;
                            std::printf("prop %d in the LEFT HAND (mesh %d '%s') - drawn on the hand node\n",
                                        pr.id, hand, playerMeshes[static_cast<std::size_t>(hand)].name);
                        }
                        continue;
                    }
                    const double rx = pr.rotDeg[0] * 0.0174532925199433;
                    const double ry = pr.rotDeg[1] * 0.0174532925199433;
                    const double rz = pr.rotDeg[2] * 0.0174532925199433;
                    const double cx = std::cos(rx), sx = std::sin(rx);
                    const double cy = std::cos(ry), sy = std::sin(ry);
                    const double cz = std::cos(rz), sz = std::sin(rz);
                    // Matrix3x3_FromEulerAngles, row-vector: Rz * Ry * Rx as
                    // the engine composes it (player.h quotes the same call).
                    const double m00 =  cy * cz, m01 =  cy * sz, m02 = -sy;
                    const double m10 = sx * sy * cz - cx * sz;
                    const double m11 = sx * sy * sz + cx * cz;
                    const double m12 = sx * cy;
                    const double m20 = cx * sy * cz + sx * sz;
                    const double m21 = cx * sy * sz - sx * cz;
                    const double m22 = cx * cy;
                    const std::size_t base = propGeo.corners.size();
                    for (const auto& c : pm->rest.corners) {
                        omk::Corner w = c;
                        // relative to the model's own root, then placed
                        const double lx = c.x - pm->origin[0];
                        const double ly = c.y - pm->origin[1];
                        const double lz = c.z - pm->origin[2];
                        w.x = static_cast<float>(lx * m00 + ly * m10 + lz * m20 + pr.pos[0]);
                        w.y = static_cast<float>(lx * m01 + ly * m11 + lz * m21 + pr.pos[1]);
                        w.z = static_cast<float>(lx * m02 + ly * m12 + lz * m22 + pr.pos[2]);
                        propGeo.corners.push_back(w);
                    }
                    for (const auto& b : pm->rest.batches) {
                        omk::Batch nb = b;
                        nb.start += static_cast<int>(base);
                        propGeo.batches.push_back(nb);
                        propBatchOwner.push_back(pm);
                    }
                    if (propsTold.insert(pr.id).second)
                        std::printf("prop %d SHOWN at %.1f %.1f %.1f rot %.1f %.1f %.1f\n",
                                    pr.id, static_cast<double>(pr.pos[0]),
                                    static_cast<double>(pr.pos[1]), static_cast<double>(pr.pos[2]),
                                    static_cast<double>(pr.rotDeg[0]),
                                    static_cast<double>(pr.rotDeg[1]),
                                    static_cast<double>(pr.rotDeg[2]));
                }
            }
            refreshSprites();
            const bool wantSprites = (session.scene().effects().count() || !ctlSprites.empty()) &&
                                     !spriteTex.empty();
            // Which sprite ids the resident scene can name. Computed BEFORE the
            // rebuild test and compared, because a scene that starts asking for
            // an id it was not asking for before needs a slot for it - a
            // composition counter cannot see that.
            spriteWanted.clear();
            for (const auto& e : session.scene().sfx().effects)
                spriteWanted.insert(static_cast<int>(e.sprite));
            for (const auto& pa : session.scene().effects().particles())
                spriteWanted.insert(pa.sprite);
            for (const auto& c : ctlSprites) spriteWanted.insert(c.sprite);
            if (poolBuiltFor != poolComposition || poolHasSprites != wantSprites ||
                poolHasPlayer != drawPlayer || spritePooled != spriteWanted) {
                pool = worldTex;
                for (auto& cm : charModels) {
                    cm.second.texBase = pool.size();
                    pool.insert(pool.end(), cm.second.tex.begin(), cm.second.tex.end());
                }
                // ...then each PROP model's, so a prop batch's slot is its
                // material plus its own base, the same rule every other
                // section follows.
                for (auto& pm : propModels) {
                    pm.second.texBase = pool.size();
                    pool.insert(pool.end(), pm.second.tex.begin(), pm.second.tex.end());
                }
                playerTexBase = pool.size();
                if (drawPlayer) pool.insert(pool.end(), playerTex.begin(), playerTex.end());
                spriteTexBase = pool.size();
                // THE SPRITES GO IN DENSELY, and that is the whole point.
                // `spriteTex` is indexed BY SPRITE ID, because an effect names
                // its sprite by id (`sub_4A5800`) - so 24 decoded sprites
                // spread over ids 0..137 make a 138-entry array of which 114
                // are EMPTY. Inserting it whole put the pool at 154 slots
                // against the **64** a bucket key's low six bits can address,
                // and every slot above 63 wrapped onto another texture: a
                // particle drawing at the right size, in the right blend, with
                // the wrong picture. That is the shape a reader reported as
                // "visible but does not render correctly", and only some of
                // them wrong, because which wrap depends on the id mod 64.
                //
                // So only the sprites that HAVE a texture go in, and
                // `spriteSlot` maps an id to its place. The id-keyed arrays
                // stay as they are - `particleGeometry` needs them for the
                // frame walk and the quad extent.
                //
                // ...and only the ones this SCENE can ask for. Every decoded
                // sprite used to go in - 24 of them, the global library's 20
                // plus the scene's - and with ANEKBAH's 20 set textures, a
                // second resident set, the staged characters and the player
                // ahead of them the pool ran past **64**, which is all a
                // bucket key's low six bits can address (`slot & 0x3F`). Past
                // that a sprite aliases onto another slot and draws someone
                // else's picture, which is a street light and a fire both
                // coming out as smoke: `EFFECTS2_GLOW` and `EFFECTS2_SMOKE1`
                // are different sprites sharing one atlas, so an aliased slot
                // lands on the neighbour and looks exactly like it.
                //
                // A scene asks for very few: Anekbah's twenty effects name
                // THREE sprites (49589 smoke, 49590 glow, 49591 explo). So
                // take the ids its own `.sfx` names, plus any a live particle
                // is already carrying, and pool those alone.
                spriteSlot.assign(spriteTex.size(), -1);
                if (wantSprites)
                    for (int id : spriteWanted) {
                        if (id < 0 || static_cast<std::size_t>(id) >= spriteTex.size()) continue;
                        const auto i = static_cast<std::size_t>(id);
                        if (spriteTex[i].rgb.empty()) continue;   // an id nothing decoded
                        spriteSlot[i] = static_cast<int>(pool.size() - spriteTexBase);
                        pool.push_back(spriteTex[i]);
                    }
                if (pool.size() > 64)
                    std::printf("WARNING: texture pool is %zu, past the 64 a bucket key "
                                "can address - slots will alias\n", pool.size());
                poolSize = pool.size();
                poolBuiltFor = poolComposition;
                poolHasSprites = wantSprites;
                poolHasPlayer = drawPlayer;
                spritePooled = spriteWanted;
                world.setTextures(pool);
                // The sprite section comes and goes with the effects, several
                // times a second; only a change of CAST is worth a line.
                if (poolBuiltFor != poolTold) {
                    poolTold = poolBuiltFor;
                    std::printf("frame %ld: texture pool - %zu set + %zu character (%zu "
                                "models) + %zu player + %zu sprite = %zu slots\n", n,
                                worldTex.size(), playerTexBase - worldTex.size(),
                                charModels.size(), spriteTexBase - playerTexBase,
                                pool.size() - spriteTexBase, pool.size());
                }
                if (pool.size() > 64 && !poolOverflowTold) {
                    poolOverflowTold = true;
                    std::printf("  ...which is over the 64 a bucket key's low six bits can "
                                "address (ASSETS 4b): every slot above 63 WRAPS\n");
                }
            }

            // ---- EVERY STAGED BODY, POSED BY WHATEVER DRIVES IT --------
            //
            // Three sources in the engine's own precedence - the program that
            // names HIM, then the conversation line, then the bank's idle -
            // and the whole point of issue 41 is that the first is resolved
            // PER ACTOR. The file used to take the last running actor program
            // for its one body, so while `A_2_DemonLook` ran that body wore
            // the Demon's clip whoever it was.
            const omk::SceneRunner& sc = session.scene();
            const int convSpeaker = session.dialogOpen()
                ? session.dialogue().conversation().speaker : -1;
            bool convOwned = false;
            for (const auto& up : staged) if (up->actor == convSpeaker) convOwned = true;
            // A running program whose actor is nobody on screen. AREA 118's
            // arrival is one: the shown list carries Kay'l as 310 and the
            // program names another id, so with no fallback the intro would
            // lose its pose. It drives the frame's ONLY body, and can never
            // mis-assign once there is more than one - which is the case the
            // issue is about. LABELLED: a reconstruction, not a rule read out
            // of the engine.
            // WHICH BODY A PROGRAM DRIVES. `scx.play.actor` names a
            // CHARACTERS id in its first field (`ScriptObject_StartOnActor`);
            // `scx.play.player` names none and drives the PLAYER - and the
            // Impasse's `A_1_KaylArrives` is one of those, so keying on
            // `how == "actor"` alone left Kay'l standing in his idle through
            // his own arrival.
            const int playerId = session.playerActor();
            const auto drivenBy = [&](const omk::SceneRunner::Started& t, int actorId) {
                if (t.clip < 0) return false;
                if (t.how == "actor")  return t.actor == actorId;
                if (t.how == "player") return actorId == playerId;
                return false;
            };
            int loneProg = -1, loneProgs = 0;
            for (std::size_t k = 0; k < sc.started().size(); ++k) {
                const auto& t = sc.started()[k];
                if (t.clip < 0 || (t.how != "actor" && t.how != "player")) continue;
                if (!sc.programRunning(static_cast<int>(k))) continue;
                bool owned = false;
                for (const auto& up : staged) if (drivenBy(t, up->actor)) owned = true;
                if (owned) continue;
                loneProg = static_cast<int>(k);
                ++loneProgs;
            }
            bool firstBody = true;
            for (auto& up : staged) {
                Staged& s = *up;
                s.drawn = false;
                if (!s.mo || !s.mo->ready) {
                    // A model with nothing to draw. `PA1_FN.3DO` is 1236
                    // bytes: ONE mesh, `PBassin`, 8 vertices and 12 triangles,
                    // flags 0x1 - which the engine's own drawable test
                    // (`flags & 0x800043`, ASSETS 4) rejects. The alley's
                    // three passers-by are that model, so they have no body in
                    // the shipped data either; this is not a gap in the
                    // viewer.
                    if (!s.placeTold) {
                        s.placeTold = true;
                        std::printf("frame %ld: actor %d %s has no drawable geometry "
                                    "(%zu meshes, %zu corners after the drawable mask) - "
                                    "nothing to stage\n", n, s.actor, s.model.c_str(),
                                    s.mo ? s.mo->meshes.size() : 0u,
                                    s.mo ? s.mo->rest.corners.size() : 0u);
                    }
                    continue;
                }
                // (a) THE PROGRAM THAT NAMES HIM.
                int prog = -1;
                for (std::size_t k = 0; k < sc.started().size(); ++k) {
                    const auto& t = sc.started()[k];
                    if (!drivenBy(t, s.actor)) continue;
                    if (!sc.programRunning(static_cast<int>(k))) continue;
                    prog = static_cast<int>(k);
                }
                bool byLone = false;
                if (prog < 0 && loneProgs == 1 && staged.size() == 1) {
                    prog = loneProg;
                    byLone = true;
                }
                int sceneClip = -1, scenePath = -1;
                float sceneFrame = 0.0f;
                const omk::SceneRunner::Started* stt = nullptr;
                if (prog >= 0) {
                    stt = &sc.started()[static_cast<std::size_t>(prog)];
                    sceneClip = stt->clip;
                    scenePath = stt->path;
                    // The frame of THAT clip, not of the program: a program
                    // walks its steps and each may name a different animation,
                    // so the clock the pose is sampled at counts from the step
                    // (`SceneRunner::programAnimClock`).
                    sceneFrame = sc.programAnimClock(prog);
                }
                if (sceneClip != s.sceneClipWas) {
                    s.sceneClipWas = sceneClip;
                    s.sceneTracks = omk::NodeTracks{};
                    if (sceneClip >= 0 && sc.loaded())
                        s.sceneTracks = omk::clipTracks(sc.scene().clipData(sceneClip));
                    if (scenePath >= 0 && sc.loaded() &&
                        scenePath < static_cast<int>(sc.scene().paths().size())) {
                        const auto& pa = sc.scene().paths()[static_cast<std::size_t>(scenePath)];
                        if (!pa.keys.empty()) {
                            // `Path_Sample(path, 1.0, ..., 1)` - the engine's
                            // own call, mode 1 being LINEAR: find the key span
                            // containing t and lerp. Not the first key.
                            const float t = 1.0f;
                            float p[3] = {pa.keys.front().pos[0],
                                          pa.keys.front().pos[1],
                                          pa.keys.front().pos[2]};
                            for (std::size_t i = 0; i + 1 < pa.keys.size(); ++i) {
                                const float a = static_cast<float>(pa.keys[i].frame);
                                const float b = static_cast<float>(pa.keys[i + 1].frame);
                                if (t < a || t > b) continue;
                                const float u = b > a ? (t - a) / (b - a) : 0.0f;
                                for (int k = 0; k < 3; ++k)
                                    p[k] = pa.keys[i].pos[k] +
                                           (pa.keys[i + 1].pos[k] - pa.keys[i].pos[k]) * u;
                                break;
                            }
                            // ...plus the call's own offset, params 9/10/11 in
                            // INCHES, which `placementOf` has already divided.
                            for (int k = 0; k < 3; ++k)
                                s.at[k] = p[k] + (stt ? stt->offset[k] : 0.0f);
                            s.placed = true;
                            s.pelvis = true;    // an authored path names the PELVIS
                            s.progPlaced = true; s.progPelvis = true;
                            // ...and the FACING is the call's Euler, written to the
                            // node every tick; the clip's root quaternion sits under
                            // it. Anekbah's Kiss couples say -70, the walkers 180 -
                            // without it a couple placed right still stood turned
                            // away and intersecting (a reader's frame, 2026-09-03).
                            // Pitch and roll (params 4 and 6) are rarely non-zero
                            // (three beggars carry -7 of pitch) and are not applied.
                            s.progYaw = stt ? stt->euler[1] : 0.0f;
                            s.progYawKnown = stt != nullptr;
                            for (int k = 0; k < 3; ++k) s.progBase[k] = s.at[k];
                        }
                        std::printf("  pose: actor %d %s - clip %d '%s' (%d frames) on path "
                                    "%d '%s' at %.0f %.0f %.0f%s\n", s.actor, s.model.c_str(),
                                    sceneClip, sc.scene().clipName(sceneClip).c_str(),
                                    s.sceneTracks.frames, scenePath, pa.name.c_str(),
                                    s.at[0], s.at[1], s.at[2],
                                    byLone ? "  [the program names another actor; this is "
                                             "the frame's only body - LABELLED]" : "");
                    } else if (sceneClip >= 0 && sc.loaded()) {
                        // No path: the plain `Script_SelectBodyAnimation`
                        // (0x02000004) SNAPS the node to the clip's root key 0
                        // - the authored placement, a PELVIS point whose
                        // height is kept - and `Anim_RootDelta` sums the keys
                        // after it (CLAUDE.md 6; `Session::trackPlayer` is the
                        // same rule for the player). Until 2026-09-03 the body
                        // stayed on its 20-byte placement record instead, and
                        // the Impasse's beat cameras, which frame the clip
                        // roots, framed nobody: Kay'l's arrival clip starts at
                        // 6462 -121 3215 while his record puts him at 7217.
                        float base[3] = {0, 0, 0};
                        if (omk::clipRootStart(sc.scene().clipData(sceneClip), base)) {
                            for (int k = 0; k < 3; ++k) s.at[k] = base[k];
                            s.placed = true;
                            s.pelvis = true;
                            s.progPlaced = true; s.progPelvis = true;
                            for (int k = 0; k < 3; ++k) s.progBase[k] = base[k];
                            std::printf("  pose: actor %d %s - clip %d '%s' (%d frames), no "
                                        "path: snapped to its root key 0 at %.0f %.0f %.0f%s\n",
                                        s.actor, s.model.c_str(), sceneClip,
                                        sc.scene().clipName(sceneClip).c_str(),
                                        s.sceneTracks.frames, s.at[0], s.at[1], s.at[2],
                                        byLone ? "  [the program names another actor; this "
                                                 "is the frame's only body - LABELLED]" : "");
                        }
                    } else if (sceneClip < 0) {
                        // The program ended. `Script_SelectBodyAnimation` never
                        // resets the node, so the accumulated offset STANDS
                        // (CLAUDE.md 6): he stays where the clip left him and
                        // falls back to the bank's idle there.
                        s.progPlaced = false;
                        if (s.placed)   // drawAt is last frame's; `drawn` was just cleared
                            for (int k = 0; k < 3; ++k) s.at[k] = s.drawAt[k];
                        std::printf("  pose: actor %d %s - its program ended; he stays where "
                                    "it left him (%.0f %.0f %.0f) and falls back to the "
                                    "bank's idle\n", s.actor, s.model.c_str(),
                                    s.at[0], s.at[1], s.at[2]);
                    }
                }
                // (b) THE CONVERSATION'S LINE, for its speaker only. The
                // DIALOG chunk's word 0 is the speaker's actor id; when no
                // staged body carries it the model name is the fallback,
                // which is what the file matched on before there were ids.
                const bool isSpeaker = session.dialogOpen() && speakerReady &&
                    (s.actor == convSpeaker ||
                     (!convOwned && !speakerModel.empty() && s.model == speakerModel));
                const float lineT = (isSpeaker && speakerTracks.valid())
                    ? static_cast<float>(session.dialogue().elapsed() * 30.0) : -1.0f;
                const bool useLine = lineT >= 0.0f &&
                                     lineT < static_cast<float>(speakerTracks.frames);
                if (isSpeaker) sceneFrameLast = sceneFrame;
                // (c) THE IDLE, built once: the default group's default entry's
                // clip at FRAME 0.
                if (!s.idleBuilt) {
                    s.idleBuilt = true;
                    if (s.bk && s.bk->ready) s.idle = idleTracksFor(*s.bk, s.mo->meshes);
                }
                // The program's placement wins over a `fromTable` reset every
                // frame it is driving, not only on the clip-change frame.
                if (s.progPlaced && sceneClip >= 0) {
                    for (int k = 0; k < 3; ++k) s.at[k] = s.progBase[k];
                    s.pelvis = s.progPelvis;
                }
                std::vector<omk::MeshPose> pose;
                std::vector<float> fv;
                float rootW = 0.0f;      // how much of the position the scene owns
                int rootFrame = 0;
                const char* src = "the rest pose (no bank clip)";
                if (useLine) {
                    const int frame = static_cast<int>(lineT);
                    // THE FADE at both ends of the line - `sub_42D120`'s,
                    // pose.h has the read - and the root is NOT cancelled when
                    // a scene clip stages him, because the engine's
                    // cancellation never fires (`g_MorphRootTrack` is only
                    // ever -2). See the note this replaces.
                    float w = 1.0f;
                    int idleFrame = static_cast<int>(sceneFrame);
                    if (s.sceneTracks.valid()) {
                        const float total = static_cast<float>(speakerTracks.frames);
                        const float bl = omk::morphBlendFrames(speakerTracks.frames);
                        const bool blendOut = speakerVoice.find("02E19A") == std::string::npos;
                        if (bl > 0.0f && lineT < bl) {
                            w = lineT / bl;                          // from rec[47]
                            idleFrame = static_cast<int>(lineIdleFrame);
                        } else if (bl > 0.0f && blendOut && lineT > total - bl) {
                            w = 1.0f - (lineT - (total - bl)) / bl;  // to KEY 1
                            idleFrame = 0;
                        }
                    }
                    const bool cancelLineRoot = !s.sceneTracks.valid();
                    if (w < 1.0f) {
                        const auto mixed = omk::blendTracks(s.sceneTracks, idleFrame, false,
                                                            speakerTracks, frame,
                                                            cancelLineRoot, w);
                        pose = omk::composePose(s.mo->meshes, mixed, 0, false);
                    } else {
                        pose = omk::composePose(s.mo->meshes, speakerTracks, frame,
                                                cancelLineRoot);
                    }
                    rootW = 1.0f - w;
                    rootFrame = w > 0.0f ? idleFrame : static_cast<int>(sceneFrame);
                    // The FACE has no bone track: its vertices come straight
                    // out of the line's own frame, which is what moves the lips.
                    if (!speakerMorph.empty()) fv = omk::faceFrame(speakerMorph, frame);
                    src = "the line's .3DM";
                } else if (s.sceneTracks.valid()) {
                    rootFrame = static_cast<int>(sceneFrame);
                    // A SCENE CLIP keeps its root rotation - it is the
                    // character's real orientation, lying on the floor and
                    // getting up (`Anim_ApplyNodeFrame` applies every node's
                    // quaternion, the root's included).
                    pose = omk::composePose(s.mo->meshes, s.sceneTracks, rootFrame, false);
                    rootW = 1.0f;
                    src = "a scene program's clip";
                } else if (s.idle.valid()) {
                    pose = omk::composePose(s.mo->meshes, s.idle, 0, false);
                    src = "the bank's default entry, frame 0";
                } else {
                    pose = omk::composePose(s.mo->meshes, omk::NodeTracks{}, 0, false);
                }
                // THE HEAD LOOK: an actor a script pointed at the player turns
                // his head toward him every frame (`Actors_TickAll` -> `Actor_
                // SetHeadLook`), the target being the player's head. His
                // world position is the frontend's; back into the pose's own
                // space through the placement below (facing, pelvis, at).
                static const bool lookAll = std::getenv("OMK_LOOK_ALL") != nullptr;   // a diagnostic: everyone looks
                if ((lookAll || session.looksAtPlayer(s.actor)) && s.placed && s.mo->root >= 0) {
                    const int head = omk::headMeshOf(s.mo->meshes);
                    if (head >= 0) {
                        const float* pp = (adventure && player) ? player->pos() : session.playerPos();
                        // the target's head: his feet less a standing head height
                        // (`th[11..13]` is the target's head node in the engine)
                        const float world[3] = {pp[0], pp[1] - 60.0f, pp[2]};
                        float pelvis[3] = {0, 0, 0};
                        if (static_cast<std::size_t>(s.mo->root) < pose.size())
                            for (int k = 0; k < 3; ++k) pelvis[k] = pose[static_cast<std::size_t>(s.mo->root)].pos[k];
                        const float rel[3] = {world[0] - s.at[0], world[1] - s.at[1], world[2] - s.at[2]};
                        float local[3];
                        omk::rotateYaw(-s.facing, rel, local);
                        const float target[3] = {local[0] + pelvis[0], local[1] + pelvis[1], local[2] + pelvis[2]};
                        omk::aimHead(pose, s.mo->meshes, head, target, s.look,
                                     static_cast<float>(frameSec * 30.0), s.lookSnap);
                        s.lookSnap = false;
                    }
                } else {
                    s.lookSnap = true;
                }
                if (!s.placed) {
                    if (!s.placeTold) {
                        s.placeTold = true;
                        std::printf("frame %ld: actor %d %s is shown but nothing says WHERE - "
                                    "no placement record, no program path, no camera solve; "
                                    "not drawn\n", n, s.actor, s.model.c_str());
                    }
                    continue;
                }
                if (s.src != src) {
                    s.src = src;
                    std::printf("frame %ld: actor %d %s - pose source: %s\n",
                                n, s.actor, s.model.c_str(), src);
                }
                // A crowd model (the PSH/FSH family the city extras wear) is
                // four LOD skeletons in one file; posing one left the other
                // three at rest - a T-pose inside every couple and beggar.
                // The rest geometry is cut to the skeleton the tracks name.
                const omk::NodeTracks& posingTracks = useLine ? speakerTracks
                                                     : s.sceneTracks.valid() ? s.sceneTracks : s.idle;
                const int skel = skeletonRootOf(*s.mo, posingTracks);
                const omk::Geometry& restUsed = skel == s.mo->root && s.mo->root >= 0 &&
                                                 !hasSeveralSkeletons(*s.mo)
                                                 ? s.mo->rest : lodRestFor(s.model, *s.mo, skel);
                omk::applyPose(s.posed, restUsed, s.mo->meshes, pose, &s.mo->face, &fv);
                // THE ROOT MOTION: `Anim_RootDelta`'s running sum, weighted by
                // how much of the pose is the scene's, so a line stands where
                // it was staged and a fade lerps the position too.
                float rootMove[3] = {0, 0, 0};
                if (rootW > 0.0f && !s.sceneTracks.trans.empty()) {
                    int fi = rootFrame;
                    if (fi < 0) fi = 0;
                    if (fi >= static_cast<int>(s.sceneTracks.trans.size()))
                        fi = static_cast<int>(s.sceneTracks.trans.size()) - 1;
                    for (int k = 0; k < 3; ++k)
                        rootMove[k] = rootW *
                            s.sceneTracks.trans[static_cast<std::size_t>(fi)]
                                               [static_cast<std::size_t>(k)];
                }
                // THE ANCHOR. An authored PATH names the pelvis - the
                // hierarchy root, whose height is authored - so the model
                // goes there as it is. A placement record and a camera solve
                // name a spot on the GROUND, so the FEET go there, and the
                // game's Y points DOWN so the feet are the largest y.
                float feet = -1e9f;
                for (const auto& c : s.posed.corners) if (c.y > feet) feet = c.y;
                if (!s.seatKnown || s.seatClip != s.sceneClipWas || s.seatSrc != src) {
                    s.seatKnown = true;
                    s.seatClip = s.sceneClipWas;
                    s.seatSrc = src;
                    s.seatFeet = feet;
                }
                feet = s.seatFeet;
                float ground = s.at[1];
                if (!s.pelvis && !playerSoup.empty()) {
                    // `Walk_ProbeGround`'s own direction: down from just above
                    // the authored point (walk_set.cpp's `seatOnFloor`).
                    if (const auto g = omk::floorUnder(playerSoup, s.at[0],
                                                       s.at[1] - 1.0, s.at[2])) {
                        const float drop = static_cast<float>(*g) - s.at[1];
                        // A body height. Further than that and the authored
                        // point is not standing on this floor at all - a
                        // balcony or a window the walk mesh does not carry -
                        // so the authored y is kept. LABELLED: the cut-off is
                        // this file's, not the engine's.
                        if (drop < 100.0f) ground = static_cast<float>(*g);
                        else if (!s.groundTold) {
                            s.groundTold = true;
                            std::printf("frame %ld: actor %d %s stands at y %.0f with the "
                                        "walkable floor %.0f BELOW him - kept where he is "
                                        "authored (a ledge the walk mesh does not carry)\n",
                                        n, s.actor, s.model.c_str(), s.at[1], drop);
                        }
                    }
                }
                // ...and the PELVIS is what the placement names, so the
                // model's authored root offset comes out first. The height
                // still follows the anchor: an authored path names the pelvis
                // and its height is kept, a placement record and a camera
                // solve name the ground and the FEET go there.
                float pelvis[3] = {0, 0, 0};
                if (s.mo->root >= 0 && static_cast<std::size_t>(s.mo->root) < pose.size())
                    for (int k = 0; k < 3; ++k)
                        pelvis[k] = pose[static_cast<std::size_t>(s.mo->root)].pos[k];
                const float off[3] = {s.at[0] - pelvis[0] + rootMove[0],
                                      (s.pelvis ? s.at[1] - pelvis[1] : ground - feet)
                                          + rootMove[1],
                                      s.at[2] - pelvis[2] + rootMove[2]};
                // THE FACING, and only for a body no clip is turning:
                // `Actor_SetEuler` is what a placement authors, while a scene
                // clip carries its own root orientation and a line's root is
                // relative to the actor's own frame.
                const bool spin = !s.sceneTracks.valid() && !useLine &&
                                  std::fabs(s.facing) > 0.01f;
                // a program's body turns by the call's Euler y about its pelvis
                const bool progSpin = s.sceneTracks.valid() && !useLine && s.progYawKnown &&
                                      std::fabs(s.progYaw) > 0.01f;
                for (auto& c : s.posed.corners) {
                    if (spin) {
                        const float in[3] = {c.x, c.y, c.z};
                        float r[3];
                        omk::rotateYaw(s.facing, in, r);
                        c.x = r[0]; c.y = r[1]; c.z = r[2];
                    } else if (progSpin) {
                        const float in[3] = {c.x - pelvis[0], c.y - pelvis[1], c.z - pelvis[2]};
                        float r[3];
                        omk::rotateYaw(s.progYaw, in, r);
                        c.x = r[0] + pelvis[0]; c.y = r[1] + pelvis[1]; c.z = r[2] + pelvis[2];
                    }
                    c.x += off[0]; c.y += off[1]; c.z += off[2];
                }
                // Where he ENDED UP, which is the placement plus the clip's
                // root motion - not the offset, which carries the model's own
                // authoring origin.
                s.drawAt[0] = s.at[0] + rootMove[0];
                s.drawAt[1] = (s.pelvis ? s.at[1] : ground) + rootMove[1];
                s.drawAt[2] = s.at[2] + rootMove[2];
                s.posed.revision = ++worldGeoRev;
                s.drawn = true;
                if (stagedProbe && (n % 100) == 0) {
                    float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
                    for (const auto& c : s.posed.corners) {
                        const float p[3] = {c.x, c.y, c.z};
                        for (int k = 0; k < 3; ++k) {
                            if (p[k] < lo[k]) lo[k] = p[k];
                            if (p[k] > hi[k]) hi[k] = p[k];
                        }
                    }
                    std::printf("    probe %ld actor %d %s box %.0f %.0f %.0f .. %.0f %.0f "
                                "%.0f (%zu corners, %zu batches) cam eye %.0f %.0f %.0f at "
                                "%.0f %.0f %.0f fov %.0f\n", n, s.actor, s.model.c_str(),
                                lo[0], lo[1], lo[2], hi[0], hi[1], hi[2],
                                s.posed.corners.size(), s.posed.batches.size(),
                                view.cam.eye[0], view.cam.eye[1], view.cam.eye[2],
                                view.cam.at[0], view.cam.at[1], view.cam.at[2],
                                view.cam.hfovDeg);
                }
                if (firstBody) {
                    firstBody = false;
                    for (int k = 0; k < 3; ++k) actorAt[k] = off[k];
                    actorKnown = true;
                }
            }
            // ---- THE PEDESTRIANS ---------------------------------------
            pedDrawn = pedLive = pedInAction = pedIdle = 0;
            vehDrawn = vehLive = vehStopped = 0;
            {
                const auto& pd = session.sliders();
                const auto& rs = session.residentSlot(session.activeSlot());
                if (pd.loaded() && !rs.ani.empty() && rs.ani != pedAniName) {
                    pedAniName = rs.ani;
                    pedAni = fs.read("ANIMS/" + rs.ani + ".ANI");
                    pedTracks.clear();
                }
                const auto& ws = pd.movers();
                if (pedStaged.size() != ws.size()) {
                    pedStaged.clear();
                    for (std::size_t i = 0; i < ws.size(); ++i) pedStaged.push_back(std::make_unique<PedStaged>());
                    pedTracks.clear();
                }
                const float reach = omk::kLodDistances[3];
                for (std::size_t i = 0; i < ws.size(); ++i) {
                    const auto& w = ws[i];
                    PedStaged& p = *pedStaged[i];
                    p.drawn = false;
                    if (!w.live || !w.clip || pedAni.empty()) continue;
                    ++pedLive;
                    if (w.flags & 0x80u) ++pedInAction;
                    if (w.flags & 0x100u) ++pedIdle;
                    const float dx = w.body[0] - view.cam.eye[0], dy = w.body[1] - view.cam.eye[1],
                                dz = w.body[2] - view.cam.eye[2];
                    if (dx * dx + dy * dy + dz * dz > reach * reach) continue;
                    if (!p.mo) p.mo = charModelFor(w.model);
                    if (!p.mo || !p.mo->ready) continue;
                    if (w.clip != p.clipWas) {
                        p.clipWas = w.clip;
                        p.tracks = pedTracksFor(w.sex, *w.clip, p.mo->meshes);
                    }
                    const int lodRoot = p.tracks ? skeletonRootOf(*p.mo, *p.tracks) : p.mo->root;
                    const omk::Geometry& rest = lodRestFor(w.model, *p.mo, lodRoot);
                    int frame = static_cast<int>(std::floor(w.clock)) - 1;
                    if (frame < 0) frame = 0;
                    if (p.tracks && frame >= p.tracks->frames) frame = p.tracks->frames - 1;
                    const auto pose = p.tracks
                        ? omk::composePose(p.mo->meshes, *p.tracks, frame, false)
                        : omk::composePose(p.mo->meshes, omk::NodeTracks{}, 0, false);
                    omk::applyPose(p.posed, rest, p.mo->meshes, pose);
                    // THE HEIGHT is the engine's rule, `sub_437F80(inst, x, body.y
                    // + footY - radius, z)`: the model origin stands one root
                    // radius (41.9 for PSH_FN - the pelvis-to-feet height) above
                    // the body point and the root track's summed y moves it.
                    // Written here as the REST pose's feet on the body point plus
                    // that summed y, which is the same constant for a model
                    // whose radius is its height - and NOT the feet of the clip's
                    // first frame, which for the seated clip are the folded legs
                    // at pelvis level and sank every sitter into the street (a
                    // reader's frame, 2026-09-03). The sit's root drops 20.7 over
                    // its enter clip; that is what puts him on the ground.
                    if (!p.feetKnown) {
                        const auto restPose = omk::composePose(p.mo->meshes, omk::NodeTracks{}, 0, false);
                        omk::Geometry restPosed;
                        omk::applyPose(restPosed, rest, p.mo->meshes, restPose);
                        p.feet = -1e9f;
                        for (const auto& c : restPosed.corners) if (c.y > p.feet) p.feet = c.y;
                        p.feetKnown = true;
                    }
                    float rootXZ[2] = {0.0f, 0.0f};
                    if (p.mo->root >= 0 && static_cast<std::size_t>(p.mo->root) < p.mo->meshes.size()) {
                        rootXZ[0] = p.mo->meshes[static_cast<std::size_t>(p.mo->root)].pos[0];
                        rootXZ[1] = p.mo->meshes[static_cast<std::size_t>(p.mo->root)].pos[2];
                    }
                    for (auto& c : p.posed.corners) {
                        const float in[3] = {c.x - rootXZ[0], c.y, c.z - rootXZ[1]};
                        float r[3];
                        omk::rotateYaw(w.facing, in, r);
                        c.x = r[0] + w.body[0];
                        c.y = r[1] + w.body[1] + w.footY - p.feet;
                        c.z = r[2] + w.body[2];
                    }
                    p.posed.revision = ++worldGeoRev;
                    p.drawn = true;
                    ++pedDrawn;
                }
                if (pedLive && (pedTold < 0 || n - pedTold >= 300)) {
                    pedTold = n;
                    std::printf("frame %ld: pedestrians - %d live, %d drawn within %.0f of the eye, "
                                "%d at an action point, %d idling\n", n, pedLive, pedDrawn, reach,
                                pedInAction, pedIdle);
                }
                // ...and the ROAD TRAFFIC on the same circuit's vehicle lanes.
                const auto& vs = pd.vehicles();
                if (vehStaged.size() != vs.size()) {
                    vehStaged.clear();
                    for (std::size_t i = 0; i < vs.size(); ++i) vehStaged.push_back(std::make_unique<VehStaged>());
                }
                // `dword_4C8860`, the VEHICLE LOD distances - 20/30/40/50 m
                // where the crowd's are 10/20/30/40, so a slider is still
                // drawn a good way past the last walker.
                const float vreach = omk::kVehLodDistances[3];
                for (std::size_t i = 0; i < vs.size(); ++i) {
                    const auto& v = vs[i];
                    VehStaged& sv = *vehStaged[i];
                    sv.drawn = false;
                    if (!v.live || v.mover < 0) continue;
                    const auto& m = pd.movers()[static_cast<std::size_t>(v.mover)];
                    ++vehLive;
                    if (m.flags & 0x100u) ++vehStopped;
                    const float vx = m.body[0] - view.cam.eye[0], vy = m.body[1] - view.cam.eye[1],
                                vz = m.body[2] - view.cam.eye[2];
                    if (vx * vx + vy * vy + vz * vz > vreach * vreach) continue;
                    if (!sv.mo) sv.mo = charModelFor(v.model);
                    if (!sv.mo || !sv.mo->ready) continue;
                    if (!sv.built) {
                        sv.lodRoot = v.lodBase == 0 ? heaviestRootOf(*sv.mo) : sv.mo->root;
                        const omk::Geometry& rest = lodRestFor(v.model, *sv.mo, sv.lodRoot);
                        const auto pose = omk::composePose(sv.mo->meshes, omk::NodeTracks{}, 0, false);
                        omk::applyPose(sv.atRest, rest, sv.mo->meshes, pose);
                        // The sub-objects of one model are laid out APART in
                        // model space - SLI_FN's four roots sit at x 351..550 -
                        // so each is re-centred on its own root, which is what
                        // makes the four LOD variants land in one place. The
                        // walkers do the same for x and z; a vehicle takes y
                        // too, because it has no feet rule to stand on.
                        if (sv.lodRoot >= 0 && static_cast<std::size_t>(sv.lodRoot) < sv.mo->meshes.size())
                            for (int k = 0; k < 3; ++k)
                                sv.origin[k] = sv.mo->meshes[static_cast<std::size_t>(sv.lodRoot)].pos[k];
                        sv.built = true;
                    }
                    sv.posed = sv.atRest;
                    // `sub_437F80(inst, x, y - 30.75, z)`: the instance sits
                    // 30.75 units ABOVE the body point (y is down), turned to
                    // the heading `sub_453330` built from the direction to its
                    // mover - which for a vehicle is where it is going.
                    for (auto& c : sv.posed.corners) {
                        const float in[3] = {c.x - sv.origin[0], c.y - sv.origin[1], c.z - sv.origin[2]};
                        float r[3];
                        omk::rotateYaw(m.facing, in, r);
                        c.x = r[0] + m.body[0];
                        c.y = r[1] + m.body[1] - omk::kVehNodeLift;
                        c.z = r[2] + m.body[2];
                    }
                    sv.posed.revision = ++worldGeoRev;
                    sv.drawn = true;
                    ++vehDrawn;
                }
                if (vehLive && (vehTold < 0 || n - vehTold >= 300)) {
                    vehTold = n;
                    std::printf("frame %ld: traffic - %d live, %d drawn within %.0f of the eye, "
                                "%d stopped\n", n, vehLive, vehDrawn, vreach, vehStopped);
                }
            }
            if (drawPlayer) {
                // THE PLAYER, posed by his channel's clip - the quaternions
                // alone, since the root motion is the position the walker
                // integrated - turned by his facing (the row-vector rotation
                // `Matrix3x3_FromEulerAngles(0, yaw, 0)` gives, the same one
                // his root delta and the camera use) and stood with his FEET
                // on `pos()`, which the walker keeps on the floor. The feet
                // offset is the rest pose's, taken once, so a clip's bob does
                // not move the ground under him.
                // `player.anim.hold` neither resets to rest nor freezes: the
                // channel keeps ticking with no input (see the tick above), so
                // the pose comes from it exactly as it does unheld. This code
                // has now been wrong twice in the other two directions - first
                // `composePose(meshes, {}, 0)`, the REST SENTINEL, which drew a
                // T-pose; then a latched pose, which froze him mid-stride.
                const omk::NodeTracks* pt = player->poseTracks();
                std::vector<omk::MeshPose> pose = pt
                    ? omk::composePose(playerMeshes, *pt, player->poseFrame(), false)
                    : omk::composePose(playerMeshes, omk::NodeTracks{}, 0, false);
                omk::applyPose(playerPosed, playerRest, playerMeshes, pose);
                // THE ANCHOR IS THE FLOOR, NOT THE HIPS (omk-play 69).
                //
                // `playerFeet` used to be latched from the FIRST pose - the
                // standing one - and subtracted for ever after. Every later
                // pose was then placed by where the STANDING feet were, so a
                // pose that lowers the body relative to its feet is drawn with
                // the hips pinned and the legs coming up instead: a reader,
                // watching a take, "the character anchor is their hips and not
                // the floor... their legs go up, like the character is
                // floating".
                //
                // The crouch itself is a root TRANSLATION, and this port has no
                // path for one: `poseTracks` assigns `t.trans` all zeroes and
                // `composePose` never reads it. Re-measuring the lowest corner
                // each frame supplies the same result from the other side - the
                // body's lowest point sits on the walker's floor point, so the
                // planted foot stays down and the pelvis drops.
                //
                // **LABELLED, a port decision rather than a transcription**:
                // the engine seats the actor by his own origin and lets root
                // motion move him, which is a different mechanism. This is
                // equivalent while some part of him is on the ground and would
                // be WRONG for a pose where both feet leave it - a jump. The
                // port has no jump or fall state yet (omk-play 68), so nothing
                // today can tell them apart; when one arrives, this is the
                // line that has to become the real root-motion path.
                if (!playerFeetKnown) {
                    playerFeet = -1e9f;
                    for (const auto& c : playerPosed.corners)
                        if (c.y > playerFeet) playerFeet = c.y;
                    // the hierarchy root: the one mesh with no parent. A model
                    // constant, so this half stays latched.
                    playerRootXZ[0] = playerRootXZ[1] = 0.0f;
                    for (const auto& m : playerMeshes)
                        if (m.parent < 0) {
                            playerRootXZ[0] = m.pos[0];
                            playerRootXZ[1] = m.pos[2];
                            break;
                        }
                    playerFeetKnown = true;
                }
                // THE ROOT TRANSLATION'S VERTICAL, which is the crouch.
                //
                // `Walk_ProbeGround` (0x00467030) anchors the actor by a
                // CONSTANT: `actor[264] = actor[236] - actor[248] + groundY +
                // sphere[12]`, where `sphere[12]` comes from
                // `Collision_BodySphere` and does not change with the pose. So
                // the engine's anchor never moves, and the crouch is carried
                // entirely by the animation's ROOT MOTION - the pelvis track's
                // summed position keys, 24.3 units (62 cm) inside one cell of
                // H_TAKL12.
                //
                // Re-measuring the lowest corner each frame, which is what
                // this did for one build, gets a similar picture and STUTTERS,
                // because it re-derives the anchor from a pose that changes
                // every frame. The anchor is constant again and the drop comes
                // from `trans` instead.
                //
                // Only the VERTICAL is taken: X and Z would double-count
                // against the walker, which already moves him.
                // AND IT ACCUMULATES ACROSS STATES. `Anim_RootDelta(prev,
                // cur)` adds the movement between the previous frame and the
                // current one every tick and "the accumulated offset stands"
                // (CLAUDE.md 6) - there is no reset per clip. Each clip's own
                // sum starts at zero, so taking it as an absolute made
                // H_TAKL22 run 0 -> -24.6: from STANDING to 62 cm above it,
                // instead of from crouched back down to standing. A reader:
                // "animation from up to down => ok / animation from down to up
                // => character suddenly way higher than they should be."
                //
                // Carried, the pair nets out: +24.3 then -24.6 is -0.3.
                static float rootAccum = 0.0f, rootLast = 0.0f;
                static int   rootState = -12345;
                float rootDrop = 0.0f;
                if (pt && !pt->trans.empty()) {
                    const int rf = player->poseFrame();
                    const std::size_t ri = static_cast<std::size_t>(
                        rf < 0 ? 0 : (rf < static_cast<int>(pt->trans.size())
                                          ? rf : static_cast<int>(pt->trans.size()) - 1));
                    const float cur = pt->trans[ri][1];
                    // a new state re-bases the delta, it does not reset the sum
                    if (player->ctlState() != rootState) {
                        rootState = player->ctlState();
                        rootLast  = cur;
                    }
                    // A WINDOW'S LAST FRAME IS A DISCONTINUITY, NOT MOTION.
                    // Measured across a take: the blend runs 0 -> +21.90 over
                    // H_TAKL12's 21 frames (the crouch, against 19.08 of feet
                    // lifted by the rotations - they very nearly cancel) and
                    // then snaps to -0.22 at f 20. That one bogus delta
                    // poisons the sum, so H_TAKL22 starts from a corrupted
                    // base and ends 22 units up - "it stays higher until I
                    // release the object".
                    //
                    // **A PORT GUARD, labelled**: real root motion is a few
                    // units a frame at most (H_WALK's whole cycle spans 2).
                    // Anything larger is a seam between variants, not the
                    // pelvis moving, so it is not accumulated. The engine has
                    // no such guard because `Anim_RootDelta` never crosses a
                    // seam: it indexes the position keys by the RAW frame and
                    // so reads cell 0 only. Reading the blended cells is this
                    // port's choice, and this is its cost.
                    rootAccum += cur - rootLast;
                    rootLast   = cur;
                    // **A PORT GUARD, labelled**: the engine relies on the
                    // authored motion netting out and has a ground probe under
                    // it every frame; this has neither, so any residue would
                    // live for ever. Back in the idle, the body is standing by
                    // definition, so the sum is released there.
                    if (player->clipName() == "H_STAND") rootAccum = 0.0f;
                    rootDrop = rootAccum;
                    // MEASURING, not fixing: how far does the model's own
                    // lowest point travel across a take? If the rotations
                    // lower the body, a CONSTANT anchor is right and the
                    // float came from somewhere else; if it barely moves
                    // while the legs bend, the body never crouches at all.
                    if (player->variantCount() > 1) {
                        float lo = -1e9f;
                        for (const auto& c : playerPosed.corners)
                            if (c.y > lo) lo = c.y;
                        const auto& lf = player->last();
                        std::printf("  crouch: %-9s f %2d  lowest %+8.2f  "
                                    "(latched %+8.2f, delta %+7.2f)  root %+6.2f"
                                    "  at %.1f %.1f  dxz %+.2f %+.2f  step %d%s\n",
                                    player->clipName().c_str(), player->poseFrame(),
                                    lo, playerFeet, lo - playerFeet, rootAccum,
                                    player->pos()[0], player->pos()[2],
                                    lf.rootDelta[0], lf.rootDelta[2],
                                    static_cast<int>(lf.step),
                                    lf.stepped ? "" : " (no step asked)");
                    }
                }
                const float* pp = player->pos();
                const float yaw = player->facing();
                // ROTATE ABOUT THE PELVIS, not the model's origin. A `.3DO`'s
                // meshes carry ABSOLUTE positions and the body is not built
                // around (0,0,0): `HO1_FN`'s root `UBassin` sits at
                // x 2.87, z **17.94**, and its whole bounding box spans
                // z 10.5..21.3. Spinning the raw corners about the origin
                // therefore swings the character around a point about 18
                // inches - half a metre - away from himself, which is what a
                // reader described as "the pivot is placed about a metre
                // ahead of the character". The actor's own origin is the
                // pelvis (`player.h`, settled with the camera lift), so that
                // is what must stay put.
                for (auto& c : playerPosed.corners) {
                    const float in[3] = {c.x - playerRootXZ[0], c.y,
                                         c.z - playerRootXZ[1]};
                    float r[3];
                    omk::rotateYaw(yaw, in, r);
                    c.x = r[0] + pp[0];
                    c.y = r[1] + pp[1] - playerFeet + rootDrop;
                    c.z = r[2] + pp[2];
                }
                lastRootDrop = rootDrop;
                playerPosed.revision = ++worldGeoRev;
                for (int k = 0; k < 3; ++k) actorAt[k] = pp[k];
                actorAt[1] -= playerFeet;
                actorKnown = true;
            }

            // THE PARTICLES. A section C effect names its sprite by an
            // index into the GLOBAL library `aventure.scx` registers, and its
            // texture goes into the pool after the set's and the speaker's -
            // the batch then carries that slot in the bucket key's low six
            // bits, which is the engine's own indexing and not a second
            // mechanism.
            // Each batch carries the SPRITE index as its material; the pool
            // holds every sprite after the set's and the speaker's textures,
            // so a batch's slot is `spriteBase + sprite`.
            int spriteBase = -1;
            if ((session.scene().effects().count() || !ctlSprites.empty()) && !spriteTex.empty()) {
                omk::particleGeometry(fxGeo, session.scene().effects(),
                                      view.cam.eye, view.cam.at, spriteFr);
                // The `.CTL` sprites, each on its bone THIS frame (flag 1,
                // "follow the bone every frame" - all shipped records here
                // carry it; the others are placed once and this moves them
                // too, labelled). The frame is `(clock - from) / duration`,
                // as Cef_TickEffects writes it; flag 8's doubled sprite
                // clock is not modelled.
                if (!ctlSprites.empty() && player && drawPlayer) {
                    const omk::NodeTracks* pt = player->poseTracks();
                    const std::vector<omk::MeshPose> pose = pt
                        ? omk::composePose(playerMeshes, *pt, player->poseFrame(), false)
                        : omk::composePose(playerMeshes, omk::NodeTracks{}, 0, false);
                    const float* pp = player->pos();
                    const float yaw = player->facing();
                    const float fr = player->channelFrame();
                    ctlField.clear();
                    for (const auto& c : ctlSprites) {
                        const char* want = c.attach < 18 ? kAttachName[c.attach] : "Buste";
                        int node = -1;
                        for (std::size_t i = 0; i < playerMeshes.size(); ++i)
                            if (std::strstr(playerMeshes[i].name, want)) node = static_cast<int>(i);
                        if (node < 0 || static_cast<std::size_t>(node) >= pose.size()) continue;
                        const omk::MeshPose& hp = pose[static_cast<std::size_t>(node)];
                        const float in[3] = {hp.pos[0] - playerRootXZ[0], hp.pos[1],
                                             hp.pos[2] - playerRootXZ[1]};
                        float o[3];
                        omk::rotateYaw(yaw, in, o);
                        omk::Particle p;
                        p.pos[0] = o[0] + pp[0];
                        p.pos[1] = o[1] + pp[1] - playerFeet + lastRootDrop;
                        p.pos[2] = o[2] + pp[2];
                        p.life = c.duration > 0.0f ? c.duration : 1.0f;
                        p.frameAge = fr - c.from;
                        if (p.frameAge < 0.0f) p.frameAge = 0.0f;
                        if (p.frameAge > p.life) p.frameAge = p.life;
                        p.age = p.frameAge;
                        p.scale = c.scale > 0.0f ? c.scale : 1.0f;
                        p.sprite = c.sprite;
                        p.mode = 4;                        // Cef_SpawnEffect: +10 = 4, additive
                        if (fr < c.from) continue;         // not in its window yet
                        ctlField.addParticle(p);
                    }
                    omk::particleGeometry(ctlGeo, ctlField, view.cam.eye, view.cam.at, spriteFr);
                    const std::size_t base = fxGeo.corners.size();
                    for (omk::Batch b : ctlGeo.batches) { b.start += base; fxGeo.batches.push_back(b); }
                    fxGeo.corners.insert(fxGeo.corners.end(), ctlGeo.corners.begin(), ctlGeo.corners.end());
                    fxGeo.cornerMirror.insert(fxGeo.cornerMirror.end(), ctlGeo.cornerMirror.begin(), ctlGeo.cornerMirror.end());
                    fxGeo.cornerMesh.insert(fxGeo.cornerMesh.end(), ctlGeo.cornerMesh.begin(), ctlGeo.cornerMesh.end());
                    fxGeo.cornerVertex.insert(fxGeo.cornerVertex.end(), ctlGeo.cornerVertex.begin(), ctlGeo.cornerVertex.end());
                    fxGeo.cornerDeclared.insert(fxGeo.cornerDeclared.end(), ctlGeo.cornerDeclared.begin(), ctlGeo.cornerDeclared.end());
                    ++fxGeo.revision;
                }
                // The pool already carries them: it is built above, in one
                // place, with a section per model.
                spriteBase = static_cast<int>(spriteTexBase);
            }
            // ONE bucket order for the set, the speaker and the particles.
            // `Render_FlushBuckets` walks a single 14-bit key ascending and
            // meshes and sprites share it (`Render_SubmitSprites` ORs its
            // mode bits into the same array), so an additive particle draws
            // after every opaque mesh and before every multiply one, whatever
            // was submitted first. Submitting the three geometries in turn
            // put `ttt`'s multiply starburst before `burn`'s additive puffs
            // and the intro's dark ring darkened only black. The state bits
            // are the mesh path's own (render.h: 0x2100 additive, 0x2200
            // multiply, 0x400 cutout); the per-face depth bits 0x80/0x1000
            // are not modelled here, as they are not for the set.
            std::vector<omk::Draw> draws;
            const auto keyOf = [](omk::Blend bl, bool cutout, std::uint32_t slot) {
                std::uint32_t state = 0;
                if (bl == omk::Blend::Add)      state = 0x2100;
                else if (bl == omk::Blend::Mul) state = 0x2200;
                else if (cutout)                state = 0x400;
                return state | (slot & 0x3Fu);
            };
            for (int slot = 0; slot < 2; ++slot) {
                const WorldSlot& w = worldSlots[static_cast<std::size_t>(slot)];
                for (const auto& b : w.geo.batches)
                    draws.push_back({keyOf(b.blend, b.cutout, static_cast<std::uint32_t>(
                                               b.material + static_cast<int>(worldTexBase[slot]))),
                                     &w.geo, b.start, b.count, b.blend, b.cutout});
            }
            // EVERY staged body, each with its own model's base - the change
            // issue 41 asks for. One geometry per actor, so two bodies wearing
            // the same model still draw at their own two places.
            for (const auto& up : staged) {
                if (!up->drawn || !up->mo) continue;
                const int base = static_cast<int>(up->mo->texBase);
                for (const auto& b : up->posed.batches)
                    draws.push_back({keyOf(b.blend, b.cutout,
                                           static_cast<std::uint32_t>(b.material + base)),
                                     &up->posed, b.start, b.count, b.blend, b.cutout});
            }
            for (const auto& up : pedStaged) {
                if (!up->drawn || !up->mo) continue;
                const int base = static_cast<int>(up->mo->texBase);
                for (const auto& b : up->posed.batches)
                    draws.push_back({keyOf(b.blend, b.cutout,
                                           static_cast<std::uint32_t>(b.material + base)),
                                     &up->posed, b.start, b.count, b.blend, b.cutout});
            }
            for (const auto& up : vehStaged) {
                if (!up->drawn || !up->mo) continue;
                const int base = static_cast<int>(up->mo->texBase);
                for (const auto& b : up->posed.batches)
                    draws.push_back({keyOf(b.blend, b.cutout,
                                           static_cast<std::uint32_t>(b.material + base)),
                                     &up->posed, b.start, b.count, b.blend, b.cutout});
            }
            if (drawPlayer)
                for (const auto& b : playerPosed.batches)
                    draws.push_back({keyOf(b.blend, b.cutout, static_cast<std::uint32_t>(
                                               b.material + static_cast<int>(playerTexBase))),
                                     &playerPosed, b.start, b.count, b.blend, b.cutout});
            // The props, each batch through its own model's pool section.
            for (std::size_t bi = 0; bi < propGeo.batches.size(); ++bi) {
                const auto& b = propGeo.batches[bi];
                const PropModel* owner = bi < propBatchOwner.size() ? propBatchOwner[bi] : nullptr;
                if (!owner) continue;
                draws.push_back({keyOf(b.blend, b.cutout,
                                       static_cast<std::uint32_t>(b.material +
                                           static_cast<int>(owner->texBase))),
                                 &propGeo, b.start, b.count, b.blend, b.cutout});
            }
            if (spriteBase >= 0)
                for (const auto& b : fxGeo.batches) {
                    // `b.material` is the sprite's ID; the pool is packed, so
                    // it has to be looked up rather than added to the base
                    const int sl = (b.material >= 0 &&
                                    b.material < static_cast<int>(spriteSlot.size()))
                                   ? spriteSlot[static_cast<std::size_t>(b.material)] : -1;
                    if (sl < 0) continue;      // a sprite with no texture: not drawn
                    draws.push_back({keyOf(b.blend, b.cutout,
                                           static_cast<std::uint32_t>(spriteBase + sl)),
                                     &fxGeo, b.start, b.count, b.blend, b.cutout});
                }
            std::stable_sort(draws.begin(), draws.end(),
                             [](const omk::Draw& a, const omk::Draw& b) {
                                 return (a.bucketKey & 0x3FFFu) < (b.bucketKey & 0x3FFFu);
                             });
            world.begin(view);
            for (const auto& d : draws) world.submit(d);
            world.end();
            // The backend drew the picture into the top-left `vw x vh`; place
            // it, leaving the bands as the black `fb` was cleared to.
            const omk::Surface& pic = world.readback();
            if (pic.w == fb.w) {
                for (int y = 0; y < view.vh && y < pic.h; ++y) {
                    const int dy = view.vy + y;
                    if (dy < 0 || dy >= fb.h) continue;
                    std::copy(pic.px.begin() + static_cast<long>(y) * pic.w,
                              pic.px.begin() + static_cast<long>(y + 1) * pic.w,
                              fb.px.begin() + static_cast<long>(dy) * fb.w);
                }
            }
            ++worldFrames;
        }
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
                for (const auto& d : destinations)
                    if (state.bit(omk::StateArray::AddressEnabled, d.bit))
                        known.push_back(d.name);
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
                    blip(sndConfirm);                // interface sound 12
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
            }
            if (const int verb = walk->takeVerb(); verb >= 0) {
                const auto carried =
                    omk::objectList(state, omk::ObjectList::Carried);
                const int row = walk->selectionOf(omk::kListSneakRows);
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
                    walk->beginCombine(objIdx, isSpell);
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
                  if (!rec->usable()) {
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
                    // The consumable arm: `Object_ApplyEffect(rec, the
                    // player)` runs inside case 35 itself, the result is
                    // **2**, and `sub_42B470` plays interface sound 13.
                    blip(sndBack);
                    std::printf("sneak: '%s' is a CONSUMABLE (record +4 bit 0 "
                                "set), effect %d -> actor property %d; case 35 "
                                "applies it and returns 2, so sound 13. "
                                "Object_ApplyEffect is named and not read, so "
                                "the apply is announced and not run\n",
                                rec->name.c_str(), rec->effect,
                                omk::effectProperty(rec->effect));
                    // `sub_42B470` returned 0, so `sub_49BEA0` takes
                    // `loc_49BEF8`: reset the row list and
                    // `sub_42A370(screen, unk_4DEE50)` - back to the
                    // INVENTORY page, screen still open.
                    walk->installPanel(omk::kPanelSneakInventory);
                  }
                }
            }
            comp.setExamineText(nullptr);
            if (pn && pn->addr == omk::kPanelSneakExamine) {
                const auto carried =
                    omk::objectList(state, omk::ObjectList::Carried);
                // BY ADDRESS: the examine page carries no row list of its
                // own, and the selections are a static record keyed by list,
                // so the row chosen two panels ago is still there.
                const int row = walk->selectionOf(omk::kListSneakRows);
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
            if (rowKind == 0 && inv.openedList() >= 0) {
                const auto carried = omk::objectList(state,
                                                     omk::ObjectList::Carried);
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
                // ---- THE ECHO BAR and THE CLOCK ---------------------
                //
                // Two of the device's rows are filled by callbacks of its
                // own, and both are readable - `sub_0049DC20` and
                // `sub_0049E090` carry no `proc` label (nothing calls them;
                // they are dwords in the widget table, CLAUDE.md 1's trap),
                // so `asmfn.py` returns a neighbour and the range has to be
                // dumped by hand.
                //
                // **The echo bar shows whatever is SELECTED**, not the
                // hovered verb as the picture suggested. `sub_0049DC20`
                // takes the panel's current item and dispatches on its
                // ADDRESS:
                //
                //     0x004DE338  "%s %d" of its string and `sub_42B1C0(4)`
                //     0x004DE380  "%s %d" of its string and `sub_42B1C0(5)`
                //     0x004DE3C8  its string alone
                //     0x004DE230  its string, with `+30` forced to 1
                //     ...
                //
                // which SETTLES what list 1 is: the three 50x50 icons are
                // the setek and anneau COUNTERS and the map reader, and
                // their strings - 8, 9 and 41, the ones a `+28`-keyed drawer
                // printed across the page - belong to them and are rendered
                // HERE. "Seteks en votre possession :" is echo-bar text for
                // the setek icon, never a caption beside it.
                //
                // It also answers what `imager` counts: NOTHING. Its arm has
                // no `sub_42B1C0` and no format - just the bare string "Lire
                // plan". It is a map reader, not ammunition.
                //
                // The two counts come from `Game_RaiseEvent(44, {4|5})`,
                // which is not modelled, so those two rows show their label
                // without its number and say so rather than inventing one.
                {
                    const auto sneakText = omk::iamStrings(fs, "IAM/Sneak");
                    const omk::UiItem* selItem = walk->selected();
                    for (const auto& l : pn->lists) {
                        for (const auto& e : l.items) {
                            if (e.textFn == 0x0049DC20u && selItem) {
                                const int id = selItem->label();
                                if (id >= 0 &&
                                    id < static_cast<int>(sneakText.size())) {
                                    std::string t = sneakText[
                                        static_cast<std::size_t>(id)];
                                    // The two COUNTER arms format "%s %d",
                                    // and the number is `Game_RaiseEvent(44,
                                    // {4|5})` -> `sub_40B360` cases 4 and 5,
                                    // which read the player record's +172 and
                                    // +174. The third model, `imager`, has no
                                    // count at all - its arm is the bare
                                    // string - which is what settles that it
                                    // is a map reader and not ammunition.
                                    if (selItem->addr == 0x004DE338u)
                                        t += " " + std::to_string(state.money());
                                    else if (selItem->addr == 0x004DE380u)
                                        t += " " + std::to_string(state.rings());
                                    sneakRows[e.addr] = t;
                                }
                            } else if (e.textFn == 0x0049E090u) {
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
        if (walk) {
            comp.setFrame(n);
            // The oscillators run on a MILLISECOND clock, not on the frame
            // index - their periods are 500, 1000 and 5000 and
            // `Ui_TickScreens` advances them by the frame delta.
            comp.setClockMs(static_cast<long>(SDL_GetTicks()));
            // THE HIGHLIGHT. `Ui_DrawItemCursor` eases sixteen elements
            // between frames, so it needs a delta and somewhere to live; it
            // is attached rather than owned by the composer so that
            // `run_screen`'s hashes stay a pure function of the screen.
            {
                static long uiLastMs = 0;
                const long nowMs = static_cast<long>(SDL_GetTicks());
                comp.setDeltaMs(uiLastMs ? nowMs - uiLastMs : 33);
                uiLastMs = nowMs;
            }
            comp.attachCursor(&uiCursor);
            comp.attachModels(&uiModels);
            comp.setRowText(sneakRows.empty() ? nullptr : &sneakRows);
            comp.setHidden(sneakHidden.empty() ? nullptr : &sneakHidden);
            comp.draw(fb, openScreen, *walk);
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
            for (int y = 0; y < fb.h; ++y) {
                const int sy = y * mediaBmp.h / fb.h;
                if (sy < 0 || sy >= mediaBmp.h) continue;
                for (int x = 0; x < fb.w; ++x) {
                    const int sx = x * mediaBmp.w / fb.w;
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
        if (!session.dialogOpen() && !walk && mediaTextFrames > 0) {
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
                             SubBox::None, 'V');
            --mediaTextFrames;
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
            if (!inMenu && lineOverflow > 0) {
                const Uint8* ks = SDL_GetKeyboardState(nullptr);
                if (ks[SDL_SCANCODE_DOWN] && lineScroll < lineOverflow) ++lineScroll;
                if (ks[SDL_SCANCODE_UP]   && lineScroll > 0)            --lineScroll;
            }
            drawSubtitle(fb, lay,
                         inMenu ? std::string() : dlg.lineText(),
                         menu, sel, dispW, dispH, 32,
                         inMenu ? SubBox::Replies : SubBox::Line, 'J',
                         lineScroll, &lineOverflow);
        }
        // ---- THE FPS COUNTER, when asked for --------------------------
        //
        // Measured over a WINDOW rather than per frame, because a per-frame
        // reciprocal is mostly noise: this loop sleeps to pace itself, so a
        // single frame's time says more about the sleep than about the work.
        // It reports the rate and the worst frame in the window, which is what
        // says whether a hitch is happening at all.
        if (showFps) {
            ++fpsFrames;
            const Uint32 nowMs = SDL_GetTicks();
            const Uint32 dtMs = nowMs - fpsLastMs;
            if (dtMs > fpsWorst) fpsWorst = dtMs;
            fpsLastMs = nowMs;
            if (nowMs - fpsSince >= 1000) {
                const double secs = (nowMs - fpsSince) / 1000.0;
                std::printf("fps %.1f  (%d frames, worst %u ms)  %s%s\n",
                            fpsFrames / secs, fpsFrames, fpsWorst,
                            vkRen ? "vulkan" : "software",
                            drawWorld ? ", 3D" : ", 2D only");
                std::fflush(stdout);
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
            if (cf.running() && k > 0.0f) {
                const int cr = static_cast<int>((cf.colour >> 16) & 0xFF);
                const int cg = static_cast<int>((cf.colour >> 8) & 0xFF);
                const int cb = static_cast<int>(cf.colour & 0xFF);
                for (auto& px : fb.px) {
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
                        for (int x = 0; x < fb.w; ++x) {
                            std::uint16_t& px = fb.px[static_cast<std::size_t>(y) *
                                                      static_cast<std::size_t>(fb.w) +
                                                      static_cast<std::size_t>(x)];
                            int r = ((px >> 11) & 31) << 3, g = ((px >> 5) & 63) << 2,
                                b = (px & 31) << 3;
                            r = r * grey / 255; g = g * grey / 255; b = b * grey / 255;
                            px = static_cast<std::uint16_t>(((r >> 3) << 11) |
                                                            ((g >> 2) << 5) | (b >> 3));
                        }
                    }
                }
            }
        }

        present(fb);
        if (!snapsDir.empty() && handoverFrame >= 0 && ((n - handoverFrame) % 30) == 0) {
            const std::string path = snapsDir + "/snap-" + std::to_string(n) + ".bin";
            if (omk::safeOutputPath(path)) {
                std::ofstream o(path, std::ios::binary);
                for (auto v : fb.px) {
                    const char b2[2] = {static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)};
                    o.write(b2, 2);
                }
            }
        }
        ++n;
        if (frames && n >= frames) break;
        // 30 Hz, PORTING A7 - and it is a CAP, not an addition. A flat
        // `SDL_Delay(33)` sleeps a whole frame budget ON TOP of however long
        // the frame took, so a 30 ms software frame ran the loop at ~16 fps
        // and halved the apparent speed of everything. Sleep only what is left
        // of the budget, and nothing at all when the frame overran it.
        if (!frames) {
            const Uint32 spent = SDL_GetTicks() - frameStartMs;
            if (spent < 33) SDL_Delay(33 - spent);
        }
    }
    std::printf("%ld frames presented\n", n);
    if (!dump.empty()) {
        // The framebuffer the WINDOW was shown, as raw LE RGB565 - so the
        // live half of PORTING A1's pair can be diffed against the reference
        // half, which is the property rule 3 is about: the frontend uploads
        // the pixels and must not have touched them.
        std::ofstream o(dump, std::ios::binary);
        for (auto v : fb.px) {
            const char b2[2] = {static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)};
            o.write(b2, 2);
        }
    }
    {
        const auto& ps = session.scene().effects().particles();
        float lo[3] = {1e9f,1e9f,1e9f}, hi[3] = {-1e9f,-1e9f,-1e9f}, sc = 0;
        for (const auto& p : ps) {
            for (int k = 0; k < 3; ++k) {
                if (p.pos[k] < lo[k]) lo[k] = p.pos[k];
                if (p.pos[k] > hi[k]) hi[k] = p.pos[k];
            }
            if (p.scale > sc) sc = p.scale;
        }
        if (!ps.empty())
            std::printf("particles: sprite %d, scale up to %.2f, box "
                        "%.0f %.0f %.0f .. %.0f %.0f %.0f\n",
                        ps.front().sprite, sc, lo[0], lo[1], lo[2],
                        hi[0], hi[1], hi[2]);
    }
    if (session.dialogOpen()) {
        const auto& dlg = session.dialogue();
        std::printf("dialogue: line '%s' at %.2f of %.2f s - frame %d of %d, blend %g\n",
                    dlg.voice().c_str(), dlg.elapsed(), dlg.lineSeconds(),
                    static_cast<int>(dlg.elapsed() * 30.0), speakerTracks.frames,
                    omk::morphBlendFrames(speakerTracks.frames));
    }
    std::printf("effects: %d set pieces shown so far, %d shown now, "
                "%d emitters registered on the last frame, %zu particles alive\n",
                session.scene().piecesFired(), session.scene().pieces().shownCount(),
                session.scene().pieces().registered(),
                session.scene().effects().count());
    std::printf("world: %ld frames drawn, last set %s (%d shown), last camera %d, "
                "%ld frames under player.anim.hold\n",
                worldFrames, worldSet.empty() ? "(none)" : worldSet.c_str(),
                session.shownCount(), session.cameraId(), heldFrames);
    {
        std::string ids;
        for (std::size_t k = 0; k < stagedIds.size(); ++k)
            ids += (k ? ", " : "") + std::to_string(stagedIds[k]);
        std::printf("staged %ld characters (ids %s), %zu on screen at the end, "
                    "%zu models and %zu banks resident\n",
                    stagedEver, ids.empty() ? "none" : ids.c_str(),
                    staged.size(), charModels.size(), charBanks.size());
        for (const auto& up : staged)
            std::printf("  actor %d %s (bank %s) at %.0f %.0f %.0f facing %.0f - %s%s\n",
                        up->actor, up->model.c_str(),
                        up->bank.empty() ? "none" : up->bank.c_str(),
                        up->drawAt[0], up->drawAt[1], up->drawAt[2], up->facing,
                        up->src, up->drawn ? "" : "  [not drawn]");
    }
    if (player)
        std::printf("player: %s/%s at %.1f %.1f %.1f facing %.1f, ACTOR_STATE %d, "
                    ".CTL state %d '%s' clip %s frame %.1f, walked %.1f over %ld ticks, "
                    "pose tracks %s\n",
                    playerModel.c_str(), playerCtlName.c_str(), player->pos()[0],
                    player->pos()[1], player->pos()[2], player->facing(),
                    static_cast<int>(player->state()), player->ctlState(),
                    player->ctlStateName().c_str(), player->clipName().c_str(),
                    player->clipFrame(), player->distanceWalked(), player->ticks(),
                    player->poseTracks() ? "valid" : "NONE (drawn at rest - a T-pose)");
    std::printf("session: %d areas entered, %d ui answers\n",
                session.areasEntered(),
                static_cast<int>(session.uiAnswers().size()));
    front.close();
    return 0;
}
