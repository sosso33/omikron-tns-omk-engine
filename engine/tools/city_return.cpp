// SPDX-License-Identifier: GPL-3.0-or-later
// IS THE CITY STILL RUNNING WHEN YOU COME BACK OUT OF A BUILDING?
//
// A reader's question: are the city's scripts - the ambient animations, the
// street lights, the fire - correctly reloaded on leaving a building. The
// engine's answer is that nothing is reloaded because nothing was lost:
// `Area_LoadIntoSlot` (0x00402B70) opens
//
//     if (dword_69BC48[4 * slot] == area) return sub_41D380(area, block + 144);
//
// - the fog block refreshed and NOTHING else. No `Area_Load`, so
// `Area_TickLoad` never reaches case 2 (`Area_LoadScx`) or case 9 (the startup
// contexts): the slot keeps its object container, and `Script_PlayAllScripts`
// has been ticking it the whole time the player was inside, because it runs
// over BOTH resident scenes. So the city's programs never stopped.
//
// This walks the round trip through the game's own door pair - AREA 0
// (Anekbah) zone record 3 is `area.goto 201, 153, 240` (Anekbah Hall 43), and
// AREA 201 zone record 2 is `area.goto 0, 19, 20` back - and reports what the
// city has before and after: which `.SCX` is resident, how many of its
// programs are running, the standing `.sfx` rows the bind shows, and the live
// emitters and particles. The two lines should agree.
//
//     city_return <gamedata> <tables>
#include "formats/iam.h"
#include "script/area.h"
#include "script/gamestate.h"
#include "script/script.h"
#include "script/world.h"
#include "platform/datafs.h"
#include <cstdio>
#include <string>
#include <span>

namespace {

void report(const char* when, omk::Session& s) {
    const auto& sc = s.scene();
    const auto& so = s.sceneOut();
    std::printf("%-22s area %3d  scx %-16s running %2d of %2zu  standing %2d  "
                "fired %2d  emitters %3zu  particles %4zu   | out: area %3d scx %-16s running %2d\n",
                when, s.currentArea(),
                sc.loaded() ? sc.file().c_str() : "(none)",
                sc.loaded() ? sc.programsRunning() : -1,
                sc.loaded() ? sc.programCount() : 0u,
                sc.loaded() ? sc.standingPieces() : -1,
                sc.loaded() ? sc.piecesFired() : -1,
                sc.loaded() ? sc.effects().emitterCount() : 0u,
                sc.loaded() ? sc.effects().count() : 0u,
                s.sceneOutArea(),
                so.loaded() ? so.file().c_str() : "(none)",
                so.loaded() ? so.programsRunning() : -1);
}

// Run one of a chunk's zone-record scripts as the engine does - a context on
// that slot, queued - and tick until the transition it starts has settled.
// `slot` is the SLOT THE CHUNK IS RESIDENT IN, and it matters: `area.goto`
// loads into `1 - slot`, so a return script run on the wrong slot targets the
// slot that already holds the city and evicts it instead of taking
// `Area_LoadIntoSlot`'s resident early-out. The first version of this probe
// passed 0 for both legs and manufactured a double load.
void runZoneScript(omk::Session& s, int slot, std::span<const std::byte> chunk,
                   const omk::Zone& z, int frames) {
    const int idx = s.newContext(slot, chunk, z.scripts, z.id, 0);
    s.queueAction(idx, 1);
    std::size_t was = s.scene().loaded() ? s.scene().programCount() : 0;
    std::string wasFile = s.scene().loaded() ? s.scene().file() : "";
    for (int f = 0; f < frames; ++f) {
        s.frame();
        const auto& sc = s.scene();
        const std::size_t now = sc.loaded() ? sc.programCount() : 0;
        const std::string file = sc.loaded() ? sc.file() : "";
        if (file != wasFile) { wasFile = file; was = now;
            std::printf("      resident scene -> %s (%zu programs)\n", file.c_str(), now);
            continue; }
        if (now != was) {
            std::printf("      +%zu program%s started in %s at frame %ld (now %zu)\n",
                        now - was, now - was == 1 ? "" : "s", file.c_str(),
                        s.frameNo(), now);
            was = now;
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: city_return <gamedata> <tables>\n");
        return 2;
    }
    const std::string fr = argv[1], tb = argv[2];
    const std::string iam = fr + "/IAM";
    const auto table = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    const auto areasFile = omk::DataFs::readPath(iam + "/AREA");
    const auto areas = omk::IamArchive::open(areasFile);

    auto state = omk::GameState::fromFile(iam + "/START");
    omk::Session s(iam, state, table);
    s.answerUiFromPerson(true);
    s.setObjectWait(true);
    s.loadScene(fr + "/SCPTDATA", omk::ChunkKind::Area, 0);
    s.loadArea(0);
    for (int f = 0; f < 200; ++f) s.frame();
    // THE SET'S OWN EMITTERS, which the viewer binds when it builds a decor
    // slot: every mesh flagged 0x40000000 whose name matches a section-D tag -
    // the neon, the steam, the smoke. `bindSetEmitters` binds them INTO THE
    // RUNNER, and its only caller is that set load, so a return that replaces
    // the runner loses them with no path to re-bind. Doing it here puts the
    // fire and the neon on the same measurement as the animations.
    const std::string set = s.residentSlot(0).set;
    if (!set.empty()) {
        const auto d = omk::DataFs::readPath(fr + "/MESHES/DECORS/" + set + ".3DO");
        if (!d.empty())
            std::printf("bound %d ambient emitters from %s.3DO\n",
                        s.sceneMutable().bindSetEmitters(d), set.c_str());
    }
    for (int f = 0; f < 30; ++f) s.frame();
    report("in the city", s);

    // ...into the building: Anekbah's zone 3, `area.goto 201`
    const auto chunk0 = areas.chunk(0);
    const auto zones0 = omk::zonesOf(chunk0, omk::ChunkKind::Area);
    runZoneScript(s, 0, chunk0, zones0.at(3), 400);   // Anekbah is in slot 0
    report("inside (Hall 43)", s);

    // ...and back out: AREA 201's zone 2, `area.goto 0`
    const auto chunk201 = areas.chunk(201);
    const auto zones201 = omk::zonesOf(chunk201, omk::ChunkKind::Area);
    runZoneScript(s, 1, chunk201, zones201.at(2), 400);  // Hall 43 landed in slot 1
    report("back in the city", s);
    for (int f = 0; f < 200; ++f) s.frame();
    report("...200 frames later", s);
    return 0;
}
