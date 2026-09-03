// SPDX-License-Identifier: GPL-3.0-or-later
// The game-state FOUNDATIONS the inventory, prop and timer opcodes need -
// object lists, the 2-bit prop state, the clock and the script timer - run
// over `IAM\START` rather than described.
//
//     state_probe <gamedata> <out.bin>
//
// Every number printed is one the port could get wrong: the list operations
// are `ObjectList_SetCapacity` / `ObjectList_InsertFront` / the four inventory
// handlers, the prop state is `ObjectState_Get` including its sign extension,
// and the timer is the five entry points around `Timer_Elapsed`, whose two
// opcode names in `tables/vm_opcodes.json` are inverted (gamestate.h says how
// that was settled).
//
// out.bin: 65 int32, in the order they are printed.
#include "platform/datafs.h"
#include "script/gamestate.h"
#include "script/savefile.h"   // kNewGameTime - Game_NewGame's clock

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: state_probe <gamedata> <out.bin>\n");
        return 2;
    }
    const std::string root = argv[1];
    const auto start = omk::GameState::fromFile(root + "/IAM/START");
    if (start.imageSize() == 0) {
        std::fprintf(stderr, "state_probe: %s/IAM/START did not read\n", root.c_str());
        return 2;
    }

    std::vector<std::int32_t> out;
    const auto say = [&out](const char* what, std::int32_t v) {
        std::printf("%-46s %d\n", what, v);
        out.push_back(v);
    };

    // ------------------------------------------------- what IAM\START ships
    std::printf("-- IAM\\START (%zu bytes)\n", start.imageSize());
    say("list 0 (carried) count", start.listCount(0));
    say("  [0]", start.listAt(0, 0));
    say("  [1]", start.listAt(0, 1));
    say("list 1 (second) count", start.listCount(1));
    say("list 2 (memos) count", start.listCount(2));

    // --------------------------------------------------- add: at the FRONT
    std::printf("-- listAdd inserts at the front (ObjectList_InsertFront)\n");
    auto s = start;
    say("add 300 to list 0", s.listAdd(0, 300));
    say("  count", s.listCount(0));
    say("  [0] is the NEW id", s.listAt(0, 0));
    say("  [1] is the old front", s.listAt(0, 1));
    say("  [2]", s.listAt(0, 2));
    say("  listHas(0, 300)", s.listHas(0, 300));
    say("  listHas(0, 301)", s.listHas(0, 301));

    // -------------------------------------- the duplicate rule is per LIST
    std::printf("-- lists 2 and 3 refuse a duplicate; 0 and 1 do not\n");
    say("list 0 refuses duplicates?",
        omk::GameState::listRefusesDuplicates(0));
    say("list 2 refuses duplicates?",
        omk::GameState::listRefusesDuplicates(2));
    say("add 300 again to list 0 (dup allowed)",
        s.listAdd(0, 300, omk::GameState::listRefusesDuplicates(0)));
    say("  count", s.listCount(0));
    auto m = start;
    say("add 500 to list 2",
        m.listAdd(2, 500, omk::GameState::listRefusesDuplicates(2)));
    say("add 500 to list 2 again (refused)",
        m.listAdd(2, 500, omk::GameState::listRefusesDuplicates(2)));
    say("  count", m.listCount(2));

    // -------------------------------------------- the full list refuses
    std::printf("-- a full list refuses (capacity == count)\n");
    auto f = start;
    f.listClear(2);
    for (int i = 0; i < omk::GameState::kListCapacity[2]; ++i) f.listAdd(2, 600 + i);
    say("list 2 filled to capacity", f.listCount(2));
    say("one more (refused)", f.listAdd(2, 700));
    say("  count unchanged", f.listCount(2));
    say("  nothing of 700 landed", f.listHas(2, 700));
    // and the front-insert order: the last one added is index 0
    say("  [0] is the last added", f.listAt(2, 0));

    // -------------------------------------------- remove shifts down
    std::printf("-- listRemove shifts the tail down and re-terminates\n");
    say("remove 604 from the middle", f.listRemove(2, 604));
    say("  count", f.listCount(2));
    say("  last slot is the terminator",
        f.listAt(2, omk::GameState::kListCapacity[2] - 1));
    say("  still holds 605", f.listHas(2, 605));
    say("remove an id it does not hold", f.listRemove(2, 999));
    auto r = start;
    for (int i = 0; i < 3; ++i) r.listAdd(0, 42);
    say("removeAll of 3 copies", r.listRemoveAll(0, 42));
    say("  carried is back to the shipped count", r.listCount(0));
    f.listClear(2);
    say("listClear leaves an empty list", f.listCount(2));

    // -------------------------------------------- the 2-bit prop state
    std::printf("-- prop state: 2 bits, and ObjectState_Get sign-extends\n");
    auto p = start;
    const int base = 100;                       // 100..103 share one byte
    for (int i = 0; i < 4; ++i) p.setPropState(base + i, 0);
    p.setPropState(base + 1, 3);
    p.setPropState(base + 3, 3);
    for (int i = 0; i < 4; ++i) say("  bits", p.propStateBits(base + i));
    // index % 4 == 3 reads -1 for 3 and -2 for 2 - the mask is movsx'd
    say("  propState(103) - SIGN EXTENDED", p.propState(base + 3));
    p.setPropState(base + 3, 2);
    say("  propState(103) with state 2", p.propState(base + 3));
    say("  propState(101) - unaffected", p.propState(base + 1));
    say("  neighbour 100 undisturbed", p.propStateBits(base));
    say("  neighbour 102 undisturbed", p.propStateBits(base + 2));

    // -------------------------------------------- the clock and the timer
    std::printf("-- the clock: 166 units per 5 frame units (a millisecond)\n");
    auto t = start;
    t.setClock(omk::kNewGameTime);
    for (int i = 0; i < 300; ++i) t.clockTick(1.0f);
    say("300 frames of clock", t.clock() - omk::kNewGameTime);
    t.setClock(omk::GameState::kClockUnitsPerDay - 100);
    t.setClockDay(52);
    for (int i = 0; i < 6; ++i) t.clockTick(1.0f);
    say("the day rolls over", t.clockDay());

    std::printf("-- the timer: 111 STOPS, 112 STARTS (mechanism, not the table)\n");
    t.setClock(1000);
    say("flags at rest", t.timerFlags());
    std::int32_t e = -1;
    say("elapsed while idle: returns", t.timerElapsed(e));
    say("  and writes", e);
    say("mode 12 while stopped", t.timerMode(12));
    say("  flags (mode | 1)", t.timerFlags());
    say("set 900 s while stopped", t.timerSet(900 * 1000));
    say("op 112 starts it", t.timerStart());
    say("  flags (stopped and expired cleared)", t.timerFlags());
    say("op 112 again while running (refused)", t.timerStart());
    say("set while running (refused)", t.timerSet(1));
    t.setClock(1000 + 12345);
    say("elapsed while running: returns", t.timerElapsed(e));
    say("  and writes", e);
    say("no expiry yet", t.timerCheckExpiry());
    t.setClock(1000 + 900 * 1000 + 1);
    say("expiry fires once", t.timerCheckExpiry());
    say("  and not twice", t.timerCheckExpiry());
    say("  flags carry 0x10", t.timerFlags());
    say("elapsed once expired: returns", t.timerElapsed(e));
    say("  frozen at the value", e);
    say("op 111 stops it", t.timerStop());
    say("  flags", t.timerFlags());
    say("op 111 again (refused)", t.timerStop());

    // the shipped block is untouched by any of it
    say("IAM\\START still ships 2 carried", start.listCount(0));

    if (!omk::safeOutputPath(argv[2])) return 2;
    std::ofstream o(argv[2], std::ios::binary);
    for (std::int32_t v : out) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) {
            const auto b = static_cast<char>((u >> (8 * k)) & 0xFF);
            o.write(&b, 1);
        }
    }
    std::printf("-- %zu values\n", out.size());
    return 0;
}
