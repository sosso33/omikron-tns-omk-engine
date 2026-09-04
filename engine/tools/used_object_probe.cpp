// SPDX-License-Identifier: GPL-3.0-or-later
// USING AN OBJECT ON THE WORLD - the sneak's `Utiliser` reaching a zone.
//
//     used_object_probe <gamedata> <tables>
//
// The chain, and every link is read rather than assumed:
//
//   1. `Game_HandleEvent` case 35's non-consumable arm (01_file.c:1546)
//      allocates a `word_4E6CA0` slot for the object's id, drops it from
//      object list 0, and returns result 1 with that slot.
//   2. `sub_42B470` hands the slot to `sub_41C490`, which writes
//      `player[+0xA4]`. `Actor_HeldObjectSlot` (0x0041A350) reads it back and
//      divides by 96, so it returns the slot again.
//   3. `Script_Pump` case 2 will not queue an activate blind while the player
//      holds something: `sub_406180` runs the activate script with
//      `Script_RunToOpcode75` (0x00406120) and queues only if it reaches
//      opcode **75**, `var.set.used_object`. Both of that function's exits
//      restore the caller's pc, so the dry run leaves nothing behind.
//   4. Opcode 75 then writes `word_4E6CA0[slot]` into the named variable and
//      the script branches on it.
//
// AREA 229 (HALL27) is where it is visible: zone records 4 and 6 are the two
// lift calls to Kay'l's apartment, and their activate scripts read
//
//     var.set.used_object 13      ; VARIABLES[13] = 'ObjetUtilisé'
//     push.i8 6 / push.var 13 / cmp.eq   -> area.goto 237, 'Appart Kayl'
//     push.i8 255 / push.var 13 / cmp.eq -> media.play 170 'Asc Sans Clé'
//
// Object 6 is the apartment key. So this asserts the three outcomes the
// player can produce, and the contrast that makes the probe mean anything:
// a zone whose activate script never mentions 75 must NOT queue while
// something is held.
//
// One line per fact, `key ...`:
//   hand      the slot, the id read back through the hook, list membership
//   probe     which of the area's activate scripts reach opcode 75
//   used      the variable the script sees, and where it branches
#include "formats/iam.h"
#include "platform/datafs.h"
#include "script/area.h"
#include "script/gamestate.h"
#include "script/interp.h"
#include "script/inventory.h"
#include "script/script.h"
#include "script/zones.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

float arcCentre(const omk::Zone& z) {
    return static_cast<float>(static_cast<int>(
        static_cast<double>(z.arcMid) * omk::kZoneArcToDegrees));
}

void stand(omk::Session& s, const omk::LiveZone* z) {
    double c[3];
    z->zone.centre(c);
    const float p[3] = {static_cast<float>(c[0]), static_cast<float>(c[1]),
                        static_cast<float>(c[2])};
    s.setPlayerPosition(p, arcCentre(z->zone));
}

