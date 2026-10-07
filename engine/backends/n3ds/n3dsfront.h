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
//   * the BOTTOM screen: the instrument panel (`n3dspanel.h`, step 2b) -
//     its numbers measured here, where the frames are presented and the
//     pacer sleeps, and its buttons read from the touch screen.
#pragma once

#include "audio/hostmix.h"
#include "n3dspanel.h"
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
    // THE DIRECT PRESENT (`todo/3ds-port.md` 6.4): this frame's top-screen
    // framebuffer (240 a column, 400 columns, RGB565) for a caller that
    // writes it itself - the straight world, dithered into it in one pass -
    // or null; and `presentWritten` once it has: `present` less its copy
    // (the capture, the frame's stats, the panel, the swap).
    std::uint16_t* topFramebuffer();
    // ...and the RIGHT eye's, when the top screen is in 3D (step 8), else null
    std::uint16_t* topFramebufferRight();
    void presentWritten();

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
    void setSoundGain(int handle, float gain) override { mix_.setGain(handle, gain); }
    double soundPlayedSeconds(int handle) override {
        const long long p = mix_.played(handle);
        return p < 0 || arate_ <= 0 ? -1.0 : static_cast<double>(p) / (arate_ * achan_);
    }
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
    // ---- the instrument panel (step 2b)
    void act(n3ds::Panel::Action a);
    void writeControl(const char* cmd);   // `<capture>.ctl`, as omkprof.py --ctl writes it
    void writeCapture(const Surface& fb);
    void finishPresent(const Surface* fb, std::uint16_t* dst);   // `fb` null: a direct present
    n3ds::Panel panel_;
    Surface panelSurf_{320, 240};
    n3ds::PanelStats stats_;
    bool panelOk_ = false;
    long captureAt_ = -1;                 // `sdmc:/omk/capture-at`: CAPTURE this present (an instrument)
    bool panelDump_ = false;
    // the present's sampling tables, made once per frame size (no division a pixel)
    std::vector<int> colMap_, rowMap_;
    std::uint64_t copyTicks_ = 0;       // the present's copy, since the last panel redraw
    long winCopies_ = 0, reports_ = 0;
    int mapW_ = 0, mapH_ = 0;              // `sdmc:/omk/panel-dump` exists: each redraw also to panel.bin
    bool captureOwed_ = false;
    bool stereo3d_ = false;               // `sdmc:/omk/stereo3d`: the top screen in 3D (step 8)
    bool screenDumpOwed_ = false;         // with a capture: the top screen as written, too
    std::uint64_t lastPresent_ = 0;       // system ticks
    std::uint64_t winStart_ = 0, winSleep_ = 0, winWorst_ = 0;
    long winFrames_ = 0;
    std::uint64_t sleepTicks_ = 0;        // what `delayMs` slept since the last present
    // the pad as last reported, so a CHANGE is logged (what a play report
    // needs, and how a press nobody made is found)
    std::uint32_t lastButtons_ = 0;
    int lastLx_ = 0, lastLy_ = 0, lastRx_ = 0, lastRy_ = 0;
    long pumps_ = 0;
};

// The open frontend, or null - for the 3DS glue's direct present.
N3dsFrontend* liveN3dsFrontend();
}  // namespace omk
