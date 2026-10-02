// SPDX-License-Identifier: GPL-3.0-or-later
// THE SDL FRONTEND's bodies - `sdlfront.h` has the class. Moved out of
// `play.cpp` by `todo/play-split.md` S1c (2026-10-02) without a change.
#include "sdlfront.h"

#include "input/pad.h"
#include "ui/surface.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <utility>

namespace omk {
namespace {

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
        // ...and the LEFT control sends its OWN code, DIK_LCONTROL, because
        // the ENGINE maps it: `Input_Poll` sets state[157] whenever state[29]
        // is down (and mirrors the two shifts), now ported as
        // `keyboardAsPolled` (input/bindings.h). DIK_RCONTROL is the binding
        // for "Plonger" (group 1 slot 5, the key that SWIMS) and the adventure
        // sidestep, and a Mac laptop keyboard HAS NO RIGHT CONTROL - a reader
        // could not swim for that reason alone. This row used to send 0x9D
        // and called that the viewer's choice; it was the game's all along
        // (read 2026-09-18), and left SHIFT, which reached no binding, runs.
        {SDL_SCANCODE_LCTRL, 0x1D},
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

// A scope lock over an SDL mutex - `std::lock_guard`'s shape, SDL's lock.
class AudioLock {
public:
#if defined(OMK_SDL3)
    explicit AudioLock(SDL_Mutex* m) : m_(m) { SDL_LockMutex(m_); }
#else
    explicit AudioLock(SDL_mutex* m) : m_(m) { SDL_LockMutex(m_); }
#endif
    ~AudioLock() { SDL_UnlockMutex(m_); }
    AudioLock(const AudioLock&) = delete;
    AudioLock& operator=(const AudioLock&) = delete;
private:
#if defined(OMK_SDL3)
    SDL_Mutex* m_;
#else
    SDL_mutex* m_;
#endif
};

}  // namespace

// OPTIONS ROW 2's LIST - `sub_43AE70`, the DirectDraw mode callback: 16-bit
// modes only, none under 640x480, at most 32 (`sub_43B100(32, ...)`), each
// labelled `"%d x %d x %d bpp"`, and the one the display is in made current.
// The port takes the host's display modes instead of DirectDraw's - and its
// framebuffer IS 16 bits (RGB565, PORTING A3), so the label is true. A size
// the host does not list as a mode (any window) is added, so the row can say
// what is running.
int optionDisplayModes(int curW, int curH, std::vector<std::string>& names) {
    std::vector<std::pair<int, int>> modes;
#if defined(OMK_SDL3)
    int n = 0;
    if (SDL_DisplayMode** ms = SDL_GetFullscreenDisplayModes(SDL_GetPrimaryDisplay(), &n)) {
        for (int i = 0; i < n; ++i) modes.push_back({ms[i]->w, ms[i]->h});
        SDL_free(ms);
    }
#else
    const int n = SDL_GetNumDisplayModes(0);
    for (int i = 0; i < n; ++i) {
        SDL_DisplayMode m{};
        if (SDL_GetDisplayMode(0, i, &m) == 0) modes.push_back({m.w, m.h});
    }
#endif
    modes.push_back({curW, curH});
    std::sort(modes.begin(), modes.end());
    modes.erase(std::unique(modes.begin(), modes.end()), modes.end());
    modes.erase(std::remove_if(modes.begin(), modes.end(), [](const auto& m) {
                    return m.first < 640 || m.second < 480; }), modes.end());
    if (modes.size() > 32) {
        // keep the running size inside the 32
        const auto cur = std::find(modes.begin(), modes.end(), std::make_pair(curW, curH));
        const std::ptrdiff_t at = cur - modes.begin();
        const std::ptrdiff_t lo = std::clamp<std::ptrdiff_t>(at - 16, 0,
                                      static_cast<std::ptrdiff_t>(modes.size()) - 32);
        modes = std::vector<std::pair<int, int>>(modes.begin() + lo, modes.begin() + lo + 32);
    }
    names.clear();
    int current = 0;
    for (std::size_t i = 0; i < modes.size(); ++i) {
        char t[48];
        std::snprintf(t, sizeof t, "%d x %d x %d bpp", modes[i].first, modes[i].second, 16);
        names.push_back(t);
        if (modes[i] == std::make_pair(curW, curH)) current = static_cast<int>(i);
    }
    return current;
}

// ---- FULLSCREEN (`todo/options-menu.md` 1) ---------------------------
//
// The original is FULLSCREEN unless told otherwise: `[Preferences]
// window` is read into `byte_91030B` (0x0040F0ED), absent means 0, and
// the `WINDOW` word on the command line sets it to 1 (`03_win32.c`). It
// took the display mode itself (`DirectDraw` exclusive). This takes the
// DESKTOP's mode instead and scales the framebuffer into it at its own
// aspect - the framebuffer is still the resolution row 2 chose, so what
// is drawn is the same picture either way, only larger.
//
// Asked for BEFORE a window exists, it is a creation flag; after, it is
// a toggle (F11).
void SdlFrontend::setFullscreen(bool on) {
    fullscreen_ = on;
    if (SDL_Window* wnd = active()) {
#if defined(OMK_SDL3)
        SDL_SetWindowFullscreen(wnd, on);
#else
        SDL_SetWindowFullscreen(wnd, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0u);
#endif
        std::printf("fullscreen: %s\n", on ? "on (F11 leaves it)" : "off");
    }
}

// The flags every window of the viewer carries. RESIZABLE because that is
// what macOS wants before it offers the green button's "Enter Full Screen"
// and the Window menu's entry for it - with the Spaces hint below, those
// go through SDL into the same desktop fullscreen F11 asks for. Every
// backend already fits the frame into whatever the window is (the software
// path's logical size, GLES's `destRect`, Vulkan's swapchain remake), so a
// dragged edge rescales the picture and changes no resolution.
std::uint32_t SdlFrontend::windowFlags() const {
    static const bool spaces = [] {
        SDL_SetHint("SDL_VIDEO_MAC_FULLSCREEN_SPACES", "1");
        return true;
    }();
    (void)spaces;
#if defined(OMK_SDL3)
    return SDL_WINDOW_RESIZABLE | (fullscreen_ ? SDL_WINDOW_FULLSCREEN : 0u);
#else
    return SDL_WINDOW_RESIZABLE | (fullscreen_ ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0u);
#endif
}

// THE FRAMEBUFFER'S SIZE CHANGED - options row 2. A window is resized to
// it (fullscreen keeps the desktop and scales), and the software path's
// streaming texture is remade at the new size.
bool SdlFrontend::resize(int w, int h) {
    w_ = w; h_ = h;
    if (SDL_Window* wnd = active(); wnd && !fullscreen_) SDL_SetWindowSize(wnd, w, h);
    if (!ren_) return true;
    if (tex_) SDL_DestroyTexture(tex_);
    tex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGB565,
                             SDL_TEXTUREACCESS_STREAMING, w, h);
    logical();
    return tex_ != nullptr;
}

