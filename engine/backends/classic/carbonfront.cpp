// SPDX-License-Identifier: GPL-3.0-or-later
// The Carbon frontend - see carbonfront.h.
#include "carbonfront.h"
#include "launchguard.h"
#include "platform/profile.h"     // OMK_PROFILE, before it is tested
#if OMK_PROFILE
#  include "heapcount.h"
#endif

#if defined(OMK_GL1_AGL)
#  include "../gl1/gl1host.h"
#  include <agl.h>
#  include <gl.h>
// Apple's upload extensions, by value: gl.h carries none of them and the
// probe asks the driver before using any (`probeUploads`)
#  ifndef GL_UNPACK_CLIENT_STORAGE_APPLE
#    define GL_UNPACK_CLIENT_STORAGE_APPLE 0x85B2
#  endif
#  ifndef GL_TEXTURE_RECTANGLE_EXT
#    define GL_TEXTURE_RECTANGLE_EXT 0x84F5
#  endif
#  ifndef GL_TEXTURE_STORAGE_HINT_APPLE
#    define GL_TEXTURE_STORAGE_HINT_APPLE 0x85BC
#  endif
#  ifndef GL_STORAGE_SHARED_APPLE
#    define GL_STORAGE_SHARED_APPLE 0x85BF
#  endif
#endif
#include "platform/datafs.h"      // hostPath, the films' HFS path
#include "ui/overlay.h"           // overlayBlockSums, the present's tile sums

#include <AppleEvents.h>
#include <Files.h>
#include <Gestalt.h>
#include <Movies.h>
#include <Events.h>
#include <MacWindows.h>
#include <QDOffscreen.h>
#include <Quickdraw.h>
#include <Sound.h>
#include <TextUtils.h>
#include <Timer.h>
#include <ToolUtils.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
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

// MAC OS 9 OR MAC OS X - the same Carbon binary runs on both, and two things
// differ: OS 9's File Manager CACHES writes (a hard freeze loses the log), and
// its multitasking is COOPERATIVE (an application that never yields starves
// the system). `gestaltSystemVersion` below 0x1000 is OS 9.
bool onMacOS9() {
    static int v = -1;
    if (v < 0) {
        long sv = 0;
        v = Gestalt(gestaltSystemVersion, &sv) == noErr && sv < 0x1000 ? 1 : 0;
    }
    return v == 1;
}

// A CHECKPOINT before a call that may freeze a real OS 9 machine (a real 3D
// card's AGL, the Sound Manager, QuickTime): the line is written and, on
// OS 9, the volume FLUSHED - so if the call freezes the computer,
// `omk-out.txt` still says which one it was; without the flush OS 9's write
// cache leaves nothing to read afterwards.
void checkpoint(const char* what) {
    classic_guard_arm(what);   // OS 9: the step named in `omk.guard`, flushed below
    std::printf("checkpoint: %s\n", what);
    std::fflush(stdout);
    if (onMacOS9()) FlushVol(nullptr, 0);
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
CarbonFrontend* g_carbon = nullptr;     // the one frontend, for gl1WindowContext

#if defined(OMK_GL1_AGL)
// THE UPLOAD FORMATS a composed frame can go to the card in. Which one a
// driver takes fastest is the driver's business: Mac OS X's take 32-bit
// ARGB (BGRA, 8-8-8-8-REV) and 16-bit ARGB 1555 without converting, and an
// RGB565 or an RGBA-bytes source may go through a software conversion texel
// by texel. On an iBook G3 (Rage 128 Pro, Mac OS X 10.2) a 565 frame costs
// 220-310 ms a menu frame to present (measured 2026-10-09). So `probeUploads`
// TIMES each when the context is made and the presents use the fastest.
struct UpFmt { GLint internal; GLenum format, type; int bytes; const char* name; };
// The first three are OpenGL 1.2's packed types (and GL_BGRA); an OpenGL 1.1
// driver refuses all three - the reader's iMac G3, "Rage 128 OpenGL Engine,
// OpenGL 1.1.ATI-5.69" (2026-10-30), whose menu then drew WHITE: every tile
// sent in a refused format, so no texture. The fourth, plain RGBA bytes, is
// OpenGL 1.1's own, probed only when the other three are all refused, stored
// at 16 bits like the 565.
const UpFmt kUiFmts[4] = {
    {GL_RGB5,    GL_RGB,  GL_UNSIGNED_SHORT_5_6_5,       2, "RGB565"},
    {GL_RGB5_A1, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV, 2, "ARGB1555"},
    {GL_RGBA8,   GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV,   4, "ARGB8888"},
    {GL_RGB5,    GL_RGBA, GL_UNSIGNED_BYTE,              4, "RGBA bytes"},
};
constexpr int kUiRgbaBytes = 3;
bool g_overlayArgb = false;              // the overlay's pick (`gl1OverlayArgb`)
omk::Gl1UploadPath g_overlayPath;        // ...and how it is sent (`gl1OverlayPath`)
long g_textureMemory = -1;               // free for textures, bytes (`gl1TextureMemory`); -1 unknown

// `n` RGB565 pixels into format `f` at `out` (format 0 is the 565 itself).
// 1555 drops green's lowest bit; 8888 replicates the high bits down, as the
// rest of the port widens a 565 colour.
void convert565(int f, const std::uint16_t* in, std::size_t n, std::uint8_t* out) {
    if (f == 1) {
        auto* o = reinterpret_cast<std::uint16_t*>(out);
        for (std::size_t i = 0; i < n; ++i) {
            const unsigned p = in[i];
            o[i] = static_cast<std::uint16_t>(0x8000u | ((p >> 1) & 0x7FE0u) | (p & 0x1Fu));
        }
    } else if (f == 2) {
        auto* o = reinterpret_cast<std::uint32_t*>(out);
        for (std::size_t i = 0; i < n; ++i) {
            const unsigned p = in[i];
            const unsigned r5 = (p >> 11) & 31, g6 = (p >> 5) & 63, b5 = p & 31;
            o[i] = 0xFF000000u | (r5 << 3 | r5 >> 2) << 16 | (g6 << 2 | g6 >> 4) << 8 | (b5 << 3 | b5 >> 2);
        }
    } else if (f == kUiRgbaBytes) {
        for (std::size_t i = 0; i < n; ++i) {
            const unsigned p = in[i];
            const unsigned r5 = (p >> 11) & 31, g6 = (p >> 5) & 63, b5 = p & 31;
            out[4 * i + 0] = static_cast<std::uint8_t>(r5 << 3 | r5 >> 2);
            out[4 * i + 1] = static_cast<std::uint8_t>(g6 << 2 | g6 >> 4);
            out[4 * i + 2] = static_cast<std::uint8_t>(b5 << 3 | b5 >> 2);
            out[4 * i + 3] = 0xFF;
        }
    }
}

// One quad over the window's `ww x wh`, texture rows top-down to `s1 x t1`.
void drawFrameQuad(int ww, int wh, float s1, float t1) {
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);   glVertex2f(0, static_cast<float>(wh));
    glTexCoord2f(s1, 0);  glVertex2f(static_cast<float>(ww), static_cast<float>(wh));
    glTexCoord2f(s1, t1); glVertex2f(static_cast<float>(ww), 0);
    glTexCoord2f(0, t1);  glVertex2f(0, 0);
    glEnd();
}
#endif
}

