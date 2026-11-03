// SPDX-License-Identifier: GPL-3.0-or-later
#include "classicpad.h"

#include <CodeFragments.h>
#include <InputSprocket.h>

#include <cstdio>

namespace omk::classic {
namespace {

// Rescanned while no pad is there, about every two seconds of frames: a USB pad
// plugged in after launch. Once one is found, the list stands.
constexpr int kRescanPumps = 60;

enum Fn { kStartup, kShutdown, kExtract, kActivate, kDeactivate, kDefinition, kElementList, kListExtract, kInfo, kSimple, kCount };
const unsigned char* const kNames[kCount] = {
    reinterpret_cast<const unsigned char*>("\pISpStartup"),
    reinterpret_cast<const unsigned char*>("\pISpShutdown"),
    reinterpret_cast<const unsigned char*>("\pISpDevices_Extract"),
    reinterpret_cast<const unsigned char*>("\pISpDevices_Activate"),
    reinterpret_cast<const unsigned char*>("\pISpDevices_Deactivate"),
    reinterpret_cast<const unsigned char*>("\pISpDevice_GetDefinition"),
    reinterpret_cast<const unsigned char*>("\pISpDevice_GetElementList"),
    reinterpret_cast<const unsigned char*>("\pISpElementList_Extract"),
    reinterpret_cast<const unsigned char*>("\pISpElement_GetInfo"),
    reinterpret_cast<const unsigned char*>("\pISpElement_GetSimpleState"),
};

}  // namespace

bool IspPads::load() {
    if (tried_) return loaded_;
    tried_ = true;
    CFragConnectionID conn = nullptr;
    Ptr mainAddr = nullptr;
    Str255 err;
    if (GetSharedLibrary(reinterpret_cast<ConstStr63Param>("\pInputSprocketLib"), kPowerPCCFragArch, kLoadCFrag,
                         &conn, &mainAddr, err) != noErr) {
        std::printf("pad: no InputSprocketLib - no gamepad on this system\n");
        return false;
    }
    for (int i = 0; i < kCount; ++i) {
        Ptr sym = nullptr;
        CFragSymbolClass cls = 0;
        if (FindSymbol(conn, kNames[i], &sym, &cls) != noErr) {
            std::printf("pad: InputSprocketLib lacks %.*s - no gamepad\n", kNames[i][0],
                        reinterpret_cast<const char*>(kNames[i]) + 1);
            return false;
        }
        fn_[i] = sym;
    }
    if (reinterpret_cast<decltype(&ISpStartup)>(fn_[kStartup])() != noErr) {
        std::printf("pad: ISpStartup refused - no gamepad\n");
        return false;
    }
    started_ = true;
    loaded_ = true;
    return true;
}

void IspPads::scan() {
    ISpDeviceReference found[32];
    UInt32 n = 0;
    if (reinterpret_cast<decltype(&ISpDevices_Extract)>(fn_[kExtract])(32, &n, found) != noErr) {
        if (!told_) { told_ = true; std::printf("pad: ISpDevices_Extract refused - no gamepad\n"); }
        return;
    }
    // The first scan says what InputSprocket lists, so that a pad not read can be
    // told from a pad not seen: a keyboard and a mouse are always there
    if (!told_) {
        told_ = true;
        std::printf("pad: InputSprocket lists %lu devices\n", static_cast<unsigned long>(n));
    }
    for (UInt32 i = 0; i < n && i < 32 && deviceCount_ < kMaxDevices; ++i) {
        ISpDeviceDefinition def;
        if (reinterpret_cast<decltype(&ISpDevice_GetDefinition)>(fn_[kDefinition])(found[i], sizeof def, &def) != noErr)
            continue;
        // NOT the keyboard and the mouse: activating those would take them from
        // `GetKeys` and the Window Manager
        if (def.theDeviceClass != kISpDeviceClass_Joystick && def.theDeviceClass != kISpDeviceClass_Gamepad) continue;
        ISpElementListReference list = nullptr;
        if (reinterpret_cast<decltype(&ISpDevice_GetElementList)>(fn_[kElementList])(found[i], &list) != noErr) continue;
        ISpElementReference els[32];
        UInt32 m = 0;
        if (reinterpret_cast<decltype(&ISpElementList_Extract)>(fn_[kListExtract])(list, 32, &m, els) != noErr) continue;
        pad::isp::Assigner assign;
        int used = 0;
        for (UInt32 k = 0; k < m && k < 32 && elements_ < kMaxElements; ++k) {
            ISpElementInfo info;
            if (reinterpret_cast<decltype(&ISpElement_GetInfo)>(fn_[kInfo])(els[k], &info) != noErr) continue;
            const pad::isp::Element e = assign.add(info.theKind, info.theLabel);
            if (e.role == pad::isp::Role::None) continue;
            live_[elements_++] = {els[k], e, deviceCount_};
            ++used;
        }
        ISpDeviceReference one = found[i];
        if (reinterpret_cast<decltype(&ISpDevices_Activate)>(fn_[kActivate])(1, &one) != noErr) {
            elements_ -= used;
            continue;
        }
        devices_[deviceCount_++] = found[i];
        std::printf("pad: %.*s, %d controls read of %lu\n", def.deviceName[0],
                    reinterpret_cast<const char*>(def.deviceName) + 1, used, static_cast<unsigned long>(m));
    }
}

void IspPads::read(pad::Pad& out) {
    if (!load()) return;
    if (deviceCount_ == 0 && --rescan_ <= 0) {
        rescan_ = kRescanPumps;
        scan();
    }
    // Each device onto its own pad, then merged: two sticks must not overwrite
    // each other, whichever comes later in the list
    pad::Pad one;
    int device = -1;
    for (int i = 0; i < elements_; ++i) {
        if (live_[i].device != device) {
            if (device >= 0) pad::merge(out, one);
            one = pad::Pad{};
            device = live_[i].device;
        }
        UInt32 state = 0;
        if (reinterpret_cast<decltype(&ISpElement_GetSimpleState)>(fn_[kSimple])(
                reinterpret_cast<ISpElementReference>(live_[i].ref), &state) == noErr)
            pad::isp::apply(live_[i].e, state, one);
    }
    if (device >= 0) pad::merge(out, one);
}

void IspPads::close() {
    if (!started_) return;
    if (deviceCount_ > 0)
        reinterpret_cast<decltype(&ISpDevices_Deactivate)>(fn_[kDeactivate])(
            static_cast<UInt32>(deviceCount_), reinterpret_cast<ISpDeviceReference*>(devices_));
    reinterpret_cast<decltype(&ISpShutdown)>(fn_[kShutdown])();
    started_ = false;
    deviceCount_ = elements_ = 0;
}

}  // namespace omk::classic
