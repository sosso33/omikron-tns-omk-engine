// SPDX-License-Identifier: GPL-3.0-or-later
// A GAMEPAD AS THE GAME'S OWN JOYSTICK - `todo/vita-port.md` F2/F3.
//
// **The engine already has a pad, and that decides the mapping.** The third
// device table `Input_InstallScheme` copies is a JOYSTICK (`bindings.h`), and
// `Input_Poll` reads it as DirectInput's `DIJOYSTATE`: the stick's `lX`/`lY`
// are HARDWIRED to slots 0..3 (turn left / right, forward / back), and button
// k presses whatever the live table binds to code 48 + k - by default slot
// 4 + k, in all four groups alike. So a pad needs no per-scheme remapping,
// which the keyboard would (shoot mode turns on numpad 4/6, combat punches on
// Q/W): the pad goes in as the joystick and the engine's own tables do the
// rest, rebinding included.
//
// What is a CHOICE, and the only choices here:
//   * which physical button is DirectInput button k - the game shipped no pad
//     layout, and a 1999 PC stick numbered its buttons however its driver
//     did. The table below puts the four actions every group uses most on the
//     four face buttons, and is a proposal to be play-tested;
//   * the stick's DEAD ZONE. The engine compares the axis against 0 over a
//     range of -1000..1000 and sets none itself - that was the Windows
//     driver's job - so an analog stick, which never rests exactly at centre,
//     needs one supplied here. `kDeadZone` is it.
//
// Platform-free: a frontend (SDL's game controller, the Vita's SceCtrl)
// fills `Pad` in these terms and calls `toDevices`. Header-only.
#pragma once

#include "input/bindings.h"

#include <cstdint>

namespace omk::pad {

// Positional names, as SDL's game controller uses them: SOUTH is the bottom
// face button (Xbox A, PlayStation CROSS).
enum Button : std::uint32_t {
    South = 1u << 0, East = 1u << 1, West = 1u << 2, North = 1u << 3,
    LeftShoulder = 1u << 4, RightShoulder = 1u << 5,
    Back = 1u << 6, Start = 1u << 7,
    DpadUp = 1u << 8, DpadDown = 1u << 9, DpadLeft = 1u << 10, DpadRight = 1u << 11,
    RightStickUp = 1u << 12, RightStickDown = 1u << 13, RightStickSide = 1u << 14,
};

// Button -> DirectInput button index k (code 48 + k, slot 4 + k by default).
// The Aventure label of each slot, for orientation:
struct Map { Button button; int index; };
inline constexpr Map kButtons[] = {
    {South,          0},   // slot 4   Action / Utiliser     (the UI's confirm)
    {East,           1},   // slot 5   Annuler / Sauter      (the UI's back)
    {West,           2},   // slot 6   - / S'accroupir / Coup de pied 1
    {North,          3},   // slot 7   Vue premiere personne / Coup de pied 2
    {RightStickSide, 4},   // slot 8   the shoot group's Action
    {RightStickUp,   5},   // slot 9   Regarder En-Haut
    {LeftShoulder,   6},   // slot 10  Pas de cote / Glisser a gauche
    {RightShoulder,  7},   // slot 11  Courir / Glisser a droite
    {RightStickDown, 8},   // slot 12  Regarder En-Bas
    {Back,           9},   // slot 13  Ouvrir sneak / Arme   (the UI's close)
};

// START is the menu key, ESCAPE on the keyboard: `Input::rebind` refuses code
// 1 as a binding and `play.cpp` reads it straight from `HostInput::held`,
// before any device state exists - so a FRONTEND puts it there, not
// `toDevices`.
inline constexpr int kEscape = 0x01;

// The stick's dead zone, in the engine's -1000..1000. A CHOICE (see above).
inline constexpr int kDeadZone = 250;
// The right stick counts as "pressed" past this, for the three slots it
// carries as buttons.
inline constexpr int kRightStickPress = 500;

struct Pad {
    std::uint32_t buttons = 0;    // `Button` bits
    // Both sticks in -1000..1000, +x right, +y DOWN (DirectInput's sense and
    // SDL's).
    int lx = 0, ly = 0, rx = 0, ry = 0;
};

// Fold the right stick into its three button bits.
inline std::uint32_t withRightStick(const Pad& p) {
    std::uint32_t b = p.buttons;
    if (p.ry < -kRightStickPress) b |= RightStickUp;
    if (p.ry >  kRightStickPress) b |= RightStickDown;
    if (p.rx < -kRightStickPress || p.rx > kRightStickPress) b |= RightStickSide;
    return b;
}

// One frame of the pad -> the engine's joystick: button codes into
// `joystick`, the axes into `joyX`/`joyY` (the d-pad drives them fully).
// Adds to `joystick`; never clears what the caller put in. START is not here
// (see `kEscape`).
inline void toDevices(const Pad& p, DeviceState& st) {
    const std::uint32_t b = withRightStick(p);
    for (const auto& m : kButtons)
        if (b & m.button) st.joystick.push_back(48 + m.index);
    const auto dz = [](int v) { return (v > -kDeadZone && v < kDeadZone) ? 0 : v; };
    int x = dz(p.lx), y = dz(p.ly);
    if (b & DpadLeft)  x = -1000;
    if (b & DpadRight) x =  1000;
    if (b & DpadUp)    y = -1000;
    if (b & DpadDown)  y =  1000;
    st.joyX = x;
    st.joyY = y;
}

}  // namespace omk::pad
