// SPDX-License-Identifier: GPL-3.0-or-later
// `Input_Poll`'s OWN RULES, driven one case at a time - the keyboard's three
// fixes and the joystick's hardwired axes (input/bindings.h,
// `keyboardAsPolled` and `DeviceState::joyX`).
//
//     input_poll <tables/key_bindings.json>
//
// One line a case: `case <name> group <g> word <hex>`, the word `Input::poll`
// returns BEFORE the edge filter, so each case is a pure function of what is
// held. `verify.py: engine: input poll` asserts every word. Prints only.
#include "input/bindings.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {
struct Case {
    const char* name;
    int group;
    std::vector<int> keyboard, joystick;
    int joyX = 0, joyY = 0;
};
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: input_poll <key_bindings.json>\n");
        return 2;
    }
    auto schemes = omk::ControlSchemes::loadJson(argv[1]);
    if (!schemes.valid()) { std::fprintf(stderr, "no schemes\n"); return 1; }
    omk::Input input(schemes);
    const std::vector<Case> cases = {
        // the keyboard: Aventure binds RUN to 54 (bit 0x800), the sidestep to
        // 157 (0x400) and the sneak to TAB 15 (0x2000); Nager binds "Plonger"
        // to 157 in slot 5 (0x20)
        {"rshift",       0, {54}, {}},
        {"lshift",       0, {42}, {}},          // mirrored onto 54: runs
        {"rctrl",        0, {157}, {}},
        {"lctrl",        0, {29}, {}},          // sets 157: sidesteps
        {"lctrl-swim",   1, {29}, {}},          // ...and dives
        {"tab",          0, {15}, {}},
        {"alt-tab",      0, {56, 15}, {}},      // TAB dropped under ALT
        // the joystick's axes, hardwired to slots 0..3 at threshold 0
        {"joy-right",    0, {}, {}, 1, 0},
        {"joy-left",     0, {}, {}, -1, 0},
        {"joy-forward",  0, {}, {}, 0, -1},     // DirectInput's Y is down
        {"joy-back",     0, {}, {}, 0, 1000},
        {"joy-rest",     0, {}, {}, 0, 0},
        // the table's axis codes are byte offsets and are never looked up
        {"joycode-0",    0, {}, {0}},
        {"joycode-4",    0, {}, {4}},
        // buttons: code 48 + k presses slot 4 + k in every group
        {"joy-button0",  0, {}, {48}},
        {"joy-button9",  3, {}, {57}},
    };
    for (const auto& c : cases) {
        input.installScheme(c.group);
        omk::DeviceState st;
        st.keyboard = c.keyboard;
        st.joystick = c.joystick;
        st.joyX = c.joyX;
        st.joyY = c.joyY;
        std::printf("case %-12s group %d word %04x\n", c.name, c.group, input.poll(st));
    }
    return 0;
}
