// SPDX-License-Identifier: GPL-3.0-or-later
// THE THREE PARKING OPCODES, PROBED - hand-built bytecode through
// `script/interp`, one line per rule the corpus sweep cannot see.
//
//     parking_probe <tables/vm_opcodes.json>
//
// Tier 6 for the transcription, tier 2 for the operand shapes (the counts and
// ranges each line quotes are the shipped corpus'), and NO oracle: the golden
// trace rig cannot reach any of this. `fight.begin` announces nothing at all
// and `player.move.wait`'s operand goes to a domain `Dbg_LogTagged` filters,
// which is the same wall `traces/fight.log` hit (CLAUDE.md 2). What is
// checkable is the handler's own decision, and that is what these lines are.
//
// The three rules, each read from the handler named beside it:
//
//   89 player.move.wait  0x004043F0. One fetch - the ADDRESSES id - then
//                        `Player_GoToMove(address, byte [esi+1Eh])` (the
//                        caller's own context slot) and
//                        `mov word ptr [esi+16h], 4`. 548 shipped sites.
//                        Op 63 `player.move` (0x00403730) is the identical
//                        handler with `push 0FFFFFFFFh` for the slot and NO
//                        status write - 312 sites - so it must NOT park, and
//                        its address must still be recorded.
//   62 fight.begin       0x004035D0. Opponent, fight mode, `mov word ptr
//                        [esi+16h], 3`, then `Camera_Request(14)` with
//                        `dword_930818 = max(field 1, 0)` - the zeroing is a
//                        `test eax,eax; jge` around
//                        `mov dword ptr [esp+18h], 0`. 108 sites, field 1 = 0
//                        at every one of them, so the clamp is the handler's
//                        word and not the data's.
//   126 camera.set.at_address  0x00405630. Like 96, with two differences that
//                        both have to survive into the result: the subject is
//                        `Address_Find(field 1)` in BOTH pointers where 96
//                        puts `Actor_Player()` twice, and the status-7 write
//                        is UNCONDITIONAL - 96 reaches its own only through
//                        `test ebp,ebp; jz loc_404CB1`, so a 0-frame travel
//                        cuts under 96 and PARKS under 126. 84 sites, all of
//                        them travelling exactly 20 frames, which is why the
//                        corpus cannot show that difference and this can.
//
// Every line prints what the port did; the check reads the numbers.
#include "script/gamestate.h"
#include "script/interp.h"
#include "script/script.h"

#include <cstdio>
#include <cstdint>
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

const char* status(omk::RunStatus s) {
    switch (s) {
        case omk::RunStatus::End:            return "end";
        case omk::RunStatus::Dialog:         return "dialog";
        case omk::RunStatus::UiOpen:         return "ui-open";
        case omk::RunStatus::CameraWait:     return "camera-wait";
        case omk::RunStatus::MoveWait:       return "move-wait";
        case omk::RunStatus::FightWait:      return "fight-wait";
        case omk::RunStatus::ObjectWait:     return "object-wait";
        case omk::RunStatus::Suspended:      return "suspended";
        case omk::RunStatus::Runaway:        return "runaway";
        case omk::RunStatus::PcOutOfRange:   return "pc-out-of-range";
        case omk::RunStatus::UnknownOpcode:  return "unknown-opcode";
        case omk::RunStatus::StackUnderflow: return "underflow";
    }
    return "?";
}

// The recorded calls, flattened: "op:f0,f1|op:f0" - so a check can assert both
// WHICH subsystem calls a run announced and their operands, in order.
std::string calls(const omk::RunResult& r) {
    std::string s;
    for (const auto& c : r.calls) {
        if (!s.empty()) s += "|";
        s += std::to_string(static_cast<int>(c.op)) + ":";
        for (std::size_t k = 0; k < c.fields.size(); ++k)
            s += (k ? "," : "") + std::to_string(static_cast<int>(c.fields[k]));
    }
    return s.empty() ? "-" : s;
}

