// SPDX-License-Identifier: GPL-3.0-or-later
// The Carbon frontend - see carbonfront.h.
#include "carbonfront.h"

#include <AppleEvents.h>
#include <Events.h>
#include <MacWindows.h>
#include <QDOffscreen.h>
#include <Quickdraw.h>
#include <TextUtils.h>
#include <Timer.h>
#include <ToolUtils.h>

#include <cstring>
#include <memory>

namespace omk {
namespace {

// Mac virtual key code -> the engine's DIK code: the SAME keys
// `backends/sdl/sdlfront.cpp`'s `keymap()` maps, and for the same reasons
// (the interface's vocabulary, the viewer's keys, the adventure bindings);
// see the comments there. Option is the Mac's Alt (DIK_LMENU, which skips
// all three films). A Mac keyboard's right shift and right control report
// their own codes only where the keyboard distinguishes them.
struct KeyRow { unsigned char mac, dik; };
constexpr KeyRow kKeys[] = {
    {0x7E, 0xC8}, {0x7D, 0xD0}, {0x7B, 0xCB}, {0x7C, 0xCD},   // arrows
    {0x24, 0x1C}, {0x31, 0x39}, {0x30, 0x0F}, {0x35, 0x01},   // return space tab escape
    {0x3A, 0x38},                                             // option = alt
    {0x0D, 0x11}, {0x00, 0x1E}, {0x01, 0x1F}, {0x02, 0x20},   // W A S D
    {0x0C, 0x10}, {0x0E, 0x12}, {0x25, 0x26}, {0x23, 0x19},   // Q E L P
    {0x21, 0x1A}, {0x1E, 0x1B},                               // [ ]
    {0x38, 0x2A}, {0x09, 0x2F}, {0x2E, 0x32},                 // left shift, V, M
    {0x3C, 0x36}, {0x3E, 0x9D}, {0x3B, 0x1D},                 // right shift, right/left control
};

bool keyHeld(const KeyMap& km, unsigned code) {
    return (reinterpret_cast<const unsigned char*>(km)[code >> 3] >> (code & 7)) & 1;
}

std::uint64_t microseconds() {
    UnsignedWide us;
    Microseconds(&us);
    return (static_cast<std::uint64_t>(us.hi) << 32) | us.lo;
}

// Quit from the application menu (Mac OS X) or the Finder: the 'quit' Apple
// event, which is how a Carbon application is asked to stop.
bool g_quitRequested = false;
pascal OSErr onQuit(const AppleEvent*, AppleEvent*, long) {
    g_quitRequested = true;
    return noErr;
}

}  // namespace

CarbonFrontend::~CarbonFrontend() { close(); }

bool CarbonFrontend::open(int w, int h, const std::string& title) {
    InitCursor();
    static AEEventHandlerUPP quitUPP = NewAEEventHandlerUPP(onQuit);
    AEInstallEventHandler(kCoreEventClass, kAEQuitApplication, quitUPP, 0, false);
    ::Rect r;
    SetRect(&r, 40, 60, 40 + w, 60 + h);
    Str255 t;
    c2pstrcpy(t, title.c_str());
    WindowRef win = NewCWindow(nullptr, &r, t, true, noGrowDocProc,
                               reinterpret_cast<WindowRef>(-1), true, 0);
    if (!win) return false;
    win_ = win;
    SetPortWindowPort(win);
    return true;
}

bool CarbonFrontend::pump(HostInput& out) {
    out.text.clear();
    EventRecord ev;
    // everything waiting, without sleeping: the frame loop is the clock
    while (WaitNextEvent(everyEvent, &ev, 0, nullptr)) {
        switch (ev.what) {
        case keyDown: case autoKey: {
            const char c = static_cast<char>(ev.message & charCodeMask);
            if ((ev.modifiers & cmdKey) && (c == 'q' || c == 'Q')) { quit_ = true; break; }
            // the characters `WM_CHAR` carries: printable text, and the four
            // control characters the name field reads (sdlfront.cpp, charmap)
            if (c >= 32 && c < 127) out.text += c;
            else if (c == 8 || c == 9 || c == 13 || c == 27) out.text += c;
            else if (c == 3) out.text += '\r';     // the keypad's enter
            break;
        }
        case mouseDown: {
            WindowRef w = nullptr;
            const short part = FindWindow(ev.where, &w);
            if (w == win_ && part == inDrag) {
                ::Rect bounds;
                GetRegionBounds(GetGrayRgn(), &bounds);
                DragWindow(w, ev.where, &bounds);
            } else if (w == win_ && part == inGoAway) {
                if (TrackGoAway(w, ev.where)) quit_ = true;
            }
            break;
        }
        case updateEvt:
            if (reinterpret_cast<WindowRef>(ev.message) == win_) {
                BeginUpdate(static_cast<WindowRef>(win_));
                blit();
                EndUpdate(static_cast<WindowRef>(win_));
            }
            break;
        case kHighLevelEvent:
            AEProcessAppleEvent(&ev);
            break;
        default:
            break;
        }
    }
    if (g_quitRequested) quit_ = true;
    out.quit = quit_;

    // the keys HELD, from the keyboard's own state - not from key-up events,
    // which classic Mac OS does not deliver by default
    out.held.clear();
    KeyMap km;
    GetKeys(km);
    for (const KeyRow& k : kKeys)
        if (keyHeld(km, k.mac)) out.held.insert(k.dik);
    out.mouse.clear();
    if (Button()) out.mouse.insert(12);
    out.mouseDX = out.mouseDY = 0.0f;
    return !out.quit;
}

void CarbonFrontend::present(const Surface& fb) {
    if (!win_ || fb.w <= 0 || fb.h <= 0) return;
    if (!gw_ || gwW_ != fb.w || gwH_ != fb.h) {
        if (gw_) DisposeGWorld(static_cast<GWorldPtr>(gw_));
        gw_ = nullptr;
        ::Rect b;
        SetRect(&b, 0, 0, static_cast<short>(fb.w), static_cast<short>(fb.h));
        GWorldPtr gw = nullptr;
        if (NewGWorld(&gw, 16, &b, nullptr, nullptr, 0) != noErr || !gw) return;
        gw_ = gw;
        gwW_ = fb.w;
        gwH_ = fb.h;
    }
    PixMapHandle pm = GetGWorldPixMap(static_cast<GWorldPtr>(gw_));
    if (!LockPixels(pm)) return;
    char* base = GetPixBaseAddr(pm);
    const long rowBytes = GetPixRowBytes(pm);
    // RGB565 -> xRGB1555, native (big-endian) 16-bit words: red and green
    // shift down one, green's low bit is dropped, blue stays
    for (int y = 0; y < fb.h; ++y) {
        auto* row = reinterpret_cast<std::uint16_t*>(base + y * rowBytes);
        const std::uint16_t* src = fb.px.data() + static_cast<std::size_t>(y) * fb.w;
        for (int x = 0; x < fb.w; ++x)
            row[x] = static_cast<std::uint16_t>(((src[x] & 0xFFC0) >> 1) | (src[x] & 0x1F));
    }
    UnlockPixels(pm);
    blit();
}

void CarbonFrontend::blit() {
    if (!win_ || !gw_) return;
    CGrafPtr port = GetWindowPort(static_cast<WindowRef>(win_));
    SetPort(port);
    ForeColor(blackColor);   // CopyBits colourises with the port's colours
    BackColor(whiteColor);
    PixMapHandle pm = GetGWorldPixMap(static_cast<GWorldPtr>(gw_));
    if (!LockPixels(pm)) return;
    ::Rect src, dst;
    SetRect(&src, 0, 0, static_cast<short>(gwW_), static_cast<short>(gwH_));
    GetPortBounds(port, &dst);
    CopyBits(GetPortBitMapForCopyBits(static_cast<GWorldPtr>(gw_)),
             GetPortBitMapForCopyBits(port), &src, &dst, srcCopy, nullptr);
    UnlockPixels(pm);
    QDFlushPortBuffer(port, nullptr);   // Mac OS X buffers the window; OS 9 draws through
}

void CarbonFrontend::close() {
    if (gw_) { DisposeGWorld(static_cast<GWorldPtr>(gw_)); gw_ = nullptr; }
    if (win_) { DisposeWindow(static_cast<WindowRef>(win_)); win_ = nullptr; }
}

std::uint32_t CarbonFrontend::ticksMs() {
    return static_cast<std::uint32_t>(microseconds() / 1000);
}
std::uint64_t CarbonFrontend::perfCounter() { return microseconds(); }

// No sleep call that is both in CarbonLib and fine-grained: whole ticks
// (1/60 s) with `Delay`, the rest by watching the microsecond clock.
void CarbonFrontend::delayMs(std::uint32_t ms) {
    const std::uint64_t end = microseconds() + static_cast<std::uint64_t>(ms) * 1000;
    for (;;) {
        const std::uint64_t now = microseconds();
        if (now >= end) break;
        if (end - now > 20000) {
            unsigned long fin = 0;
            Delay(1, &fin);
        }
    }
}

std::string CarbonFrontend::windowTitle() const {
    if (!win_) return {};
    Str255 t;
    GetWTitle(static_cast<WindowRef>(win_), t);
    return std::string(reinterpret_cast<const char*>(t + 1), t[0]);
}

void CarbonFrontend::setWindowTitle(const std::string& title) {
    if (!win_) return;
    Str255 t;
    c2pstrcpy(t, title.c_str());
    SetWTitle(static_cast<WindowRef>(win_), t);
}

std::unique_ptr<Frontend> makeHostFrontend() { return std::make_unique<CarbonFrontend>(); }

}  // namespace omk
