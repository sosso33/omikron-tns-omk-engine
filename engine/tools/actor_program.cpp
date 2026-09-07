// SPDX-License-Identifier: GPL-3.0-or-later
// ONE PROGRAM PER ACTOR - `ScriptObject_StartOnActor`'s opening clause.
//
//     actor_program <gamedata> <tables>
//
// `ScriptObject_StartOnActor` (0x0041BA80) begins by clearing whatever the
// actor was already running:
//
//     v5 = &g_Actors + 1312 * a1;          // the actor record
//     if (u32i(v5, 43)) {                  // +172: its current script object
//         Scene_ResetObjectState(u32i(v5, 43));
//         u32i(v5, 43) = 0;
//     }
//
// and `Scene_ResetObjectState` (0x0044AA20) is two stores - `u16(+30) &=
// 0xFFF0` and `u16(+28) = 0`, the busy word - so the old object stops.
//
// Kay'l's flat is where it shows. `Aapkayl.SCX`'s `TélisLitSeule` (handle
// 156) has loop -1 and RUNS FOR EVER, and SCENE 57's opening cutscene starts
// it on actor 53; the goodbye then starts `TelisAuRevoir` (184, loop 1) on
// the same actor. Without the reset both are live, and the moment the
// goodbye's own program ends the bed one takes the body back - Telis vanishes
// from the doorway the shot is framed on.
#include "formats/scx.h"
#include "platform/datafs.h"
#include "script/scenerunner.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: actor_program <gamedata> <tables>\n");
        return 2;
    }
    const std::string fr = argv[1];
    const auto table = omk::OpcodeTable::loadJson(std::string(argv[2]) + "/vm_opcodes.json");
    omk::SceneRunner sc;
    if (!sc.load(fr + "/SCPTDATA", fr + "/IAM", table, omk::ChunkKind::Scene, 57)) {
        std::fprintf(stderr, "cannot make Aapkayl.SCX resident\n");
        return 1;
    }
    const auto say = [&](const char* when) {
        std::printf("%-22s", when);
        for (std::size_t k = 0; k < sc.started().size(); ++k)
            std::printf("  %s(%d)=%s", sc.started()[k].name.c_str(),
                        sc.started()[k].object,
                        sc.programRunning(static_cast<int>(k)) ? "running" : "stopped");
        std::printf("\n");
    };
    // the flat's opening: the bed program, on actor 53, looping for ever
    sc.handle({omk::Call{60, {53, 156, 0}}});
    for (int f = 0; f < 30; ++f) sc.tick();
    say("bed started:");
    // ...and later the goodbye, on the SAME actor
    sc.handle({omk::Call{60, {53, 184, 0}}});
    say("goodbye started:");
    // a SCENE object (57/58) binds to no actor and must reset nothing
    sc.handle({omk::Call{58, {137, 0, 0}}});
    say("a scene object too:");
    return 0;
}
