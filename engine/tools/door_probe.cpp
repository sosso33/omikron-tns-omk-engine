// SPDX-License-Identifier: GPL-3.0-or-later
// DOES A SCENE-OBJECT DOOR ACTUALLY MOVE? - sampled per frame, not per run.
//
//     door_probe <gamedata> <tables>
//
// `todo/next-tasks.md` item 7 was confirmed from two things that do NOT
// establish motion: `omk-play`'s `motion:` census line, which is printed once
// when a mesh is first claimed by a pool, and two stills taken from two
// DIFFERENT camera positions. Neither can tell an animation from a snap, and a
// reader watching the apartment reported no animation at all.
//
// This stands the player in Kay'l's entrance-door zone and prints the door
// mesh's world position on every tick, so the question is answered by the
// sequence: a slide is many distinct samples, a snap is one jump between two
// consecutive frames, and a dead program is no samples at all.
#include "platform/datafs.h"
#include "script/area.h"
#include "script/gamestate.h"
#include "script/zones.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: door_probe <gamedata> <tables>\n");
        return 2;
    }
    const std::string fr = argv[1], tb = argv[2];
    const auto table = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    if (!table.valid()) { std::fprintf(stderr, "no opcode table\n"); return 1; }
    const std::string iam = fr + "/IAM";
    auto state = omk::GameState::fromFile(iam + "/START");
    omk::Session s(iam, state, table);
    s.loadAnnounceMap(tb + "/vm_announce.json");

    s.loadArea(237);
    s.sceneLoad(237, 57);                 // the scene the save has over it
    // ...and the `.SCX` itself: the Session holds the scripts, `loadScene`
    // is what gives it the resources the programs move.
    s.loadScene(fr + "/SCPTDATA", omk::ChunkKind::Area, 237);
    for (int f = 0; f < 40; ++f) s.frame();

    const omk::LiveZone* z = s.zones().resolve(-28735);   // the kitchen cupboard
    const omk::LiveZone* d = nullptr;
    for (int id : {4028}) if ((d = s.zones().resolve(id))) break;
    std::printf("area %d scene %d; entrance zone %s, cupboard zone %s\n",
                s.currentArea(), s.residentSlot(s.shownSlot()).scene,
                d ? "live" : "ABSENT", z ? "live" : "ABSENT");
    if (!d) return 1;

    double c[3];
    d->zone.centre(c);
    const float p[3] = {static_cast<float>(c[0]), static_cast<float>(c[1]),
                        static_cast<float>(c[2])};
    s.setPlayerPosition(p, static_cast<float>(static_cast<int>(
        static_cast<double>(d->zone.arcMid) * omk::kZoneArcToDegrees)));

    // Tick, and print every motion sample the scene pools produce. At frame
    // 40 the player is taken OUT of the zone, which is what a reader does by
    // walking away: the zone's LEAVE script (@9601) should shut the door.
    for (int f = 0; f < 140; ++f) {
        if (f == 40) {
            const float away[3] = {p[0] + 400.0f, p[1], p[2]};
            s.setPlayerPosition(away, 90.0f);
            std::printf("frame  40  --- the player steps out of the zone ---\n");
        }
        s.frame();
        for (const omk::SceneRunner* sr : {&s.scene(), &s.sceneOut()}) {
            if (!sr->loaded()) continue;
            for (const auto& mo : sr->motions())
                std::printf("frame %3d  %-14s %8.1f %8.1f %8.1f%s\n",
                            f, mo.name.c_str(), mo.pos[0], mo.pos[1], mo.pos[2],
                            mo.rotated ? "  rotated" : "");
        }
    }
    return 0;
}
