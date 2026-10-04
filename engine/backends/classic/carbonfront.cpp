// SPDX-License-Identifier: GPL-3.0-or-later
// The Carbon frontend - see carbonfront.h.
#include "carbonfront.h"
#include "platform/profile.h"     // OMK_PROFILE, before it is tested
#if OMK_PROFILE
#  include "heapcount.h"
#endif

#if defined(OMK_GL1_AGL)
#  include "../gl1/gl1host.h"
#  include <agl.h>
#  include <gl.h>
#endif
#include <AppleEvents.h>
#include <Events.h>
#include <MacWindows.h>
#include <QDOffscreen.h>
#include <Quickdraw.h>
#include <Sound.h>
#include <TextUtils.h>
#include <Timer.h>
#include <ToolUtils.h>

#include <cstdio>
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

// A buffer has PLAYED: the `callBackCmd` queued after it names its index. On
// Mac OS 9 this runs at interrupt time, so it sets one flag and nothing more;
// the main thread refills (`refillAudio`).
pascal void soundPlayed(SndChannelPtr chan, SndCommand* cmd) {
    reinterpret_cast<volatile bool*>(chan->userInfo)[cmd->param2] = true;
}

}  // namespace

namespace {
CarbonFrontend* g_carbon = nullptr;     // the one frontend, for gl1AglContext
}

CarbonFrontend::~CarbonFrontend() {
    close();
    if (g_carbon == this) g_carbon = nullptr;
}

bool CarbonFrontend::openAudio(int rate, int channels) {
    if (chan_) return true;
    static SndCallBackUPP played = NewSndCallBackUPP(soundPlayed);
    SndChannelPtr ch = nullptr;
    const OSErr e = SndNewChannel(&ch, sampledSynth, channels == 2 ? initStereo : initMono, played);
    if (e != noErr || !ch) {
        lastError_ = "SndNewChannel " + std::to_string(e);
        return false;
    }
    ch->userInfo = reinterpret_cast<long>(played_);
    chan_ = ch;
    refills_ = 0;                                  // counted per device opened
    arate_ = rate;
    achan_ = channels;
    const std::size_t frames = static_cast<std::size_t>(rate / 10);
    for (int i = 0; i < kBuffers; ++i) {
        buf_[i].assign(sizeof(ExtSoundHeader) + frames * static_cast<std::size_t>(channels) * 2, 0);
        played_[i] = true;
    }
    refillAudio();
    return true;
}

// The films open the device at their 44100 and the world reopens it at 22050
// (`Frontend::reopenAudio`): closed first, everything queued dropped.
bool CarbonFrontend::reopenAudio(int rate, int channels) {
    if (chan_ && arate_ == rate && achan_ == channels) return true;
    closeAudio();
    mix_.clear();
    return openAudio(rate, channels);
}

void CarbonFrontend::closeAudio() {
    if (!chan_) return;
    // what proves the device played: a buffer is refilled only after the
    // Sound Manager has called back to say it played the last one
    const long after = refills_ > kBuffers ? refills_ - kBuffers : 0;
    std::printf("audio: Sound Manager at %d Hz, %d channel(s) - %ld buffers queued, %ld of "
                "them after the device said one had played (%.1f s)\n",
                arate_, achan_, refills_, after, after * 0.1);
    SndDisposeChannel(static_cast<SndChannelPtr>(chan_), true);   // quietly, now
    chan_ = nullptr;
}

