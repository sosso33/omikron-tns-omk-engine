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
//   * WHICH GROUP sees which layout. The game's own tables already differ per
//     group (`Input_InstallScheme`), and the Aventure group takes its own
//     (`kAventure`, the reader's of 2026-10-27): the sneak on a face button;
//   * THE TRIGGERS AND THE RIGHT STICK IN SHOOT MODE (the reader's, 2026-11-02):
//     the game knew neither - a 1999 stick had neither. A trigger is its
//     shoulder's twin in every group but shoot mode, where the right one fires
//     and the left is Action, and where the right stick AIMS as the mouse does,
//     through the same sensitivity rows (`StickAim`). A pad with no triggers (a
//     Vita, a 3DS) keeps the older layout: taking the stick's side push would
//     leave it no Action at all;
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
    LeftTrigger = 1u << 15, RightTrigger = 1u << 16,
};

// Button -> DirectInput button index k (code 48 + k, slot 4 + k by default),
// the layout of the swimming and fighting groups; the adventure and shoot
// groups take their own below. The Aventure label of each slot, for
// orientation. SELECT / BACK is in none of them: it is OMK's original/
// remaster switch in every group (`switchPressed`), and button 9 - slot 13,
// which swimming and fighting leave unbound - is TRIANGLE's where it is bound.
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
    {LeftTrigger,    6},
    {RightTrigger,   7},
};

