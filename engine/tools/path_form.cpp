// SPDX-License-Identifier: GPL-3.0-or-later
// WHICH ARM OF `Script_MoveObjectOnPath` EACH CALL ASKS FOR - a census.
//
//     path_form <gamedata>                     the whole corpus, one line
//     path_form <gamedata> <Scene.SCX>         every call in one scene, named
//     path_form <gamedata> <tables> --run      RUN the flat's greeting and
//                                              report where each moved node
//                                              actually lands
//
// The `--run` mode is the one that can tell the two arms apart in the PORT
// rather than in the data: it stands the player in zone 4116, ticks SCENE 57
// until `A_2_TelisKiss` is moving things, and puts every motion through
// `NodeMotion::placeOn` against the mesh's own authored position - the exact
// call `omk-play` makes. A displacement-only reading leaves `Gunbl` on its
// parked anchor; the corrected one lands it on the path.
//
// The handler (0x0046F400, `readable/src/23_script.c`) reads
// `v88 = Script_GetParamInt(a2, 5)` and branches on it: NON-ZERO takes the
// displacement form - `sample(t) - sample(t0) + the node's own position when
// the move began` - and ZERO falls to `LABEL_39`, one line,
// `o3de_SetNodePos(node, sample(t))`, the path placed OUTRIGHT in world
// coordinates. Nothing in the path's own numbers separates the two: the
// flat's door starts at 632/-43/34 and reads local, `Gunbl` starts at
// 3352/1056/-884 and reads absolute, and `AHALL40`'s lift doors are
// displacements whose samples sit within 7 units of their mesh and would pass
// for either. The parameter is the only discriminator.
#include "formats/mesh3do.h"
#include "formats/scx.h"
#include "platform/datafs.h"
#include "script/area.h"
#include "script/gamestate.h"
#include "script/program.h"
#include "script/zones.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
constexpr std::uint32_t kMove = 0x03000008u;

int paramFive(const omk::ScxFunction& f) {
    return f.params.size() > 5 ? static_cast<int>(f.params[5]) : -1;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: path_form <gamedata> [Scene.SCX]\n");
        return 2;
    }
    const omk::DataFs data(argv[1]);
    if (argc > 3 && std::strcmp(argv[3], "--run") == 0) {
        const std::string fr = argv[1], tb = argv[2];
        const auto table = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
        if (!table.valid()) { std::fprintf(stderr, "no opcode table\n"); return 1; }
        auto state = omk::GameState::fromFile(fr + "/IAM/START");
        // the beat is gated on VARIABLES[649] `J dans la Cuisine` - a harness
        // write, the way `omk-play --var` does it, so the probe needs no save
        state.setVar(649, 1);
        omk::Session s(fr + "/IAM", state, table);
        s.loadAnnounceMap(tb + "/vm_announce.json");
        s.loadArea(237);
        s.sceneLoad(237, 57);
        s.loadScene(fr + "/SCPTDATA", omk::ChunkKind::Area, 237);
        // the set's own meshes, for the anchor `placeOn` lays a displacement on
        const auto mo = data.resolve("MESHES/DECORS/AAPKAYL.3DO");
        std::vector<omk::Mesh> meshes;
        if (mo) {
            const auto md = omk::DataFs::readPath(*mo);
            if (const auto mh = omk::readHeader(md)) meshes = omk::readMeshes(md, *mh);
        }
        const auto anchorOf = [&](const std::string& name, float out[3]) {
            for (const auto& m : meshes)
                if (name == m.name) {
                    for (int c = 0; c < 3; ++c) out[c] = m.pos[c];
                    return true;
                }
            return false;
        };
        // zone 4116 - the greeting the beat hangs off; its quad's centre
        const omk::LiveZone* z = s.zones().resolve(4116);
        if (!z) { std::fprintf(stderr, "zone 4116 absent\n"); return 1; }
        double c[3];
        z->zone.centre(c);
        const float p[3] = {static_cast<float>(c[0]), static_cast<float>(c[1]),
                            static_cast<float>(c[2])};
        for (int f = 0; f < 4; ++f) s.frame();
        s.setPlayerPosition(p, 181.0f);
        for (int f = 0; f < 500; ++f) {
            s.frame();
            for (const omk::SceneRunner* sr : {&s.scene(), &s.sceneOut()}) {
                if (!sr->loaded()) continue;
                for (const auto& m : sr->motions()) {
                    float anchor[3] = {0, 0, 0}, at[3];
                    if (!anchorOf(m.name, anchor)) continue;
                    m.placeOn(anchor, at);
                    std::printf("frame %3d  %-14s sample %8.1f %8.1f %8.1f  "
                                "anchor %8.1f %8.1f %8.1f  -> %8.1f %8.1f %8.1f\n",
                                f, m.name.c_str(), m.pos[0], m.pos[1], m.pos[2],
                                anchor[0], anchor[1], anchor[2], at[0], at[1], at[2]);
                }
            }
        }
        return 0;
    }
    if (argc > 2) {
        const auto p = data.resolve(std::string("SCPTDATA/") + argv[2]);
        if (!p) { std::fprintf(stderr, "no such scx\n"); return 1; }
        const omk::ScxScene s = omk::readScx(omk::DataFs::readPath(*p));
        for (const auto& o : s.objects)
            for (const auto& f : o.functions) {
                if (f.id != kMove) continue;
                const int five = paramFive(f);
                std::printf("%-22s %-14s param5 %3d  %s\n", o.name.c_str(),
                            f.params.empty()
                                ? std::string("(no node)").c_str()
                                : omk::ScxRuntime::objectName(o, static_cast<int>(f.params[0])).c_str(),
                            five, five ? "displacement" : "ABSOLUTE");
            }
        return 0;
    }
    // the corpus
    long calls = 0, absolute = 0, relative = 0, shortCall = 0, files = 0;
    std::vector<std::string> names;
    const std::string dir = std::string(argv[1]) + "/SCPTDATA";
    for (const auto& e : fs::directory_iterator(dir)) {
        const std::string n = e.path().filename().string();
        if (n.size() < 4) continue;
        const std::string ext = n.substr(n.size() - 4);
        if (ext != ".SCX" && ext != ".scx") continue;
        const omk::ScxScene s = omk::readScx(omk::DataFs::readPath(e.path().string()));
        if (!s.valid) continue;
        ++files;
        for (const auto& o : s.objects)
            for (const auto& f : o.functions) {
                if (f.id != kMove) continue;
                ++calls;
                const int five = paramFive(f);
                if (five < 0) ++shortCall;
                else if (five) ++relative;
                else ++absolute;
            }
    }
    std::printf("scx %ld  moveOnPath %ld  absolute %ld  displacement %ld  short %ld\n",
                files, calls, absolute, relative, shortCall);
    return 0;
}
