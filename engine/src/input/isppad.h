// SPDX-License-Identifier: GPL-3.0-or-later
// A PAD THROUGH INPUTSPROCKET, in platform-free terms - the choices of Mac OS 9's
// gamepad (`backends/classic/classicpad.cpp` is the Toolbox half). No Mac header
// here, so `tools/pad_layout.cpp` checks it on any host.
//
// InputSprocket does not number a pad's controls the way SDL's mapping does: a
// driver hands over a list of ELEMENTS, each with a kind (button, direction pad,
// axis) and a label. A label says what it is when the driver knows; most buttons
// on a generic pad are labelled `none`, and then the list's order is all there is.
//   * buttons: `strt`/`paus` are START and `optn` is SELECT by label; every other
//     takes the next free of the four face buttons, the shoulders, then SELECT
//     and START, as a 1999 driver lists them. A CHOICE, to be play-tested;
//   * axes: by label (`xaxi` `yaxi` the left stick; `rxax` `zaxi` and `ryax`
//     `rzax` the right); a `none` axis takes the next free of the four;
//   * the first direction pad is the d-pad, and any other pad is ignored.
// No pad here has triggers (`Pad::hasTriggers`): the header's vocabulary names
// none, so the right stick keeps its three buttons in shoot mode.
#pragma once

#include "input/pad.h"

#include <cstdint>

namespace omk::pad::isp {

constexpr std::uint32_t fourcc(char a, char b, char c, char d) {
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(a)) << 24) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 8) |
            static_cast<std::uint32_t>(static_cast<unsigned char>(d));
}

inline constexpr std::uint32_t kKindButton = fourcc('b', 'u', 't', 'n');
inline constexpr std::uint32_t kKindDPad   = fourcc('d', 'p', 'a', 'd');
inline constexpr std::uint32_t kKindAxis   = fourcc('a', 'x', 'i', 's');
inline constexpr std::uint32_t kLabelNone  = fourcc('n', 'o', 'n', 'e');

// `kISpAxisMiddle` / `kISpAxisMaximum`.
inline constexpr std::uint32_t kAxisMiddle = 0x7FFFFFFFu;
// Which way is up on `yaxi`? The header does not say, and no pad could be tried
// where this was written (2026-11-03): if a pad walks backwards, this is the line.
inline constexpr bool kYMaximumIsUp = true;

enum class Role : std::uint8_t { None, Button, DPad, LX, LY, RX, RY };

struct Element {
    Role role = Role::None;
    std::uint32_t button = 0;   // the `Button` bit, for `Role::Button`
};

// Elements in the driver's order, one `add` each; one per device.
class Assigner {
public:
    Element add(std::uint32_t kind, std::uint32_t label) {
        Element e;
        if (kind == kKindButton) {
            std::uint32_t bit = 0;
            if (label == fourcc('s', 't', 'r', 't') || label == fourcc('p', 'a', 'u', 's')) bit = Start;
            else if (label == fourcc('o', 'p', 't', 'n')) bit = Back;
            else for (std::uint32_t b : kOrder) if (!(used_ & b)) { bit = b; break; }
            if (bit && !(used_ & bit)) { used_ |= bit; e.role = Role::Button; e.button = bit; }
        } else if (kind == kKindDPad) {
            if (!dpad_) { dpad_ = true; e.role = Role::DPad; }
        } else if (kind == kKindAxis) {
            Role r = Role::None;
            if (label == fourcc('x', 'a', 'x', 'i')) r = Role::LX;
            else if (label == fourcc('y', 'a', 'x', 'i')) r = Role::LY;
            else if (label == fourcc('r', 'x', 'a', 'x') || label == fourcc('z', 'a', 'x', 'i')) r = Role::RX;
            else if (label == fourcc('r', 'y', 'a', 'x') || label == fourcc('r', 'z', 'a', 'x')) r = Role::RY;
            else if (label == kLabelNone) {
                for (Role c : {Role::LX, Role::LY, Role::RX, Role::RY})
                    if (!(axes_ & bitOf(c))) { r = c; break; }
            }
            // an axis the pad has twice, or a throttle: not ours
            if (r != Role::None && !(axes_ & bitOf(r))) { axes_ |= bitOf(r); e.role = r; }
        }
        return e;
    }

private:
    static constexpr std::uint32_t kOrder[] = {South, East, West, North, LeftShoulder, RightShoulder, Back, Start};
    static constexpr unsigned bitOf(Role r) { return 1u << static_cast<unsigned>(r); }
    std::uint32_t used_ = 0;
    unsigned axes_ = 0;
    bool dpad_ = false;
};

// `kISpAxis*`'s 0..0xFFFFFFFF -> -1000..1000.
inline int axisValue(std::uint32_t state) {
    const long long v = (static_cast<long long>(state) - kAxisMiddle) * 1000 / kAxisMiddle;
    return v > 1000 ? 1000 : v < -1000 ? -1000 : static_cast<int>(v);
}

// One element's state (`ISpElement_GetSimpleState`) into the pad. Several devices
// land on one pad: the frontend `merge`s them.
inline void apply(const Element& e, std::uint32_t state, Pad& p) {
    switch (e.role) {
    case Role::None: break;
    case Role::Button: if (state != 0) p.buttons |= e.button; break;   // kISpButtonDown
    case Role::DPad:   // kISpPadIdle 0, Left 1, UpLeft 2, Up 3, UpRight 4, Right 5, DownRight 6, Down 7, DownLeft 8
        if (state == 1 || state == 2 || state == 8) p.buttons |= DpadLeft;
        if (state >= 4 && state <= 6)               p.buttons |= DpadRight;
        if (state >= 2 && state <= 4)               p.buttons |= DpadUp;
        if (state >= 6 && state <= 8)               p.buttons |= DpadDown;
        break;
    case Role::LX: p.lx = axisValue(state); break;
    case Role::LY: p.ly = kYMaximumIsUp ? -axisValue(state) : axisValue(state); break;
    case Role::RX: p.rx = axisValue(state); break;
    case Role::RY: p.ry = kYMaximumIsUp ? -axisValue(state) : axisValue(state); break;
    }
}

}  // namespace omk::pad::isp
