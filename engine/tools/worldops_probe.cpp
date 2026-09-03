// SPDX-License-Identifier: GPL-3.0-or-later
// THE WORLD OPCODES, PROBED - hand-built bytecode through `script/interp`
// against `IAM\START`, a real AREA chunk and a test double of the engine's
// actor table, each line one rule of one handler that the corpus sweep cannot
// see because its reference stubs the same opcodes.
//
//     worldops_probe <gamedata> <tables/vm_opcodes.json>
//
// Written 2026-09-02 against the interpreter as it stood - every one of these
// opcodes a stub - and its first run FAILED every line but the `off.*` and
// `*.untouched` ones, which is the point (todo/pending/T12.md carries that
// output). Each rule is read from the handler named beside it:
//
//   lists     49 (0x40A440) scans list field0 for field1 -> 1/0; 50 (0x40A4D0)
//             refuses a held id on lists 2 and 3 ONLY (`cmp edi,3 / cmp edi,2`)
//             and inserts at the FRONT; 51 (0x40A5A0) removes one; 52
//             (0x40A6A0) repeats it - and its id == -1 arm compares every
//             entry against the literal 0FFFFh, so it can never remove
//             anything; 128 (0x405810) moves the first n of one list to the
//             front of another and writes n.
//   timer     111 (0x405350 -> loc_41E2B0) STOPS, 112 (0x405360 -> loc_41E2D0)
//             STARTS, 113 x1000 under Timer_SetValue, 114 Timer_SetMode,
//             115 Timer_Elapsed's three-way read (value | 0 | clock - start),
//             110 (0x405340) resets.
//   player    91 (0x404530) is Actor_IdBySlot(Actor_Player()) - the DB's +332;
//             93 (0x404790) / 86 (0x404230) on actor -1 or the DB's own id go
//             to the DB record at +60, with Actor_SetProperty's UNSIGNED
//             clamp (`cmp esi,0C8h ; jbe`): -5 -> 200.
//   hooks     75 (0x40AC90) writes word_4E6CA0[slot] or -1; 67 (0x40A9D0)
//             attaches the prop's slot and writes the record's +270 unless it
//             already holds the id; 69 (0x40AC20) drops and clears +270 only
//             when Actor_HeldObjectSlot is not -1; 76 (0x40ACF0) sets prop
//             bit 1 only when bit 0 is set; 68 (0x40AAF0) matches the held
//             SLOT against the record's +0 and takes the drop or the remove
//             arm; 98 (0x404DB0) places.
//
// Every line prints what the port did; the check reads the numbers.
#include "formats/iam.h"
#include "platform/datafs.h"
#include "script/gamestate.h"
#include "script/hooks.h"
#include "script/interp.h"
#include "script/props.h"
#include "script/script.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace {

using Code = std::vector<std::uint8_t>;

void i8(Code& c, int v)  { c.push_back(static_cast<std::uint8_t>(v & 0xFF)); }
void i16(Code& c, int v) { i8(c, v); i8(c, v >> 8); }

std::span<const std::byte> bytes(const Code& c) {
    return {reinterpret_cast<const std::byte*>(c.data()), c.size()};
}

std::int16_t i16at(std::span<const std::byte> b, std::size_t o) {
    if (o + 2 > b.size()) return 0;
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(b[o]) |
                                     static_cast<std::uint16_t>(b[o + 1]) << 8);
}

// The engine's side of the hooks, as a TEST DOUBLE: one real AREA chunk
// copied out (its actor and prop tables are the shipped ones), the 50 object
// slots' ids (`word_4E6CA0`), who holds which slot, and a log of the scene
// calls the handlers make. What the double does NOT do is any 3D.
struct TestHooks : omk::WorldHooks {
    std::vector<std::byte> chunk;              // AREA chunk, writable copy
    omk::ChunkKind kind = omk::ChunkKind::Area;
    std::array<int, 50> slotIds{};             // word_4E6CA0
    std::map<int, int> heldSlot;               // actor id -> slot; -1 = player
    std::vector<std::string> log;

    TestHooks() { slotIds.fill(-1); }