CarbonFrontend::~CarbonFrontend() {
    close();
    if (g_carbon == this) g_carbon = nullptr;
}

bool CarbonFrontend::openAudio(int rate, int channels) {
    if (chan_) return true;
    if (classic_guard_skips("sound")) {
        lastError_ = "omk.safe skips the Sound Manager (a run stopped in SndNewChannel)";
        return false;
    }
    static SndCallBackUPP played = NewSndCallBackUPP(soundPlayed);
    SndChannelPtr ch = nullptr;
    checkpoint("the sound channel (SndNewChannel)");
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
    {
        long sv = 0;
        Gestalt(gestaltSystemVersion, &sv);
        std::printf("system: Mac OS %lx.%lx.%lx%s\n", (sv >> 8) & 0xFF, (sv >> 4) & 0xF, sv & 0xF,
                    onMacOS9() ? " - the log is flushed to the disk at each checkpoint and every 2 s"
                               : "");
    }
    checkpoint("the window (NewCWindow)");
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
    classic_guard_tick();
#if OMK_PROFILE
    // the memory budget's low-water mark, once a frame - HERE, not in
    // `present`: a world frame swapped straight by the GL1 backend never
    // calls it, and the street start sampled once in 90 frames
    classic_heap_sample();
#endif
    // OS 9 keeps written bytes in its cache: the log reaches the disk every
    // 2 s, so a freeze leaves the last of it readable
    if (onMacOS9()) {
        static std::uint64_t lastFlush = 0;
        const std::uint64_t now = microseconds();
        if (now - lastFlush > 2000000) {
            lastFlush = now;
            std::fflush(stdout);
            FlushVol(nullptr, 0);
        }
    }
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
    static bool first = true;
    if (first) {
        first = false;
        checkpoint(agl_ ? "the first frame presented through OpenGL" : "the first frame presented");
    }
    if (agl_) {
        presentGL(fb);
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
    filmClose();
    if (qtState_ == 1) { ExitMovies(); qtState_ = 0; }
    closeAudio();
#if defined(OMK_GL1_AGL)
    if (agl_) {
        AGLContext ctx = static_cast<AGLContext>(agl_);
        agl_ = nullptr;
        if (aglGetCurrentContext() == ctx) aglSetCurrentContext(nullptr);
        aglSetDrawable(ctx, nullptr);
        aglDestroyContext(ctx);   // the frame's tiles go with it
        uiTiles_.clear();
        uiTilesX_ = uiTilesY_ = 0;
        filmTex_ = 0;
        filmTexW_ = filmTexH_ = 0;
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
        // YIELD, not spin: `Delay` busy-waits and never gives the processor
        // up, which OS 9's cooperative scheduler cannot take back. An EMPTY
        // event mask takes no event - the keys stay queued for the frame's
        // own `pump` - and lets every other process run for up to a tick.
        if (end - now > 20000) {
            EventRecord ev;
            WaitNextEvent(0, &ev, 1, nullptr);
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
    checkpoint("an OpenGL pixel format (aglChoosePixelFormat)");
    AGLPixelFormat pf = aglChoosePixelFormat(nullptr, 0, accel);
    const bool accelerated = pf != nullptr;
    if (!pf) pf = aglChoosePixelFormat(nullptr, 0, any);
    if (!pf) {
        std::printf("display: no AGL pixel format (%s)\n",
                    reinterpret_cast<const char*>(aglErrorString(aglGetError())));
        return nullptr;
    }
    checkpoint(accelerated ? "the OpenGL context, accelerated (aglCreateContext)"
                           : "the OpenGL context, NOT accelerated (aglCreateContext)");
    AGLContext ctx = aglCreateContext(pf, nullptr);
    aglDestroyPixelFormat(pf);
    if (!ctx) return nullptr;
    checkpoint("the context on the window (aglSetDrawable)");
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
    checkpoint("the upload probe (the first textures and draws)");
    probeUploads();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    aglSwapBuffers(ctx);
    agl_ = ctx;
    std::printf("display: an AGL context on the window (%s); the frame is presented "
                "as a textured quad and aglSwapBuffers from here on\n",
                accelerated ? "accelerated" : "NOT accelerated");
#endif
    return agl_;
}

void CarbonFrontend::presentGL(const Surface& fb) {
#if defined(OMK_GL1_AGL)
    // A frame the world is not swapped straight for - the interface alone,
    // the films, a composite - as TEXTURED QUADS, not `glDrawPixels` with
    // `glPixelZoom`, which the drivers of the period run in software: on an
    // iBook G3 under Mac OS X 10.2 (Rage 128) that makes the menus far
    // slower than the city, whose world never takes this path. The frame
    // stays in RGB565, the packed type in the host's
    // own word order - the one `glDrawPixels` already read correctly on
    // OS 9 and Tiger - into 16-bit textures, and only the TILES whose
    // content changed since they were last sent are uploaded: a menu's text
    // and frame stand still while only its animated parts move.
    AGLContext ctx = static_cast<AGLContext>(agl_);
    const AGLContext prev = aglGetCurrentContext();
    if (prev != ctx) aglSetCurrentContext(ctx);
    ::Rect b;
    GetPortBounds(GetWindowPort(static_cast<WindowRef>(win_)), &b);
    const int ww = b.right - b.left, wh = b.bottom - b.top;
    // the 2D state for one quad; the renderer sets all of its own in begin(),
    // its texture cache included
    glViewport(0, 0, ww, wh);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, ww, 0, wh, -1, 1);
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_FOG);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_LIGHTING);
    glDrawBuffer(GL_BACK);
    // IN 64x64 TILES, as the GL1 overlay: as ONE 1024x512 client-storage
    // texture, `present: rows` costs 44.9 ms a menu frame on the iBook G3
    // (`todo/handoff-classic-mac.md`) - the start menu's cloud (`ui/cloud.h`)
    // changes every row below the title band every frame, and the Rage's
    // driver sends the whole texture again after any change, the 384 columns
    // and 32 rows of padding included. So a sum a row a tile
    // column (`overlayBlockSums`) says which tiles changed, only their rows
    // are written into their memory and only they are sent; the title band,
    // a menu's frame and the padding are not. Always GL_TEXTURE_2D: a
    // 64x64 tile is a power of two, so the rectangle extension is moot.
    const UpFmt& fmt = kUiFmts[uiFmt_];
    constexpr int kTile = 64;
    const int tilesX = (fb.w + kTile - 1) / kTile, tilesY = (fb.h + kTile - 1) / kTile;
    if (tilesX != uiTilesX_ || tilesY != uiTilesY_ || uiTileFmt_ != uiFmt_) {
        for (UiTile& t : uiTiles_) if (t.tex) { GLuint g = t.tex; glDeleteTextures(1, &g); }
        uiTiles_.assign(static_cast<std::size_t>(tilesX) * tilesY, UiTile{});
        uiTilesX_ = tilesX;
        uiTilesY_ = tilesY;
        uiTileFmt_ = uiFmt_;
        uiRowKey_.assign(static_cast<std::size_t>(fb.h) * tilesX, 0);   // nothing sent yet
    }
    const std::size_t pitch = static_cast<std::size_t>(kTile) * fmt.bytes;
    glPixelStorei(GL_UNPACK_ALIGNMENT, fmt.bytes == 4 ? 4 : 2);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glColor4f(1, 1, 1, 1);
    {
        OMK_ZONE("present: rows");               // the profiler (todo/debug-tools.md)
        bool waited = false;                     // the card done with the client memory, once
        std::uint64_t rowChanged[kTile];         // per row of a band, the tile columns changed
        for (int ty = 0; ty < tilesY; ++ty) {
            const int y0 = ty * kTile, rows = std::min(kTile, fb.h - y0);
            std::uint64_t bandChanged = 0;
            for (int r = 0; r < rows; ++r) {
                const std::uint16_t* src = fb.px.data() + static_cast<std::size_t>(y0 + r) * fb.w;
                std::uint32_t* keys = uiRowKey_.data() + static_cast<std::size_t>(y0 + r) * tilesX;
                std::uint64_t ch = 0;
                for (int tx = 0; tx < tilesX; ++tx) {
                    const int x0 = tx * kTile, n = std::min(kTile, fb.w - x0);
                    std::uint32_t h1 = 0x811C9DC5u, h2 = 0x9E3779B9u;
                    omk::overlayBlockSums(src + x0, static_cast<std::size_t>(n) * 2, h1, h2);
                    const std::uint32_t key = (h1 ^ (h2 * 3u)) | 1u;        // 0 = never sent
                    if (keys[tx] != key) { keys[tx] = key; ch |= std::uint64_t{1} << (tx & 63); }
                }
                rowChanged[r] = ch;
                bandChanged |= ch;
            }
            if (!bandChanged) continue;
            for (int tx = 0; tx < tilesX; ++tx) {
                const std::uint64_t bit = std::uint64_t{1} << (tx & 63);
                if (!(bandChanged & bit)) continue;
                UiTile& T = uiTiles_[static_cast<std::size_t>(ty) * tilesX + tx];
                const int x0 = tx * kTile, n = std::min(kTile, fb.w - x0);
                if (!T.tex) {
                    GLuint g = 0;
                    glGenTextures(1, &g);
                    T.tex = g;
                    glBindTexture(GL_TEXTURE_2D, g);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
                    if (uiHint_) glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_STORAGE_HINT_APPLE, GL_STORAGE_SHARED_APPLE);
                    // the tile's memory: the client-storage texture's own, or
                    // the band a row is converted into before it is sent
                    T.store.assign(pitch * kTile, 0);
                    if (uiClient_) glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_TRUE);
                    glTexImage2D(GL_TEXTURE_2D, 0, fmt.internal, kTile, kTile, 0, fmt.format, fmt.type,
                                 uiClient_ ? T.store.data() : nullptr);
                    if (uiClient_) glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_FALSE);
                }
                // the card may still be reading last frame's texels
                if (uiClient_ && !waited) { glFinish(); waited = true; }
                for (int r = 0; r < rows; ++r) {
                    if (!(rowChanged[r] & bit)) continue;       // the tile's memory has it already
                    const std::uint16_t* s = fb.px.data() + static_cast<std::size_t>(y0 + r) * fb.w + x0;
                    std::uint8_t* d = T.store.data() + static_cast<std::size_t>(r) * pitch;
                    if (uiFmt_ == 0) std::memcpy(d, s, static_cast<std::size_t>(n) * 2);
                    else convert565(uiFmt_, s, static_cast<std::size_t>(n), d);
                }
                glBindTexture(GL_TEXTURE_2D, T.tex);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kTile, rows, fmt.format, fmt.type, T.store.data());
                ++uiStatTiles_;
            }
        }
    }
    // one quad a tile; texture row 0 is the frame's TOP, drawn at the
    // window's top (GL's y up)
    const float inv = 1.0f / kTile;
    for (int ty = 0; ty < tilesY; ++ty) {
        const int y0 = ty * kTile, rows = std::min(kTile, fb.h - y0);
        const float yTop = static_cast<float>(wh) - static_cast<float>(y0) * wh / fb.h;
        const float yBot = static_cast<float>(wh) - static_cast<float>(y0 + rows) * wh / fb.h;
        for (int tx = 0; tx < tilesX; ++tx) {
            const UiTile& T = uiTiles_[static_cast<std::size_t>(ty) * tilesX + tx];
            if (!T.tex) continue;                               // never sent: cannot happen after frame 1
            const int x0 = tx * kTile, n = std::min(kTile, fb.w - x0);
            const float xl = static_cast<float>(x0) * ww / fb.w, xr = static_cast<float>(x0 + n) * ww / fb.w;
            const float s1 = n * inv, t1 = rows * inv;
            glBindTexture(GL_TEXTURE_2D, T.tex);
            glBegin(GL_QUADS);
            glTexCoord2f(0, 0);   glVertex2f(xl, yTop);
            glTexCoord2f(s1, 0);  glVertex2f(xr, yTop);
            glTexCoord2f(s1, t1); glVertex2f(xr, yBot);
            glTexCoord2f(0, t1);  glVertex2f(xl, yBot);
            glEnd();
        }
    }
    if (++uiStatFrames_ % 300 == 0)
        std::printf("present: %ld frames as %dx%d tiles, %ld tiles sent (%.1f a frame of %d)\n",
                    uiStatFrames_, tilesX, tilesY, uiStatTiles_,
                    static_cast<double>(uiStatTiles_) / uiStatFrames_, tilesX * tilesY);
    const GLenum target = GL_TEXTURE_2D;
    glDisable(target);
    {
        OMK_ZONE("present: swap");
        aglSwapBuffers(ctx);
    }
    if (prev != ctx) aglSetCurrentContext(prev);
