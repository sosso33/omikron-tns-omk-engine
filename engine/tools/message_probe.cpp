// SPDX-License-Identifier: GPL-3.0-or-later
// A MESSAGE AND WHAT ITS HANDLER DOES TO THE ZONES (`todo/drift-audit.md` S8).
//
//     message_probe <gamedata> <tables> <area> <message> <sender> <zone>
//
// The area loaded from `IAM\START` with `zone` enabled, the message posted
// through `Session::postMessage` from `sender` - a CHARACTERS id, as
// `Message_RunHandlers` hands the handlers - and a few frames run. One line:
//
//   message <n> sender <s> handled <0|1> zone <z> live <before> -> <after>
//
// AREA 144's message 2 is the case it was written for: sent by character 403,
// the X-Tech sentinel, when a bolt hits it and it lives (`sub_423EF0`), its
// handler retires zone 2349 'ztech sentinelle attaque'.
#include "formats/iam.h"
#include "script/area.h"
#include "script/gamestate.h"
#include "script/script.h"
#include "script/zones.h"

#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
    if (argc < 7) {
        std::fprintf(stderr, "usage: message_probe <gamedata> <tables> <area> <message> "
                             "<sender> <zone>\n");
        return 2;
    }
    const std::string fr = argv[1], tb = argv[2];
    const int area = std::atoi(argv[3]), message = std::atoi(argv[4]);
    const int sender = std::atoi(argv[5]), zone = std::atoi(argv[6]);
    const auto table = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    if (!table.valid()) return 1;
    const std::string iam = fr + "/IAM";
    auto state = omk::GameState::fromFile(iam + "/START");
    state.setBit(omk::StateArray::ZoneState, zone, 1);
    omk::Session s(iam, state, table);
    s.loadArea(area);
    for (int k = 0; k < 2; ++k) {
        if (s.residentSlot(k).areaCtx >= 0) s.freeContext(s.residentSlot(k).areaCtx);
        if (s.residentSlot(k).sceneCtx >= 0) s.freeContext(s.residentSlot(k).sceneCtx);
    }
    const auto live = [&]() {
        for (const auto& z : s.zones().registered())
            if (z.zone.id == zone) return 1;
        return 0;
    };
    s.frame();
    const int before = live();
    const bool handled = s.postMessage(message, sender);
    for (int f = 0; f < 5; ++f) s.frame();
    std::printf("message %d sender %d handled %d zone %d live %d -> %d\n",
                message, sender, handled ? 1 : 0, zone, before, live());
    return 0;
}