    std::span<std::byte> record(int actor) {
        const auto o = omk::findActorRecord(chunk, kind, actor);
        if (!o) return {};
        return std::span<std::byte>(chunk).subspan(*o, omk::kActorRecordSize);
    }
    // 86/93 go through the 20-byte object table FIRST (`Scene_FindObject
    // Record`) and read garbage when the id is not in it; the double refuses.
    bool getActorProperty(int actor, int property, std::int32_t& out) override {
        if (!omk::findCharacterRecord(chunk, kind, actor)) return false;
        const auto r = record(actor);
        return !r.empty() && omk::readActorProperty(r, property, out);
    }
    bool setActorProperty(int actor, int property, std::int32_t value) override {
        if (!omk::findCharacterRecord(chunk, kind, actor)) return false;
        const auto r = record(actor);
        return !r.empty() && omk::writeActorProperty(r, property, value);
    }
    int heldObjectField(int actor) override {
        const auto r = record(actor);
        return r.empty() ? -1 : omk::heldObjectOf(r);
    }
    void setHeldObjectField(int actor, int objectId) override {
        const auto r = record(actor);
        if (!r.empty()) omk::setHeldObjectOf(r, objectId);
    }
    int heldObjectSlot(int actor) override {
        const auto it = heldSlot.find(actor);
        return it == heldSlot.end() ? -1 : it->second;
    }
    int objectIdInSlot(int slot) override {
        return slot >= 0 && slot < 50 ? slotIds[static_cast<std::size_t>(slot)] : -1;
    }
    void clearObjectSlot(int slot) override {
        if (slot >= 0 && slot < 50) slotIds[static_cast<std::size_t>(slot)] = -1;
        log.push_back("clearslot " + std::to_string(slot));
    }
    void releaseObject(int actor, bool remove) override {
        heldSlot.erase(actor);
        log.push_back(std::string(remove ? "remove " : "drop ") + std::to_string(actor));
    }
    void holdObject(int actor, int slot) override {
        heldSlot[actor] = slot;
        log.push_back("hold " + std::to_string(actor) + " " + std::to_string(slot));
    }
    void showObject(int slot) override { log.push_back("show " + std::to_string(slot)); }
    void hideObject(int slot) override { log.push_back("hide " + std::to_string(slot)); }
    bool propBySlot(int slot, omk::PropRef& out) override {
        const auto p = omk::findPropBySlot(chunk, kind, slot);
        if (!p) return false;
        out = {p->slot, p->id, p->stateIndex};
        return true;
    }
    bool propById(int id, omk::PropRef& out) override {
        const auto p = omk::findPropById(chunk, kind, id);
        if (!p) return false;
        out = {p->slot, p->id, p->stateIndex};
        return true;
    }
    void placeObjectAt(int objectId, int address) override {
        log.push_back("place " + std::to_string(objectId) + " " + std::to_string(address));
    }
    int count(const char* prefix) const {
        int n = 0;
        for (const auto& l : log) n += l.rfind(prefix, 0) == 0;
        return n;
    }
};

omk::RunResult run(omk::GameState& s, const omk::OpcodeTable& t, const Code& c,
                   omk::WorldHooks* hooks = nullptr, bool worldWrites = true) {
    omk::Interpreter vm(s, t);
    vm.setRecordCalls(true);
    vm.setUiOpenSuspends(false);
    vm.setHooks(hooks);
    vm.setWorldWrites(worldWrites);
    return vm.run(bytes(c), 0);
}

