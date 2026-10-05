// SPDX-License-Identifier: GPL-3.0-or-later
// WHICH HEIGHTS A ZONE IS FOUND FROM (`todo/drift-audit.md` S10).
//
//     zone_box_probe <gamedata> <tables>
//
// `Actor_ScanZones` asks a sweep-and-prune index for the zones whose box
// overlaps the actor's in all three axes: the zone's is its quad with 19.685
// added ABOVE it (`Zone_Add`, `flt_52B90C`), the actor's his pelvis plus and
// minus his root mesh's radius (`sub_431D40`, node +88). The port used a band
// of one metre around the quad instead. AREA 50's zone 1040 ('Elevateur
// Bas'), the player stood at its centre with his FEET at five heights from
// the quad's y - Y grows downward, so a positive offset puts the quad BELOW
// his feet - and one line each: `offset <dy> touched <0|1>`.
#include "formats/iam.h"
#include "script/area.h"
#include "script/gamestate.h"
#include "script/script.h"
#include "script/zones.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: zone_box_probe <gamedata> <tables>\n");
        return 2;
    }
    const std::string fr = argv[1], tb = argv[2];
    const auto table = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    if (!table.valid()) return 1;
    const std::string iam = fr + "/IAM";
    auto state = omk::GameState::fromFile(iam + "/START");
    state.setBit(omk::StateArray::ZoneState, 1040, 1);
    omk::Session s(iam, state, table);
    s.loadArea(50);
    for (int k = 0; k < 2; ++k) {
        if (s.residentSlot(k).areaCtx >= 0) s.freeContext(s.residentSlot(k).areaCtx);
        if (s.residentSlot(k).sceneCtx >= 0) s.freeContext(s.residentSlot(k).sceneCtx);
    }
    const auto* z = s.zones().resolve(1040);
    if (!z) { std::printf("zone 1040 NOT RESIDENT\n"); return 1; }
    double c[3];
    z->zone.centre(c);
    const float facing = static_cast<float>(static_cast<int>(
        static_cast<double>(z->zone.arcMid) * omk::kZoneArcToDegrees));
    for (const double dy : {0.0, 25.0, -60.0, 40.0, -100.0}) {
        // away first, so each height is a fresh touch
        const float away[3] = {static_cast<float>(c[0] + 1e6), static_cast<float>(c[1]),
                               static_cast<float>(c[2] + 1e6)};
        s.setPlayerPosition(away, facing);
        s.frame();
        const auto before = s.zones().touches();
        // the quad's y is `c[1]`; his FEET at quad - dy
        const float at[3] = {static_cast<float>(c[0]), static_cast<float>(c[1] - dy),
                             static_cast<float>(c[2])};
        s.setPlayerPosition(at, facing);
        s.frame();
        std::printf("offset %.0f touched %d\n", dy, s.zones().touches() > before ? 1 : 0);
    }
    return 0;
}
