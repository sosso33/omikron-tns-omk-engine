// SPDX-License-Identifier: GPL-3.0-or-later
// The AMBIENT family - fire, smoke, neon, steam - through the LIVE path, the
// way `loadWorldSlot` does it: a Session on an area, then `bindSetEmitters`
// with that area's own `.3DO`. Then ticked, because the question is not
// whether they bind but whether they are still emitting a second later.
//
// `Sfx_TickAmbient` frees an emitter once both its countdowns have run out, so
// a SUSTAINED effect has to be re-registered; `sub_451600` does exactly that
// each frame for every shown set piece. If nothing re-registers the mesh-bound
// ones, they emit once and the set goes dead.
//
//     ambient_live <gamedata> <vm_opcodes.json> <START> <SCPTDATA> <area>
#include "script/area.h"
#include "platform/datafs.h"
#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr, "usage: ambient_live <gamedata> <vm_opcodes.json> <START>"
                             " <SCPTDATA> <area>\n");
        return 2;
    }
    const std::string data = argv[1];
    const auto table = omk::OpcodeTable::loadJson(argv[2]);
    const int area = std::atoi(argv[5]);
    auto state = omk::GameState::fromFile(argv[3]);
    omk::Session s(data + "/IAM", state, table);
    s.loadScene(argv[4], omk::ChunkKind::Area, area);
    s.loadArea(area);
    for (int i = 0; i < 64 && s.loading(); ++i) s.frame();
    const std::string stem = s.setName();
    std::printf("AREA %d: set '%s', scx '%s', sfx valid %d\n",
                area, stem.c_str(), s.scene().file().c_str(),
                (int)s.scene().sfx().valid);
    const omk::DataFs fs(data);
    const auto mp = fs.resolve("MESHES/DECORS/" + stem + ".3DO");
    if (!mp) { std::fprintf(stderr, "no set model for '%s'\n", stem.c_str()); return 1; }
    const auto md = omk::DataFs::readPath(*mp);
    const int bound = s.sceneMutable().bindSetEmitters(md);
    std::printf("bindSetEmitters -> %d ambient emitters\n", bound);
    for (int f = 0; f <= 120; ++f) {
        if (f % 20 == 0)
            std::printf("  f%-4d emitters %3zu  particles %4zu\n",
                        f, s.scene().effects().emitterCount(),
                        s.scene().effects().count());
        s.frame();
    }
    return 0;
}