bool SdlFrontend::open(int w, int h, const std::string& title) {
    w_ = w; h_ = h;
#if defined(OMK_SDL3)
    if (!SDL_Init(SDL_INIT_VIDEO)) return false;
    win_ = SDL_CreateWindow(title.c_str(), w, h, windowFlags());
    if (!win_) return false;
    ren_ = SDL_CreateRenderer(win_, nullptr);
#else
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return false;
    win_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED, w, h, windowFlags());
    if (!win_) return false;
    ren_ = SDL_CreateRenderer(win_, -1, 0);
#endif
    if (!ren_) return false;
    logical();
    // RGB565 all the way to the window: A3 fixes the reference framebuffer
    // at 16 bits, and uploading 565 directly is what keeps what is shown
    // identical to what a capture is diffed against. Asking SDL for 888
    // here would expand every pixel by the HOST's rule, which A3 measured
    // as not being bit replication.
    tex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGB565,
                             SDL_TEXTUREACCESS_STREAMING, w, h);
    return tex_ != nullptr;
}

bool SdlFrontend::pump(omk::HostInput& out) {
    out.text.clear();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
#if defined(OMK_SDL3)
        if (e.type == SDL_EVENT_QUIT) out.quit = true;
        else if (e.type == SDL_EVENT_TEXT_INPUT) out.text += e.text.text;
        else if (e.type == SDL_EVENT_KEY_DOWN) {
            // F11 is the viewer's own fullscreen toggle - no DIK code
            // the game binds, so it reaches no input word
            if (e.key.scancode == SDL_SCANCODE_F11 && !e.key.repeat)
                setFullscreen(!fullscreen_);
            const auto c = charmap().find(static_cast<int>(e.key.scancode));
            if (c != charmap().end()) out.text += c->second;
        }
#else
        if (e.type == SDL_QUIT) out.quit = true;
        else if (e.type == SDL_TEXTINPUT) out.text += e.text.text;
        else if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.scancode == SDL_SCANCODE_F11 && !e.key.repeat)
                setFullscreen(!fullscreen_);
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

    // ---- THE MOUSE (`todo/omk-play.md` 97b) --------------------------
    //
    // The engine's codes come straight out of `Input_ReadOneControl`
    // (0x0043E360), whose mouse arm reads a DirectInput `DIMOUSESTATE`
    // and tests the button bytes in order: `& 0x80` -> **12**,
    // `& 0x8000` -> **13**, `& 0x800000` -> **14**. So 12 is the LEFT
    // button, and the shoot scheme binds `Tir` - the trigger - to it.
    //
    // MOTION IS NOT A BINDING. That function maps motion to codes 0 and 4
    // only on the JOYSTICK arm; the mouse arm reads buttons alone. So
    // mouse look is not part of the 14-slot word at all - it aims the
    // camera directly, which is what a reader who played these phases
    // with a mouse describes.
    out.mouse.clear();
    out.mouseDX = 0; out.mouseDY = 0;
