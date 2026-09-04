// SPDX-License-Identifier: GPL-3.0-or-later
//
// THE TUNNEL'S DOORS (todo/omk-play.md 70) — the two resident object pools.
//
// AREA 224 is the tunnel between Anekbah and Qalisar, and each end carries TWO
// transition zones: one naming a door pair and one naming none. The doorless
// one is the OUTER at both ends, so walking through fires it first; by the time
// the door-carrying goto runs, the doorless route has completed and the
// player's area has changed.
//
// The engine keeps TWO resident slots and `Area_LoadScx` (0x0041B4E0) fills THE
// SLOT's object container (`slot+8`), so the tunnel's pool is still there and
// `ScriptObject_Start(obj, a1[3], ...)` - a1[3] being the OUTGOING block -
// finds the door. The port kept ONE pool keyed on the active area, so the door
// resolved to nothing and `startTransitionObject` answered "-2, ends next
// frame": a door asked for and never played, silently, four times in the game.
//
// This runs the two gotos in the order a walk fires them and reports whether
// the door objects resolve. It is the check the fix is judged by, and it FAILS
// on the one-pool build: `door_program` is -2 and `resolved` 0.
//
//     tunnel_doors <gamedata> <tables> <out.bin>
#include "formats/iam.h"
#include "script/area.h"
#include "script/gamestate.h"
#include "script/script.h"
#include "script/world.h"
#include "platform/datafs.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::vector<std::byte> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const auto n = static_cast<std::size_t>(f.tellg());
    std::vector<std::byte> d(n);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(d.data()), static_cast<std::streamsize>(n));
    return d;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: tunnel_doors <gamedata> <tables> <out.bin>\n");
        return 2;
    }
    if (!omk::safeOutputPath(argv[3])) return 2;
    const std::string fr = argv[1], tb = argv[2];
    const auto table = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    if (!table.valid()) return 1;
    const std::string iam = fr + "/IAM";
    const auto areaFile = readFile(iam + "/AREA");
    const auto areas = omk::IamArchive::open(areaFile);

    auto state = omk::GameState::fromFile(iam + "/START");
    omk::Session s(iam, state, table);
    s.answerUiFromPerson(true);
    s.setObjectWait(true);
    s.loadScene(fr + "/SCPTDATA", omk::ChunkKind::Area, 224);
    s.loadArea(224);
    for (int i = 0; i < 64 && s.loading(); ++i) s.frame();

    const auto chunk = areas.chunk(224);
    const auto zones = omk::zonesOf(chunk, omk::ChunkKind::Area);
    // record 3 is the DOORLESS route to Qalisar, record 1 the one carrying
    // doors 5 and 6 - the pair the reader watches for and never sees.
    if (zones.size() < 4) {
        std::fprintf(stderr, "AREA 224: %zu zones, chunk %zu bytes\n",
                     zones.size(), chunk.size());
        return 1;
    }

    const std::string tunnelScx = s.scene().file();
    const int startArea = s.currentArea();

    // ---- 1. the doorless route fires first, and completes
    {
        const auto& z = zones.at(3);
        const int idx = s.newContext(0, chunk, z.scripts, z.id, 0);
        s.queueAction(idx, 1);
        for (int f = 0; f < 3000; ++f) {
            s.frame();
            if (s.transition().state == 0 && s.currentArea() != startArea) break;
        }
    }
    const int afterArea    = s.currentArea();
    const int outArea      = s.sceneOutArea();
    const bool outIsTunnel = s.sceneOut().loaded() && s.sceneOut().file() == tunnelScx;
    const bool activeMoved = s.scene().file() != tunnelScx;

    // ---- 2. now the door-carrying goto, exactly as the inner zone fires it
    int doorProgram = -1, doorF1 = -1;
    bool outPoolUsed = false;
    {
        const auto& z = zones.at(1);
        const int idx = s.newContext(0, chunk, z.scripts, z.id, 0);
        s.queueAction(idx, 1);
        for (int f = 0; f < 600; ++f) {
            s.frame();
            const auto& tr = s.transition();
            if (tr.f1 >= 0 && doorF1 < 0) doorF1 = tr.f1;
            if (tr.program != -1 && doorProgram == -1) {
                doorProgram = tr.program;
                outPoolUsed = tr.outPool;
            }
        }
    }
    const int resolved = doorProgram >= 0 ? 1 : 0;

    std::printf("tunnel scx '%s' start_area %d after_doorless %d\n",
                tunnelScx.c_str(), startArea, afterArea);
    std::printf("outgoing pool: area %d loaded %d is_tunnel %d; active pool moved %d ('%s')\n",
                outArea, s.sceneOut().loaded() ? 1 : 0, outIsTunnel ? 1 : 0,
                activeMoved ? 1 : 0, s.scene().file().c_str());
    std::printf("door f1 %d -> program %d  resolved %d  from_outgoing_pool %d\n",
                doorF1, doorProgram, resolved, outPoolUsed ? 1 : 0);

    std::ofstream o(argv[3], std::ios::binary);
    const int rec[8] = {startArea, afterArea, outArea, outIsTunnel ? 1 : 0,
                        activeMoved ? 1 : 0, doorF1, doorProgram, resolved};
    o.write(reinterpret_cast<const char*>(rec), sizeof rec);
    return 0;
}
