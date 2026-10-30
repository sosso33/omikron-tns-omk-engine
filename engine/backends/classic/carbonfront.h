// SPDX-License-Identifier: GPL-3.0-or-later
// THE CARBON FRONTEND - the window, the keyboard and the clocks of classic Mac
// OS, behind the gateway (`platform/frontend.h`), so `omk-play` runs on Mac
// OS 9 and Mac OS X from one Carbon binary without SDL
// (`todo/classic-mac-port-1999.md` step 5). The only file of the viewer that
// includes a Mac Toolbox header besides the classic entry point.
//
// What a 1999 Mac port would have used, and no more: a window and an
// offscreen GWorld at 16 bits - the Mac's 16-bit format is xRGB 1-5-5-5, so
// the engine's RGB565 frame loses green's low bit on the way, as it would have
// - `CopyBits` to the window, `GetKeys` for the keys held, `WaitNextEvent`
// for typed characters and quitting, and `Microseconds` for the clocks.
// AUDIO through the Sound Manager: one sampled-sound channel and a ring of
// short 16-bit buffers, refilled from `HostMixer` (`audio/hostmix.h`, the
// same sum the SDL frontend makes) on the MAIN thread - the completion
// callback runs at interrupt time on Mac OS 9, so it only marks a buffer
// played and touches nothing else.
//
// OPENGL (the build with `OMK_GL1_AGL`): when the fixed-function
// backend asks (`backends/gl1/gl1host.h`), an AGL context is attached to the
// window, the world is drawn into its back buffer, and from then on every
// frame is PRESENTED through it - `glDrawPixels` of the composited RGB565
// frame, then `aglSwapBuffers` - since on Mac OS X an AGL surface lies over
// the window and would hide what CopyBits draws. Never asked, the window stays
// plain and CopyBits presents.
#pragma once

#include "audio/hostmix.h"
#include "platform/frontend.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace omk {

class CarbonFrontend : public Frontend {
public:
    ~CarbonFrontend() override;
    bool open(int w, int h, const std::string& title) override;
    bool pump(HostInput& out) override;
    void present(const Surface& fb) override;
    void close() override;

    std::uint32_t ticksMs() override;
    std::uint64_t perfCounter() override;
    std::uint64_t perfFrequency() override { return 1000000; }   // Microseconds()
    void delayMs(std::uint32_t ms) override;

    bool openAudio(int rate, int channels) override;
    bool reopenAudio(int rate, int channels) override;
    void queueAudio(std::span<const float> s) override;
    void setMusicGain(float g) override { mix_.setMusicGain(g); }
    int playSound(std::span<const float> s, bool loop = false, float gain = 1.0f) override;
    int playSound(std::vector<float>&& s, bool loop = false, float gain = 1.0f) override;
    int playSound(std::shared_ptr<const std::vector<float>> s, bool loop = false,
                  float gain = 1.0f) override;
    int playSound(std::shared_ptr<const omk::DeviceSound> s, bool loop = false,
                  float gain = 1.0f) override;
    int openSound(int channels, int rate, std::size_t frames, float gain) override {
        return arate_ > 0 ? mix_.open(channels, rate, frames, arate_, gain) : -1;
    }
    void feedSound(int handle, std::span<const std::int16_t> pcm) override { mix_.feed(handle, pcm); }
    void stopSound(int handle) override { mix_.stop(handle); }
    void setSoundGain(int handle, float gain) override { mix_.setGain(handle, gain); }
    double soundPlayedSeconds(int handle) override {
        const long long p = mix_.played(handle);
        return p < 0 || arate_ <= 0 ? -1.0 : static_cast<double>(p) / (arate_ * achan_);
    }
    void flushAudio() override { mix_.flush(); }
    double queuedSeconds() override;
    std::string lastError() const override { return lastError_; }

    const void* windowId() const override { return win_; }
    // The AGL context on the window, made on the first call (`gl1host.h`).
    void* glContext();
    std::string windowTitle() const override;
    void setWindowTitle(const std::string& t) override;
    bool cheapWindowTitle() const override { return false; }