#else
    (void)fb;
#endif
}

#if defined(OMK_GL1_AGL)
// What the window's drawable and the card are, and how long each upload
// format takes to send a 640x480 frame AND draw it - a driver may defer a
// texture's upload to its first use, so the draw and a `glFinish` are inside
// the time. Best of four, the frame changed between them. A format the
// driver refuses (a GL error) is never picked.
void CarbonFrontend::probeUploads() {
    GLint rb = 0, gb = 0, bb = 0, ab = 0, db = 0;
    glGetIntegerv(GL_RED_BITS, &rb); glGetIntegerv(GL_GREEN_BITS, &gb);
    glGetIntegerv(GL_BLUE_BITS, &bb); glGetIntegerv(GL_ALPHA_BITS, &ab);
    glGetIntegerv(GL_DEPTH_BITS, &db);
    std::printf("display: the drawable is %d-%d-%d bits (alpha %d), depth buffer %d bits%s\n",
                static_cast<int>(rb), static_cast<int>(gb), static_cast<int>(bb),
                static_cast<int>(ab), static_cast<int>(db),
                rb <= 5 ? " - the display is in thousands of colours" : " - millions of colours");
    ::Rect b;
    GetPortBounds(GetWindowPort(static_cast<WindowRef>(win_)), &b);
    const int ww = b.right - b.left, wh = b.bottom - b.top;
    // THE CARD'S FREE TEXTURE MEMORY, which the original asks for before it
    // sizes its textures (`sub_45F4F0`: `GetAvailableVidMem` on
    // DDSCAPS_TEXTURE | DDSCAPS2_TEXTUREMANAGE, after its screen buffers are
    // made - `docs/ASSETS.md` 4b): the accelerated renderer's texture memory
    // (its video memory less the display's own buffer; the video memory less
    // that buffer where the driver gives none), less this window's back and
    // depth buffers. The GL1 backend applies the original's rule to it
    // (`gl1TextureMemory`).
    {
        GLint vram = 0, tmem = 0;
        GDHandle gd = GetMainDevice();
        if (AGLRendererInfo ri = aglQueryRendererInfo(&gd, 1)) {
            for (AGLRendererInfo r = ri; r; r = aglNextRendererInfo(r)) {
                GLint acc = 0, vm = 0, tm = 0;
                aglDescribeRenderer(r, AGL_ACCELERATED, &acc);
                aglDescribeRenderer(r, AGL_VIDEO_MEMORY, &vm);
                aglDescribeRenderer(r, AGL_TEXTURE_MEMORY, &tm);
                if (acc && vm > vram) { vram = vm; tmem = tm; }
            }
            aglDestroyRendererInfo(ri);
        }
        long screen = 0;
        if (gd && (**gd).gdPMap) {
            const PixMapHandle pm = (**gd).gdPMap;
            const ::Rect sb = (**pm).bounds;
            screen = static_cast<long>(sb.right - sb.left) * (sb.bottom - sb.top) * (((**pm).pixelSize + 7) / 8);
        }
        const long back = static_cast<long>(ww) * wh * (rb <= 5 ? 2 : 4);
        const long depth = static_cast<long>(ww) * wh * ((db + 7) / 8);
        const long forTextures = tmem > 0 ? tmem : (vram > 0 ? vram - screen : 0);
        g_textureMemory = vram > 0 ? forTextures - back - depth : -1;
        std::printf("display: video memory %ld KB, %ld KB for textures by the driver (the screen's "
                    "buffer %ld KB), this window's back and depth buffers %ld KB -> %ld KB free for "
                    "textures%s\n", static_cast<long>(vram) / 1024, static_cast<long>(tmem) / 1024,
                    screen / 1024, (back + depth) / 1024, g_textureMemory / 1024,
                    vram > 0 ? "" : " (not reported: the textures are sized as on a large card)");
    }
    const int W = 640, H = 480, TW = 1024, TH = 512;
    std::vector<std::uint16_t> frame(static_cast<std::size_t>(W) * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            frame[static_cast<std::size_t>(y) * W + x] = static_cast<std::uint16_t>(x * 37 ^ y * 101);
    std::vector<std::uint8_t> conv(static_cast<std::size_t>(W) * H * 4);
    std::vector<std::uint32_t> rgba(static_cast<std::size_t>(W) * H);   // the overlay's source
    glViewport(0, 0, ww, wh);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, ww, 0, wh, -1, 1);
    glMatrixMode(GL_TEXTURE); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_DEPTH_TEST); glDisable(GL_ALPHA_TEST); glDisable(GL_FOG);
    glDisable(GL_CULL_FACE); glDisable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glColor4f(1, 1, 1, 1);
    // -> ms for one send-and-draw, best of four; -1 when the driver refused it
    const auto timeOne = [&](GLint internal, GLenum format, GLenum type, int bytes,
                             const std::function<const void*()>& source, bool blend) {
        while (glGetError() != GL_NO_ERROR) {}
        GLuint t = 0;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, internal, TW, TH, 0, format, type, nullptr);
        bool ok = glGetError() == GL_NO_ERROR;
        if (blend) { glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_SRC_ALPHA); }
        else glDisable(GL_BLEND);
        double best = 1e30;
        for (int rep = 0; rep < 4 && ok; ++rep) {
            frame[static_cast<std::size_t>(rep)] ^= 0x0841u;
            glFinish();
            const std::uint64_t t0 = microseconds();
            const void* src = source();
            glPixelStorei(GL_UNPACK_ALIGNMENT, bytes == 4 ? 4 : 2);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, format, type, src);
            drawFrameQuad(ww, wh, static_cast<float>(W) / TW, static_cast<float>(H) / TH);
            glFinish();
            const double ms = static_cast<double>(microseconds() - t0) / 1000.0;
            if (ms < best) best = ms;
            ok = glGetError() == GL_NO_ERROR;
        }
        glDisable(GL_BLEND);
        glDeleteTextures(1, &t);
        return ok ? best : -1.0;
    };
    double ui[4] = {-1.0, -1.0, -1.0, -1.0};
    int pick = -1;
    for (int f = 0; f < 4; ++f) {
        if (f == kUiRgbaBytes && pick >= 0) break;      // OpenGL 1.1's form, only as the last resort
        const UpFmt& u = kUiFmts[f];
        ui[f] = timeOne(u.internal, u.format, u.type, u.bytes, [&]() -> const void* {
            if (f == 0) return frame.data();
            convert565(f, frame.data(), frame.size(), conv.data());
            return conv.data();
        }, false);
        if (ui[f] >= 0 && (pick < 0 || ui[f] < ui[pick])) pick = f;
    }
    uiFmt_ = pick < 0 ? kUiRgbaBytes : pick;
    // APPLE'S FAST UPLOAD: the same frame through CLIENT STORAGE
    // - the texture's memory is ours and the card reads it there, with no
    // copy into the driver - then also as a RECTANGLE (640x480, not a
    // 1024x512 power of two), then with the SHARED storage hint. Each only
    // where the driver names the extension; the conversion is in the time,
    // written straight into the texture's memory as `presentGL` writes it.
    // The Rage 128's figures above (63 ms for ARGB1555) are what this tries
    // to beat: every menu, the films and the overlay pay one such upload.
    const char* ext = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    const auto has = [&](const char* n) { return ext && std::strstr(ext, n) != nullptr; };
    const bool hasClient = has("GL_APPLE_client_storage");
    const bool hasRect = has("GL_EXT_texture_rectangle") || has("GL_ARB_texture_rectangle");
    const bool hasRange = has("GL_APPLE_texture_range");
    std::printf("gl1: upload extensions - client storage %s, rectangle textures %s, "
                "texture range %s\n", hasClient ? "YES" : "no", hasRect ? "YES" : "no",
                hasRange ? "YES" : "no");
    const auto timeClient = [&](int f, bool rect, bool hint) {
        while (glGetError() != GL_NO_ERROR) {}
        const UpFmt& u = kUiFmts[f];
        const GLenum target = rect ? GL_TEXTURE_RECTANGLE_EXT : GL_TEXTURE_2D;
        const int tw = rect ? W : TW, th = rect ? H : TH;
        const std::size_t pitch = static_cast<std::size_t>(tw) * u.bytes;
        std::vector<std::uint8_t> store(pitch * th);
        glDisable(GL_TEXTURE_2D);
        GLuint t = 0;
        glGenTextures(1, &t);
        glBindTexture(target, t);
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        if (hint) glTexParameteri(target, GL_TEXTURE_STORAGE_HINT_APPLE, GL_STORAGE_SHARED_APPLE);
        glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_TRUE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, u.bytes == 4 ? 4 : 2);
        glTexImage2D(target, 0, u.internal, tw, th, 0, u.format, u.type, store.data());
        glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_FALSE);
        glEnable(target);
        bool ok = glGetError() == GL_NO_ERROR;
        double best = 1e30;
        for (int rep = 0; rep < 4 && ok; ++rep) {
            frame[static_cast<std::size_t>(rep)] ^= 0x0841u;
            glFinish();
            const std::uint64_t t0 = microseconds();
            for (int y = 0; y < H; ++y) {
                const std::uint16_t* s = frame.data() + static_cast<std::size_t>(y) * W;
                std::uint8_t* d = store.data() + static_cast<std::size_t>(y) * pitch;
                if (f == 0) std::memcpy(d, s, static_cast<std::size_t>(W) * 2);
                else convert565(f, s, static_cast<std::size_t>(W), d);
            }
            glPixelStorei(GL_UNPACK_ROW_LENGTH, tw);
            glTexSubImage2D(target, 0, 0, 0, W, H, u.format, u.type, store.data());
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            drawFrameQuad(ww, wh, rect ? static_cast<float>(W) : static_cast<float>(W) / TW,
                          rect ? static_cast<float>(H) : static_cast<float>(H) / TH);
            glFinish();
            const double ms = static_cast<double>(microseconds() - t0) / 1000.0;
            if (ms < best) best = ms;
            ok = glGetError() == GL_NO_ERROR;
        }
        glDisable(target);
        glDeleteTextures(1, &t);
        glEnable(GL_TEXTURE_2D);
        return ok ? best : -1.0;
    };
    double argbClient[3] = {-1.0, -1.0, -1.0};   // ARGB8888's three variants, for the overlay
    if (hasClient && pick >= 0) {
        // the variants of the format already picked, and of the other 16-bit
        // one, so a format that loses as a copy may still win as client memory
        double bestMs = ui[pick];
        const char* how = "a copy";
        for (const int f : {1, 2}) {
            if (ui[f] < 0) continue;
            const double c  = timeClient(f, false, false);
            const double cr = hasRect ? timeClient(f, true, false) : -1.0;
            const double ch = hasRect && hasRange ? timeClient(f, true, true) : -1.0;
            if (f == 2) { argbClient[0] = c; argbClient[1] = cr; argbClient[2] = ch; }
            std::printf("gl1: upload probe, %s through client storage: %.1f ms, as a rectangle "
                        "%.1f, with the shared hint %.1f (-1: not offered or refused)\n",
                        kUiFmts[f].name, c, cr, ch);
            const struct { double ms; bool rect, hint; const char* how; } v[3] = {
                {c, false, false, "client storage"}, {cr, true, false, "client storage, rectangle"},
                {ch, true, true, "client storage, rectangle, shared"}};
            for (const auto& o : v)
                if (o.ms >= 0 && o.ms < bestMs) {
                    bestMs = o.ms; uiFmt_ = f; uiClient_ = true; uiRect_ = o.rect; uiHint_ = o.hint;
                    how = o.how;
                }
        }
        std::printf("gl1: the menus will be sent as %s through %s (%.1f ms a frame in the probe)\n",
                    kUiFmts[uiFmt_].name, how, bestMs);
    }
    argbOk_ = ui[2] >= 0;                    // the films' layout (`filmShow`)
    film1555_ = ui[1] >= 0 && (ui[2] < 0 || ui[1] < ui[2]);
    // the overlay: RGBA bytes (what it sent) against ARGB 8888, both 8-bit
    // alpha and blended as the overlay draws; the conversion is in the time
    const auto fillRgba = [&](bool argb) -> const void* {
        for (std::size_t i = 0; i < rgba.size(); ++i) {
            const unsigned p = frame[i];
            const unsigned r = (p >> 8) & 0xF8, g = (p >> 3) & 0xFC, bl = (p << 3) & 0xF8, a = p & 0xFF;
            if (argb) rgba[i] = a << 24 | r << 16 | g << 8 | bl;
            else {
                auto* o = reinterpret_cast<std::uint8_t*>(&rgba[i]);
                o[0] = static_cast<std::uint8_t>(r); o[1] = static_cast<std::uint8_t>(g);
                o[2] = static_cast<std::uint8_t>(bl); o[3] = static_cast<std::uint8_t>(a);
            }
        }
        return rgba.data();
    };
    const double ovBytes = timeOne(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4,
                                   [&]() { return fillRgba(false); }, true);
    const double ovArgb = timeOne(GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, 4,
                                  [&]() { return fillRgba(true); }, true);
    g_overlayArgb = ovArgb >= 0 && (ovBytes < 0 || ovArgb < ovBytes);
    // the overlay through client storage when an ARGB8888 variant beat its
    // copy (the overlay is always 32-bit: it carries an 8-bit alpha)
    {
        double bestOv = g_overlayArgb ? ovArgb : ovBytes;
        const char* how = "a copy";
        const bool rect[3] = {false, true, true}, hint[3] = {false, false, true};
        const char* name[3] = {"client storage", "client storage, rectangle",
                               "client storage, rectangle, shared"};
        for (int k = 0; k < 3; ++k)
            if (argbClient[k] >= 0 && (bestOv < 0 || argbClient[k] < bestOv)) {
                bestOv = argbClient[k];
                g_overlayPath = {true, rect[k], hint[k]};
                how = name[k];
            }
        std::printf("gl1: the overlay will be sent through %s\n", how);
    }
    glDisable(GL_TEXTURE_2D);
    std::printf("gl1: upload probe, a 640x480 frame sent and drawn (ms, best of 4; -1 refused): "
                "RGB565 %.1f, ARGB1555 %.1f, ARGB8888 %.1f, RGBA bytes %.1f (tried only when those three are refused) -> the menus as %s; overlay RGBA "
                "bytes %.1f, ARGB8888 %.1f -> %s\n", ui[0], ui[1], ui[2], ui[3], kUiFmts[uiFmt_].name,
                ovBytes, ovArgb, g_overlayArgb ? "ARGB8888" : "RGBA bytes");
}
#endif