// How the switches are set for one run. Each parking switch defaults OFF and
// `run_scripts` relies on that: the corpus sweep must stay byte-identical to
// `tools/sim`, which stubs all of these.
struct Opts {
    bool move = false, fight = false, camera = false;
};

struct Run {
    omk::GameState state = omk::GameState::fromBytes({});
    omk::RunResult r;
};

Run run(const omk::OpcodeTable& t, const Code& c, Opts o) {
    Run x;
    omk::Interpreter vm(x.state, t);
    vm.setRecordCalls(true);
    vm.setMoveWaitSuspends(o.move);
    vm.setFightWaitSuspends(o.fight);
    vm.setCameraWaitSuspends(o.camera);
    x.r = vm.run(bytes(c), 0);
    return x;
}

// Run, then RESUME from the pc the park reported, with the same interpreter
// state fresh - what the Session does when the event releases the context.
// -> the status of the second run. A park whose pc landed ON the instruction
// instead of after it re-runs it here and never reaches `end`.
const char* parkThenResume(const omk::OpcodeTable& t, const Code& c, Opts o,
                           omk::RunResult& first) {
    omk::GameState st = omk::GameState::fromBytes({});
    omk::Interpreter vm(st, t);
    vm.setRecordCalls(true);
    vm.setMoveWaitSuspends(o.move);
    vm.setFightWaitSuspends(o.fight);
    vm.setCameraWaitSuspends(o.camera);
    first = vm.run(bytes(c), 0);
    // The park is released; the switches stay on, so a SECOND parking opcode
    // would park again - here the rest is just `end`.
    const auto second = vm.resume(bytes(c), first.pc);
    return status(second.status);
}