#if defined(OMK_SDL3)
    float mx = 0.0f, my = 0.0f;
    const auto mb = SDL_GetRelativeMouseState(&mx, &my);
    if (mb & SDL_BUTTON_MASK(SDL_BUTTON_LEFT))   out.mouse.insert(12);
    if (mb & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT))  out.mouse.insert(13);
    if (mb & SDL_BUTTON_MASK(SDL_BUTTON_MIDDLE)) out.mouse.insert(14);
    out.mouseDX = mx; out.mouseDY = my;
#else
    int mx = 0, my = 0;
    const Uint32 mb = SDL_GetRelativeMouseState(&mx, &my);
    if (mb & SDL_BUTTON(SDL_BUTTON_LEFT))   out.mouse.insert(12);
    if (mb & SDL_BUTTON(SDL_BUTTON_RIGHT))  out.mouse.insert(13);
    if (mb & SDL_BUTTON(SDL_BUTTON_MIDDLE)) out.mouse.insert(14);
    out.mouseDX = static_cast<float>(mx);
    out.mouseDY = static_cast<float>(my);
#endif
    readPad(out);
    // the green button and the Window menu change the window behind F11's
    // back - take the state from the window, so the next F11 undoes it
    if (SDL_Window* wnd = active()) {
#if defined(OMK_SDL3)
        const bool fs = (SDL_GetWindowFlags(wnd) & SDL_WINDOW_FULLSCREEN) != 0;
#else
        const bool fs = (SDL_GetWindowFlags(wnd) & SDL_WINDOW_FULLSCREEN) != 0;
#endif
        if (fs != fullscreen_) {
            fullscreen_ = fs;
            std::printf("fullscreen: %s (by the window)\n", fs ? "on" : "off");
        }
    }
    return !out.quit;
}