// ---- THE BOOT FILMS THROUGH QUICKTIME ------------------------------------
//
// What a 1999 Mac port would have done (`todo/classic-mac-port-1999.md`:
// QuickTime for the MPEG-1 films), and what the iBook needs: the software
// decoder runs ~0.5 fps there (measured 2026-10-09, `GAME.mpg: 82 of 115
// frames dropped`) - every picture decoded by pl_mpeg, doubled on the CPU and
// sent as a whole 640x480 frame. QuickTime decodes and paces the film and
// plays its sound itself; only the picture it drew is sent, at its own
// 320x240, and the card doubles it (GL_NEAREST: the same replication as the
// CPU's exact 2x).

namespace {
pascal OSErr filmDrawn(::Movie, long refCon) {
    reinterpret_cast<CarbonFrontend*>(refCon)->filmDrawn_ = true;
    return noErr;
}
}  // namespace

bool CarbonFrontend::filmOpen(const std::string& hostFile) {
    if (!win_ || qtState_ < 0) return false;
    if (qtState_ == 0 && classic_guard_skips("quicktime")) {
        std::printf("film: omk.safe skips QuickTime - the software decoder plays the films\n");
        qtState_ = -1;
        return false;
    }
    if (qtState_ == 0) {
        long version = 0;
        if (Gestalt(gestaltQuickTime, &version) != noErr) {
            std::printf("film: no QuickTime - the software decoder plays the films\n");
            qtState_ = -1;
            return false;
        }
        checkpoint("QuickTime (EnterMovies)");
        const OSErr e = EnterMovies();
        if (e != noErr) {
            std::printf("film: EnterMovies failed (%d) - the software decoder plays the films\n", e);
            qtState_ = -1;
            return false;
        }
        std::printf("film: QuickTime %lx entered\n", static_cast<unsigned long>(version) >> 16);
        qtState_ = 1;
    }
    const std::string hp = hostPath(hostFile);
    Str255 pname;
    c2pstrcpy(pname, hp.c_str());
    FSSpec spec;
    OSErr e = FSMakeFSSpec(0, 0, pname, &spec);
    short ref = 0;
    if (e == noErr) e = OpenMovieFile(&spec, &ref, fsRdPerm);
    if (e != noErr) {
        std::printf("film: %s not opened by QuickTime (%d) - the software decoder\n", hp.c_str(), e);
        return false;
    }
    ::Movie m = nullptr;
    short resId = 0;
    checkpoint("a film opened (NewMovieFromFile)");
    e = NewMovieFromFile(&m, ref, &resId, nullptr, newMovieActive, nullptr);
    CloseMovieFile(ref);
    if (e != noErr || !m) {
        std::printf("film: %s not imported by QuickTime (%d) - the software decoder\n", hp.c_str(), e);
        return false;
    }
    ::Rect box;
    GetMovieBox(m, &box);
    OffsetRect(&box, static_cast<short>(-box.left), static_cast<short>(-box.top));
    filmW_ = box.right;
    filmH_ = box.bottom;
    ::Rect win;
    CGrafPtr port = GetWindowPort(static_cast<WindowRef>(win_));
    GetPortBounds(port, &win);
    const int ww = win.right - win.left, wh = win.bottom - win.top;
    filmTaskUs_ = filmConvUs_ = filmUpUs_ = filmDrawUs_ = 0;
    filmSteps_ = 0;
    filmGL_ = agl_ && !filmDirect_;
#if defined(OMK_GL1_AGL)
    if (agl_ && filmDirect_) {
        // `--films-direct`: the AGL context's drawable set aside for the film,
        // so QuickTime's own drawing into the window is what shows - its
        // decoder's own path to the screen, with no conversion or upload of
        // ours (an OpenGL 1.1 driver takes the picture only as RGBA bytes,
        // converted on the CPU). Put back by `filmClose`.
        aglSetDrawable(static_cast<AGLContext>(agl_), nullptr);
        filmDetached_ = true;
    }
#endif
    if (filmGL_) {
        // under GL: QuickTime draws into a GWorld, `filmShow` sends it
        // AT 16 BITS when the probe found ARGB1555 the faster upload - an
        // iBook G3 in thousands of colours: 63 ms a 640x480 frame against
        // 113 for ARGB8888 (measured 2026-10-09). A 16-bit GWorld is xRGB
        // 1-5-5-5, which BGRA 1-5-5-5-REV reads as it stands, and QuickTime
        // converts less to fill it.
        filmDepth_ = film1555_ ? 16 : 32;
        GWorldPtr gw = nullptr;
        if (NewGWorld(&gw, static_cast<short>(filmDepth_), &box, nullptr, nullptr, 0) != noErr || !gw) {
            std::printf("film: no %d-bit GWorld - the software decoder\n", filmDepth_);
            DisposeMovie(m);
            return false;
        }
        LockPixels(GetGWorldPixMap(gw));
        filmGw_ = gw;
        SetMovieGWorld(m, gw, nullptr);
    } else {
        // the plain window: QuickTime draws into it, fitted and centred
        SetPort(port);
        ForeColor(blackColor);
        PaintRect(&win);
        const double fit = std::min(static_cast<double>(ww) / filmW_, static_cast<double>(wh) / filmH_);
        const int ow = static_cast<int>(filmW_ * fit), oh = static_cast<int>(filmH_ * fit);
        ::Rect r;
        SetRect(&r, static_cast<short>((ww - ow) / 2), static_cast<short>((wh - oh) / 2),
                static_cast<short>((ww - ow) / 2 + ow), static_cast<short>((wh - oh) / 2 + oh));
        SetMovieGWorld(m, port, nullptr);
        SetMovieBox(m, &r);
    }
    static MovieDrawingCompleteUPP drawn = NewMovieDrawingCompleteUPP(filmDrawn);
    SetMovieDrawingCompleteProc(m, movieDrawingCallWhenChanged, drawn, reinterpret_cast<long>(this));
    // the world's sound channel stays as it is: QuickTime opens its own
    flushAudio();
    film_ = m;
    filmFrames_ = 0;
    filmDrawn_ = false;
    GoToBeginningOfMovie(m);
    SetMovieVolume(m, kFullVolume);
    StartMovie(m);
    std::printf("film: %s through QuickTime, %dx%d, %.1f s, %s\n", hp.c_str(), filmW_, filmH_,
                static_cast<double>(GetMovieDuration(m)) / GetMovieTimeScale(m),
                filmGL_ ? (filmDepth_ == 16 ? "sent to the card as ARGB1555"
                        : argbOk_ ? "sent to the card as ARGB8888" : "sent to the card as RGBA bytes")
                     : "drawn into the window");
    return true;
}