int nvalues = 0;
void say(const char* label, long v) {
    std::printf("%-34s %ld\n", label, v);
    ++nvalues;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: worldops_probe <gamedata> <vm_opcodes.json>\n"); return 2; }
    const std::string fr = argv[1];
    const auto t = omk::OpcodeTable::loadJson(argv[2]);
    if (!t.valid()) { std::fprintf(stderr, "no opcode table\n"); return 1; }
    const std::string start = fr + "/IAM/START";

    // ================================================================ LISTS
    std::printf("-- the object lists (IAM\\START ships carried [6, 171], second 2, memos 0)\n");
    {
        auto s = omk::GameState::fromFile(start);
        Code c;
        i8(c, 49); i16(c, 0); i16(c, 6);   i16(c, 5);    // has_object list 0, id 6 -> var 5
        i8(c, 49); i16(c, 0); i16(c, 7);   i16(c, 6);    // id 7 -> var 6
        i8(c, 50); i16(c, 0); i16(c, 300);               // add 300 to list 0 twice
        i8(c, 50); i16(c, 0); i16(c, 300);
        i8(c, 50); i16(c, 2); i16(c, 500);               // add 500 to list 2 twice
        i8(c, 50); i16(c, 2); i16(c, 500);
        i8(c, 3);
        const auto r = run(s, t, c);
        say("has.6", s.var(5));
        say("has.7", s.var(6));
        say("add.dup.list0.count", s.listCount(0));
        say("add.dup.list0.front", s.listAt(0, 0));
        say("add.dup.list2.count", s.listCount(2));
        say("lists.recorded", static_cast<long>(r.calls.size()));

        Code d;
        i8(d, 51); i16(d, 0); i16(d, 300);               // remove one
        i8(d, 3);
        run(s, t, d);
        say("remove.one.count", s.listCount(0));
        Code e;
        i8(e, 52); i16(e, 0); i16(e, 300);               // remove all
        i8(e, 3);
        run(s, t, e);
        say("remove_all.count", s.listCount(0));
        Code f;
        i8(f, 52); i16(f, 0); i16(f, 0xFFFF);            // the id == -1 arm: dead
        i8(f, 3);
        run(s, t, f);
        say("remove_all.minus1.count", s.listCount(0));

        // inventory.transfer: from, to, count variable, result variable
        s.setVar(270, -1);                               // -1 = everything
        Code g;
        i8(g, 128); i16(g, 0); i16(g, 1); i16(g, 270); i16(g, 271);
        i8(g, 3);
        run(s, t, g);
        say("transfer.all.moved", s.var(271));
        say("transfer.all.list0.count", s.listCount(0));
        say("transfer.all.list1.count", s.listCount(1));
        say("transfer.all.list1.front", s.listAt(1, 0));   // moved one at a time to the FRONT
        s.setVar(270, 1);
        Code h;
        i8(h, 128); i16(h, 1); i16(h, 0); i16(h, 270); i16(h, 271);
        i8(h, 3);
        run(s, t, h);
        say("transfer.one.moved", s.var(271));
        say("transfer.one.list0.front", s.listAt(0, 0));
        say("transfer.one.list1.count", s.listCount(1));
        s.setVar(270, -3);
        run(s, t, h);
        say("transfer.neg.moved", s.var(271));           // written as computed, nothing moves
        say("transfer.neg.list1.count", s.listCount(1));
    }

    // ================================================================ TIMER
    std::printf("-- the timer: 111 STOPS, 112 STARTS, 115 reads three ways\n");
    {
        auto s = omk::GameState::fromFile(start);
        s.setClock(2000000);
        Code a; i8(a, 115); i16(a, 5); i8(a, 3);          // var.set.timer -> var 5
        s.setVar(5, 77);
        run(s, t, a);
        say("timer.fresh.elapsed", s.var(5));              // flags exactly 1 -> 0
        Code b;
        i8(b, 114); i16(b, 12);                            // mode 12
        i8(b, 113); i16(b, 900);                           // set 900 s
        i8(b, 112);                                        // START
        i8(b, 3);
        run(s, t, b);
        say("timer.flags.running", s.timerFlags());        // 12: stopped and expired cleared
        say("timer.value.ms", s.timerValue());             // 900000
        s.setClock(s.clock() + 3000);
        run(s, t, a);
        say("timer.running.elapsed", s.var(5));            // clock - start
        Code c2; i8(c2, 113); i16(c2, 5); i8(c2, 3);       // set while running: refused
        run(s, t, c2);
        say("timer.set.refused.running", s.timerValue());
        s.setClock(s.timerBase() + 900001);
        say("timer.expiry.fired", s.timerCheckExpiry() ? 1 : 0);
        run(s, t, a);
        say("timer.expired.elapsed", s.var(5));            // frozen at the value
        Code d; i8(d, 111); i8(d, 3);                      // STOP
        run(s, t, d);
        say("timer.stopped.flags", s.timerFlags());        // 29
        Code e; i8(e, 110); i8(e, 3);                      // reset
        run(s, t, e);
        run(s, t, a);
        say("timer.reset.elapsed", s.var(5));              // flags 1 again -> 0
    }

    // =============================================================== PLAYER
    std::printf("-- the player: identity, stat block and held field live in the DB\n");
    {
        auto s = omk::GameState::fromFile(start);
        Code a; i8(a, 91); i16(a, 5); i8(a, 3);            // var.set.player_id -> var 5
        s.setVar(5, 77);
        run(s, t, a);
        say("player_id.fresh", s.var(5));                  // START ships -1
        // `player.become 136`'s `rep movsd` lands +272 at DB +332
        {
            auto raw = s.rawMutable();
            raw[332] = static_cast<std::byte>(136); raw[333] = static_cast<std::byte>(0);
        }
        run(s, t, a);
        say("player_id.become", s.var(5));
        Code b;
        i8(b, 14); i16(b, 6); i8(b, 100);                  // set.var.i8 6 = 100  (Vie)
        i8(b, 93); i16(b, 0xFFFF); i16(b, 1); i16(b, 6);   // actor.stat.set -1, Vie, var 6
        i8(b, 86); i16(b, 0xFFFF); i16(b, 1); i16(b, 7);   // var.set.actor_stat -1, Vie -> var 7
        i8(b, 14); i16(b, 6); i8(b, -5);                   // -5 -> the UNSIGNED clamp
        i8(b, 93); i16(b, 0xFFFF); i16(b, 1); i16(b, 6);
        i8(b, 86); i16(b, 136); i16(b, 1); i16(b, 8);      // by the DB's own id
        i8(b, 9); i8(b, 0x70); i8(b, 0x11); i8(b, 0x01); i8(b, 0x00);   // push.i32 70000
        i8(b, 18); i16(b, 6);                              // var 6 = 70000 -> Argent clamps at 65535
        i8(b, 93); i16(b, 0xFFFF); i16(b, 4); i16(b, 6);
        i8(b, 86); i16(b, 0xFFFF); i16(b, 4); i16(b, 9);
        i8(b, 14); i16(b, 6); i8(b, -3);                   // Anneaux: no clamp, i16
        i8(b, 93); i16(b, 0xFFFF); i16(b, 5); i16(b, 6);
        i8(b, 86); i16(b, 0xFFFF); i16(b, 5); i16(b, 10);
        i8(b, 14); i16(b, 11); i8(b, 77);
        i8(b, 86); i16(b, 0xFFFF); i16(b, 0); i16(b, 11);  // Sexe: the pointer slot
        i8(b, 3);
        run(s, t, b);
        say("stat.player.vie", s.var(7));
        say("stat.player.vie.record", i16at(s.raw(), 60 + 170));
        say("stat.player.vie.neg", s.var(8));              // 200, read back by id 136
        say("stat.player.argent", s.var(9));
        say("stat.player.anneaux", s.var(10));
        say("stat.pointer.untouched", s.var(11));
    }

    // ================================================================ HOOKS
    std::printf("-- through the hooks: AREA 1's actor 397 (KWH_FN) and its prop record 0\n");
    {
        auto s = omk::GameState::fromFile(start);
        const auto areaFile = omk::DataFs::readPath(fr + "/IAM/AREA");
        const auto areas = omk::IamArchive::open(areaFile);
        TestHooks hk;
        {
            const auto ch = areas.chunk(1);
            hk.chunk.assign(ch.begin(), ch.end());
        }
        const int actor = 397;
        const auto prop = omk::findPropById(hk.chunk, omk::ChunkKind::Area, 360);
        say("prop.found", prop ? 1 : 0);
        const int propSlot = 7, propId = prop ? prop->id : -1;
        const int stateIdx = prop ? prop->stateIndex : -1;
        say("prop.stateIndex", stateIdx);
        if (prop) {
            // `Scene_LoadProps` writes the runtime slot into +0; the double
            // does the same, so the slot walk of 68 has something to match.
            hk.chunk[prop->offset]     = static_cast<std::byte>(propSlot);
            hk.chunk[prop->offset + 1] = static_cast<std::byte>(0);
        }
        hk.slotIds[propSlot] = propId;

        // ---- 86/93 on an NPC record
        Code a;
        i8(a, 86); i16(a, actor); i16(a, 1); i16(a, 5);          // Vie -> var 5
        i8(a, 14); i16(a, 6); i8(a, 42);
        i8(a, 93); i16(a, actor); i16(a, 1); i16(a, 6);          // Vie = 42
        i8(a, 86); i16(a, actor); i16(a, 1); i16(a, 7);
        i8(a, 9); i8(a, 0xA0); i8(a, 0x86); i8(a, 0x01); i8(a, 0x00);   // push.i32 100000
        i8(a, 18); i16(a, 6);                                    // var 6 = 100000
        i8(a, 93); i16(a, actor); i16(a, 4); i16(a, 6);          // Argent -> 65535
        i8(a, 86); i16(a, actor); i16(a, 4); i16(a, 8);
        i8(a, 9); i8(a, 17); i8(a, 0); i8(a, 2); i8(a, 0);       // push.i32 (2 << 16) | 17
        i8(a, 18); i16(a, 6);
        i8(a, 93); i16(a, actor); i16(a, 35); i16(a, 6);         // ammunition slot 2 = 17
        i8(a, 14); i16(a, 9); i8(a, 77);
        i8(a, 86); i16(a, 9999); i16(a, 1); i16(a, 9);           // no such actor
        i8(a, 3);
        run(s, t, a, &hk);
        say("npc.vie.shipped", s.var(5));
        say("npc.vie.set", s.var(7));
        say("npc.argent.clamp", s.var(8));
        say("npc.ammo.slot2", i16at(hk.chunk, *omk::findActorRecord(hk.chunk, omk::ChunkKind::Area, actor) + 264));
        say("npc.unknown.untouched", s.var(9));

        // ---- 75: nothing held, then slot 7, then no hooks at all
        Code u; i8(u, 75); i16(u, 5); i8(u, 3);
        s.setVar(5, 77);
        run(s, t, u, &hk);
        say("used.none", s.var(5));
        hk.heldSlot[-1] = propSlot;
        run(s, t, u, &hk);
        say("used.held", s.var(5));
        s.setVar(5, 77);
        run(s, t, u, nullptr);
        say("used.nohooks", s.var(5));
        hk.heldSlot.erase(-1);

        // ---- 67: hold, and hold again
        Code h; i8(h, 67); i16(h, actor); i16(h, propId); i8(h, 3);
        run(s, t, h, &hk);
        say("hold.calls", hk.count("hold "));
        say("hold.field", hk.heldObjectField(actor));
        run(s, t, h, &hk);
        say("hold.calls.again", hk.count("hold "));       // already holds it: nothing

        // ---- 69: the actor drops it; an actor holding nothing does nothing
        Code r; i8(r, 69); i16(r, actor); i8(r, 3);
        run(s, t, r, &hk);
        say("release.actor.drop.calls", hk.count("drop "));
        say("release.actor.field", hk.heldObjectField(actor));
        run(s, t, r, &hk);
        say("release.actor.none.calls", hk.count("drop "));

        // ---- 76: show needs bit 0
        Code sh; i8(sh, 76); i16(sh, propId); i8(sh, 3);
        s.setPropState(stateIdx, 0);
        run(s, t, sh, &hk);
        say("show.absent.state", s.propStateBits(stateIdx));
        say("show.absent.calls", hk.count("show "));
        s.setPropState(stateIdx, 1);
        run(s, t, sh, &hk);
        say("show.state", s.propStateBits(stateIdx));
        say("show.calls", hk.count("show "));
        Code sh2; i8(sh2, 76); i16(sh2, 9999); i8(sh2, 3);   // no record: nothing
        run(s, t, sh2, &hk);
        say("show.unknown.calls", hk.count("show "));

        // ---- 68: the player releases what it holds
        Code rel; i8(rel, 68); i8(rel, 3);
        const auto before0 = hk.log.size();
        run(s, t, rel, &hk);                                  // holding nothing
        say("release.none.calls", static_cast<long>(hk.log.size() - before0));
        {
            auto raw = s.rawMutable();                        // DB +330: held id
            raw[330] = static_cast<std::byte>(propId & 0xFF);
            raw[331] = static_cast<std::byte>((propId >> 8) & 0xFF);
        }
        hk.heldSlot[-1] = propSlot;                           // the record's +0 matches
        const auto before = hk.log.size();
        run(s, t, rel, &hk);
        say("release.state", s.propStateBits(stateIdx));      // bit 1 cleared
        say("release.drop.calls", hk.count("drop -1"));
        say("release.hide.calls", hk.count("hide "));
        say("release.player.field", i16at(s.raw(), 330));
        say("release.log.added", static_cast<long>(hk.log.size() - before));
        hk.heldSlot[-1] = 9;                                  // a slot no record carries
        run(s, t, rel, &hk);
        say("release.remove.calls", hk.count("remove -1"));
        say("release.clearslot.calls", hk.count("clearslot 9"));

        // ---- 98
        Code pl; i8(pl, 98); i16(pl, 48); i16(pl, 606); i8(pl, 3);
        run(s, t, pl, &hk);
        say("place.calls", hk.count("place 48 606"));

        // ---- the sweep switch: everything above inert, everything recorded
        auto s2 = omk::GameState::fromFile(start);
        TestHooks hk2 = hk;
        Code off;
        i8(off, 50); i16(off, 0); i16(off, 300);
        i8(off, 14); i16(off, 6); i8(off, 42);
        i8(off, 93); i16(off, 0xFFFF); i16(off, 1); i16(off, 6);
        i8(off, 112);
        i8(off, 76); i16(off, propId);
        i8(off, 3);
        const auto ro = run(s2, t, off, &hk2, false);
        say("off.list0.count", s2.listCount(0));
        say("off.vie.record", i16at(s2.raw(), 60 + 170));
        say("off.flags", s2.timerFlags());
        say("off.log", static_cast<long>(hk2.log.size() - hk.log.size()));
        say("off.recorded", static_cast<long>(ro.calls.size()));
    }
    std::printf("-- %d values\n", nvalues);
    return 0;
}