void CarbonFrontend::refillAudio() {
    if (!chan_) return;
    auto* ch = static_cast<SndChannelPtr>(chan_);
    const std::size_t frames = static_cast<std::size_t>(arate_ / 10);
    mixed_.resize(frames * static_cast<std::size_t>(achan_));
    for (int i = 0; i < kBuffers; ++i) {
        if (!played_[i]) continue;
        mix_.mix(mixed_.data(), mixed_.size());
        auto* h = reinterpret_cast<ExtSoundHeader*>(buf_[i].data());
        std::memset(h, 0, sizeof *h);
        h->samplePtr = nullptr;                  // the samples follow the header
        h->numChannels = static_cast<unsigned long>(achan_);
        h->sampleRate = static_cast<UnsignedFixed>(static_cast<unsigned long>(arate_) << 16);
        h->encode = extSH;
        h->baseFrequency = kMiddleC;
        h->numFrames = static_cast<unsigned long>(frames);
        h->sampleSize = 16;                      // signed, big-endian: the host's own
        auto* dst = reinterpret_cast<std::int16_t*>(h->sampleArea);
        for (std::size_t k = 0; k < mixed_.size(); ++k)
            dst[k] = static_cast<std::int16_t>(mixed_[k] * 32767.0f);   // already in [-1, 1]
        played_[i] = false;
        ++refills_;
        SndCommand c;
        c.cmd = bufferCmd; c.param1 = 0; c.param2 = reinterpret_cast<long>(h);
        SndDoCommand(ch, &c, true);
        c.cmd = callBackCmd; c.param1 = 0; c.param2 = i;
        SndDoCommand(ch, &c, true);
    }
}

void CarbonFrontend::queueAudio(std::span<const float> s) {
    if (s.empty()) return;
    if (!chan_) {
        if (!droppedToldOnce_) {
            droppedToldOnce_ = true;
            std::printf("audio: no device - the stream is dropped, not queued\n");
        }
        return;
    }
    mix_.queue(s);
    refillAudio();
}

int CarbonFrontend::playSound(std::span<const float> s, bool loop, float gain) {
    if (s.empty()) return -1;
    return playSound(std::make_shared<const std::vector<float>>(s.begin(), s.end()), loop, gain);
}
int CarbonFrontend::playSound(std::vector<float>&& s, bool loop, float gain) {
    if (s.empty()) return -1;
    return playSound(std::make_shared<const std::vector<float>>(std::move(s)), loop, gain);
}
int CarbonFrontend::playSound(std::shared_ptr<const std::vector<float>> s, bool loop, float gain) {
    return mix_.play(std::move(s), loop, gain);
}
int CarbonFrontend::playSound(std::shared_ptr<const omk::DeviceSound> s, bool loop, float gain) {
    return mix_.play(std::move(s), loop, gain);
}

// The films pace their video by this (the audio clock), so it refills first.
double CarbonFrontend::queuedSeconds() {
    refillAudio();
    return arate_ > 0 ? static_cast<double>(mix_.queued()) / (arate_ * achan_) : 0.0;
}

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
    // AN INSTRUMENT: `omk.quit` beside the application, holding a number of
    // seconds, ends the run by itself after that long, as Cmd-Q would - so a
    // run on Mac OS 9 (no shell, nothing to send it a quit) still closes its
    // audio device and writes its last lines. Absent, it does nothing.
    if (std::FILE* q = std::fopen("omk.quit", "r")) {
        double secs = 0.0;
        if (std::fscanf(q, "%lf", &secs) == 1 && secs > 0.0) {
            quitAt_ = microseconds() + static_cast<std::uint64_t>(secs * 1e6);
            std::printf("quit: omk.quit - the run ends after %.0f s\n", secs);
        }
        std::fclose(q);
    }
    return true;
}

