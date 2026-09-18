// SPDX-License-Identifier: GPL-3.0-or-later
// WHERE A SCENE PROGRAM LEAVES THE NODE IT MOVED - the corpus, and one scene.
//
//     node_rest <gamedata>                 the census: every shipped program
//     node_rest <gamedata> <Scene.SCX>     every object of one scene, named
//     node_rest <gamedata> <tables> --lift Anekbah Hall 40's centre lift,
//                                          driven through a real `Session`:
//                                          the door opens, its program ends,
//                                          and the placement must STAND
//
// `Script_MoveObjectOnPath` (0x0046F400) ends in `o3de_SetNodePos(node, x, y,
// z)` - read in `readable/src/23_script.c` and again in the raw listing, where
// the tail is `call sub_4370A0` (`o3de_SetNodePos`), `call sub_437160` (the
// node's 3x3), then the loop counter and `retn`. `o3de_SetNodePos` writes the
// node's own `+36/+40/+44` (or `+128/+132/+136` under a parent). NOTHING in
// the handler puts the node back, and nothing in `Script_PlayScript` does
// either: the placement is PERMANENT until something else moves the node.
//
// The port modelled a motion as a per-tick record - `SceneRunner::motions()`
// is refilled by every `tick` - so the tick a program ended, the record
// vanished and every consumer lost the node. This counts how much of the
// shipped corpus that reaches: how many object programs finish with at least
// one node left displaced from where the set authored it.
//
// WHAT COUNTS AS DISPLACED, and why the two arms are counted apart.
// `v88 = Script_GetParamInt(a2, 5)` picks the handler's two forms
// (`tools/path_form.cpp` has the reading):
//
//   * NON-ZERO, the displacement form: the node ends at `sample(t_end) -
//     sample(t0) + anchor`, so it rests displaced by `sample(t_end) -
//     sample(t0)` - a quantity in the path alone, needing no set;
//   * ZERO, the absolute form: `o3de_SetNodePos(node, sample(t_end))`, a
//     world point. Whether that differs from the mesh's authored place cannot
//     be answered from the `.SCX`, which never names its `.3DO`, so those are
//     reported as their own number and NOT folded into the displaced count.
//
// The run is the port's own `Program`, ticked one frame at a time until it
// stops, so what is measured is the port's model of the engine and not a
// second reading of the data. A program whose object loops for ever (`loop`
// -1, 960 of them) never stops; those are capped and counted separately,
// since a node under continuous control never comes to rest at all.
#include "formats/scx.h"
#include "platform/datafs.h"
#include "script/area.h"
#include "script/gamestate.h"
#include "script/program.h"
#include "script/zones.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kMove   = 0x03000008u;
// `Script_ScaleObjectX/Y/Z`. A scale PERSISTS in `SceneRunner::nodeScales()`
// while a motion did not, and the frontend's patch seeds itself from both - so
// a mesh that is scaled and moved keeps a patch entry after its move ends, and
// that entry, having no motion, re-places it at its AUTHORED origin every
// frame. That pair is the one place a finished program's node really was put
// back, so it is counted here beside the rest.
constexpr std::uint32_t kScaleX = 0x03000023u;
constexpr std::uint32_t kScaleY = 0x03000024u;
constexpr std::uint32_t kScaleZ = 0x03000025u;
constexpr int  kCap = 4000;      // ticks before a program is called endless
constexpr float kEps = 1.0f;     // one world unit - an inch (docs/PORTING A2)

struct Rest {
    float d[3] = {0, 0, 0};   // the displacement the last motion leaves
    bool  relative = false;   // which arm the last motion took
};

// Run one object to a stop and report where each mesh it names comes to rest.
// -> false when the program was still running at the cap.
bool runToRest(const omk::ScxRuntime& rt, const omk::ScxObject& obj,
               std::map<std::string, Rest>& out, int& ticks) {
    omk::Program p(rt, obj);
    p.start();
    ticks = 0;
    while (p.running() && ticks < kCap) {
        p.tick(1.0f);
        ++ticks;
        for (const auto& m : p.motions()) {
            if (!m.placed || m.name.empty()) continue;
            Rest& r = out[m.name];
            r.relative = m.hasFrom;
            for (int c = 0; c < 3; ++c)
                r.d[c] = m.hasFrom ? m.pos[c] - m.from[c] : m.pos[c];
        }
    }
    return !p.running();
}

