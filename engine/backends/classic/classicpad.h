// SPDX-License-Identifier: GPL-3.0-or-later
// A GAMEPAD ON MAC OS 9, through InputSprocket (`input/isppad.h` has the choices).
//
// `InputSprocketLib` is not in Carbon, though the header calls it "compatible with
// Carbon", and it does not exist on Mac OS X. So it is NOT linked: a strong import
// would stop this one binary launching wherever the library is missing. It is
// loaded at run time and every symbol looked up, and with no library there is
// simply no pad. Mac OS X has the SDL build's pads (`ppc-darwin.mk`).
#pragma once

#include "input/isppad.h"

namespace omk::classic {

class IspPads {
public:
    // One frame, into a pad already zeroed. Scans for devices itself.
    void read(pad::Pad& out);
    void close();

private:
    bool load();
    void scan();

    static constexpr int kMaxElements = 64, kMaxDevices = 8;
    struct Live { void* ref; pad::isp::Element e; int device; };
    Live live_[kMaxElements];
    void* devices_[kMaxDevices];
    int elements_ = 0, deviceCount_ = 0;
    bool tried_ = false, loaded_ = false, started_ = false, told_ = false;
    int rescan_ = 0;
    // The library's entry points, each as FindSymbol gave it
    void* fn_[10] = {};
};

}  // namespace omk::classic