bool CarbonFrontend::pump(HostInput& out) {
    refillAudio();
    out.text.clear();
    EventRecord ev;
    // everything waiting, without sleeping: the frame loop is the clock
    while (WaitNextEvent(everyEvent, &ev, 0, nullptr)) {
        switch (ev.what) {
        case keyDown: case autoKey: {
            const char c = static_cast<char>(ev.message & charCodeMask);
            if ((ev.modifiers & cmdKey) && (c == 'q' || c == 'Q')) {
                if (!quit_) std::printf("quit: Cmd-Q\n");
                quit_ = true;
                break;
            }
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
#if defined(OMK_GL1_AGL)
                if (agl_) aglUpdateContext(static_cast<AGLContext>(agl_));
#endif
            } else if (w == win_ && part == inGoAway) {
                if (TrackGoAway(w, ev.where)) {
                    if (!quit_) std::printf("quit: the window's close box\n");
                    quit_ = true;
                }
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
    if (g_quitRequested && !quit_) {
        std::printf("quit: a Quit Apple event (the Finder, the Dock, or another program)\n");
        quit_ = true;
    }
    if (quitAt_ && !quit_ && microseconds() >= quitAt_) {
        quit_ = true;
        std::printf("quit: omk.quit's time is up\n");
    }
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
    if (agl_) {
        presentGL(fb);
#if OMK_PROFILE
        classic_heap_sample();
#endif
        return;
    }
    if (!gw_ || gwW_ != fb.w || gwH_ != fb.h) {
        if (gw_) DisposeGWorld(static_cast<GWorldPtr>(gw_));
        gw_ = nullptr;
        ::Rect b;
        SetRect(&b, 0, 0, static_cast<short>(fb.w), static_cast<short>(fb.h));
        // AT THE WINDOW'S DEPTH: a GWorld of another depth makes CopyBits
        // convert every pixel of every frame (Mac OS X windows are 32 bits;
        // a 1999 Mac in thousands of colours is 16), so the one conversion
        // is the 565 one below. The films dropped a third of their frames
        // on Tiger with a 16-bit GWorld.
        const int depth = GetPixDepth(GetPortPixMap(GetWindowPort(static_cast<WindowRef>(win_)))) >= 24 ? 32 : 16;
        GWorldPtr gw = nullptr;
        if (NewGWorld(&gw, static_cast<short>(depth), &b, nullptr, nullptr, 0) != noErr || !gw) return;
        gw_ = gw;
        gwW_ = fb.w;
        gwH_ = fb.h;
        gwDepth_ = depth;
    }
    PixMapHandle pm = GetGWorldPixMap(static_cast<GWorldPtr>(gw_));
    if (!LockPixels(pm)) return;
    char* base = GetPixBaseAddr(pm);
    const long rowBytes = GetPixRowBytes(pm);
    for (int y = 0; y < fb.h; ++y) {
        const std::uint16_t* src = fb.px.data() + static_cast<std::size_t>(y) * fb.w;
        if (gwDepth_ == 16) {
            // RGB565 -> xRGB1555, native (big-endian) words: red and green
            // shift down one, green's low bit is dropped, blue stays
            auto* row = reinterpret_cast<std::uint16_t*>(base + y * rowBytes);
            for (int x = 0; x < fb.w; ++x)
                row[x] = static_cast<std::uint16_t>(((src[x] & 0xFFC0) >> 1) | (src[x] & 0x1F));
        } else {
            // RGB565 -> xRGB8888, each channel widened by bit replication -
            // what the 16-bit target looks like expanded, the colours unchanged
            auto* row = reinterpret_cast<std::uint32_t*>(base + y * rowBytes);
            for (int x = 0; x < fb.w; ++x) {
                const std::uint32_t v = src[x];
                const std::uint32_t r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
                row[x] = ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
            }
        }
    }
    UnlockPixels(pm);
    blit();
#if OMK_PROFILE
    classic_heap_sample();   // the memory budget's low-water mark, once a frame
#endif
}

void CarbonFrontend::blit() {
    if (!win_ || !gw_ || agl_) return;       // under GL the next frame redraws
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
    closeAudio();
#if defined(OMK_GL1_AGL)
    if (agl_) {
        AGLContext ctx = static_cast<AGLContext>(agl_);
        agl_ = nullptr;
        if (aglGetCurrentContext() == ctx) aglSetCurrentContext(nullptr);
        aglSetDrawable(ctx, nullptr);
        aglDestroyContext(ctx);
    }
#endif
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

// ---- OPENGL through AGL ------------------------------------------------------

void* CarbonFrontend::glContext() {
#if defined(OMK_GL1_AGL)
    if (agl_ || !win_) return agl_;
    // a hardware renderer first; else whatever renders (Mac OS 9 under
    // emulation has only Apple's software one)
    const GLint accel[] = {AGL_RGBA, AGL_DOUBLEBUFFER, AGL_DEPTH_SIZE, 16,
                           AGL_ACCELERATED, AGL_NO_RECOVERY, AGL_NONE};
    const GLint any[] = {AGL_RGBA, AGL_DOUBLEBUFFER, AGL_DEPTH_SIZE, 16, AGL_NONE};
    AGLPixelFormat pf = aglChoosePixelFormat(nullptr, 0, accel);
    const bool accelerated = pf != nullptr;
    if (!pf) pf = aglChoosePixelFormat(nullptr, 0, any);
    if (!pf) {
        std::printf("display: no AGL pixel format (%s)\n",
                    reinterpret_cast<const char*>(aglErrorString(aglGetError())));
        return nullptr;
    }
    AGLContext ctx = aglCreateContext(pf, nullptr);
    aglDestroyPixelFormat(pf);
    if (!ctx) return nullptr;
    if (!aglSetDrawable(ctx, GetWindowPort(static_cast<WindowRef>(win_)))) {
        std::printf("display: aglSetDrawable failed (%s)\n",
                    reinterpret_cast<const char*>(aglErrorString(aglGetError())));
        aglDestroyContext(ctx);
        return nullptr;
    }
    aglSetCurrentContext(ctx);
    // the surface brought up to the window now, and one black frame shown, so
    // the window holds nothing stale before the first world is drawn
    aglUpdateContext(ctx);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    aglSwapBuffers(ctx);
    agl_ = ctx;
    std::printf("display: an AGL context on the window (%s); the frame is presented "
                "by glDrawPixels and aglSwapBuffers from here on\n",
                accelerated ? "accelerated" : "NOT accelerated");
#endif
    return agl_;
}

void CarbonFrontend::presentGL(const Surface& fb) {
#if defined(OMK_GL1_AGL)
    AGLContext ctx = static_cast<AGLContext>(agl_);
    const AGLContext prev = aglGetCurrentContext();
    if (prev != ctx) aglSetCurrentContext(ctx);
    ::Rect b;
    GetPortBounds(GetWindowPort(static_cast<WindowRef>(win_)), &b);
    const int ww = b.right - b.left, wh = b.bottom - b.top;
    // the 2D state for one image; the renderer sets all of its own in begin()
    glViewport(0, 0, ww, wh);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, ww, 0, wh, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_FOG);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDrawBuffer(GL_BACK);
    // RGB565 in the host's own word order, which is what the packed type reads
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    // the raster position at the TOP-left (a move by glBitmap is never
    // clipped away, where a position on the window's edge can be), and the
    // rows drawn downward, scaled to the window
    glRasterPos2i(0, 0);
    glBitmap(0, 0, 0.0f, 0.0f, 0.0f, static_cast<GLfloat>(wh), nullptr);
    glPixelZoom(static_cast<GLfloat>(ww) / fb.w, -static_cast<GLfloat>(wh) / fb.h);
    glDrawPixels(fb.w, fb.h, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, fb.px.data());
    glPixelZoom(1.0f, 1.0f);
    aglSwapBuffers(ctx);
    if (prev != ctx) aglSetCurrentContext(prev);
#else
    (void)fb;
#endif
}

void* gl1AglContext() { return g_carbon ? g_carbon->glContext() : nullptr; }

std::unique_ptr<Frontend> makeHostFrontend() {
    auto f = std::make_unique<CarbonFrontend>();
    g_carbon = f.get();
    return f;
}

}  // namespace omk