bool movesAnything(const omk::ScxObject& o) {
    for (const auto& f : o.functions) if (f.id == kMove) return true;
    return false;
}

// Every set mesh this object NAMES under one of the given function ids.
void namesUnder(const omk::ScxObject& o, std::uint32_t a, std::uint32_t b,
                std::uint32_t c, std::vector<std::string>& out) {
    for (const auto& f : o.functions) {
        if (f.id != a && f.id != b && f.id != c) continue;
        if (f.params.empty()) continue;
        std::string n = omk::ScxRuntime::objectName(o, static_cast<int>(f.params[0]));
        if (!n.empty()) out.push_back(std::move(n));
    }
}

float len(const float d[3]) {
    return std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: node_rest <gamedata> [Scene.SCX]\n");
        return 2;
    }
    const omk::DataFs data(argv[1]);

    // ---- the LIVE case, through a Session -------------------------------
    // Anekbah Hall 40's centre lift, which `verify.py: engine: lift doors`
    // already establishes is ungated: zone 512's enter script is a bare
    // `scx.play` and `end`. Standing in it runs the door program, and what is
    // asserted here is what happens AFTER that program stops - the node must
    // still be where the last `o3de_SetNodePos` put it.
    if (argc > 3 && std::strcmp(argv[3], "--lift") == 0) {
        const std::string fr = argv[1], tb = argv[2];
        const auto table = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
        if (!table.valid()) { std::fprintf(stderr, "no opcode table\n"); return 1; }
        auto state = omk::GameState::fromFile(fr + "/IAM/START");
        omk::Session s(fr + "/IAM", state, table);
        s.loadAnnounceMap(tb + "/vm_announce.json");
        s.loadArea(13);
        s.loadScene(fr + "/SCPTDATA", omk::ChunkKind::Area, 13);
        // the stand `engine: lift doors` uses, and its heading: 42 degrees is
        // where zone 512's `arcMid` points, and the EXIT zone shares the quad
        const float at[3] = {3923.0f, -19.0f, -1200.0f};
        for (int f = 0; f < 4; ++f) s.frame();
        s.setPlayerPosition(at, 42.0f);
        int lastMotion = -1, ticks = 0;
        std::map<std::string, Rest> live;   // from placements(), not motions()
        for (int f = 0; f < 160; ++f) {
            s.frame();
            ++ticks;
            for (const omk::SceneRunner* sr : {&s.scene(), &s.sceneOut()}) {
                if (!sr->loaded()) continue;
                if (!sr->motions().empty()) lastMotion = f;
                // THE PLACEMENTS, which is the state under test. A reader of
                // `motions()` would have nothing at all to print here once the
                // program stops, which is exactly the bug.
                for (const auto& [nm, m] : sr->placements()) {
                    Rest& r = live[nm];
                    r.relative = m.hasFrom;
                    for (int c = 0; c < 3; ++c)
                        r.d[c] = m.hasFrom ? m.pos[c] - m.from[c] : m.pos[c];
                }
            }
        }
        std::printf("lift: %d frames, last motion at frame %d, %zu nodes placed\n",
                    ticks, lastMotion, live.size());
        for (const auto& [nm, r] : live)
            std::printf("lift: %-14s rests %7.1f %7.1f %7.1f  |d| %6.1f  %s\n",
                        nm.c_str(), r.d[0], r.d[1], r.d[2], len(r.d),
                        r.relative ? "displacement" : "ABSOLUTE");
        return 0;
    }

    if (argc > 2) {
        const auto p = data.resolve(std::string("SCPTDATA/") + argv[2]);
        if (!p) { std::fprintf(stderr, "no such scx\n"); return 1; }
        const omk::ScxRuntime rt(omk::DataFs::readPath(*p));
        if (!rt.valid()) { std::fprintf(stderr, "bad scx\n"); return 1; }
        for (const auto& o : rt.scene().objects) {
            if (!movesAnything(o)) continue;
            std::map<std::string, Rest> rest;
            int ticks = 0;
            const bool ended = runToRest(rt, o, rest, ticks);
            for (const auto& [nm, r] : rest)
                std::printf("%-24s %-14s %s  ticks %4d  %s  rests %8.1f %8.1f %8.1f"
                            "  |d| %7.1f\n",
                            o.name.c_str(), nm.c_str(), ended ? "ENDS   " : "endless",
                            ticks, r.relative ? "displacement" : "ABSOLUTE   ",
                            r.d[0], r.d[1], r.d[2], r.relative ? len(r.d) : 0.0f);
        }
        return 0;
    }

    // ---- the corpus -------------------------------------------------------
    long files = 0, movers = 0, ended = 0, endless = 0;
    long endedDisplaced = 0, endedHome = 0, endedAbsolute = 0;
    long nodesDisplaced = 0;
    // ...and how many of the displacing programs have a LINKED PARTNER STATE.
    // `Scene_LoadSCX` pairs objects by name at load (`CSPorte79hopen` /
    // `CSPorte79hclosed`), and the partner is the shipped way a door SHUTS: it
    // runs the same path the other way. A node that returned home by itself
    // when its program ended would make every one of those partners redundant,
    // so this count is the data's own argument for the handler's tail.
    long displacedWithPartner = 0;
    long scaledAndMoved = 0;
    std::vector<std::string> scaledAndMovedWhere;
    double worst = 0.0;
    std::string worstWhere;
    const std::string dir = std::string(argv[1]) + "/SCPTDATA";
    std::vector<std::string> paths;
    for (const auto& e : fs::directory_iterator(dir)) {
        const std::string n = e.path().filename().string();
        if (n.size() < 4) continue;
        const std::string ext = n.substr(n.size() - 4);
        if (ext == ".SCX" || ext == ".scx") paths.push_back(e.path().string());
    }
    std::sort(paths.begin(), paths.end());
    for (const auto& pth : paths) {
        const omk::ScxRuntime rt(omk::DataFs::readPath(pth));
        if (!rt.valid()) continue;
        ++files;
        // the scale/move overlap is a property of the SCENE, since the
        // frontend merges every running program's into one patch table
        {
            std::vector<std::string> scaled, movedNames;
            for (const auto& o : rt.scene().objects) {
                namesUnder(o, kScaleX, kScaleY, kScaleZ, scaled);
                namesUnder(o, kMove, kMove, kMove, movedNames);
            }
            for (const auto& sn : scaled)
                for (const auto& mn : movedNames)
                    if (sn == mn) {
                        ++scaledAndMoved;
                        scaledAndMovedWhere.push_back(
                            fs::path(pth).filename().string() + " " + sn);
                        break;
                    }
        }
        for (const auto& o : rt.scene().objects) {
            if (!movesAnything(o)) continue;
            ++movers;
            std::map<std::string, Rest> rest;
            int ticks = 0;
            if (!runToRest(rt, o, rest, ticks)) { ++endless; continue; }
            ++ended;
            bool anyDisplaced = false, anyAbsolute = false;
            for (const auto& [nm, r] : rest) {
                if (!r.relative) { anyAbsolute = true; continue; }
                const float l = len(r.d);
                if (l <= kEps) continue;
                anyDisplaced = true;
                ++nodesDisplaced;
                if (l > worst) {
                    worst = l;
                    worstWhere = fs::path(pth).filename().string() + " " +
                                 o.name + " " + nm;
                }
            }
            if (anyDisplaced) {
                ++endedDisplaced;
                if (o.hasLink) ++displacedWithPartner;
            }
            else if (anyAbsolute) ++endedAbsolute;
            else ++endedHome;
        }
    }
    std::printf("scx %ld  objects that move a node %ld  (end %ld, endless %ld)\n",
                files, movers, ended, endless);
    std::printf("of the %ld that END: %ld leave a node DISPLACED, %ld leave every "
                "node back home, %ld end on the absolute arm alone\n",
                ended, endedDisplaced, endedHome, endedAbsolute);
    std::printf("nodes left displaced %ld   worst %.1f units  (%s)\n",
                nodesDisplaced, worst, worstWhere.c_str());
    std::printf("of the %ld displacing programs, %ld have a LINKED partner "
                "state - the object that puts the node back\n",
                endedDisplaced, displacedWithPartner);
    std::printf("meshes a scene both SCALES and MOVES %ld%s%s\n", scaledAndMoved,
                scaledAndMovedWhere.empty() ? "" : "  - ",
                scaledAndMovedWhere.empty() ? ""
                                            : scaledAndMovedWhere.front().c_str());
    return 0;
}
