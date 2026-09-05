// SPDX-License-Identifier: GPL-3.0-or-later
//
// List the live particles of a scene's effects at given frames of a cutscene
// - sprite, mode, scale, colour, position, age/life - so a portal that looks
// dimmer than the original can be argued from what is actually emitted.
//
//     fx_dump <gamedata> <vm_opcodes.json> <START> <SCPTDATA> <area> <scene chunk> <frame,frame,...>
#include "script/area.h"
#include "platform/datafs.h"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

int main(int argc, char** argv) {
    if (argc < 8) {
        std::fprintf(stderr, "usage: fx_dump <gamedata> <vm_opcodes.json> <START> <SCPTDATA> <area> <scene chunk> <frames>\n");
        return 2;
    }
    const std::string data = argv[1];
    const auto table = omk::OpcodeTable::loadJson(argv[2]);
    auto state = omk::GameState::fromFile(argv[3]);
    const int area = std::atoi(argv[5]), chunk = std::atoi(argv[6]);
    std::set<int> want;
    { std::string t = argv[7], cur; for (char c : t + ",") { if (c == ',') { if (!cur.empty()) want.insert(std::atoi(cur.c_str())); cur.clear(); } else cur.push_back(c); } }
    omk::Session s(data + "/IAM", state, table);
    s.loadScene(argv[4], omk::ChunkKind::Area, area);
    s.loadArea(area);
    for (int i = 0; i < 64 && s.loading(); ++i) s.frame();
    s.sceneLoad(area, chunk);
    const int last = *want.rbegin();
    for (int f = 0; f <= last; ++f) {
        s.frame();
        if (!want.count(f)) continue;
        const auto& ps = s.scene().effects().particles();
        std::printf("frame %d: %zu particles, %d pieces shown\n", f, ps.size(), s.scene().standingPieces());
        for (const auto& p : ps)
            std::printf("  sprite %3d mode %d scale %5.2f (ramp %+.3f) col %.2f %.2f %.2f alpha %.2f at %7.1f %7.1f %7.1f age %4.1f/%4.1f angle %5.2f\n",
                        p.sprite, p.mode, p.scale, p.dScale, p.col[0], p.col[1], p.col[2], p.alpha,
                        p.pos[0], p.pos[1], p.pos[2], p.age, p.life, p.angle);
    }
    return 0;
}
