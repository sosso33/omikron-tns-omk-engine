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
// No audio yet: `openAudio` says no and the game runs silent.
#pragma once

#include "platform/frontend.h"

#include <cstdint>
#include <string>

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

    const void* windowId() const override { return win_; }
    std::string windowTitle() const override;
    void setWindowTitle(const std::string& t) override;

private:
    void blit();                 // the GWorld to the window
    void* win_ = nullptr;        // WindowRef
    void* gw_ = nullptr;         // GWorldPtr, 16 bits
    int gwW_ = 0, gwH_ = 0;
    bool quit_ = false;
};

}  // namespace omk