void line(const char* tag, const omk::RunResult& r, std::size_t endPc,
          const char* resume) {
    std::printf("%s status=%s pc=%zu end=%zu cam=%d camop=%d camaddr=%d "
                "travel=%d moveaddr=%d opp=%d fighttravel=%d calls=%s resume=%s\n",
                tag, status(r.status), r.pc, endPc, r.camId, r.camWaitOp,
                r.camAddress, r.camTravel, r.moveAddress, r.fightOpponent,
                r.fightCamTravel, calls(r).c_str(), resume);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: parking_probe <tables/vm_opcodes.json>\n");
        return 2;
    }
    const omk::OpcodeTable t = omk::OpcodeTable::loadJson(argv[1]);
    if (!t.valid()) { std::fprintf(stderr, "cannot load %s\n", argv[1]); return 2; }

    // The operand lengths these programs are laid out with, from the table
    // itself - a check asserts them, because every `end=` below is
    // `1 + operandLength(op)` and a park at the wrong pc is exactly what a
    // wrong length looks like.
    std::printf("lengths 89=%d 63=%d 62=%d 96=%d 126=%d\n",
                t.operandLength(89), t.operandLength(63), t.operandLength(62),
                t.operandLength(96), t.operandLength(126));

    // ---- 89 `player.move.wait` -------------------------------------------
    // Address 58 is the shipped corpus' most common one: 383 of the 548 sites.
    {
        Code c; i8(c, 89); i16(c, 58); i8(c, 3);          // ... then `end`
        const std::size_t endPc = 1 + static_cast<std::size_t>(t.operandLength(89));
        omk::RunResult r;
        const char* res = parkThenResume(t, c, {true, false, false}, r);
        line("89.park", r, endPc, res);
        const auto off = run(t, c, {});
        line("89.off", off.r, endPc, "-");
    }

    // ---- 63 `player.move`: the same handler WITHOUT the hold ---------------
    // `push 0FFFFFFFFh` in place of the slot and no status write, so it may
    // never park - and the Session still has to be able to start the move,
    // which means the recorded call has to carry the address. It does, through
    // the generic stub recorder, and that is what `calls=63:100` asserts.
    // 100 is the corpus' most common operand (60 of the 312 sites).
    {
        Code c; i8(c, 63); i16(c, 100); i8(c, 3);
        const auto both = run(t, c, {true, true, true});   // every switch ON
        line("63.on", both.r, 1 + static_cast<std::size_t>(t.operandLength(63)), "-");
    }

    // ---- 62 `fight.begin` --------------------------------------------------
    // Opponent 25 is the corpus' most common (24 of 108); field 1 is 0 at
    // every shipped site, so the second program's -3 is the only way to see
    // the handler's `jge` clamp at all.
    {
        // three fetches at 0x004035D0: opponent, camera travel, Fight_Begin's
        // second argument - the table said 4 bytes until 2026-09-02
        Code c; i8(c, 62); i16(c, 25); i16(c, 0); i16(c, 0); i8(c, 3);
        const std::size_t endPc = 1 + static_cast<std::size_t>(t.operandLength(62));
        omk::RunResult r;
        const char* res = parkThenResume(t, c, {false, true, false}, r);
        line("62.park", r, endPc, res);
        const auto off = run(t, c, {});
        line("62.off", off.r, endPc, "-");
    }
    {
        // The clamp, and the only literal that can reach it. A negative field
        // is NOT expressible as a plain operand: the shared fetch tests
        // `cmp ax,0FFFFh` and then `test ah,40h`, so every negative 16-bit
        // pattern except 0xFFFF itself has bit 0x4000 set and is read as an
        // INDIRECT index instead. -1 passes through, and the handler's
        // `test eax,eax; jge` then zeroes it for `dword_930818` while the
        // recorded operand stays -1 - the clamp is on the camera's travel, not
        // on the field.
        Code c; i8(c, 62); i16(c, 25); i16(c, -1); i16(c, 0); i8(c, 3);
        const auto on = run(t, c, {false, true, false});
        line("62.minus1", on.r, 1 + static_cast<std::size_t>(t.operandLength(62)), "-");
    }

    // ---- 126 `camera.set.at_address` vs 96 `camera.set.wait` ---------------
    // Camera 4781 and address 709 are shipped values (the four cameras
    // 4781..4784 and the 42 addresses 709..762 are all 126 ever names); the
    // travel is 20 at all 84 sites.
    {
        Code c; i8(c, 126); i16(c, 4781); i16(c, 709); i16(c, 20); i8(c, 3);
        const std::size_t endPc = 1 + static_cast<std::size_t>(t.operandLength(126));
        omk::RunResult r;
        const char* res = parkThenResume(t, c, {false, false, true}, r);
        line("126.park", r, endPc, res);
        const auto off = run(t, c, {});
        line("126.off", off.r, endPc, "-");
    }
    {
        // THE CONTRAST, and the reason `camWaitOp` exists. Same camera, same
        // address, travel 0: 126 parks and 96 does not.
        Code c; i8(c, 126); i16(c, 4781); i16(c, 709); i16(c, 0); i8(c, 3);
        const std::size_t endPc = 1 + static_cast<std::size_t>(t.operandLength(126));
        omk::RunResult r;
        const char* res = parkThenResume(t, c, {false, false, true}, r);
        line("126.zero", r, endPc, res);
    }
    {
        Code c; i8(c, 96); i16(c, 4785); i16(c, 60); i16(c, 1); i8(c, 3);
        const std::size_t endPc = 1 + static_cast<std::size_t>(t.operandLength(96));
        omk::RunResult r;
        const char* res = parkThenResume(t, c, {false, false, true}, r);
        line("96.park", r, endPc, res);
    }
    {
        Code c; i8(c, 96); i16(c, 4785); i16(c, 0); i16(c, 1); i8(c, 3);
        const auto on = run(t, c, {false, false, true});
        line("96.zero", on.r, 1 + static_cast<std::size_t>(t.operandLength(96)), "-");
    }

    return 0;
}