// ---- THE GAMEPAD (`input/pad.h`, `todo/vita-port.md` F2) -----------------
//
// SDL's game controller, in positional terms (SOUTH is Xbox A and the
// PlayStation CROSS - which is also how SDL2 reports the Vita's own
// pad), handed on as `pad::Pad`; `pad::toDevices` then makes it the
// engine's JOYSTICK device. The subsystem is started HERE, lazily, rather
// than in `open`, because a Vulkan window never calls `open`. The first
// pad found is used and a pad plugged in later is picked up.
void SdlFrontend::readPad(omk::HostInput& out) {
    out.pad = {};
    if (!padInit_) {
        padInit_ = true;
#if defined(OMK_SDL3)
        SDL_InitSubSystem(SDL_INIT_GAMEPAD);
#else
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
#endif
    }
#if defined(OMK_SDL3)
    if (!pad_) {
        int n = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&n);
        if (ids && n > 0) pad_ = SDL_OpenGamepad(ids[0]);
        SDL_free(ids);
        if (pad_) std::printf("pad: %s\n", SDL_GetGamepadName(pad_));
    }
    if (!pad_) return;
    if (!SDL_GamepadConnected(pad_)) { SDL_CloseGamepad(pad_); pad_ = nullptr; return; }
    const auto btn = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(pad_, b); };
    const auto axis = [&](SDL_GamepadAxis a) {
        return static_cast<int>(SDL_GetGamepadAxis(pad_, a)) * 1000 / 32767; };
    const std::pair<SDL_GamepadButton, omk::pad::Button> map[] = {
        {SDL_GAMEPAD_BUTTON_SOUTH, omk::pad::South}, {SDL_GAMEPAD_BUTTON_EAST, omk::pad::East},
        {SDL_GAMEPAD_BUTTON_WEST, omk::pad::West}, {SDL_GAMEPAD_BUTTON_NORTH, omk::pad::North},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, omk::pad::LeftShoulder},
        {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, omk::pad::RightShoulder},
        {SDL_GAMEPAD_BUTTON_BACK, omk::pad::Back}, {SDL_GAMEPAD_BUTTON_START, omk::pad::Start},
        {SDL_GAMEPAD_BUTTON_DPAD_UP, omk::pad::DpadUp}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN, omk::pad::DpadDown},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, omk::pad::DpadLeft}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, omk::pad::DpadRight}};
    for (const auto& [b, m] : map) if (btn(b)) out.pad.buttons |= m;
    out.pad.lx = axis(SDL_GAMEPAD_AXIS_LEFTX);  out.pad.ly = axis(SDL_GAMEPAD_AXIS_LEFTY);
    out.pad.rx = axis(SDL_GAMEPAD_AXIS_RIGHTX); out.pad.ry = axis(SDL_GAMEPAD_AXIS_RIGHTY);
