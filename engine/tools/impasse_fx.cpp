// SPDX-License-Identifier: GPL-3.0-or-later
// What the port produces for the IMPASSE cutscene's effects: the standing set
// pieces the set brings up, the rows an object START fires (`sub_451470`), and
// the particles alive as the sixteen beats play. The originals are the blue
// portal ring with its dark core, a yellow star flare above it, the green
// soul-drain spray and the red-filter shot's smoke.
//
//     impasse_fx <gamedata> <vm_opcodes.json> <START> <SCPTDATA> <out.bin>
#include "script/area.h"
#include "platform/datafs.h"
#include <cstdio>
#include <fstream>
#include <map>
#include <string>

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr,
            "usage: impasse_fx <gamedata> <vm_opcodes.json> <START> <SCPTDATA> <out.bin>\n");
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
    std::printf("area %d  scx '%s'  sfx rows: standing %d\n",
                s.currentArea(), s.scene().file().c_str(),
                s.scene().standingPieces());
    s.sceneLoad(222, 55);                       // the Impasse cutscene
    std::printf("after scene.load(222,55): programs %zu running %d, standing %d\n",
                s.scene().programCount(), s.scene().programsRunning(),
                s.scene().standingPieces());
    int maxParticles = 0, maxEmitters = 0, framesWithFx = 0;
    std::map<int, int> firedAt;
    for (int f = 0; f < 900; ++f) {
        s.frame();
        const int n = static_cast<int>(s.scene().effects().count());
        const int e = static_cast<int>(s.scene().effects().emitterCount());
        if (n > maxParticles) maxParticles = n;
        if (e > maxEmitters) maxEmitters = e;
        if (n > 0) ++framesWithFx;
        if (f % 100 == 0)
            std::printf("  f%-4d running %2d  emitters %3d  particles %4d  fired %d\n",
                        f, s.scene().programsRunning(), e, n, s.scene().piecesFired());
    }
    std::printf("peak: emitters %d, particles %d; frames with any particle %d/900; "
                "pieces fired %d\n",
                maxEmitters, maxParticles, framesWithFx, s.scene().piecesFired());
    const std::int32_t out[4] = {s.scene().piecesFired(), maxParticles,
                                 framesWithFx, maxEmitters};
    std::ofstream of(argv[5], std::ios::binary);
    of.write(reinterpret_cast<const char*>(out), sizeof out);
    return 0;
}
