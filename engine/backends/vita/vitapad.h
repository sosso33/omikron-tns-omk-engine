// SPDX-License-Identifier: GPL-3.0-or-later
// THE VITA PAD AS THE GAME'S OWN JOYSTICK - `todo/vita-port.md` step F3.
//
// **The engine already has a pad**, and that decides the mapping. Of the three
// device tables `Input_InstallScheme` copies (`input/bindings.h`), the third is
// a JOYSTICK, and its compiled defaults (`tables/key_bindings.json`, the
// `joystick` column) are the SAME in all four control groups:
//
//     slot 0/1   axis 0 - turn left / right         (code 0)
//     slot 2/3   axis 4 - forward / back            (code 4)
//     slot 4+k   button k, k = 0..9                 (code 48 + k)
//
// So a pad needs no per-scheme remapping at all - which the keyboard would:
// shoot mode turns on numpad 4/6 and strafes on the arrows, combat punches on
// A/Z and kicks on Q/S, and a fixed pad-to-key table would be wrong in three
// of the four groups. Feeding the pad in as the joystick is the faithful path,
// and the one this header takes for the BUTTONS.
//
// **The AXES are not ported, and that is a finding, not a gap in this file.**
// `Input::poll` skips code 0 outright (`c != 0 && holds`), and one code serves
// two slots per axis, so the direction must come from the axis's sign - which
// is `Input_ReadOneControl`'s joystick arm, and that arm has not been read.
// Until it is (F3's first half: read it from the listing, port it into
// `input/bindings.*`, check it), the stick and the d-pad go in as KEYBOARD
// codes, chosen per group from the live keyboard table - the codes the group's
// own turn / move slots carry - so every scheme still gets its own directions.
//
// Which Vita button is which joystick button is a CHOICE, and the only one
// here: the game shipped no pad layout. The table below is a proposal to be
// play-tested (`todo/play-test.md` once a build exists), ordered so the four
// face buttons carry the four actions every group uses most.
//
// Header-only and SDK-free: the button bits are the SDK's values copied as
// constants, so this compiles and can be tested on the host.
#pragma once

#include "input/bindings.h"

#include <cstdint>
#include <vector>

namespace omk::vita {

// `SceCtrlButtons`, psp2/ctrl.h - copied, so no SDK header is needed here.
enum : std::uint32_t {
    kSelect   = 0x00000001u,
    kStart    = 0x00000008u,
    kUp       = 0x00000010u,
    kRight    = 0x00000020u,
    kDown     = 0x00000040u,
    kLeft     = 0x00000080u,
    kLTrigger = 0x00000100u,
    kRTrigger = 0x00000200u,
    kTriangle = 0x00001000u,
    kCircle   = 0x00002000u,
    kCross    = 0x00004000u,
    kSquare   = 0x00008000u,
};

// The joystick button a Vita control presses, as the engine's code (48 + k).
// k -> the slot it drives (4 + k), with the Aventure label for orientation:
struct ButtonMap { std::uint32_t mask; int button; };
inline constexpr ButtonMap kButtons[] = {
    {kCross,    0},   // slot 4   Action / Utiliser        (the UI's confirm)
    {kCircle,   1},   // slot 5   Annuler / Sauter         (the UI's back)
    {kSquare,   2},   // slot 6   - / S'accroupir / Coup de pied 1
    {kTriangle, 3},   // slot 7   Vue premiere personne / Coup de pied 2
    {kLTrigger, 6},   // slot 10  Pas de cote / Glisser a gauche
    {kRTrigger, 7},   // slot 11  Courir / Glisser a droite
    {kSelect,   9},   // slot 13  Ouvrir sneak / Arme      (the UI's close)
};
// The right stick carries the three slots a four-button pad runs out for:
// up = button 5 (slot 9, Regarder En-Haut), down = 8 (slot 12, En-Bas), and
// either side = 4 (slot 8, the shoot group's second Action).
inline constexpr int kRightUp = 5, kRightDown = 8, kRightSide = 4;

// START is ESCAPE, not a joystick button: the menu key is refused as a
// binding (`Input::rebind` rejects code 1) and `play.cpp` reads it directly.
inline constexpr int kEscape = 0x01;

// A stick is "pressed" past this far from centre (0..127). The engine's own
// dead zone lives in the unread axis arm; this is a placeholder for it.
inline constexpr int kStickDeadZone = 48;

struct PadState {
    std::uint32_t buttons = 0;
    // 0..255, 128 at rest, as `SceCtrlData` reports them
    std::uint8_t lx = 128, ly = 128, rx = 128, ry = 128;
};

struct PadCodes {
    std::vector<int> keyboard;   // DIK scan codes held this frame
    std::vector<int> joystick;   // joystick codes (48 + k) held this frame
};

// One frame of the pad -> the codes held on the engine's two devices.
// `input` supplies the live tables, so the directions follow the installed
// group (the keyboard half) without this file knowing the groups.
inline PadCodes translate(const PadState& p, const Input& input) {
    PadCodes out;
    for (const auto& b : kButtons)
        if (p.buttons & b.mask) out.joystick.push_back(48 + b.button);
    const int rdx = static_cast<int>(p.rx) - 128, rdy = static_cast<int>(p.ry) - 128;
    if (rdy < -kStickDeadZone) out.joystick.push_back(48 + kRightUp);
    if (rdy >  kStickDeadZone) out.joystick.push_back(48 + kRightDown);
    if (rdx < -kStickDeadZone || rdx > kStickDeadZone) out.joystick.push_back(48 + kRightSide);
    if (p.buttons & kStart) out.keyboard.push_back(kEscape);

    // Directions through the group's OWN keyboard slots 0..3 (turn left,
    // turn right, forward, back) - the stand-in for the unported axis arm.
    const int ldx = static_cast<int>(p.lx) - 128, ldy = static_cast<int>(p.ly) - 128;
    const bool left  = (p.buttons & kLeft)  || ldx < -kStickDeadZone;
    const bool right = (p.buttons & kRight) || ldx >  kStickDeadZone;
    const bool up    = (p.buttons & kUp)    || ldy < -kStickDeadZone;
    const bool down  = (p.buttons & kDown)  || ldy >  kStickDeadZone;
    const auto key = [&](int slot) {
        const int c = input.live(slot, Device::Keyboard);
        if (c != 0) out.keyboard.push_back(c);
    };
    if (left)  key(0);
    if (right) key(1);
    if (up)    key(2);
    if (down)  key(3);
    return out;
}

}  // namespace omk::vita
