// SPDX-License-Identifier: GPL-3.0-or-later
// THE 3DS FRONTEND - the screens, the buttons, the sound and the clocks of a
// Nintendo 3DS, behind the gateway (`platform/frontend.h`), written on libctru
// as `carbonfront.cpp` is on the Toolbox: no SDL (`todo/3ds-port.md` 3 - the
// stereoscopic 3D of step 8 needs libctru's own calls, and SDL's 3DS port
// would bring a 2D renderer this port does not use).
//
// What it does, and no more (A8 rule 3: the frontend shows pixels and reports
// buttons; everything else is the engine's):
//
//   * PRESENT: the composited RGB565 frame onto the TOP screen. The frame is
//     640x480 (A3) and the screen 400x240, so it is FITTED: halved by a 2x2
//     box filter into 320x240 and centred, black either side - 4:3 onto the
//     4:3 middle of a 5:3 screen (the reader's decision of 2026-10-05: the
//     interface stays on the top screen until the per-screen survey, step
//     10). A frame of another size is fitted by nearest sampling. This is
//     presentation, as SDL's stretch of the frame to a window is; it touches
//     no pixel the engine composed beyond that.
//   * the BUTTONS as the engine's own joystick (`input/pad.h`), the circle pad
//     as its stick, START as ESCAPE (the menu key), START + SELECT together to
//     leave;
//   * the SOUND through `ndsp`: one channel and a ring of short 16-bit
//     buffers in linear memory, refilled from `HostMixer` (the same sum every
//     frontend makes) on the MAIN thread, at each pump and each
//     `queuedSeconds`, as the Carbon frontend does;
//   * the CLOCKS from the system tick (`svcGetSystemTick`, 268 MHz on both
//     models - the New 3DS's faster CPU does not change it).
//
// The bottom screen is not this class's: it holds libctru's console (the log,
// `n3ds_main.cpp`) until step 2b makes it the instrument panel.
#pragma once

#include "audio/hostmix.h"
#include "platform/frontend.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace omk {

class N3dsFrontend : public Frontend {
public:
    ~N3dsFrontend() override;
    bool open(int w, int h, const std::string& title) override;
    bool pump(HostInput& out) override;
    void present(const Surface& fb) override;
    void close() override;

    std::uint32_t ticksMs() override;
    std::uint64_t perfCounter() override;
    std::uint64_t perfFrequency() override;
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
    void stopSound(int handle) override { mix_.stop(handle); }
    void flushAudio() override { mix_.flush(); }
    double queuedSeconds() override;
    std::string lastError() const override { return lastError_; }

private:
    void refillAudio();          // every played buffer, mixed again and queued
    void closeAudio();
    HostMixer mix_;
    bool dsp_ = false;           // ndspInit succeeded
    bool dspFailed_ = false;     // ...or failed, and is not retried (once said is enough)
    bool chan_ = false;          // channel 0 set up, buffers allocated
    int arate_ = 0, achan_ = 2;
    // A buffer is 1/20 s, so the ring holds 0.2 s ahead: enough for a frame
    // of 200 ms on the main thread before the sound gaps. A first figure, to
    // be measured on the console with the frame times beside it.
    static constexpr int kBuffers = 4;
    struct Wave;                 // an ndspWaveBuf and its linear-memory samples
    std::unique_ptr<Wave[]> wave_;
    std::size_t waveFrames_ = 0;
    std::vector<float> mixed_;
    bool droppedToldOnce_ = false;
    std::string lastError_;
    bool quit_ = false;
    bool opened_ = false;
    // the pad as last reported, so a CHANGE is logged (what a play report
    // needs, and how a press nobody made is found)
    std::uint32_t lastButtons_ = 0;
    int lastLx_ = 0, lastLy_ = 0, lastRx_ = 0, lastRy_ = 0;
    long pumps_ = 0;
};

}  // namespace omk