    // THE BOOT FILMS THROUGH QUICKTIME (`Frontend::filmOpen`): the MPEG-1
    // decoded and its sound played by the system. Under GL the picture is
    // drawn into a 32-bit GWorld at the film's own 320x240 and sent to the
    // card in the probe's ARGB layout, the card scaling it to the window;
    // without GL, QuickTime draws into the window itself.
    void setFilmDirect(bool direct) override { filmDirect_ = direct; }
    bool filmOpen(const std::string& hostFile) override;
    bool filmStep() override;
    void filmClose() override;
    volatile bool filmDrawn_ = false;   // set by QuickTime's drawing-complete proc

private:
    void blit();                 // the GWorld to the window
    void presentGL(const Surface& fb);   // the frame through the AGL context
    void refillAudio();          // every played buffer, mixed again and queued
    void closeAudio();
    HostMixer mix_;
    void* chan_ = nullptr;       // SndChannelPtr
    int arate_ = 0, achan_ = 2;
    static constexpr int kBuffers = 6;          // of a tenth of a second each
    std::vector<unsigned char> buf_[kBuffers];  // an ExtSoundHeader, then the samples
    volatile bool played_[kBuffers] = {};       // set by the completion callback
    std::vector<float> mixed_;
    bool droppedToldOnce_ = false;
    long refills_ = 0;           // buffers mixed and queued after the first ring
    std::uint64_t quitAt_ = 0;   // `omk.quit`: the run ends by itself (an instrument)
    std::string lastError_;
    void* win_ = nullptr;        // WindowRef
    void* gw_ = nullptr;         // GWorldPtr, at the WINDOW's depth (16 or 32)
    int gwW_ = 0, gwH_ = 0, gwDepth_ = 0;
    void* agl_ = nullptr;        // AGLContext, once the GL backend asked
    // the frame as 64x64 TILES, a texture each (`presentGL`), row-major
    struct UiTile { unsigned tex = 0; std::vector<std::uint8_t> store; };
    std::vector<UiTile> uiTiles_;
    int uiTilesX_ = 0, uiTilesY_ = 0, uiTileFmt_ = -1;
    long uiStatFrames_ = 0, uiStatTiles_ = 0;   // the `present:` line's counts
    int uiFmt_ = 0;              // its upload format, `probeUploads`' pick (0 RGB565)
    // ...and HOW it is sent, the probe's pick too: Apple's CLIENT STORAGE (the
    // card reads `uiStore_` itself, no copy into the driver), a RECTANGLE
    // texture (640x480 exactly, not 1024x512) and the SHARED storage hint -
    // each only where the driver names the extension
    bool uiClient_ = false, uiRect_ = false, uiHint_ = false;
    std::vector<std::uint8_t> uiConv_;   // a run of rows converted to that format
    void probeUploads();         // times the upload formats once, on the new context
    std::vector<std::uint32_t> uiRowKey_;   // per row and tile column, a sum of what was last sent
    bool argbOk_ = false;        // the probe: the driver takes ARGB8888 (BGRA 8-8-8-8-REV)
    bool film1555_ = false;      // the probe: ARGB1555 sent faster than ARGB8888 (a 16-bit display)
    int filmDepth_ = 32;         // the film GWorld's depth, 16 or 32
    // the film playing (`filmOpen`): a QuickTime Movie, its GWorld under GL
    void* film_ = nullptr;       // Movie
    void* filmGw_ = nullptr;     // GWorldPtr, 32 bits, the film's own size
    int filmW_ = 0, filmH_ = 0;
    unsigned filmTex_ = 0;       // GLuint, power-of-two, made once
    int filmTexW_ = 0, filmTexH_ = 0;
    long filmFrames_ = 0;        // pictures QuickTime drew
    bool filmDirect_ = false;    // `--films-direct`: QuickTime into the window even under GL
    bool filmGL_ = false;        // this film's pictures go through the AGL context (`filmShow`)
    bool filmDetached_ = false;  // the AGL context's drawable set aside for a direct film
    // where a film's time goes, for its close line: QuickTime's `MoviesTask`
    // (its decoding, and its drawing when direct), and `filmShow`'s three parts
    std::uint64_t filmTaskUs_ = 0, filmConvUs_ = 0, filmUpUs_ = 0, filmDrawUs_ = 0;
    long filmSteps_ = 0;
    int qtState_ = 0;            // 0 not asked, 1 QuickTime entered, -1 absent
    void filmShow();             // the GWorld's picture through the AGL context
    bool quit_ = false;
};

}  // namespace omk
