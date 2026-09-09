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
#include "actor/shoot.h"
#include "actor/moves.h"
#include "actor/slider.h"

#include <limits>
#include <optional>
#include "actor/pose.h"
#include "actor/speaker.h"
#include "actor/player.h"
#include "actor/moves.h"
#include "actor/walk.h"
#include "o3de/collision.h"
#include "o3de/geom3do.h"
#include "o3de/particles.h"
#include "o3de/shadow.h"
#include "o3de/shimmer.h"
#include "audio/mixer.h"
#include "audio/music.h"
#include "audio/voiceover.h"
#include "formats/adpcm.h"
#include "script/area.h"
#include "script/savefile.h"
#include "formats/light3do.h"
#include "o3de/vertexlight.h"
#include "platform/options.h"
#include "platform/settings.h"
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
// `cN` in a `--keys` list arrives as `kCharMarker - N`, so one negative range
// carries any character the field's switch understands. -2 is character 0,
// which nothing sends, so the first real one is -10 for backspace.
constexpr int kCharMarker = -2;

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
// **What is NOT recovered**: the reply menu has no recovered placement at all
// and is stacked under the line, with the selected row in the ink and the rest
// in the 0x808080 the engine also loads. So the BOX is read from the engine
// and the STACK inside it is a reconstruction; they are labelled differently
// on purpose. The WRAP is no longer among them - `Text_LayOutBlock` is ported
// (2026-09-07) and `drawSubtitle` lays every string through it.
//
// `wrapInto` was this file's own greedy break on spaces and has gone with it.

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

// THE CONTROL CHARACTERS THE ENGINE'S CHARACTER CHANNEL CARRIES.
//
// `sub_4397B0` hands the name-field hook ONE character a frame, and the
// hook's switch covers 8..27 - so BACKSPACE, TAB, RETURN and ESCAPE arrive
// there as characters, not as input bits. That channel is Windows' `WM_CHAR`,
// which delivers exactly those four alongside the printable keys.
//
// SDL's text-input events do NOT: `SDL_TEXTINPUT` carries printable text
// only. So the frontend has to put them back, which is the same job
// `keymap()` above does for scan codes - a physical key turned into the code
// the engine expects, and no interface behaviour of its own. Without it a
// player could type a name and never correct it: backspace did nothing at
// all, which is what one reported.
//
// A key that is both - RETURN is character 13 AND the confirm bit - reaches
// the walk twice, exactly as it reaches the engine twice (DirectInput for the
// scan code, `WM_CHAR` for the character). `Ui_DispatchInput` stops at the
// first hook to return 1, and the caller below models that by dropping the
// frame's bits when the field consumed it.
//
// Repeats are kept rather than filtered: `WM_CHAR` auto-repeats, so holding
// BACKSPACE deletes until the field is empty.
const std::map<int, char>& charmap() {
    static const std::map<int, char> m = {
        {SDL_SCANCODE_BACKSPACE, 8},  {SDL_SCANCODE_TAB,    9},
        {SDL_SCANCODE_RETURN,    13}, {SDL_SCANCODE_KP_ENTER, 13},
        {SDL_SCANCODE_ESCAPE,    27},
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
            else if (e.type == SDL_EVENT_KEY_DOWN) {
                const auto c = charmap().find(static_cast<int>(e.key.scancode));
                if (c != charmap().end()) out.text += c->second;
            }
#else
            if (e.type == SDL_QUIT) out.quit = true;
            else if (e.type == SDL_TEXTINPUT) out.text += e.text.text;
            else if (e.type == SDL_KEYDOWN) {
                const auto c = charmap().find(
                    static_cast<int>(e.key.keysym.scancode));
                if (c != charmap().end()) out.text += c->second;
            }
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
            if (self->sHead_ < self->stream_.size()) v += self->stream_[self->sHead_++] * self->musicGain_;
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

    void setMusicGain(float g) override {
        std::lock_guard<std::mutex> lk(amx_);
        musicGain_ = g < 0.0f ? 0.0f : (g > 1.0f ? 1.0f : g);
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
    float musicGain_ = 1.0f;      // Music_SetVolume, applied to the stream in feed()
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
                const std::string& dump, bool startVulkan, bool noDelay,
                int aaSamples, int texFilter, int texAniso, int ssaa,
                bool sceneDither) {
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
    float sceneShimmer = 0.0f;   // `dword_907310` for the set viewer's own path
    omk::SoftwareRenderer sw;
    sw.init(PW, PH);
    omk::Renderer* live = nullptr;
#if defined(OMK_VULKAN)
    live = omk::makeVulkanRenderer();
    if (live && aaSamples > 1) live->setMultisample(aaSamples);   // the enhancements
    if (live && texFilter > 0) live->setTextureFilter(texFilter);
    if (live && texAniso > 1) live->setAnisotropy(texAniso);
    if (live && ssaa > 1) live->setSupersample(ssaa);
    if (live && !live->init(PW, PH)) { delete live; live = nullptr; }
    if (live) {
        live->setTextures(tex);
        std::printf("vulkan: %s  (V swaps the backend)\n",
                    omk::vulkanDeviceName(live));
    } else {
        std::printf("vulkan: no device - software only\n");
    }
    if (aaSamples > 1)
        std::printf("aa: %dx MSAA asked - an ENHANCEMENT the original never had; "
                    "%s\n", aaSamples,
                    live ? "the Vulkan backend has it, the software one does not"
                         : "no Vulkan device, so nothing here draws it");
    if (texFilter > 0)
        std::printf("filter: %s%s asked - an ENHANCEMENT the original never had; "
                    "%s\n", omk::textureFilterName(texFilter),
                    texAniso > 1 ? " with anisotropy" : "",
                    live ? "the Vulkan backend has it, the software one does not"
                         : "no Vulkan device, so nothing here draws it");
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
            if (aaSamples > 1) vr->setMultisample(aaSamples);
            if (texFilter > 0) vr->setTextureFilter(texFilter);
            if (texAniso > 1) vr->setAnisotropy(texAniso);
            if (ssaa > 1) vr->setSupersample(ssaa);
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
        // THE SHIMMER's clock, advanced here as well - the set viewer is
        // exactly where a skyline gets looked at, and it draws through its own
        // path rather than the frame loop's.
        sceneShimmer += 2.0f;
        if (sceneShimmer >= omk::kShimmerWrap) sceneShimmer -= omk::kShimmerWrap;
        view.shimmerClock = sceneShimmer;
        view.dither = sceneDither;
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
        // the SIZE, not a literal: this said "640x480" whatever it wrote.
        std::printf("wrote %s (%dx%d RGB565)\n", dump.c_str(), fb.w, fb.h);
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

// THE MORPH ROOT TURNED BY A WORLD YAW, the engine's own composition.
// `08_wave.c` 963-965: `sub_442B50(v40, 0, X, 0)` builds the yaw and
// `sub_442940(&root, &yaw, out)` - the Hamilton product `root ⊗ yaw`
// (16_o3de.c 1704) - composes it onto the .3DM's root, per frame, BEFORE the
// fade against the node's current pose. Adjudicated against the port's own
// heading extractor on a real root: of the four candidate products only
// `stored ⊗ Ry(+H)` moves the heading by exactly H (338.0 for H = 338).
static void turnRootBy(omk::NodeTracks& t, float yawDeg) {
    if (!t.valid() || t.rootTrack < 0) return;
    const float h = yawDeg * 0.0174532925199433f * 0.5f;
    const omk::Quatf ry{std::cos(h), 0.0f, std::sin(h), 0.0f};
    for (auto& frame : t.quats)
        if (static_cast<std::size_t>(t.rootTrack) < frame.size())
            frame[static_cast<std::size_t>(t.rootTrack)] =
                omk::qmul(frame[static_cast<std::size_t>(t.rootTrack)], ry);
}

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
"  --dump out.bin   write the last framebuffer as RGB565. NOTE: this still\n"
"                   OPENS A WINDOW, and a window takes the keyboard - set\n"
"                   SDL_VIDEODRIVER=dummy for a headless render, which is\n"
"                   what verify.py does. Two comparison renders once came\n"
"                   out of different places because a reader saw the window\n"
"                   appear and walked the player, quite reasonably\n"
"  --snaps <dir>    write a framebuffer every 30 frames after the hand-over\n"
"  --snap-every N   ...every N frames instead; 1 is how a FLICKER is caught,\n"
"                   a fault no one-second snapshot can show\n"
"  --flicker <dir>  CATCH A ONE-TO-FIVE-FRAME FAULT. Watches its own\n"
"                   output and, when a frame is far darker than the median\n"
"                   of the last 31 - a body drawn on a set that is not\n"
"                   there - writes that frame with the three before and\n"
"                   three after it, plus a line of context each (which\n"
"                   camera, the set runs drawn and culled, the bodies).\n"
"                   For a fault you can see and cannot screenshot\n""  --fps            report the rate and the worst frame once a second\n"
"\n"
"SAVED GAMES\n"
"  --saves FILE     the save file this port reads and WRITES, default\n"
"                   omk-saves/GAMES. The engine keeps it as IAM\\GAMES\n"
"                   inside the game tree; this port must not, because\n"
"                   gamedata/ is input and safeOutputPath refuses it. When\n"
"                   the file does not exist yet, reading falls back to the\n"
"                   shipped IAM/GAMES so the directory still reads\n"
"  --slot N         load slot N and RESUME: adventure mode in the save's\n"
"                   own area, standing where it was saved. --area, --stand\n"
"                   or --address override the placement; --save names a\n"
"                   file to take the slot from instead of --saves\n"
"  --save-slot N    when the run ENDS, write a save into slot N: the live\n"
"                   area and the scene over it, the player's position and\n"
"                   facing, the clock, and a 128x96 picture of the last\n"
"                   frame - which is what the load panel draws beside the\n"
"                   row. A HARNESS path: in the game a save is `ui.open 30`\n"
"                   out of a save point's activate script and costs one\n"
"                   anneau, and neither of those is here yet\n"
"  --save-name S    the profile name to write with (default: the name of\n"
"                   the slot this run loaded)\n"
"\n"
"STARTING IN A STREET (street life)\n"
"  --save FILE      load a save instead of IAM/START, so there is no intro.\n"
"                   Slot 0 unless --slot says otherwise. Paired with --area\n"
"                   this drops the save's PLAYER RECORD into another area,\n"
"                   which is what the street starts want; on its own with\n"
"                   --slot it resumes the game the save holds\n"
"  --area N         the area to stand in\n"
"  --address A      the ADDRESSES record to stand on\n"
"  --ride           mount a slider where he stands and FLY it (step 3's\n"
"                   harness: no pool, no reservation, no arrival)\n"
"  --stand x,y,z[,facing]   an explicit spot instead of an address\n"
"  --density 0..4   how much crowd - the options menu\'s own row 6\n"
"  --shadows 0|1    the character shadows - options row 5 (--no-shadows too)\n"
"  --detail 0..2    how many bones cast one - options row 7\n"
"  --world-vulkan   draw the WORLD through an offscreen Vulkan renderer (a\n"
"                   harness: it needs no window, so the GPU-only enhancements\n"
"                   can be measured headlessly)\n"
"  --shadow-quality classic|fitted|mapped  ENHANCEMENT: fitted lays each blob\n"
"                   on the surface under it; mapped is a real shadow map, the\n"
"                   Vulkan backend only (default classic)\n"
"  --dither 0|1     the engine's own DITHERENABLE, on by default - `--no-dither`\n"
"                   is for comparing two frames, not for play\n"
"  --ssaa N         ENHANCEMENT: render N times larger each way and average it\n"
"                   down - 1 off, 2 or 4. Reaches the CUTOUT edges (grilles,\n"
"                   railings, signs) that MSAA never looks at; Vulkan only\n"
"  --enhance-all    every ENHANCEMENT as high as it goes - none of them is what\n"
"                   the original drew; a specific flag still wins\n"
"  --lighting pervertex|perpixel  ENHANCEMENT: the engine's own light law per\n"
"                   fragment, and every character receives it rather than the\n"
"                   crowd alone; Vulkan only (default pervertex)\n"
"\n"
"  the HARNESS flags - ways in that the game reaches by being played:\n"
"  --call N         the SNEAK CALL idiom (`ui.open 0` then `dialog.start N`) on\n"
"                   the first adventure frame; in the game a zone script does it\n"
"  --anim-hold      hold the player as a staged beat's `player.anim.hold` does,\n"
"                   for reaching camera mode without playing the beat\n"
"  --board          walk onto the called slider and press action once, so the\n"
"                   engine\'s own boarding gate runs (todo/slider.md)\n"
"  --newgame-world  keep the save\'s PLAYER but take the world from IAM\\START,\n"
"                   to try a flow against a new game\'s state without the intro\n"
"  --var N=V,...    set world VARIABLES before the first frame, for a gate that\n"
"                   a save predates\n"
"  --config <ini>   the game's own config file - [Preferences], and this\n"
"                   port's [Options] for density and level of detail\n"
"  --clip <metres>  options row 3, the clip distance (25/50/100/150/200);\n"
"                   a --save's own header supplies it otherwise\n"
"  --sky 0|1        options row 4, Affichage du ciel\n"
"  --fog 0|1        the linear fog (default on - the engine always fogs)\n"
"  --no-crowd-light  do not light the crowd from the set's .3DO lights\n"
"  --fog-colour r,g,b   override the scene's +336, which ships as 0,0,0\n"
"  --no-crowd       no pedestrians at all\n"
"  --no-script-sprites  DEBUG: do not draw Script_Display3DSprite's sprites (a before/after)\n"
"  --scx-play h,h   HARNESS: start scene objects by handle on the first adventure frame\n"
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
"  --aa N           ENHANCEMENT, off by default: N-sample MSAA (2/4/8) on the\n"
"                   Vulkan backend only - the original has none; also\n"
"                   [Enhancements] antialiasing=N in --config\n"
"  --filter M       ENHANCEMENT, off by default: texture filtering, M =\n"
"                   nearest (the original), bilinear or trilinear (a mip\n"
"                   chain generated at load); Vulkan only; also\n"
"                   [Enhancements] texturefiltering=M in --config\n"
"  --anisotropy N   ENHANCEMENT: N-tap anisotropic filtering (2..16), with\n"
"                   trilinear only; [Enhancements] anisotropy=N\n"
"  --clip 0         ENHANCEMENT: UNLIMITED draw distance - the option's own\n"
"                   values stop at 200 m. The fog goes with it, its range\n"
"                   being the clip distance; [Enhancements] clipdistance=0\n"
"  --ui-scaling M   ENHANCEMENT, off by default: how the 640x480 interface is\n"
"                   stretched to the display, M = nearest (the original's\n"
"                   Blt) or linear; BOTH backends, since the interface is\n"
"                   composed on the CPU; [Enhancements] uiscaling=M\n"
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
    std::string holdStream, snapsDir, flickerDir;
    int snapEvery = 30;            // `--snap-every N`: 1 catches a flicker
    // A STREET START (docs/STREET_LIFE.md, step 4): `--save FILE` takes the
    // game DB from a save's slot 0 - the player record lives there, and
    // Kay'l's actor record is in no city chunk - `--area N` loads that area
    // instead of the save's own, `--address A` puts him down on one of its
    // ADDRESSES (listed at start), and adventure mode begins at once, with
    // no intro to replay. `--density` is options row 6 for the crowd
    // (default the engine's 3), `--no-crowd` leaves the pedestrians out.
    std::string saveFile;
    // WHERE THIS PORT KEEPS ITS SAVES, and it is deliberately not where the
    // engine keeps them.  `Game_WriteSave` writes `IAM\GAMES` inside the game
    // directory; `omk::safeOutputPath` refuses that, because `gamedata/` is
    // input and a shipped file was destroyed once already (CLAUDE.md 1).  So
    // the file lives outside the tree and READING falls back to the shipped
    // one, which means a checkout that has never been saved into still sees
    // the (empty) directory the game would see.  `--saves FILE` moves it.
    std::string savesPath = "omk-saves/GAMES";
    // Which slot of it to load.  -1 is "no slot asked for", which is not the
    // same as slot 0: asking for a slot is asking to RESUME, and that starts
    // adventure mode in the save's own area at its own placement, the way
    // `Game_LoadSave` -> `State_Apply` does.
    int slotArg = -1;
    // ...and the slot a run WRITES when it ends.  A harness path, not the
    // game's: in the game a save is `ui.open 30` out of a save point's
    // activate script and the ring is spent when the panel confirms
    // (GAME_STATE 8c).  This writes the file without either, so a save can be
    // made and re-loaded before the panel exists.
    int saveSlotArg = -1;
    std::string saveNameArg;
    int areaArg = -1, addressArg = -1, density = omk::kDefaultStreetActivity;
    // `--ride`: MOUNT the player on a slider where he stands, without the
    // pool, the reservation or the arrival - `todo/slider.md` step 3's
    // harness. The flight model is the engine's (`actor/slider.h`); what this
    // skips is how a slider gets to you, which is `sub_452570`'s other arm.
    bool rideArg = false;
    // `--board`: HARNESS. When the called slider goes OPEN, put him at its
    // door point and press the action button once, so a check can board
    // without a scripted walk that has to find the door side of a vehicle
    // whose park point moves with every call. The gate - `MDACTION`'s side
    // and reach - still runs for real on where he is put.
    bool boardArg = false;
    bool boardPress = false;
    bool mountSpent = false;    // the action button is edged, not held
    bool calledOpenTold = false;
    bool boarded = false;           // aboard, `Slider_TickRide` not yet driving
    // ...and the BOARDING that comes first: `MDACTION` has put him at the
    // door, group 60 (`H_SLDIN`, 72 frames) is playing the door and the step
    // in, and the channel's own `MDSLIDIN` entry at the end of it is what
    // takes him aboard. Between the two he is ACTOR_STATE 6.
    bool  boarding = false;
    float doorOff[3] = {0, 0, 0};   // the placement, in the SLIDER's frame
    int   doorOffState = 0;         // 0 not read yet, 1 read, -1 unavailable
    int   boardCam = 0;             // frames left of `Camera_Request(9, ..)`
    // ...and the EXIT, which is the same shape mirrored: `sub_468FA0` places
    // him from group 61's clip against a DIFFERENT reference (slf_113.3da,
    // `dword_9103D8`) and plays `H_SLDOUT`.
    bool  leaving = false;
    float exitOff[3] = {0, 0, 0};
    int   exitOffState = 0;
    // ---- THE SLIDER'S OWN DOOR CLIPS -------------------------------------
    //
    // `Cef_TickChannel`'s ACTOR_STATE switch (19_dsound.c, cases 6 and 8)
    // plays a clip ON THE SLIDER while the character plays `H_SLDIN` /
    // `H_SLDOUT`, driven by the SAME clock `a2`:
    //
    //     sub_437FC0(sub, dword_90EF28);              // bind
    //     sub_437FE0(sub, dword_90EF28, 0.0, a2, &d); // sample
    //     sub_438310(slider, &sp);
    //     sub_437F80(sub, sp + d, sp.y - 33.149605 + d.y, sp.z + d.z);
    //
    // `dword_90EF28` is `ANIMS\slf_112.3da` and `dword_9103D8` is
    // `slf_113.3da` (05_sys.c 1842-1844) - and the clips are **72 and 51
    // frames**, exactly the lengths of `H_SLDIN` and `H_SLDOUT`. So the door
    // is an ANIMATION, not the two-state model swap `sub_4521E0` does, and
    // this port had read the clips only for their root key and never played
    // them. `build/slider_doorclip` measures what each drives: of the five
    // tracks, one moves - **`SlPorteG` turns 71.4 degrees**, the gull-wing
    // swing - and the other four and the root hold still.
    omk::NodeTracks doorIn, doorOut;
    bool doorClipsRead = false;
    int  journeyTo = -1;            // the address the journey ends at
    // `dword_6A17CC` - which destination row the call was made for.
    int  calledDestination = -1;
    // THE LIVE RIDE, when there is one. `todo/slider.md` step 3's harness.
    std::optional<omk::SliderRide> ride;
    // --config: the game's own ini (`[Preferences]`, 65 keys) plus this
    // port's `[Options]` for the two rows with no key. The SAVE's 3496-byte
    // header carries the same settings and is LATER, so it wins - see
    // `platform/settings.h`. An explicit --density or --clip beats both,
    // because a flag typed on the command line is the most recent word of
    // all; `densityFlag`/`clipFlag` record whether one was.
    std::string configFile;
    bool densityFlag = false, clipFlag = false;
    int clipArg = 0;
    int skyFlag = -1;      // --sky 0|1, options row 4; -1 = take it from the settings
    int shadowFlag = -1;   // --shadows 0|1, options row 5; -1 = the settings'
    int detailFlag = -1;   // --detail 0..2, options row 7; -1 = the settings'
    int aaFlag = -1;       // --aa N, [Enhancements] antialiasing; -1 = the settings'
    int filterFlag = -1;   // --filter nearest|bilinear|trilinear, [Enhancements] texturefiltering
    int anisoFlag = -1;    // --anisotropy N, [Enhancements] anisotropy
    int shadowQFlag = -1;  // --shadow-quality classic|fitted|mapped, [Enhancements] shadowquality
    int uiScaleFlag = -1;  // --ui-scaling nearest|linear, [Enhancements] uiscaling
    int lightingFlag = -1; // --lighting pervertex|perpixel, [Enhancements] lighting
    int ssaaFlag = -1;     // --ssaa N, [Enhancements] supersampling
    // `--dither 0|1`. NOT an enhancement: `sub_4638C0` sets D3DRENDERSTATE 26
    // (DITHERENABLE) to 1 on both device arms, so on is what the engine does.
    // The flag exists to lay a dithered frame beside an undithered one.
    bool dither = true;
    // --enhance-all: every enhancement as high as it goes, in one word. The
    // two the DEVICE caps are asked for at their largest defined value and the
    // backend reduces what it cannot meet, which is what "max available" means
    // here. A specific flag still wins, whatever order they are typed.
    bool enhanceAll = false;
    // The fog is not an option row - it is always on in the engine - so this
    // is a diagnostic switch, not a setting. Default ON, because that is what
    // the game does.
    bool drawFog = true;
    // The crowd's dynamic lighting from the set's own light table. On by
    // default because that is what the engine does; `--no-crowd-light` is the
    // before/after.
    bool lightCrowd = true;
    std::uint8_t fogRGB[3] = {0, 0, 0};   // the scene's +336, which ships as 0
    // --give: object ids for the carried list, comma-separated. A LIST
    // rather than one id because the flows worth driving need a bagful - row
    // scrolling wants more than the nine row widgets, and `Utiliser sur`
    // wants a recipe PAIR, and a new game ships exactly two objects.
    std::string giveList;
    std::string varList;
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
    bool noScriptSprites = false;   // DEBUG: leave the scripted sprites undrawn, for a before/after
    std::vector<int> scxPlay;       // --scx-play: objects to start by handle, once
    bool scxPlayed = false;
    // `--sneak` opens the device as soon as the player is on his feet,
    // through the SAME path TAB takes - `MDSNEAK0`'s handler, event 25 and
    // screen 9 - rather than a second way in. A testing convenience for a
    // screen that otherwise costs a walk to reach; it sets the same
    // `playerScreen` the special move sets and nothing else.
    bool openSneak = false;
    bool startShoot = false;   // --shoot: enter shoot mode at the hand-over
    float standAt[4] = {0, 0, 0, 0};
    bool haveStand = false;      // `--stand x,y,z,yaw`: put the player down there after the hand-over
    // ...and the save's OWN placement, which is `State_Apply`'s and not a
    // harness flag: a loaded game stands where it was saved unless something
    // explicit says otherwise.
    float savedAt[3] = {0, 0, 0}, savedYaw = 0.0f;
    bool  haveSavedPlacement = false;
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
    // `--call N`: the SNEAK CALL idiom - `ui.open 0` then `dialog.start N` -
    // fired on the first adventure frame. A HARNESS: in the game it is a zone
    // script that does this, and reaching one means playing the beat.
    int  callDialog = -1;
    // `--anim-hold`: hold the player on the first adventure frame, the way a
    // staged beat's `player.anim.hold` does. A HARNESS, for reaching camera
    // mode without playing a beat.
    bool animHoldHarness = false;
    bool haveEye = false, haveAt = false, letterbox = false, startVulkan = false, noDelay = false;
    double speed = 1.0;                 // --speed: the frame delta's multiplier
    bool forceSoftware = false, showFps = false;
    // A HARNESS flag, not a mode: draw the 3D world through a SURFACELESS
    // Vulkan renderer while the frame is still presented (or dumped) the
    // ordinary way. `--vulkan` needs a real window and therefore a real
    // display, so nothing that only the GPU backend does - the mapped shadow,
    // the MSAA, the filters - can otherwise be measured headlessly.
    bool worldVulkan = false;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--scene" && i + 1 < argc) scene = argv[++i];
        else if (a == "--cam" && i + 1 < argc) camIndex = std::atoi(argv[++i]);
        else if (a == "--fov" && i + 1 < argc) fovA = static_cast<float>(std::atof(argv[++i]));
        else if (a == "--call" && i + 1 < argc) callDialog = std::atoi(argv[++i]);
        else if (a == "--anim-hold") animHoldHarness = true;
        else if (a == "--letterbox") letterbox = true;   // camera mode, for comparing with captures
        else if (a == "--full") letterbox = false;       // kept: it was the old spelling
        else if (a == "--vulkan") startVulkan = true;
        // The game uses Vulkan when the machine has it; this forces the
        // software reference, which is what every check is written against.
        else if (a == "--software") forceSoftware = true;
        else if (a == "--world-vulkan") worldVulkan = true;
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
        else if (a == "--snap-every" && i + 1 < argc) snapEvery = std::max(1, std::atoi(argv[++i]));
        else if (a == "--flicker" && i + 1 < argc) flickerDir = argv[++i];
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
        else if (a == "--saves" && i + 1 < argc) savesPath = argv[++i];
        else if (a == "--slot" && i + 1 < argc) slotArg = std::atoi(argv[++i]);
        else if (a == "--save-slot" && i + 1 < argc) saveSlotArg = std::atoi(argv[++i]);
        else if (a == "--save-name" && i + 1 < argc) saveNameArg = argv[++i];
        else if (a == "--area" && i + 1 < argc) areaArg = std::atoi(argv[++i]);
        else if (a == "--address" && i + 1 < argc) addressArg = std::atoi(argv[++i]);
        else if (a == "--ride") rideArg = true;
        else if (a == "--board") boardArg = true;
        // A HARNESS FLAG, not a port: put an object into the carried list so
        // a flow can be exercised from a save that does not carry it. VM
        // opcode 50 `inventory.add` is what the game uses; this writes the
        // slot and runs none of its bookkeeping.
        else if (a == "--give" && i + 1 < argc) giveList = argv[++i];
        // A HARNESS FLAG, not a port: `--var 652=1,657=1` writes the game DB
        // directly. A flow can sit behind state no flag can otherwise reach -
        // the flat's lift gate tests `Porte Asc Fermee` and `Rencontre Telis`
        // before it even looks at what you carry, so the Telis cutscene
        // behind it cannot be entered from a save that predates them.
        else if (a == "--var" && i + 1 < argc) varList = argv[++i];
        else if (a == "--bank-reject") bankReject = true;
        // ...and its companion: keep the save's PLAYER but take the world
        // from `IAM\START`, so a flow can be tried against a new game's
        // state without the intro. Also a harness flag, not a port.
        else if (a == "--newgame-world") newWorld = true;
        else if (a == "--scene-chunk" && i + 1 < argc) sceneChunk = std::atoi(argv[++i]);
        else if (a == "--density" && i + 1 < argc) { density = std::atoi(argv[++i]); densityFlag = true; }
        else if (a == "--shadows" && i + 1 < argc) shadowFlag = std::atoi(argv[++i]);
        else if (a == "--no-shadows") shadowFlag = 0;
        else if (a == "--detail" && i + 1 < argc) detailFlag = std::atoi(argv[++i]);
        else if (a == "--config" && i + 1 < argc) configFile = argv[++i];
        // `--clip 0` is the ENHANCEMENT, not a zero distance: the option's
        // five values stop at 200 m and 0 is outside them, so it says "no cap".
        else if (a == "--clip" && i + 1 < argc) { clipArg = std::atoi(argv[++i]); clipFlag = true; }
        else if (a == "--sky" && i + 1 < argc) skyFlag = std::atoi(argv[++i]);
        else if (a == "--aa" && i + 1 < argc) aaFlag = omk::msaaSamples(std::atoi(argv[++i]));
        else if (a == "--filter" && i + 1 < argc) {
            filterFlag = omk::textureFilterMode(argv[++i]);
            if (filterFlag < 0) {
                std::fprintf(stderr, "--filter %s: not a mode (nearest|bilinear|trilinear)\n",
                             argv[i]);
                return 2;
            }
        }
        else if (a == "--anisotropy" && i + 1 < argc)
            anisoFlag = std::max(1, std::min(16, std::atoi(argv[++i])));
        else if (a == "--enhance-all") enhanceAll = true;
        else if (a == "--ssaa" && i + 1 < argc) ssaaFlag = std::atoi(argv[++i]);
        else if (a == "--dither" && i + 1 < argc) dither = std::atoi(argv[++i]) != 0;
        else if (a == "--no-dither") dither = false;
        else if (a == "--lighting" && i + 1 < argc) {
            lightingFlag = omk::lightingMode(argv[++i]);
            if (lightingFlag < 0) {
                std::fprintf(stderr, "--lighting %s: not a mode (pervertex|perpixel)\n",
                             argv[i]);
                return 2;
            }
        }
        else if (a == "--ui-scaling" && i + 1 < argc) {
            uiScaleFlag = omk::uiScalingMode(argv[++i]);
            if (uiScaleFlag < 0) {
                std::fprintf(stderr, "--ui-scaling %s: not a mode (nearest|linear)\n",
                             argv[i]);
                return 2;
            }
        }
        else if (a == "--shadow-quality" && i + 1 < argc) {
            shadowQFlag = omk::shadowQualityMode(argv[++i]);
            if (shadowQFlag < 0) {
                std::fprintf(stderr, "--shadow-quality %s: not a mode "
                                     "(classic|fitted|mapped)\n", argv[i]);
                return 2;
            }
        }
        else if (a == "--fog" && i + 1 < argc) drawFog = std::atoi(argv[++i]) != 0;
        else if (a == "--no-crowd-light") lightCrowd = false;
        else if (a == "--fog-colour" && i + 1 < argc) {
            int rr = 0, gg = 0, bb = 0;
            if (std::sscanf(argv[++i], "%d,%d,%d", &rr, &gg, &bb) == 3) {
                fogRGB[0] = static_cast<std::uint8_t>(std::clamp(rr, 0, 255));
                fogRGB[1] = static_cast<std::uint8_t>(std::clamp(gg, 0, 255));
                fogRGB[2] = static_cast<std::uint8_t>(std::clamp(bb, 0, 255));
            }
        }
        else if (a == "--no-crowd") noCrowd = true;
        else if (a == "--no-script-sprites") noScriptSprites = true;
        // A HARNESS FLAG: start scene objects by their `scx.play` operand (the
        // handle >> 16) on the first adventure frame the scene is resident,
        // so a scene function can be looked at without the script that
        // would reach it. Not a port of anything.
        else if (a == "--scx-play" && i + 1 < argc) {
            std::string t = argv[++i], cur;
            for (char c : t + ",") {
                if (c == ',') { if (!cur.empty()) scxPlay.push_back(std::atoi(cur.c_str())); cur.clear(); }
                else cur.push_back(c);
            }
        }
        else if (a == "--shoot") startShoot = true;
        else if (a == "--sneak") openSneak = true;
        else if (a == "--stand" && i + 1 < argc)
            haveStand = std::sscanf(argv[++i], "%f,%f,%f,%f", &standAt[0], &standAt[1], &standAt[2], &standAt[3]) >= 3;
        else if (a == "--type" && i + 1 < argc) typeText = argv[++i];
        else if (a == "--keys" && i + 1 < argc) {
            // A `T` in the list means "type `--type` here" - the start menu's
            // confirm is gated on a non-empty name field, so a scripted walk
            // has to type between two presses, not before them.
            //
            // ...and `cN` sends CHARACTER N down the same channel, which is
            // the only way to drive the field's control codes headlessly:
            // BACKSPACE and RETURN are `WM_CHAR` 8 and 13 to the engine and
            // reach no binding table at all, so a scan code in this list
            // cannot express them. `c8` is a backspace.
            std::string t = argv[++i], cur;
            for (char c : t + ",") {
                if (c == ',') {
                    if (cur == "T") scripted.push_back(kTypeMarker);
                    else if (cur.size() > 1 && cur[0] == 'c')
                        scripted.push_back(kCharMarker -
                                           std::stoi(cur.substr(1), nullptr, 0));
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
                           startVulkan, noDelay, aaFlag < 0 ? 0 : aaFlag,
                           filterFlag < 0 ? 0 : filterFlag, anisoFlag < 0 ? 1 : anisoFlag,
                           // the scene viewer runs BEFORE settings resolve, so
                           // it takes the flag alone; `--config` is the game's
                           ssaaFlag < 0 ? 1 : ssaaFlag, dither);

    const omk::DataFs fs(fr);
    auto w = omk::UiWidgets::loadJson(tb + "/ui_widgets.json");
    // `Ui_BuildLoadPanel`'s layout, applied for screen 29.  The four buttons
    // of the load panel all ship at (460, 210) and the builder moves three of
    // them apart; without this they draw on top of one another and the panel
    // shows one line of text where the original shows three.  The engine does
    // this in the OPEN callback and would redo it for screen 30's save
    // layout - which this port does not open yet, so it is applied once.
    omk::applyLoadPanelLayout(w, 29);
    if (!w.valid())
        std::printf("tables: no widget tree (ui_widgets.json) - the interface "
                    "screens cannot be drawn or walked, so the Session answers "
                    "them itself and the start menu is skipped\n");
    w.loadScreens(tb + "/ui.json");
    const auto fonts = omk::FontTable::loadJson(tb + "/ui.json");
    const omk::TextLayout lay(fonts, fr + "/FONTS");
    omk::ScreenComposer comp(fs, w, lay);
    // The world rendered at a panel's 3D VIEWPORT item's size, for the
    // composer to place (`ScreenComposer::attachView3D`).
    omk::Surface view3dPic;
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
    // THE SETTINGS, from the three sources in their own order: the engine's
    // defaults (`sub_41F4C0`), then the ini, then the save file's 3496-byte
    // header - which is what the options MENU last wrote, and so is later
    // than the ini and wins (`platform/settings.h`, GAME_STATE 8a). The
    // header is read from the same bytes the slot comes out of, because it
    // IS the head of that file.
    //
    // WHICH FILE, and it is one decision made once so the settings and the
    // slot cannot come from two different places: an explicit `--save` names
    // a file directly (the fixtures do), otherwise a `--slot` reads the saves
    // file - the writable one if it exists, else the shipped `IAM/GAMES`.
    std::vector<std::byte> saveBytes;
    std::string saveFrom, loadedName;
    if (!saveFile.empty()) {
        saveBytes = omk::DataFs::readPath(saveFile);
        saveFrom = saveFile;
    } else if (slotArg >= 0) {
        saveFrom = savesPath;
        saveBytes = omk::readSaveFile(savesPath, fr + "/IAM/GAMES", &saveFrom);
        if (saveBytes.empty())
            std::fprintf(stderr, "--slot: no save file at %s, and none in the "
                                 "game tree either\n", savesPath.c_str());
    }
    std::optional<omk::SettingsBlock> saveSettings;
    if (!saveBytes.empty())
        saveSettings = omk::readSettingsBlock(saveBytes);
    const omk::OptionsFile ini =
        configFile.empty() ? omk::OptionsFile{} : omk::loadOptionsFile(configFile);
    if (!configFile.empty() && !ini.loaded)
        std::fprintf(stderr, "%s: no such config file - using defaults\n", configFile.c_str());
    const omk::Settings settings = omk::resolveSettings(ini, saveSettings);
    // A flag typed on the command line is the most recent word of all.
    if (!densityFlag) density = settings.v.streetActivity;
    // UNLIMITED DRAW DISTANCE (`todo/enhancements.md` 4). `--clip 0` says it
    // on the command line, `[Enhancements] clipdistance = 0` in the file, and
    // `--enhance-all` includes it. What it lifts is the visible-set walk's
    // distance test and the fog, whose range IS the clip distance - so with
    // no distance there is nothing to fade over and the fog goes off.
    const bool unlimitedClip = (clipFlag && clipArg <= 0)
                            || (!clipFlag && (enhanceAll || settings.unlimitedDrawDistance));
    const double clipInches =
        unlimitedClip ? std::numeric_limits<double>::infinity()
                      : clipFlag ? static_cast<double>(clipArg) * omk::kInchesPerMetre
                                 : settings.clipInches();
    if (unlimitedClip)
        std::printf("clip: UNLIMITED - an ENHANCEMENT the original never had "
                    "(the option's five values stop at %d m); the fog goes with it, "
                    "its range being the clip distance's own\n", omk::kMaxOptionClipMetres);
    // one line the first frame that draws, so a run says what the option did
    bool clipReport = true;
    // THE FLICKER CATCHER (--flicker <dir>). A fault a player sees for one to
    // five frames cannot be screenshotted and cannot be found by choosing a
    // frame to render: a reader reported "a Kay'l with a black background" and
    // could not say where. So the viewer watches its own output - a frame far
    // darker than its neighbours is a body drawn on a set that is not there -
    // and writes the frames AROUND the dip, with a line of context each, so
    // the event arrives on disk instead of in a description.
    //
    // It is an INSTRUMENT and no part of the port: nothing here changes a
    // pixel, and with the flag absent none of it runs.
    std::vector<std::vector<std::uint16_t>> flickRing;   // the last kFlickPre frames
    std::vector<std::string> flickNote;                  // ...and their context
    std::vector<long> flickLit;                          // the lit-count window
    std::string frameNote;                               // this frame's context
    int  flickAfter = 0;                                 // frames still to write
    long flickEvent = -1, flickQuietUntil = -1;
    constexpr int kFlickPre = 3, kFlickPost = 3, kFlickWindow = 31;
    const bool drawSky = skyFlag >= 0 ? skyFlag != 0 : settings.v.sky;
    // Options rows 5 and 7, with a flag beating the save the way --sky does.
    const bool drawShadows = shadowFlag >= 0 ? shadowFlag != 0 : settings.v.shadows;
    // The ENHANCEMENT, and it is subordinate to the option above: with row 5
    // off nothing draws whatever this says. Default 0 = what the engine draws.
    // `--enhance-all` is a BASE: an explicit flag beats it, and so does a
    // specific `[Enhancements]` key, because `resolveSettings` decided that
    // half already.
    const auto enh = [&](int flag, int fromSettings, int top) {
        return flag >= 0 ? flag : enhanceAll ? top : fromSettings;
    };
    const int shadowQuality = enh(shadowQFlag, settings.shadowQuality,
                                  omk::kMaxShadowQuality);
    const int  shadowDetail = detailFlag >= 0 ? detailFlag : settings.v.levelOfDetail;
    // Row 7. Per pixel ALSO widens who receives: the engine lights the
    // procedural crowd and nothing else, and this lets every character.
    const int  lighting = enh(lightingFlag, settings.lighting, omk::kMaxLighting);
    if (lighting > 0)
        std::printf("lighting: per pixel - an ENHANCEMENT the original never had "
                    "(it lights the crowd alone, per vertex); every character receives\n");
    // The enhancement: OFF unless --aa or [Enhancements] said otherwise.
    const int aaSamples = enh(aaFlag, settings.antiAliasing, omk::kMaxAntiAliasing);
    const int texFilter = enh(filterFlag, settings.textureFilter, omk::kMaxTextureFilter);
    const int texAniso  = enh(anisoFlag, settings.anisotropy, omk::kMaxAnisotropy);
    const int ssaa      = enh(ssaaFlag, settings.supersample, omk::kMaxSupersample);
    // The INTERFACE's own, and the one enhancement here that is not the
    // Vulkan backend's: the 640x480 layer is composed on the CPU for both, so
    // a filtered stretch reaches the software renderer too.
    const int uiScaling = enh(uiScaleFlag, settings.uiScaling, omk::kMaxUiScaling);
    comp.setScaling(uiScaling);
    if (uiScaling > 0)
        std::printf("ui scaling: linear - an ENHANCEMENT the original never had "
                    "(DirectDraw's Blt takes one texel); it changes nothing at "
                    "640x480, where nothing stretches\n");
    if (enhanceAll || settings.enhanceAll)
        std::printf("enhancements: all on - %dx MSAA, %s filtering, anisotropy %d, "
                    "%s shadows, %s lighting, %dx supersampling, %s interface, %s draw distance. As high as each goes "
                    "unless a specific "
                    "setting said otherwise; none of it is what the original drew, and "
                    "the device reduces what it cannot meet.\n",
                    aaSamples, omk::textureFilterName(texFilter), texAniso,
                    omk::shadowQualityName(shadowQuality), omk::lightingName(lighting), ssaa,
                    omk::uiScalingName(uiScaling), unlimitedClip ? "unlimited" : "capped");
    // The clip half of the line reads differently when it is unlimited:
    // "0 m = inf in" is arithmetic rather than a report.
    char clipText[128];
    if (unlimitedClip)
        std::snprintf(clipText, sizeof clipText,
                      "clip UNLIMITED (enhancement), no fog, no distance cull");
    else
        std::snprintf(clipText, sizeof clipText,
                      "clip %d m (%s) = %.0f in, near/far split %.0f/%.0f",
                      clipFlag ? clipArg : settings.v.clipDistance,
                      clipFlag ? "flag" : omk::sourceName(settings.clipDistance),
                      clipInches, clipInches * 0.25, clipInches * 0.95);
    std::printf("settings: %s;"
                " crowd %d (%s); sky %d (%s), shadows %d (%s), detail %d (%s);"
                " aa %d (%s, enhancement), filter %s (%s, enhancement),"
                " anisotropy %d (%s, enhancement), interface %s (%s, enhancement)\n",
                clipText,
                density, densityFlag ? "flag" : omk::sourceName(settings.streetActivity),
                drawSky ? 1 : 0, skyFlag >= 0 ? "flag" : omk::sourceName(settings.sky),
                settings.v.shadows ? 1 : 0, omk::sourceName(settings.shadows),
                settings.v.levelOfDetail, omk::sourceName(settings.levelOfDetail),
                aaSamples, aaFlag >= 0 ? "flag" : omk::sourceName(settings.antiAliasingSource),
                omk::textureFilterName(texFilter),
                filterFlag >= 0 ? "flag" : omk::sourceName(settings.textureFilterSource),
                texAniso, anisoFlag >= 0 ? "flag" : omk::sourceName(settings.anisotropySource),
                omk::uiScalingName(uiScaling),
                uiScaleFlag >= 0 ? "flag" : omk::sourceName(settings.uiScalingSource));
    if (!ini.unknown.empty()) {
        std::printf("settings: %zu key(s) under [Preferences] the engine never reads:",
                    ini.unknown.size());
        for (const auto& k : ini.unknown) std::printf(" %s", k.c_str());
        std::printf("\n");
    }
    if (!saveBytes.empty()) {
        const int slotNo = slotArg >= 0 ? slotArg : 0;
        const auto slot = omk::readSaveSlot(saveBytes, slotNo);
        if (!slot) { std::fprintf(stderr, "%s: not a save file, or it has no slot %d\n",
                                  saveFrom.c_str(), slotNo); return 1; }
        // AN EMPTY SLOT IS NOT A SAVE.  `SaveDir_Build` skips a slot whose
        // name begins with a zero and the load panel never offers one, so
        // loading it would put the player in area 0 out of a zeroed DB and
        // read as a fault in the port.  The distinction worth keeping is
        // between a slot that was CLEARED - `SaveDir_ClearSlot` writes one
        // byte and leaves the DB on disk - and one that was never written,
        // where there is nothing behind the name either.
        if (slot->name.empty()) {
            bool anything = false;
            for (const auto b : slot->state.raw())
                if (b != std::byte{0}) { anything = true; break; }
            if (!anything) {
                std::fprintf(stderr, "%s: slot %d is empty. Occupied slots:",
                             saveFrom.c_str(), slotNo);
                int shown = 0;
                for (int k = 0; k < static_cast<int>(omk::kSaveSlots); ++k) {
                    const auto o = omk::readSaveSlot(saveBytes, k);
                    if (!o || o->name.empty()) continue;
                    if (shown++ < 12) std::fprintf(stderr, " %d (%s)", k, o->name.c_str());
                }
                std::fprintf(stderr, shown ? "\n" : " none\n");
                return 1;
            }
            std::printf("save: slot %d's name is empty - `SaveDir_ClearSlot` "
                        "writes one byte and leaves the rest, so this slot was "
                        "DELETED and its game is still on disk. Loading it\n",
                        slotNo);
        }
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
        // `State_Apply`'s FIRST use of the header pair, and the port had only
        // ever read its second:
        //
        //     u16(u32(g_GameDB, 12), 2 * i16(g_GameDB, 1414)) = u16(g_GameDB, 1416);
        //     result = Area_Load(i16(g_GameDB, 1414), 0);
        //
        // `+12` is the scene-per-area table, so the header's scene is copied
        // INTO it for the header's area before the area is loaded - and
        // `Area_Load` reads exactly that entry at its top (`v35 = i16(u32(
        // g_GameDB, 12), 2 * a1)`) and ends `Scene_Load(slot, v35)`.  So the
        // header is what decides which SCENE goes over the area a save
        // resumes in, and the table merely carries it there.
        //
        // In all four save slots this tree has the two already agree, which
        // is why nothing has broken; they can disagree, because they have
        // different writers - the table is written by opcode 71 `scene.load`
        // and the header by `State_Save` out of the live resident slot - and
        // when they do, the engine's answer is the HEADER's.
        // `verify.py: engine: save write` measures the agreement so a
        // divergence cannot pass unnoticed.
        state.setSceneOfArea(state.currentArea(), state.currentScene());
        // WHERE THE SAVE SAYS HE WAS.  `State_Apply` converts +44..+56 back to
        // world units and stands the player there; the port had never read
        // those four fields at all, so a loaded save came up wherever the
        // harness put him.  Kept here and applied at the hand-over, because
        // that is when there is a player to place.
        state.placementWorld(savedAt, savedYaw);
        // ...and `State_Apply`'s own guard on it.  The whole spawn half -
        // the model, the bank list, `Player_SetActor` and the placement with
        // them - sits inside `if (u16(g_PlayerRecord, 272) != 0xFFFF)`, so a
        // block whose player record names no actor has a placement that means
        // nothing and the engine never applies it.  `IAM\START` is exactly
        // that block, which is why a new game stands wherever the opening
        // puts him.
        loadedName = slot->name;
        haveSavedPlacement = state.playerActorId() != -1;
        if (!haveSavedPlacement)
            std::printf("save: slot %d's player record names no actor (+272 "
                        "is 0xFFFF), so its placement is not applied - which "
                        "is `State_Apply`'s own guard\n", slotNo);
        // The row the load panel would draw for this slot, which is the
        // directory's four fields in the order the original puts them
        // (GAME_STATE 8): the character name, the date, the time - over a
        // heading that is the profile name.
        std::printf("save: the load panel's row for it is \"%s - %s - %s\" "
                    "under \"Joueur : %s\"\n",
                    state.characterName().c_str(),
                    omk::formatDate(slot->day).c_str(),
                    omk::formatTime(slot->time).c_str(), slot->name.c_str());
        std::printf("save: slot %d '%s', %s %s, area %d scene %d, standing at "
                    "%.0f %.0f %.0f facing %.0f\n", slotNo, slot->name.c_str(),
                    omk::formatDate(slot->day).c_str(), omk::formatTime(slot->time).c_str(),
                    state.currentArea(), state.currentScene(),
                    savedAt[0], savedAt[1], savedAt[2], savedYaw);
    }
    // Asking for a SLOT is asking to resume: adventure mode in the save's own
    // area, at its own placement, which is what `Game_LoadSave` does.  A bare
    // `--save FILE` keeps its old meaning - the DB as a starting state, the
    // intro still playing - because every street-start recipe in the tree is
    // written that way and pairs it with `--area`.
    // ...and a LOAD sets it too, below: loading a save is resuming, so the
    // hand-over should not wait on the scene's beats any more than `--slot`
    // does. Not const for that reason.
    bool forceAdventure = areaArg >= 0 || slotArg >= 0;
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
            // `LIST:ID` names the list, a bare `ID` means list 0. Op 49
            // `var.set.has_object`'s FIELD 0 is the list and field 1 the
            // object, and the lists are not interchangeable: the flat's lift
            // gate asks `has_object 1, 3, 20` - list ONE for the police card
            // - so a bag written only into list 0 never satisfies it, and the
            // cutscene behind that gate could not be reached at all.
            int list = 0;
            std::string idPart = cur;
            const auto colon = cur.find(':');
            if (colon != std::string::npos) {
                list = std::atoi(cur.substr(0, colon).c_str());
                idPart = cur.substr(colon + 1);
            }
            const int id = std::atoi(idPart.c_str());
            cur.clear();
            if (id <= 0) continue;
            // `debugPutObject` fills the FIRST free slot, so the ids land in
            // the order they are given - which is the reverse of what the
            // game's own `ObjectList_InsertFront` would do, and is fine for a
            // harness whose point is to have a bag at all.
            if (state.debugPutObject(list, id)) ++placed;
            else { ++refused; std::printf("--give: no free slot for object %d "
                                          "in list %d\n", id, list); }
        }
        std::printf("--give: %d object%s put in the carried list, %d refused "
                    "(a harness write, not `inventory.add`)\n",
                    placed, placed == 1 ? "" : "s", refused);
    }
    if (!varList.empty()) {
        std::string cur;
        int wrote = 0;
        for (char ch : varList + ",") {
            if (ch != ',') { cur.push_back(ch); continue; }
            const auto eq = cur.find('=');
            if (eq != std::string::npos) {
                const int id = std::atoi(cur.substr(0, eq).c_str());
                const int v  = std::atoi(cur.substr(eq + 1).c_str());
                state.setVar(id, v);
                std::printf("--var: VARIABLES[%d] = %d\n", id, v);
                ++wrote;
            }
            cur.clear();
        }
        std::printf("--var: %d variable%s written straight into the DB "
                    "(a harness write, not a script)\n", wrote,
                    wrote == 1 ? "" : "s");
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
        // THE PRECEDENCE, and the save's own placement is now in it: an
        // explicit `--stand` or `--address` is the reader's word and wins;
        // otherwise a save that was made IN THIS AREA says where he stood;
        // otherwise the area's first ADDRESSES record, which is the harness
        // default and was previously the only one.  The area test matters -
        // `--save X --area 0` deliberately drops a save's player record into
        // a city he was never in, and his apartment coordinates mean nothing
        // there.
        const bool useSaved = haveSavedPlacement && !haveStand && addressArg < 0 &&
                              state.currentArea() == startArea;
        if (useSaved) {
            session.setPlayerPosition(savedAt, savedYaw);
            std::printf("save: the player stands where the save left him, "
                        "%.0f %.0f %.0f facing %.0f\n",
                        savedAt[0], savedAt[1], savedAt[2], savedYaw);
        }
        if (!useSaved && addressArg < 0 && !rs.addresses.empty())
            addressArg = rs.addresses.front().id;
        if (addressArg < 0 && haveStand) {
            // an area with no ADDRESSES table (the Impasse airlock, 142):
            // `--stand` is the only placement there is, so it is the one
            session.setPlayerPosition(standAt, standAt[3]);
            std::printf("street start: no address in area %d - --stand places the player at "
                        "%.0f %.0f %.0f facing %.0f\n", startArea, standAt[0], standAt[1], standAt[2], standAt[3]);
        }
        if (addressArg >= 0) {
            if (session.placeActorAt(addressArg))
                std::printf("street start: the player at address %d, %.0f %.0f %.0f facing %.0f\n",
                            addressArg, session.playerPos()[0], session.playerPos()[1],
                            session.playerPos()[2], session.playerYaw());
            else
                std::printf("street start: address %d is not in area %d\n", addressArg, startArea);
        }
        // `--ride`: MOUNT him where he now stands. `MDSLIDIN`'s own gate is
        // ACTOR_STATE 6 plus a slider standing OPEN (its mode 3), and neither
        // exists here - this is the harness, and it says so.
        if (rideArg) {
            omk::SliderRide r;
            r.x = session.playerPos()[0];
            r.y = session.playerPos()[1];
            r.z = session.playerPos()[2];
            r.yaw = session.playerYaw();
            ride = r;
            std::printf("ride: mounted at %.0f %.0f %.0f facing %.0f - the "
                        "harness, NOT `MDSLIDIN` (which wants ACTOR_STATE 6 "
                        "and a slider in mode 3)\n", r.x, r.y, r.z, r.yaw);
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
    // ---- THE SNEAK CALL (screen 0, `VIDEOPHONE`) ----------------------
    //
    // The device the game shows a caller on, and the ONE screen in the game
    // that answers its own question. `Ui_OpenSneakFamily`'s param-2 arm
    // (0x0049B400) hides the tab column, installs panel 0x004DF128 and ends
    //
    //     mov  dword_4DF140, edi        ; 1
    //     mov  dword_670BF0, edi        ; 1 - "a call is up"
    //     call sub_42B560               ; UI_SendAnswer
    //
    // and `UI_SendAnswer` fires `Game_RaiseEvent(5, {ctx, dword_930750})`,
    // which is what resumes a script parked at `ui.open`. `UI_OpenScreen`
    // seeded that answer at **-1** and nothing on this screen ever writes it
    // - which is exactly why `docs/UI.md` 3d-bis lists VIDEOPHONE as one of
    // the three screens that "keep the answer with no writer". The screen
    // does not ask a question: it opens, hands -1 straight back, and STAYS
    // UP while the script runs on.
    //
    // That is the whole call idiom, and the corpus is unambiguous: **10
    // `ui.open 0` sites**, 8 of them followed immediately by `dialog.start`
    // and 2 by `media.play 534` (`ZVO P315 DATA MEMORIZED`, the caption a
    // capture shows top-right). The conversation's SPEAKER is a real actor
    // parked off-stage in the chunk - 386 `Policier Sneak/Supermarche` is
    // actor 95, the guard `ARESTO14` parks 745 units above the restaurant
    // (docs/UI.md 3d-bis) - and the script hides him afterwards with
    // `character.hide`.
    constexpr int kScreenVideophone = 0;
    omk::LoadPanel loadPanelState;   // rebuilt each time a screen opens
    int  pendingLoadSlot = -1;       // `dword_4C09B4`
    bool quitRequested = false;      // `dword_4E6C9C`, the pause screen's Oui
    // `dword_670BF0`: a sneak CALL is up. Set by the videophone's open arm,
    // cleared by the first close attempt.
    bool videophoneCall = false;
    bool videophoneSpoke = false;   // a line or a voice-over has played
    bool callHarness = false;       // `--call` opened this one
    int  callPending = -1;          // ...and the conversation it owes
    // THE LOADING SEQUENCE IS NOT WIRED HERE, and a first version of it
    // was. `Charger` answers **0**, and AREA 118's parked startup script
    // has an arm for exactly that: the Grid fly-through - cameras
    // 2152/2153/2154/2158 over `scx.play 20` with a `media.play 753` - after
    // which the script ends. So the engine covers a load with its own
    // script, and the port only has to answer the right number.
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
    // The input word as it stood when the current screen opened - see the
    // edge gate below. Cleared once those bits are released.
    std::uint32_t screenOpenBits = 0;
    // A screen the PLAYER asked for this frame, before it is opened below -
    // so the open, its sounds and its bookkeeping stay in one place.
    int playerScreen = -1;
    // The world's action button is spent until it is RELEASED - see the
    // one-activation-per-press gate below. Cleared where `bits` is taken, so
    // a frame that never reaches the gate cannot leave it stale.
    bool actionSpent = false;
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
                            float gain = 1.0f, const std::vector<float>* pcm = nullptr) {
        const double secs = samples / 44100.0 / 2.0;
        // the PEAK of what is fed, because a reader heard the effects "very
        // low" against the music: the number says whether the source or the
        // mix is quiet
        float peak = 0.0f;
        if (pcm) for (const float v : *pcm) peak = std::max(peak, std::fabs(v));
        if (secs >= 0.75 || pcm)
            std::printf("audio: %-14s %6.2f s  (%d, %d)  gain %.2f  peak %.3f\n",
                        what, secs, a, b, gain, static_cast<double>(peak));
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
    // ...and the same machinery serves any PLAYER-subject preset asked for
    // with a travel: `Camera_Request(mode, block)` with `block+24` the
    // frames. Preset 17 is what `sub_4570F0` asks for when the ride ends -
    // eye (-39.3701, 78.7402, 0) = 1.00 m and 2.00 m, target the actor,
    // subjects 0 and 0, over `dword_930818 = 60.0` frames. The take's
    // preset 1 was the only one wired, as constants.
    float takeCamEye[3] = {kTakeCamEye[0], kTakeCamEye[1], kTakeCamEye[2]};
    float takeCamAt[3]  = {kTakeCamAt[0], kTakeCamAt[1], kTakeCamAt[2]};
    float takeCamFov    = kTakeCamFov;
    float takeCamTravel = kTakeCamTravel;
    auto takeCamRequest = [&](int phase) {
        takeCamPhase = phase;
        takeCamClock = 0.0f;
        for (int k = 0; k < 3; ++k) { takeCamFromEye[k] = lastEye[k]; takeCamFromAt[k] = lastAt[k]; }
        takeCamFromFov = lastFov;
        if (phase == 1) {                       // the take: preset 1, 30 frames
            for (int k = 0; k < 3; ++k) { takeCamEye[k] = kTakeCamEye[k]; takeCamAt[k] = kTakeCamAt[k]; }
            takeCamFov = kTakeCamFov; takeCamTravel = kTakeCamTravel;
        }
    };
    auto playerCamRequest = [&](const float eye[3], const float at[3], float fov, float frames) {
        takeCamRequest(1);
        for (int k = 0; k < 3; ++k) { takeCamEye[k] = eye[k]; takeCamAt[k] = at[k]; }
        takeCamFov = fov; takeCamTravel = frames;
        takeCam = true;
    };
    float lastRoll = 0.0f;              // the camera ROLL, blended like the fov
    // THE CAMERA HOLDS WHEN AN EDITING ENDS, and the fall-back this used to do
    // is a PREFERENCE that ships off. `Game_Frame` (05_sys.c 2144) requests
    // mode 0 on the player only under
    //
    //     else if (Camera_GetMode(C) == 13 && byte_910322 && g_PlayerActorRec)
    //
    // and `byte_910322` is the `[Preferences]` key `autocameraplayer`, read
    // with a default of "0" (Runtime.exe.asm:23252; the only other write in
    // the image is the defaults block that zeroes it, so nothing else can turn
    // it on). With it off the branch never runs, and the camera tick's own
    // mode-13 arm is `if (dword_9103D4) { copy eye/at/fov/roll }` (sub_417CF0,
    // 04_sys.c 3701): a null active camera copies NOTHING, so the block keeps
    // the values it had. The view therefore FREEZES on the editing's last
    // frame until something else requests a camera.
    //
    // Measured before this landed, on the intro path: the Impasse's eight
    // gaps were 0 of 480000 pixels lit - a black frame after every beat and a
    // 73-frame black stretch mid-cutscene - because the Session still held
    // camera 2158, AREA 118's, from the area the player had just left
    // (next-tasks 5).
    bool  musicPaused = false;          // the pause screen suspends the sound
    bool  holdEditCam = false;          // mode 13 with no active camera: hold
    int   heldUnderCamera = -1;         // the Session camera the hold began under
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
    // `Player_GoToMove` behind `player.move` (63) and `player.move.wait` (89)
    // - `PlayerController::goToMove`. Neither reached the walker until
    // 2026-09-08: op 63 was recorded and dropped, and 89 ran on because no
    // hook was installed, so the `player.move 100` in front of every staged
    // sequence never stopped him and he played his walk out under the
    // cutscene camera. In AREA 222's tutorial that is 19 units short of the
    // zone he had just been teleported out of.
    //
    // `moveWaitCtx` / `moveWaitGroup` are `dword_930744` / `dword_91068C`:
    // the context a 89 must report to, and the group it entered. `Game_Tick`
    // (0x004200F0) checks them after `Actors_TickAll` and raises event 3 the
    // frame the channel's current entry is in some other group - the release
    // below, before the next pump.
    int moveWaitCtx = -1, moveWaitGroup = -1;
    session.setMoveHook([&](int groupId, int ctx) -> bool {
        if (!player) return false;
        if (!player->goToMove(groupId)) {
            std::printf("frame %ld: player.move%s %d - no group %d in the player's bank, "
                        "the script runs on\n", session.frameNo(),
                        ctx >= 0 ? ".wait" : "", groupId, groupId);
            return false;
        }
        const int g = player->ctlGroup();
        const int st = player->ctlState();
        const char* nm = (st >= 0 && st < static_cast<int>(playerCtl.states.size()))
                             ? playerCtl.states[static_cast<std::size_t>(st)].name.c_str()
                             : "?";
        if (ctx >= 0) { moveWaitCtx = ctx; moveWaitGroup = g; }
        std::printf("frame %ld: player.move%s %d - the channel put on group %d (index %d)'s "
                    "entry '%s' at %.1f %.1f %.1f%s\n", session.frameNo(),
                    ctx >= 0 ? ".wait" : "", groupId, groupId, g, nm,
                    player->pos()[0], player->pos()[1], player->pos()[2],
                    ctx >= 0 ? ", the script parked until the channel leaves it" : "");
        return true;
    });
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
    // A `scx.play.player` program owns his body right now (op 46/90).
    bool  playerProgram = false;
    bool  playerProgramWas = false;   // ...and did last frame, for the hand-back
    bool  mirrorLive = false;         // the set's mirror is reflecting, said once
    bool  mirrorSeen = false;         // ...and has actually covered a pixel
    float playerHeadAt[3] = {0, 0, 0};   // the player's `Tete`, for subject kinds 0/1
    bool  playerHeadKnown = false;
    std::vector<float> playerMeshAt;     // ...and every mesh, for the shadow
    bool  playerMeshAtKnown = false;
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
    // ...AND THE PELVIS TRANS THE ANCHOR WAS LATCHED AT, which is the whole of
    // `todo/player-vertical.md` step 1. `playerFeet` is a POSE's lowest corner
    // and every pose carries its own pelvis translation, so an anchor latched
    // without recording that translation has no shared origin with the drop
    // measured below - see the long note at the latch.
    float playerRootRef = 0.0f;
    bool  playerStandLatched = false;   // the anchor came from H_STAND, not from whatever was up
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
    omk::Renderer* worldVk = nullptr;   // --world-vulkan, the offscreen harness
#if defined(OMK_VULKAN)
    if (!forceSoftware && !worldVulkan) {
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
            if (aaSamples > 1) vr->setMultisample(aaSamples);   // the enhancements
            if (texFilter > 0) vr->setTextureFilter(texFilter);
            if (texAniso > 1) vr->setAnisotropy(texAniso);
            if (ssaa > 1) vr->setSupersample(ssaa);
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
#if defined(OMK_VULKAN)
    // ...and the harness: a Vulkan renderer with no surface, for the WORLD
    // alone. `run_vulkan` and `shadow_probe` already prove the backend comes
    // up offscreen; this is the same thing inside the viewer.
    if (!vkRen && worldVulkan) {
        omk::Renderer* wv = omk::makeVulkanRenderer();
        if (wv && aaSamples > 1) wv->setMultisample(aaSamples);
        if (wv && texFilter > 0) wv->setTextureFilter(texFilter);
        if (wv && texAniso > 1) wv->setAnisotropy(texAniso);
        if (wv && ssaa > 1) wv->setSupersample(ssaa);
        if (wv && wv->init(dispW, dispH)) {
            worldVk = wv;
            std::printf("renderer: the world through VULKAN offscreen - %s "
                        "(a harness; the frame is still presented on the CPU)\n",
                        omk::vulkanDeviceName(wv));
        } else { delete wv; std::printf("--world-vulkan: no offscreen device\n"); }
    }
#endif
    if (aaSamples > 1)
        std::printf("aa: %dx MSAA - an ENHANCEMENT the original never had; %s\n", aaSamples,
                    vkRen ? "drawn by the Vulkan backend"
                          : "the software reference has none, --vulkan for it");
    if (texFilter > 0)
        std::printf("filter: %s%s - an ENHANCEMENT the original never had; %s\n",
                    omk::textureFilterName(texFilter), texAniso > 1 ? " with anisotropy" : "",
                    vkRen ? "drawn by the Vulkan backend"
                          : "the software reference has none, --vulkan for it");
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
    // Whether the player's actor is in DIALOGUE MODE, so the two sides of
    // `Actor_EnterDialogueMode` / `Actor_LeaveDialogueMode` are called once
    // each per conversation. See the transition below.
    bool  dialogMode = false;
    bool  shootMode  = false;      // ops 80/81, `actor/shootmode.h`

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
        float localOff[3] = {0, 0, 0};   // the root mesh's +128: where it sits under a parent
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
        std::vector<float> poseWas;    // OMK_BODYLOG: last frame's posed corners
        // The yaw the body was last DRAWN with, kept so a held pose is held
        // whole: a scene clip's pose already carries the clip's root rotation,
        // so the world heading must not be applied over it a second time.
        bool  lastYawKnown = false;
        float lastBodyYaw = 0.0f, lastDrawnYaw = 0.0f;
        bool  lastAboutPelvis = true;
        // The placement a running scene PROGRAM gives him (a path sample or
        // the clip's root key 0), cached so it can be re-asserted every
        // frame - a `fromTable` body's per-frame reset to its 20-byte record
        // would otherwise win on every frame but the clip-change one, and he
        // would stand at the placement plus the whole root motion.
        bool  progPlaced = false;      // a program placed him this clip
        // ...and whether one EVER did. The two are not the same question:
        // "the program ended, so he stays where it left him" is only true of
        // a body a program actually moved. An actor no program ever named -
        // Gandhar's cave stages twelve of them, `shoot.actor.enter` bodies
        // driven by the shoot AI - has only his placement record, and
        // carrying `drawAt` over on his FIRST frame reads it before anything
        // wrote it and teleports him to the world origin.
        bool  progRan = false;         // a program placed him at some point
        float progYaw = 0.0f;          // the call's Euler y (`Actor_SetEuler(node, p4, p5, p6)` every tick)
        bool  progYawKnown = false;
        float progBase[3] = {0, 0, 0};
        bool  progPelvis = false;
        const omk::SceneRunner* poolWas = nullptr;   // which pool last drove him
        const char* src = "none";
        omk::HeadLook look;            // `character.look_at_player`'s head aim, eased
        bool  lookSnap = true;
        bool  shootTold = false;
        bool  brainTold = false;
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
        // THE YAW THE BODY WAS DRAWN WITH - the node's world heading, which is
        // what subject kind 2 (`sub_4151E0`) reads back for a camera that
        // hangs off him: `atan2` of the node matrix's forward, +90.
        float drawnYaw = 0.0f;
        bool  drawnYawKnown = false;
        // WHERE THE HEAD WAS DRAWN. Subject kinds 1 and 3 anchor a camera on
        // the actor's `Tete` node (`actor+16`, cached by `Actor_LoadModel`),
        // not on the body - 176 of the 253 relative cameras in `IAM\DIALOG`
        // ask for one of the two. Recorded as the body is posed and read a
        // frame later by the camera block, which runs earlier in the frame.
        float headAt[3] = {0, 0, 0};
        // WHICH SKELETON was posed - a crowd model has four, side by side,
        // and a bone name matches in all of them (see `o3de/shadow.h`).
        int   shadowRoot = -1;
        // EVERY MESH'S WORLD POSITION, filled by the same transform that
        // places `headAt` - what the shadow needs, because
        // `Shadow_EmitBoneBlob` probes from the BONE NODE'S OWN ORIGIN
        // (`node+44/48/52`) rather than from anything in the drawn corners.
        // Three floats a mesh, empty when the body was not placed this frame.
        std::vector<float> meshAt;
        bool  headKnown = false;
        // `Morph_Play` reads the node's world heading ONCE, when the line
        // starts, and `sub_42BE00` hands the morph that yaw for its whole
        // length - so the line's yaw is a LATCH, not a per-frame read.
        float lineYaw = 0.0f;
        bool  lineYawLatched = false;
        // ...and the heading a scene clip left the node with, kept after the
        // program ends: nothing resets the Euler at +416 or the last frame
        // `Anim_ApplyNodeFrame` wrote, so a body a program turned stays
        // turned - the facing half of the rule the position already obeys.
        float restYaw = 0.0f;
        bool  restYawKnown = false;
        // The line's .3DM with its root already turned by the clip's heading
        // - `Morph_Play`'s `heading`, composed the way `08_wave.c` does it -
        // so the fade blends two poses in ONE frame. Blending the raw morph
        // against a clip whose root carries the yaw, then adding the yaw on
        // top, double-turned the clip end and faded to single: she started
        // every line turned away and slowly came round to the lens.
        omk::NodeTracks lineTracks;
        float lineRootYaw = 0.0f;
        std::string lineVoice;         // whose line `lineTracks` was taken from
        // A body nothing drives keeps the pose it was last given. The engine
        // never resets a node - the last frame `Anim_ApplyNodeFrame` wrote
        // stays - so when a program and a line are both over and the model
        // has no bank, this is what stands there; the REST pose was drawn
        // instead, a T-pose in software and nothing at all in the Vulkan
        // window (a reader: *Telis appears normally at the beginning before
        // disappearing*).
        std::vector<omk::MeshPose> lastPose;
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
        // `Piedg` and `Piedd` in world space - `Slider_PlaceShadow`'s two
        // nodes, bound into the pedestrian record at spawn by
        // `sub_41E210(aPiedg, model, &ped[16])`.
        float footAt[2][3] = {};
        bool  footKnown = false;
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
    // how many (walker, light) pairs actually reached this frame
    int pedLit = 0;
    long pedTold = -1;
    // THE SHOOT-MODE POSE. These characters carry no `.CTL` in any of the
    // three 9-byte slots, so nothing the actor runtime does can pose them:
    // `Shoot_ActorEnter` resolves their CHARACTER TYPE's group in the area's
    // `.ani` (`sub_434530`) and `Shoot_ActorAction` asks it for a clip by
    // BEHAVIOUR type through `List_PickRandomByType`:
    //
    //     action 0, 5        type 11, else 25
    //     action 1           type 9
    //     action 2,3,4,6,7   type 10, else 9
    //
    // WHAT RUNS HERE IS THE SCRIPT'S LAST ACTION, NOT THE AI, and that is a
    // decision rather than an omission. `Shoot_TickNpc` calls one of four
    // brains every frame; `actor/shoot.h` models them and is deliberately not
    // wired to this, because the generic arm - 302 of the 306 shipped sites -
    // "takes the first edge and RECORDS the choice rather than pretending to
    // compute it": the real branch needs the navigation node, the line of
    // sight and the weapon's range, none of which this tree has. Running it
    // here would draw a deterministic first-edge walk as if it were the
    // game's behaviour, which is putting a guess where a fact belongs.
    //
    // THE PICK, though, is a fact and is now faithful. `List_PickRandomByType`
    // returns a RANDOM one of the matches, and 40 of the 195 (library, group,
    // behaviour type) buckets in the shipped `.ani` hold more than one clip -
    // up to six - so the choice is real in a fifth of them. The roll is seeded
    // per (actor, action) rather than re-rolled every frame: the engine rolls
    // once per `Shoot_ActorAction` and this has no AI asking again, so a
    // per-request seed is the same shape and keeps a still frame
    // reproducible. LABELLED as that.
    std::map<int, std::vector<omk::PedClip>> shootClips;      // character type -> its group
    const auto shootClipFor = [&](int group, int action) -> const omk::PedClip* {
        if (pedAni.empty()) return nullptr;
        auto it = shootClips.find(group);
        if (it == shootClips.end())
            it = shootClips.emplace(group, omk::animGroupClips(pedAni, group)).first;
        if (it->second.empty()) return nullptr;
        int want[2] = {9, -1};
        if (action == 0 || action == 5)      { want[0] = 11; want[1] = 25; }
        else if (action == 1)                { want[0] = 9;  want[1] = -1; }
        else if (action >= 2 && action <= 7) { want[0] = 10; want[1] = 9;  }
        for (int w : want) {
            if (w < 0) continue;
            std::vector<const omk::PedClip*> m;
            for (const auto& c : it->second) if (c.type == w) m.push_back(&c);
            if (m.empty()) continue;
            // the roll: a cheap LCG on (group, action), so two characters of
            // one type asking for one action can still draw different clips
            std::uint32_t r = static_cast<std::uint32_t>(group * 2654435761u
                                                         + action * 40503u + w);
            r ^= r >> 15; r *= 2246822519u; r ^= r >> 13;
            return m[r % m.size()];
        }
        return &it->second.front();          // "anim non existante dans le .ANI"
    };
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
            // THE BONE IS THE NAME AFTER ITS PREFIX, AND THE PREFIX IS NOT
            // TWO LETTERS - it is whatever the library and the model each
            // chose. `braqueur.ani`'s tracks are `UBassin`, `UCuissed`,
            // `UPiedd`; VIR_FN's meshes are `ViBassin`, `ViCuissed`,
            // `ViPiedd`. Stripping a fixed 2 from both gives "assin" against
            // "bassin" and NOT ONE of the nineteen tracks bound, which is why
            // the Shooting gallery's gunmen stood in their rest pose
            // (`todo/omk-play.md` 96). The crowd's four libraries all happen
            // to use two-letter prefixes (`Ph`, `Sh`, `Kh`, `Fh`), so the
            // fixed strip was right everywhere it had been looked at.
            //
            // The ENGINE does not strip at all: `o3de_FindMeshByName`
            // (0x00436D90) is `o3de_Traverse` running a `strstr` over the
            // node names and keeping the LAST match, and its callers pass the
            // BARE bone - `Bassin`, `Tete`, `Buste`, `Cuisseg`, `Piedd`
            // (04_sys.c 5497-5513, seventeen of them in a row). So the bone
            // name is a substring and the prefix's length never enters into
            // it. The same `strstr`-on-the-last-match is what finds the
            // shadow bones (`docs/ASSETS.md`).
            //
            // Reproduced here without hard-coding the seventeen: every prefix
            // in the corpus is a capitalised letter followed by lower case,
            // and every bone starts with a capital, so THE BONE BEGINS AT THE
            // SECOND UPPERCASE LETTER. `verify.py: bone names` measures that
            // over every shipped library and character model rather than
            // taking it on trust.
            const auto suffix = [&](const std::string& n) {
                for (std::size_t i = 1; i < n.size(); ++i)
                    if (n[i] >= 'A' && n[i] <= 'Z') return lower(n.substr(i));
                return n.size() > 2 ? lower(n.substr(2)) : lower(n);
            };
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
    // one shoot record per gunman, built from his own properties the first
    // frame he is staged and kept for the run (`todo/shoot-mode.md` 7d)
    std::map<int, omk::ShootRecord> shootBrains;
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
    // A DIAGNOSTIC for the sign of the scene call's Euler: CLAUDE.md 5's
    // rule is that leaving the game's space reflects one axis and so
    // reverses the sense of every rotation about it.
    const float progYawSign = std::getenv("OMK_PROGYAW_NEG") ? -1.0f : 1.0f;
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
                    for (int k = 0; k < 3; ++k)
                        m.localOff[k] = ms[static_cast<std::size_t>(root)].local[k];
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
    // THE GLOBAL LIBRARY IS THE EFFECTS SCENE. `Game_Start` loads
    // `SCPTDATA\aventure.scx` into `stru_930780` (04_sys.c 4425), and
    // `Effects_SetScene(.., &stru_930780)` (05_sys.c 2177/2208) makes THAT the
    // scene `Cef_TickEffects` resolves a `.CTL` record's sound id in through
    // `Scene_FindSoundIndex`, and `Cef_SpawnEffect` its sprite id through
    // `sub_4A5800`. Not the resident scene: the Impasse's 20 sounds have no
    // 194 (the grab, POBJ01.WAV), 199/203 (the footsteps STPR/STPL) or 180,
    // and its 187 is a DEMON footstep where the library's 187 is SNEAKIN.WAV.
    // Searching the resident scene - what this did until 2026-09-05 - made
    // the grab silent and played a demon's step on the confirm.
    std::unique_ptr<omk::ScxRuntime> globalRt;
    if (const auto gp = fs.resolve("SCPTDATA/aventure.SCX")) {
        globalRt = std::make_unique<omk::ScxRuntime>(omk::DataFs::readPath(*gp));
        if (!globalRt->valid()) globalRt.reset();
    }
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
    omk::Renderer& world = vkRen ? *vkRen
                         : worldVk ? *worldVk
                                   : static_cast<omk::Renderer&>(worldSw);
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
        // THE SET'S LIGHTS (`todo/mesh-lights.md`). A decor `.3DO` supplies
        // them and the street's moving population receives them - the static
        // set is shaded by a colour baked into every vertex and needs none.
        std::vector<omk::Light3do> lights;
        std::vector<omk::Corner> baseCorners;
        // THE VISIBLE-SET WALK's unit of work. `sub_48D3B0` walks the scene
        // MESH BY MESH and submits each one that passes; this port flattens a
        // set into batches by material, so a mesh's corners are runs inside
        // them. One run is a maximal stretch of consecutive corners in one
        // batch that all belong to one mesh - computed once at the load, so a
        // frame tests each mesh once (a few thousand) instead of each corner
        // (1.7 million).
        struct MeshRun { std::uint32_t start, count; std::int32_t mesh; std::size_t batch; };
        std::vector<MeshRun> runs;
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

    // ------------------------------------------------------------- THE SKY
    //
    // `Area_TickLoad` case 4 hands the AREA chunk's `+133` to
    // `Area_LoadMiscModel` (0x0041D2C0), which loads `MESHES\MISC\<name>.3DO`,
    // takes its node 0, scales it 12.5x on all three axes and lifts it 2250
    // units (Y is DOWN, so `-2250.0` is UP). Every frame, while options row 4
    // is on, `sub_41CF10` sets the node to the CAMERA's x and z and its OWN y
    // and relinks it under the scene root - so it follows you horizontally and
    // never gets closer.
    //
    // It is not a dome. All 864 corners sit at Y = 401.09: a flat 12x12 quad
    // grid, 5310 x 5164 units, carrying one 256x256 texture whose name in the
    // file is `ciel2` - French for sky - over a mesh called `face`, or
    // `TOITCIEL` in the roof set. So Omikron's sky is a painted CEILING, which
    // is what a domed city has.
    struct Sky {
        std::string stem;
        omk::Geometry geo;                 // the plane, rebuilt each frame
        std::vector<omk::Corner> base;     // ...from these, as loaded
        std::vector<omk::Texture> tex;
        std::size_t texBase = 0;
        float origin[3] = {0, 0, 0};       // node 0's own position
    };
    Sky sky;
    // ---------------------------------------------------- THE SHADOWS
    //
    // Option row 5 *Affichage des ombres*. `sub_419060` loads
    // `MESHES\MISC\shadows.3DO` once at game start, `Actor_LoadModel` clones
    // it under every character, and `Actors_TickAll` emits blobs under a
    // fixed set of BONES each frame - see `o3de/shadow.h` for the whole
    // mechanism. Loaded once here for the same reason the engine loads it
    // once: it is not a set's asset and no area change touches it.
    const omk::ShadowModel shadowModel = omk::loadShadowModel(fs);
    // Rebuilt every frame into this one object, outside the loop so the
    // revision accumulates - the same rule `fxGeo` above states, and for the
    // same backend-side reason.
    omk::Geometry shadowGeo;
    std::size_t shadowTexBase = 0;
    // The worst distance from a walker's foot-pair midpoint to his own body
    // point this frame - the orphan-shadow detector (see the ped loop).
    float shadowFootOffMax = 0.0f, pedFootOffMax = 0.0f;
    // The worst vertical spread inside ONE blob - 0 for every classic blob by
    // construction, nonzero for a fitted one wherever the ground is not flat.
    float shadowSpreadMax = 0.0f, shadowSpreadPlayer = 0.0f;
    long shadowBlobsDrawn = 0;
    bool shadowTold = false, shadowLightTold = false, lightsTold = false;
    float shimmerClock = 0.0f;   // `dword_907310`, wrapped at 256
    // `Area_LoadMiscModel`'s two constants.
    constexpr float kSkyScale = 12.5f;
    constexpr float kSkyLift  = 2250.0f;
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
        if (const auto mh = omk::readHeader(d)) w.lights = omk::readLights(d, *mh);
        // The runs, in submission order: batch by batch, and inside a batch
        // split wherever `cornerMesh` changes. A set whose corners carry no
        // mesh index leaves this empty, and the draw path then submits whole
        // batches exactly as it did before.
        w.runs.clear();
        if (w.geo.cornerMesh.size() == w.geo.corners.size()) {
            for (std::size_t bi = 0; bi < w.geo.batches.size(); ++bi) {
                const auto& b = w.geo.batches[bi];
                std::uint32_t c = 0;
                while (c < b.count) {
                    const std::size_t at = static_cast<std::size_t>(b.start) + c;
                    const std::int32_t mi = w.geo.cornerMesh[at];
                    std::uint32_t n = 0;
                    while (c + n < b.count &&
                           w.geo.cornerMesh[static_cast<std::size_t>(b.start) + c + n] == mi) ++n;
                    w.runs.push_back({static_cast<std::uint32_t>(b.start + c), n, mi, bi});
                    c += n;
                }
            }
        }
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
            if (!vkRen && !worldVk) worldSw.init(dispW, dispH);
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

    std::printf("screen %d. arrows move, ENTER confirms, TAB closes, "
                "ESC opens the pause screen.\n", screenId);

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
    Uint32 lastMs = SDL_GetTicks();
    Uint32 fpsSince = lastMs, fpsLastMs = lastMs, fpsWorst = 0;
    int    fpsFrames = 0;
    // Where `Script_Display3DSprite` puts a sprite: the active camera's
    // TARGET, read by the handler on every tick it runs (program.h has the
    // trace; the XYZ table it would prefer is never written). The runner is
    // handed the camera the LAST frame drew with - one frame behind, since
    // the script ticks before this frame's camera is settled.
    float spriteAnchor[3] = {0.0f, 0.0f, 0.0f};
    bool  spriteAnchorSet = false;
    std::map<int, long> spriteLogged;     // row -> the link tick already reported
    std::set<std::string> motionLogged;   // mesh/pool pairs already reported
    for (;;) {
        const Uint32 frameStartMs = SDL_GetTicks();
        if (!front.pump(host)) break;
        // ---- ESC OPENS THE PAUSE SCREEN (next-tasks 3) -------------------
        //
        // It is not a binding, and `Game_Frame` never sees it. `Game_RunLoop`
        // (0x00439310) polls it itself, one line before the frame:
        //
        //     if ((((uint16_t)GetAsyncKeyState(27) >> 8) & 0x80) != 0
        //         && !dword_4E9728)
        //         UI_LoadScreen(31, -1, -1);
        //     Game_Frame(dword_4C5944, word_90EF2E);
        //
        // Three things follow, and all three are the engine's:
        //
        //  * VK_ESCAPE is read LEVEL-triggered - `GetAsyncKeyState`'s bit 15
        //    is "currently down", not an edge. Nothing debounces it except
        //    the guard;
        //  * the guard is `dword_4E9728`, the PAUSE FLAG, and it has exactly
        //    two writes in the image: screen 31's open callback (0x004ADDB0)
        //    sets it, its close (0x004ADEB0) clears it. So ESC held opens the
        //    screen once and cannot open it twice, and ESC held THROUGH the
        //    close reopens it at once - which is the same shape as the held
        //    action button of next-tasks 1, and is what the original does;
        //  * it is `UI_LoadScreen`, not `UI_OpenScreen`, so no answer
        //    variable is written and no script is parked on it. The port
        //    therefore asks for it the way the PLAYER asks for the sneak,
        //    not the way a script asks.
        //
        // AND `UI_LoadScreen` REFUSES IT OVER TWO SCREENS BY NAME. Its slot
        // scan opens
        //
        //     if (slot->screen == a1) return 1;              // already up
        //     ...
        //     if (v5 || a1 == 31 && UI_TestScreenFlag(slot, 0x20000400))
        //         return 1;                                  // refused
        //
        // and 0x20000400 is set on exactly two screens, `OMK START MENU` and
        // `SAVE GAME` (`tables/ui.json`, flags 0x20000400). So ESC at the
        // start menu or over the save panel does NOTHING - which is the
        // guard this port needs most, since the boot parks on screen 29.
        // (`docs/UI.md` 2 read that flag the other way round - "opening
        // either fires screen 31's open callback" - and the branch says the
        // opposite: it is what makes 31 decline. Corrected there.)
        //
        // ONE DEPARTURE, and it is this frontend's rather than a reading:
        // the engine has THREE slots and would put the pause screen up over
        // a screen that does not carry that flag - the sneak, say - while
        // `omk-play` holds one `walk`. So any screen already up refuses the
        // pause here, which is the engine's answer for two screens and a
        // limitation of this frontend for the rest. Nothing here models the
        // three slots.
        //
        // This used to `break`, ending the run: the viewer quit on the one
        // key everybody presses, so a reader could not stop to look at
        // anything. Quitting is now what the screen's own `Quitter le jeu`
        // does, behind its Oui/Non confirm, exactly as in the game.
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
        // ...and a CONVERSATION, which is neither `adventure` nor `walk` and
        // so used to swallow the whole stream: every scripted press after the
        // one that opened a dialogue was dropped, and a headless run could
        // never reach the end of one. `--hold` is a harness, and a harness
        // that cannot press ENTER through a conversation cannot test what
        // happens when the conversation ends.
        if ((adventure || walk || session.dialogOpen()) && !holds.empty()) {
            for (int k : holds.front().keys) st.keyboard.push_back(k);
            if (--holds.front().frames <= 0) holds.erase(holds.begin());
        }
        if (!scripted.empty() && (n % keyEvery) == 0) {
            const int k = scripted.front();
            scripted.erase(scripted.begin());
            if (k == kTypeMarker) host.text += typeText;   // as if it were typed
            else if (k <= kCharMarker)                    // `cN`: one character
                host.text += static_cast<char>(kCharMarker - k);
            else st.keyboard.push_back(k);
        }
        // ...and ESC is read off THAT, once the scripted streams have been
        // merged in, rather than off `host.held`. The engine reads it from
        // Windows (`GetAsyncKeyState`) and not through DirectInput, so
        // either is faithful to "is the key down"; taking it here is what
        // lets `--keys`/`--hold` drive it, and a screen the harness cannot
        // open is a screen nothing can check.
        const bool esc = std::find(st.keyboard.begin(), st.keyboard.end(), 0x01)
                         != st.keyboard.end();
        if (esc && !walk && playerScreen < 0) {
            playerScreen = kScreenPause;
            std::printf("frame %ld: ESC -> screen %d PAUSE GAME "
                        "(`Game_RunLoop`'s own GetAsyncKeyState(27), guarded "
                        "by the pause flag)\n", n, kScreenPause);
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
        std::uint32_t bits = in.frame(st);
        if (boardPress) { bits |= 0x10u; boardPress = false; }   // `--board`, one press
        // The EDGES, taken here rather than at each consumer so a frame that
        // never reaches one - a dialogue, a cutscene, a screen - cannot leave
        // the latch stale and manufacture a press on the way back. This is
        // `Game_Frame`'s `dword_4E971C` with every bit masked: the engine's
        // own `held & (held ^ (mask & last))` at mask = all ones.
        const std::uint32_t edgeBits = bits & ~prevBits;
        prevBits = bits;
        // ...and the world's action, which is not an edge: it is HELD, and
        // stays spent until the bit goes up. `kUiConfirm` is 0x10, the same
        // bit `H1AVNT` entry 24 (MDACTION) matches on.
        if (!(bits & omk::kUiConfirm)) actionSpent = false;
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
        // ---- THE PAUSE FLAG, and it is a DELTA and nothing else ----------
        //
        // `dword_4E9728` has two writes in the image - screen 31's open
        // callback sets it, its close clears it - and what it does is force
        // the frame delta to 0.0. That is why `Slider_TickRide` sits behind
        // `flt_4C30D8 != 0.0` two lines down in `Game_Tick`, and why
        // `Game_Tick` needs no test for an open screen anywhere in it
        // (docs/UI.md 2a: it has none - that reading was corrected once
        // already, when the sneak froze the city).
        //
        // So the pause is not "skip the tick": every subsystem still runs,
        // on a delta of zero. Modelled here for the same reason - the port
        // used to express it as `adventure = false`, which stops the PLAYER
        // and leaves the crowd walking behind the menu.
        const bool uiPause = walk && openScreen == kScreenPause;
        if (uiPause) { frameSec = 0.0; session.setFrameSeconds(0.0); }

        // ---- one frame of the GAME -------------------------------------
        //
        // The script runs unless a screen is up. That is the engine: a script
        // parked at `ui.open` is waiting on a person, and `Game_HandleEvent`
        // case 5 is the only thing that releases it.
        if (!scxPlayed && !scxPlay.empty() && adventure && session.scene().loaded()) {
            scxPlayed = true;
            for (const int h : scxPlay) {
                omk::Call c;
                c.op = 58;
                c.fields = {static_cast<std::int16_t>(h), 0, 0};
                const int idx = session.sceneMutable().handle({c});
                std::printf("--scx-play: frame %ld  object handle %d -> program %d (a harness start)\n",
                            frames, h, idx);
            }
        }
        if (spriteAnchorSet) session.sceneMutable().setSpriteAnchor(spriteAnchor);
        // ---- AND THE SESSION RUNS UNDER A SCREEN, which it did not ------
        //
        // `Game_Tick` (0x004200F0) has NO test for an open screen anywhere in
        // it - `Script_SetFrameTime`, the per-slot `Script_PlayAllScripts`
        // loop, `Projectiles_Tick`, `Sliders_Tick` and `Slider_TickRide` all
        // run whatever is on screen. The only thing that stops the world is
        // the pause flag, and that is a DELTA of zero (docs/UI.md 2a), which
        // is already applied above.
        //
        // This is the THIRD time that assumption has had to come out. It was
        // corrected once for the world DRAW (a player opened the sneak and
        // watched Anekbah freeze, 19 frames in 1924), once for the player's
        // own tick, and this is the line that still held it for the SCRIPTS.
        // The sneak call is what needs it: `ui.open 0` answers itself and the
        // script's very next instruction is `dialog.start`, so a session that
        // stops while the screen is up can never play the call.
        //
        // A script parked at `ui.open` does not run either way - its status
        // is 6 and only `answerUi` clears it - so what this releases is
        // everything ELSE: the scene programs, the crowd, the other slots.
        //
        // ...and before it, `Game_Tick`'s release of a `player.move.wait`:
        // the channel's current group against the one the move entered.
        if (moveWaitCtx >= 0 && player && player->ctlGroup() != moveWaitGroup) {
            std::printf("frame %ld: player.move.wait ended - the channel left group %d "
                        "for %d, context %d resumes\n", n, moveWaitGroup,
                        player->ctlGroup(), moveWaitCtx);
            session.playerMoveEnded(moveWaitCtx);
            moveWaitCtx = -1; moveWaitGroup = -1;
        }
        session.frame();

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
        // ...AND ITS SCALE. `Script_ScaleObjectX/Y/Z` (program.h) writes the
        // node's `+128..+136`, and `sub_494E80` multiplies each row of the
        // node's 3x3 by its axis's scale before the vertices go through it:
        // a scale in the node's LOCAL axes, about its origin, ahead of the
        // rotation a motion may have set. So a corner is (base - origin),
        // scaled per axis, rotated, placed. The apartment's transfer tube is
        // every shipped site: its beam grows along Y from 1 to 35.
        // BOTH POOLS. A transition's door resolves in the OUTGOING pool
        // (`engine: tunnel doors`: 93% of the corpus's door objects live
        // there), and its program runs in `sceneOut()` - so a motion read
        // from the active pool alone never moved the tunnel's doors, drawn
        // or collided. The meshes are matched by name across both slots.
        // WHERE EACH MOVED MESH ENDED UP, by name - filled by the patch
        // below and read by the scene-sound attenuation further down, which
        // needs the same answer. It used the raw path sample, and a sample is
        // not a position (`NodeMotion::placeOn`): the flat's entrance door
        // reports 632/-43/34, so the listener was measured against a point
        // 3400 units outside the building and the door slid in silence at
        // gain 0.03. Computed once here rather than twice.
        std::map<std::string, std::array<float, 3>> motionAt;
        std::vector<omk::Program::NodeMotion> allMotions;
        std::map<std::string, omk::SceneRunner::NodeScale> allScales;
        for (const omk::SceneRunner* sr : {&session.sceneOut(), &session.scene()}) {
            if (!sr->loaded()) continue;
            for (const auto& mo : sr->motions()) {
                // say once which pool moved which mesh - a door that resolved
                // in the outgoing pool is the case this line exists for
                const std::string key = mo.name + (sr == &session.scene() ? "/active" : "/out");
                if (motionLogged.insert(key).second)
                    std::printf("motion: mesh '%s' moved by the %s pool (%s) - first sample %.0f %.0f %.0f%s\n",
                                mo.name.c_str(), sr == &session.scene() ? "ACTIVE" : "OUTGOING",
                                sr->file().c_str(), mo.pos[0], mo.pos[1], mo.pos[2],
                                mo.rotated ? ", rotated" : "");
                allMotions.push_back(mo);
            }
            for (const auto& ns : sr->nodeScales()) allScales[ns.first] = ns.second;
        }
        if (!allMotions.empty() || !allScales.empty()) {
            struct Patch {
                float s[3] = {1.0f, 1.0f, 1.0f};
                bool  hasMotion = false;
                float pos[3] = {0, 0, 0};
                omk::Quatf q{1, 0, 0, 0};
                bool  rotated = false;
            };
            for (int sl = 0; sl < 2; ++sl) {
                WorldSlot& w = worldSlots[static_cast<std::size_t>(sl)];
                if (w.geo.corners.empty() || w.meshes.empty()) continue;
                const auto meshIndex = [&](const std::string& name) {
                    for (std::size_t k = 0; k < w.meshes.size(); ++k)
                        if (sameName(w.meshes[k].name, name)) return static_cast<int>(k);
                    return -1;
                };
                std::map<int, Patch> patches;
                for (const auto& ns : allScales) {
                    const int mi = meshIndex(ns.first);
                    if (mi < 0) continue;
                    Patch& p = patches[mi];
                    for (int c = 0; c < 3; ++c) p.s[c] = ns.second.s[c];
                }
                for (const auto& mo : allMotions) {
                    if (!mo.placed) continue;
                    const int mi = meshIndex(mo.name);
                    if (mi < 0) continue;
                    Patch& p = patches[mi];
                    p.hasMotion = true;
                    // THE PATH IS A DISPLACEMENT, and the anchor is the mesh
                    // itself. `Script_MoveObjectOnPath` places the node at
                    // `sample(t) - sample(t0) + the node's position when the
                    // move began`; for a set mesh that anchor is where the
                    // set authored it, which is `mp` below. Taking `pos`
                    // outright is right only where a path happens to be
                    // authored on its mesh - `AHALL40`'s are, `AAPKAYL`'s are
                    // not, and that is the whole of the apartment door bug.
                    mo.placeOn(w.meshes[static_cast<std::size_t>(mi)].pos, p.pos);
                    motionAt[mo.name] = {p.pos[0], p.pos[1], p.pos[2]};
                    if (std::getenv("OMK_TRACE_MOTION")) {
                        const float* mp = w.meshes[static_cast<std::size_t>(mi)].pos;
                        std::printf("  [motion] %-12s sample %.0f %.0f %.0f  from %.0f %.0f %.0f"
                                    "  anchor %.0f %.0f %.0f  ->  %.0f %.0f %.0f  (%s)\n",
                                    mo.name.c_str(), mo.pos[0], mo.pos[1], mo.pos[2],
                                    mo.hasFrom ? mo.from[0] : 0.0f,
                                    mo.hasFrom ? mo.from[1] : 0.0f,
                                    mo.hasFrom ? mo.from[2] : 0.0f,
                                    mp[0], mp[1], mp[2], p.pos[0], p.pos[1], p.pos[2],
                                    mo.hasFrom ? "param 5 set: a displacement"
                                               : "param 5 zero: the sample OUTRIGHT");
                    }
                    p.q = omk::Quatf{mo.quat[0], mo.quat[1], mo.quat[2], mo.quat[3]};
                    p.rotated = mo.rotated;
                }
                bool moved = false;
                for (const auto& kv : patches) {
                    const int mi = kv.first;
                    const Patch& pa = kv.second;
                    if (w.baseCorners.empty()) w.baseCorners = w.geo.corners;
                    if (w.baseSoup.empty()) w.baseSoup = w.soup;
                    if (w.baseSteep.empty()) w.baseSteep = w.steep;
                    const float* mp = w.meshes[static_cast<std::size_t>(mi)].pos;
                    // The motion's orientation: the path sample's 3x3 goes to
                    // node `+56` through `sub_437160` beside the
                    // `o3de_SetNodePos`, and an object that turns in place
                    // has ONLY the rotation - the Impasse's fan `Ventilo`
                    // (omk-play 53). A `Mesh` record carries no orientation,
                    // so the node's matrix starts as identity and the
                    // sample's applies directly, about the authored origin.
                    const float* at = pa.hasMotion ? pa.pos : mp;
                    const auto place = [&](const float in[3], float out[3]) {
                        const float local[3] = {(in[0] - mp[0]) * pa.s[0],
                                                (in[1] - mp[1]) * pa.s[1],
                                                (in[2] - mp[2]) * pa.s[2]};
                        float r[3] = {local[0], local[1], local[2]};
                        if (pa.rotated) omk::qrot(pa.q, local, r);
                        out[0] = r[0] + at[0]; out[1] = r[1] + at[1]; out[2] = r[2] + at[2];
                    };
                    for (std::size_t c = 0; c < w.geo.corners.size(); ++c) {
                        if (c >= w.geo.cornerMesh.size() || w.geo.cornerMesh[c] != mi) continue;
                        const float in[3] = {w.baseCorners[c].x, w.baseCorners[c].y, w.baseCorners[c].z};
                        float o[3];
                        place(in, o);
                        w.geo.corners[c].x = o[0]; w.geo.corners[c].y = o[1]; w.geo.corners[c].z = o[2];
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
                                float out[3];
                                place(&base[o], out);
                                soup[o] = out[0]; soup[o + 1] = out[1]; soup[o + 2] = out[2];
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
                    if (static_cast<std::size_t>(e.sprite) >= spriteTex.size() ||
                        spriteTex[static_cast<std::size_t>(e.sprite)].rgb.empty())
                        std::printf("ctl-effect: sprite %d is not registered by the library or the "
                                    "scene - nothing will draw\n", e.sprite);
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
        if (player && globalRt) {
            const auto& rt = *globalRt;         // the library, see its construction
            for (const auto& es : player->sounds()) {
                const int i = rt.wavBydId(es.id);
                if (i < 0) {
                    std::printf("ctl-effect: sound id %d is not in the global library\n", es.id);
                    continue;
                }
                const auto pcm = wavToDevice(rt.wavData(i), 44100);
                if (!pcm.empty()) { sfxLog("ctl-effect", pcm.size(), es.id, i, 1.0f, &pcm);
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
                float sceneSoundDist = -1.0f;
                {
                    const auto mo = sc.motionsOf(fs.program);
                    if (!mo.empty() && player) {
                        const float* L = player->pos();
                        float best = 1e30f;
                        for (const auto& m : mo) {
                            // the PLACED position, not the raw sample
                            const auto pl = motionAt.find(m.name);
                            const float* w = pl == motionAt.end() ? m.pos
                                                                 : pl->second.data();
                            const float dx = w[0] - L[0], dy = w[1] - L[1],
                                        dz = w[2] - L[2];
                            best = std::min(best, dx * dx + dy * dy + dz * dz);
                        }
                        const float d = std::sqrt(best);
                        // full within a room's width, then 1/d out to silence
                        constexpr float kNear = 120.0f;    // inches: ~3 m
                        constexpr float kFar  = 4000.0f;   // ~100 m
                        gain = d <= kNear ? 1.0f
                             : d >= kFar  ? 0.0f
                             : kNear / d;
                        sceneSoundDist = d;
                    }
                }
                sfxLog(fs.cue.loop ? "scene-LOOP" : "scene-sound",
                       pcm.size(), fs.cue.wav, fs.object, gain, &pcm);
                if (sceneSoundDist >= 0.0f)
                    std::printf("audio:   ...at %.0f units from the nearest motion of object %d\n",
                                static_cast<double>(sceneSoundDist), fs.object);
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
            // ---- BOUND TO HIM, AND POSING HIM, ARE TWO QUESTIONS ---------
            //
            // Op 46 ends `ScriptObject_StartOnActor(Actor_Player(), object,
            // scene, caller)`, and that function binds the object's program to
            // the player's actor record and puts him in ACTOR_STATE 4:
            //
            //     v18 = u32i(v5, 101);        // +404, his ACTOR_STATE
            //     if (v18 != 4) { u32i(v5, 102) = v18;   // saved at +408
            //                     sub_436D20(node); u32i(v5, 101) = 4; }
            //
            // A CUTSCENE BEAT starts that way, and the flat's DOORS do not:
            // they are `scx.play.wait` (op 58), which is `how` "scene". So
            // this loop never saw them, and the door report of 2026-09-09
            // reaches the frontend through `parkedOnProgram` alone (see
            // `script/area.h`). A `clip >= 0` guard was added here in the same
            // breath as that fix, defensively, and it was WRONG: a beat's clip
            // is its FIRST body animation, so a chain on a step that animates
            // nothing stopped counting and the walker took the body back for a
            // frame. `engine: player program` and `line facing` caught it, one
            // frame out each. The binding is the test, and the clip is only
            // read for the facing below.
            bool playerDriven = false;      // a program is bound to him
            if (sc.loaded()) {
                const auto& started = sc.started();
                for (std::size_t k = 0; k < started.size(); ++k) {
                    const auto& stt = started[k];
                    if (stt.how != "player" || !sc.programRunning(static_cast<int>(k)))
                        continue;
                    playerDriven = true;
                    if (stt.clip < 0) continue;   // no body step to read a facing off
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
            // ---- WHO A `scx.play.player` PROGRAM POSES --------------------
            //
            // Op 46 (0x00402C30) ends
            // `ScriptObject_StartOnActor(Actor_Player(), object, scene,
            // caller)` - the three pushes before `call sub_419E00` are that
            // call's own last three arguments, since `Actor_Player` takes
            // none - and `ScriptObject_StartOnActor` binds the program to
            // `&g_Actors + 1312 * a1`, the player's actor record, exactly as
            // 59/60 bind one to the actor they name. So the body a player
            // program poses and places is an ACTOR like any other, and the
            // engine draws it through the same walk.
            //
            // This viewer had no path for that: `staged` is built from
            // `Session::shown()`, which the player is not in (no placement
            // record puts him anywhere - the save does), and the controller
            // draws him only in adventure mode, which a program suspends. So
            // during the flat's goodbye the program ran, posed NOBODY, and
            // Kay'l was simply absent from his own cutscene.
            playerProgram = playerDriven;
            // ...and the OUTGOING pool drives bodies too (`poolFor` below
            // searches both), so a program that survives a scene swap keeps
            // him staged. `playerDriven` itself stays the active scene's
            // question, because the camera hold and the adventure gate are
            // about who is at the keys now.
            if (!playerProgram && session.sceneOut().loaded()) {
                const auto& so = session.sceneOut();
                for (std::size_t k = 0; k < so.started().size(); ++k)
                    if (so.started()[k].how == "player" &&
                        so.programRunning(static_cast<int>(k)))
                        playerProgram = true;
            }
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
                            su.sweepSpheres = sph;
                            std::printf("adventure: the walker sweeps a sphere of radius %.1f "
                                        "(the model's %zu sweep spheres%s) against %zu wall faces\n",
                                        su.sweepRadius, sph.size(),
                                        sph.empty() ? " - none, the sim's 12 stands in" : "",
                                        playerSteep.size() / 9);
                        }
                        for (int k = 0; k < 3; ++k) su.pos[k] = session.playerPos()[k];
                        su.facing = handoverFacingKnown ? handoverFacing : session.playerYaw();
                        player = std::make_unique<omk::PlayerController>(su);
                        // THE CAMERA'S SOLIDS. `sub_417070` casts from the
                        // camera's target to 1.2x its eye distance and pulls
                        // the eye in to the first hit; the engine's ray
                        // (`sub_444810`) walks the scene's meshes, and the
                        // nearest thing here is the two soups the walker
                        // already keeps - the walkable faces and the steep
                        // complement, which between them are the set's solid
                        // surfaces. Both are refilled in place by
                        // `rebuildWorld`, so the pointers survive a
                        // transition the way the walker's do.
                        static const bool noCamCollide =
                            std::getenv("OMK_NO_CAM_COLLIDE") != nullptr;
                        if (!noCamCollide)
                            player->setCameraSolids(&playerSteep, &playerSoup);
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
            // ...AND NOT BETWEEN TWO BEATS OF ONE CUTSCENE.
            // `!playerDriven && !activeEditing()` asks "is a program driving
            // him THIS FRAME", which is false for the one or two frames
            // between a beat ending and the script starting the next - and
            // for those frames the controller took his body, drew his .CTL
            // idle and the hand-back's `facing 0`. `Session::parkedOnProgram`
            // is the chain still being in flight: the beat's script is parked
            // on the object it started and will start the next when case 3
            // resumes it. See the accessor for why the engine cannot have
            // this fault at all (todo/omk-play.md 78).
            adventure = player && !playerDriven && !session.dialogOpen() &&
                        !uiPause && !sc.activeEditing() &&
                        !session.parkedOnProgram();
            // A TELEPORT IS CONSUMED WHATEVER THE MODE. `sub_41BF50` writes the
            // actor's position outright; nothing about it waits for adventure
            // mode. This sat inside `if (adventure)` below, and Kay'l's flat
            // runs `actor.goto_address 678` and `dialog.start 402` on the
            // SAME pump frame - so by the time the frontend looked, the
            // conversation was open, `adventure` was false, and the body the
            // shot is framed against stayed 18 units short of the address.
            // Camera 4555 is an over-the-shoulder POV authored 15 units in
            // front of 678's face; with him short of it the eye sat inside
            // his head and the close-up drew the inside of it. The engine's
            // own frame of that shot (`traces/frames/dlg402-44`) has no Kay'l.
            if (player && session.placementSeq() != placementSeen) {
                placementSeen = session.placementSeq();
                player->placeAt(session.playerPos(), session.playerYaw());
                std::printf("frame %ld: actor.goto_address %d - the player put down at "
                            "%.0f %.0f %.0f facing %.0f\n", n, session.playerAddress(),
                            player->pos()[0], player->pos()[1], player->pos()[2],
                            player->facing());
            }
            // ...AND THE CHANNEL KEEPS TICKING THROUGH A CONVERSATION.
            //
            // `Game_Tick` runs `Actors_TickAll` whatever is on screen, and
            // ACTOR_STATE 16/17 is in its dispatch - `Actor_TickDialogue`,
            // whose last line is `return Actor_ScanZones(a1)`. So the
            // player's channel goes on running while he talks, and that is
            // what carries the gait he arrived on into the group-400 stance
            // `Actor_EnterDialogueMode` selected.
            //
            // This viewer ticked him only in adventure mode, which a
            // conversation turns off, so he froze on whatever frame the walk
            // left him - a reader's shot of Kay'l standing mid-stride beside
            // Telis for the length of the conversation, arms out, one leg
            // lifted. It only became visible once he was drawn in
            // conversations at all. Nothing pressed, exactly as the `walk`
            // case below and `player.anim.hold` do it.
            // ...and WHETHER HE TICKED, because `specialMoves()` is the list
            // this tick produced and `PlayerController::tick` is the only
            // thing that clears it. A frame that skips the tick - which
            // riding and boarding both do, ACTOR_STATE 7 and 8 not being
            // walkers - therefore re-reads the PREVIOUS tick's list, and on
            // 2026-09-08 that fired `MDSLIDIN` a second time one frame after
            // boarding: he was put aboard, then put aboard again, and the
            // journey's arrival landed on a body that had just been re-seated.
            // Every handler in those two loops is edge-shaped, so the guard
            // belongs on the read and not on each of them.
            bool playerTicked = false;
            static const std::vector<std::string> kNoMoves;
            if (player && !adventure && session.dialogOpen()) {
                player->tick(static_cast<float>(frameSec * 30.0), 0);
                playerTicked = true;
            }
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
                // (the placement itself is consumed above `if (adventure)`,
                // whatever mode the frame is in - see there)
                // THE CROWD PUSH - `Actor_TickNpc`, before `Actor_ApplyMotion`:
                // the spatial index's answer for his spheres, added to his
                // position outright (docs/STREET_LIFE.md 3). The Session
                // posts the bump message when a walker was touched.
                {
                    float push[3];
                    // ...and NOT while he boards or leaves. ACTOR_STATE 6 and
                    // 8 tick through `Actor_TickChannelOnly`, and the push is
                    // `Actor_TickNpc`'s - it never runs for them. It ran here:
                    // `MDACTION` snaps him INSIDE the vehicle's own body
                    // sphere, so the pool shoved him out every frame faster
                    // than `H_SLDIN` walked him in - 25 units in the first
                    // five frames, before the clip had moved him at all - and
                    // he finished the clip beside the slider with the door
                    // open above him. Traced frame by frame 2026-09-08.
                    if (!boarding && !leaving && !playerSpheres.empty() &&
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
                    playerTicked = true;
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
                    playerTicked = true;
                } else {
                    // `sub_4A7A20` NEVER YIELDS 0 - nothing held remaps to the
                    // idle word - and the channel reads a literal 0 as
                    // `kQueueDrives`, the "no device this tick" contract, which
                    // SKIPS the input pass entirely. Passing the raw `bits`
                    // therefore stopped the 20-slot latch from ever being
                    // dropped once nothing was pressed (`latch_[i] = 0` lives
                    // inside that pass), so a button spent by one press stayed
                    // spent for ever and the SECOND press did nothing at all.
                    // Invisible until the latch was made to survive
                    // `SetPersoBankGroup` the way the engine's does.
                    // The call's own arrival, announced once: state 2
                    // drives until the 117 units are met, and then the slot
                    // goes OPEN (3) - `MDSLIDIN`'s "slider is not in open
                    // mode !" is the refusal that guards this.
                    if (session.sliders().calledIsOpen() && !calledOpenTold) {
                        calledOpenTold = true;
                        float at[3] = {0, 0, 0};
                        session.sliders().calledAt(at);
                        // ...and WHERE, because "walk to it" is not the rule:
                        // `MDACTION` wants him within 4.00 m AND on the
                        // slider's own -X, and the door point is the placement
                        // the arm would snap him to.
                        float ax[3], az[3];
                        if (player && session.sliders().calledFrame(at, ax, az)) {
                            if (!doorOffState) {
                                const auto ref = fs.read("ANIMS/slf_112.3da");
                                doorOffState = (!ref.empty() &&
                                                player->boardOffset(ref, 60, doorOff)) ? 1 : -1;
                            }
                            float door[3] = {at[0], at[1] - omk::kBoardSeatY, at[2]};
                            if (doorOffState == 1) {
                                for (int k = 0; k < 3; ++k)
                                    door[k] += doorOff[0] * ax[k] + doorOff[2] * az[k];
                                door[1] += doorOff[1];
                            }
                            std::printf("slider: its DOOR is at %.0f %.0f %.0f - stand "
                                        "within 4.00 m of that side (its -X, heading "
                                        "%.2f %.2f) and press the action button\n",
                                        door[0], door[1], door[2], az[0], az[2]);
                            if (boardArg && !boarded && !boarding) {
                                float feet[3] = {door[0], door[1] + player->cameraLift(), door[2]};
                                const float yaw = static_cast<float>(
                                    std::atan2(at[0] - door[0], -(at[2] - door[2])) * 57.29577951308232);
                                player->placeAt(feet, yaw);
                                session.setPlayerPosition(player->pos(), yaw);
                                boardPress = true;
                                std::printf("harness: --board put him at the door point %.0f %.0f %.0f "
                                            "facing the slider, and presses the action button\n",
                                            feet[0], feet[1], feet[2]);
                            }
                        }
                        std::printf("slider: OPEN at %.0f %.0f %.0f - walk to "
                                    "it and press the action button\n",
                                    at[0], at[1], at[2]);
                    }
                    // ---- MDACTION'S SLIDER ARM -----------------------
                    //
                    // Boarding begins HERE and not at `MDSLIDIN`, which is a
                    // correction: this port fired `MDSLIDIN` straight off the
                    // action button, so there was no animation, no side test
                    // and no camera. `MDACTION` (0x0046AEC0) takes its slider
                    // arm at `loc_46AFF8` and does all of it -
                    //
                    //   sub_438240()           the active slider, or nothing
                    //   [+404] != 3            not while shooting
                    //   sub_438290(slider)     its node+180 bit 4 set
                    //   dot(d, localX) >= 0    THE CORRECT SIDE: the man must
                    //                          stand on the slider's -X, the
                    //                          side its door and camera 9 are
                    //   |d| < 157.48032        FOUR METRES
                    //   sub_438420(slider, 3)  the slider goes OPEN - the arm
                    //                          SETS the mode, it does not ask
                    //   the snap              slider + M . (root0(H_SLDIN) -
                    //                          root0(slf_112.3da)), y - 33.15
                    //   sub_437140(node, M)    the root frame becomes the
                    //                          SLIDER's, so the clip's step
                    //                          runs in the vehicle's frame
                    //   [+404] = 6             ACTOR_STATE 6
                    //   SetPersoBankGroup(60)  H_SLDIN: the door opens, he
                    //                          steps in, the door shuts
                    //   Camera_Request(9, ..)  60 frames on the slider
                    //
                    // - and the `MDSLIDIN` half is what the CHANNEL fires at
                    // the end of that clip (group 60's entry [160], no input,
                    // goto H_SLIDER), handled with the other special moves.
                    if (!ride && !boarded && !boarding && (bits & 0x10u) && !mountSpent) {
                        const float me[3] = {session.playerPos()[0],
                                             session.playerPos()[1],
                                             session.playerPos()[2]};
                        float at[3], ax[3], az[3];
                        // ...and when it refuses, SAY WHICH TEST. Both are
                        // geometric and neither is visible from the seat of a
                        // keyboard: "nothing happened" is the same picture for
                        // standing 5 m away and for standing at the wrong door.
                        if (player && !session.sliders().canMount(me) &&
                            session.sliders().calledVehicle() >= 0 &&
                            session.sliders().calledFrame(at, ax, az)) {
                            const float dx = at[0] - me[0];
                            const float dy = at[1] + omk::kBoardSeatY - me[1];
                            const float dz = at[2] - me[2];
                            const float dot = dx * ax[0] + dy * ax[1] + dz * ax[2];
                            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
                            const bool side = dot < 0.0f;
                            const bool far_ = len >= omk::kBoardReach;
                            const int  st   = session.sliders().callMachine().state;
                            std::printf("MDACTION: the slider refuses - %s%s%s%s (dot %+.0f, "
                                        "%.2f m of the 4.00 it allows, ride state %d)\n",
                                        st != 3 ? "it is not standing OPEN" : "",
                                        (st != 3 && (side || far_)) ? "; " : "",
                                        side ? "he is on the WRONG SIDE; its door is on "
                                               "its own -X" : "",
                                        (side && far_) ? ", and he is too far away"
                                                       : (far_ ? "he is too far away" : ""),
                                        dot, len * 0.0254f, st);
                        }
                        if (player && session.sliders().canMount(me) &&
                            session.sliders().calledFrame(at, ax, az)) {
                            if (!doorOffState) {
                                // `dword_90EF28`, which `Game_Init` fills with
                                // `anims\slf_112.3da` for exactly this.
                                const auto ref = fs.read("ANIMS/slf_112.3da");
                                doorOffState = (!ref.empty() &&
                                                player->boardOffset(ref, 60, doorOff)) ? 1 : -1;
                            }
                            float door[3] = {at[0], at[1] - omk::kBoardSeatY, at[2]};
                            if (doorOffState == 1)
                                for (int k = 0; k < 3; ++k)
                                    door[k] += doorOff[0] * ax[k] + doorOff[2] * az[k];
                            if (doorOffState == 1) door[1] += doorOff[1];
                            // ...AND THAT Y IS A PELVIS, THIS CLASS TAKES FEET.
                            // The engine writes the actor's +244..+252, which
                            // is his ORIGIN and is the PELVIS (`player.h`,
                            // settled with the camera lift - 41.9 for
                            // `HO1_FNM`), while `PlayerController`'s position
                            // is the walker's, at the feet. Handing the
                            // engine's number straight over left him standing
                            // 0.8 m in the air with his feet at the slider's
                            // waistline - which is what a render of the
                            // boarding beside the original's screenshot shows
                            // at once and no amount of reading the listing
                            // was going to say. Y points down, so the feet are
                            // BELOW the pelvis by the lift.
                            door[1] += player->cameraLift();
                            const float dd = std::sqrt(
                                (at[0] - me[0]) * (at[0] - me[0]) +
                                (at[2] - me[2]) * (at[2] - me[2]));
                            // The euler is NOT written by this arm - only the
                            // position and the root frame are - so he keeps
                            // the way he was facing and the clip turns him.
                            player->rideAt(door, player->facing());
                            player->setActorState(omk::ActorState::ChannelOnly6, "MDACTION");
                            player->setRootFrame(ax, az);
                            player->setChannelOnly(true);
                            boarding = true;
                            mountSpent = true;
                            boardCam = 60;
                            const bool got = player->enterGroupById(60);
                            std::printf("MDACTION: the slider's door at %.0f %.0f %.0f, "
                                        "%.1f m away on the right side - snapped to "
                                        "%.0f %.0f %.0f (offset %.1f %.1f %.1f in its "
                                        "frame%s), ACTOR_STATE 6, %s, camera 9\n",
                                        at[0], at[1], at[2], dd * 0.0254f,
                                        door[0], door[1], door[2],
                                        doorOff[0], doorOff[1], doorOff[2],
                                        doorOffState == 1 ? "" : " - UNREAD, at the slider",
                                        got ? "H_SLDIN plays" : "but the bank has no group 60");
                        }
                    }
                    if (!(bits & 0x10u)) mountSpent = false;
                    // ...UNLESS HE IS RIDING. ACTOR_STATE 7 and 8 do not
                    // walk - `Actors_TickAll`'s row for each has `walks`
                    // false - and the ride owns the body: `sub_457F50` writes
                    // his position outright every frame. Ticking the walker
                    // as well made the two fight, and the walker won.
                    // ...and while BOARDING he still ticks: ACTOR_STATE 6 is
                    // `Actor_TickChannelOnly`, the channel and nothing else,
                    // which is what `setChannelOnly` models - so `H_SLDIN`
                    // plays and its root motion carries him in.
                    if (!ride && !boarded) {
                        player->tick(static_cast<float>(frameSec * 30.0),
                                     bits ? bits : omk::kIdleInput);
                        playerTicked = true;
                        // While he BOARDS or LEAVES, say where the clip is
                        // carrying him - the door snap is one number and the
                        // carry-in is seventy-two more, and a render at the
                        // end of the clip showed him beside the vehicle.
                        if ((boarding || leaving) && player->ticks() % 12 == 0) {
                            const auto& lf = player->last();
                            std::printf("%s: clip frame %d at %.1f %.1f %.1f (root delta "
                                        "%+.2f %+.2f %+.2f this tick, .CTL %s)\n",
                                        boarding ? "boarding" : "leaving",
                                        player->poseFrame(), player->pos()[0],
                                        player->pos()[1], player->pos()[2],
                                        lf.rootDelta[0], lf.rootDelta[1], lf.rootDelta[2],
                                        player->clipName().c_str());
                        }
                    }
                }
                // ---- SEATED, not driving ------------------------------
                //
                // Aboard between the mount and either "Manuelle" or the end
                // of a journey: `sub_457F50` writes his position from the
                // slider's every frame, and the slider is where the pool's
                // drive put it.
                // ...and SAY when the slider rejoins the traffic, which is
                // `sub_456530` case 7's `sub_438420(slider, 0)`.
                // `sub_456530` case 7 releases the slider only once he is
                // 300 clear of it and in front of it, so it needs him.
                {
                    const float me[3] = {session.playerPos()[0], session.playerPos()[1],
                                         session.playerPos()[2]};
                    session.sliders().setRider(me, player ? player->facing()
                                                          : session.playerYaw());
                }
                // WHERE THE SLIDER IS after he gets out - a reader lost it at
                // Qalisar's kerb while the staging said "staged".
                if (leaving || (session.sliders().calledVehicle() >= 0 && !boarded && !boarding)) {
                    static long lastVehTold = -1000;
                    if (n - lastVehTold >= 45) {
                        lastVehTold = n;
                        float vat[3] = {0, 0, 0};
                        const bool have = session.sliders().calledAt(vat);
                        std::printf("frame %ld: after the ride - the called vehicle %s at %.0f %.0f %.0f, "
                                    "ride state %d; he is at %.0f %.0f %.0f\n", n,
                                    have ? "is" : "is GONE", vat[0], vat[1], vat[2],
                                    session.sliders().callMachine().state,
                                    session.playerPos()[0], session.playerPos()[1], session.playerPos()[2]);
                    }
                }
                // ...and FOLLOW the released vehicle for ten seconds after,
                // since a reader lost it at Qalisar's kerb: once it is
                // traffic again the ambient drive owns it, and where that
                // drive puts it on its first step is the question.
                static int  releasedSlot = -1;
                static long releasedAt = -1;
                if (session.sliders().calledVehicle() >= 0) releasedSlot = session.sliders().calledVehicle();
                if (session.sliders().takeReleasedNotice()) {
                    releasedAt = n;
                    std::printf("slider: RELEASED - he is 300 clear and in front of it, "
                                "so it goes back to mode 0 and drives as ordinary "
                                "traffic again\n");
                }
                if (releasedAt >= 0 && n - releasedAt <= 300 && (n - releasedAt) % 30 == 0 &&
                    releasedSlot >= 0 && static_cast<std::size_t>(releasedSlot) < session.sliders().vehicles().size()) {
                    const auto& rv = session.sliders().vehicles()[static_cast<std::size_t>(releasedSlot)];
                    if (rv.live && rv.mover >= 0) {
                        const auto& rm = session.sliders().movers()[static_cast<std::size_t>(rv.mover)];
                        std::printf("frame %ld: the released vehicle (slot %d, '%s', state %d) at %.0f %.0f %.0f, "
                                    "lane %d seg %d remaining %.0f; he is at %.0f %.0f %.0f\n", n, releasedSlot,
                                    rv.model.c_str(), rv.state, rm.body[0], rm.body[1], rm.body[2], rm.lane, rm.seg,
                                    rm.remaining, session.playerPos()[0], session.playerPos()[1], session.playerPos()[2]);
                    } else {
                        std::printf("frame %ld: the released vehicle (slot %d) is DEAD\n", n, releasedSlot);
                    }
                }
                if (boarded && !ride) {
                    float at[3];
                    if (session.sliders().calledAt(at)) {
                        // `sub_457F50`: the rider's +248 = the RIDE's y + 10,
                        // and the ride's y is the vehicle's HOVERING height -
                        // `SliderRide` flies `kHover` (30.75) above the floor -
                        // so his PELVIS sits 10 below the hovering node, 20.75
                        // above the road: a seat. `calledAt` is the body point
                        // at ROAD level (the node is drawn 30.75 above it), and
                        // this class takes FEET, so: road - 30.75 + 10 + lift.
                        // It read `at.y + 10` in feet-space until 2026-09-08,
                        // which sat him 11 units (28 cm) too high.
                        const float seat[3] = {at[0],
                                               at[1] - static_cast<float>(omk::SliderRide::kHover)
                                                     + static_cast<float>(omk::SliderRide::kNodeUp)
                                                     + (player ? player->cameraLift() : 0.0f),
                                               at[2]};
                        const float yaw = session.sliders().calledYaw();
                        session.setPlayerPosition(seat, yaw);
                        if (player) player->rideAt(seat, yaw);
                    }
                    // ...and THE JOURNEY'S END: state 6 -> 4, camera 10. He
                    // gets out at the destination, the slider leaves
                    // (state 7, released once he is 300 clear and ahead).
                    if (session.sliders().journeyArrived() && journeyTo >= 0) {
                        // ---- HE GETS OUT WHERE THE SLIDER STOPPED --------
                        //
                        // NOT at the destination's address, which is what this
                        // did and what a reader reported as being *"teleported
                        // instead of just leaving the slider where it
                        // arrives"*. `sub_4570F0` (the stop) writes only the
                        // actor's Y - `sliderY - 33.149605` - and copies +244
                        // and +252 through UNCHANGED, then hands over to
                        // `sub_468FA0`, which does the real placement and is
                        // the exact mirror of `MDACTION`'s entry:
                        //
                        //   slider mode 4, its speed zeroed, then mode 5
                        //   off = root0(group 61's clip) - root0(dword_9103D8)
                        //   actor = slider + M . off, y -= 33.149605
                        //   +260 = FLT_MAX, o3de_MoveNodeBy, sub_437140(M)
                        //   ACTOR_STATE 8 (both +404 and +408)
                        //   SetPersoBankGroup(group 61) - H_SLDOUT, 51 frames
                        //
                        // and back in `sub_4570F0`: `sub_438420(slider, 7)`,
                        // the slider leaves, and `Camera_Request(17, ..., 60)`.
                        // The reference clip is `slf_113.3da` and NOT the
                        // entry's `slf_112.3da` - two different globals, and
                        // measuring group 61 against 112 was wrong even though
                        // the two clips' root keys turn out to be identical.
                        float at[3], ax[3], az[3];
                        bool out = false;
                        if (player && session.sliders().calledFrame(at, ax, az)) {
                            if (!exitOffState) {
                                const auto ref = fs.read("ANIMS/slf_113.3da");
                                exitOffState = (!ref.empty() &&
                                                player->boardOffset(ref, 61, exitOff)) ? 1 : -1;
                            }
                            float o[3] = {at[0], at[1] - omk::kBoardSeatY, at[2]};
                            if (exitOffState == 1) {
                                for (int k = 0; k < 3; ++k)
                                    o[k] += exitOff[0] * ax[k] + exitOff[2] * az[k];
                                o[1] += exitOff[1];
                            }
                            o[1] += player->cameraLift();   // pelvis -> feet, as above
                            player->rideAt(o, player->facing());
                            player->setActorState(omk::ActorState::SliderRide, "sub_468FA0");
                            player->setRootFrame(ax, az);
                            player->setChannelOnly(true);
                            session.setPlayerPosition(o, player->facing());
                            // ...and the RELEASE test's rider, NOW. `case 7` is
                            // armed by `dismountCalled` below and tests "300
                            // clear and in front" on the next tick; after a
                            // load the rider it had was his position in the
                            // OLD city, nine kilometres away, so the slider was
                            // handed back to the traffic one frame after he
                            // got out and was gone before he stood up (traced:
                            // "the called vehicle is GONE at 0 0 0, ride state
                            // 0" at ARRIVED+1). The engine's ride writes +244
                            // before mode 7 is set; this is that write.
                            session.sliders().setRider(o, player->facing());
                            out = player->enterGroupById(61);
                            leaving = true;
                            std::printf("slider: ARRIVED - he gets OUT WHERE IT "
                                        "STOPPED, %.0f %.0f %.0f (offset %.1f %.1f "
                                        "%.1f in its frame%s), ACTOR_STATE 8, %s\n",
                                        o[0], o[1], o[2], exitOff[0], exitOff[1],
                                        exitOff[2],
                                        exitOffState == 1 ? "" : " - UNREAD",
                                        out ? "H_SLDOUT plays" : "but the bank has "
                                              "no group 61");
                        }
                        // `Camera_Request(17, {player, player, 60.0f, 1, .., -1})`
                        // - `sub_4570F0`'s last act. Preset 17: eye
                        // (-39.3701, 78.7402, 0), target (0, 0, 0), fov 75,
                        // subjects 0/0 - a metre behind and two up, on him, over
                        // sixty frames. The same blend the take camera uses.
                        {
                            static constexpr float kExitEye[3] = {-39.3701f, 78.7402f, 0.0f};
                            static constexpr float kExitAt[3]  = {0.0f, 0.0f, 0.0f};
                            playerCamRequest(kExitEye, kExitAt, 75.0f, 60.0f);
                            std::printf("slider: camera 17 requested - preset 17 on him over 60 frames\n");
                        }
                        session.sliders().dismountCalled();
                        boarded = false;
                        journeyTo = -1;
                        calledDestination = -1;
                    }
                }
                // ---- THE RIDE ---------------------------------------
                //
                // `Slider_TickRide` (0x00458150), with the pool and the
                // arrival left out: `--ride` mounts him where he stands, and
                // what runs from there is the engine's own model.
                //
                // The delta is HALVED, because that function's first act is
                // `flt_4C30D8 *= 0.5` for all three helpers - the whole ride
                // advances at half a frame per frame - and the input word is
                // the same one the walker takes, which is what
                // `dword_8F5DE0 = dword_4E9718` hands the flight model.
                //
                // After the helpers the engine drops the player onto the
                // surface under him and runs `Actor_ScanZones`, so riding
                // still triggers zones; `setPlayerPosition` is what tells the
                // Session, and the zone scan is its own.
                if (ride) {
                    const auto probe = [&](double px, double py, double pz,
                                           double& drop, bool& braking) {
                        braking = false;      // the "OP" surfaces are not
                                              // identified here - step 3b
                        if (const auto h = omk::surfaceUnder(playerSoup, px, py, pz)) {
                            drop = h->y - py;
                            return true;
                        }
                        return false;
                    };
                    const double dt = frameSec * 30.0 * 0.5;
                    ride->fly(bits, dt, probe);
                    ride->hover(dt, probe);
                    if (ride->stopped) {
                        // `sub_4570F0`: the ride ends, the slider is dropped
                        // onto the ground and the camera hands back at mode
                        // **17** with the PLAYER as both subjects - not mode
                        // 0, which is why `sub_452570`'s arrive arm guards
                        // its own `Camera_Request(0, ...)` on the mode not
                        // already being 17.
                        std::printf("ride: stopped at %.0f %.0f %.0f - "
                                    "`sub_4570F0`, camera mode 17\n",
                                    ride->x, ride->y, ride->z);
                        const float at[3] = {static_cast<float>(ride->x),
                                             static_cast<float>(ride->y
                                                 + omk::SliderRide::kRiderUp),
                                             static_cast<float>(ride->z)};
                        session.setPlayerPosition(at, static_cast<float>(ride->yaw));
                        if (player) player->placeAt(at, static_cast<float>(ride->yaw));
                        // `sub_456530` state 7: it drives off once he is 300
                        // clear and in front of it.
                        session.sliders().dismountCalled();
                        ride.reset();
                    } else {
                        // `sub_457F50`: the rider takes the slider's x and z,
                        // and the actor's own `+248` its y plus 10.
                        double at[3];
                        ride->riderAt(at);
                        const float p3[3] = {static_cast<float>(at[0]),
                                             static_cast<float>(at[1]),
                                             static_cast<float>(at[2])};
                        session.setPlayerPosition(p3, static_cast<float>(ride->yaw));
                        if (player) player->rideAt(p3, static_cast<float>(ride->yaw));
                        // ...and the VEHICLE goes where the ride is, so the
                        // model the crowd pool draws is under him rather than
                        // left on the road. `sub_457F50` writes the slider's
                        // node from the ride's own position for the same
                        // reason.
                        const float vp[3] = {static_cast<float>(ride->x),
                                             static_cast<float>(ride->y),
                                             static_cast<float>(ride->z)};
                        session.sliders().placeCalled(vp, static_cast<float>(ride->yaw));
                    }
                }
                // STUCK BETWEEN WALLS. A move blocked for a whole second while a
                // direction is being asked for is the sweep's own failure to
                // slide out of a corner - a reader was held between the alley's
                // wall and the crates (2026-09-05). Printed with the position,
                // the heading and the move asked, so the corner can be probed.
                {
                    static int blockedRun = 0; static long stuckTold = -1000;
                    const auto& lf = player->last();
                    const bool asked = std::fabs(lf.rootDelta[0]) + std::fabs(lf.rootDelta[2]) > 0.05f;
                    if (lf.stepped && asked && lf.step == omk::StepResult::Blocked) ++blockedRun;
                    else blockedRun = 0;
                    if (blockedRun >= 30 && n - stuckTold >= 30) {
                        stuckTold = n;
                        std::printf("walker: STUCK - blocked %d frames running at %.1f %.1f %.1f facing %.0f, "
                                    "asking %+.2f %+.2f, %d wall passes hit last step\n",
                                    blockedRun, player->pos()[0], player->pos()[1], player->pos()[2],
                                    player->facing(), lf.rootDelta[0], lf.rootDelta[2],
                                    player->walker().lastSlides());
                    }
                }
                // WHERE THE FLOOR ENDS. A step that finds no floor under its
                // destination (`Reverted`), or a fall that begins, is the moment
                // the walker leaves the geometry - which a reader did through the
                // Impasse alley's wall on 2026-09-05. Printed with the position,
                // the heading and the sweep's verdict, once a second at most.
                {
                    static long voidTold = -1000;
                    const auto& lf = player->last();
                    const bool off = lf.stepped && (lf.step == omk::StepResult::Reverted ||
                                                    lf.step == omk::StepResult::Fell);
                    if (off && n - voidTold >= 30) {
                        voidTold = n;
                        const auto gp = player->walker().ground(player->pos()[0], player->pos()[1], player->pos()[2]);
                        std::printf("walker: %s at %.1f %.1f %.1f facing %.0f - move %+.2f %+.2f, "
                                    "%d wall passes hit, ground %s (probe: %s, soup %zu tris)\n",
                                    lf.step == omk::StepResult::Reverted ? "NO FLOOR ahead (reverted)"
                                                                          : "FALLING",
                                    player->pos()[0], player->pos()[1], player->pos()[2],
                                    player->facing(), lf.rootDelta[0], lf.rootDelta[2],
                                    player->walker().lastSlides(),
                                    lf.onGround ? "under him" : "NOT under him",
                                    gp ? (std::to_string(*gp)).c_str() : "none",
                                    player->walker().soup().size() / 9);
                    }
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
                    for (const auto& mv : (playerTicked ? player->specialMoves() : kNoMoves))
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
                // `--call N`: the sneak-call idiom, fired once. The screen
                // is requested the way a SCRIPT requests it (so it answers
                // itself) and the conversation started right after, which is
                // exactly `ui.open 0` / `dialog.start N`.
                if (animHoldHarness) {
                    animHoldHarness = false;
                    session.harnessHoldPlayer(true);
                    std::printf("--anim-hold: the player is held (a harness "
                                "for `player.anim.hold`)\n");
                }
                if (callDialog >= 0 && !walk && playerScreen < 0) {
                    std::printf("--call: ui.open 0 + dialog.start %d (a "
                                "harness for the sneak call, UI 3i)\n",
                                callDialog);
                    playerScreen = kScreenVideophone;
                    callHarness  = true;
                    callPending  = callDialog;
                    callDialog   = -1;
                }
                if (startShoot && !walk && player) {
                    // The harness way in. The engine's own is a script's
                    // `shoot.begin`, which is what `verify.py: engine: shoot
                    // mode` drives; this exists so a PERSON can stand in an
                    // arena and look at the mode, the way `--ride` does for
                    // the slider. -1 is the operand 27 of the 30 shipped
                    // sites pass, so the weapon is the Gun Waver.
                    startShoot = false;
                    session.shootBegin(-1);
                    // camera mode 4: `camera_presets.json` row 4 is eye
                    // (0,0,0) and target (0, 0, 787.4016) - the eye ON the
                    // player and the aim 20.00 m in front of him - both
                    // subject 0, fov 75, and every smoothing divisor ZERO, so
                    // it does not lag him at all. A script that names its own
                    // camera still wins, which is the follow block below.
                    const float eye[3] = {0.0f, 0.0f, 0.0f};
                    const float at[3]  = {0.0f, 0.0f, 787.4016f};
                    player->setCameraOffsets(eye, at, 75.0f);
                    std::printf("--shoot: shoot.begin -1 - camera mode %d, "
                                "eye on the player, aim 20 m ahead\n",
                                omk::ShootMode::kCameraMode);
                }
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
                // ...AND WHETHER MDACTION'S OWN OBJECT ARM CONSUMED THE PRESS.
                // See the gate on `session.pressAction()` below: the press
                // only reaches the zone system on the arm where the scan
                // found NOTHING.
                bool actionTookObject = false;
                for (const auto& mv : (playerTicked ? player->specialMoves() : kNoMoves)) {
                    if (mv == "MDACTION") actionFromMove = true;
                    // THE JUMP'S IMPULSE (`todo/player-vertical.md` step 2).
                    // `MDJUMP01` is the one that writes the three velocity
                    // fields; `MDJUMP0A`/`0B` only prepare them, and the
                    // controller does that arithmetic (`player.cpp`). Before
                    // this the walker's `vy_`/`airborne_` existed for FALLING
                    // and nothing ever pushed into them, which is the reader's
                    // *"jump is broken (animation play, but y position is too
                    // low so the jump become useless)"* - the clip played and
                    // the body never left the floor.
                    if (mv == "MDJUMP0A" || mv == "MDJUMP0B") {
                        if (player->jumpPrepare())
                            std::printf("jump: armed on '%s'\n",
                                        player->ctlStateName().c_str());
                    }
                    if (mv == "MDJUMP03") {
                        const double d = player->walker().lastLandingDrop();
                        const int band = player->jumpLand();
                        std::printf("jump: landed, drop %.2f -> band %d%s\n",
                                    d, band,
                                    band == 2 ? " (short, no reaction)"
                                              : " (ACTOR_STATE 18, bank group 2)");
                    }
                    if (mv == "MDJUMP01") {
                        const bool went = player->jumpLaunch();
                        std::printf("jump: %s\n", went
                            ? "launched"
                            : "refused (nothing armed, or already off the ground)");
                    }
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
                        // `sub_41C810` answered with something inside
                        // `flt_4BC918`, so MDACTION takes its object arm and
                        // RETURNS - see the `pressAction` gate below. Set from
                        // the scan and not from whether the take succeeded,
                        // because `sub_465D30` refusing lands at `loc_46AFB2`,
                        // which returns too.
                        if (obj >= 0) actionTookObject = true;
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
                            {
                                float op[3] = {0, 0, 0};
                                session.propPos(takeCandidate, op);
                                const auto& objs = voiceLib.objects();
                                const PropModel* pm = takeCandidate >= 0 &&
                                    static_cast<std::size_t>(takeCandidate) < objs.size()
                                    ? propModelFor(objs[static_cast<std::size_t>(takeCandidate)].stem) : nullptr;
                                std::printf("take: GRAB object %d - placement %.1f %.1f %.1f, model %s "
                                            "origin %.2f %.2f %.2f local(+128) %.2f %.2f %.2f, %zu rest corners\n",
                                            takeCandidate, op[0], op[1], op[2],
                                            pm ? "ready" : "MISSING",
                                            pm ? pm->origin[0] : 0.f, pm ? pm->origin[1] : 0.f, pm ? pm->origin[2] : 0.f,
                                            pm ? pm->localOff[0] : 0.f, pm ? pm->localOff[1] : 0.f, pm ? pm->localOff[2] : 0.f,
                                            pm ? pm->rest.corners.size() : 0u);
                            }
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
                        std::printf("take: RELEASE object %d into the bank\n", takeCandidate);
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
                        std::printf("take: MDLETOBJ - RELEASE object %d, put back where it was\n",
                                    takeCandidate);
                        session.putHeldObjectBack();
                        heldInHand = -1;
                        takeCandidate = -1;
                    } else if (mv == "MDSLIDIN") {
                        // 0x0046B7F0, and the channel is what fires it: group
                        // 60's entry [160] is a child of `H_SLDIN` with no
                        // input at all, so it is taken when the door clip
                        // ends, and its GoTo lands on `H_SLIDER` - the riding
                        // pose. The handler's own body is
                        // `sub_438420(slider, 4); player[+404] = 7;
                        // UI_OpenScreen(7, -1, -1, -1)`: he is ABOARD, and the
                        // slider page opens as SCREEN 7, whose param is 1.
                        // What happens next is that page's hook (0x0049D4D0):
                        // with a destination remembered from the sneak the
                        // journey starts at once; with none, the bar reads
                        // Automatique / Manuelle and he chooses.
                        boarding = false;
                        boarded = true;
                        boardCam = 0;
                        player->setActorState(omk::ActorState::SliderMount, "MDSLIDIN");
                        player->setChannelOnly(false);
                        player->clearRootFrame();
                        session.sliders().mountCalled();
                        playerScreen = 7;
                        float at[3] = {0, 0, 0};
                        session.sliders().calledAt(at);
                        std::printf("MDSLIDIN: aboard at %.0f %.0f %.0f - H_SLDIN "
                                    "ended and the channel took its no-input child "
                                    "to H_SLIDER; ACTOR_STATE 7, screen 7 opens\n",
                                    at[0], at[1], at[2]);
                    } else if (mv == "MDSLIDOU") {
                        // 0x0046B890, the mirror of `MDSLIDIN`: group 61's
                        // entry [162] is a child of `H_SLDOUT` with no input,
                        // so it fires when that clip ends, and [163] gotos
                        // `H_STAND`. The handler refuses from anything but
                        // ACTOR_STATE 8 ("bad mode getting out of the slider
                        // !") and leaves the actor at 1.
                        leaving = false;
                        if (takeCam) takeCamRequest(3);      // back to the follow camera
                        player->setActorState(omk::ActorState::Normal, "MDSLIDOU");
                        player->setChannelOnly(false);
                        player->clearRootFrame();
                        std::printf("MDSLIDOU: out and standing - H_SLDOUT ended, "
                                    "the channel gotos H_STAND, ACTOR_STATE 1\n");
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
                // NOTE: his EULER is deliberately not touched while he boards.
                // `MDACTION` and `sub_468FA0` write node+156 and never +420,
                // so the zone scan and everything else that reads his facing
                // keep the way he walked up; only the DRAWN orientation
                // belongs to the vehicle, and that is applied where the model
                // is posed - putting it here moved nothing on screen at all.
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
                // ---- `tab_special_move[3]` = 0x0046AEC0, the MDACTION
                // handler, and it is what makes a HELD action button fire
                // ONCE (todo/next-tasks.md 1).
                //
                // It has no `proc` label - read it with `asmfn.py`. It opens
                // by switching on `[esi+194h]`, the actor's `+404`
                // ACTOR_STATE, over eleven cases indexed `state - 4` through
                // `byte_46B2BC[] = {0,4,4,4,4,4,4,1,4,2,3}`:
                //
                //     state 4, 11   -> arm at loc_46B29F        (unported)
                //     state 13      -> sub_4A9580(actor)        (unported)
                //     state 14      -> Game_RaiseEvent(6, 4)    (unported)
                //     everything else, including 0..3 and 15+ which fall past
                //                      the `ja` -> the object-search arm
                //
                // and the object-search arm refuses on `ACTOR_STATE == 3`.
                //
                // WHY THE ENGINE DOES NOT REPEAT, traced rather than assumed:
                // it is NOT an edge filter. `Cef_TickChannel` reads DirectInput
                // STATE (`sub_43D920`, bit 0x80 = down) and `sub_4A7A20` is a
                // pure bit remap, so the engine queues MDACTION every frame
                // while the button is held, exactly as this port does. The
                // guard is at the END of a SUCCESSFUL action: `sub_465D30`
                // calls `SetPersoBankGroup`, whose memset clears the channel's
                // queue and latches and seeds them with the idle word - so the
                // held word must change before anything matches again.
                //
                // Ported here as the two gates that can be transcribed exactly
                // plus that input memset. What is NOT ported and is labelled
                // so: the three special arms above, and the BANK the engine
                // switches to (it comes from the object, in `sub_465D30`).
                const int actorState = player ? static_cast<int>(player->state()) : -1;
                const bool stateAllowsAction =
                    actorState != 3 &&                       // the arm's own refusal
                    actorState != 4 && actorState != 11 &&   // loc_46B29F
                    actorState != 13 && actorState != 14;    // the other two arms
                if (actionFromMove && !stateAllowsAction)
                    std::printf("action: refused - ACTOR_STATE %d takes another arm of "
                                "tab_special_move[3]\n", actorState);
                // ---- ONE ACTIVATION PER PRESS ----------------------
                //
                // `MDACTION` fires EVERY frame the button is held - that is
                // the engine, not a fault, and `Cef_TickChannel`'s chain loop
                // (`readable/src/29_win32.c`, the `(v17 & 0x10) && (v17 &
                // 0x200000)` arm) re-applies it from the raw device word on
                // every tick. What the engine does NOT do is run the zone's
                // activate script every one of those frames: `Game_RaiseEvent
                // (6, 4)` is raised by the ACTOR's state handlers, once on the
                // way INTO the action state, and `Script_Pump`'s slot machine
                // then runs the arm once.
                //
                // The slot machine is not modelled to that depth here, so this
                // stands in for it and is labelled as the RECONSTRUCTION it
                // is: the world's action is honoured once per press, and the
                // bit must go up before another is. The take's own second and
                // third presses are unaffected - `MDPUTSNK` (entry 55, input
                // 0x10) and `MDNOTAKE` (entry 56, 0x20) are transitions inside
                // the take bank, reached by the `.CTL` machine and not by this
                // gate.
                //
                // This replaces an earlier guard that installed the take bank
                // (group 41) on ANY successful press. That was wrong twice
                // over: `sub_465D30` reaches `SetPersoBankGroup` only when its
                // object scan FOUND something - a press that merely activates
                // a zone never gets there - and the take pipeline above
                // already installs 41/143/600 when it does. Its symptom was a
                // reader finishing a shop seller's conversation and watching
                // the whole take graph run on nothing: reach, wait, put back.
                // ---- AND THE PRESS ONLY REACHES THE ZONES IF MDACTION
                // ---- FOUND NOTHING TO TAKE.
                //
                // `Game_RaiseEvent(6, 4)` - the thing `Script_Pump` sees as
                // `dword_4E6C90` - is raised from INSIDE `MDACTION`
                // (0x0046AEC0), on one arm and one only. The handler's exits:
                //
                //     scan hit, in reach -> sub_465D30 ... retn   (the take)
                //     sub_465D30 refused -> loc_46AFB2   ... retn
                //     something HELD     -> loc_46AFD0   ... retn
                //     scan MISS / out of reach / state 3 -> loc_46AFF8, the
                //         slider arm; no slider -> loc_46B281:
                //             sub_452280 (talk to a walker) ... else
                //             sub_467950 -> Game_RaiseEvent(6, 4)
                //
                // So a press that finds an object never becomes a zone press
                // at all, and `Script_Pump` step 2 - "a press was registered,
                // no zone activated, the hand is empty" - cannot fire on it.
                //
                // THIS IS `todo/omk-play.md` 92, and it is the link that was
                // never on the list: seven others were read and cleared while
                // this one was assumed, because the port raised the press off
                // the special-move list rather than off the arm. The symptom
                // was a reader taking food out of Kay'l's kitchen cupboard and
                // hearing *"Je ne vois pas quoi faire avec ca"* every time -
                // one of GLOBAL script 10's seven, posted because the cupboard
                // zone is a spent ONE-SHOT and nothing else could consume the
                // press. With the arm modelled, the line goes back to what it
                // is for: a press with nothing in front of you.
                if (actionFromMove && stateAllowsAction && !session.dialogOpen() &&
                    !actionSpent && !actionTookObject) {
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
                    // THE REPEAT GUARD, and it fires on the SUCCESSFUL press
                    // only - which is the engine's rule, not a convenience:
                    // `sub_465D30` reaches `SetPersoBankGroup` only when the
                    // action actually did something, so a press that finds
                    // nothing leaves the latch alone and the next frame tries
                    // again. That is why holding the button at a door opens it
                    // once, while holding it in the open street keeps looking.
                    const bool did = session.pressAction();
                    // THE REPEAT GUARD IS NOT PORTED, and the attempt is
                    // recorded because it half-worked, which is the dangerous
                    // kind. `sub_465D30` ends with
                    //
                    //     SetPersoBankGroup(channel,
                    //         Cef_FindGroupById(bank, dy <= 27.472441 ? 143 : 41))
                    //
                    // and it is that STATE CHANGE - not the memset beside it -
                    // that stops the `H_STAND -> 24` per-tick entry matching.
                    // Doing only the switch takes the count from 20 holds to 1
                    // - and PARKS the player in `.CTL` state 54 `H_WAITOB` for
                    // ever, because the engine leaves that bank again when the
                    // action completes and this port has no such path. A
                    // second press then never works at all, which is worse
                    // than the repeat. Measured both ways; see
                    // `todo/next-tasks.md` 1.
                    // The press is spent whether or not it found anything:
                    // the engine's event is raised once on the way into the
                    // action state, and a second one needs the button to go up
                    // and come down again. A press that reaches nothing still
                    // costs the press - which is also why holding the button
                    // in the open street does not keep re-running a script.
                    actionSpent = true;
                    if (did)
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
        {
            // the engine's music level, every frame (the ramp, the dialogue
            // duck, the fade-in) - logged when it moves by a dB or more
            static double musicDbTold = -1.0;
            const double db = session.musicAttenuationDb();
            front.setMusicGain(session.musicGain());
            if (std::fabs(db - musicDbTold) >= 1.0) {
                musicDbTold = db;
                std::printf("audio: music attenuation %.0f dB (gain %.2f)%s\n", db,
                            static_cast<double>(session.musicGain()),
                            session.dialogOpen() ? " - a conversation ducks it 10" : "");
            }
        }
        if (session.musicTrack() != playingTrack) {
            playingTrack = session.musicTrack();
            std::printf("audio: music switch to %d - the stream is flushed; "
                        "one-shots already playing are NOT\n", playingTrack);
            front.flushAudio();
            if (music.play(fs, adpcmTables, playingTrack, session.musicLoops()))
                std::printf("music: track %d, %.1f s%s\n", music.track(),
                            music.seconds(), music.looping() ? ", looping" : "");
        }
        // ---- AND THE PAUSE SCREEN STOPS THE SOUND ------------------------
        //
        // Read out of screen 31's own open and close callbacks (0x004ADDB0 /
        // 0x004ADEB0), which do far more than set the pause flag: after
        // `mov dword_4E9728, 1` the open calls FOUR suspend routines, and the
        // close calls their four partners in the same order.
        //
        //     sub_46C290 / sub_46C2C0   walk the sound bank at unk_53B36C and
        //                               stop / restart every buffer in it
        //     sub_42BA70 / sub_42BA90   one streaming handle (word_4EB5F8):
        //                               `sub_46CAE0(h)` to stop, and
        //                               `sub_46CBB0(h, dword_4EB614)` to
        //                               restart FROM THE SAVED POSITION
        //     sub_42BB10 / sub_42BB30   the same for word_4EA7B8, saved in
        //                               dword_4EB610
        //     sub_412120 / sub_412140   `timeGetTime` re-baselined, so the
        //                               paused interval is never integrated
        //
        // Both stream pairs guard on `handle != 0xFFFF` and both restart from
        // a position stored on the way in - so it is a SUSPEND, not a stop:
        // the music resumes where it was rather than from the top.
        //
        // Here that is two things, because the port pulls a second of music
        // ahead into the device: stop pulling, and FLUSH what is already
        // queued - otherwise the pause is silent-eventually rather than
        // silent. `MusicPlayer::pos_` is untouched while nothing pulls, so it
        // is already the engine's saved position and the resume needs nothing.
        if (uiPause != musicPaused) {
            musicPaused = uiPause;
            if (musicPaused) front.flushAudio();
            std::printf("audio: the pause screen %s the sound (screen 31's own "
                        "open/close callbacks suspend the bank and both streams)\n",
                        musicPaused ? "SUSPENDS" : "resumes");
        }
        // Keep about a second of it in the device. The player decides what
        // comes next - including whether the track wraps - so all this does
        // is ask for more and hand it over.
        if (!musicPaused && music.playing() && front.queuedSeconds() < 1.0) {
            std::vector<float> chunk;
            music.pull(chunk, 44100);
            {
                static double musicPeakTold = 0.0; static float musicPeak = 0.0f; static std::size_t musicSamples = 0;
                for (const float v : chunk) musicPeak = std::max(musicPeak, std::fabs(v));
                musicSamples += chunk.size();
                if (musicSamples >= 44100u * 2u * 10u) {   // every ten seconds
                    std::printf("audio: music peak %.3f over the last %.0f s\n",
                                static_cast<double>(musicPeak), musicSamples / 88200.0);
                    musicPeak = 0.0f; musicSamples = 0; (void)musicPeakTold;
                }
            }
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
            // THE PRESS BELONGS TO THE CONVERSATION. While a dialogue is up
            // the action bit is the interface's - `Dialog_TickUI` takes it -
            // and the world must not see the same press again when the
            // conversation closes. Without this the ENTER that dismisses the
            // last line was still down on the next frame, `MDACTION` matched
            // `H_STAND -> 24`, and the seller was talked to a second time the
            // instant he had finished. Marking it spent here means the button
            // has to go up first, which is the same rule the gate above
            // applies to two presses in the world.
            if (bits & omk::kUiConfirm) actionSpent = true;
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
                    // READY MEANS THE MODEL IS LOADED, NOT THAT A CAMERA SOLVE
                    // FOUND HIM. `Morph_Play` runs the line on the speaker's
                    // own node whatever the cameras are; the solve exists
                    // only to PLACE a speaker nothing else places, and
                    // `speakerSolved` carries that for the staging below.
                    // Gating readiness on the solve meant a conversation
                    // framed entirely off its speaker - the transcan's
                    // advert, whose one camera is [2,2] - cast no rays,
                    // solved nothing, and never loaded the line's .3DM: the
                    // hologram played its scene clip through the whole
                    // advert, "frame 95 of 0". A reader: *the character on
                    // transcan is not animated correctly*.
                    speakerReady = true;
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

        // ---- DIALOGUE MODE, and the bug its absence caused ---------------
        //
        // `Actor_EnterDialogueMode` (0x00468DE0) and `Actor_LeaveDialogueMode`
        // (0x00468E80) bracket every conversation, and both end with a BANK
        // SWITCH on the player's channel:
        //
        //     enter: Perso_SetInputEnabled(channel, 0)
        //            SetPersoBankGroup(channel, Cef_FindGroupById(bank, 400))
        //     leave: SetPersoBankGroup(channel, Cef_FindGroupById(bank, 100))
        //            Perso_SetInputEnabled(channel, 1)
        //
        // H1AVNT has both (`engine/tools/dialog_bank.cpp`): group id 400 is
        // the 15-entry dialogue group, and id 100 is the bank's DEFAULT group
        // (`flags & 1`), 44 entries, default `H_STAND`/`MDSTAND` - adventure
        // mode. `SetPersoBankGroup`'s memset clears the channel's queue and
        // latches and its `gotoMove` re-enters that default, which is the
        // SAME guard `sub_465D30` uses at the end of a successful take: the
        // held word must CHANGE before anything matches again.
        //
        // That is what stops the ENTER which dismisses a conversation's last
        // line from falling straight through into the take. `ActorRuntime`
        // has carried both functions since the ACTOR_STATE machine was
        // ported, but NOTHING here called them - so the viewer never entered
        // dialogue mode, never left it, and a reader who finished a shop
        // seller's dialogue with Enter still held watched the take graph run
        // (MDACTION -> MDGETOBJ -> H_WAITOB -> MDPUTSNK) on the next frame.
        //
        // The transition is tested HERE, after the block that may have ended
        // the conversation, so the leave lands on the SAME frame the dialogue
        // closes - one frame later would be one frame too late, because the
        // MDACTION gate above runs before this point in the next frame.
        // ---- SHOOT MODE, the same shape one subsystem over ---------------
        //
        // `shoot.begin` / `shoot.end` (ops 80/81) are decisions the Session
        // makes (`actor/shootmode.h`); what a frontend owes them is the three
        // installs `Shoot_Enter` does and `Shoot_Leave` undoes: the player's
        // `ACTOR_STATE` 3 with `.CTL` group 200, input scheme 2, and camera
        // mode 4. Nothing here calls the AI - `actor/shoot.h`'s brains are
        // still unwired for the generic arm (`todo/standing-unknowns.md` 2) -
        // so what this draws is the MODE, not a gunfight.
        if (session.shootMode().active() != shootMode) {
            shootMode = session.shootMode().active();
            if (player) {
                if (shootMode) player->enterShootMode();
                else           player->leaveShootMode();
            }
            in.installScheme(shootMode ? omk::ShootMode::kInputScheme : 0);
            std::printf("frame %ld: SHOOT MODE %s - weapon slot %d (object %d), "
                        "HUD screen %d, library %s, ACTOR_STATE %d, scheme %d\n",
                        n, shootMode ? "ENTER (Shoot_Enter)" : "LEAVE (Shoot_Leave)",
                        session.shootMode().weaponSlot(),
                        session.shootMode().weaponObject(),
                        session.shootMode().hudScreen(),
                        session.shootMode().library(),
                        player ? static_cast<int>(player->state()) : -1,
                        in.group());
        }

        if (session.dialogOpen() != dialogMode) {
            dialogMode = session.dialogOpen();
            if (player) {
                if (dialogMode) player->enterDialogueMode();
                else            player->leaveDialogueMode();
            }
            std::printf("frame %ld: dialogue mode %s - bank group %d, ACTOR_STATE %d\n",
                        n,
                        dialogMode ? "ENTER (Actor_EnterDialogueMode)"
                                   : "LEAVE (Actor_LeaveDialogueMode)",
                        dialogMode ? 400 : 100,
                        player ? static_cast<int>(player->state()) : -1);
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
            // The load panel's directory, so its rows have something to be.
            // `Ui_BuildLoadPanel` calls `SaveDir_Build` itself in the OPEN
            // callback, which is why it is read here and not once at start-up:
            // a save written during this run has to show up next time the
            // panel opens.
            loadPanelState = omk::buildLoadPanel(
                omk::saveDirectory(omk::readSaveFile(savesPath, fr + "/IAM/GAMES").empty()
                                       ? fr + "/IAM/GAMES" : savesPath, w));
            loadPanelState.path =
                omk::readSaveFile(savesPath, fr + "/IAM/GAMES").empty()
                    ? fr + "/IAM/GAMES" : savesPath;
            fresh->attachLoadPanel(&loadPanelState);
            // `Ui_BuildLoadPanel` runs in the OPEN callback and lays the
            // shared panel out differently for each of its two screens - 29
            // puts `Charger une partie` in the top slot and hides `Nouvelle
            // partie`, 30 does the opposite. Re-applied per open for that
            // reason.
            omk::applyLoadPanelLayout(w, want);
            // The player's ANNEAUX, which the save screen's `Sauvegarde`
            // refuses without. `Actor_GetProperty` case 5 is the player
            // record's +174, and `GameState::rings` reads it.
            fresh->setRings(state.rings());
            // whatever is held on the frame it opens does not count as input
            // to it (see the gate in the walk's dispatch below)
            screenOpenBits = bits;
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
                // THE SLIDER PAGE'S HOOK, 0x0049D4D0, on the frame screen 7
                // opens: `if (dword_6A17CC != -1)` resolve it with
                // `sub_40E630` and call `sub_452570` at once - the journey
                // starts without the menu being used. That is what "called
                // by a destination -> transported directly" is.
                if (want == 7 && boarded && calledDestination >= 0) {
                    walk->requestTravel(calledDestination);
                    std::printf("slider: screen 7 - a destination was "
                                "remembered (row %d), the journey starts\n",
                                calledDestination);
                }
                std::printf("frame %ld: screen %d %s - arrows move, ENTER confirms, "
                            "TAB closes\n", n, openScreen,
                            fromScript ? "is asking" : "opened by the player");
                // The screen's own sounds, by slot. Which slot is which is
                // `sub_482FE0`'s answer - it dispatches on the INPUT BIT - not
                // a guess from the file names. Nothing plays when a screen
                // opens: the engine has no such slot, and the one this used to
                // play was the MOVE sound fired at the wrong moment.
                sndMove    = loadSlot(openScreen, omk::UiWidgets::kSoundMove);
                sndConfirm = loadSlot(openScreen, omk::UiWidgets::kSoundConfirm);
                sndBack    = loadSlot(openScreen, omk::UiWidgets::kSoundBack);
                // ...and the screen's OWN sound, slot 4, played as it comes
                // up. Only SAVE GAME and PAUSE GAME carry one.
                if (const auto own = loadSlot(openScreen, omk::UiWidgets::kSoundScreen);
                    !own.empty()) {
                    blip(own);
                    std::printf("screen %d: its own sound (slot 4, `%s`)\n",
                                openScreen,
                                w.soundName(openScreen, omk::UiWidgets::kSoundScreen).c_str());
                }
                // ...AND THE SNEAK CALL ANSWERS ITSELF ON THE WAY IN.
                // `Ui_OpenSneakFamily`'s param-2 arm ends `call sub_42B560`,
                // so the screen hands its preset -1 back the moment it is up
                // and the parked script runs on with the device still on
                // screen. Without this the port waited for a person to close
                // it and the call's own `dialog.start` never ran - the game
                // showed an empty videophone.
                if ((fromScript || callHarness) && openScreen == kScreenVideophone) {
                    std::printf("screen %d VIDEOPHONE answers itself (-1, "
                                "`UI_SendAnswer` in its own open callback) - "
                                "the script runs on with the call up\n",
                                openScreen);
                    session.answerUi(-1);
                    videophoneCall = true;
                    callHarness = false;
                    if (callPending >= 0) {
                        session.harnessStartDialogue(callPending);
                        callPending = -1;
                    }
                }
            }
        }

        if (walk) {
            // What the person typed goes to the field before the navigation
            // bits, because `Confirmer` opens by testing the field's cursor
            // and writes nothing when it is empty (docs/UI.md) - so a confirm
            // in the same frame as the last letter must see the letter.
            // ...and the field may CONSUME the frame. `Ui_DispatchInput`
            // returns at the first hook that answers 1, so a key that is both
            // a character and a bit - RETURN is 13 and confirm - must not do
            // both. Without this gate ENTER moved the focus to the buttons
            // and then confirmed one of them in the same press, which starts
            // a game the player never asked for.
            const bool ate = !host.text.empty() && walk->typeName(host.text);
            // A KEY ALREADY DOWN WHEN THE SCREEN OPENED IS NOT A PRESS.
            //
            // `Ui_BeginScreen` installs the 0x203F repeat mask and
            // `Game_Frame` edge-filters against it, so the interface sees
            // EDGES. The action button that activates a save point is still
            // held on the frame `ui.open 30` puts the screen up, and without
            // this it is read again as that screen's confirm - so the save
            // menu opened and descended into the slot list in one press,
            // which is what a reader met ("when I interact with the save
            // point I have directly this"). The bits are swallowed until they
            // are RELEASED.
            std::uint32_t uiBits = bits;
            if (screenOpenBits & uiBits) {
                screenOpenBits &= uiBits;    // still down: keep swallowing
                uiBits = 0;
            } else {
                screenOpenBits = 0;
            }
            // ...AND A PANEL CAN OPT OUT OF INPUT ALTOGETHER. `panel+72 & 8`,
            // which `Ui_ScreenInput` tests before it dispatches anything, and
            // the VIDEOPHONE's panel is the one in the tree that sets it. So
            // a sneak CALL swallows nothing: every press goes past the device
            // to the conversation running over it.
            if (!walk->takesInput()) uiBits = 0;
            if (uiBits) {
                const int wasSel = walk->selection(), wasList = walk->currentList();
                // THE SOUND IS NOT GATED ON IT, and that is read rather than
                // assumed: `Ui_ScreenInput` (0x0042A0F0) calls
                // `Ui_DispatchInput` and then `sub_482FE0(screen)`
                // UNCONDITIONALLY, so the slot is picked from the live input
                // word whatever the dispatch did. A frame the field ate still
                // clicks - RETURN in the name box plays the confirm sound and
                // presses nothing.
                if (!ate) walk->press(uiBits);
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
            // `dword_4C09B4`, taken before the walk is dropped.  `Charger`
            // does not load - it records a request and closes the screen, and
            // `sub_408410` consumes it at the top of the NEXT script pump.
            // Kept here and served below for the same reason: a load in the
            // middle of a screen's own dispatch would free the panel it is
            // standing in.
            if (const int req = walk->takePendingLoad(); req >= 0)
                pendingLoadSlot = req;
            // ...and the QUIT the pause screen's `Oui` asks for, taken here
            // for the same reason: `dword_4E6C9C` is a request the engine
            // serves at the top of the next `Script_Pump(1)`, not work the
            // callback does.
            if (walk->takeQuitRequest()) quitRequested = true;
            // ...and the SAVE, which the callback performs itself rather than
            // deferring: `Game_WriteSave(slot)` right after the charge, and
            // then the screen closes. So this is served here and not at the
            // pump.
            // `Detruire`'s confirm: `SaveDir_ClearSlot` (0x004090A0), which
            // is ONE zero byte over the slot's name. The day, the DB and the
            // picture stay on disk - an empty slot is an empty NAME and
            // nothing else (GAME_STATE 8b).
            if (const int slot = walk->takePendingClear(); slot >= 0) {
                auto file = omk::readSaveFile(savesPath, fr + "/IAM/GAMES");
                if (omk::clearSaveSlot(file, slot) &&
                    omk::writeSaveFile(savesPath, file)) {
                    std::printf("detruire: slot %d cleared\n", slot);
                    loadPanelState = omk::buildLoadPanel(
                        omk::saveDirectory(loadPanelState.path, w));
                } else {
                    std::fprintf(stderr, "detruire: slot %d not cleared\n", slot);
                }
            }
            // ---- THE SLIDER'S TRAVEL --------------------------------
            //
            // `sub_40E630(row)` then `sub_452570(&point)`, and the pair is
            // three things the walk cannot do. `sub_40E630` counts ENABLED
            // destinations to the row, and when the record's `+2` AREA is not
            // the resident one it frees both slots' contexts, `Area_Load`s
            // that area, re-attaches the player, rebinds his facing matrix
            // and raises event 9 - a SYNCHRONOUS load, the same shape the
            // save path takes here, not the staged `area.goto` transition.
            // Only then does it look for a position, and it looks in the
            // newly resident chunk's ADDRESS table for the entry whose `+14`
            // equals the record's own bit.
            //
            // `sub_452570`'s ARRIVE arm is what places him: the position,
            // velocities zeroed, the facing rebuilt from his own Euler (so
            // the address's heading is NOT used), `Walk_ProbeGround`,
            // ACTOR_STATE 1, camera mode 0 and `Screen_Fade(0)` - which is
            // `Screen_StartColorFade` mode 4 over 60 frames.
            //
            // Its OTHER arm, the one that runs when a slider POOL exists,
            // reserves a real slider and fades the other way instead. That is
            // the RIDE, and it is step 2 of `todo/slider.md`; what is here is
            // the arm the engine itself takes wherever there is no circuit.
            // ---- "Appel du slider" ----------------------------------
            //
            // 0x0049D400: `sub_452570` on the player's own position, and
            // `dword_6A17CC` untouched - a call with NO destination. The only
            // reset of that global is in the new-game path (`sub_49B400`),
            // so a destination chosen earlier would still be remembered by
            // the engine; the port keeps that faithfully rather than
            // clearing it here.
            if (walk->takeCallHere()) {
                float me[3] = {session.playerPos()[0], session.playerPos()[1],
                               session.playerPos()[2]};
                if (session.sliders().callSlider(me))
                    std::printf("slider: Appel du slider - a slider is COMING "
                                "to %.0f %.0f %.0f, no destination\n",
                                me[0], me[1], me[2]);
                else
                    std::printf("slider: Appel du slider, but no vehicle lane "
                                "here - the call FAILS (text 42)\n");
            }
            // ---- "Manuelle" -----------------------------------------
            //
            // 0x0049D4A0: `sub_457040(slider, player)` - ACTOR_STATE 7 and
            // `Slider_TickRide` takes the body. The flight model, from where
            // the vehicle stands.
            if (walk->takeManual() && boarded) {
                float at[3];
                if (session.sliders().calledAt(at)) {
                    omk::SliderRide r;
                    r.x = at[0]; r.y = at[1]; r.z = at[2];
                    r.yaw = session.sliders().calledYaw();
                    ride = r;
                    std::printf("slider: Manuelle - `sub_457040`, the controls "
                                "are his\n");
                }
            }
            if (const int row = walk->takeTravel(); row >= 0) {
                std::vector<const omk::Destination*> known;
                for (const auto& d : destinations)
                    if (state.bit(omk::StateArray::AddressEnabled, d.bit))
                        known.push_back(&d);
                if (row >= static_cast<int>(known.size())) {
                    std::printf("slider: row %d is past the %zu enabled "
                                "destinations\n", row, known.size());
                } else if (!walk->travelIsJourney()) {
                    // ---- THE SNEAK'S PAGE CALLS ONE ---------------------
                    //
                    // Screen 9's `param` is 0, so `sub_49BC60` takes the arm
                    // that uses the PLAYER'S own position: a slider is armed
                    // and comes to him, and the row he chose is remembered
                    // (`dword_6A17CC = tag`) for the arrival camera. The
                    // JOURNEY is a second confirm, on screen 7, once he is
                    // aboard - which is why `MDSLIDIN` ends in
                    // `UI_OpenScreen(7, ...)`.
                    const auto* d = known[static_cast<std::size_t>(row)];
                    calledDestination = row;
                    float me[3] = {session.playerPos()[0], session.playerPos()[1],
                                   session.playerPos()[2]};
                    if (session.sliders().callSlider(me))
                        std::printf("slider: '%s' chosen - a slider is COMING "
                                    "to %.0f %.0f %.0f. Wait for it, then walk "
                                    "to it and press the action button\n",
                                    d->name.c_str(), me[0], me[1], me[2]);
                    else
                        std::printf("slider: '%s' chosen, but there is no "
                                    "vehicle lane here - the call FAILS, which "
                                    "is what the engine does too (text 42)\n",
                                    d->name.c_str());
                } else if (boarded && known[static_cast<std::size_t>(row)]->area
                                       == session.residentSlot(session.activeSlot()).area) {
                    // ...the RESIDENT area, not `state.currentArea()`: the
                    // drive is possible exactly when the destination is in
                    // the world the circuit belongs to, and the harness's
                    // `--area` leaves the DB's current area at the save's
                    // (237) while the world is Anekbah (0). Compared against
                    // the DB, every journey in the fixture took the
                    // other-area arm and was placed instead of driven.
                    // ---- THE JOURNEY, inside this area --------------------
                    //
                    // Screen 7's row confirm: `sub_40E630(tag)` finds the
                    // address, `sub_452570` with a slider already assigned
                    // sets it to state 6, and `sub_456530` drives it to the
                    // lane nearest that address. He is on it the whole way.
                    const auto* d = known[static_cast<std::size_t>(row)];
                    const auto& rs = session.residentSlot(session.activeSlot());
                    const omk::Address* ad = nullptr;
                    for (const auto& x : rs.addresses) if (x.id == d->bit) ad = &x;
                    if (ad && session.sliders().sendCalledTo(ad->pos)) {
                        journeyTo = d->bit;
                        std::printf("slider: JOURNEY to '%s' - state 6, driving "
                                    "to the lane nearest address %d\n",
                                    d->name.c_str(), d->bit);
                    } else {
                        std::printf("slider: '%s' has no road within reach - "
                                    "the journey FAILS (text 42)\n", d->name.c_str());
                    }
                } else {
                    // ---- THE JOURNEY, to another AREA -------------------
                    //
                    // `sub_40E630` loads the area first and only then looks
                    // for a lane; the circuit changes under the vehicle. Not
                    // driven here: the port loads the area and places him,
                    // which is the arrive arm, and says so.
                    const auto* d = known[static_cast<std::size_t>(row)];
                    const int wasArea = state.currentArea();
                    // ...FROM ABOARD, and it is not a bare placement: after
                    // `sub_40E630`'s load, `sub_452570` runs against the NEW
                    // pool - the lane nearest the destination, `sub_452CC0`
                    // relinking a vehicle THERE (not at the top of the lane),
                    // state 6 - so `sub_456530` case 6 finds it within its 117
                    // at once and it ARRIVES: the stop, the exit clip, camera
                    // 17, the release. This dropped him at the address bare.
                    const bool wasAboard = boarded;
                    if (d->area != wasArea) {
                        state.setCurrentArea(static_cast<std::int16_t>(d->area));
                        session.loadArea(d->area);
                    }
                    bool arrived = false;
                    if (wasAboard) {
                        const auto& rs2 = session.residentSlot(session.activeSlot());
                        const omk::Address* ad2 = nullptr;
                        for (const auto& x : rs2.addresses) if (x.id == d->bit) ad2 = &x;
                        if (ad2 && session.sliders().arriveAt(ad2->pos)) {
                            journeyTo = d->bit;
                            arrived = true;
                            std::printf("slider: JOURNEY to '%s' in area %d - loaded, "
                                        "the slider relinked at the lane nearest address "
                                        "%d with him aboard, state 6\n",
                                        d->name.c_str(), d->area, d->bit);
                        } else {
                            session.sliders().dismountCalled();
                            boarded = false;
                            std::printf("slider: '%s' - area %d has no road within reach "
                                        "of address %d; placing him instead\n",
                                        d->name.c_str(), d->area, d->bit);
                        }
                    }
                    // The address whose `+14` is this record's own bit - the
                    // one number that joins the two tables.
                    const bool changed = d->area != wasArea;
                    const bool placed = arrived ? true : session.placeActorAt(d->bit);
                    session.requestCamera(0, 0);
                    // `Screen_Fade(0)` is `fade.from_black` - it CLEARS the
                    // load's black at the exit (case 8 calls it every tick of
                    // H_SLDOUT), not a sixty-frame dip. The dip was the old
                    // bare placement's, and a reader saw it on the aboard
                    // path: "a fade effect that shouldn't be here".
                    if (!arrived) session.startColourFade(4, 0u, 60.0f);
                    // ONLY WHEN THE AREA ACTUALLY CHANGED. Dropping
                    // `playerReady` asks the hand-over gate to build the
                    // player again for a new area's set, which is right after
                    // a load and WRONG without one: nothing rebuilds him, so
                    // there is no player left at all and control goes with
                    // him. A reader met exactly that - *"calling a slider
                    // with the sneak teleports me and makes the character
                    // disappear"* - and every headless test of this path had
                    // taken the other branch, because the fixture save's area
                    // is 237 and every destination is in 0, 1, 64 or 101.
                    // Travelling INSIDE the city you are standing in is the
                    // common case and was the untested one.
                    // ONLY WHEN THERE IS NO CONTROLLER YET. The hand-over gate
                    // that rebuilds him and sets `playerReady` runs on
                    // `!player`; with a controller already alive an area
                    // change keeps him ("he keeps walking", the walk-through
                    // path above). Dropping `playerReady` here with `player`
                    // alive left it false for ever, and the model eviction
                    // keeps his model only while `playerReady` - so in a city
                    // where no staged actor wears HO1_FN (Qalisar) Kay'l was
                    // thrown away on the first frame. The reader, twice:
                    // *"the character disappearing"*.
                    if (changed && !player) {
                        playerReady = false; adventure = false;
                        forceAdventure = true;
                    }
                    if (player && !arrived) {
                        const float at[3] = {session.playerPos()[0],
                                             session.playerPos()[1],
                                             session.playerPos()[2]};
                        player->placeAt(at, session.playerYaw());
                    }
                    if (!arrived)
                    std::printf("slider: '%s' - area %d -> %d, address %d %s"
                                " at %.0f %.0f %.0f facing %.0f\n",
                                d->name.c_str(), wasArea, d->area, d->bit,
                                placed ? "placed him" : "IS NOT IN THAT AREA",
                                session.playerPos()[0], session.playerPos()[1],
                                session.playerPos()[2], session.playerYaw());
                }
            }
            if (const int slot = walk->takePendingSave(); slot >= 0) {
                // ONE RING, through `Actor_GetProperty` / `Actor_SetProperty`
                // (events 44 and 45, property 5) exactly as the callback
                // does - and only when it has one to spend, which is the
                // `jz` that skips the decrement without skipping the write.
                const int had = state.rings();
                if (had > 0) state.setRings(had - 1);
                state.setCurrentArea(static_cast<std::int16_t>(session.activeArea()));
                state.setCurrentScene(static_cast<std::int16_t>(
                    session.residentSlot(session.activeSlot()).scene));
                state.setPlacement(session.playerPos(), session.playerYaw());
                omk::SaveSlot out;
                out.name = loadedName.empty() ? std::string("OMK") : loadedName;
                out.day  = state.clockDay();
                out.time = state.clock();
                out.state = state;
                const auto thumb = omk::thumbFromRgb565(fb.px, fb.w, fb.h);
                auto file = omk::readSaveFile(savesPath, fr + "/IAM/GAMES");
                if (file.size() < omk::kSaveFileSize)
                    file = omk::blankSaveFile(saveSettings ? *saveSettings
                                                           : omk::defaultSettingsBlock());
                if (saveSettings) omk::putSettings(file, *saveSettings);
                if (omk::writeSaveSlot(file, slot, out, thumb) &&
                    omk::writeSaveFile(savesPath, file))
                    std::printf("save: slot %d written - '%s', %s %s, area %d "
                                "scene %d; %d anneau%s left\n", slot,
                                out.name.c_str(), omk::formatDate(out.day).c_str(),
                                omk::formatTime(out.time).c_str(),
                                state.currentArea(), state.currentScene(),
                                state.rings(), state.rings() == 1 ? "" : "x");
                else
                    std::fprintf(stderr, "save: slot %d could not be written\n", slot);
            }
            const bool leaving = walk->answer() >= 0 || walk->closed();
            // THE KEY THAT CLOSED THE SCREEN IS NOT THE WORLD'S ACTION.
            //
            // The mirror of the gate at the open. The world's action button
            // is HELD rather than edged - "spent until the bit goes up" - but
            // while a screen is up the world never reaches that gate, so
            // `actionSpent` is still false when the screen closes. The Enter
            // that pressed `Annuler` is then read as a fresh press on the
            // save point the player is standing on, and the menu reopens at
            // once. A reader met this here and had met it elsewhere in the
            // game: "it is like the enter pressed event continues to be
            // triggered while the button is not released".
            //
            // Spending it costs nothing when the key is already up: the gate
            // above clears `actionSpent` on the first frame the bit is not
            // held.
            if (leaving || walk->answer() >= 0 || walk->closed())
                actionSpent = true;
            // THE PAUSE SCREEN CLOSES ON ITS OWN TERMS, and they are not
            // the sneak's. Its close callback (0x004ADEB0) clears the pause
            // flag, restores the sound volume it saved at the open, undoes
            // the four subsystem pauses and `Sleep`s 500 ms; it raises no
            // event 26 and frees no object list, because nothing was opened.
            // Sending it through the branch below would have raised the
            // sneak's close event and shut a list that was never open.
            if (leaving && openScreen == kScreenPause) {
                std::printf("screen %d PAUSE GAME closed - the world runs "
                            "again\n", openScreen);
                walk.reset();
                openScreen = -1;
                screenFromScript = true;
            } else if (leaving && !screenFromScript) {
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
            // ...AND THE SNEAK CALL CLOSES WHEN THE CALL IS OVER.
            //
            // What is READ: `Ui_CloseSneakFamily`'s param-2 arm refuses the
            // first attempt - it clears `dword_670BF0`, resumes the player
            // (`sub_466B60`) and starts oscillator 5 for 100 ms, a closing
            // ANIMATION - and closes on the next one. What is NOT read is
            // what makes the attempt: no VM opcode closes a screen, no other
            // screen's open closes this one (only `UI_LoadScreen(34)`, SHOOT
            // HUMAN, does), and the function has no direct caller because it
            // is a dword in the screen table.
            //
            // So this is a RECONSTRUCTION, and it is labelled as one: the
            // call closes when the conversation opened over it ends. It is
            // what the script requires - SCENE 53 runs `dialog.start 386`
            // over the device and then `dialog.start 388` in the world, and
            // 388 cannot play through a videophone - and what a capture
            // shows. The alternative, that the player presses a key to hang
            // up, is not excluded by anything here.
            //
            // A CALL IS A CONVERSATION **OR** A VOICE-OVER. Two of the ten
            // sites are `media.play 534` rather than `dialog.start`, and a
            // close keyed on the dialogue alone would leave those two on
            // screen for ever.
            if (walk && openScreen == kScreenVideophone && videophoneCall) {
                if (session.dialogOpen() || mediaTextFrames > 0)
                    videophoneSpoke = true;
                else if (videophoneSpoke) {
                    std::printf("screen %d VIDEOPHONE: the call is over - "
                                "closing (reconstruction: the engine's own "
                                "trigger is not read)\n", openScreen);
                    walk.reset();
                    openScreen = -1;
                    screenFromScript = true;
                    videophoneCall = videophoneSpoke = false;
                }
            }
        }

        // ---- THE QUIT `Quitter le jeu` ASKED FOR, served between pumps
        //
        // `Script_Pump(1)` (0x00407DC0) opens with
        //
        //     if (dword_4E6C9C) { Script_Pump(3);      // tear the game down
        //                         dword_4E6C9C = 0;
        //                         Script_Pump(2);      // Game_NewGame
        //                         Screen_FadeFromColor(0xFFFFFF, 15, 0); }
        //
        // and `Script_Pump(2)` is `Game_NewGame` (0x0040E060) - reset the
        // session, load `IAM\START` over a zeroed DB, apply it. So the
        // engine does NOT exit here: `Quitter le jeu` ends the GAME and
        // starts a fresh one, which walks straight back out through AREA
        // 118's startup script into `ui.open 29`, the start menu. Quitting
        // the PROGRAM is the start menu's own `Quitter`, a different item on
        // a different screen.
        //
        // **The port ends the run instead, and that is a gap rather than a
        // reading.** `omk-play`'s whole boot - the data root, the tables, the
        // Session, the movies, the first area - is `main`'s body, not a
        // function that can be called twice, so there is nothing here to
        // restart into. The request, the flag and the two menu items are all
        // the engine's; only what happens after the fade is missing, and the
        // line below says so rather than pretending the run ended for the
        // reader's own reason.
        if (quitRequested) {
            std::printf("pause: `Quitter le jeu` confirmed - the engine would "
                        "run Script_Pump(3), Game_NewGame and fade in from "
                        "white at the START MENU; this viewer has no restart, "
                        "so the run ends here\n");
            break;
        }

        // ---- THE PENDING LOAD, served the way `sub_408410` serves it
        //
        //     if (!dword_4E6C7C && dword_4C09B4 != -1) {
        //         v1 = playerActorRec[+396];
        //         Game_LoadSave(dword_4C09B4);
        //         dword_4C09B4 = -1;
        //         playerActorRec[+396] = v1;
        //         Screen_FadeFromColor(0xFFFFFF, 15, 0);
        //     }
        //     Script_Pump(...)
        //
        // - so it happens BETWEEN pumps, one-shot, and the screen is already
        // gone by then.  `Game_LoadSave` is the slot's name, day, time and DB
        // into `State_Apply`, which relocates, copies the header's scene into
        // the scene-per-area table and calls `Area_Load` (GAME_STATE 5a).
        // `if (!dword_4E6C7C && dword_4C09B4 != -1)` - and `dword_4E6C7C` is
        // the BOOT STARTUP CONTEXT (`bootCtx_`, AREA 118's own `+4` script).
        // So a load WAITS while that script is still running, which is what
        // gives the Grid sequence time to play: `Charger` answers 0, the
        // script's `var19 == 0` arm flies the cameras and ends, the context
        // frees, and only then does the save land. Serve it any earlier and
        // the fly-through is replaced by the apartment a frame later, which
        // is what a first version did.
        if (pendingLoadSlot >= 0 && session.bootContext() < 0) {
            const int slotNo = pendingLoadSlot;
            pendingLoadSlot = -1;
            const auto bytes = omk::readSaveFile(savesPath, fr + "/IAM/GAMES");
            if (const auto sl = omk::readSaveSlot(bytes, slotNo)) {
                state = sl->state;
                state.setClockDay(sl->day);
                state.setClock(sl->time);
                // `State_Apply`'s first act, before `Area_Load` reads it back
                state.setSceneOfArea(state.currentArea(), state.currentScene());
                state.placementWorld(savedAt, savedYaw);
                haveSavedPlacement = state.playerActorId() != -1;
                loadedName = sl->name;
                session.loadArea(state.currentArea());
                if (haveSavedPlacement)
                    session.setPlayerPosition(savedAt, savedYaw);
                session.requestCamera(0, 0);
                // the hand-over gate rebuilds the player for the new area,
                // on the same terms `--slot` gets: a resume does not wait for
                // the scene's beats to finish before handing over
                playerReady = false; adventure = false;
                forceAdventure = true;
                // `Screen_FadeFromColor(0xFFFFFF, 15, 0)` is
                // `Screen_StartColorFade(2, 0xFFFFFF, 15, 0)` - mode 2, the
                // "from" arm, fifteen frames, and in WHITE rather than the
                // black every other fade in the game uses.
                session.startColourFade(2, 0xFFFFFFu, 15.0f);
                std::printf("load: slot %d '%s', %s %s, area %d scene %d, "
                            "standing at %.0f %.0f %.0f facing %.0f\n",
                            slotNo, sl->name.c_str(),
                            omk::formatDate(sl->day).c_str(),
                            omk::formatTime(sl->time).c_str(),
                            state.currentArea(), state.currentScene(),
                            savedAt[0], savedAt[1], savedAt[2], savedYaw);
            } else {
                std::fprintf(stderr, "load: slot %d cannot be read\n", slotNo);
            }
        }

        if (session.areasEntered() != lastArea) {
            lastArea = session.areasEntered();
            if (lastArea > 0) std::printf("area transition %d\n", lastArea);
        }

        // WHICH `.SCX` IS RESIDENT, every time it changes. A transition is
        // supposed to swap it (`Session::reloadScene`), and if it does not
        // the destination's `scx.play*` have no objects to start - every one
        // of its bodies then falls back to the bank idle, or for a character
        // with no bank to the REST pose, standing on his 20-byte placement
        // record instead of his program's path.
        {
            static std::string scxWas = "-";
            const std::string now = session.scene().loaded() ? session.scene().file() : "(none)";
            if (now != scxWas) {
                scxWas = now;
                std::printf("[scx] frame %ld  resident scene is now %s (%zu objects)\n", n,
                            now.c_str(),
                            session.scene().loaded()
                                ? session.scene().scene().scene().objects.size() : 0u);
            }
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
            // ---- REINCARNATION: `player.become` moves the player into ----
            // another actor's body (`Session::becomePlayer`, the DB record's
            // +144 naming the new `.3DO`). The controller was built once, for
            // one model's meshes, `.CTL` bank and collision soup, and the
            // hand-over gate that builds it runs only on `!player` - so a
            // become mid-game kept the old body. The reader: *"different
            // characters can be played in the game, not just Kay'l, so if you
            // just force loading his model it will not work anymore when
            // switching characters."* The rule is: rebuild the controller
            // when the player ACTOR changes, keep it otherwise (an area load
            // does not change him).
            static int lastPlayerActor = -2;
            if (lastPlayerActor == -2) lastPlayerActor = playerId;
            if (playerId != lastPlayerActor) {
                if (player) {
                    std::printf("frame %ld: player.become - the player is actor %d now, was %d; "
                                "the controller is rebuilt for the new body\n", n, playerId, lastPlayerActor);
                    player.reset();
                    playerReady = false; adventure = false;
                    forceAdventure = true;
                }
                lastPlayerActor = playerId;
            }
            for (const auto& sh : session.shown()) {
                // In adventure mode the CONTROLLER owns the player's body; a
                // second one here would draw him twice.
                if ((adventure || session.dialogOpen()) && player && sh.actor == playerId) continue;
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
                // ...BUT NOT ONCE A PROGRAM HAS MOVED HIM. This ran every
                // frame and overwrote both the position and the FACING from
                // the 20-byte record. While a program drives, the placement
                // below puts its own back (`progPlaced && sceneClip >= 0`),
                // so the clobber was invisible; the frame the program ENDS,
                // `progPlaced` goes false and this wins - the body snaps back
                // to where the chunk parked him, which for Kay'l's flat is
                // 3635/1278/-656 against a floor at 1040, and vanishes.
                //
                // `Script_SelectBodyAnimation` never resets the node
                // (CLAUDE.md 6), so the accumulated placement STANDS; the
                // branch that carries `drawAt` over on the clip-change frame
                // is already written for exactly that and was being undone
                // one frame later. The facing half is the same bug seen from
                // the side: a reader watched Telis "look the wrong way, then
                // go back to the right one for the idle" - the record's
                // facing and the program's Euler alternating.
                if (sh.fromTable && !s->progRan) {
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
            // ...and neither need the PLAYER, when a `scx.play.player`
            // program owns him. The engine has no separate player body: op 46
            // starts the program on `Actor_Player()`'s own actor record, so it
            // is that actor the program poses, places and turns. He joins the
            // staged list exactly while such a program runs - which is also
            // exactly when the adventure controller stands down, so the two
            // can never both draw him - and the sweep below drops him again
            // when it ends.
            const int progPid = session.playerActor();
            if (playerProgram && player && progPid >= 0 && !playerModel.empty()) {
                const int pid = progPid;
                Staged* s = nullptr;
                for (auto& up : staged) if (up->actor == pid) { s = up.get(); break; }
                if (!s) {
                    staged.push_back(std::make_unique<Staged>());
                    s = staged.back().get();
                    s->actor = pid;
                    s->model = playerModel;
                    s->bank  = playerCtlName;
                    s->mo = charModelFor(playerModel);
                    s->bk = charBankFor(playerCtlName);
                    ++stagedEver;
                    stagedIds.push_back(pid);
                    // Where he stands until the program's first body step
                    // RUNS: the walker's own position, which is where the
                    // world last had him. `pelvis` is false because that is a
                    // point on the GROUND (`Walk_ProbeGround`'s anchor), not
                    // an authored hip height.
                    for (int k = 0; k < 3; ++k) s->at[k] = player->pos()[k];
                    s->at[1] -= playerFeetKnown ? playerFeet : 0.0f;
                    s->facing = player->facing();
                    s->placed = true;
                    s->pelvis = false;
                    std::printf("frame %ld: staged the PLAYER as actor %d %s "
                                "(bank %s) at %.0f %.0f %.0f facing %.0f - a "
                                "scene program owns his body (scx.play.player "
                                "starts on Actor_Player())\n",
                                n, pid, playerModel.c_str(),
                                playerCtlName.empty() ? "none" : playerCtlName.c_str(),
                                s->at[0], s->at[1], s->at[2], s->facing);
                }
                s->seen = true;
            } else if (playerProgramWas && player) {
                // ---- AND THE HAND BACK -------------------------------
                //
                // The engine has one body: the program moved the player's own
                // actor, so when it ends he is standing where it left him and
                // the walker carries on from there. Handing back the
                // CONTROLLER's stale position instead would snap him across
                // the room the frame the cutscene ends - the same shape as the
                // placement-record clobber, one level up.
                for (const auto& up : staged)
                    if (up->actor == session.playerActor() && up->progRan) {
                        // ...and the yaw the body was DRAWN with, which for a
                        // program-driven one is the step's own Euler and not
                        // the placement record's facing (`bodyYaw` below).
                        const float yaw = up->progYawKnown ? up->progYaw : up->facing;
                        player->placeAt(up->drawAt, yaw);
                        session.setPlayerPosition(up->drawAt, yaw);
                        std::printf("frame %ld: the program ended - the player "
                                    "keeps the body's place, %.0f %.0f %.0f "
                                    "facing %.0f\n", n, up->drawAt[0],
                                    up->drawAt[1], up->drawAt[2], yaw);
                        break;
                    }
            }
            playerProgramWas = playerProgram;
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
                if (it->first == playerModel)
                    std::printf("frame %ld: the PLAYER's model %s evicted - playerReady %d, "
                                "nobody staged wears it\n", n, it->first.c_str(), playerReady ? 1 : 0);
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
                // THE SKY IS GLOBAL AND EITHER SLOT CAN BRING IT.
                //
                // `Area_LoadMiscModel` keeps ONE model rather than one per
                // slot: `Area_TickLoad` case 4 calls it for the area being
                // loaded, so an area naming a sky REPLACES whatever is there
                // and one naming none (`*a1` false) leaves the previous
                // standing rather than clearing it. Loaded here so it arrives
                // with the set.
                //
                // **This asked slot 0 only, and that is a fault a reader
                // met**: *it happens when I load a save located in a
                // building*. A load puts its own area in slot 0, so walking
                // out of it lands the city in SLOT 1 - their own session log
                // has `SHOW area 217 in slot 0` and then `SHOW area 0 in slot
                // 1` - and Anekbah's `ASKY` was never asked for. Load in the
                // street instead and the same area arrives in slot 0 and the
                // sky is there, which is why it looked like a black sky "in
                // some cases".
                {
                    const std::string wantSky = shown ? rs.sky : std::string();
                    if (!wantSky.empty() && wantSky != sky.stem) {
                        sky = Sky{};
                        const auto o = fs.resolve("MESHES/MISC/" + wantSky + ".3DO");
                        if (!o) std::printf("sky: no model %s.3DO\n", wantSky.c_str());
                        else {
                            const auto d = omk::DataFs::readPath(*o);
                            sky.stem = wantSky;
                            sky.geo  = omk::buildGeometry(d, omk::DrawFilter::Engine);
                            sky.base = sky.geo.corners;
                            const auto t = fs.resolve("MESHES/MISC/" + wantSky + ".3DT");
                            if (t) sky.tex = omk::textures(d, omk::DataFs::readPath(*t));
                            const auto hdr = omk::readHeader(d);
                            if (hdr) {
                                const auto ms = omk::readMeshes(d, *hdr);
                                if (!ms.empty())
                                    for (int k = 0; k < 3; ++k) sky.origin[k] = ms[0].pos[k];
                            }
                            std::printf("sky: %s - %zu corners, %zu texture(s), origin"
                                        " %.1f %.1f %.1f, drawn at y %.1f\n",
                                        wantSky.c_str(), sky.base.size(), sky.tex.size(),
                                        sky.origin[0], sky.origin[1], sky.origin[2],
                                        sky.origin[1] - kSkyLift);
                            changed = true;
                        }
                    }
                }
            }
            if (changed) {
                ++poolComposition;      // the sky section of the pool changed
                rebuildWorld();
                std::printf("world: rebuilt - slot 0 '%s' (area %d, %s), slot 1 '%s' (area %d, %s); "
                            "walkable %zu tris, walls %zu tris\n",
                            worldSlots[0].stem.c_str(), worldSlots[0].area,
                            session.slotShown(0) ? "shown" : "hidden",
                            worldSlots[1].stem.c_str(), worldSlots[1].area,
                            session.slotShown(1) ? "shown" : "hidden",
                            playerSoup.size() / 9, playerSteep.size() / 9);
            }
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
                // ---- A DIALOGUE CAMERA'S POINTS CAN HANG OFF A SPEAKER ----
                //
                // `Camera_LoadParams` (0x004146C0): a point whose subject is
                // -1 is absolute and lands at +20/+32; any other subject makes
                // it an OFFSET at +124, and the block's subject resolver
                // (`sub_415A10` -> kind 2 is `sub_4151E0`) fills the anchor -
                // the actor NODE's world origin and its heading, `atan2` of
                // the node matrix's forward, +90 - and the solver places the
                // point at `anchor - R(heading) * offset`, the very sign
                // `resolveCamera` carries. `dialog_issue_camera` maps the
                // code to an actor: 0/1 the first speaker, 2/3 the second,
                // 6 both. The first is the player and the second the
                // conversation's speaker - read off the data rather than the
                // driver's write: every camera of DIALOG 39 is [2,2] and the
                // engine's own frame of it is a close-up of the hologram it
                // speaks with, and 401's camera 11 is [1,1].
                //
                // This viewer drew ONLY absolute dialogue cameras and marked
                // the rest `[relative - not drawn]`, so the transcan's advert
                // - the only camera the conversation has - fell through to
                // the follow camera and framed the room from across it. A
                // reader, with the original beside it: *transcan camera is
                // not good*.
                const auto anchorFor = [&](std::uint16_t code, float pos[3], float& yaw) -> bool {
                    if (code == 0xFFFF) return false;               // absolute: no anchor
                    const auto speakerBody = [&]() -> const Staged* {
                        const int sp = session.dialogue().conversation().speaker;
                        for (const auto& up : staged) if (up->actor == sp) return up.get();
                        return nullptr;
                    };
                    // THE CODE IS THE RESOLVER KIND, not merely which actor.
                    // `sub_415A10` switches on it - 0 -> `sub_414F30`,
                    // 1 -> `sub_415050`, 2 -> `sub_4151E0`, 3 -> `sub_415320` -
                    // and the four differ in WHERE on the actor they anchor:
                    //
                    //   0  the actor record's +244/+248/+252
                    //   1  the `Tete` node (actor+16), `Actor_LoadModel`'s cache
                    //   2  the body node's world origin
                    //   3  the head node's world origin
                    //
                    // while `dialog_issue_camera` picks the actor: 0/1 the
                    // first speaker, 2/3 the second, 6 both. Applying kind 2's
                    // body anchor to all of them put camera 11 of dialog 401 -
                    // `[1,1]`, on the player's HEAD - a foot in front of
                    // Kay'l's chest, and 176 of the 253 relative cameras the
                    // file ships are one of the two head kinds.
                    const auto playerBody = [&](float out[3], float& y) {
                        const float lift = player ? player->cameraLift() : 0.0f;
                        const float* pp = session.playerPos();
                        out[0] = pp[0]; out[1] = pp[1] - lift; out[2] = pp[2];
                        y = session.playerYaw();
                    };
                    const auto playerHead = [&](float out[3], float& y) {
                        if (!playerHeadKnown) { playerBody(out, y); return; }
                        for (int k = 0; k < 3; ++k) out[k] = playerHeadAt[k];
                        y = session.playerYaw();
                    };
                    const auto npcBody = [&](float out[3], float& y) -> bool {
                        const Staged* sb = speakerBody();
                        if (!sb) return false;
                        const float* at = sb->progRan ? sb->drawAt : sb->at;
                        for (int k = 0; k < 3; ++k) out[k] = at[k];
                        y = sb->drawnYawKnown ? sb->drawnYaw : sb->facing;
                        return true;
                    };
                    const auto npcHead = [&](float out[3], float& y) -> bool {
                        const Staged* sb = speakerBody();
                        if (!sb) return false;
                        y = sb->drawnYawKnown ? sb->drawnYaw : sb->facing;
                        if (!sb->headKnown) return npcBody(out, y);
                        for (int k = 0; k < 3; ++k) out[k] = sb->headAt[k];
                        return true;
                    };
                    switch (code) {
                        case 0: playerBody(pos, yaw); return true;
                        case 1: playerHead(pos, yaw); return true;
                        case 2: return npcBody(pos, yaw);
                        case 3: return npcHead(pos, yaw);
                        case 6: {                                   // the two-shot: both
                            float a[3], b[3], ya, yb;
                            playerBody(a, ya);
                            if (!npcBody(b, yb)) return false;
                            for (int k = 0; k < 3; ++k) pos[k] = 0.5f * (a[k] + b[k]);
                            yaw = ya;
                            return true;
                        }
                        default: return false;                      // detached: unresolvable
                    }
                };
                const auto placePoint = [&](const float off[3], std::uint16_t code, float out[3]) -> bool {
                    float p[3], yaw;
                    if (!anchorFor(code, p, yaw)) {
                        if (code != 0xFFFF) return false;
                        for (int k = 0; k < 3; ++k) out[k] = off[k];
                        return true;
                    }
                    const float t = yaw * 0.0174532925199433f;
                    const float cs = std::cos(t), sn = std::sin(t);
                    const float rx = off[0] * cs - off[2] * sn;
                    const float rz = off[0] * sn + off[2] * cs;
                    out[0] = p[0] - rx; out[1] = p[1] - off[1]; out[2] = p[2] - rz;
                    return true;
                };
                float ea[3], aa[3], eb[3], ab[3];
                const bool okA = placePoint(ca->eye, ca->subject[0], ea) &&
                                 placePoint(ca->at,  ca->subject[1], aa);
                const bool okB = placePoint(cbb->eye, cbb->subject[0], eb) &&
                                 placePoint(cbb->at,  cbb->subject[1], ab);
                for (int k = 0; k < 3; ++k) {
                    dlgView.cam.eye[k] = ea[k] + (eb[k] - ea[k]) * u;
                    dlgView.cam.at[k]  = aa[k] + (ab[k] - aa[k]) * u;
                }
                // THE FOV AND THE ROLL TRAVEL WITH THE POINTS. `sub_418410`
                // lerps four things across a move, not two:
                //
                //     out[52..60] = C[20..28]*u + prev[20..28]*(1-u)   // eye
                //     out[64..72] = C[32..40]*u + prev[32..40]*(1-u)   // target
                //     out[44]     = prev[11]*(1-u) + C[11]*u           // ROLL
                //     out[48]     = C[12]*u + prev[12]*(1-u)           // FOV
                //
                // and `Camera_LoadParams` (0x004146C0) settles which is which:
                // `+44` is the roll, WRAPPED to (-180, 180] as it is loaded
                // (`if (v14 > 180) v14 -= 360; if (v14 <= -180) v14 += 360`),
                // and `+48` the fov. `angle4096` already wraps on load here,
                // so a plain lerp of each is the engine's own arithmetic.
                //
                // This viewer SNAPPED the fov at the halfway point and dropped
                // the roll entirely. Six of 402's sixteen pairs change fov
                // across the move and one changes it by 15 degrees
                // (4572 -> 4574, 84 -> 99): a 15-degree widening in one frame
                // in the middle of a travel reads exactly as a reader
                // described it - *the camera suddenly returns to a previous
                // position while continuing the interpolation*. Eleven pairs
                // are rolled, up to 12 degrees, and were drawn upright.
                dlgView.cam.hfovDeg = ca->fov  + (cbb->fov  - ca->fov)  * u;
                dlgView.cam.rollDeg = ca->roll + (cbb->roll - ca->roll) * u;
                dlgView.cam.w = dispW; dlgView.cam.h = dispH;
                haveDlgCam = okA && okB;
                if (ca->id != lastDlgCam) {
                    lastDlgCam = ca->id;
                    std::printf("  dialogue camera %d -> %d (%s), fov %.0f, "
                                "roll %.0f%s\n", ca->id, cbb->id,
                                dlg.phase() == omk::DialogPhase::Menu
                                    ? "reply pair" : "line pair",
                                ca->fov, ca->roll,
                                haveDlgCam ? (ca->absolute() ? "" : "  [hangs off a speaker]")
                                           : "  [unresolvable subject - not drawn]");
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
            // Mode 13 stays installed and the scene sets no active camera, so
            // the camera holds what it last drew. `holdEditCam` is cleared by
            // the next thing that requests one - a script's `camera.set`, a
            // conversation, or the hand-over to adventure mode, all of which
            // are a real `Camera_Request` in the engine.
            holdEditCam = haveLastDrawn;
            heldUnderCamera = session.cameraId();
            std::printf("frame %ld: editing over - the camera HOLDS its last frame "
                        "(mode 13, no active camera, autocameraplayer 0)%s\n", n,
                        holdEditCam ? "" : " - nothing drawn yet, so the world camera stands");
        }
        if (edit) holdEditCam = false;      // a new editing takes it back
        // ...and any other `Camera_Request` ends mode 13. The Session's target
        // id changing IS one: `camera.set` is mode 12 and the hand-over asks
        // for the follow preset.
        if (holdEditCam && session.cameraId() != heldUnderCamera) holdEditCam = false;
        // ...and so are the other two the frontend issues itself: a
        // conversation is `Camera_Request(12)` (`Dialog_ApplyLineCameras`) and
        // the take is mode 1 out of `MDGETOBJ`. Either installs a mode that is
        // not 13, so the hold is over.
        if (holdEditCam && (haveDlgCam || takeCam)) holdEditCam = false;
        const bool anyWorld = !worldSlots[0].geo.corners.empty() ||
                              !worldSlots[1].geo.corners.empty();
        // ...and the same for DRAWING it: the screen composes over the
        // world afterwards, and every sneak page's background is an opaque
        // tile map, so nothing shows through that should not.
        //
        // THE PAUSE SCREEN IS THE EXCEPTION, and it was excluded here on an
        // assumption that nothing had tested, because nothing had ever
        // opened screen 31. Its panel 0x004E26C8 takes the FOURTH background
        // arm - `+76` is 0x40001800, so neither 0x2000 nor 0x4000, and
        // `+20`, the 80-tile array, is null - which is the arm that paints
        // nothing at all (`exetables.py: panels by background arm`). All it
        // draws is one fill item and three lines of text. So the world has
        // to be behind it, frozen: `uiPause` above already stops it
        // advancing, and stopping the DRAW as well left a pause menu on
        // black.
        //
        // **AND THE RULE IS THE SCREEN'S OWN FLAG** (2026-09-07), which
        // subsumes that: `UI_LoadScreen` (0x00429BB0) reads the screen
        // record's `+112` and, if bit `0x40000` is CLEAR, calls `sub_466B30`
        // - `byte_90E155 = 0`, so `Game_Frame` stops submitting the
        // full-screen 3D view through `sub_479C20`; `sub_46C290`, so the
        // sound bank is suspended; and the player's `+194` goes to
        // ACTOR_STATE 9. `Ui_CloseScreenDefault` calls the partner
        // `sub_466B60` to undo all three. Exactly THREE of the 37 screens
        // carry the bit - PAUSE GAME, SHOOT MECA and SHOOT HUMAN - the three
        // that must show the live world.
        //
        // Every other screen turns it off, and the sneak is one of them: the
        // sentence above ("every sneak page's background is an opaque tile
        // map") is not true of the shipped data. `sneak.bmp` is a SHEET the
        // page tiles 1:1, 31.8% of it is the colour key, and 21 of the 80
        // cells are more than 90% key - the middle of the device. That hole
        // is deliberate, and what belongs behind it is the device's own 3D
        // view, which the UI submits itself through `I2D_Submit3DView` (seven
        // call sites, all in the `Ui_*` range). Drawing the street there is
        // what a reader saw as *the sneak background is transparent*
        // (todo/omk-play.md 82).
        const bool screenKeepsWorld = !walk || openScreen < 0 ||
                                      w.worldBehind(openScreen);
        // ...UNLESS THE PANEL CARRIES A 3D VIEWPORT ITEM. `byte_90E155` only
        // guards `Game_Frame`'s full-screen submit; a UI item whose draw
        // callback is `sub_4782B0` submits the SAME world through the SAME
        // live camera (`I2D_Submit3DView(&rect, dword_93076C, flt_90E120, 0,
        // layer - 1)`) as a display-list node with its own rectangle, and
        // nothing gates it. The VIDEOPHONE's panel has one at (105, 85)
        // 500x280 on layer 6, and it is how the caller's face reaches the
        // device: the world is rendered into that rectangle - its own
        // viewport, its own aspect - and composed between the sheet and the
        // panel's items (`docs/UI.md` 3i, todo/omk-play.md 83).
        const omk::UiItem* vpItem = (walk && openScreen >= 0)
            ? omk::ScreenComposer::viewportItem(walk->panel() ? walk->panel()
                                                              : w.screen(openScreen))
            : nullptr;
        comp.attachView3D(nullptr);
        const bool drawWorld = (screenKeepsWorld || vpItem) &&
                               worldReady && anyWorld &&
                               (haveDlgCam || haveEdit || holdEditCam ||
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
            // (`|| uiPause`: a pause does not change the camera mode, so it
            // does not put bars on a walk either.)
            //
            // **AND IT IS THE PLAYER'S CONTROL THAT DECIDES, NOT THE SHAPE OF
            // THE CAMERA** (next-tasks 2, "black stripes entering/leaving a
            // building"). This also required `followCam` - the area's own
            // camera 0, relative to actor 0 - and a great many areas roam
            // under a FIXED camera instead: leaving Kay'l's flat runs
            // `player.anim.hold` / `fade.to_black` / `camera.set 4418` and
            // hands over to Hall 27, whose script leaves absolute camera 4353
            // installed. Measured on that walk: by frame 400 `adventure` is 1
            // and `animHeld` is 0 - the player has control - while `followCam`
            // is still 0, so the bars went on at frame 3 and never came off
            // again in 900 frames. That is the report.
            //
            // The captures say the same thing from the other side. Every
            // letterboxed one is a frame the player does NOT control -
            // `dlg402-32..41` at 64/64 rows, and `intro-75`, a CUTSCENE shot
            // with a scripted world camera, at 64/65 - so the strip is not
            // "a conversation" either, it is camera mode. Nothing establishes
            // it for a frame he does control, whatever camera is up.
            //
            // ...AND "HE HAS CONTROL" IS THE HOLD, not `adventure` alone.
            // A reader, on the end of the Telis lunch: *the stripes were not
            // displayed on the zoom on the talisman*. SCENE 53's beat holds
            // the player at pc 1090 and does not release him until 1220, so
            // the whole of it - the conversation, `object.show 27` and the
            // 4215/4216 zoom that follows - is camera mode; but a game
            // resumed from the LOAD PANEL sets `forceAdventure`, whose
            // `wantAdventure` asks only that he is placed with no dialogue
            // and no screen up. Between the conversation closing and the
            // release, that is true, and the bars came off over the zoom.
            //
            // `player.anim.hold` is the engine's own marker for it, and the
            // two traced `Screen_Fade` sites pair with exactly that:
            // `Screen_Fade(1)` with `Actor_HoldAnimation(player, 1)` on the
            // way into a slider travel and a fight, `Screen_Fade(0)` with
            // `Actor_HoldAnimation(player, 0)` on the way out.
            //
            // ...AND THE STRIP OUTLASTS THE RELEASE BY THE FADE. SCENE 53's
            // beat brackets itself
            //
            //     1089  fade.to_black       ; Screen_Fade(1) -> state 3
            //     1090  player.anim.hold
            //      ...  387, the sneak call, 388, the talisman zoom
            //     1220  player.anim.release
            //     1221  fade.from_black     ; Screen_Fade(0) -> state 4
            //
            // - the release comes BEFORE the fade, so a strip that ends with
            // the hold ends one frame early and vanishes instead of fading.
            // A reader: *there was fade to show the black stripes then they
            // suddenly disappeared*.
            //
            // **The test is `bandsDark`, not `running`**, and that is a
            // second report: *the stripes of the loading screen are not
            // removed when I can actually play*. Mode 3 HOLDS once its clock
            // is spent - armed for ever, drawing no bands at all - so a strip
            // keyed on the fade being armed never lifts after a load.
            // `bandsDark` asks whether it is darkening the bands THIS frame,
            // which is the engine's own two quads, `(h << 6) / 480` tall -
            // the 64 the captures measure.
            if ((adventure || uiPause) && !holdEditCam &&
                !session.playerAnimHeld() && !session.blackFade().bandsDark())
                view.vh = dispH;
            if (view.vh > dispH) view.vh = dispH;
            view.vx = 0;
            view.vy = (dispH - view.vh) / 2;
            if (vpItem) {
                // The viewport item's rectangle, scaled the way `I2D_ScaleX/Y`
                // scale it (`v * screen / 640`), and it replaces the
                // letterbox: `sub_45FA20` sets the D3D viewport to exactly
                // this rect and the vertical fov follows its aspect.
                view.vx = vpItem->x * dispW / 640;
                view.vy = vpItem->y * dispH / 480;
                view.vw = vpItem->w * dispW / 640;
                view.vh = vpItem->h * dispH / 480;
            }
            // THE FOG (todo/options-config.md step 4). Linear, over the range
            // the clip distance sizes - `sub_440BE0` writes `+328 = D * 0.25`
            // as the start and `+340 = D` as the end, so it ENDS at the clip
            // distance and not at the 0.95 bucket split. The colour is the
            // scene's `+336`, which `Scene_Load3DO`'s caller sets to ZERO and
            // nothing else in the decompilation writes: the shipped fog
            // DARKENS toward the horizon rather than hazing it, which is what
            // a domed city at night wants - and it is what hides the hard edge
            // at the clip distance. `--fog 0` turns it off, `--fog-colour
            // r,g,b` overrides it (the one mode that colours it uses
            // 40,80,64 with a 15 m clip - `docs/ASSETS.md`, "The fog").
            // With an unlimited clip there is no range to fade over, so the
            // fog is OFF rather than infinite - an infinite start would reach
            // the shader as a comparison against inf, a value no check could
            // read back and no driver need agree about.
            view.fog      = drawFog && !unlimitedClip;
            view.fogStart = unlimitedClip ? 0.0f : static_cast<float>(clipInches * 0.25);
            view.fogEnd   = unlimitedClip ? 0.0f : static_cast<float>(clipInches);
            for (int k = 0; k < 3; ++k) view.fogColour[k] = fogRGB[k];
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
            } else if (!haveDlgCam && holdEditCam) {
                // MODE 13 WITH NO ACTIVE CAMERA: the block is not written, so
                // the last frame stands. Right after the editing branch,
                // because the engine's hold outranks everything the frontend
                // would otherwise pick - the follow camera included, since the
                // mode is still 13 and nothing has requested another.
                for (int k = 0; k < 3; ++k) {
                    view.cam.eye[k] = lastEye[k];
                    view.cam.at[k]  = lastAt[k];
                }
                view.cam.hfovDeg = lastFov;
                view.cam.rollDeg = lastRoll;
                view.cam.w = dispW; view.cam.h = dispH;
            } else if (!haveDlgCam && !ride &&
                       session.sliders().calledVehicle() >= 0 &&
                       (session.sliders().callMachine().state == 2 ||
                        session.sliders().callMachine().state == 6 ||
                        (boarding && boardCam > 0) ||
                        (boarded && session.sliders().callMachine().state == 4))) {
                // ---- THE CAMERA THAT WATCHES IT COME ------------------
                //
                // `sub_456530` case 2 asks for **camera mode 8 on the
                // SLIDER** the moment a call is armed, and the only guard on
                // it is `if (sub_413360(C) != 8)` - which stops it RE-asking
                // when it is already there, not from asking at all. So the
                // camera cuts to the vehicle and follows it in every time,
                // which is what a reader described as seeing the slider on
                // its road; this port kept the follow camera on the player
                // and showed none of it.
                //
                // Same preset and same resolution as the ride's, because it
                // is the same mode - only the subject differs, and in both
                // cases the subject is the VEHICLE.
                float at[3];
                session.sliders().calledAt(at);
                const float t = session.sliders().calledYaw() * 0.0174532925199433f;
                const float cs = std::cos(t), sn = std::sin(t);
                const auto place = [&](const float off[3], float out[3]) {
                    const float rx = off[0] * cs - off[2] * sn;
                    const float rz = off[0] * sn + off[2] * cs;
                    out[0] = at[0] - rx;
                    out[1] = at[1] - off[1];
                    out[2] = at[2] - rz;
                };
                static constexpr float kComeEye[3] = {0.0f, 118.1102f, -275.5905f};
                static constexpr float kComeAt[3]  = {0.0f, 78.7402f, 0.0f};
                // ...and preset 9 for the BOARDING, which is what
                // `MDACTION`'s arm asks for over 60 frames
                // (`dword_930818 = 42700000h`). Its eye is 157.4803 - 4.00 m,
                // the same distance as the gate's reach - along the slider's
                // -X, which is the door side and the side the man had to be
                // standing on, and 59.0551 (1.50 m) up. Placed from the
                // matrix ROWS and not from `calledYaw`, because the door
                // geometry is what showed the pool's yaw and the actor's
                // euler to be mirror conventions.
                static constexpr float kBoardEye[3] = {157.4803f, 59.0551f, 0.0f};
                if (boarding && boardCam > 0) {
                    float bat[3], bx[3], bz[3];
                    if (session.sliders().calledFrame(bat, bx, bz)) {
                        for (int k = 0; k < 3; ++k) {
                            view.cam.eye[k] = bat[k] - kBoardEye[0] * bx[k]
                                                     - kBoardEye[2] * bz[k];
                            view.cam.at[k]  = bat[k];
                        }
                        view.cam.eye[1] -= kBoardEye[1];
                    }
                    --boardCam;
                } else {
                    place(kComeEye, view.cam.eye);
                    place(kComeAt,  view.cam.at);
                }
                view.cam.hfovDeg = 75.0f;      // the preset's own fov
                view.cam.rollDeg = 0.0f;
                view.cam.w = dispW; view.cam.h = dispH;
            } else if (!haveDlgCam && ride) {
                // CAMERA MODE 8, the ride camera, and its subject is the
                // SLIDER and not the player: `camera_presets.json`'s row 8 is
                // eye (0, 118.1102, -275.5905), target (0, 78.7402, 0) with
                // `eyeSubject` and `targetSubject` both **5**, `f42` 0 (so
                // the target does not lag) and `f44`/`f46` 8. Those offsets
                // are exact metres - 3.00 up, 7.00 back and 2.00 up - which
                // is what says they were authored rather than tuned.
                //
                // Resolved the way every subject-relative camera is:
                // `out = subject - rotateYaw(offset)`, with `out[1] =
                // subject[1] - offset[1]` (`o3de/worldcam.cpp`).
                const float t = static_cast<float>(ride->yaw) * 0.0174532925199433f;
                const float cs = std::cos(t), sn = std::sin(t);
                const float sub[3] = {static_cast<float>(ride->x),
                                      static_cast<float>(ride->y),
                                      static_cast<float>(ride->z)};
                const auto place = [&](const float off[3], float out[3]) {
                    const float rx = off[0] * cs - off[2] * sn;
                    const float rz = off[0] * sn + off[2] * cs;
                    out[0] = sub[0] - rx;
                    out[1] = sub[1] - off[1];
                    out[2] = sub[2] - rz;
                };
                static constexpr float kRideEye[3] = {0.0f, 118.1102f, -275.5905f};
                static constexpr float kRideAt[3]  = {0.0f, 78.7402f, 0.0f};
                place(kRideEye, view.cam.eye);
                place(kRideAt,  view.cam.at);
                view.cam.hfovDeg = 75.0f;      // the preset's own fov
                view.cam.rollDeg = 0.0f;
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
                const omk::FollowCamera tc = player->resolveOffsets(takeCamEye, takeCamAt, takeCamFov);
                const omk::FollowCamera& fc = player->followCamera();
                const omk::FollowCamera& to = takeCamPhase == 3 ? fc : tc;
                float u = 1.0f;
                if (takeCamPhase == 1 || takeCamPhase == 3) {
                    takeCamClock += static_cast<float>(frameSec * 30.0);
                    u = haveLastDrawn ? std::min(1.0f, takeCamClock / takeCamTravel) : 1.0f;
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
            } else if (!haveDlgCam && (adventure || uiPause) && followCam &&
                       player) {
                // The controller's follow camera: the world camera's offsets
                // resolved against HIS position and facing every frame, with
                // the engine's lag (player.h quotes sub_415D10/sub_415E60).
                //
                // `|| uiPause` because OPENING A SCREEN DOES NOT MOVE THE
                // CAMERA. Nothing in the pause screen's open callback touches
                // the camera mode, and `Game_Frame` renders with whatever is
                // installed, so the view behind the menu is the view that was
                // on screen. `adventure` alone is a per-frame mode that any
                // screen takes false, so pausing used to swap the follow
                // camera for the area's own camera 0 - the shot jumped the
                // moment the menu came up.
                // THE CAMERA THROUGH A WALL, measured: the one invariant
                // `sub_417070` exists to keep is that nothing solid lies
                // between the camera's target and its eye. Cast the segment
                // and say so.
                if (stagedProbe && (n % 10) == 0) {
                    const omk::FollowCamera& c = player->followCamera();
                    const double at[3] = {c.at[0], c.at[1], c.at[2]};
                    const double d[3]  = {c.eye[0] - c.at[0], c.eye[1] - c.at[1],
                                          c.eye[2] - c.at[2]};
                    bool through = false;
                    for (const omk::TriangleSoup* sp : {&playerSteep, &playerSoup})
                        if (!sp->empty())
                            if (const auto h = omk::sweepSphere(*sp, at, d, 1.0))
                                if (h->t < 0.98) through = true;
                    std::printf("    cam %ld eye %.0f %.0f %.0f  at %.0f %.0f %.0f  "
                                "dist %.0f  %s  (soups %zu steep / %zu walk tris)\n", n,
                                c.eye[0], c.eye[1], c.eye[2],
                                c.at[0], c.at[1], c.at[2],
                                std::sqrt(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]),
                                through ? "THROUGH a solid face" : "clear",
                                playerSteep.size() / 9, playerSoup.size() / 9);
                }
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
                // ...and the Session solves a TRAVEL's two ends at the same
                // point, so it has to know the lift too
                session.setCameraSubjectLift(lift);
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
            // ...and the RESOLVED eye, which is the quantity a camera fault
            // is actually about. A frame counts lit pixels and cannot tell a
            // correct shot from a wrong one that happens to see the sky; the
            // eye's distance from the player can (`verify.py: camera travel`).
            if (std::getenv("OMK_CAMEYE")) {
                if (session.dialogOpen()) {
                    const auto& dg = session.dialogue();
                    const omk::DialogCamera* qa = dg.cameraA();
                    const omk::DialogCamera* qb = dg.cameraB();
                    std::printf("  [dlgcam] frame %ld  pair %d -> %d  u %.3f  phase %d  node %d"
                                "  inForce %d  fov %.2f  roll %.2f\n", n,
                                qa ? qa->id : -1, qb ? qb->id : -1,
                                static_cast<double>(dg.cameraProgress()),
                                static_cast<int>(dg.phase()), dg.node(), haveDlgCam ? 1 : 0,
                                static_cast<double>(dlgView.cam.hfovDeg),
                                static_cast<double>(dlgView.cam.rollDeg));
                }
                std::printf("  [cameye] frame %ld  eye %.0f %.0f %.0f  at %.0f %.0f %.0f"
                            "  player %.0f %.0f %.0f\n", n,
                            view.cam.eye[0], view.cam.eye[1], view.cam.eye[2],
                            view.cam.at[0], view.cam.at[1], view.cam.at[2],
                            session.playerPos()[0], session.playerPos()[1],
                            session.playerPos()[2]);
            }
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
            // ...AND IN A CONVERSATION. `Actor_EnterDialogueMode` puts the
            // player's channel on group 400, the dialogue stance, and he goes
            // on being drawn like any actor - the reverse shots of 402 frame
            // him across the room. This viewer drew him only in adventure
            // mode, so every cut to Kay'l during a conversation showed an
            // empty floor. A reader: *Kay'l is not visible when the camera
            // changes*. The controller ticks through the conversation, so
            // its pose is the stance.
            // ...and the THIRD time this mode test has been too narrow: a
            // PAUSE also takes `adventure` false, and the pause screen draws
            // over the live scene, so without `uiPause` the menu came up on
            // a street with the player deleted from it. Same shape as the
            // conversation above.
            // SHOOT MODE IS FIRST PERSON, so the player's own body is not
            // drawn - the reader's word, 2026-09-09, and the engine agrees:
            // `Shoot_Enter` (0x004222D0) calls `sub_436CE0` on the player's
            // node, which is `o3de_Traverse` setting flag bit 2 on every node
            // that does not carry 0x200000, and bit 2 is inside the
            // not-drawable mask 0x800043. `sub_436D20` is its exact inverse
            // (`& 0xFD`) and is what shows him again on the way out.
            //
            // Without this the camera - preset row 4, eye offset (0,0,0),
            // which is the PELVIS in all 181 character models - sits inside
            // his own mesh, and from some facings the whole view is the
            // inside of his back. That is what the first play-test render of
            // shoot mode showed, and it did not move when the player moved,
            // which is the tell for something drawn in camera space.
            //
            // NOT MODELLED, and labelled rather than dropped: the 0x200000
            // exemption, which leaves some nodes of the tree visible. The
            // port draws the player as one body with no per-node flags, so
            // it can only take him out whole. Whatever the exemption is for
            // in first person - the weapon in his hands is the obvious
            // candidate - is not reproduced here.
            const bool drawPlayer = playerReady && player && !session.shootMode().active() &&
                                    (adventure || uiPause ||
                                     (session.dialogOpen() && !playerProgram));
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
                        // WHERE IN THE HAND - a RECONSTRUCTION, labelled. The
                        // node's origin is the wrist joint, and a 6 cm object
                        // (ANNEAU's extent is 2.6 units) placed there sits
                        // inside the hand mesh: measured, the drawn rings
                        // centred within 0.2 of the node and nobody could see
                        // them. What offset the engine gives a re-linked prop
                        // is not read (`sub_41C490` leaves the node's +36
                        // alone and `Anim_ApplyNodeFrame` skips a node whose
                        // header +12 is -1). Until it is, the object is
                        // carried at the hand mesh's own centre, which is the
                        // palm.
                        // THE ENGINE'S RULE, read 2026-09-05. The transform pass
                        // (`sub_4942A0`, 26_ole.c) composes a child as
                        // `world = parent.world + parent.worldMatrix * local`,
                        // `local` being the mesh record's +128..+136 - which
                        // `sub_41C490` leaves as the prop's file carries it
                        // (ANNEAU: -2.24, -0.17, -2.01, three units from the
                        // wrist toward the knuckles) - and the child's rotation
                        // is `matrix(+56) x facing(+156)` under the parent's.
                        // A prop's placement ROTATION lives in +156, not +56:
                        // `Object_SetPlacement` builds its Euler matrix into the
                        // slot record and points +156 at it, while +56 stays
                        // identity - and the grab's `sub_437140(node, 0)` clears
                        // +156. So a held object turns with the HAND ALONE. The
                        // release (`sub_41C540(actor, 0)`) resets +56, re-links
                        // the node under the scene root and restores the saved
                        // placement, position and angles both, which is what
                        // drawing a released prop from its record already does.
                        const float handOff[3] = {pm->localOff[0], pm->localOff[1], pm->localOff[2]};
                        const std::size_t base = propGeo.corners.size();
                        for (const auto& c : pm->rest.corners) {
                            omk::Corner w = c;
                            const float local[3] = {c.x - pm->origin[0] + handOff[0],
                                                    c.y - pm->origin[1] + handOff[1],
                                                    c.z - pm->origin[2] + handOff[2]};
                            float r[3];
                            omk::qrot(hp.q, local, r);              // the hand's rotation, alone
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
                        static long heldToldFrame = -1000;
                        if (n - heldToldFrame >= 30) {
                            heldToldFrame = n;
                            double cx = 0, cy = 0, cz = 0; const std::size_t cnt = propGeo.corners.size() - base;
                            float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
                            for (std::size_t c = base; c < propGeo.corners.size(); ++c) {
                                const auto& w = propGeo.corners[c];
                                cx += w.x; cy += w.y; cz += w.z;
                                lo[0] = std::min(lo[0], w.x); hi[0] = std::max(hi[0], w.x);
                                lo[1] = std::min(lo[1], w.y); hi[1] = std::max(hi[1], w.y);
                                lo[2] = std::min(lo[2], w.z); hi[2] = std::max(hi[2], w.z);
                            }
                            if (cnt) { cx /= cnt; cy /= cnt; cz /= cnt; }
                            float hin[3] = {hp.pos[0] - playerRootXZ[0], hp.pos[1], hp.pos[2] - playerRootXZ[1]}, ho[3];
                            omk::rotateYaw(yaw, hin, ho);
                            // the FIST as drawn: the hand mesh's corners in playerPosed, world
                            float flo[3] = {1e9f, 1e9f, 1e9f}, fhi[3] = {-1e9f, -1e9f, -1e9f}; std::size_t fc = 0;
                            for (std::size_t c = 0; c < playerPosed.corners.size(); ++c) {
                                if (c >= playerPosed.cornerMesh.size() || playerPosed.cornerMesh[c] != hand) continue;
                                const auto& w = playerPosed.corners[c]; ++fc;
                                flo[0] = std::min(flo[0], w.x); fhi[0] = std::max(fhi[0], w.x);
                                flo[1] = std::min(flo[1], w.y); fhi[1] = std::max(fhi[1], w.y);
                                flo[2] = std::min(flo[2], w.z); fhi[2] = std::max(fhi[2], w.z);
                            }
                            std::printf("held %d: hand node %.1f %.1f %.1f q(%.2f %.2f %.2f %.2f); object centre "
                                        "%.1f %.1f %.1f box [%.1f..%.1f %.1f..%.1f %.1f..%.1f] %zu corners %zu batches "
                                        "(mat %d texBase %zu); fist box [%.1f..%.1f %.1f..%.1f %.1f..%.1f] %zu corners "
                                        "(as drawn LAST frame); player %.1f %.1f %.1f yaw %.0f; camera eye %.0f %.0f %.0f\n",
                                        pr.id, ho[0] + pp[0], ho[1] + pp[1] - playerFeet + lastRootDrop, ho[2] + pp[2],
                                        hp.q.w, hp.q.x, hp.q.y, hp.q.z,
                                        cx, cy, cz, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], cnt,
                                        pm->rest.batches.size(),
                                        pm->rest.batches.empty() ? -1 : pm->rest.batches[0].material, pm->texBase,
                                        flo[0], fhi[0], flo[1], fhi[1], flo[2], fhi[2], fc,
                                        pp[0], pp[1], pp[2], yaw, lastEye[0], lastEye[1], lastEye[2]);
                        }
                        continue;
                    }
                    const double rx = pr.rotDeg[0] * 0.0174532925199433;
                    const double ry = pr.rotDeg[1] * 0.0174532925199433;
                    const double rz = pr.rotDeg[2] * 0.0174532925199433;
                    const double cx = std::cos(rx), sx = std::sin(rx);
                    const double cy = std::cos(ry), sy = std::sin(ry);
                    const double cz = std::cos(rz), sz = std::sin(rz);
                    // `Matrix3x3_FromEulerAngles` (0x00441EB0, CLEAN in readable/),
                    // TRANSCRIBED term for term - `Object_SetPlacement` builds a
                    // prop's facing matrix with it, the transform pass composes
                    // it under the scene root, and the vertex pass applies it
                    // row-vector (`v . M`, as `Matrix3x3_RotateVector`). The
                    // block this replaces claimed to be that function and was
                    // its INVERSE: numerically it equals the engine's with all
                    // three angles negated, so every prop on the floor was
                    // turned the wrong way - unnoticed on symmetric props until
                    // the rings came back from a correctly turned hand
                    // (2026-09-05).
                    const double m00 = cz * cy,                 m01 = -(sz * cy),               m02 = sy;
                    const double m10 = sy * sx * cz + sz * cx,  m11 = cz * cx - sx * sz * sy,   m12 = -(sx * cy);
                    const double m20 = sz * sx - sy * cz * cx,  m21 = cx * sz * sy + sx * cz,   m22 = cy * cx;
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
            // THE PROPS' GEOMETRY CHANGES, AND THE GPU MUST HEAR OF IT. The Vulkan
            // backend keys a vertex buffer on the Geometry's pointer and
            // `revision` and returns the cached buffer while they match - and
            // `propGeo`'s revision was never bumped, so the props uploaded on the
            // first frame were drawn for ever: the rings stayed on the floor and
            // never appeared in the hand however right the CPU-side placement
            // was (the log showed it exactly on the hand node for three days of
            // reports). Every other per-frame geometry here bumps its revision;
            // this one does now, whenever a prop is held or the set of shown
            // props changes size.
            // ...and on EVERY rebuild, not "while held or when the count changes":
            // that rule missed the first frame after a release, so the GPU kept
            // the last held frame's buffer - the rings standing where the hand
            // let them go - while the CPU had them back on the floor (a reader's
            // before/after screenshots, 2026-09-05). A few hundred corners a
            // frame is nothing.
            propGeo.revision = ++worldGeoRev;
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
                // the sky's one texture, on the same rule as every other
                // section: a batch's slot is its material plus its own base
                sky.texBase = pool.size();
                pool.insert(pool.end(), sky.tex.begin(), sky.tex.end());
                // THE SHADOW's one texture, on the same rule. It is resident
                // for the whole run rather than per set, because the engine
                // loads the model once at game start and never frees it.
                shadowTexBase = pool.size();
                pool.insert(pool.end(), shadowModel.tex.begin(), shadowModel.tex.end());
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
                // (a) THE PROGRAM THAT NAMES HIM - IN EITHER RESIDENT POOL.
                //
                // `Session::reloadScene` records why: an area transition does
                // not call `Scene_LoadSCX`, so every running program survives
                // it and the OUTGOING pool goes on being ticked. This file
                // already reads set-mesh MOTIONS from both (the tunnel door,
                // which closes behind the player out of `sceneOut()`), and
                // read the POSE from the active one alone - so the moment the
                // resident `.SCX` swapped, every body the outgoing scene was
                // animating fell to its bank idle, or with no bank to the REST
                // pose. Walking Anekbah -> the restaurant that is 17 of
                // Anekbah's street bodies frozen while both sets are still
                // drawn. The active pool is tried FIRST, so a body both pools
                // could claim keeps the incoming scene's program.
                const omk::SceneRunner* run = &sc;
                int prog = -1;
                static const bool activeOnly =
                    std::getenv("OMK_ACTIVE_POOL_ONLY") != nullptr;   // the old, wrong lookup
                for (const omk::SceneRunner* cand : {&sc, &session.sceneOut()}) {
                    if (activeOnly && cand != &sc) break;
                    if (!cand->loaded()) continue;
                    for (std::size_t k = 0; k < cand->started().size(); ++k) {
                        const auto& t = cand->started()[k];
                        if (!drivenBy(t, s.actor)) continue;
                        if (!cand->programRunning(static_cast<int>(k))) continue;
                        prog = static_cast<int>(k);
                        run = cand;
                    }
                    if (prog >= 0) break;
                }
                bool byLone = false;
                if (prog < 0 && loneProgs == 1 && staged.size() == 1) {
                    prog = loneProg;
                    run = &sc;                 // the lone-program fallback is the ACTIVE pool's
                    byLone = true;
                }
                // Which pool answered, said once per change - a body that
                // swaps pools mid-transition is worth seeing in the log.
                if (run != s.poolWas) {
                    s.poolWas = run;
                    if (prog >= 0 && stagedProbe)
                        std::printf("    pool %ld actor %d %s driven by the %s pool (%s)\n",
                                    n, s.actor, s.model.c_str(),
                                    run == &sc ? "ACTIVE" : "OUTGOING", run->file().c_str());
                }
                int sceneClip = -1, scenePath = -1;
                float sceneFrame = 0.0f;
                const omk::SceneRunner::Started* stt = nullptr;
                if (prog >= 0) {
                    stt = &run->started()[static_cast<std::size_t>(prog)];
                    // Nothing is posed or placed before the program's first
                    // body-animation step RUNS (`Started::animReached`): an
                    // object that opens with a wait leaves its actor where
                    // the world has him - Kay'l under the alley at address
                    // 654 for the 60 frames before his jump out of the portal.
                    sceneClip = stt->animReached ? stt->clip : -1;
                    scenePath = stt->animReached ? stt->path : -1;
                    // The frame of THAT clip, not of the program: a program
                    // walks its steps and each may name a different animation,
                    // so the clock the pose is sampled at counts from the step
                    // (`SceneRunner::programAnimClock`).
                    sceneFrame = run->programAnimClock(prog);
                    // THE FACING, from the step the pc is on - and from BOTH
                    // kinds of body animation, because both end in
                    // `Actor_SetEuler(node, p4, p5, p6)`. It is not sticky:
                    // the waiter's walk clips author 0 and turn him back to 0,
                    // and reading only the relative call's Euler rotated his
                    // whole route by the 145 his standing step had left.
                    static const bool stickyEuler =
                        std::getenv("OMK_STICKY_EULER") != nullptr;   // the old, wrong reading
                    if (stt->animReached && (!stickyEuler || stt->relative)) {
                        s.progYaw = stt->euler[1];
                        s.progYawKnown = true;
                    }
                }
                if (sceneClip != s.sceneClipWas) {
                    // THE HAND-OVER GAP. A program's steps are authored to
                    // chain: each clip's placement is where the previous one
                    // left the body, so a correct run re-places him on the
                    // spot he already occupies and nothing visibly moves.
                    // Where the accumulated root motion is wrong the two
                    // disagree and the next step YANKS him - "he is teleported
                    // sometimes" (a reader, 2026-09-05). The gap is therefore
                    // a measure of the root motion, independent of the walk
                    // mesh: two chains that must agree.
                    const float wasAt[3] = {s.drawAt[0], s.drawAt[1], s.drawAt[2]};
                    const bool hadOne = s.progRan && s.sceneClipWas >= 0;
                    s.sceneClipWas = sceneClip;
                    s.sceneTracks = omk::NodeTracks{};
                    if (sceneClip >= 0 && run->loaded())
                        s.sceneTracks = omk::clipTracks(run->scene().clipData(sceneClip));
                    if (scenePath >= 0 && run->loaded() &&
                        scenePath < static_cast<int>(run->scene().paths().size())) {
                        const auto& pa = run->scene().paths()[static_cast<std::size_t>(scenePath)];
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
                            s.progRan = true;
                            // ...and the FACING is the call's Euler, written to the
                            // node every tick; the clip's root quaternion sits under
                            // it. Anekbah's Kiss couples say -70, the walkers 180 -
                            // without it a couple placed right still stood turned
                            // away and intersecting (a reader's frame, 2026-09-03).
                            // Pitch and roll (params 4 and 6) are rarely non-zero
                            // (three beggars carry -7 of pitch) and are not applied.
                            for (int k = 0; k < 3; ++k) s.progBase[k] = s.at[k];
                        }
                        std::printf("  pose: actor %d %s - clip %d '%s' (%d frames) on path "
                                    "%d '%s' at %.0f %.0f %.0f%s\n", s.actor, s.model.c_str(),
                                    sceneClip, run->scene().clipName(sceneClip).c_str(),
                                    s.sceneTracks.frames, scenePath, pa.name.c_str(),
                                    s.at[0], s.at[1], s.at[2],
                                    byLone ? "  [the program names another actor; this is "
                                             "the frame's only body - LABELLED]" : "");
                    } else if (sceneClip >= 0 && run->loaded()) {
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
                        if (omk::clipRootStart(run->scene().clipData(sceneClip), base)) {
                            // ...plus the call's own offset, params 7/8/9 in
                            // inches: `x = node.x - GetParamFloatB(fn, 7) *
                            // -0.39370078`. Zero at every shipped site read so
                            // far, but it is the field the function reads.
                            for (int k = 0; k < 3; ++k)
                                base[k] += stt ? stt->offset[k] : 0.0f;
                            for (int k = 0; k < 3; ++k) s.at[k] = base[k];
                            s.placed = true;
                            s.pelvis = true;
                            s.progPlaced = true; s.progPelvis = true;
                            s.progRan = true;
                            for (int k = 0; k < 3; ++k) s.progBase[k] = base[k];
                            std::printf("  pose: actor %d %s - clip %d '%s' (%d frames), no "
                                        "path: snapped to its root key 0 at %.0f %.0f %.0f%s\n",
                                        s.actor, s.model.c_str(), sceneClip,
                                        run->scene().clipName(sceneClip).c_str(),
                                        s.sceneTracks.frames, s.at[0], s.at[1], s.at[2],
                                        byLone ? "  [the program names another actor; this "
                                                 "is the frame's only body - LABELLED]" : "");
                        }
                    } else if (sceneClip < 0 && stt && !stt->animReached) {
                        // NOT YET: the program has not reached a body-animation
                        // step, so the engine has not touched the actor. He is
                        // where the world has him - for the player, the DB
                        // position (`actor.goto_address` parks Kay'l at address
                        // 654, under the alley, before his jump); for anyone
                        // else, the placement record.
                        s.progPlaced = false;
                        if (stt->how == "player") {
                            const float* pp = session.playerPos();
                            for (int k = 0; k < 3; ++k) s.at[k] = pp[k];
                            s.placed = true;
                            s.pelvis = false;
                        }
                        std::printf("  pose: actor %d %s - its program has not reached an "
                                    "animation yet; he stays where the world has him "
                                    "(%.0f %.0f %.0f)\n", s.actor, s.model.c_str(),
                                    s.at[0], s.at[1], s.at[2]);
                    } else if (sceneClip < 0 && s.progRan) {
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
                    } else if (sceneClip < 0) {
                        // NO program ever drove this body. `drawAt` is not
                        // "where the program left him" - on the first frame it
                        // has never been written - so the placement record the
                        // chunk gives him STANDS, untouched. Gandhar's cave is
                        // the case: twelve ZOH_FN whose actor records carry no
                        // .CTL at all and whom only `shoot.actor.enter` names,
                        // every one of them teleported to 0 0 0 on frame 0.
                        s.progPlaced = false;
                    }
                    if (stagedProbe && hadOne && sceneClip >= 0) {
                        const float dx = s.at[0] - wasAt[0], dz = s.at[2] - wasAt[2];
                        std::printf("    handover %ld actor %d %s  %.0f %.0f -> %.0f %.0f"
                                    "   gap %.0f\n", n, s.actor, s.model.c_str(),
                                    wasAt[0], wasAt[2], s.at[0], s.at[2],
                                    std::sqrt(dx * dx + dz * dz));
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
                // (c0) SHOOT MODE, ahead of the bank's idle: `Shoot_ActorEnter`
                // puts him in ACTOR_STATE 3 and his clips come from the area's
                // `.ani`, not from a `.CTL` he does not have.
                const omk::NodeTracks* shootTracks = nullptr;
                {
                    const int act = session.shootAction(s.actor);
                    // ---- THE GENERIC BRAIN, ticked (todo/shoot-mode.md 7d)
                    //
                    // `Shoot_TickNpc` calls the arm `Shoot_ActorEnter` chose;
                    // for 302 of the 306 shipped sites that is `sub_424DE0`,
                    // now transcribed whole (7c). The record is built once,
                    // out of the CHARACTER's own six properties, exactly as
                    // `sub_422540` does.
                    //
                    // WHAT IS SUPPLIED RATHER THAN COMPUTED, and it is the
                    // honest limit of this wiring: `sub_421020` (unread),
                    // `sub_421CD0` (unread) and `sub_435900`'s "am I there
                    // yet" all arrive as false, and no route or nav edge is
                    // handed over because the viewer has no path-finder on
                    // the grid yet. So the machine RUNS - it acquires, turns,
                    // engages and disengages on the real distances - and it
                    // does not yet WALK. A gunman aims at the player and
                    // holds his ground.
                    if (act >= 0 && shootMode) {
                        auto it = shootBrains.find(s.actor);
                        if (it == shootBrains.end()) {
                            omk::ShootRecord fresh;
                            std::int32_t props[6] = {0, 0, 0, 0, 0, 0};
                            omk::ShootProperties sp;
                            if (session.actorShootProperties(s.actor, props)) {
                                sp.health        = props[0];
                                sp.rangeAcquireM = props[1];
                                sp.rangeInnerM   = props[2];
                                sp.rangeThirdM   = props[3];
                                sp.coneDegrees   = props[4];
                                sp.behaviourBits = props[5];
                            }
                            omk::initShootRecord(fresh, sp);
                            fresh.state = 6;          // the hub, where a
                            fresh.node  = 0;          // gunman waits
                            it = shootBrains.emplace(s.actor, fresh).first;
                            std::printf("frame %ld: actor %d %s - shoot brain: "
                                        "acquire %.0f engage %.0f disengage %.0f "
                                        "cone %.3f health %d\n", n, s.actor,
                                        s.model.c_str(), it->second.rangeAcquire,
                                        it->second.rangeInner, it->second.rangeThird,
                                        it->second.coneCos, it->second.health);
                        }
                        omk::ShootRecord& rec = it->second;
                        const int before = rec.state;
                        omk::ShootFrameIn fin;
                        fin.self[0] = s.drawAt[0]; fin.self[1] = s.drawAt[1];
                        fin.self[2] = s.drawAt[2]; fin.self[3] = s.facing;
                        if (player) {
                            fin.target[0] = float(player->pos()[0]);
                            fin.target[1] = float(player->pos()[1]);
                            fin.target[2] = float(player->pos()[2]);
                            fin.target[3] = player->facing();
                        }
                        fin.dt = 1.0f;
                        fin.defaultClipType = act;
                        fin.targetAlive = true;
                        // the SIGHT half is real: the grid walk the port owns
                        fin.gridLineOfSight = true;
                        omk::AcquireOut ao;
                        const bool cone = omk::shootAcquires(rec, fin.self,
                                                             fin.target, ao, false);
                        omk::EngageIn ein;
                        ein.sameNode = true; ein.targetAlive = true;
                        ein.gridClear = true; ein.rayHits = true;
                        ein.coinHeads = ((s.actor * 2654435761u) >> 16) & 1;
                        const int eng = omk::shootEngage(rec, ao, cone, ein);
                        fin.targetPredicate = eng != 0;
                        fin.targetPredicateBits = eng;
                        fin.canFire = eng != 0;
                        float yaw = s.facing;
                        const auto st = omk::shootGenericStep(rec, fin, yaw);
                        s.facing = yaw;
                        if (rec.state != before || st.outcome == omk::ShootOutcome::Fire) {
                            if (!s.brainTold) {
                                s.brainTold = true;
                                std::printf("frame %ld: actor %d %s - brain %d -> %d, "
                                            "outcome %d, %.0f units away\n", n,
                                            s.actor, s.model.c_str(), before,
                                            rec.state, int(st.outcome), ao.dist3d);
                            }
                        }
                    }
                    if (act >= 0) {
                        const int grp = static_cast<int>(session.typeOfActor(s.actor));
                        const omk::PedClip* c = (grp >= 0 && grp < 64)
                                              ? shootClipFor(grp, act) : nullptr;
                        if (c) shootTracks = pedTracksFor(grp, *c, s.mo->meshes);
                        if (!s.shootTold && shootTracks) {
                            // THE INSTRUMENT omk-play 96 asked for: a staged
                            // actor never reported whether its clip resolved
                            // against its own model, so a body could blow
                            // apart while every line of the log read healthy.
                            // The unit-quaternion count is the discriminator -
                            // all 243362 quaternions in the shipped `.ani`
                            // corpus are unit, so a non-unit one here means
                            // the track offsets are being read against the
                            // WRONG BLOB, not that the pose maths is wrong.
                            int bound = 0;
                            for (auto id : shootTracks->ids) if (id >= 0) ++bound;
                            int nonUnit = 0;
                            for (const auto& row : shootTracks->quats)
                                for (const auto& q : row) {
                                    const double m = double(q.x) * q.x + double(q.y) * q.y
                                                   + double(q.z) * q.z + double(q.w) * q.w;
                                    if (m < 0.98 || m > 1.02) ++nonUnit;
                                }
                            std::printf("frame %ld: actor %d %s - %d/%zu tracks resolve "
                                        "against %zu meshes, %d frames, %d non-unit "
                                        "quaternions\n", n, s.actor, s.model.c_str(),
                                        bound, shootTracks->ids.size(),
                                        s.mo->meshes.size(), shootTracks->frames, nonUnit);
                            if (bound == 0) {
                                const auto dd = omk::animDescriptor(pedAni, c->descriptor);
                                std::printf("  clip tracks:");
                                if (dd) for (std::size_t q = 0; q < dd->tracks.size() && q < 6; ++q)
                                    std::printf(" %s", dd->tracks[q].name.c_str());
                                std::printf("\n  model meshes:");
                                for (std::size_t q = 0; q < s.mo->meshes.size() && q < 6; ++q)
                                    std::printf(" %s", s.mo->meshes[q].name);
                                std::printf("\n");
                            }
                        }
                        if (!s.shootTold) {
                            s.shootTold = true;
                            std::printf("frame %ld: actor %d %s - shoot mode, action %d, "
                                        "character type %d -> %s (%s, %zu clips in the "
                                        "group)\n", n, s.actor, s.model.c_str(), act, grp,
                                        c ? "a clip" : "NO CLIP",
                                        pedAniName.empty() ? "no .ani loaded"
                                                           : pedAniName.c_str(),
                                        shootClips.count(grp) ? shootClips[grp].size() : 0u);
                        }
                    }
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
                // Whether this frame is holding the last beat's pose. It
                // decides the YAW too: that pose already carries the clip's
                // root rotation, so the branch below must not add the world
                // heading over it - which spun Kay'l 147 degrees for exactly
                // the held frame and back again on the next.
                bool poseHeld = false;
                const char* src = "the rest pose (no bank clip)";
                if (useLine) {
                    const int frame = static_cast<int>(lineT);
                    // THE LATCH, on the line's first frame - what `Morph_Play`
                    // reads off the node once and `sub_42BE00` hands the morph
                    // for its whole length. `heading` is the node's world yaw
                    // less the Euler: the scene clip's root heading at the
                    // frame the line began. The clip's root goes INTO the
                    // morph's root here (`turnRootBy`), the Euler is kept
                    // beside it, and neither is re-read while the line plays -
                    // the program may end under it, as the goodbye's does.
                    // ...and it must be THIS line's tracks. The Session enters
                    // a line on its tick and the frontend rebuilds
                    // `speakerTracks` on the next frame's `lineChanged`, so
                    // the first `useLine` frame still holds the PREVIOUS
                    // line's. Latched then, a 451-frame line ran on a
                    // 135-frame copy and `composePose` clamped at 135: Telis
                    // frozen but for her mouth for the last two thirds of
                    // "Il y a quatre jours..." - a reader's report. The latch
                    // is re-taken when the tracks' line changes under it.
                    if (s.lineYawLatched && s.lineVoice != speakerVoice) s.lineYawLatched = false;
                    if (!s.lineYawLatched) {
                        s.lineVoice = speakerVoice;
                        const bool haveClip = s.sceneTracks.valid() && sceneClip >= 0 && run && run->loaded();
                        // ...at the frame the FADE blends from. `Morph_Play`
                        // hands the morph `rec[47]` (`sub_42BDD0`), and the
                        // fade-in slerps from the clip at that frame -
                        // `lineIdleFrame` here - toward the morph. The latch
                        // must read the clip at the SAME frame, or the two
                        // ends of the fade sit at different headings: read at
                        // the live `sceneFrame` instead, 402's greeting
                        // latched 73 against an idle end at 85 and she stepped
                        // 12 degrees on the line's first frame.
                        s.lineRootYaw = haveClip
                            ? omk::headingFromClipRoot(run->scene().clipData(sceneClip),
                                                       static_cast<int>(lineIdleFrame))
                            : (s.restYawKnown ? s.restYaw - (s.progYawKnown ? progYawSign * s.progYaw : 0.0f) : 0.0f);
                        s.lineYaw = s.progYawKnown ? progYawSign * s.progYaw : 0.0f;   // the Euler
                        s.lineTracks = speakerTracks;
                        turnRootBy(s.lineTracks, s.lineRootYaw);
                        s.lineYawLatched = true;
                    }
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
                                                            s.lineTracks, frame,
                                                            cancelLineRoot, w);
                        pose = omk::composePose(s.mo->meshes, mixed, 0, false);
                    } else {
                        pose = omk::composePose(s.mo->meshes, s.lineTracks, frame,
                                                cancelLineRoot);
                    }
                    // THE PLACEMENT IS THE SCENE CLIP'S, WHOLE, WHILE A LINE
                    // PLAYS. A line changes the POSE, never where the body
                    // stands: the morph player (08_wave.c) never writes the
                    // line's root translation into the node - `g_MorphRootTrack`
                    // is -2, so the `f32(node + 28) = frameTranslation` write
                    // matches no track - and instead rotates that translation
                    // by the Y euler and adds it to `g_MorphOrigin`, latched
                    // once at the start of the line (`dword_4EA8FC`). So a
                    // line's root is a DELTA from where the body already
                    // stands, and the scene clip's own root motion is still
                    // under it.
                    //
                    // This weighted it by the blend (`rootW = 1 - w`), so a
                    // speaking body drifted back to its bare staged placement
                    // and the fade lerped it there and back: a reader watching
                    // Telis in the restaurant saw her FLY between the line and
                    // the idle, and said which end was right - the idle's,
                    // because her face leaves the (correct) camera while she
                    // speaks. The 2026-09-02 reading that put the weight here
                    // had the same report ("alternate between flying and
                    // landing") and treated the position as something the fade
                    // owns; it is not (`todo/omk-play.md` 81).
                    rootW = 1.0f;
                    rootFrame = static_cast<int>(sceneFrame);
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
                } else if (shootTracks && shootTracks->valid()) {
                    pose = omk::composePose(s.mo->meshes, *shootTracks, 0, false);
                    src = "shoot mode: the area's .ani, by character type";
                } else if (!s.lastPose.empty() && session.parkedOnProgram()) {
                    // BETWEEN TWO BEATS OF ONE CUTSCENE the body holds the
                    // pose its last step left it in. The engine never resets a
                    // node - `Script_SelectBodyAnimation` writes the node's
                    // animation and nothing clears it when the program ends -
                    // and the chain is still in flight, so the next beat is
                    // about to pose him again. Falling back to the bank's idle
                    // here put Kay'l in his standing stance for the one frame
                    // between every pair of the Impasse's beats, which is what
                    // a reader saw once the camera stopped cutting away at the
                    // same moment (todo/omk-play.md 78).
                    //
                    // Scoped to the gap deliberately: a body with a bank and
                    // NOTHING coming IS driven by its channel
                    // (`Cef_TickChannel`), so the idle below stays the right
                    // answer everywhere else.
                    pose = s.lastPose;
                    poseHeld = true;
                    src = "the pose the last beat left (the chain is mid-flight)";
                } else if (s.idle.valid()) {
                    pose = omk::composePose(s.mo->meshes, s.idle, 0, false);
                    src = "the bank's default entry, frame 0";
                } else if (!s.lastPose.empty()) {
                    pose = s.lastPose;
                    src = "the last pose it was given (nothing drives it now)";
                } else {
                    pose = omk::composePose(s.mo->meshes, omk::NodeTracks{}, 0, false);
                }
                if (useLine || s.sceneTracks.valid() || (shootTracks && shootTracks->valid()) || s.idle.valid())
                    s.lastPose = pose;
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
                        // ...IN THE FRAME THE BODY WILL ACTUALLY BE DRAWN IN.
                        // A body a scene program drives is turned by the
                        // call's Euler (`progYaw`), not by its placement
                        // record's facing, and the aim was always backed out
                        // through `s.facing` - 0 for every one of the
                        // restaurant's diners while their Euler is 90. So the
                        // target sat 90 degrees off, the yaw pinned at
                        // `Actor_SetHeadLook`'s own +-70 clamp, and a diner
                        // the player is standing almost in front of stared
                        // sideways for as long as his zone held (a reader's
                        // frames, 2026-09-05: "head at 90 when I think they
                        // should be at 0"). Diner 71 wants -13 degrees.
                        const bool progTurned = s.sceneTracks.valid() && !useLine &&
                                                s.progYawKnown;
                        const float bodyYaw = progTurned ? progYawSign * s.progYaw : s.facing;
                        omk::rotateYaw(-bodyYaw, rel, local);
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
                // `OMK_TRACE_ACTOR=<id>` - one line a frame for one body:
                // where the port thinks he stands and where it last drew him,
                // with the three flags that decide between them. A pose
                // source changes hands rarely and the position between two
                // such changes is exactly what a per-change report cannot
                // show, which is how "he stays where the program left him"
                // could be printed and then not happen.
                static const char* traceEnv = std::getenv("OMK_TRACE_ACTOR");
                if (traceEnv && s.actor == std::atoi(traceEnv))
                    std::printf("  [trace] frame %ld actor %d  at %.0f %.0f %.0f"
                                "  drawAt %.0f %.0f %.0f  placed %d progPlaced %d"
                                "  progRan %d  sceneClip %d  yaw %.0f  src %s\n",
                                n, s.actor, s.at[0], s.at[1], s.at[2],
                                s.drawAt[0], s.drawAt[1], s.drawAt[2],
                                s.placed ? 1 : 0, s.progPlaced ? 1 : 0,
                                s.progRan ? 1 : 0, sceneClip,
                                static_cast<double>(s.drawnYawKnown ? s.drawnYaw : 0.0f), src);
                // A crowd model (the PSH/FSH family the city extras wear) is
                // four LOD skeletons in one file; posing one left the other
                // three at rest - a T-pose inside every couple and beggar.
                // The rest geometry is cut to the skeleton the tracks name.
                const omk::NodeTracks& posingTracks = useLine ? s.lineTracks
                                                     : s.sceneTracks.valid() ? s.sceneTracks : s.idle;
                const int skel = skeletonRootOf(*s.mo, posingTracks);
                const omk::Geometry& restUsed = skel == s.mo->root && s.mo->root >= 0 &&
                                                 !hasSeveralSkeletons(*s.mo)
                                                 ? s.mo->rest : lodRestFor(s.model, *s.mo, skel);
                s.shadowRoot = skel;
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
                    // ...TURNED BY THE ACTOR'S FACING, which is what
                    // `Anim_RootDelta` (0x004711D0) does and this file did
                    // not. Its second argument is `node+156`, and
                    // `Actor_LoadModel` binds that to `actor+288` -
                    // `sub_437140(node, actor + 288)` - which `Actors_TickAll`
                    // rebuilds every frame from the Euler at `actor+416..424`:
                    //
                    //     Matrix3x3_FromEulerAngles(a[104]*rad, a[105]*rad,
                    //                               a[106]*rad, a + 288);
                    //
                    // and the Euler is what `Script_SelectRelativeBodyAnimation`
                    // writes with `Actor_SetEuler(node, p4, p5, p6)` on the tick
                    // BEFORE it calls `Actor_MoveBy(node, delta)`. The tail of
                    // `Anim_RootDelta` is the row-vector multiply
                    //
                    //     out.x = m0*dx + m3*dy + m6*dz
                    //     out.y = m1*dx + m4*dy + m7*dz
                    //     out.z = m2*dx + m5*dy + m8*dz
                    //
                    // so for a yaw-only Euler it is the same `rotateYaw` the
                    // body is turned by. Without it the restaurant's waiter -
                    // `Serveur00`, Euler y 145 - played a walk cycle facing one
                    // way and travelled the other: a reader's report, "animation
                    // of walking forward, character moving backward".
                    // Note the Euler is STICKY, as it is in the engine: only
                    // the relative call writes it, `Script_SelectBodyAnimation`
                    // never does, and the actor record keeps the last one - so
                    // the waiter's ABS walk clips travel under the 145 his
                    // preceding REL wait left there.
                    static const bool noRootSpin =
                        std::getenv("OMK_NO_ROOTSPIN") != nullptr;
                    // ...and the SENSE is its own question: the body's spin and
                    // `Anim_RootDelta`'s multiply are different code paths in the
                    // engine and need not share it. `OMK_ROOTSPIN_NEG` turns the
                    // delta the other way while leaving the body alone.
                    static const float rootSpinSign =
                        std::getenv("OMK_ROOTSPIN_NEG") ? -1.0f : 1.0f;
                    if (!noRootSpin && s.progYawKnown && std::fabs(s.progYaw) > 0.01f) {
                        float r[3];
                        omk::rotateYaw(rootSpinSign * progYawSign * s.progYaw, rootMove, r);
                        for (int k = 0; k < 3; ++k) rootMove[k] = r[k];
                    }
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
                // THE FACING, for a body no scene clip is turning:
                // `Actor_SetEuler` is what a placement authors, while a scene
                // clip carries its own root orientation.
                //
                // A SPOKEN LINE DOES NOT CANCEL IT, and that was the fault a
                // reader met as *"she's looking at the wrong direction then
                // goes back to the correct one for the idle"*. Both this and
                // `progSpin` below carried `!useLine`, so while a line played
                // a body was drawn with NO yaw at all and turned to its real
                // one the moment the line ended. The line's own `.3DM` root
                // rotation is RELATIVE to the actor's frame - it is the
                // whole-body bow (CLAUDE.md 4) - so the frame still has to be
                // applied under it, and `tools/omkdata.py` says the same from
                // the other side, written when 387 was staged: "the clip's
                // root yaw is the character's facing for the WHOLE
                // conversation - the game keeps the scene orientation during
                // spoken lines too (compared frame-by-frame against a longplay
                // of 387)".
                const bool spin = !s.sceneTracks.valid() &&
                                  std::fabs(s.facing) > 0.01f;
                // a program's body turns by the call's Euler y about its pelvis
                // A DIAGNOSTIC, because a still frame is what decides this:
                // `node+156` (the actor's facing matrix, `Actor_LoadModel` ->
                // `sub_437140(node, actor+288)`) has exactly three readers in
                // the binary and all three are the ROOT-MOTION path
                // (`Anim_RootDelta`, `sub_471460`). Nothing in the draw walk
                // reads it - `Anim_ApplyNodeFrame` orients every node,
                // the pelvis included, from the CLIP's own quaternion. So the
                // call's Euler may well turn the motion and not the body.
                static const bool noProgSpin = std::getenv("OMK_NO_PROGSPIN") != nullptr;
                const bool progSpin = !noProgSpin && s.sceneTracks.valid() &&
                                      s.progYawKnown && std::fabs(s.progYaw) > 0.01f;
                // A LINE PLAYS IN THE FRAME THE NODE WAS IN WHEN IT STARTED.
                // `Morph_Play` (0x0041AFC0, CLEAN):
                //
                //     Matrix3x3_RotateVector(0,0,-1, node+92, &dir)   // the WORLD matrix
                //     heading = atan2(dirZ, dirX)*180/pi + 90 - actor[ACTOR_FACING]
                //     sub_42BE00(heading)                             // the morph's yaw
                //
                // and the morph player composes the .3DM root's rotation with
                // yaw(heading) (`08_wave.c` 963-965) under the node's own
                // facing matrix. `ACTOR_FACING` is +420, the Euler y, so
                // `heading` is the node's world yaw LESS the Euler - which is
                // the scene clip's own root orientation, the one
                // `Anim_ApplyNodeFrame` left on the pelvis. A line therefore
                // keeps the clip's root yaw plus the Euler, and its own root
                // goes on top. This viewer drew the .3DM alone and lost the
                // clip's yaw for the length of the line: Telis in profile
                // through "Oh Kay'l, je croyais ne plus jamais te revoir"
                // where the engine's own capture has her facing the lens,
                // then turning to face it when the idle came back.
                // THE BODY'S YAW, one decision. A scene clip's own root
                // quaternion is already in the pose (`Anim_ApplyNodeFrame`
                // orients the pelvis from it), so under a clip only the
                // step's Euler is added; a LINE's .3DM carries no world yaw,
                // so the node's heading at the moment `Morph_Play` ran - the
                // clip's root yaw plus the Euler - is latched and applied for
                // the whole line; and once the program is over the last
                // heading it left is kept.
                const float rootYawNow = (s.sceneTracks.valid() && sceneClip >= 0 && run && run->loaded())
                    ? omk::headingFromClipRoot(run->scene().clipData(sceneClip),
                                               static_cast<int>(sceneFrame)) : 0.0f;
                if (s.sceneTracks.valid() && !useLine) {
                    s.restYaw = (progSpin ? progYawSign * s.progYaw : 0.0f) + rootYawNow;
                    s.restYawKnown = true;
                }
                if (!useLine) s.lineYawLatched = false;
                // THE BODY'S YAW, one decision. Under a scene clip the clip's
                // own root quaternion is in the pose, so only the Euler is
                // added; under a LINE the clip's root heading is in the
                // morph's root (the latch above), so again only the Euler -
                // the latched one; after the program the last heading it
                // left is kept whole; a placement-only body spins by its
                // record about the origin.
                float bodyYaw = 0.0f;
                bool aboutPelvis = true;
                if (useLine)                      bodyYaw = s.lineYaw;
                else if (s.sceneTracks.valid())   bodyYaw = progSpin ? progYawSign * s.progYaw : 0.0f;
                else if (poseHeld && s.lastYawKnown) {
                    bodyYaw = s.lastBodyYaw;      // held whole, with its pose
                    aboutPelvis = s.lastAboutPelvis;
                }
                else if (s.restYawKnown)          bodyYaw = s.restYaw;
                else if (spin)                  { bodyYaw = s.facing; aboutPelvis = false; }
                // ...and the WORLD heading the body ends up drawn with, which
                // is what a camera hanging off him reads back.
                s.drawnYaw = poseHeld && s.lastYawKnown ? s.lastDrawnYaw
                           : useLine ? s.lineYaw + s.lineRootYaw
                           : s.sceneTracks.valid() ? bodyYaw + rootYawNow
                           : aboutPelvis ? bodyYaw : s.facing;
                s.drawnYawKnown = true;
                // ...and remember it, so the next held frame can hold it.
                s.lastBodyYaw = bodyYaw;
                s.lastAboutPelvis = aboutPelvis;
                s.lastDrawnYaw = s.drawnYaw;
                s.lastYawKnown = true;
                if (const int hd = omk::headMeshOf(s.mo->meshes);
                    hd >= 0 && static_cast<std::size_t>(hd) < pose.size()) {
                    const float hp[3] = {pose[static_cast<std::size_t>(hd)].pos[0] - pelvis[0],
                                         pose[static_cast<std::size_t>(hd)].pos[1] - pelvis[1],
                                         pose[static_cast<std::size_t>(hd)].pos[2] - pelvis[2]};
                    float r[3];
                    const bool spins = std::fabs(bodyYaw) > 0.01f;
                    omk::rotateYaw(spins && aboutPelvis ? bodyYaw : 0.0f, hp, r);
                    for (int k = 0; k < 3; ++k) s.headAt[k] = r[k] + pelvis[k] + off[k];
                    if (spins && !aboutPelvis) {
                        const float in[3] = {s.headAt[0], s.headAt[1], s.headAt[2]};
                        omk::rotateYaw(bodyYaw, in, r);
                        for (int k = 0; k < 3; ++k) s.headAt[k] = r[k];
                    }
                    s.headKnown = true;
                }
                // ...and every mesh, on the same transform, for the shadow.
                s.meshAt.assign(pose.size() * 3, 0.0f);
                for (std::size_t mi = 0; mi < pose.size(); ++mi) {
                    const float mp[3] = {pose[mi].pos[0] - pelvis[0],
                                         pose[mi].pos[1] - pelvis[1],
                                         pose[mi].pos[2] - pelvis[2]};
                    float r[3];
                    const bool spins = std::fabs(bodyYaw) > 0.01f;
                    omk::rotateYaw(spins && aboutPelvis ? bodyYaw : 0.0f, mp, r);
                    for (int k = 0; k < 3; ++k) r[k] += pelvis[k] + off[k];
                    if (spins && !aboutPelvis) {
                        const float in[3] = {r[0], r[1], r[2]};
                        omk::rotateYaw(bodyYaw, in, r);
                    }
                    for (int k = 0; k < 3; ++k) s.meshAt[mi * 3 + static_cast<std::size_t>(k)] = r[k];
                }
                const bool turn = std::fabs(bodyYaw) > 0.01f;
                for (auto& c : s.posed.corners) {
                    if (turn && !aboutPelvis) {
                        const float in[3] = {c.x, c.y, c.z};
                        float r[3];
                        omk::rotateYaw(bodyYaw, in, r);
                        c.x = r[0]; c.y = r[1]; c.z = r[2];
                    } else if (turn) {
                        const float in[3] = {c.x - pelvis[0], c.y - pelvis[1], c.z - pelvis[2]};
                        float r[3];
                        omk::rotateYaw(bodyYaw, in, r);
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
                // ---- A BODY THAT JUMPS, measured on the body and not on the
                // picture. `OMK_BODYLOG` prints, per staged body per frame,
                // where it was DRAWN and how far its posed vertices moved
                // since the last frame. A pop - a beat hand-over that changes
                // the owner, a placement record clobbering a program's
                // position, a pose falling back to an idle - shows as a step
                // in one or both; walking and animating show as a small
                // steady value. A diagnostic: nothing draws differently.
                if (std::getenv("OMK_BODYLOG")) {
                    double moved = 0.0;
                    std::size_t nc = s.posed.corners.size();
                    if (s.poseWas.size() == nc * 3 && nc) {
                        for (std::size_t k = 0; k < nc; ++k) {
                            const double dx = s.posed.corners[k].x - s.poseWas[k * 3];
                            const double dy = s.posed.corners[k].y - s.poseWas[k * 3 + 1];
                            const double dz = s.posed.corners[k].z - s.poseWas[k * 3 + 2];
                            moved += std::sqrt(dx * dx + dy * dy + dz * dz);
                        }
                        moved /= static_cast<double>(nc);
                    }
                    s.poseWas.resize(nc * 3);
                    for (std::size_t k = 0; k < nc; ++k) {
                        s.poseWas[k * 3]     = s.posed.corners[k].x;
                        s.poseWas[k * 3 + 1] = s.posed.corners[k].y;
                        s.poseWas[k * 3 + 2] = s.posed.corners[k].z;
                    }
                    std::printf("  [body] frame %ld actor %d  at %.1f %.1f %.1f"
                                "  yaw %.1f  vertsMoved %.2f  src %s\n",
                                n, s.actor, s.drawAt[0], s.drawAt[1], s.drawAt[2],
                                bodyYaw * 57.2957795f, moved, src ? src : "-");
                }
                // ON THE FLOOR? A scene program's body walks its clip's root
                // motion, and the one invariant that motion has to satisfy is
                // that it stays on the set's own walkable mesh - which is what
                // decides the SENSE the delta is turned in, over a corpus
                // rather than over one waiter.
                if (stagedProbe && (n % 20) == 0 && !playerSoup.empty() &&
                    s.sceneTracks.valid()) {
                    const auto g = omk::floorUnder(playerSoup, s.drawAt[0],
                                                   s.drawAt[1] - 120.0, s.drawAt[2]);
                    std::printf("    floor %ld actor %d %s at %.0f %.0f %.0f  %s\n", n,
                                s.actor, s.model.c_str(), s.drawAt[0], s.drawAt[1],
                                s.drawAt[2], g ? "on the walk mesh" : "OFF the walk mesh");
                }
                // THE HEAD'S TWIST against the shoulders, in degrees - a
                // number for the reader's "head at 90 degrees". The engine
                // clamps its own head look to +-70 of yaw and +-40 of pitch
                // (`Actor_SetHeadLook`, 0x00468B50, and the player's free look
                // in `Actors_TickAll` uses the same two), so anything past 70
                // cannot have come from that path and is the CLIP's or the
                // composition's.
                if (stagedProbe && (n % 100) == 0 && s.mo->root >= 0) {
                    const int hd = omk::headMeshOf(s.mo->meshes);
                    if (hd >= 0 && static_cast<std::size_t>(hd) < pose.size() &&
                        static_cast<std::size_t>(s.mo->root) < pose.size()) {
                        const float fwd[3] = {0.0f, 0.0f, -1.0f};
                        float hf[3], rf[3];
                        omk::qrot(pose[static_cast<std::size_t>(hd)].q, fwd, hf);
                        omk::qrot(pose[static_cast<std::size_t>(s.mo->root)].q, fwd, rf);
                        const float d = std::atan2(rf[0] * hf[2] - rf[2] * hf[0],
                                                   rf[0] * hf[0] + rf[2] * hf[2]) * 57.29578f;
                        std::printf("    head %ld actor %d %s twist %.0f deg from the pelvis"
                                    "%s\n", n, s.actor, s.model.c_str(), d,
                                    std::fabs(d) > 70.0f ? "   <- past the engine's own "
                                                           "+-70 clamp" : "");
                    }
                }
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
            pedLit = 0;
            vehDrawn = vehLive = vehStopped = 0;
            {
                const auto& pd = session.sliders();
                const auto& rs = session.residentSlot(session.activeSlot());
                // `Area_TickLoad` case 7 loads `ANIMS\<+124>.ANI` for the area
                // whether or not it has a slider circuit - the crowd is only
                // one of its consumers, and a shoot-mode character with no
                // `.CTL` is another. So this no longer waits on `pd.loaded()`.
                if (!rs.ani.empty() && rs.ani != pedAniName) {
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
                pedFootOffMax = 0.0f;
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
                        // the normal turns with the walker and does not move
                        const float n[3] = {c.nx, c.ny, c.nz};
                        float rn[3];
                        omk::rotateYaw(w.facing, n, rn);
                        c.nx = rn[0]; c.ny = rn[1]; c.nz = rn[2];
                    }
                    // `Slider_PlaceShadow`'s two nodes, on the same transform.
                    p.footKnown = false;
                    {
                        const int fi[2] = {
                            omk::findMeshContaining(p.mo->meshes, "Piedg", lodRoot),
                            omk::findMeshContaining(p.mo->meshes, "Piedd", lodRoot)};
                        if (fi[0] >= 0 && fi[1] >= 0 &&
                            static_cast<std::size_t>(fi[0]) < pose.size() &&
                            static_cast<std::size_t>(fi[1]) < pose.size()) {
                            for (int f = 0; f < 2; ++f) {
                                const auto& mp = pose[static_cast<std::size_t>(fi[f])].pos;
                                const float in[3] = {mp[0] - rootXZ[0], mp[1], mp[2] - rootXZ[1]};
                                float r[3];
                                omk::rotateYaw(w.facing, in, r);
                                p.footAt[f][0] = r[0] + w.body[0];
                                p.footAt[f][1] = r[1] + w.body[1] + w.footY - p.feet;
                                p.footAt[f][2] = r[2] + w.body[2];
                            }
                            p.footKnown = true;
                            // THE INVARIANT THAT WOULD HAVE CAUGHT THE ORPHANS.
                            // A walker's feet are within a stride of his own
                            // body point; a bone taken off the wrong LOD
                            // skeleton is 240 units away. Measured every frame
                            // and reported, because "a shadow with no origin"
                            // is only visible to a person and this is not.
                            const float mx = (p.footAt[0][0] + p.footAt[1][0]) * 0.5f - w.body[0];
                            const float mz = (p.footAt[0][2] + p.footAt[1][2]) * 0.5f - w.body[2];
                            const float off = std::sqrt(mx * mx + mz * mz);
                            if (off > pedFootOffMax) pedFootOffMax = off;
                        }
                    }
                    // THE DYNAMIC LIGHTS (`o3de/vertexlight.h`). `sub_4380B0`
                    // registers a walker in the same structure the set's
                    // lights went into and `sub_48D7F0` applies every one that
                    // reaches it, per frame, before submitting. Both resident
                    // slots contribute, because during a transition two sets
                    // are drawn and the engine's structure holds both.
                    // ...unless the GPU is going to do it per pixel, in which
                    // case the corners keep their black base and `Draw::lit`
                    // carries the decision to the shader.
                    if (lightCrowd && lighting == 0) {
                        // THE BASE IS BLACK, and that is the part that had to
                        // be read rather than assumed. A lit instance does not
                        // start from the model's baked vertex colour: the lit
                        // path `sub_494E80` writes `instance[+416]` into every
                        // runtime vertex's colour, and every site that sets
                        // +416 sets it to 0. The crowd models ship pure white
                        // (all 446 of PSH_FN's vertices are 255,255,255), so
                        // there is no baked light in them to keep - the .3DO
                        // lights ARE their lighting, and adding to white is
                        // what made this port's first attempt change exactly
                        // zero pixels.
                        for (auto& c : p.posed.corners) { c.r = 0.0f; c.g = 0.0f; c.b = 0.0f; }
                        const float at[3] = {w.body[0], w.body[1], w.body[2]};
                        for (const WorldSlot& ws2 : worldSlots)
                            if (!ws2.lights.empty())
                                pedLit += omk::applyLights(p.posed, 0, p.posed.corners.size(),
                                                           at, ws2.lights);
                    }
                    p.posed.revision = ++worldGeoRev;
                    p.drawn = true;
                    ++pedDrawn;
                }
                if (pedLive && (pedTold < 0 || n - pedTold >= 300)) {
                    pedTold = n;
                    std::printf("frame %ld: pedestrians - %d live, %d drawn within %.0f of the eye, "
                                "%d at an action point, %d idling, %d light hits\n",
                                n, pedLive, pedDrawn, reach, pedInAction, pedIdle, pedLit);
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
                    // ...and SAY why the player's own slider is not staged, once
                    // per reason: after a load into another city it vanished
                    // from the picture with the log silent about it.
                    const bool mine = static_cast<int>(i) == pd.calledVehicle();
                    static int calledSkipTold = -1;
                    auto skipMine = [&](int why, const char* what) {
                        if (mine && calledSkipTold != why) {
                            calledSkipTold = why;
                            std::printf("frame %ld: the called vehicle (slot %zu, model '%s') is NOT staged: %s\n",
                                        n, i, v.model.c_str(), what);
                        }
                    };
                    if (!v.live || v.mover < 0) { skipMine(1, "not live / no mover"); continue; }
                    const auto& m = pd.movers()[static_cast<std::size_t>(v.mover)];
                    ++vehLive;
                    if (m.flags & 0x100u) ++vehStopped;
                    const float vx = m.body[0] - view.cam.eye[0], vy = m.body[1] - view.cam.eye[1],
                                vz = m.body[2] - view.cam.eye[2];
                    if (vx * vx + vy * vy + vz * vz > vreach * vreach) { skipMine(2, "beyond the vehicle LOD reach"); continue; }
                    if (!sv.mo) sv.mo = charModelFor(v.model);
                    if (!sv.mo || !sv.mo->ready) { skipMine(3, "its model did not load"); continue; }
                    if (mine && calledSkipTold != 0) { calledSkipTold = 0; std::printf("frame %ld: the called vehicle (slot %zu, model '%s') is staged at %.0f %.0f %.0f\n", n, i, v.model.c_str(), m.body[0], m.body[1], m.body[2]); }
                    // ---- `sub_4521E0`, THE MODEL SWAP ----------------------
                    // The slider model TABLE (`dword_538E28`, 88-byte rows)
                    // holds its four root sub-objects at +4..+16 sorted
                    // heaviest first by `sub_453A70` (vertices + faces):
                    // SlBassin 1527 corners, slider_fl 750, SlBasA 366, SlBasB
                    // 144. The reserved slider is created on +8 (slider_fl -
                    // `CharModel::root` here, a shell with no interior and NO
                    // DOOR: both door meshes hang off SlBassin) and
                    // `sub_4521E0` toggles the sub-node to +4, SlBassin, the
                    // COCKPIT, at `MDACTION`'s snap - which is what puts a
                    // door under the clip - and `sub_4570F0` toggles it back
                    // before the exit clip. A reader saw the shell: *"the
                    // current model when Kay'l enters the slider has no
                    // modelised interior"*.
                    const bool swapped = (boarding || boarded) && static_cast<int>(i) == pd.calledVehicle();
                    const int wantRoot = swapped ? heaviestRootOf(*sv.mo)
                                                 : (v.lodBase == 0 ? heaviestRootOf(*sv.mo) : sv.mo->root);
                    if (sv.built && sv.lodRoot != wantRoot) sv.built = false;
                    if (!sv.built) {
                        sv.lodRoot = wantRoot;
                        if (static_cast<int>(i) == pd.calledVehicle() &&
                            wantRoot >= 0 && static_cast<std::size_t>(wantRoot) < sv.mo->meshes.size())
                            std::printf("slider: the called vehicle is staged on sub-object "
                                        "'%s'%s\n", sv.mo->meshes[static_cast<std::size_t>(wantRoot)].name,
                                        swapped ? " - the COCKPIT, sub_4521E0's swap" : "");
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
                    // ...unless this is the slider he is CLIMBING INTO, in
                    // which case its door is posed from the clip at the
                    // character's own frame - the engine's shared clock.
                    bool doorPosed = false;
                    if ((boarding || leaving) && static_cast<int>(i) == pd.calledVehicle() && player) {
                        if (!doorClipsRead) {
                            doorClipsRead = true;
                            const auto a = fs.read("ANIMS/SLF_112.3DA");
                            const auto b = fs.read("ANIMS/SLF_113.3DA");
                            if (!a.empty()) doorIn  = omk::clipTracks(a);
                            if (!b.empty()) doorOut = omk::clipTracks(b);
                        }
                        const omk::NodeTracks& dt0 = boarding ? doorIn : doorOut;
                        // THE TRACKS ARE KEYED BY NODE ID, NOT MESH INDEX. The
                        // clip names its five tracks with the bytes 0,3,1,4,2
                        // and `SLI_FN.3DO`'s ids run SlBassin 0, SlPorteZG 1,
                        // SlPorteG 2, SlPorteZD 3, SlPorteD 4 - the cockpit and
                        // its four door parts, exactly a door clip's set. Read
                        // as indices they land on two SHELLS, and the one
                        // moving track fell on the wrong panel. `clipTracks`
                        // fills `ids` as indices (right for the scene clips it
                        // was written for); remapped through the model's ids.
                        omk::NodeTracks dt = dt0;
                        // BY NAME (+4 of the track header), not by id or index:
                        // the mover is named `SlPorteZG`, the parent copy on the
                        // G side; its child `SlPorteG` follows it, so the two
                        // coincident copies swing together and nothing stays
                        // over the hole. Read by index the swing landed on
                        // `SlPorteG` alone; by id on `SlPorteD`, the far door.
                        for (std::size_t ti = 0; ti < dt.ids.size(); ++ti) {
                            int idx = -1;
                            const std::string& nm = ti < dt.names.size() ? dt.names[ti] : std::string();
                            for (std::size_t k = 0; k < sv.mo->meshes.size(); ++k)
                                if (nm == sv.mo->meshes[k].name) { idx = static_cast<int>(k); break; }
                            dt.ids[ti] = idx;
                        }
                        static bool doorTold = false;
                        if (!doorTold && dt.valid()) {
                            doorTold = true;
                            std::printf("slider: the door clip's tracks by name ->");
                            for (auto id : dt.ids)
                                std::printf(" %s", id >= 0 ? sv.mo->meshes[static_cast<std::size_t>(id)].name : "?");
                            std::printf("\n");
                        }
                        if (dt.valid()) {
                            int f = player->poseFrame();
                            if (f < 0) f = 0;
                            if (f >= dt.frames) f = dt.frames - 1;
                            const omk::Geometry& rest = lodRestFor(v.model, *sv.mo, sv.lodRoot);
                            const auto dp = omk::composePose(sv.mo->meshes, dt, f, false);
                            omk::applyPose(sv.posed, rest, sv.mo->meshes, dp);
                            doorPosed = true;
                        }
                    }
                    if (!doorPosed) sv.posed = sv.atRest;
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
                // THE ANCHOR AND THE DROP MUST SHARE ONE ORIGIN
                // (`todo/player-vertical.md` step 1, 2026-09-09).
                //
                // The reader: *"walking or running seems to rise its y
                // position and stopping (and so returning to the idle
                // position) reset the y position to a normal one"*. It is not
                // the clips: measured with `tools/vertical_probe`, `H_WALK`
                // f0 lifts the body's lowest corner 2.06 above the standing
                // one while its pelvis track drops 2.09, so the authored pair
                // CANCELS to 0.03 - the planted foot stays planted, which is
                // the "authored motion netting out" the engine relies on.
                //
                // What floated was this port's bookkeeping. `playerFeet` is a
                // POSE's lowest corner, latched from `H_STAND` - whose pelvis
                // trans is +0.68, not zero - while `rootAccum` below re-bases
                // to the ENTERED clip's first frame: +2.09 for `H_WALK`,
                // +3.68 for `H_RUN`. The two halves therefore measured from
                // different origins, and the body was drawn a CONSTANT
                // 2.09-0.68 = 1.41 too high walking and 3.68-0.68 = 3.00 too
                // high running, snapping back the moment `H_STAND` released
                // the sum. That is the report exactly, running rising more.
                //
                // So the anchor records the pelvis trans it was taken at
                // (`playerRootRef`) and the drop is measured from it.
                //
                // AND IT IS LATCHED FROM `H_STAND` SPECIFICALLY, not from
                // whatever pose happened to be up on the first frame: the
                // anchor is meant to be a model constant, and one taken off an
                // arbitrary pose carries that pose's own vertical into every
                // frame afterwards. A provisional latch draws until the idle
                // first comes round, and is then replaced once.
                //
                // Cross-checked against the engine's own constant, which is a
                // different quantity and does not substitute for this one:
                // `Walk_ProbeGround` (0x00467030) seats by the model's
                // COLLISION SPHERES - `Collision_BodySphere`'s largest radius
                // (10.91) plus `sub_4443B0`'s second-lowest centre.y (14.98) -
                // and HO1_FN's lowest sphere reaches 41.81 below the node
                // against a standing visual foot at 38.76 + 0.68 = 39.44. The
                // sphere hangs 2.37 BELOW the feet, and where the engine
                // absorbs that is `actor+276`, the reference its clearance
                // `actor[264] - actor[276]` is taken against - which is
                // UNTRACED. So the sphere constant is not used as the seat
                // here; it is quoted because it corroborates the scale.
                const bool standing = player->clipName() == "H_STAND";
                if (!playerFeetKnown || (standing && !playerStandLatched)) {
                    playerFeet = -1e9f;
                    for (const auto& c : playerPosed.corners)
                        if (c.y > playerFeet) playerFeet = c.y;
                    playerRootRef = (pt && !pt->trans.empty())
                                        ? pt->trans[static_cast<std::size_t>(
                                              player->poseFrame() > 0 ? player->poseFrame() : 0)][1]
                                        : 0.0f;
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
                    if (standing) playerStandLatched = true;
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
                    // ...and NOT while the slider clips carry him. In ACTOR_STATE
                    // 6 and 8 the clip's root y is applied to his POSITION
                    // (`Actor_MoveBy`, the channel-only tick), so adding the
                    // same travel here as a drop put him one clip's descent
                    // (12.5) too low in the seat - the reader: *"he is just a
                    // bit too low"*.
                    if (boarding || leaving) rootAccum = 0.0f;
                    rootDrop = rootAccum;
                    // ...EXCEPT WHERE THE BODY IS ON THE GROUND BY DEFINITION,
                    // which is every LOOPING clip - the idle, the walk, the
                    // run (`todo/player-vertical.md` step 1).
                    //
                    // The accumulator above exists for the TAKE, whose clips
                    // CHAIN: H_TAKL12 crouches +24.3 and H_TAKL22 carries that
                    // crouch back up, so each reads as a delta on the last and
                    // an absolute reading of either is meaningless (it is the
                    // regression recorded in the note above - H_TAKL22 run as
                    // an absolute goes 0 -> -24.6, from standing to 62 cm
                    // ABOVE it). A locomotion clip is the opposite: it is one
                    // self-contained cycle that begins and ends on the floor,
                    // so its pelvis trans is an OFFSET FROM THE STANDING POSE
                    // and must be read against the origin the anchor was
                    // latched at, never against whatever the last state left
                    // in the sum.
                    //
                    // The discriminator is structural, not a list of names:
                    // `variantCount() > 1` is what marks the grid/take states
                    // (it is what selects `gridTracks`, and what the crouch
                    // trace below is gated on). Everything else loops.
                    //
                    // H_STAND lands on cur 0.68 - ref 0.68 = 0 by
                    // construction, which is why the explicit release below it
                    // was able to look correct while the walk was 1.41 out.
                    if (player->variantCount() <= 1) rootDrop = cur - playerRootRef;
                    // MEASURING, not fixing: how far does the model's own
                    // lowest point travel across a take? If the rotations
                    // lower the body, a CONSTANT anchor is right and the
                    // float came from somewhere else; if it barely moves
                    // while the legs bend, the body never crouches at all.
                    // OMK_PLY=N prints every Nth frame (1 for every frame, which is what a
                    // jump needs - its whole arc is 14 frames and a stride of 4
                    // steps straight over the apex).
                    static const long plyEvery = [] {
                        const char* e = std::getenv("OMK_PLY");
                        const long v = e ? std::atol(e) : 0;
                        return v > 1 ? v : 1;
                    }();
                    if (std::getenv("OMK_PLY") && (n % plyEvery) == 0) {
                        float lo = -1e9f, hi = 1e9f;
                        for (const auto& c : playerPosed.corners) {
                            if (c.y > lo) lo = c.y;
                            if (c.y < hi) hi = c.y;
                        }
                        // `foot` is the one that matters and the one this
                        // print did NOT carry: `lo` is a MODEL-space corner
                        // (this runs before the offset loop below), so its
                        // `gap` against the ground is not a distance on
                        // screen. The drawn foot sits at
                        // `lo + pos.y - playerFeet + rootDrop`, so its height
                        // above the floor is `lo - playerFeet + rootDrop` -
                        // NEGATIVE is above, y growing down. That is the
                        // number `verify.py: engine player vertical` asserts
                        // and the one the walk float showed up in.
                        std::printf("DBG ply f%ld %-9s pf %2d  ground %+8.2f  lowest %+8.2f"
                                    "  gap %+7.2f  head %+8.2f  rootDrop %+6.2f  foot %+7.2f"
                                    "  at %+9.2f %+9.2f  air %d\n",
                                    n, player->clipName().c_str(), player->poseFrame(),
                                    player->pos()[1], lo, lo - player->pos()[1], hi,
                                    rootDrop, lo - playerFeet + rootDrop,
                                    player->pos()[0], player->pos()[2],
                                    player->walker().airborne() ? 1 : 0);
                    }
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
                float yaw = player->facing();
                // ---- ...AND THE SLIDER OWNS HIM WHILE HE BOARDS ----------
                //
                // `MDACTION` and `sub_468FA0` both end with
                // `sub_437140(node, M_slider)`, which writes the SLIDER's
                // matrix into the actor node's `+156` - and `+156` is read as
                // an ORIENTATION (21_d3d.c 2854 takes
                // `Matrix3x3_RotateVector(0, 0, -1, node+156)` as the node's
                // heading). So for the whole of ACTOR_STATE 6 and 8 the body
                // is drawn in the vehicle's frame, which is what squares him
                // to the door however he walked up to it.
                //
                // The engine's forward is `-row2`: `sub_456530` case 7 tests
                // `(sin y, 0, -cos y)` against the player's own +420, and the
                // vehicle matrix's row 2 is the negated travel direction. So
                // the drawn yaw is `atan2(-row2.x, row2.z)`.
                //
                // THIS is the line that had to change. Writing the same yaw
                // into `Session::setPlayerPosition` instead - which is what
                // the first attempt did - updates the logical player record
                // and NOTHING on screen, because the model is posed from
                // `player->facing()` right here. A reader saw exactly that:
                // *"I don't see any change"*.
                if (boarding || leaving) {
                    float bat[3], bx[3], bz[3];
                    if (session.sliders().calledFrame(bat, bx, bz))
                        yaw = static_cast<float>(
                            std::atan2(-bz[0], bz[2]) * 57.29577951308232);
                }
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
                if (const int hd = omk::headMeshOf(playerMeshes);
                    hd >= 0 && static_cast<std::size_t>(hd) < pose.size()) {
                    const float in[3] = {pose[static_cast<std::size_t>(hd)].pos[0] - playerRootXZ[0],
                                         pose[static_cast<std::size_t>(hd)].pos[1],
                                         pose[static_cast<std::size_t>(hd)].pos[2] - playerRootXZ[1]};
                    float r[3];
                    omk::rotateYaw(yaw, in, r);
                    playerHeadAt[0] = r[0] + pp[0];
                    playerHeadAt[1] = r[1] + pp[1] - playerFeet + rootDrop;
                    playerHeadAt[2] = r[2] + pp[2];
                    playerHeadKnown = true;
                }
                playerMeshAt.assign(pose.size() * 3, 0.0f);
                for (std::size_t mi = 0; mi < pose.size(); ++mi) {
                    const float in[3] = {pose[mi].pos[0] - playerRootXZ[0],
                                         pose[mi].pos[1],
                                         pose[mi].pos[2] - playerRootXZ[1]};
                    float r[3];
                    omk::rotateYaw(yaw, in, r);
                    playerMeshAt[mi * 3 + 0] = r[0] + pp[0];
                    playerMeshAt[mi * 3 + 1] = r[1] + pp[1] - playerFeet + rootDrop;
                    playerMeshAt[mi * 3 + 2] = r[2] + pp[2];
                }
                playerMeshAtKnown = true;
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
            for (int k = 0; k < 3; ++k) spriteAnchor[k] = view.cam.at[k];
            spriteAnchorSet = true;
            const bool scriptSprites = !noScriptSprites && session.scene().loaded() && !session.scene().sprites().empty();
            if ((session.scene().effects().count() || !ctlSprites.empty() || scriptSprites) && !spriteTex.empty()) {
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
                // THE SCRIPTED SPRITES - `Script_Display3DSprite` and its
                // family (program.h). An instance the scene has linked is
                // drawn every frame from its own fields: frame `+22`, type
                // `+20` (the blend mode), the two scales, the roll. The
                // engine walks the scene's `+36` list after the effects, and
                // `Sprite_SetFrame` hides an instance whose frame is past the
                // sprite's count by writing 0xFFFF, which the clamp here
                // stands in for.
                if (scriptSprites) {
                    omk::ParticleField scField;
                    for (const auto& kv : session.scene().sprites()) {
                        const auto& sp = kv.second;
                        if (!sp.linked || sp.id < 0 ||
                            static_cast<std::size_t>(sp.id) >= spriteFr.size() ||
                            spriteFr[static_cast<std::size_t>(sp.id)].frames.empty()) continue;
                        const int n = static_cast<int>(spriteFr[static_cast<std::size_t>(sp.id)].frames.size());
                        if (sp.frame < 0 || sp.frame >= n) continue;   // 0xFFFF: not drawn
                        if (spriteLogged[sp.row] != sp.linkedAt + 1) {
                            spriteLogged[sp.row] = sp.linkedAt + 1;
                            std::printf("sprite: LINKED row %d id %d type %d frame %d/%d scale %.2f/%.2f at %.0f,%.0f,%.0f (scene tick %ld)\n",
                                        sp.row, sp.id, sp.type, sp.frame, n, sp.sx, sp.sy,
                                        sp.pos[0], sp.pos[1], sp.pos[2], sp.linkedAt);
                        }
                        omk::Particle p;
                        for (int k = 0; k < 3; ++k) p.pos[k] = sp.pos[k];
                        p.life = 1.0f;
                        p.frame = sp.frame;
                        p.scale = sp.sx;
                        p.scaleY = sp.sy;
                        p.angle = sp.roll;
                        p.sprite = sp.id;
                        p.mode = static_cast<std::uint8_t>(sp.type & 0xF);
                        scField.addParticle(p);
                    }
                    omk::Geometry scGeo;
                    omk::particleGeometry(scGeo, scField, view.cam.eye, view.cam.at, spriteFr);
                    const std::size_t base = fxGeo.corners.size();
                    for (omk::Batch b : scGeo.batches) { b.start += base; fxGeo.batches.push_back(b); }
                    fxGeo.corners.insert(fxGeo.corners.end(), scGeo.corners.begin(), scGeo.corners.end());
                    fxGeo.cornerMirror.insert(fxGeo.cornerMirror.end(), scGeo.cornerMirror.begin(), scGeo.cornerMirror.end());
                    fxGeo.cornerMesh.insert(fxGeo.cornerMesh.end(), scGeo.cornerMesh.begin(), scGeo.cornerMesh.end());
                    fxGeo.cornerVertex.insert(fxGeo.cornerVertex.end(), scGeo.cornerVertex.begin(), scGeo.cornerVertex.end());
                    fxGeo.cornerDeclared.insert(fxGeo.cornerDeclared.end(), scGeo.cornerDeclared.begin(), scGeo.cornerDeclared.end());
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
            // THE SKY, placed and submitted before anything else.
            //
            // `sub_41CF10` sets the node to the camera's x and z and its own
            // y, so the plane slides with you and never approaches; the
            // corners are rebuilt from the loaded ones about node 0's origin,
            // scaled 12.5x. Options row 4 turns it off by UNLINKING the node,
            // which is simply not submitting it.
            //
            // Its bucket state is 0x800 and not computed from the blend flags:
            // `Area_LoadMiscModel` sets mesh flag 0x10000, and 0x10000's line
            // in `Render_SubmitMesh` is an ASSIGNMENT - `state = 0x800` - which
            // wipes everything else. That is also why the sky never picks up
            // the far-bucket bit (`!(key & 0x800)` guards it) and why its fog
            // range is DOUBLED. It clears 0x3000 too, so the sky is opaque
            // whatever the material says.
            if (drawSky && !sky.base.empty() && !sky.geo.batches.empty()) {
                const float sx = view.cam.eye[0], sz = view.cam.eye[2];
                const float sy = sky.origin[1] - kSkyLift;
                for (std::size_t k = 0; k < sky.base.size(); ++k) {
                    const omk::Corner& b0 = sky.base[k];
                    omk::Corner& c = sky.geo.corners[k];
                    c = b0;
                    c.x = sx + (b0.x - sky.origin[0]) * kSkyScale;
                    c.y = sy + (b0.y - sky.origin[1]) * kSkyScale;
                    c.z = sz + (b0.z - sky.origin[2]) * kSkyScale;
                }
                sky.geo.revision = ++worldGeoRev;
                for (const auto& b : sky.geo.batches)
                    draws.push_back({0x800u | ((static_cast<std::uint32_t>(b.material) +
                                                static_cast<std::uint32_t>(sky.texBase)) & 0x3Fu),
                                     &sky.geo, b.start, b.count, omk::Blend::Opaque, false});
            }

            // THE VISIBLE SET, and it is the CLIP DISTANCE that sizes it.
            //
            // `sub_48D3B0` walks the scene's meshes and keeps one when
            // `radius + dword_6A2B9C` exceeds its distance from the camera -
            // and `dword_6A2B9C` is the scene's `+340`, which is options row
            // 3 in metres times 39.37 (`platform/settings.h`). So the option
            // is exactly this radius, and at 25 m ("Tres proche") a street
            // ends a few buildings away.
            //
            // What is NOT applied here: the engine follows that test with the
            // four SIDE planes, built from the same global at `25_sys.c`
            // 11598. Those are the camera's business rather than the option's,
            // this backend already clips to the viewport, and a wrong plane
            // sign deletes the world silently - so only the distance half is
            // taken, and the visible set is a superset of the engine's.
            const float clipReach = static_cast<float>(clipInches);
            std::size_t runsDrawn = 0, runsCulled = 0;
            for (int slot = 0; slot < 2; ++slot) {
                const WorldSlot& w = worldSlots[static_cast<std::size_t>(slot)];
                const std::uint32_t texBase = static_cast<std::uint32_t>(worldTexBase[slot]);
                if (w.runs.empty()) {          // no per-corner mesh: whole batches
                    for (const auto& b : w.geo.batches)
                        draws.push_back({keyOf(b.blend, b.cutout,
                                               static_cast<std::uint32_t>(b.material) + texBase),
                                         &w.geo, b.start, b.count, b.blend, b.cutout});
                    continue;
                }
                // One test per mesh, cached across its runs.
                std::vector<std::uint8_t> vis(w.meshes.size(), 2);   // 2 = untested
                const auto visible = [&](std::int32_t mi) -> bool {
                    if (mi < 0 || static_cast<std::size_t>(mi) >= w.meshes.size()) return true;
                    std::uint8_t& v = vis[static_cast<std::size_t>(mi)];
                    if (v != 2) return v != 0;
                    const omk::Mesh& m = w.meshes[static_cast<std::size_t>(mi)];
                    const float dx = m.pos[0] - view.cam.eye[0];
                    const float dy = m.pos[1] - view.cam.eye[1];
                    const float dz = m.pos[2] - view.cam.eye[2];
                    const float reach = m.radius + clipReach;
                    v = (reach * reach > dx * dx + dy * dy + dz * dz) ? 1 : 0;
                    return v != 0;
                };
                // Emit, merging adjacent surviving runs so a fully visible
                // batch still costs one draw.
                std::size_t i = 0;
                while (i < w.runs.size()) {
                    if (!visible(w.runs[i].mesh)) { ++runsCulled; ++i; continue; }
                    const auto& first = w.runs[i];
                    std::uint32_t start = first.start, count = first.count;
                    std::size_t j = i + 1;
                    while (j < w.runs.size() && w.runs[j].batch == first.batch &&
                           w.runs[j].start == start + count && visible(w.runs[j].mesh)) {
                        count += w.runs[j].count; ++j;
                    }
                    runsDrawn += j - i;
                    const auto& b = w.geo.batches[first.batch];
                    draws.push_back({keyOf(b.blend, b.cutout,
                                           static_cast<std::uint32_t>(b.material) + texBase),
                                     &w.geo, start, count, b.blend, b.cutout});
                    i = j;
                }
            }
            if (clipReport) {
                if (unlimitedClip)
                    std::printf("clip: unlimited - %zu mesh runs drawn, %zu culled\n",
                                runsDrawn, runsCulled);
                else
                    std::printf("clip: %.0f in - %zu mesh runs drawn, %zu culled\n",
                                clipInches, runsDrawn, runsCulled);
                clipReport = false;
            }
            // A FRAME WHERE THE SET IS CULLED AND THE BODIES ARE NOT draws a
            // character on black, which is what a reader sees as a flicker:
            // the visible-set walk above tests every mesh against the clip
            // distance FROM THE CAMERA EYE, and a staged body is added below
            // with no such test. One line a frame, so a run can be read for
            // the frames where the two disagree.
            std::size_t litBodies = 0;
            for (const auto& up : staged) if (up->drawn && up->mo) ++litBodies;
            if (std::getenv("OMK_CLIPLOG")) {
                std::printf("  [clip] frame %ld  runs %zu drawn %zu culled  bodies %zu"
                            "  eye %.0f %.0f %.0f\n", n, runsDrawn, runsCulled, litBodies,
                            view.cam.eye[0], view.cam.eye[1], view.cam.eye[2]);
            }
            // ...and the same facts as one line for the flicker catcher, which
            // needs to say WHY a frame went dark, not just that it did.
            if (!flickerDir.empty()) {
                char buf[320];
                std::snprintf(buf, sizeof buf,
                    "3D  cam %s  eye %.0f %.0f %.0f  at %.0f %.0f %.0f  fov %.1f"
                    "  set runs %zu drawn / %zu culled  bodies %zu  area %d scx '%s'",
                    haveDlgCam ? "dialogue" : haveEdit ? "editing" : holdEditCam ? "held"
                        : (takeCam && player) ? "take"
                        : (adventure && followCam) ? "follow" : "world",
                    view.cam.eye[0], view.cam.eye[1], view.cam.eye[2],
                    view.cam.at[0], view.cam.at[1], view.cam.at[2],
                    static_cast<double>(view.cam.hfovDeg),
                    runsDrawn, runsCulled, litBodies,
                    session.currentArea(), session.scene().file().c_str());
                frameNote = buf;
            }
            // ---- THE LIGHTS, per pixel (`todo/enhancements.md` 7) -------
            //
            // The same `.3DO` records the crowd is lit by, handed to the
            // backend instead of applied on the CPU. Nearest first and capped,
            // because a uniform block is finite and a body is reached by a
            // handful: Anekbah ships 155 and its lamps reach 700-900 units.
            // THE SHIMMER's clock, advanced the way `Game_Tick` does it:
            // `clock += 2 * frameDelta`, wrapping at 256, where one frame's
            // delta is 1.0 at 30 Hz (`docs/BOOT.md` 4). 233 set meshes ride
            // it - the far skyline of every city - and it is the GAME's, not
            // an enhancement, so both backends draw it.
            shimmerClock += 2.0f;
            if (shimmerClock >= omk::kShimmerWrap) shimmerClock -= omk::kShimmerWrap;
            view.shimmerClock = shimmerClock;
            view.dither = dither;
            const bool litPerPixel = lighting > 0;
            // TWO BASES, and the difference is a property of the models.
            //
            // `1` starts from BLACK, which is the engine's own rule for the
            // bodies it lights: `sub_494E80` writes `instance[+416]` into
            // every runtime vertex colour and every site that sets +416 sets
            // it to 0, and the crowd models ship pure white (all 446 of
            // PSH_FN's vertices are 255,255,255), so there is no baked light
            // in them to lose.
            //
            // `2` ADDS to the baked colour, and that is this port's decision
            // for the bodies the engine never lights. HO1_FN is not a white
            // model - it carries real baked shading - so black-plus-lamps
            // throws away everything the artist put in and leaves him a
            // silhouette wherever no lamp reaches. Stated here because it is
            // the one place row 7 departs from transcription.
            const int litCrowd  = litPerPixel ? 1 : 0;
            const int litStaged = litPerPixel ? 2 : 0;
            view.lights.clear();
            if (litPerPixel) {
                float at[3] = {view.cam.eye[0], view.cam.eye[1], view.cam.eye[2]};
                if (drawPlayer && !playerPosed.corners.empty()) {
                    at[0] = playerPosed.corners.front().x;
                    at[1] = playerPosed.corners.front().y;
                    at[2] = playerPosed.corners.front().z;
                }
                std::vector<std::pair<float, const omk::Light3do*>> near;
                for (const WorldSlot& ws2 : worldSlots)
                    for (const auto& l : ws2.lights) {
                        const float dx = at[0] - l.pos[0], dy = at[1] - l.pos[1],
                                    dz = at[2] - l.pos[2];
                        const float d2 = dx * dx + dy * dy + dz * dz;
                        if (d2 > l.radiusA * l.radiusA) continue;   // out of reach
                        if (!(l.radiusA > l.radiusB)) continue;
                        near.emplace_back(d2, &l);
                    }
                std::sort(near.begin(), near.end(),
                          [](const auto& a, const auto& b) { return a.first < b.first; });
                if (near.size() > static_cast<std::size_t>(omk::View::kMaxGpuLights))
                    near.resize(static_cast<std::size_t>(omk::View::kMaxGpuLights));
                for (const auto& [d2, l] : near) {
                    omk::View::GpuLight g{};
                    for (int k = 0; k < 3; ++k) { g.pos[k] = l->pos[k]; g.dir[k] = l->dir[k]; }
                    g.radiusA = l->radiusA; g.radiusB = l->radiusB;
                    g.colour[0] = static_cast<float>((l->colour >> 16) & 0xFF) / 255.0f;
                    g.colour[1] = static_cast<float>((l->colour >> 8) & 0xFF) / 255.0f;
                    g.colour[2] = static_cast<float>(l->colour & 0xFF) / 255.0f;
                    g.intensity = l->f32;
                    view.lights.push_back(g);
                }
                if (!lightsTold) {
                    lightsTold = true;
                    std::printf("frame %ld: per-pixel lighting - %zu of the set's lights "
                                "reach the player, nearest first\n", n, view.lights.size());
                }
            }
            // ---- THE MAPPED SHADOW's LIGHT (`todo/enhancements.md` 6) --
            //
            // An ENHANCEMENT: nothing in the engine casts a real shadow. What
            // keeps it from being an invented sun is where the direction comes
            // from - the strongest of the SET's own `.3DO` light records
            // reaching the casters, the same table and the same reach and
            // falloff rules `applyLights` uses to light the crowd. With no
            // light in reach nothing is drawn, which is honest: the port does
            // not know where the light is, so it does not guess.
            //
            // Only characters cast. A set is shaded by a colour baked into
            // every vertex and that colour ALREADY contains the artists'
            // shadows (ASSETS 4c), so a map that darkened the set from the set
            // would paint a second shadow over the first.
            const bool castShadows = drawShadows && shadowQuality >= 2;
            view.shadow = omk::View::ShadowLight{};
            if (castShadows) {
                float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
                bool any = false;
                int bodies = 0;
                // ...and only the bodies NEAR THE CAMERA. The slab is one
                // 1024-texel map, so its width is its resolution: bounding
                // every staged body in the city made it 7484 units across -
                // 15 units a texel, which is a shadow the size of a torso.
                // 1200 units is 30 m, past which a character's shadow is
                // sub-pixel anyway. LABELLED as this port's number.
                constexpr float kShadowRange = 1200.0f;
                const auto bound = [&](const omk::Geometry& g) {
                    if (g.corners.empty()) return;
                    const auto& c0 = g.corners.front();
                    const float dx = c0.x - view.cam.eye[0], dy = c0.y - view.cam.eye[1],
                                dz = c0.z - view.cam.eye[2];
                    if (dx * dx + dy * dy + dz * dz > kShadowRange * kShadowRange) return;
                    for (const auto& c : g.corners) {
                        const float p3[3] = {c.x, c.y, c.z};
                        for (int k = 0; k < 3; ++k) {
                            lo[k] = std::min(lo[k], p3[k]);
                            hi[k] = std::max(hi[k], p3[k]);
                        }
                    }
                    ++bodies;
                    any = true;
                };
                if (drawPlayer && !playerPosed.corners.empty()) bound(playerPosed);
                for (const auto& up : staged) if (up->drawn && up->mo) bound(up->posed);
                for (const auto& up : pedStaged) if (up->drawn && up->mo) bound(up->posed);
                if (any) {
                    // ...CENTRED ON THE PLAYER, not on the bodies' midpoint.
                    // A street's lamps reach about 700 units and eleven
                    // scattered bodies put their midpoint out in the road
                    // beyond all of them, so the light picked there was the
                    // one distant fill that reaches everywhere - and its
                    // shadow lands off the frame. One map, centred on the
                    // character you are looking at; a body outside the slab
                    // casts nothing, which is stated rather than hidden.
                    float centre[3];
                    if (drawPlayer && !playerPosed.corners.empty()) {
                        float plo[3] = {1e30f, 1e30f, 1e30f}, phi[3] = {-1e30f, -1e30f, -1e30f};
                        for (const auto& c : playerPosed.corners) {
                            const float q[3] = {c.x, c.y, c.z};
                            for (int k = 0; k < 3; ++k) {
                                plo[k] = std::min(plo[k], q[k]);
                                phi[k] = std::max(phi[k], q[k]);
                            }
                        }
                        for (int k = 0; k < 3; ++k) centre[k] = (plo[k] + phi[k]) * 0.5f;
                    } else {
                        for (int k = 0; k < 3; ++k) centre[k] = (lo[k] + hi[k]) * 0.5f;
                    }
                    // Enough for a body and the throw of its shadow under a
                    // steep light; this port's number, and the resolution is
                    // its width (1024 texels over the slab).
                    float rad = 220.0f;
                    const omk::Light3do* best = nullptr;
                    float bestK = 0.0f;
                    for (const WorldSlot& ws2 : worldSlots)
                        if (!ws2.lights.empty()) {
                            float k = 0.0f;
                            const omk::Light3do* l =
                                omk::strongestLightAt(centre, ws2.lights, &k);
                            if (l && k > bestK) { bestK = k; best = l; }
                        }
                    if (best) {
                        view.shadow.on = true;
                        for (int k = 0; k < 3; ++k) {
                            view.shadow.dir[k] = best->dir[k];
                            view.shadow.centre[k] = centre[k];
                        }
                        view.shadow.radius = rad;
                        view.shadow.strength = 0.6f;
                        static const bool shLog = std::getenv("OMK_SHADOWLOG") != nullptr;
                        if (!shadowLightTold || (shLog && (n % 30) == 0)) {
                            shadowLightTold = true;
                            std::printf("frame %ld: mapped shadows - light '%s' at "
                                        "%.0f %.0f %.0f, direction %.2f %.2f %.2f, "
                                        "slab radius %.0f over %d bodies, asked at "
                                        "%.0f %.0f %.0f\n", n, best->name.c_str(),
                                        static_cast<double>(best->pos[0]),
                                        static_cast<double>(best->pos[1]),
                                        static_cast<double>(best->pos[2]),
                                        static_cast<double>(best->dir[0]),
                                        static_cast<double>(best->dir[1]),
                                        static_cast<double>(best->dir[2]),
                                        static_cast<double>(rad), bodies,
                                        static_cast<double>(centre[0]),
                                        static_cast<double>(centre[1]),
                                        static_cast<double>(centre[2]));
                        }
                    } else if (!shadowLightTold) {
                        shadowLightTold = true;
                        std::printf("frame %ld: mapped shadows asked for, but no set light "
                                    "reaches the cast - nothing drawn (the port does not "
                                    "invent a sun)\n", n);
                    }
                }
            }
            // ---- THE SHADOWS ------------------------------------------
            //
            // `Actors_TickAll` calls `Actor_DrawShadow(detail, actor)` for
            // every live actor and `Sliders_Tick` places the crowd's, both
            // behind option row 5. Built here, after every body has been
            // placed and before the draw list is sorted, because the blobs
            // are FRAME GEOMETRY - the engine writes them straight into the
            // scene's vertex and triangle pools each frame, not into a node.
            //
            // The probe is the walkable soup, which is what the bodies are
            // seated on; the engine's `World_ProbePoint(-1, ...)` runs on the
            // same collision geometry.
            shadowGeo.corners.clear();
            shadowGeo.batches.clear();
            shadowGeo.cornerMirror.clear();
            shadowGeo.cornerMesh.clear();
            shadowGeo.cornerVertex.clear();
            shadowGeo.cornerDeclared.clear();
            // ...and only up to `fitted`: a MAPPED frame replaces the blobs
            // with a real shadow rather than drawing both.
            if (drawShadows && shadowQuality < 2 && shadowModel.loaded && !playerSoup.empty()) {
                // Option row 7. The player and the fight opponent get it
                // whole; every other actor gets it MINUS ONE, so an npc always
                // casts one tier coarser.
                const int detail = shadowDetail;
                // FITTED gathers the triangles under a body ONCE and probes
                // its grids against those - see `o3de/shadow.h`. Gathered per
                // body inside `castBones`.
                const bool fitted = shadowQuality >= 1;
                shadowSpreadMax = 0.0f; shadowSpreadPlayer = 0.0f;
                long blobs = 0, nPlayer = 0, nActor = 0, nCrowd = 0;
                long nPedDrawn = 0, nPedFeet = 0;
                // ...and the same detector over the BONE path: a bone taken
                // off the wrong LOD skeleton is ~240 units from the body it
                // belongs to, so measure every blob against where the body was
                // actually drawn (its corners' horizontal centre) and report
                // the worst. `ref` is null for the player, whose model has one
                // skeleton and cannot meet this.
                const auto castBones = [&](const std::vector<omk::Mesh>& meshes,
                                           const std::vector<float>& at, int lvl,
                                           int root, const omk::Geometry* ref) {
                    if (at.empty()) return;
                    float rx = 0.0f, rz = 0.0f;
                    if (ref && !ref->corners.empty()) {
                        float lo[2] = {1e30f, 1e30f}, hi[2] = {-1e30f, -1e30f};
                        for (const auto& c : ref->corners) {
                            lo[0] = std::min(lo[0], c.x); hi[0] = std::max(hi[0], c.x);
                            lo[1] = std::min(lo[1], c.z); hi[1] = std::max(hi[1], c.z);
                        }
                        rx = (lo[0] + hi[0]) * 0.5f; rz = (lo[1] + hi[1]) * 0.5f;
                    }
                    const auto bones = omk::shadowBonesFor(lvl);
                    // THE BODY'S OWN PATCH OF GROUND, gathered once. Fitted
                    // probes 25 vertices a blob; rescanning the whole set for
                    // each would be thousands of city-wide scans a frame.
                    omk::TriangleSoup local;
                    if (fitted) {
                        float lo[2] = {1e30f, 1e30f}, hi[2] = {-1e30f, -1e30f};
                        bool any = false;
                        for (int bi : bones) {
                            const int mi = omk::findMeshContaining(
                                meshes, omk::kShadowBones[static_cast<std::size_t>(bi)].bone, root);
                            if (mi < 0 || static_cast<std::size_t>(mi) * 3 + 2 >= at.size()) continue;
                            const float* q = &at[static_cast<std::size_t>(mi) * 3];
                            lo[0] = std::min(lo[0], q[0]); hi[0] = std::max(hi[0], q[0]);
                            lo[1] = std::min(lo[1], q[2]); hi[1] = std::max(hi[1], q[2]);
                            any = true;
                        }
                        if (!any) return;
                        // inflated by more than the widest blob's half-width
                        // (275.59 x 0.07 x 1.5 = 28.9)
                        local = omk::soupInBox(playerSoup, lo[0] - 40.0, hi[0] + 40.0,
                                               lo[1] - 40.0, hi[1] + 40.0);
                    }
                    const omk::TriangleSoup& soup = fitted ? local : playerSoup;
                    for (int bi : bones) {
                        const auto& sb = omk::kShadowBones[static_cast<std::size_t>(bi)];
                        const int mi = omk::findMeshContaining(meshes, sb.bone, root);
                        if (mi < 0 || static_cast<std::size_t>(mi) * 3 + 2 >= at.size()) continue;
                        const float* p3 = &at[static_cast<std::size_t>(mi) * 3];
                        if (ref && !ref->corners.empty()) {
                            const float dx = p3[0] - rx, dz = p3[2] - rz;
                            const float d = std::sqrt(dx * dx + dz * dz);
                            if (d > shadowFootOffMax) shadowFootOffMax = d;
                        }
                        const auto f = omk::floorUnder(soup, p3[0], p3[1], p3[2]);
                        if (!f) continue;
                        const std::size_t was = shadowGeo.corners.size();
                        const float rad = meshes[static_cast<std::size_t>(mi)].radius;
                        const bool drew = fitted
                            ? omk::shadowBlobFitted(shadowGeo, shadowModel, p3, rad,
                                                    sb.divisor, sb.reach,
                                                    static_cast<float>(*f), soup)
                            : omk::shadowBlob(shadowGeo, shadowModel, p3, rad,
                                              sb.divisor, sb.reach, static_cast<float>(*f));
                        if (drew) {
                            ++blobs;
                            // THE PROPERTY `fitted` EXISTS FOR: how far a
                            // single blob's own vertices spread vertically.
                            // A classic blob is a flat quad, so this is 0 for
                            // every one of them at every camera and on every
                            // surface; a fitted blob on a slope or a stair
                            // follows the ground and it is not. That is the
                            // measurement, and a pixel count is not - the fan
                            // and the grid interpolate the disc's UVs
                            // differently, so they differ a little even on
                            // perfectly flat ground.
                            float lo = 1e30f, hi = -1e30f;
                            for (std::size_t ci = was; ci < shadowGeo.corners.size(); ++ci) {
                                lo = std::min(lo, shadowGeo.corners[ci].y);
                                hi = std::max(hi, shadowGeo.corners[ci].y);
                            }
                            if (hi - lo > shadowSpreadMax) shadowSpreadMax = hi - lo;
                        }
                    }
                };
                // `Actors_TickAll` skips the shadow in ACTOR_STATE 7 and
                // 11..14 - the slider mount, the ladder, the two scripted
                // states and the water. Read off the same `if` the mark's
                // enable/disable sits under.
                const auto castsIn = [](omk::ActorState st) {
                    const int v = static_cast<int>(st);
                    return !(v == 7 || (v > 10 && v <= 14));
                };
                // The player's model has ONE skeleton, so -1 is the whole of it.
                shadowFootOffMax = pedFootOffMax;
                if (drawPlayer && playerMeshAtKnown && player && castsIn(player->state()))
                    castBones(playerMeshes, playerMeshAt, detail, -1, nullptr);
                nPlayer = blobs;
                shadowSpreadPlayer = shadowSpreadMax;   // his alone, before the rest
                for (const auto& up : staged)
                    if (up->drawn && up->mo)
                        castBones(up->mo->meshes, up->meshAt, detail - 1, up->shadowRoot,
                                  &up->posed);
                nActor = blobs - nPlayer;
                // The crowd's, which is the other mechanism entirely.
                for (const auto& up : pedStaged) {
                    if (up->drawn) ++nPedDrawn;
                    if (up->drawn && up->footKnown) ++nPedFeet;
                    if (!up->drawn || !up->footKnown) continue;
                    const float mid[3] = {(up->footAt[0][0] + up->footAt[1][0]) * 0.5f,
                                          (up->footAt[0][1] + up->footAt[1][1]) * 0.5f,
                                          (up->footAt[0][2] + up->footAt[1][2]) * 0.5f};
                    const auto h = omk::surfaceUnder(playerSoup, mid[0], mid[1], mid[2]);
                    if (!h) continue;
                    const float nrm[3] = {static_cast<float>(h->n[0]),
                                          static_cast<float>(h->n[1]),
                                          static_cast<float>(h->n[2])};
                    const std::size_t before = shadowGeo.corners.size();
                    if (omk::shadowFootBlob(shadowGeo, shadowModel, up->footAt[0],
                                            up->footAt[1], static_cast<float>(h->y), nrm)) {
                        ++blobs; ++nCrowd;
                        static const bool pedLog = std::getenv("OMK_SHADOWLOG") != nullptr;
                        if (pedLog && nCrowd == 1 && (n % 60) == 0)
                            std::printf("  [shadow] crowd blob: feet %.0f %.0f %.0f / "
                                        "%.0f %.0f %.0f  floor %.0f  corner0 %.0f %.0f %.0f "
                                        "extent %.1f\n",
                                        static_cast<double>(up->footAt[0][0]),
                                        static_cast<double>(up->footAt[0][1]),
                                        static_cast<double>(up->footAt[0][2]),
                                        static_cast<double>(up->footAt[1][0]),
                                        static_cast<double>(up->footAt[1][1]),
                                        static_cast<double>(up->footAt[1][2]), h->y,
                                        static_cast<double>(shadowGeo.corners[before].x),
                                        static_cast<double>(shadowGeo.corners[before].y),
                                        static_cast<double>(shadowGeo.corners[before].z),
                                        static_cast<double>(
                                            shadowGeo.corners[before].x - up->footAt[0][0]));
                    }
                }
                if (!shadowGeo.corners.empty()) {
                    // ONE batch: every blob goes through the same material and
                    // the same mesh flags 0x5000, so the whole frame's shadow
                    // is one submission.
                    shadowGeo.batches.push_back({0, false, omk::Blend::Mul, 0,
                                                 shadowGeo.corners.size()});
                    shadowGeo.revision = ++worldGeoRev;
                    draws.push_back({keyOf(omk::Blend::Mul, false,
                                           static_cast<std::uint32_t>(shadowTexBase)),
                                     &shadowGeo, 0, shadowGeo.corners.size(),
                                     omk::Blend::Mul, false});
                }
                shadowBlobsDrawn = blobs;
                if (!shadowTold && blobs > 0) {
                    shadowTold = true;
                    std::printf("frame %ld: shadows ON (row 7 detail %d, quality %s) - "
                                "%ld blobs, %zu corners, texture slot %zu\n",
                                n, detail, omk::shadowQualityName(shadowQuality),
                                blobs, shadowGeo.corners.size(), shadowTexBase);
                }
                static const bool shadowLog = std::getenv("OMK_SHADOWLOG") != nullptr;
                if (shadowLog && (n % 30) == 0)
                    std::printf("  [shadow] frame %ld: %ld player + %ld actor + %ld crowd"
                                " = %ld blobs (%zu peds staged, %ld drawn, %ld with feet)\n",
                                n, nPlayer, nActor, nCrowd, blobs, pedStaged.size(),
                                nPedDrawn, nPedFeet);
                if (shadowLog && (n % 30) == 0)
                    std::printf("  [shadow] worst blob vertical spread %.2f units, "
                                "the player's %.2f (a flat quad is 0 by construction)\n",
                                static_cast<double>(shadowSpreadMax),
                                static_cast<double>(shadowSpreadPlayer));
                if (shadowLog && (n % 30) == 0)
                    std::printf("  [shadow] worst foot-to-body offset %.1f units "
                                "(a bone off the wrong LOD skeleton is ~240)\n",
                                static_cast<double>(shadowFootOffMax));
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
                                     &up->posed, b.start, b.count, b.blend, b.cutout,
                                     litStaged, castShadows});
            }
            for (const auto& up : pedStaged) {
                if (!up->drawn || !up->mo) continue;
                const int base = static_cast<int>(up->mo->texBase);
                for (const auto& b : up->posed.batches)
                    draws.push_back({keyOf(b.blend, b.cutout,
                                           static_cast<std::uint32_t>(b.material + base)),
                                     &up->posed, b.start, b.count, b.blend, b.cutout,
                                     litCrowd, castShadows});
            }
            for (const auto& up : vehStaged) {
                if (!up->drawn || !up->mo) continue;
                const int base = static_cast<int>(up->mo->texBase);
                for (const auto& b : up->posed.batches)
                    draws.push_back({keyOf(b.blend, b.cutout,
                                           static_cast<std::uint32_t>(b.material + base)),
                                     &up->posed, b.start, b.count, b.blend, b.cutout,
                                     litCrowd, castShadows});
            }
            if (drawPlayer)
                for (const auto& b : playerPosed.batches)
                    draws.push_back({keyOf(b.blend, b.cutout, static_cast<std::uint32_t>(
                                               b.material + static_cast<int>(playerTexBase))),
                                     &playerPosed, b.start, b.count, b.blend, b.cutout,
                                     litStaged, castShadows});
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
            // ---- AND THE MIRROR, which until now only `--scene` ever got.
            //
            // `drawWithMirror` has been on the renderer boundary since
            // 2026-09-01 and the free-fly viewer was its only caller, so a
            // mirror reflected there and was a flat blended pane in adventure
            // mode and in every cutscene - which is where a player meets one.
            // A reader: *the mirror in the chamber is displayed with
            // transparency instead of reflecting.* The plane was already
            // being read per set (`w.mirror`) and used for nothing but a word
            // in a log line.
            //
            // The engine keeps a SINGLE global (`dword_534F48`), so at most
            // one mirror is live at a time; the shown slot's is the one.
            static const bool noMirror = std::getenv("OMK_NO_MIRROR") != nullptr;
            const omk::MirrorPlane& wmp =
                worldSlots[static_cast<std::size_t>(session.shownSlot() & 1)].mirror;
            {
                // `OMK_CAMLOG=1`: the frame's camera as DRAWN, every frame,
                // after every writer above. A creep of a hundredth of a unit
                // is invisible in any still and is what makes a point-sampled
                // texture twinkle (todo/omk-play 88).
                static const bool camLog = [] { const char* e = std::getenv("OMK_CAMLOG"); return e && *e == '1'; }();
                if (camLog)
                    std::fprintf(stderr, "[cam] frame %ld eye %.4f %.4f %.4f at %.4f %.4f %.4f fov %.3f\n",
                                 n, view.cam.eye[0], view.cam.eye[1], view.cam.eye[2],
                                 view.cam.at[0], view.cam.at[1], view.cam.at[2], view.cam.hfovDeg);
            }
            const auto mst = omk::drawWithMirror(world, draws, view,
                                                 noMirror ? omk::MirrorPlane{} : wmp);
            if (mst.active != mirrorLive || (mst.maskPixels > 0 && !mirrorSeen)) {
                mirrorLive = mst.active;
                if (mst.maskPixels > 0) mirrorSeen = true;
                std::printf("frame %ld: the set's mirror is %s%s (%ld px, camera "
                            "%.0f in front)\n", n,
                            mst.active ? "REFLECTING" : "out of view - the camera is behind it",
                            mst.native ? " [gpu stencil]" : "",
                            mst.maskPixels, static_cast<double>(mst.distance));
            }
            // The backend drew the picture into the top-left `vw x vh`; place
            // it, leaving the bands as the black `fb` was cleared to.
            const omk::Surface& pic = world.readback();
            if (vpItem && pic.w == fb.w) {
                // The picture is the viewport item's; the composer places it
                // at the item's layer, not the frame.
                view3dPic = omk::Surface(view.vw, view.vh, 0);
                for (int y = 0; y < view.vh && y < pic.h; ++y)
                    std::copy(pic.px.begin() + static_cast<long>(y) * pic.w,
                              pic.px.begin() + static_cast<long>(y) * pic.w + view.vw,
                              view3dPic.px.begin() + static_cast<long>(y) * view.vw);
                comp.attachView3D(&view3dPic);
            } else if (pic.w == fb.w) {
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
                // ...and the TAG here too: `sub_49BFF0` latches
                // `dword_4DE74C = selected_widget[+0x3C]` when Examiner is
                // confirmed, and the page draws whatever that names.
                const int row = walk->selectedRow(omk::kListSneakRows);
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
            if (!std::getenv("OMK_NOUI")) comp.draw(fb, openScreen, *walk);
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

        // ...and what the finished frame actually CONTAINS, for hunting a
        // flicker: a body drawn on a set that is not there shows up as a lit
        // count far below its neighbours', which is what a reader reports as
        // "Kay'l with a black background" and what no still frame can be
        // chosen to catch.
        if (std::getenv("OMK_CLIPLOG")) {
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
        present(fb);
        if (!snapsDir.empty() && handoverFrame >= 0 && ((n - handoverFrame) % snapEvery) == 0) {
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
        // SAY THE SIZE. This wrote the bytes and no dimensions, and a reader
        // of the file has nothing to go on but its length - 960000 bytes is
        // 800x600, not the 640x480 that a `.bin` from this viewer is assumed
        // to be, and one shot was decoded at the wrong stride and read as a
        // broken renderer.
        std::printf("wrote %s (%dx%d RGB565, %zu bytes)\n", dump.c_str(),
                    fb.w, fb.h, fb.px.size() * 2);
    }
    // ------------------------------------------------- WRITING A SAVE
    //
    // `Game_WriteSave` (0x00408EF0), with `State_Save`'s snapshot half in
    // front of it (GAME_STATE 5a, 8b).  The order is the engine's: take the
    // snapshot the block does not otherwise keep, then the four copies into
    // the slot, then the whole file back.
    //
    // WHERE the scene comes from matters.  `State_Save` reads the LIVE
    // resident slot (`dword_69BC4C[4 * dword_69BC60]`), not the scene-per-area
    // table - and `State_Apply` copies the header back INTO that table before
    // `Area_Load` reads it, so the header is what a load believes.  Taking it
    // from the table here would make the port unable to express a divergence
    // the engine can.
    if (saveSlotArg >= 0) {
        state.setCurrentArea(static_cast<std::int16_t>(session.activeArea()));
        state.setCurrentScene(static_cast<std::int16_t>(
            session.residentSlot(session.activeSlot()).scene));
        state.setPlacement(session.playerPos(), session.playerYaw());

        omk::SaveSlot out;
        out.name = !saveNameArg.empty() ? saveNameArg
                 : (!loadedName.empty() ? loadedName : std::string("omk-play"));
        out.day  = state.clockDay();
        out.time = state.clock();
        out.state = state;

        // The picture the load panel draws beside the selected row: the back
        // buffer scaled into a 128 x 96 rect and repacked to X1R5G5B5
        // (GAME_STATE 8b).  `fb` is what the window was shown, so this is the
        // frame the player was looking at - which is what the engine's blit
        // takes too.
        const auto thumb = omk::thumbFromRgb565(fb.px, fb.w, fb.h);

        auto file = omk::readSaveFile(savesPath, fr + "/IAM/GAMES");
        if (file.size() < omk::kSaveFileSize) {
            // `sub_4092A0`'s create arm - the settings, then 256 empty slots.
            file = omk::blankSaveFile(saveSettings ? *saveSettings
                                                   : omk::defaultSettingsBlock());
            std::printf("save: %s did not exist - created, %zu bytes\n",
                        savesPath.c_str(), file.size());
        }
        // ...and `Game_WriteSave`'s first copy: the settings over the head, on
        // EVERY slot save.  Saving a game saves the options (GAME_STATE 8a).
        if (saveSettings) omk::putSettings(file, *saveSettings);
        if (!omk::writeSaveSlot(file, saveSlotArg, out, thumb))
            std::fprintf(stderr, "save: slot %d is out of range\n", saveSlotArg);
        else if (omk::writeSaveFile(savesPath, file))
            std::printf("save: slot %d written to %s - '%s', %s %s, area %d "
                        "scene %d, standing at %.0f %.0f %.0f facing %.0f, "
                        "with a %dx%d picture\n",
                        saveSlotArg, savesPath.c_str(), out.name.c_str(),
                        omk::formatDate(out.day).c_str(),
                        omk::formatTime(out.time).c_str(),
                        state.currentArea(), state.currentScene(),
                        session.playerPos()[0], session.playerPos()[1],
                        session.playerPos()[2], session.playerYaw(),
                        omk::kThumbW, omk::kThumbH);
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
    if (player)
        std::printf("player: ends at %.1f %.1f %.1f facing %.0f, channel entry %d group %d\n",
                    player->pos()[0], player->pos()[1], player->pos()[2],
                    player->facing(), player->ctlState(), player->ctlGroup());
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