// THE AVENTURE GROUP'S LAYOUT (group 0 - also what the screens run under):
// the sneak, which only this group binds, on TRIANGLE / Y (North) - button
// 9, also the UI's close, so the same button shuts it - and `Vue premiere
// personne`, which North carried, on SQUARE / X (West), whose button 2 this
// group leaves unbound.
inline constexpr Map kAventure[] = {
    {South,          0},   // slot 4   Action / Utiliser
    {East,           1},   // slot 5   Annuler / Sauter
    {West,           3},   // slot 7   Vue premiere personne
    {North,          9},   // slot 13  Ouvrir sneak          (the UI's close)
    {RightStickSide, 4},
    {RightStickUp,   5},
    {LeftShoulder,   6},   // slot 10  Pas de cote
    {RightShoulder,  7},   // slot 11  Courir
    {RightStickDown, 8},
    {LeftTrigger,    6},
    {RightTrigger,   7},
};
// THE SHOOT GROUP'S (group 2, `Shoot_Enter`'s): `Arme` on TRIANGLE / Y -
// button 9, where SELECT carried it - whose button 3 this group leaves
// unbound; the rest as `kButtons`.
inline constexpr int kShootGroup = 2;
inline constexpr Map kShoot[] = {
    {South,          0},   // slot 4   Tir
    {East,           1},   // slot 5   Sauter
    {West,           2},   // slot 6   S'accroupir
    {North,          9},   // slot 13  Arme
    {RightStickSide, 4},   // slot 8   Action / Utiliser
    {RightStickUp,   5},   // slot 9   Regarder En-Haut
    {LeftShoulder,   6},   // slot 10  Glisser a gauche
    {RightShoulder,  7},   // slot 11  Glisser a droite
    {RightStickDown, 8},   // slot 12  Regarder En-Bas
    {RightTrigger,   0},   // slot 4   Tir
    {LeftTrigger,    4},   // slot 8   Action / Utiliser
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
// A trigger counts as pressed past this, of its 0..1000.
inline constexpr int kTriggerPress = 500;

struct Pad {
    std::uint32_t buttons = 0;    // `Button` bits
    // Both sticks in -1000..1000, +x right, +y DOWN (DirectInput's sense and
    // SDL's).
    int lx = 0, ly = 0, rx = 0, ry = 0;
    // Has the pad triggers at all? Said by the frontend, not guessed from the
    // bits: it decides who carries the shoot group's Action (`stickAims`).
    bool hasTriggers = false;
};

// Does the right stick aim, rather than press three buttons, under this group?
inline bool stickAims(const Pad& p, int group) { return group == kShootGroup && p.hasTriggers; }

// SELECT / BACK as THE ORIGINAL/REMASTER SWITCH (`backends/remaster/
// playremaster_mode.cpp`, F10's twin), in every group - the sneak and `Arme`,
// which it carried, moved to TRIANGLE (the reader's, 2026-10-27). Not a
// binding: the frontend reads it, as it reads START for ESCAPE.
inline bool switchPressed(const Pad& p) { return (p.buttons & Back) != 0; }

// Fold the right stick into its three button bits.
inline std::uint32_t withRightStick(const Pad& p) {
    std::uint32_t b = p.buttons;
    if (p.ry < -kRightStickPress) b |= RightStickUp;
    if (p.ry >  kRightStickPress) b |= RightStickDown;
    if (p.rx < -kRightStickPress || p.rx > kRightStickPress) b |= RightStickSide;
    return b;
}

// One frame of the pad -> the engine's joystick under the installed GROUP
// (`Input::group`): button codes into `joystick`, the axes into `joyX`/`joyY`
// (the d-pad drives them fully). Adds to `joystick`; never clears what the
// caller put in. START is not here (see `kEscape`), nor SELECT (the switch,
// `switchPressed`).
inline void toDevices(const Pad& p, DeviceState& st, int group) {
    const std::uint32_t b = stickAims(p, group) ? p.buttons : withRightStick(p);
    const auto feed = [&](const auto& layout) {
        for (const auto& m : layout)
            if (b & m.button) st.joystick.push_back(48 + m.index);
    };
    if (group == 0) feed(kAventure);
    else if (group == kShootGroup) feed(kShoot);
    else feed(kButtons);
    const auto dz = [](int v) { return (v > -kDeadZone && v < kDeadZone) ? 0 : v; };
    int x = dz(p.lx), y = dz(p.ly);
    if (b & DpadLeft)  x = -1000;
    if (b & DpadRight) x =  1000;
    if (b & DpadUp)    y = -1000;
    if (b & DpadDown)  y =  1000;
    st.joyX = x;
    st.joyY = y;
}

// THE RIGHT STICK AS THE MOUSE (`stickAims`): its motion in the mouse's own
// units, which `sub_45D1D0` then turns by the player's sensitivity rows and the
// inverted row, as for a mouse. Full deflection is the keys' own step per 30 Hz
// tick - `MDRG`/`MDRD` turn 50 units, `MDLUP`/`MDLDO` look 25. The push is
// squared past the dead zone, a CHOICE: a thumb cannot hold a small angle on a
// linear stick. The aim block rounds to whole units, so the fraction is carried
// here, or a slow push would never move at all.
inline constexpr float kAimYawUnits = 50.0f, kAimPitchUnits = 25.0f;

struct StickAim {
    float carryX = 0.0f, carryY = 0.0f;

    void reset() { carryX = carryY = 0.0f; }
    // `ticks` is the frame's length in 30 Hz ticks. Adds to `dx` / `dy`.
    void step(const Pad& p, float ticks, float& dx, float& dy) {
        const auto curve = [](int v) {
            const int m = v < 0 ? -v : v;
            if (m < kDeadZone) return 0.0f;
            const float n = static_cast<float>(m - kDeadZone) / static_cast<float>(1000 - kDeadZone);
            return (v < 0 ? -n : n) * n;
        };
        carryX += curve(p.rx) * kAimYawUnits * ticks;
        carryY += curve(p.ry) * kAimPitchUnits * ticks;
        const float wx = static_cast<float>(static_cast<int>(carryX));
        const float wy = static_cast<float>(static_cast<int>(carryY));
        dx += wx; carryX -= wx;
        dy += wy; carryY -= wy;
    }
};

// THE STICK UNDER A SCREEN: ONE DIRECTION, WITH HYSTERESIS. A CHOICE, like
// the dead zone, and a frontend's, not the engine's: a 1999 DirectInput
// stick through `Input_Poll` sets a bit on each side of 0 per axis, and the
// interface's 0x203F mask turns a HELD bit into one press. An analog thumb
// does not hold a bit steady: pushed "down" it drifts 0.3-0.4 sideways, and
// a component hovering at the 250 dead zone turns its bit on and off frame
// after frame - each return a fresh edge, so a held stick keeps moving the
// selection (seen on the Quest). Under a screen only
// the DOMINANT axis counts, entered past `kMenuPress` and held until its
// magnitude falls under `kMenuRelease`; the d-pad's +-1000 passes unchanged.
// The world keeps both axes - a diagonal walk is two bits there.
inline constexpr int kMenuPress = 500;
inline constexpr int kMenuRelease = 300;

struct MenuStick {
    int dir = 0;   // 0 none, 1 left, 2 right, 3 up, 4 down (+y is DOWN)

    void reset() { dir = 0; }
    void apply(int& x, int& y) {
        const int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
        // the held direction survives until its own axis lets go
        const int held = (dir == 1 || dir == 2) ? ax : (dir ? ay : 0);
        if (dir && held < kMenuRelease) dir = 0;
        if (!dir) {
            if (ax >= ay && ax >= kMenuPress) dir = x < 0 ? 1 : 2;
            else if (ay > ax && ay >= kMenuPress) dir = y < 0 ? 3 : 4;
        }
        x = dir == 1 ? -1000 : dir == 2 ? 1000 : 0;
        y = dir == 3 ? -1000 : dir == 4 ? 1000 : 0;
    }
};

}  // namespace omk::pad
