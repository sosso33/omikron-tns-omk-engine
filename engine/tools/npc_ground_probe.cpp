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
//   near    2 .. 100 - the port seats him (its own cut-off)
//   far     100 and more - the port KEEPS the authored height (a ledge "the
//           walk mesh does not carry"); the original's `Actor_ApplyMotion`
//           falls an undriven NPC to whatever `Walk_ProbeGround` finds
//   none    no walkable surface below at all - the original's response puts
//           him back at his last safe position, i.e. where he is
//
// So `far` is the population M6 is about. Printed: the totals, then each
// `far` record (area, actor, position, drop), so a reader can look at them.
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
        ++areasWith;
        for (const auto& p : places) {
            ++recs;
            const auto g = omk::floorUnder(soup, p.pos[0], p.pos[1] - 2.0, p.pos[2]);
            if (!g) { ++none; continue; }
            const double drop = *g - p.pos[1];              // Y is down
            if (drop < 2.0) ++on;
            else if (drop < 100.0) {
                ++nearC;
                ++nearHist[drop < 10.0 ? 0 : drop < 30.0 ? 1 : drop < 60.0 ? 2 : 3];
            }
            else {
                ++far;
                char buf[128];
                std::snprintf(buf, sizeof buf, "far area %zu actor %d at %.0f %.0f %.0f drop %.0f\n",
                              i, p.actor, p.pos[0], p.pos[1], p.pos[2], drop);
                farList += buf;
            }
        }
    }
    std::printf("npc ground areas %d records %d on %d near %d far %d none %d noset %d\n",
                areasWith, recs, on, nearC, far, none, noSet);
    std::printf("near 2-10 %d 10-30 %d 30-60 %d 60-100 %d\n", nearHist[0], nearHist[1],
                nearHist[2], nearHist[3]);
    std::fputs(farList.c_str(), stdout);
    return 0;
}
