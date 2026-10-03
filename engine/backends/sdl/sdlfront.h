// SPDX-License-Identifier: GPL-3.0-or-later
// THE SDL FRONTEND - the window, the keyboard, the pad, the audio device.
//
// Moved out of `play.cpp` by `todo/play-split.md` S1c (2026-10-02) without a
// change: the class's declarations stay here, every body of more than one line
// is in `sdlfront.cpp`, beside the comment that explains it. `backends/sdl/`
// is the only place in the tree that includes an SDL header (PORTING A8
// rule 2, one dependency per backend).
#pragma once

#if defined(OMK_SDL3)
#  include <SDL3/SDL.h>
#else
#  include <SDL.h>
#endif

#include "platform/frontend.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace omk {

// The host frontend AS SDL's, for the SDL-side files that need what is not
// the gateway's - the GPU glue's window flags and window hand-over. Valid
// because `makeHostFrontend` here makes an `SdlFrontend`; game code never
// calls it (it does not include this header).
class SdlFrontend;
SdlFrontend& sdlFrontend(Frontend& f);

// OPTIONS ROW 2's LIST - the host's display modes; see `sdlfront.cpp`.
int optionDisplayModes(int curW, int curH, std::vector<std::string>& names);

class SdlFrontend : public omk::Frontend {
public:
    void setFullscreen(bool on) override;
    bool fullscreen() const override { return fullscreen_; }
    // The flags a GPU backend's window is created with (fullscreen or not) -
    // for the GPU glue, which makes its own window; not part of the gateway.
    std::uint32_t windowFlags() const;
    // A GPU backend's window, which this frontend did not create but whose
    // fullscreen toggle and resize it still serves. A Vulkan one remakes its
    // swapchain when it sees the window's extent change.
    void attachWindow(SDL_Window* w) { gpuWin_ = w; }
    SDL_Window* active() const { return win_ ? win_ : gpuWin_; }

    bool resize(int w, int h) override;

    bool open(int w, int h, const std::string& title) override;

    bool pump(omk::HostInput& out) override;

    void readPad(omk::HostInput& out);

    void setRelativeMouse(bool on) override;

    void present(const omk::Surface& fb) override;

    static void feed(void* user, Uint8* out, int len);

    bool openAudio(int rate, int channels) override;

    bool reopenAudio(int rate, int channels) override;

    void setMusicGain(float g) override;

    void queueAudio(std::span<const float> s) override;

    int playSound(std::span<const float> s, bool loop = false,
                  float gain = 1.0f) override;

    int playSound(std::vector<float>&& s, bool loop = false,
                  float gain = 1.0f) override;

    int playSound(std::shared_ptr<const std::vector<float>> s, bool loop = false,
                  float gain = 1.0f) override;

    void stopSound(int handle) override;

    void flushAudio() override;

    double queuedSeconds() override;

    // the gateway's clocks and window (platform/frontend.h)
    std::uint32_t ticksMs() override;
    std::uint64_t perfCounter() override;
    std::uint64_t perfFrequency() override;
    void delayMs(std::uint32_t ms) override;
    int displayModes(int curW, int curH, std::vector<std::string>& names) override {
        return optionDisplayModes(curW, curH, names);
    }
    const void* windowId() const override { return active(); }
    std::string windowTitle() const override;
    void setWindowTitle(const std::string& t) override;
    void startTextInput() override;
    std::string lastError() const override;

    void close() override;

private:
    void logical();
    SDL_Window*   win_ = nullptr;
    SDL_Window*   gpuWin_ = nullptr;   // a GPU backend's window (attachWindow)
    bool          fullscreen_ = false;
    SDL_Renderer* ren_ = nullptr;
    SDL_Texture*  tex_ = nullptr;
#if defined(OMK_SDL3)
    SDL_AudioStream* astream_ = nullptr;
#else
    SDL_AudioDeviceID adev_ = 0;
#endif
    int w_ = 0, h_ = 0;
    int arate_ = 0, achan_ = 2;
    bool droppedToldOnce_ = false;   // queueAudio's one line with no device
    bool padInit_ = false;           // the controller subsystem, started lazily
#if defined(OMK_SDL3)
    SDL_Gamepad* pad_ = nullptr;
#else
    SDL_GameController* pad_ = nullptr;
#endif

    // omk-play 72: a LOOPING shot wraps instead of ending. `Script_PlaySound`
    // carries a loop flag the port recorded and never honoured, so an ambience
    // was re-fired by its program every cycle - wav 23 started 33 times in 521
    // frames, a 1.76 s sample overlapping itself three deep. That restart is
    // what a reader heard as "the loop feels unnatural".
    struct Shot { std::shared_ptr<const std::vector<float>> pcm; std::size_t pos; int id;
                  bool loop = false; float gain = 1.0f; };
    int                 nextShot_ = 1;
    // SDL's own mutex, not `std::mutex`: on the Vita the standard library's
    // threading sits on the SDK's pthread layer, and the engine keeps to the
    // platform's own calls there (`todo/vita-port.md`); SDL's is built on
    // them. The audio callback runs on SDL's thread, so every touch of the
    // mixer's state below takes it.
#if defined(OMK_SDL3)
    SDL_Mutex*          amx_ = SDL_CreateMutex();
#else
    SDL_mutex*          amx_ = SDL_CreateMutex();
#endif
    float musicGain_ = 1.0f;      // Music_SetVolume, applied to the stream in feed()
    std::vector<float>  stream_;
    std::size_t         sHead_ = 0;
    std::vector<Shot>   shots_;
};

}  // namespace omk
