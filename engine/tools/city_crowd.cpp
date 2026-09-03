// SPDX-License-Identifier: GPL-3.0-or-later
// city_crowd - load a city's AREA into the Session with its .SCX, run a few
// frames, and list the scene programs its own startup script started: the
// AUTHORED EXTRAS of docs/STREET_LIFE.md 1 (one looping program per placed
// character - couples, beggars, patrols), which is mechanism B of a street.
//
//     city_crowd <gamedata> <tables dir> <area> [frames]
//
// Prints one `area` line, then one `program` line per started object with
// its object id (the SCX object's handle >> 16), how it was started, the
// CHARACTERS id it drives and the clip and path it resolved. `verify.py:
// engine: city crowd` compares the object set against the startup script's
// own `scx.play.actor` words decoded independently by tools/dialog_disasm.py.
#include "script/area.h"
#include "script/gamestate.h"
#include "script/script.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: city_crowd <gamedata> <tables dir> <area> [frames]\n");
        return 2;
    }
    const std::string fr = argv[1], tb = argv[2];
    const int area   = std::atoi(argv[3]);
    const int frames = argc > 4 ? std::atoi(argv[4]) : 60;
    const auto table = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    if (!table.valid()) return 1;
    const std::string iam = fr + "/IAM";
    auto state = omk::GameState::fromFile(iam + "/START");
    omk::Session s(iam, state, table);
    s.answerUiFromPerson(true);
    s.loadScene(fr + "/SCPTDATA", omk::ChunkKind::Area, area);
    s.loadArea(area);
    // every object seen started at any frame, keyed by object id
    std::map<int, omk::SceneRunner::Started> seen;
    for (int f = 0; f < frames; ++f) {
        s.frame();
        for (const auto& st : s.scene().started())
            if (!seen.count(st.object)) seen[st.object] = st;
    }
    int actorProgs = 0, resolved = 0;
    for (const auto& kv : seen) {
        if (kv.second.how != "actor") continue;
        ++actorProgs;
        if (kv.second.actor >= 0 && kv.second.clip >= 0 && kv.second.path >= 0) ++resolved;
    }
    std::printf("area %d scx %s shown %zu started %zu actor_programs %d resolved %d\n",
                area, s.scxName().c_str(), s.shown().size(), seen.size(), actorProgs, resolved);
    for (const auto& kv : seen) {
        const auto& st = kv.second;
        // the name last: Lahoreh's carry spaces ("Kiss 01F")
        std::printf("program 0x%04x how %s actor %d clip %d path %d name %s\n",
                    st.object & 0xFFFF, st.how.c_str(), st.actor, st.clip, st.path, st.name.c_str());
    }
    return 0;
}