bool CarbonFrontend::filmStep() {
    if (!film_) return false;
    ::Movie m = static_cast<::Movie>(film_);
    const std::uint64_t t0 = microseconds();
    MoviesTask(m, 0);
    filmTaskUs_ += microseconds() - t0;
    ++filmSteps_;
    if (filmDrawn_) {
        filmDrawn_ = false;
        ++filmFrames_;
        if (filmGL_) filmShow();
        else QDFlushPortBuffer(GetWindowPort(static_cast<WindowRef>(win_)), nullptr);
    }
    return !IsMovieDone(m) && GetMoviesError() == noErr;
}

void CarbonFrontend::filmClose() {
    if (!film_) return;
    ::Movie m = static_cast<::Movie>(film_);
    StopMovie(m);
    DisposeMovie(m);
    film_ = nullptr;
#if defined(OMK_GL1_AGL)
    // a texture made over the GWorld's own memory (client storage) must go
    // BEFORE that memory does - and each film makes its own GWorld anyway
    if (filmTex_ && agl_) {
        const AGLContext prev = aglGetCurrentContext();
        aglSetCurrentContext(static_cast<AGLContext>(agl_));
        GLuint t = static_cast<GLuint>(filmTex_);
        glDeleteTextures(1, &t);
        if (prev != static_cast<AGLContext>(agl_)) aglSetCurrentContext(prev);
        filmTex_ = 0;
        filmTexW_ = filmTexH_ = 0;
    }
#endif
    if (filmGw_) {
        DisposeGWorld(static_cast<GWorldPtr>(filmGw_));
        filmGw_ = nullptr;
    }
#if defined(OMK_GL1_AGL)
    if (filmDetached_) {
        aglSetDrawable(static_cast<AGLContext>(agl_), GetWindowPort(static_cast<WindowRef>(win_)));
        aglUpdateContext(static_cast<AGLContext>(agl_));
        filmDetached_ = false;
    }
#endif
    std::printf("film: %ld pictures drawn by QuickTime %s; MoviesTask %.0f ms over %ld steps (%.1f ms a "
                "picture)%s",
                filmFrames_, filmGL_ ? "and sent through OpenGL" : "straight into the window",
                filmTaskUs_ / 1000.0, filmSteps_,
                filmFrames_ ? filmTaskUs_ / 1000.0 / filmFrames_ : 0.0, filmGL_ ? "" : "\n");
    if (filmGL_)
        std::printf("; a picture shown: converted %.1f ms, uploaded %.1f, drawn and swapped %.1f\n",
                    filmFrames_ ? filmConvUs_ / 1000.0 / filmFrames_ : 0.0,
                    filmFrames_ ? filmUpUs_ / 1000.0 / filmFrames_ : 0.0,
                    filmFrames_ ? filmDrawUs_ / 1000.0 / filmFrames_ : 0.0);
}

