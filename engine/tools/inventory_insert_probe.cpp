// SPDX-License-Identifier: GPL-3.0-or-later
// `Inventory_Insert` (0x004098E0) WHOLE - money, rings and AMMUNITION.
//
//     inventory_insert_probe <gamedata> <vm_opcodes.json> <save>
//
// From slot 0 of `traces/save-appart.bin`: IAM\START's player record holds
// 0xFFFF in the ammunition and the money (the new game fills them later),
// which would show every sum as the clamp.
//
// Every way an object enters a list - op 50 `inventory.add`, the take
// (`Game_HandleEvent` case 10), the multiplan's withdrawal (case 8) and the
// shop's purchase (case 38) - runs `Inventory_Insert(rec, list, player)`
// before `ObjectList_InsertFront`, and inserts only when it answers 1. Its
// ladder on the object's kind (the record's +2), over the rows of `list`:
//
//   12 seteks, 13 rings  -> `Object_ApplyEffect`, 0 (consumed)
//   7..11 a magazine     -> a gun of kind - 5 in the list: the effect, 0;
//                           none: 1 (a row, kept for a later gun)
//   2..6 a gun           -> a gun of the SAME kind: the effect (quantity 0),
//                           0; none: every magazine of kind + 5 in the list
//                           applied and REMOVED, then 1 (the gun's row)
//
// `Object_ApplyEffect` (0x00409780) puts a magazine's +12 quantity into
// property 35, the ammunition array at record +260, slot kind - 7 (a gun's
// slot is kind - 2: the same one). Until 2026-11-07 the port announced that
// write and never made it ("NOT applied: no writer for 35"), so a magazine
// taken or bought added no rounds; op 50 and the shop skipped the ladder
// entirely.
//
// One line per case, `key value...`, every number read back from the state.
// The shipped objects used (IAM\OBJECT): 189 Double-Waver (kind 2), 766
// another Double-waver (kind 2), 193 and 200 'Munitions double-waver' (kind 7,
// 50 rounds each), 194 'Munitions octogun' (kind 8, 30), 164 '10 seteks'
// (kind 12, 10).
#include "platform/datafs.h"
#include "script/area.h"
#include "script/gamestate.h"
#include "script/interp.h"
#include "script/inventory.h"
#include "script/props.h"
#include "script/savefile.h"
#include "script/script.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int ammo(const omk::GameState& st, int slot) {
    std::int32_t v = -1;
    omk::readAmmoSlot(st.raw().subspan(static_cast<std::size_t>(omk::GameState::kPlayerRecord),
                                       static_cast<std::size_t>(omk::GameState::kPlayerRecordSize)),
                      slot, v);
    return static_cast<int>(v);
}

std::string list0(const omk::GameState& st) {
    std::string s;
    for (const int id : omk::objectList(st, omk::ObjectList::Carried))
        s += (s.empty() ? "" : ",") + std::to_string(id);
    return s.empty() ? "-" : s;
}

// The world's half of op 50, as the Session's own hooks forward it.
struct Forward : omk::WorldHooks {
    omk::Session* s = nullptr;
    bool insertObject(int list, int id) override { return s->insertObject(list, id); }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: inventory_insert_probe <gamedata> <vm_opcodes.json> <save>\n");
        return 2;
    }
    const auto saved = omk::readSaveSlotAt(argv[3], 0);
    if (!saved) { std::fprintf(stderr, "no slot 0 in %s\n", argv[3]); return 1; }
    const std::string fr = argv[1];
    const auto table = omk::OpcodeTable::loadJson(argv[2]);
    if (!table.valid()) return 1;
    const std::string iam = fr + "/IAM";

    // The save's state afresh, a Session that can read IAM\OBJECT, list 0
    // emptied.
    struct World {
        omk::GameState st;
        omk::Session s;
        World(const omk::GameState& from, const std::string& iam, const std::string& fr,
              const omk::OpcodeTable& t)
            : st(from), s(iam, st, t) {
            s.setDataRoot(fr);
            while (st.listCount(0) > 0) st.listRemove(0, st.listAt(0, 0));
        }
    };

    // 1. A magazine with NO gun: a row, no rounds.
    {
        World w(saved->state, iam, fr, table);
        const int a0 = ammo(w.st, 0);
        const bool row = w.s.insertObject(0, 193);
        std::printf("magazine-alone row %d ammo0 %d -> %d\n", row ? 1 : 0, a0, ammo(w.st, 0));
    }
    // 2. A magazine for a CARRIED gun: consumed, its 50 rounds into slot 0.
    {
        World w(saved->state, iam, fr, table);
        w.st.debugPutObject(0, 189);
        const int a0 = ammo(w.st, 0);
        const bool row = w.s.insertObject(0, 193);
        std::printf("magazine-for-gun row %d ammo0 %d -> %d list %s\n", row ? 1 : 0, a0,
                    ammo(w.st, 0), list0(w.st).c_str());
    }
    // 3. A NEW gun with magazines already carried: both of its kind taken in
    //    (2 x 50 into slot 0) and removed, the octogun's left alone; the gun
    //    wants its row.
    {
        World w(saved->state, iam, fr, table);
        w.st.debugPutObject(0, 193);
        w.st.debugPutObject(0, 194);
        w.st.debugPutObject(0, 200);
        const int a0 = ammo(w.st, 0), a1 = ammo(w.st, 1);
        const bool row = w.s.insertObject(0, 189);
        std::printf("gun-takes-magazines row %d ammo0 %d -> %d ammo1 %d -> %d list %s\n",
                    row ? 1 : 0, a0, ammo(w.st, 0), a1, ammo(w.st, 1), list0(w.st).c_str());
    }
    // 4. A second gun of a kind already carried: consumed, nothing added.
    {
        World w(saved->state, iam, fr, table);
        w.st.debugPutObject(0, 766);
        const int a0 = ammo(w.st, 0);
        const bool row = w.s.insertObject(0, 189);
        std::printf("second-gun row %d ammo0 %d -> %d list %s\n", row ? 1 : 0, a0,
                    ammo(w.st, 0), list0(w.st).c_str());
    }
    // 5. Seteks: consumed into Argent.
    {
        World w(saved->state, iam, fr, table);
        const int m = w.st.money();
        const bool row = w.s.insertObject(0, 164);
        std::printf("seteks row %d money %d -> %d\n", row ? 1 : 0, m, w.st.money());
    }
    // 6. Op 50 `inventory.add 0, 193` with the gun carried, through the hook:
    //    no row, the rounds. And the same op with no world (no hooks): a row,
    //    as an interpreter alone has always done.
    {
        World w(saved->state, iam, fr, table);
        w.st.debugPutObject(0, 189);
        Forward f;
        f.s = &w.s;
        std::vector<std::byte> code = {std::byte{50}, std::byte{0}, std::byte{0},
                                       std::byte{193}, std::byte{0}, std::byte{3}};
        const int a0 = ammo(w.st, 0);
        omk::Interpreter vm(w.st, table);
        vm.setHooks(&f);
        vm.run(code, 0);
        std::printf("op50-world ammo0 %d -> %d list %s\n", a0, ammo(w.st, 0),
                    list0(w.st).c_str());
        omk::Interpreter bare(w.st, table);
        bare.run(code, 0);
        std::printf("op50-bare ammo0 %d list %s\n", ammo(w.st, 0), list0(w.st).c_str());
    }
    return 0;
}
