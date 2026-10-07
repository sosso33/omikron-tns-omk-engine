// SPDX-License-Identifier: GPL-3.0-or-later
// THE STICK UNDER A SCREEN (`pad::MenuStick`, input/pad.h): a held thumb is
// one press. Each case is a run of stick frames fed through `pad::toDevices`
// and the engine's own edge filter at `Ui_BeginScreen`'s 0x203F, once raw
// and once through `MenuStick`, and the presses of the four direction slots
// are counted.
//
//     menu_stick <tables/key_bindings.json>
//
// One line a case: `case <name> raw L R U D menu L R U D`.
// `verify.py: engine: menu stick` asserts every count. Prints only.
#include "input/bindings.h"
#include "input/pad.h"

#include <cstdio>
#include <functional>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: menu_stick <key_bindings.json>\n");
        return 2;
    }
    auto schemes = omk::ControlSchemes::loadJson(argv[1]);
    if (!schemes.valid()) { std::fprintf(stderr, "no schemes\n"); return 1; }

    struct Case {
        const char* name;
        int frames;
        std::function<void(int, omk::pad::Pad&)> at;   // frame -> the stick
    };
    // a deterministic wobble: -1, 0, +1, 0, ... scaled
    const auto wob = [](int f) { return (f % 4 == 0) ? -1 : (f % 4 == 2) ? 1 : 0; };
    const Case cases[] = {
        // pushed down and held, the thumb drifting 0.2-0.26 to the right:
        // the x component crosses the 250 dead zone every few frames
        {"held-down-drift", 90, [&](int f, omk::pad::Pad& p) { p.ly = 950; p.lx = 230 + 30 * wob(f); }},
        {"held-down-wobble", 90, [&](int f, omk::pad::Pad& p) {
             p.ly = 950; p.lx = 275 + 75 * wob(f); }},   // 0.2-0.35
        // a partial push hovering about the dead zone on its own axis
        {"hover-at-dead-zone", 90, [&](int f, omk::pad::Pad& p) { p.ly = 250 + 30 * wob(f); }},
        // pushed past the press, then sagging about the press line itself
        {"push-then-sag", 90, [&](int f, omk::pad::Pad& p) { p.ly = f < 10 ? 900 : 500 + 60 * wob(f); }},
        // held down on a diagonal, the sideways part well past the press
        {"held-down-diagonal", 90, [&](int, omk::pad::Pad& p) { p.ly = 900; p.lx = 600; }},
        // three separate pushes up, released to centre between them
        {"three-pushes-up", 90, [&](int f, omk::pad::Pad& p) { p.ly = (f % 30) < 15 ? -900 : 0; }},
        // the d-pad held right
        {"dpad-right", 90, [&](int, omk::pad::Pad& p) { p.buttons = omk::pad::DpadRight; }},
        // left, then rolled round to up without returning to centre
        {"left-roll-to-up", 90, [&](int f, omk::pad::Pad& p) {
             if (f < 30) { p.lx = -900; } else if (f < 45) { p.lx = -600; p.ly = -600; }
             else { p.ly = -900; } }},
    };
    for (const auto& c : cases) {
        int counts[2][4] = {};
        for (int pass = 0; pass < 2; ++pass) {
            omk::Input in(schemes);
            in.installScheme(0);
            in.setRepeatMask(omk::kUiRepeatMask);
            omk::pad::MenuStick ms;
            for (int f = 0; f < c.frames; ++f) {
                omk::pad::Pad p;
                c.at(f, p);
                omk::DeviceState st;
                omk::pad::toDevices(p, st);
                if (pass == 1) ms.apply(st.joyX, st.joyY);
                const std::uint32_t e = in.frame(st);
                // slots 0..3: turn left, turn right, forward, back
                if (e & 1u) ++counts[pass][0];
                if (e & 2u) ++counts[pass][1];
                if (e & 4u) ++counts[pass][2];
                if (e & 8u) ++counts[pass][3];
            }
        }
        std::printf("case %-18s raw %d %d %d %d menu %d %d %d %d\n", c.name,
                    counts[0][0], counts[0][1], counts[0][2], counts[0][3],
                    counts[1][0], counts[1][1], counts[1][2], counts[1][3]);
    }
    return 0;
}