#else
    if (!pad_) {
        for (int i = 0; i < SDL_NumJoysticks() && !pad_; ++i)
            if (SDL_IsGameController(i)) pad_ = SDL_GameControllerOpen(i);
        if (pad_) std::printf("pad: %s\n", SDL_GameControllerName(pad_));
    }
    if (!pad_) return;
    if (!SDL_GameControllerGetAttached(pad_)) { SDL_GameControllerClose(pad_); pad_ = nullptr; return; }
    const auto btn = [&](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(pad_, b) != 0; };
    const auto axis = [&](SDL_GameControllerAxis a) {
        return static_cast<int>(SDL_GameControllerGetAxis(pad_, a)) * 1000 / 32767; };
    const std::pair<SDL_GameControllerButton, omk::pad::Button> map[] = {
        {SDL_CONTROLLER_BUTTON_A, omk::pad::South}, {SDL_CONTROLLER_BUTTON_B, omk::pad::East},
        {SDL_CONTROLLER_BUTTON_X, omk::pad::West}, {SDL_CONTROLLER_BUTTON_Y, omk::pad::North},
        {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, omk::pad::LeftShoulder},
        {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, omk::pad::RightShoulder},
        {SDL_CONTROLLER_BUTTON_BACK, omk::pad::Back}, {SDL_CONTROLLER_BUTTON_START, omk::pad::Start},
        {SDL_CONTROLLER_BUTTON_DPAD_UP, omk::pad::DpadUp}, {SDL_CONTROLLER_BUTTON_DPAD_DOWN, omk::pad::DpadDown},
        {SDL_CONTROLLER_BUTTON_DPAD_LEFT, omk::pad::DpadLeft}, {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, omk::pad::DpadRight}};
    for (const auto& [b, m] : map) if (btn(b)) out.pad.buttons |= m;
    out.pad.lx = axis(SDL_CONTROLLER_AXIS_LEFTX);  out.pad.ly = axis(SDL_CONTROLLER_AXIS_LEFTY);
    out.pad.rx = axis(SDL_CONTROLLER_AXIS_RIGHTX); out.pad.ry = axis(SDL_CONTROLLER_AXIS_RIGHTY);
#endif
    // START is the menu key, read straight from `held` (`pad::kEscape`)
    if (out.pad.buttons & omk::pad::Start) out.held.insert(omk::pad::kEscape);
}

// Grab the pointer for first-person aiming, and let it go again. Called
// when shoot mode's own camera comes and goes, not when the mode does -
// the two are not the same thing (issue 97a).
void SdlFrontend::setRelativeMouse(bool on) {
#if defined(OMK_SDL3)
    SDL_SetWindowRelativeMouseMode(win_, on);
#else
    SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
#endif
}

