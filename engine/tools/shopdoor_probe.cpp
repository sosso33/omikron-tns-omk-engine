// SPDX-License-Identifier: GPL-3.0-or-later
// DOES THE SHOP'S OWN DOOR OPEN? - the exit half of an interior transition.
//
//     shopdoor_probe <gamedata> <tables> [area] [objectId] [mesh]
//
// A reader walked into the drugstore by the security centre and could not get
// out again: "doors stay closed", and the same in Qalisar's temple. The way in
// works because the doors that open are the CITY's (`anekbah.SCX`'s own
// objects); the way out asks the interior's `.SCX` for its copy, and every
// `Script_MoveObjectOnPath` in those interiors carries a param 1 of -65536.
// The engine reads that parameter as a `uint16_t` (0x0046F400: `v4 =
// (uint16_t)Script_GetParamInt((int)a2, 1)`), so it is file 0; the port passed
// it whole, matched no path, and the leaves never moved - keeping their
// collision across the doorway.
//
// So this starts the door object the area's own `area.goto` names and reports
// how far each leaf actually travels. A door that does not move prints 0.
#include "platform/datafs.h"
#include "script/area.h"
#include "script/gamestate.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: shopdoor_probe <gamedata> <tables>"
                             " [area] [objectId]\n");
        return 2;
    }
    const std::string fr = argv[1], tb = argv[2];
    const int area   = argc > 3 ? std::atoi(argv[3]) : 248;
    const int object = argc > 4 ? std::atoi(argv[4]) : 7;
    const auto table = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    if (!table.valid()) { std::fprintf(stderr, "no opcode table\n"); return 1; }
    const std::string iam = fr + "/IAM";
    auto state = omk::GameState::fromFile(iam + "/START");
    omk::Session s(iam, state, table);
    s.loadAnnounceMap(tb + "/vm_announce.json");

    s.loadArea(area);
    s.loadScene(fr + "/SCPTDATA", omk::ChunkKind::Area, area);
    for (int f = 0; f < 5; ++f) s.frame();

    // `area.goto <area> <departure> <arrival>` names the door objects by the
    // scene id the op-58 field carries, which is what `handle` resolves.
    omk::Call c;
    c.op = 58;
    c.fields = {static_cast<std::int16_t>(object), 0, 0};
    const int prog = s.sceneMutable().handle({c});
    std::printf("area %d object %d -> program %d\n", area, object, prog);
    if (prog < 0) return 1;

    // Per mesh: the first sample seen, the last, and the straight-line
    // distance between them. A leaf that never moves gives 0.
    std::map<std::string, std::pair<std::array<float, 3>, std::array<float, 3>>> seen;
    int samples = 0;
    for (int f = 0; f < 120; ++f) {
        s.frame();
        for (const auto& mo : s.scene().motions()) {
            if (!mo.placed) continue;
            ++samples;
            const std::array<float, 3> at{mo.pos[0], mo.pos[1], mo.pos[2]};
            auto it = seen.find(mo.name);
            if (it == seen.end()) seen.emplace(mo.name, std::make_pair(at, at));
            else it->second.second = at;
        }
    }
    std::printf("samples %d meshes %zu\n", samples, seen.size());
    for (const auto& kv : seen) {
        const auto& a = kv.second.first;
        const auto& b = kv.second.second;
        const double d = std::sqrt((b[0] - a[0]) * (b[0] - a[0]) +
                                   (b[1] - a[1]) * (b[1] - a[1]) +
                                   (b[2] - a[2]) * (b[2] - a[2]));
        std::printf("%-14s travel %7.1f  from %8.1f %8.1f %8.1f  to %8.1f %8.1f %8.1f\n",
                    kv.first.c_str(), d, a[0], a[1], a[2], b[0], b[1], b[2]);
    }
    return 0;
}
