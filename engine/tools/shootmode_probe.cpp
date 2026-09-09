// SPDX-License-Identifier: GPL-3.0-or-later
// SHOOT MODE, entered by a SHIPPED SCRIPT - what ops 80 and 81 decide.
//
//     shootmode_probe <gamedata> <tables>
//
// AREA 59 is the Anekbah Shooting gallery and its zone record 24's ENTER
// script ends `shoot.begin -1` (offset 0x34ea, `tools/script_dump.py AREA
// 59`). That is the whole of the mode's entry as a replica can make it: the
// operand names a weapon OBJECT, `Weapon_SlotForObject` turns it into a slot
// out of `IAM\GLOBAL +42`, and -1 - which 27 of the 30 shipped sites pass -
// falls to slot 11, the `Gun Waver`.
//
// Printed as one `key value` line each, all of them the port's answer to
// something the engine's own code settles (`engine/src/actor/shootmode.h`).
#include "platform/datafs.h"
#include "script/area.h"
#include "script/gamestate.h"

#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: shootmode_probe <gamedata> <tables>\n");
        return 2;
    }
    const std::string fr = argv[1], tb = argv[2];
    const auto table = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    if (!table.valid()) { std::fprintf(stderr, "no opcode table\n"); return 1; }
    const std::string iam = fr + "/IAM";
    auto state = omk::GameState::fromFile(iam + "/START");
    omk::Session s(iam, state, table);
    s.loadArea(59);

    const auto& slot = s.residentSlot(s.shownSlot());
    std::printf("area %d  chunk %zu bytes\n", slot.area, slot.areaChunk.size());
    std::printf("before  active %d  weapon %d  hud %d  library %s\n",
                s.shootMode().active(), s.shootMode().weaponSlot(),
                s.shootMode().hudScreen(), s.shootMode().library());

    // record 24's ENTER script, the gallery's first "épreuve"
    const std::int32_t scripts[3] = {0x34ea, 0, 0};
    const int ctx = s.newContext(s.shownSlot(), slot.areaChunk, scripts, -1, 59);
    if (ctx < 0) { std::fprintf(stderr, "no context\n"); return 1; }
    s.queueAction(ctx, 1);
    for (int f = 0; f < 60; ++f) s.frame();

    const auto& m = s.shootMode();
    std::printf("after   active %d  weapon %d  object %d  hud %d  library %s\n",
                m.active(), m.weaponSlot(), m.weaponObject(), m.hudScreen(), m.library());
    std::printf("constants  state %d/%d  group %d  camera %d  scheme %d  records %dx%d\n",
                omk::ShootMode::kPlayerState, omk::ShootMode::kPlayerStateOut,
                omk::ShootMode::kChannelGroup, omk::ShootMode::kCameraMode,
                omk::ShootMode::kInputScheme, omk::ShootMode::kRecordCount,
                omk::ShootMode::kRecordBytes);
    for (const auto& e : m.log())
        std::printf("log  %-6s weapon %d hud %d  %s\n",
                    e.enter ? "ENTER" : "LEAVE", e.weapon, e.hud, e.why);

    // ...and the weapon table the slot came out of
    for (int sl = omk::kWeaponSlotFirst;
         sl < omk::kWeaponSlotFirst + omk::kWeaponSlotCount; ++sl) {
        const int o = m.objectForSlot(sl);
        if (o != -1) std::printf("weapon slot %2d  object %d\n", sl, o);
    }

    // the exit, both arms: keep the gun, then drop it
    omk::ShootMode& mm = s.shootModeMutable();
    mm.end(0);
    std::printf("end(0)  active %d  weapon %d\n", mm.active(), mm.weaponSlot());
    mm.begin(-1);
    mm.end(1);
    std::printf("end(1)  active %d  weapon %d\n", mm.active(), mm.weaponSlot());
    return 0;
}
