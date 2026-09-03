// SPDX-License-Identifier: GPL-3.0-or-later
// THE INPUT PATH, RUN - the four schemes installed, the edge filter applied,
// and the start menu answered BY SCANCODE.
//
//     run_input <tables/key_bindings.json> <tables/ui_widgets.json> <out.bin>
//
// The differential this exists for: `tools/sim` decides the start menu handed
// the input WORDS directly. Here nothing is handed a word. Keys go in as
// scancodes, `Input` looks them up in the live tables `Input_InstallScheme`
// filled, `Game_Frame`'s edge filter turns them into a word, and the ported
// UI walk gets that. Reaching the same answer tests the tables and the filter
// together, which is the whole of the input path this side of the driver.
//
// And the filter is shown to MATTER rather than asserted to: the same three
// keys with the ENTER never released reach a different answer, because the
// second confirm falls inside `repeatMask` and produces no edge at all.
#include "platform/datafs.h"
#include "input/bindings.h"
#include "ui/widgets.h"

#include <cstdio>
#include <fstream>
#include <vector>

using omk::Device;

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: run_input <key_bindings.json> "
                             "<ui_widgets.json> <out.bin>\n");
        return 2;
    }
    const auto s = omk::ControlSchemes::loadJson(argv[1]);
    if (!s.valid()) { std::fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
    const auto w = omk::UiWidgets::loadJson(argv[2]);
    if (!w.valid()) { std::fprintf(stderr, "cannot load %s\n", argv[2]); return 1; }

    std::vector<std::int32_t> out;

    // ---- the initialiser, and its replacement -------------------------
    omk::Input in(s);
    const int initSlot4 = in.live(4, Device::Keyboard);   // 18, E
    const int initSlot5 = in.live(5, Device::Keyboard);   // 19, R
    in.installScheme(0);
    const int liveSlot4 = in.live(4, Device::Keyboard);   // 28, ENTER
    const int liveSlot5 = in.live(5, Device::Keyboard);   // 57, SPACE
    out.insert(out.end(), {initSlot4, initSlot5, liveSlot4, liveSlot5});
    std::printf("live keyboard slots 4/5 before Game_Init's install: %d %d "
                "(E R - the static initialiser)\n", initSlot4, initSlot5);
    std::printf("                             after: %d %d (ENTER SPACE)\n",
                liveSlot4, liveSlot5);

    // ---- every group installs, and each cell is its scheme's -----------
    int cells = 0, wrong = 0, bound = 0;
    for (int g = 0; g < omk::kGroups; ++g) {
        in.installScheme(g);
        for (int a = 0; a < omk::kSlots; ++a)
            for (int d = 0; d < omk::kDevices; ++d) {
                const auto dev = static_cast<Device>(d);
                ++cells;
                if (in.live(a, dev) != s.code(g, a, dev)) ++wrong;
                if (in.live(a, dev) != 0) ++bound;
            }
    }
    out.insert(out.end(), {cells, wrong, bound});
    std::printf("installs: %d cells over four groups, %d disagreeing with the "
                "compiled scheme, %d bound\n", cells, wrong, bound);

    // ---- the edge filter ----------------------------------------------
    // 0x10 (confirm) is inside 0x203F, 0x4000 is outside it - and 0x4000 is
    // outside the fourteen slots too, so it is driven by hand rather than by
    // a key: the point is the filter, not the binding.
    in.installScheme(0);
    in.setRepeatMask(omk::kUiRepeatMask);
    omk::DeviceState hold;
    hold.keyboard.push_back(28);          // ENTER -> slot 4 -> 0x10
    int maskedFires = 0;
    for (int f = 0; f < 3; ++f) if (in.frame(hold) & 0x10) ++maskedFires;
    // the same three frames with the mask cleared, which is what closing the
    // last screen does
    omk::Input in2(s);
    in2.installScheme(0);
    in2.setRepeatMask(0);
    int unmaskedFires = 0;
    for (int f = 0; f < 3; ++f) if (in2.frame(hold) & 0x10) ++unmaskedFires;
    out.insert(out.end(), {maskedFires, unmaskedFires});
    std::printf("ENTER held three frames: %d fires under mask 0x203F, "
                "%d with the mask cleared\n", maskedFires, unmaskedFires);

    // ---- the start menu, answered by scancode -------------------------
    // ENTER, DOWN, ENTER - the same three the simulator sends as words.
    const auto walkByKey = [&](bool release, int& answer, int& words) {
        omk::Input i(s);
        i.installScheme(0);
        i.setRepeatMask(omk::kUiRepeatMask);
        omk::UiWalk u(w);
        answer = -1; words = 0;
        if (!u.open(29)) return;
        const int keys[3] = {28, 208, 28};       // ENTER, DOWN, ENTER
        omk::DeviceState st;
        for (int k = 0; k < 3; ++k) {
            if (release) st.keyboard.clear();   // the previous key came back up
            st.keyboard.push_back(keys[k]);
            const std::uint32_t word = i.frame(st);
            if (word) { u.press(word); ++words; }
            if (k == 0) u.typeName("Kay'l");
        }
        answer = u.answer();
    };
    int aRel = -1, wRel = 0, aHeld = -1, wHeld = 0;
    walkByKey(true, aRel, wRel);
    walkByKey(false, aHeld, wHeld);
    out.insert(out.end(), {aRel, wRel, aHeld, wHeld});
    std::printf("start menu by SCANCODE, keys released: answer %d "
                "(%d words reached the walk)\n", aRel, wRel);
    std::printf("                     nothing released: answer %d "
                "(%d words - the third frame adds no edge)\n", aHeld, wHeld);

    // ---- rebinding, which is group-local -------------------------------
    omk::Input r(s);
    r.installScheme(0);
    const bool refuse0 = r.rebind(0, 2, Device::Keyboard, 0);
    const bool refuse1 = r.rebind(0, 2, Device::Keyboard, 1);
    const bool refuse4 = r.rebind(0, 2, Device::Keyboard, 4);
    // bind Avancer to LEFT, which slot 0 of the same group already holds
    const bool took = r.rebind(0, 2, Device::Keyboard, 203);
    const int slot0After = r.live(0, Device::Keyboard);     // cleared -> 0
    const int slot2After = r.live(2, Device::Keyboard);     // 203
    r.installScheme(3);
    const int combatSlot0 = r.live(0, Device::Keyboard);    // untouched, 203
    out.insert(out.end(), {refuse0 ? 1 : 0, refuse1 ? 1 : 0, refuse4 ? 1 : 0,
                           took ? 1 : 0, slot0After, slot2After, combatSlot0});
    std::printf("rebind: codes 0/1/4 %s; binding Avancer to LEFT clears "
                "Aventure slot 0 (%d) and leaves Combat's alone (%d)\n",
                (!refuse0 && !refuse1 && !refuse4) ? "all refused" : "NOT refused",
                slot0After, combatSlot0);

    if (!omk::safeOutputPath(argv[3])) return 2;
    std::ofstream f(argv[3], std::ios::binary);
    for (std::int32_t v : out) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) { const char c = static_cast<char>(u >> (8 * k)); f.write(&c, 1); }
    }
    return 0;
}
