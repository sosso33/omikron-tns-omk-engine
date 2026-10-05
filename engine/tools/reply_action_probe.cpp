// SPDX-License-Identifier: GPL-3.0-or-later
// A REPLY'S ACTION RUNS AS A CONTEXT (`todo/drift-audit.md` S2):
// `Game_HandleEvent` case 59 creates a context in the active slot, runs it
// once through `Script_Execute` - every handler, the zone re-registration
// among them - and frees it. The port ran it in a bare `Interpreter`.
//
//     reply_action_probe <gamedata> <tables>
//
// On the shipped data: SCENE 19 over its area, zone 990 (its record 0), whose
// script launches dialog 197 'Namtar/Base1/2'; node 0's reply 0 does
// `zone.disable 990`, `zone.enable 991`. The probe plays the conversation -
// every line skipped, reply 0 at every menu - and prints:
//
//   setup    the area, whether 990 is live and 991 is not before
//   dialog   the conversation that opened, how many lines and menus it took,
//            and how many reply actions the Session ran as contexts
//   zones    after it closes: the two save bits, and whether each zone is in
//            the LIVE list - the snapshot `Zones_RegisterAll` rebuilds, which
//            is what a bare interpreter never touched
#include "formats/iam.h"
#include "script/area.h"
#include "script/dialogue.h"
#include "script/gamestate.h"
#include "script/script.h"
#include "script/zones.h"

#include <cstdio>
#include <string>

namespace {

float arcCentreDegrees(const omk::Zone& z) {
    return static_cast<float>(static_cast<int>(
        static_cast<double>(z.arcMid) * omk::kZoneArcToDegrees));
}

bool isLive(const omk::Session& s, int id) {
    for (const auto& z : s.zones().registered())
        if (z.zone.id == id) return true;
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reply_action_probe <gamedata> <tables>\n");
        return 2;
    }
    const std::string fr = argv[1], tb = argv[2];
    const auto table = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    if (!table.valid()) return 1;
    const std::string iam = fr + "/IAM";

    const auto& map = omk::sceneAreaMap(iam, table);
    const auto it = map.find(19);
    if (it == map.end()) { std::printf("setup scene 19 has no area\n"); return 1; }
    const int area = it->second;

    auto state = omk::GameState::fromFile(iam + "/START");
    state.setBit(omk::StateArray::ZoneState, 990, 1);
    state.setBit(omk::StateArray::ZoneState, 991, 0);
    omk::Session s(iam, state, table);
    s.attachDialogue(fr + "/MORPH");
    s.loadArea(area);
    s.sceneLoad(area, 19);
    // the chunks' own startup scripts are not the subject
    for (int k = 0; k < 2; ++k) {
        if (s.residentSlot(k).areaCtx >= 0) s.freeContext(s.residentSlot(k).areaCtx);
        if (s.residentSlot(k).sceneCtx >= 0) s.freeContext(s.residentSlot(k).sceneCtx);
    }
    s.frame();
    std::printf("setup area %d live990 %d live991 %d\n", area,
                isLive(s, 990) ? 1 : 0, isLive(s, 991) ? 1 : 0);

    const auto* z = s.zones().resolve(990);
    if (!z) { std::printf("setup zone 990 NOT RESIDENT\n"); return 1; }
    double c[3];
    z->zone.centre(c);
    const float p[3] = {static_cast<float>(c[0]), static_cast<float>(c[1]),
                        static_cast<float>(c[2])};
    s.setPlayerPosition(p, arcCentreDegrees(z->zone));

    int opened = -1, lines = 0, menus = 0;
    for (int f = 0; f < 3000; ++f) {
        s.frame();
        if (opened < 0 && !s.dialogOpen() && f % 20 == 2) s.pressAction();
        if (!s.dialogOpen()) {
            if (opened >= 0) break;                         // it closed
            continue;
        }
        const auto& d = s.dialogue();
        if (opened < 0) opened = d.conversation().id;
        if (d.phase() == omk::DialogPhase::Speaking) { s.dialogNext(); ++lines; }
        else if (d.phase() == omk::DialogPhase::Menu) { s.dialogChoose(0); ++menus; }
    }
    // step the hand-back and whatever the zone's script does after
    for (int f = 0; f < 5; ++f) s.frame();
    std::printf("dialog opened %d lines %d menus %d reply_actions %ld\n",
                opened, lines, menus, s.replyActionsRun());
    std::printf("zones bit990 %d bit991 %d live990 %d live991 %d\n",
                state.bit(omk::StateArray::ZoneState, 990),
                state.bit(omk::StateArray::ZoneState, 991),
                isLive(s, 990) ? 1 : 0, isLive(s, 991) ? 1 : 0);
    return 0;
}
