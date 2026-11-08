// SPDX-License-Identifier: GPL-3.0-or-later
// HOW FAR ABOVE THE WALKABLE FLOOR the shipped characters are placed - the
// measurement todo/drift-audit.md M6 asked for before deciding anything.
//
//     npc_ground_probe <gamedata>
//
// Every character placement record of every AREA chunk (`readPlacements`,
// the positions `Actors_SpawnFromTables` reads), against its own area's set
// (`AREA +88`, `MESHES/DECORS/<set>.3DO`, the walkable soup): the floor
// under the point, probed DOWN from one unit above it - the probe
// `PlayState::worldStagedGround` makes for a record-placed body. Each
// record falls in one class by the drop to that floor:
//
//   on      under 2 units - standing on it
//   near    2 .. 100
//   far     100 and more
//   none    no walkable surface below at all
//
// CORRECTED 2026-11-08 (todo/drift-audit.md M6): this was written believing
// the original falls an undriven NPC. It does not - a placed body sits in
// ACTOR_STATE 0 with his node at the record, whatever is under him - so
// these classes describe the data, not a behaviour. Printed: the totals, then each
// `far` record (area, actor, position, drop), so a reader can look at them.
//
// ...and WHAT THE ORIGINAL'S PROBE WOULD SEE, a second line. `Walk_ProbeGround`
// asks `sub_443560`, which walks EVERY mesh in a box under the point
// (`o3de_ForEachMeshInBox` -> `sub_443810`) and skips only a mesh flagged
// 0x41 - no slope filter - so it can stop on a face the walkable soup leaves
// out (a steep roof, a prop's top). Each record is probed again against the
// `All` collision soup (an INDICATION: `All` keeps the few 0x41 meshes, and
// the set's other decor slots are not in either soup): `higher` counts the
// records where that probe meets a surface above the walkable floor, by
// class, and each far / none record prints the all-faces drop beside it.
// SCENE chunks' placements are not counted: their area is the scene's
// host, resolved at run time.
#include "formats/iam.h"
#include "formats/placements.h"
#include "o3de/collision.h"
#include "platform/datafs.h"

#include <cstdio>
#include <string>

namespace {

std::string headerName(std::span<const std::byte> b, std::size_t off, std::size_t max) {
    std::string s;
    for (std::size_t k = 0; k < max && off + k < b.size(); ++k) {
        const char c = static_cast<char>(b[off + k]);
        if (!c) break;
        s.push_back(c);
    }
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: npc_ground_probe <gamedata>\n");
        return 2;
    }
    const omk::DataFs fs(argv[1]);
    const auto areaFile = fs.read("IAM/AREA");
    const auto areas = omk::IamArchive::open(areaFile);
    int recs = 0, on = 0, nearC = 0, far = 0, none = 0, areasWith = 0, noSet = 0;
    int nearHist[4] = {0, 0, 0, 0};   // 2..10, 10..30, 30..60, 60..100
    int hiOn = 0, hiNear = 0, hiFar = 0, hiNone = 0;   // the all-faces probe stops higher
    std::string farList;
    for (std::size_t i = 0; i < areas.size(); ++i) {
        const auto chunk = areas.chunk(i);
        if (chunk.empty()) continue;
        const auto places = omk::readPlacements(chunk, omk::ChunkKind::Area);
        if (places.empty()) continue;
        const std::string set = headerName(chunk, 88, 9);
        const auto setData = set.empty() ? std::vector<std::byte>{}
                                         : fs.read("MESHES/DECORS/" + set + ".3DO");
        if (setData.empty()) { noSet += static_cast<int>(places.size()); continue; }
        const auto soup = omk::collisionSoup(setData, omk::SoupKind::Walkable);
        const auto all = omk::collisionSoup(setData, omk::SoupKind::All);
        ++areasWith;
        for (const auto& p : places) {
            ++recs;
            const auto g = omk::floorUnder(soup, p.pos[0], p.pos[1] - 2.0, p.pos[2]);
            const auto ga = omk::floorUnder(all, p.pos[0], p.pos[1] - 2.0, p.pos[2]);
            // the all-faces probe stops on something above the walkable floor
            const bool higher = ga && (!g || *ga < *g - 2.0);
            char allDrop[32] = "none";
            if (ga) std::snprintf(allDrop, sizeof allDrop, "%.0f", *ga - p.pos[1]);
            if (!g) {
                ++none;
                if (higher) ++hiNone;
                char buf[160];
                std::snprintf(buf, sizeof buf, "none area %zu actor %d at %.0f %.0f %.0f all-faces drop %s\n",
                              i, p.actor, p.pos[0], p.pos[1], p.pos[2], allDrop);
                farList += buf;
                continue;
            }
            const double drop = *g - p.pos[1];              // Y is down
            if (drop < 2.0) { ++on; if (higher) ++hiOn; }
            else if (drop < 100.0) {
                ++nearC;
                if (higher) ++hiNear;
                ++nearHist[drop < 10.0 ? 0 : drop < 30.0 ? 1 : drop < 60.0 ? 2 : 3];
            }
            else {
                ++far;
                if (higher) ++hiFar;
                char buf[160];
                std::snprintf(buf, sizeof buf, "far area %zu actor %d at %.0f %.0f %.0f drop %.0f all-faces drop %s\n",
                              i, p.actor, p.pos[0], p.pos[1], p.pos[2], drop, allDrop);
                farList += buf;
            }
        }
    }
    std::printf("npc ground areas %d records %d on %d near %d far %d none %d noset %d\n",
                areasWith, recs, on, nearC, far, none, noSet);
    std::printf("near 2-10 %d 10-30 %d 30-60 %d 60-100 %d\n", nearHist[0], nearHist[1],
                nearHist[2], nearHist[3]);
    std::printf("all faces higher on %d near %d far %d none %d\n", hiOn, hiNear, hiFar, hiNone);
    std::fputs(farList.c_str(), stdout);
    return 0;
}
