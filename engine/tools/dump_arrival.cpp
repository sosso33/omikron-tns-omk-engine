// SPDX-License-Identifier: GPL-3.0-or-later
// THE ARRIVAL - `actor.goto_address`, and the camera that resolves against it.
//
//     dump_arrival <gamedata> <tables> <out.txt>
//
// The intro's last four instructions are `area.goto 222`, `scene.load 222, 55`,
// `actor.goto_address 654` and `area.arrive 118`. The third is what gives the
// player a world position, and without it every subject-relative world camera
// is unresolvable - 1443 of the 5384 - so the frame after the cutscene was
// black with the music still playing.
//
// This drives the Session directly rather than replaying the whole intro: load
// AREA 222, place the player at 654, and resolve the camera the game then asks
// for. What it reports is therefore the ported path, not a second decode.
#include "platform/datafs.h"
#include "formats/addresses.h"
#include "formats/mesh3do.h"
#include "o3de/geom3do.h"
#include "o3de/worldcam.h"
#include "script/area.h"
#include "script/gamestate.h"

#include <cstdio>
#include <algorithm>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: dump_arrival <gamedata> <tables> <out.txt>\n");
        return 2;
    }
    if (!omk::safeOutputPath(argv[3])) return 2;
    const std::string fr = argv[1], tb = argv[2];
    const auto table = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    if (!table.valid()) return 1;

    auto state = omk::GameState::fromFile(fr + "/IAM/START");
    omk::Session s(fr + "/IAM", state, table);
    s.loadArea(222);

    std::ofstream f(argv[3]);
    // the chunk's own table, as the port reads it
    const auto areaFile = omk::DataFs::readPath(fr + "/IAM/AREA");
    const auto areas = omk::IamArchive::open(areaFile);
    const auto ab = areas.chunk(222);
    for (const auto& a : omk::readAddresses(ab))
        f << "address " << a.id << ' ' << int(a.pos[0]) << ' ' << int(a.pos[1])
          << ' ' << int(a.pos[2]) << ' ' << int(a.yaw) << '\n';

    // ...then the placement and the camera it makes resolvable
    const bool placed = s.placeActorAt(654);
    f << "placed " << (placed ? 1 : 0) << ' ' << s.playerAddress() << ' '
      << int(s.playerPos()[0]) << ' ' << int(s.playerPos()[1]) << ' '
      << int(s.playerPos()[2]) << ' ' << int(s.playerYaw()) << '\n';

    if (const omk::WorldCamera* c = s.cameras().find(0)) {
        f << "camera " << c->id << ' ' << c->eyeSubject << ' ' << c->atSubject
          << ' ' << (c->absolute() ? 1 : 0) << '\n';
        const auto r = omk::resolveCamera(*c, s.playerPos(), s.playerYaw());
        f << "resolved " << int(r.eye[0]) << ' ' << int(r.eye[1]) << ' '
          << int(r.eye[2]) << ' ' << int(r.at[0]) << ' ' << int(r.at[1])
          << ' ' << int(r.at[2]) << '\n';
        std::printf("player at %.0f %.0f %.0f yaw %.0f -> camera 0 eye "
                    "%.0f %.0f %.0f  at %.0f %.0f %.0f\n",
                    s.playerPos()[0], s.playerPos()[1], s.playerPos()[2],
                    s.playerYaw(), r.eye[0], r.eye[1], r.eye[2],
                    r.at[0], r.at[1], r.at[2]);
    }

    // ...and the SURFACE the address stands on, which is what settles the
    // resolve's SIGN. `sub_415D10`/`sub_415E60` compute `subjectPos - offset`;
    // the alternative reading, `+`, differs by 52 units in y here. AIMPASSE's
    // `GEM0` is a flat plane at y 399 with the address 2 above it, and Y points
    // DOWN - so subtracting puts the eye 28 ABOVE that surface and adding puts
    // it 24 BELOW, inside the geometry. The data cannot choose the wrong one.
    {
        const omk::DataFs fs(fr);
        const auto p = fs.resolve("MESHES/DECORS/AImpasse.3DO");
        if (p) {
            const auto d = omk::DataFs::readPath(*p);
            const auto g = omk::buildGeometry(d, omk::DrawFilter::Engine);
            const auto h = omk::readHeader(d);
            if (h) {
                const auto ms = omk::readMeshes(d, *h);
                for (const auto& m : ms) {
                    if (std::string(m.name) != "GEM0") continue;
                    float loY = 1e9f, hiY = -1e9f;
                    for (std::size_t i = 0; i < g.corners.size(); ++i) {
                        if (g.cornerMesh[i] != m.index) continue;
                        loY = std::min(loY, g.corners[i].y);
                        hiY = std::max(hiY, g.corners[i].y);
                    }
                    f << "surface GEM0 " << int(loY) << ' ' << int(hiY) << '\n';
                }
            }
        }
    }
    return 0;
}
