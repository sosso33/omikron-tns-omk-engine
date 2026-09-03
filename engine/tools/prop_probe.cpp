// SPDX-License-Identifier: GPL-3.0-or-later
// The Impasse's rings, through a live Session. `SCENE 55`'s startup script
// runs `object.show 162`, OBJECTS 162 is `3 Anneaux magiques` with stem
// `ANNEAU`, and `MESHES/OBJETS/ANNEAU.3DO` ships.
//
// A prop record is 24 bytes: `+0` i16 slot, `+2` i16 id, `+4/+8/+12` int32
// position, `+16/+18/+20` i16 rotation, `+22` i16 the DB state index. Bit 0
// says the loader loads it at all, bit 1 that it is SHOWN. `Area_Load`
// converts the table in place - position `v*100/256/2.54 - 1`, rotation
// `v*360/4096` - so the rings' stored `(47397, -514, 19614)` is `(7288, -80,
// 3015)`, where the player walks.
//
//     prop_probe <gamedata> <vm_opcodes.json> <START> <SCPTDATA> <out.bin>
#include "script/area.h"
#include "platform/datafs.h"
#include <cstdio>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr, "usage: prop_probe <gamedata> <vm_opcodes.json> <START>"
                             " <SCPTDATA> <out.bin>\n");
        return 2;
    }
    if (!omk::safeOutputPath(argv[5])) return 2;
    const std::string data = argv[1];
    const auto table = omk::OpcodeTable::loadJson(argv[2]);
    auto state = omk::GameState::fromFile(argv[3]);
    omk::Session s(data + "/IAM", state, table);
    s.loadScene(argv[4], omk::ChunkKind::Area, 222);
    s.loadArea(222);
    for (int i = 0; i < 64 && s.loading(); ++i) s.frame();
    const auto before = s.props();
    int shownBefore = 0, ringBefore = 0;
    for (const auto& p : before) { if (p.shown) ++shownBefore; if (p.id == 162 && p.shown) ringBefore = 1; }
    std::printf("AREA 222: %zu props exist, %d shown; the rings shown %d\n",
                before.size(), shownBefore, ringBefore);
    // ...then the Impasse's own script, which ends with `object.show 162`.
    s.sceneLoad(222, 55);
    for (int f = 0; f < 900; ++f) s.frame();
    const auto after = s.props();
    int shownAfter = 0, ringAfter = 0;
    float rx = 0, ry = 0, rz = 0, rot = 0;
    for (const auto& p : after) {
        if (p.shown) ++shownAfter;
        if (p.id == 162) {
            if (p.shown) ringAfter = 1;
            rx = p.pos[0]; ry = p.pos[1]; rz = p.pos[2]; rot = p.rotDeg[0];
        }
    }
    std::printf("after the beats: %zu exist, %d shown; the rings shown %d at "
                "%.1f %.1f %.1f rot %.1f\n",
                after.size(), shownAfter, ringAfter, rx, ry, rz, rot);
    const std::int32_t out[8] = {
        static_cast<std::int32_t>(before.size()), shownBefore, ringBefore,
        static_cast<std::int32_t>(after.size()), shownAfter, ringAfter,
        static_cast<std::int32_t>(rx + 0.5f), static_cast<std::int32_t>(rz + 0.5f)};
    std::ofstream f(argv[5], std::ios::binary);
    f.write(reinterpret_cast<const char*>(out), sizeof out);
    return 0;
}