// Does this script reach opcode 75 before it ends? The probe the pump runs.
bool reaches75(omk::GameState& st, const omk::OpcodeTable& t,
               std::span<const std::byte> code, std::size_t at) {
    if (code.empty() || at == 0 || at >= code.size()) return false;
    omk::Interpreter vm(st, t);
    vm.setStopAtOpcode(75);
    return vm.run(code, at).status == omk::RunStatus::StoppedAtOpcode;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: used_object_probe <gamedata> <tables>\n");
        return 2;
    }
    const std::string fr = argv[1], tb = argv[2];
    const auto table = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    if (!table.valid()) return 1;
    const std::string iam = fr + "/IAM";

    // ---- 1/2: the hand ---------------------------------------------------
    {
        auto state = omk::GameState::fromFile(iam + "/START");
        omk::Session s(iam, state, table);
        s.loadArea(229);
        s.frame();
        state.debugPutObject(0, 6);                  // the key, in the bag
        const auto before = omk::objectList(state, omk::ObjectList::Carried);
        const int slot = s.useObject(6);
        const auto after = omk::objectList(state, omk::ObjectList::Carried);
        int carriedBefore = 0, carriedAfter = 0;
        for (const int id : before) if (id == 6) ++carriedBefore;
        for (const int id : after)  if (id == 6) ++carriedAfter;
        std::printf("hand slot %d held %d id_in_slot %d in_bag_before %d "
                    "in_bag_after %d\n",
                    slot, s.heldSlotOf(-1), s.objectSlotId(slot),
                    carriedBefore, carriedAfter);
    }

    // ---- 1b: TAKING one off the floor, `Game_HandleEvent` case 10 --------
    // MDACTION finds a prop, MDGETOBJ holds it, MDPUTSNK banks it - and the
    // bank is case 10, which inserts at the FRONT of list 0 and clears the
    // prop's state bit 0. Object 173 'Note sur les anneaux' is a prop of
    // AREA 237, Kay'l's apartment, and a player took it in play.
    {
        auto state = omk::GameState::fromFile(iam + "/START");
        omk::Session s(iam, state, table);
        // `objectName`/`objectKind` read IAM\OBJECT through `dataRoot_`,
        // which ONLY `loadTraffic` sets - so without this the kind reads -1
        // and `Inventory_Insert`'s gate is never exercised.
        s.loadTraffic(fr);
        s.loadArea(237);
        s.frame();
        const auto before = omk::objectList(state, omk::ObjectList::Carried);
        const bool took = s.takeObject(173);
        s.bankHeldObject(173);
        const auto after = omk::objectList(state, omk::ObjectList::Carried);
        int front = after.empty() ? -1 : after[0];
        std::printf("take object 173 kind %d took %d carried %zu -> %zu front %d "
                    "held_after %d\n",
                    s.objectKind(173), took ? 1 : 0, before.size(), after.size(),
                    front, s.heldSlotOf(-1));
    }

    // ---- 3: which of HALL27's activate scripts ask what is held -----------
    {
        auto state = omk::GameState::fromFile(iam + "/START");
        omk::Session s(iam, state, table);
        s.loadArea(229);
        s.frame();
        int asks = 0, silent = 0, total = 0;
        std::vector<std::int16_t> asking;
        for (const auto& z : s.zones().all()) {
            if (!z.zone.scripts[1]) continue;
            ++total;
            if (reaches75(state, table, z.code,
                          static_cast<std::size_t>(z.zone.scripts[1]))) {
                ++asks; asking.push_back(z.zone.id);
            } else {
                ++silent;
            }
        }
        std::printf("probe area 229 activates %d ask75 %d silent %d zones",
                    total, asks, silent);
        for (const auto id : asking) std::printf(" %d", static_cast<int>(id));
        std::printf("\n");
    }

    // ---- 4: the three outcomes at one of those zones ----------------------
    // Each on its own GameState, so one run's save bits do not spend another's.
    for (int arm = 0; arm < 3; ++arm) {
        auto state = omk::GameState::fromFile(iam + "/START");
        omk::Session s(iam, state, table);
        s.loadAnnounceMap(tb + "/vm_announce.json");
        s.loadArea(229);
        s.frame();
        // the first zone whose activate script reaches 75
        const omk::LiveZone* lift = nullptr;
        for (const auto& z : s.zones().all())
            if (z.zone.scripts[1] &&
                reaches75(state, table, z.code,
                          static_cast<std::size_t>(z.zone.scripts[1]))) {
                lift = s.zones().resolve(z.zone.id);
                if (lift) break;
            }
        if (!lift) { std::printf("used arm %d NO LIFT ZONE\n", arm); continue; }

        const char* what = "empty";
        if (arm == 1) { state.debugPutObject(0, 6); s.useObject(6); what = "key"; }
        if (arm == 2) { state.debugPutObject(0, 5); s.useObject(5); what = "wrong"; }

        stand(s, lift);
        const std::size_t from = s.announced().size();
        s.frame();                                   // arm
        for (int f = 0; f < 8; ++f) { s.pressAction(); s.frame(); }
        for (int f = 0; f < 40; ++f) s.frame();

        long gotoArea = -1, voice = -1;
        for (std::size_t i = from; i < s.announced().size(); ++i) {
            const auto& a = s.announced()[i];
            if (a.domain == "AREAS"   && a.value == 237) gotoArea = a.value;
            if (a.domain == "OBJECTS" && (a.value == 170 || a.value == 112))
                voice = a.value;
        }
        std::printf("used arm %s zone %d var13 %d goto237 %s voice %ld\n",
                    what, static_cast<int>(lift->zone.id), state.var(13),
                    gotoArea == 237 ? "yes" : "no", voice);
    }
    return 0;
}