void CarbonFrontend::filmShow() {
#if defined(OMK_GL1_AGL)
    AGLContext ctx = static_cast<AGLContext>(agl_);
    const AGLContext prev = aglGetCurrentContext();
    if (prev != ctx) aglSetCurrentContext(ctx);
    PixMapHandle pm = GetGWorldPixMap(static_cast<GWorldPtr>(filmGw_));
    const auto* base = reinterpret_cast<const std::uint8_t*>(GetPixBaseAddr(pm));
    const int rowBytes = GetPixRowBytes(pm) & 0x3FFF;
    ::Rect b;
    GetPortBounds(GetWindowPort(static_cast<WindowRef>(win_)), &b);
    const int ww = b.right - b.left, wh = b.bottom - b.top;
    // fitted and centred, as the software path fits the film
    const double fit = std::min(static_cast<double>(ww) / filmW_, static_cast<double>(wh) / filmH_);
    const int ow = static_cast<int>(filmW_ * fit), oh = static_cast<int>(filmH_ * fit);
    glViewport(0, 0, ww, wh);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport((ww - ow) / 2, (wh - oh) / 2, ow, oh);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, ow, 0, oh, -1, 1);
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_FOG);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDrawBuffer(GL_BACK);
    // THE GWORLD AS THE TEXTURE'S MEMORY when the probe chose client storage
    // and rectangles (`probeUploads`): QuickTime draws the picture where the
    // card reads it, and nothing is copied - the period's own recipe for
    // QuickTime under OpenGL. A rectangle, because the GWorld is the film's
    // 320x240 and not a power of two.
    const bool client = uiClient_ && uiRect_ && (filmDepth_ == 16 || argbOk_);
    const GLenum target = client ? GL_TEXTURE_RECTANGLE_EXT : GL_TEXTURE_2D;
    const GLenum upFormat = GL_BGRA;
    const GLenum upType = filmDepth_ == 16 ? GL_UNSIGNED_SHORT_1_5_5_5_REV : GL_UNSIGNED_INT_8_8_8_8_REV;
    const int bpp = filmDepth_ == 16 ? 2 : 4;
    int tw = 1, th = 1;
    if (client) { tw = filmW_; th = filmH_; }
    else {
        while (tw < filmW_) tw <<= 1;
        while (th < filmH_) th <<= 1;
    }
    GLuint tex = static_cast<GLuint>(filmTex_);
    if (!tex || filmTexW_ != tw || filmTexH_ != th) {
        if (tex) glDeleteTextures(1, &tex);
        glGenTextures(1, &tex);
        filmTex_ = tex;
        glBindTexture(target, tex);
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP);
        if (client) {
            if (uiHint_) glTexParameteri(target, GL_TEXTURE_STORAGE_HINT_APPLE, GL_STORAGE_SHARED_APPLE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, bpp);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, rowBytes / bpp);
            glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_TRUE);
            glTexImage2D(target, 0, filmDepth_ == 16 ? GL_RGB5 : GL_RGB8, tw, th, 0, upFormat,
                         upType, base);
            glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_FALSE);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            std::printf("film: the GWorld is the texture's own memory (client storage, %s)\n",
                        uiHint_ ? "rectangle, shared" : "rectangle");
        } else {
            glTexImage2D(target, 0, filmDepth_ == 16 ? GL_RGB5 : GL_RGB8, tw, th, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, nullptr);
        }
        filmTexW_ = tw;
        filmTexH_ = th;
    }
    glBindTexture(target, tex);
    std::uint64_t tUp = microseconds();
    {
        OMK_ZONE("film: upload");
        if (client) {
            // the same memory: this tells the driver the picture changed
            glPixelStorei(GL_UNPACK_ALIGNMENT, bpp);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, rowBytes / bpp);
            glTexSubImage2D(target, 0, 0, 0, filmW_, filmH_, upFormat, upType, base);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        } else if (filmDepth_ == 16) {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, rowBytes / 2);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, filmW_, filmH_, GL_BGRA,
                            GL_UNSIGNED_SHORT_1_5_5_5_REV, base);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        } else if (argbOk_) {
            // a 32-bit GWorld is xRGB in memory, big-endian: one word
            // x<<24|r<<16|g<<8|b, which BGRA 8-8-8-8-REV reads as it stands
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, rowBytes / 4);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, filmW_, filmH_, GL_BGRA,
                            GL_UNSIGNED_INT_8_8_8_8_REV, base);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        } else {
            const std::size_t n = static_cast<std::size_t>(filmW_) * filmH_;
            if (uiConv_.size() < n * 4) uiConv_.resize(n * 4);
            std::uint8_t* o = uiConv_.data();
            for (int y = 0; y < filmH_; ++y) {
                const std::uint8_t* s = base + static_cast<std::size_t>(y) * rowBytes;
                for (int x = 0; x < filmW_; ++x, s += 4, o += 4) {
                    o[0] = s[1]; o[1] = s[2]; o[2] = s[3]; o[3] = 255;
                }
            }
            const std::uint64_t tConv = microseconds();
            filmConvUs_ += tConv - tUp;
            tUp = tConv;
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, filmW_, filmH_, GL_RGBA, GL_UNSIGNED_BYTE,
                            uiConv_.data());
        }
    }
    const std::uint64_t tDraw = microseconds();
    filmUpUs_ += tDraw - tUp;
    glEnable(target);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glColor4f(1, 1, 1, 1);
    if (client) drawFrameQuad(ow, oh, static_cast<float>(filmW_), static_cast<float>(filmH_));
    else drawFrameQuad(ow, oh, static_cast<float>(filmW_) / tw, static_cast<float>(filmH_) / th);
    glDisable(target);
    {
        OMK_ZONE("film: swap");
        aglSwapBuffers(ctx);
    }
    filmDrawUs_ += microseconds() - tDraw;
    if (prev != ctx) aglSetCurrentContext(prev);
#endif
}

void* gl1WindowContext() { return g_carbon ? g_carbon->glContext() : nullptr; }
#if defined(OMK_GL1_AGL)
bool gl1OverlayArgb() { return g_overlayArgb; }
Gl1UploadPath gl1OverlayPath() { return g_overlayPath; }
long gl1TextureMemory() { return g_textureMemory; }
#endif

void gl1WindowSize(int& w, int& h) {
    w = h = 0;
    if (!g_carbon || !g_carbon->windowId()) return;
    ::Rect b;
    GetPortBounds(GetWindowPort(static_cast<WindowRef>(const_cast<void*>(g_carbon->windowId()))), &b);
    w = b.right - b.left;
    h = b.bottom - b.top;
}

std::unique_ptr<Frontend> makeHostFrontend() {
    auto f = std::make_unique<CarbonFrontend>();
    g_carbon = f.get();
    return f;
}

}  // namespace omk
