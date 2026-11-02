// SPDX-License-Identifier: GPL-3.0-or-later
// THE SDL FRONTEND'S PADS, with no pad: SDL's virtual joysticks plugged into the
// real `SdlFrontend::pump` - the probe behind `verify.py: engine: pads`. One
// line a step, read as it is printed. `pad_probe` runs in a directory it may
// write its `gamecontrollerdb.txt` into.
#include "input/pad.h"
#include "sdlfront.h"

#include <cstdio>
#include <string>

namespace {

#if defined(OMK_SDL3)
using Joy = SDL_Joystick*;
Joy attach() {
    SDL_VirtualJoystickDesc d;
    SDL_INIT_INTERFACE(&d);
    d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    d.naxes = SDL_GAMEPAD_AXIS_COUNT;
    d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    d.name = "virtual";
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&d);
    return id ? SDL_OpenJoystick(id) : nullptr;
}
void detach(Joy j) { const SDL_JoystickID id = SDL_GetJoystickID(j); SDL_CloseJoystick(j); SDL_DetachVirtualJoystick(id); }
void button(Joy j, int b, bool on) { SDL_SetJoystickVirtualButton(j, b, on); }
void axis(Joy j, int a, int v) { SDL_SetJoystickVirtualAxis(j, a, static_cast<Sint16>(v)); }
constexpr int kSouth = SDL_GAMEPAD_BUTTON_SOUTH, kEast = SDL_GAMEPAD_BUTTON_EAST;
constexpr int kLeftX = SDL_GAMEPAD_AXIS_LEFTX, kRightTrigger = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
#else
using Joy = SDL_Joystick*;
Joy attach() {
    const int i = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX,
                                            SDL_CONTROLLER_BUTTON_MAX, 0);
    return i >= 0 ? SDL_JoystickOpen(i) : nullptr;
}
void detach(Joy j) {
    const SDL_JoystickID id = SDL_JoystickInstanceID(j);
    SDL_JoystickClose(j);
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_JoystickGetDeviceInstanceID(i) == id) { SDL_JoystickDetachVirtual(i); break; }
}
void button(Joy j, int b, bool on) { SDL_JoystickSetVirtualButton(j, b, on ? 1 : 0); }
void axis(Joy j, int a, int v) { SDL_JoystickSetVirtualAxis(j, a, static_cast<Sint16>(v)); }
constexpr int kSouth = SDL_CONTROLLER_BUTTON_A, kEast = SDL_CONTROLLER_BUTTON_B;
constexpr int kLeftX = SDL_CONTROLLER_AXIS_LEFTX, kRightTrigger = SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
#endif

constexpr int kRescan = 31;   // past `kPadRescanPumps`

}  // namespace

int main() {
    // a mapping file for this platform, for `loadPadMappings`
    if (std::FILE* f = std::fopen("gamecontrollerdb.txt", "wb")) {
        std::fprintf(f, "03000000ffffffffffffffffffffffff,OMK probe,a:b0,b:b1,platform:%s,\n", SDL_GetPlatform());
        std::fclose(f);
    }
    omk::SdlFrontend front;
    if (!front.open(320, 240, "pad probe")) { std::printf("no video\n"); return 1; }
    omk::HostInput h;
    const auto pump = [&](int n) { for (int i = 0; i < n; ++i) front.pump(h); };
    const auto show = [&](const char* what) {
        std::printf("%s buttons %u lx %d triggers %d\n", what, h.pad.buttons, h.pad.lx, h.pad.hasTriggers ? 1 : 0);
        std::fflush(stdout);
    };
    pump(2);
    show("none");

    Joy a = attach();
    if (!a) { std::printf("no virtual joysticks\n"); return 0; }
    pump(kRescan);
    show("one idle");
    button(a, kSouth, true);
    pump(2);
    show("one south");
    button(a, kSouth, false);

    // ---- the second pad: pressed on it, with the first lying idle
    Joy b = attach();
    pump(kRescan);
    button(b, kEast, true);
    pump(2);
    show("second east");
    button(a, kSouth, true);
    pump(2);
    show("both");
    button(a, kSouth, false);
    button(b, kEast, false);
    // the furthest stick wins, whichever pad and sign
    axis(a, kLeftX, 16384);
    axis(b, kLeftX, -29491);
    pump(2);
    show("sticks");
    axis(a, kLeftX, 0);
    axis(b, kLeftX, 0);
    // a trigger pressed past its threshold, and not short of it. A virtual
    // joystick's raw axis is the full -32768..32767, which SDL rescales for a
    // trigger to 0..32767: -16384 is a quarter of the way in
    axis(b, kRightTrigger, -16384);
    pump(2);
    show("trigger light");
    axis(b, kRightTrigger, 32767);
    pump(2);
    show("trigger full");
    axis(b, kRightTrigger, -32768);

    // ---- unplugged: the other goes on, and no pad is no pad
    detach(a);
    pump(2);
    button(b, kEast, true);
    pump(2);
    show("first gone");
    detach(b);
    pump(2);
    show("all gone");
    // ---- and back
    Joy c = attach();
    pump(kRescan);
    button(c, kSouth, true);
    pump(2);
    show("plugged again");
    detach(c);
    front.close();
    return 0;
}
