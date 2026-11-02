// SPDX-License-Identifier: GPL-3.0-or-later
// THE PAD'S LAYOUT PER GROUP (`input/pad.h`): each button pressed alone under
// each of the four groups, through `pad::toDevices` and the engine's own
// tables (`Input::poll` after `installScheme`), and the slots it sets named
// by the group's labels; and whether it is OMK's original/remaster switch
// (`pad::switchPressed`).
//
//     pad_layout <tables/key_bindings.json>
//
// One line a button and group: `group <g> <button> slots <s,...> switch <0|1>
// | <label>|...`. `verify.py: engine: pad layout` asserts them. Prints only.
#include "input/bindings.h"
#include "input/pad.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: pad_layout <key_bindings.json>\n");
        return 2;
    }
    auto schemes = omk::ControlSchemes::loadJson(argv[1]);
    if (!schemes.valid()) { std::fprintf(stderr, "no schemes\n"); return 1; }
    using omk::pad::Button;
    const struct { const char* name; Button b; } buttons[] = {
        {"south", omk::pad::South}, {"east", omk::pad::East}, {"west", omk::pad::West},
        {"north", omk::pad::North}, {"lshoulder", omk::pad::LeftShoulder},
        {"rshoulder", omk::pad::RightShoulder}, {"back", omk::pad::Back},
        {"ltrigger", omk::pad::LeftTrigger}, {"rtrigger", omk::pad::RightTrigger},
    };
    for (int g = 0; g < 4; ++g) {
        omk::Input in(schemes);
        in.installScheme(g);
        for (const auto& b : buttons) {
            omk::pad::Pad p;
            p.buttons = b.b;
            omk::DeviceState st;
            omk::pad::toDevices(p, st, g);
            const std::uint32_t w = in.poll(st);
            std::string slots, labels;
            for (int s = 0; s < 14; ++s)
                if (w & (1u << s)) {
                    slots += (slots.empty() ? "" : ",") + std::to_string(s);
                    labels += "|" + schemes.label(g, s);
                }
            std::printf("group %d %s slots %s switch %d %s\n", g, b.name, slots.empty() ? "-" : slots.c_str(),
                        omk::pad::switchPressed(p) ? 1 : 0, labels.c_str());
        }
    }
    // THE RIGHT STICK under Tirer: aiming on a pad with triggers, its three old
    // buttons on one without - and the aim's own arithmetic.
    {
        omk::Input in(schemes);
        in.installScheme(omk::pad::kShootGroup);
        for (const bool triggers : {true, false})
            for (const int rx : {0, 1000}) {
                omk::pad::Pad p;
                p.hasTriggers = triggers;
                p.rx = rx;
                omk::DeviceState st;
                omk::pad::toDevices(p, st, omk::pad::kShootGroup);
                std::printf("stick triggers %d rx %d word %u\n", triggers ? 1 : 0, rx, in.poll(st));
            }
        omk::pad::Pad p;
        p.hasTriggers = true;
        const auto run = [&](int rx, int ry, int ticks) {
            omk::pad::StickAim aim;
            float dx = 0.0f, dy = 0.0f;
            p.rx = rx; p.ry = ry;
            for (int i = 0; i < ticks; ++i) aim.step(p, 1.0f, dx, dy);
            std::printf("aim %d,%d x%d -> %.0f,%.0f\n", rx, ry, ticks, double(dx), double(dy));
        };
        run(1000, 0, 1);       // the keys' 50 units a tick
        run(0, -1000, 1);      // the look keys' 25, up is negative as a mouse's is
        run(200, 0, 30);       // inside the dead zone
        run(400, 0, 30);       // a slow push, whole units from fractions
        run(-625, 625, 10);
    }
    return 0;
}
