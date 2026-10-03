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
    void stopSound(int handle) override { mix_.stop(handle); }
    void flushAudio() override { mix_.flush(); }
    double queuedSeconds() override;
    std::string lastError() const override { return lastError_; }

    const void* windowId() const override { return win_; }
    std::string windowTitle() const override;
    void setWindowTitle(const std::string& t) override;

private:
    void blit();                 // the GWorld to the window
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
    void* gw_ = nullptr;         // GWorldPtr, 16 bits
    int gwW_ = 0, gwH_ = 0;
    bool quit_ = false;
};

}  // namespace omk