void SdlFrontend::present(const omk::Surface& fb) {
    // One upload of the finished framebuffer. No filtering; in a window
    // the window is the framebuffer's own size and nothing scales, and
    // fullscreen scales it to the desktop at its own aspect (`logical`).
    if (fb.w != w_ || fb.h != h_) resize(fb.w, fb.h);
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
void SdlFrontend::feed(void* user, Uint8* out, int len) {
    auto* self = static_cast<SdlFrontend*>(user);
    auto* dst = reinterpret_cast<float*>(out);
    const std::size_t n = static_cast<std::size_t>(len) / sizeof(float);
    AudioLock lk(self->amx_);
    for (std::size_t i = 0; i < n; ++i) {
        float v = 0.0f;
        if (self->sHead_ < self->stream_.size()) v += self->stream_[self->sHead_++] * self->musicGain_;
        for (auto& one : self->shots_) {
            const std::vector<float>& pcm = *one.pcm;
            if (one.pos >= pcm.size()) {
                if (!one.loop || pcm.empty()) continue;
                one.pos = 0;                  // a looping shot wraps
            }
            v += pcm[one.pos++] * one.gain;
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
        return !o.loop && o.pos >= o.pcm->size();  // a loop ends only on stopSound
    });
}

bool SdlFrontend::openAudio(int rate, int channels) {
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
    // native-order floats, which is what the mixer hands over: AUDIO_F32 is
    // LITTLE-endian in SDL2 (and SDL3's old name maps it to F32LE), so on a
    // big-endian Mac every sample played byte-reversed - noise on PowerPC
    want.format = AUDIO_F32SYS;
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

bool SdlFrontend::reopenAudio(int rate, int channels) {
#if defined(OMK_SDL3)
    const bool open = astream_ != nullptr;
#else
    const bool open = adev_ != 0;
#endif
    if (open && arate_ == rate && achan_ == channels) return true;
    if (open) {
        // CLOSED before anything is dropped: closing waits for the
        // callback, so nothing reads the buffers below while they clear
#if defined(OMK_SDL3)
        SDL_DestroyAudioStream(astream_);
        astream_ = nullptr;
#else
        SDL_CloseAudioDevice(adev_);
        adev_ = 0;
#endif
        AudioLock lk(amx_);
        stream_.clear(); sHead_ = 0;
        shots_.clear();
    }
    return openAudio(rate, channels);
}

void SdlFrontend::setMusicGain(float g) {
    AudioLock lk(amx_);
    musicGain_ = g < 0.0f ? 0.0f : (g > 1.0f ? 1.0f : g);
}

void SdlFrontend::queueAudio(std::span<const float> s) {
    if (s.empty()) return;
    AudioLock lk(amx_);
    // NO DEVICE, NOTHING QUEUED (todo/optimization.md step 5). With no
    // device the callback that consumes the stream never runs and
    // `queuedSeconds` answers 0 for ever, so the music's "keep a second
    // queued" asked for another second EVERY frame and a headless run grew
    // this vector by ~1 MB a frame - 45 MB after 117 frames. Nothing could
    // ever have played it; with a device open this changes nothing.
#if defined(OMK_SDL3)
    const bool open = astream_ != nullptr;
#else
    const bool open = adev_ != 0;
#endif
    if (!open) {
        if (!droppedToldOnce_) {
            droppedToldOnce_ = true;
            std::printf("audio: no device - the stream is dropped, not queued\n");
        }
        return;
    }
    stream_.insert(stream_.end(), s.begin(), s.end());
}

// Every play SHARES its samples (`Shot::pcm`): a caller's own span is
// copied once, BEFORE the lock, a vector is taken, and a shared sample -
// an effect out of the cache - costs the play nothing.
int SdlFrontend::playSound(std::span<const float> s, bool loop,
              float gain) {
    if (s.empty()) return -1;
    return playSound(std::make_shared<const std::vector<float>>(s.begin(), s.end()),
                     loop, gain);
}

int SdlFrontend::playSound(std::vector<float>&& s, bool loop,
              float gain) {
    if (s.empty()) return -1;
    return playSound(std::make_shared<const std::vector<float>>(std::move(s)), loop, gain);
}

int SdlFrontend::playSound(std::shared_ptr<const std::vector<float>> s, bool loop,
              float gain) {
    if (!s || s->empty()) return -1;
    // what the cap pushes out is freed AFTER the lock, not under it
    std::shared_ptr<const std::vector<float>> dropped;
    AudioLock lk(amx_);
    // A cap, because a held key would otherwise stack voices without end.
    if (shots_.size() >= 8) {
        dropped = std::move(shots_.front().pcm);
        shots_.erase(shots_.begin());
    }
    const int id = nextShot_++;
    shots_.push_back({std::move(s), 0, id, loop, gain});
    return id;
}

void SdlFrontend::stopSound(int handle) {
    if (handle < 0) return;
    AudioLock lk(amx_);
    std::erase_if(shots_, [handle](const Shot& o) { return o.id == handle; });
}

void SdlFrontend::flushAudio() {
    AudioLock lk(amx_);
    stream_.clear(); sHead_ = 0;
}

double SdlFrontend::queuedSeconds() {
    AudioLock lk(amx_);
    const double per = arate_ > 0 ? 1.0 / (arate_ * achan_) : 0.0;
    return static_cast<double>(stream_.size() - sHead_) * per;
}

void SdlFrontend::close() {
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

// The framebuffer is the LOGICAL size and SDL fits it into whatever the
// window is, letterboxed - which is a no-op in a window of the same size.
void SdlFrontend::logical() {
    if (!ren_) return;
#if defined(OMK_SDL3)
    SDL_SetRenderLogicalPresentation(ren_, w_, h_, SDL_LOGICAL_PRESENTATION_LETTERBOX);
#else
    SDL_RenderSetLogicalSize(ren_, w_, h_);
#endif
}

}  // namespace omk
