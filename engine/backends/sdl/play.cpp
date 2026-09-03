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
#include "actor/pose.h"
#include "actor/speaker.h"
#include "actor/player.h"
#include "actor/walk.h"
#include "o3de/collision.h"
#include "o3de/geom3do.h"
#include "o3de/particles.h"
#include "audio/mixer.h"
#include "audio/music.h"
#include "audio/voiceover.h"
#include "formats/adpcm.h"
#include "script/area.h"
#include "script/gamestate.h"
#include "o3de/raster.h"
#include "o3de/renderer.h"
#include "platform/boot.h"
#include "platform/movie.h"
#include "platform/datafs.h"
#include "platform/frontend.h"
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
              std::vector<std::string>& out) {
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

void drawSubtitle(omk::Surface& fb, const omk::TextLayout& lay,
                  const std::string& line,
                  const std::vector<std::string>& menu, int selected,
                  int dispW, int dispH, int inset640 = 32) {
    // 32 is `Dialog_TickUI`'s block; `Subtitle_Show` (0x0041E040) lays a
    // `media.play` line out inset 16 - the caller says which.
    const int inset = inset640 * dispW / 640;
    const int left = inset, right = dispW - inset, width = right - left;
    if (width <= 0) return;

    std::vector<std::string> rows;
    std::vector<std::uint8_t> tone;
    std::vector<std::string> tmp;
    if (!line.empty()) {
        wrapInto(lay, line, width, tmp);
        for (auto& r : tmp) { rows.push_back(r); tone.push_back(255); }
    }
    for (std::size_t k = 0; k < menu.size(); ++k) {
        tmp.clear();
        wrapInto(lay, menu[k], width, tmp);
        for (auto& r : tmp) {
            rows.push_back(r);
            tone.push_back(static_cast<int>(k) == selected ? 255 : 128);
        }
    }
    if (rows.empty()) return;

    const auto probe = omk::parseMarkup("Ag");
    const int lineH = lay.height(probe.run) + 2;
    int y = dispH - inset / 2 - static_cast<int>(rows.size()) * lineH;
    if (y < 0) y = 0;
    for (std::size_t k = 0; k < rows.size(); ++k) {
        auto pt = omk::parseMarkup(rows[k]);
        for (auto& sc : pt.run) sc.rgb[0] = sc.rgb[1] = sc.rgb[2] = tone[k];
        const int w = lay.measure(pt.run);
        lay.drawRun(fb, left + (width - w) / 2, y, pt.run);
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
            for (auto& one : self->shots_)
                if (one.pos < one.pcm.size()) v += one.pcm[one.pos++];
            dst[i] = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
        }
        // Reclaim the consumed head rather than growing for ever.
        if (self->sHead_ > (1u << 20)) {
            self->stream_.erase(self->stream_.begin(),
                                self->stream_.begin() + static_cast<std::ptrdiff_t>(self->sHead_));
            self->sHead_ = 0;
        }
        std::erase_if(self->shots_, [](const Shot& o) { return o.pos >= o.pcm.size(); });
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

    int playSound(std::span<const float> s) override {
        if (s.empty()) return -1;
        std::lock_guard<std::mutex> lk(amx_);
        // A cap, because a held key would otherwise stack voices without end.
        if (shots_.size() >= 8) shots_.erase(shots_.begin());
        const int id = nextShot_++;
        shots_.push_back({std::vector<float>(s.begin(), s.end()), 0, id});
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

    struct Shot { std::vector<float> pcm; std::size_t pos; int id; };
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
            vkWin = SDL_CreateWindow("Omikron - scene viewer (vulkan)",
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
    if (!direct && !front.open(640, 480, "Omikron - scene viewer")) {
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
    if (argc < 3) {
        std::fprintf(stderr,
            "usage: omk-play <gamedata> <tables dir> [screen] [--frames N] "
            "[--dump out.bin] [--keys DIK,DIK,...]\n"
            "       omk-play <gamedata> <tables dir> --scene <set> [--cam N] "
            "[--eye x,y,z] [--at x,y,z] [--fov F] [--full]\n");
        return 2;
    }
    const std::string fr = argv[1], tb = argv[2];
    int screenId = 29, frames = 0;
    bool playMovies = true;
    std::string dump, typeText;
    // THE DISPLAY. The interface is authored at 640x480 and its coordinates
    // are scaled by `I2D_ScaleX/Y` (`v * w / 640`, `v * h / 480`), so a bigger
    // display spreads the same layout without enlarging the glyphs. 800x600 is
    // the mode a reader's own screenshot of the original is in.
    int dispW = 800, dispH = 600;
    std::vector<int> scripted;
    int keyEvery = 2;
    // ADVENTURE MODE, headless: `--hold k200*120,k203*30` is a replayable
    // input stream fed AFTER the hand-over - DIK scancodes HELD for that many
    // frames (`+` joins several), `0*n` holds nothing - the same syntax
    // `tools/player_probe.cpp` takes, so a walk can be reproduced without a
    // person at the keys. `--snaps DIR` writes the framebuffer as raw LE
    // RGB565 every 30 frames from the hand-over on (`snap-<frame>.bin`,
    // 640x480 after the display size), which is how the walk was LOOKED at.
    std::string holdStream, snapsDir;
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
    if (!w.valid()) { std::fprintf(stderr, "cannot load the widget tree\n"); return 1; }
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
    if (!opcodes.valid()) { std::fprintf(stderr, "no VM opcode table\n"); return 1; }
    omk::GameState state = omk::GameState::fromFile(fr + "/IAM/START");
    omk::Session session(fr + "/IAM", state, opcodes);
    session.loadAnnounceMap(tb + "/vm_announce.json");
    session.answerUiFromPerson(true);
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
    const int startArea = state.currentArea();
    if (startArea < 0) { std::fprintf(stderr, "IAM/START names no area\n"); return 1; }
    session.loadArea(startArea);
    std::printf("session: area %d loaded, waiting for its script\n", startArea);
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
    // constructed once a script has actually asked for a screen.
    std::unique_ptr<omk::UiWalk> walk;
    int openScreen = -1, conversations = 0, lastArea = -1;
    int replySel = 0;            // which reply the player is on
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
    bool  editFromKnown = false;              // ...and it was captured for the travel
    float editFromEye[3] = {0, 0, 0}, editFromAt[3] = {0, 0, 0}, editFromFov = 75.0f;
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
    omk::TriangleSoup playerSoup;
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
    float playerFeet = 0.0f;
    bool  playerFeetKnown = false;
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
            vkWin = SDL_CreateWindow("Omikron - the replica (vulkan)",
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
    if (!vkRen && !front.open(dispW, dispH, "Omikron - the replica")) {
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
        float progBase[3] = {0, 0, 0};
        bool  progPelvis = false;
        const char* src = "none";
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
    long stagedEver = 0;                 // for the summary line
    std::vector<int> stagedIds;
    // The pool is rebuilt on a COMPOSITION change, not on a size change: two
    // models with the same texture count swapping is exactly what a size test
    // cannot see.
    std::uint64_t poolComposition = 1, poolBuiltFor = 0, poolTold = 0;
    bool poolHasSprites = false, poolHasPlayer = false;
    std::size_t playerTexBase = 0, spriteTexBase = 0;
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
    }

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
        w.soup = omk::collisionSoup(d, omk::SoupKind::Walkable);
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
        for (int slot = 0; slot < 2; ++slot) {
            WorldSlot& w = worldSlots[static_cast<std::size_t>(slot)];
            worldTexBase[slot] = worldTex.size();
            if (w.stem.empty()) continue;
            worldTex.insert(worldTex.end(), w.tex.begin(), w.tex.end());
            worldDecors.push_back({w.area, &w.soup});
            playerSoup.insert(playerSoup.end(), w.soup.begin(), w.soup.end());
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
        if (adventure && !holds.empty()) {
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
        if (adventure) in.setRepeatMask(walk ? omk::kUiRepeatMask : 0);
        const std::uint32_t bits = in.frame(st);

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
            session.setFrameSeconds(dt);
            frameSec = dt;
        }

        // ---- one frame of the GAME -------------------------------------
        //
        // The script runs unless a screen is up. That is the engine: a script
        // parked at `ui.open` is waiting on a person, and `Game_HandleEvent`
        // case 5 is the only thing that releases it.
        if (!walk) session.frame();

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
            const bool wantAdventure = followCam && !playerDriven && beatsOver &&
                session.playerPlaced() && !session.dialogOpen() && !walk &&
                !sc.activeEditing() && feetSetLoaded;
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
            adventure = player && !playerDriven && !session.dialogOpen() && !walk &&
                        !sc.activeEditing();
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
                if (session.playerAnimHeld()) {
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
                // `Actor_TickNpc`: `Actor_ApplyMotion`, then `Actor_ScanZones`
                // at the position it left - the Session's scan reads this on
                // its next frame (wave B, T15). Facing in the +420 degrees.
                session.setPlayerPosition(player->pos(), player->facing());
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
                replySel = 0; menuShown = false;
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
            if (bits & omk::kUiConfirm) {
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
                       (bits & (omk::kUiUp | omk::kUiDown))) {
                // Step to the next AVAILABLE reply, the way `Ui_MoveSelection`
                // steps over an unselectable row.
                const int n = static_cast<int>(dlg.replies().size());
                const int dir = (bits & omk::kUiDown) ? 1 : -1;
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

        // A script asked for a screen: open it. Which screen is the SESSION's
        // answer, not this file's.
        if (!walk && session.pendingUiScreen() >= 0) {
            openScreen = session.pendingUiScreen();
            walk = std::make_unique<omk::UiWalk>(w);
            if (!walk->open(openScreen)) {
                std::fprintf(stderr, "screen %d has no panel in the tree\n", openScreen);
                return 1;
            }
            std::printf("screen %d is asking - arrows move, ENTER confirms\n",
                        openScreen);
            // The screen's own sounds, by slot. Which slot is which is
            // `sub_482FE0`'s answer - it dispatches on the INPUT BIT - not a
            // guess from the file names. Nothing plays when a screen opens:
            // the engine has no such slot, and the one this used to play was
            // the MOVE sound fired at the wrong moment.
            sndMove    = loadSlot(openScreen, omk::UiWidgets::kSoundMove);
            sndConfirm = loadSlot(openScreen, omk::UiWidgets::kSoundConfirm);
            sndBack    = loadSlot(openScreen, omk::UiWidgets::kSoundBack);
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
            if (walk->answer() >= 0) {
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
            std::printf("frame %ld: editing %d '%s' takes the camera (mode 13): object %d '%s', "
                        "%u frames, travel %.0f%s  [roll not drawn: RCamera has none]\n",
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
        const bool drawWorld = !walk && worldReady && anyWorld &&
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
                view.cam.w = dispW; view.cam.h = dispH;
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
                view.cam.w = dispW; view.cam.h = dispH;
            } else if (!haveDlgCam) {
                // A relative point is `subjectPos - R(yaw) * offset`, which is
                // what `sub_415D10`/`sub_415E60` do; an absolute one is passed
                // through. `resolveCamera` handles both per point, because the
                // engine decides per point and 959 of the 1443 relative
                // cameras are relative in ONE of their two.
                const omk::ResolvedCamera rc = omk::resolveCamera(
                    *wc, session.playerPos(), session.playerYaw());
                for (int k = 0; k < 3; ++k) {
                    view.cam.eye[k] = rc.eye[k];
                    view.cam.at[k]  = rc.at[k];
                }
                view.cam.hfovDeg = wc->fov > 1.0f ? wc->fov : 75.0f;
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
            const bool wantSprites = session.scene().effects().count() && !spriteTex.empty();
            if (poolBuiltFor != poolComposition || poolHasSprites != wantSprites ||
                poolHasPlayer != drawPlayer) {
                pool = worldTex;
                for (auto& cm : charModels) {
                    cm.second.texBase = pool.size();
                    pool.insert(pool.end(), cm.second.tex.begin(), cm.second.tex.end());
                }
                playerTexBase = pool.size();
                if (drawPlayer) pool.insert(pool.end(), playerTex.begin(), playerTex.end());
                spriteTexBase = pool.size();
                if (wantSprites) pool.insert(pool.end(), spriteTex.begin(), spriteTex.end());
                poolSize = pool.size();
                poolBuiltFor = poolComposition;
                poolHasSprites = wantSprites;
                poolHasPlayer = drawPlayer;
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
                    sceneFrame = sc.programClock(prog);
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
                omk::applyPose(s.posed, s.mo->rest, s.mo->meshes, pose, &s.mo->face, &fv);
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
                for (auto& c : s.posed.corners) {
                    if (spin) {
                        const float in[3] = {c.x, c.y, c.z};
                        float r[3];
                        omk::rotateYaw(s.facing, in, r);
                        c.x = r[0]; c.y = r[1]; c.z = r[2];
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
                if (!playerFeetKnown) {
                    playerFeet = -1e9f;
                    for (const auto& c : playerPosed.corners)
                        if (c.y > playerFeet) playerFeet = c.y;
                    playerFeetKnown = true;
                }
                const float* pp = player->pos();
                const float yaw = player->facing();
                for (auto& c : playerPosed.corners) {
                    const float in[3] = {c.x, c.y, c.z};
                    float r[3];
                    omk::rotateYaw(yaw, in, r);
                    c.x = r[0] + pp[0];
                    c.y = r[1] + pp[1] - playerFeet;
                    c.z = r[2] + pp[2];
                }
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
            if (session.scene().effects().count() && !spriteTex.empty()) {
                omk::particleGeometry(fxGeo, session.scene().effects(),
                                      view.cam.eye, view.cam.at, spriteFr);
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
            if (drawPlayer)
                for (const auto& b : playerPosed.batches)
                    draws.push_back({keyOf(b.blend, b.cutout, static_cast<std::uint32_t>(
                                               b.material + static_cast<int>(playerTexBase))),
                                     &playerPosed, b.start, b.count, b.blend, b.cutout});
            if (spriteBase >= 0)
                for (const auto& b : fxGeo.batches)
                    draws.push_back({keyOf(b.blend, b.cutout,
                                           static_cast<std::uint32_t>(spriteBase + b.material)),
                                     &fxGeo, b.start, b.count, b.blend, b.cutout});
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
        if (walk) { comp.setFrame(n); comp.draw(fb, openScreen, *walk); }

        // A `media.play` line, while `Subtitle_Show`'s timer runs: inset 16,
        // against the bottom, white. A conversation's own text takes over.
        if (!session.dialogOpen() && !walk && mediaTextFrames > 0) {
            drawSubtitle(fb, lay, mediaText, {}, -1, dispW, dispH, 16);
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
            drawSubtitle(fb, lay,
                         dlg.phase() == omk::DialogPhase::Menu
                             ? std::string() : dlg.lineText(),
                         menu, sel, dispW, dispH);
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
                    ".CTL state %d '%s' clip %s frame %.1f, walked %.1f over %ld ticks\n",
                    playerModel.c_str(), playerCtlName.c_str(), player->pos()[0],
                    player->pos()[1], player->pos()[2], player->facing(),
                    static_cast<int>(player->state()), player->ctlState(),
                    player->ctlStateName().c_str(), player->clipName().c_str(),
                    player->clipFrame(), player->distanceWalked(), player->ticks());
    std::printf("session: %d areas entered, %d ui answers\n",
                session.areasEntered(),
                static_cast<int>(session.uiAnswers().size()));
    front.close();
    return 0;
}
