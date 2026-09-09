// SPDX-License-Identifier: GPL-3.0-or-later
// WHAT THE MOUSE DOES, per control scheme (`todo/omk-play.md` 97b).
//
//     mousebit <tables>
//
// The engine's mouse codes come out of `Input_ReadOneControl` (0x0043E360),
// whose mouse arm reads a DirectInput `DIMOUSESTATE` and tests the button
// bytes in order: `& 0x80` -> 12, `& 0x8000` -> 13, `& 0x800000` -> 14. So 12
// is the LEFT button - and the shoot scheme binds `Tir`, the trigger, to it.
//
// This prints the 14-slot input word the left button produces in each of the
// four groups, which is what shows that a frontend may feed the mouse
// UNCONDITIONALLY: the binding tables are what gate a device per context, and
// deciding in the frontend which groups get a mouse would be inventing a
// control scheme the game does not have.
#include "input/bindings.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: mousebit <tables>\n"); return 2; }
    const auto sc = omk::ControlSchemes::loadJson(std::string(argv[1]) + "/key_bindings.json");
    if (!sc.valid()) { std::fprintf(stderr, "no key_bindings.json\n"); return 2; }
    for (int button = 12; button <= 14; ++button) {
        omk::DeviceState st;
        st.mouse.push_back(button);
        for (int g = 0; g < omk::kGroups; ++g) {
            std::uint32_t w = 0;
            for (int slot = 0; slot < omk::kSlots; ++slot) {
                const int c = sc.code(g, slot, omk::Device::Mouse);
                if (c && st.holds(omk::Device::Mouse, c)) w |= (1u << slot);
            }
            std::printf("button %d group %d %-9s word %u\n",
                        button, g, sc.groupName(g).c_str(), w);
        }
    }
    return 0;
}
