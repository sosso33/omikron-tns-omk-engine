// SPDX-License-Identifier: GPL-3.0-or-later
// THE SCRIPT VM, PROBED - hand-built bytecode through `script/interp`, each
// line a rule of one handler that the corpus sweep cannot see.
//
//     vm_probe <tables/vm_opcodes.json>
//
// Written 2026-09-02 against the interpreter as it stood, and its first run
// FAILED every line but the last two - which is the point. Each rule is read
// from the handler named beside it:
//
//   div       `cdq; idiv` at 0x402770 (op 34) and 0x402400 (op 22) truncate
//             toward zero: -7/2 is -3. The port had Python's floor, -4,
//             after `tools/sim`. The zero divisor is the port's choice: the
//             engine faults (#DE) where this returns 0.
//   push.i16  op 8 (0x401D70) runs the shared fetch: 0x4000|k is the
//             parameter block's word at 2k+2 - for a message handler the
//             SENDER - not the constant 16384.
//   set.var   ops 14/15/16 (0x401F70..) fetch the variable index; 15
//             (0x401FE0) fetches its VALUE as well; 17 (0x402110) both.
//   scene.load op 71 (0x403950) fetches both fields, so a recorded call
//             carries the resolved area and scene.
//   random    op 120 - the REAL handler is at 0x405480, not the block the
//             pre-split file carries - fetches lo, hi, then the variable, and
//             writes `Random_NoRepeat(lo, hi)` (0x0041D6B0): in range, never
//             the previous draw, and lo when lo == hi.
//
// Every line prints what the port did; the check reads the numbers.
#include "script/gamestate.h"
#include "script/interp.h"
#include "script/script.h"

#include <cstdio>
#include <cstdint>
#include <set>
#include <span>
#include <vector>

namespace {

using Code = std::vector<std::uint8_t>;

void i8(Code& c, int v)  { c.push_back(static_cast<std::uint8_t>(v & 0xFF)); }
void i16(Code& c, int v) { i8(c, v); i8(c, v >> 8); }
void i32(Code& c, std::int32_t v) { i16(c, v & 0xFFFF); i16(c, (v >> 16) & 0xFFFF); }

std::span<const std::byte> bytes(const Code& c) {
    return {reinterpret_cast<const std::byte*>(c.data()), c.size()};
}

struct Run {
    omk::GameState state = omk::GameState::fromBytes({});
    omk::RunResult r;
};

// Run `c` from 0 with `params`, recording stubbed calls; the state is fresh.
Run run(const omk::OpcodeTable& t, const Code& c,
        std::vector<std::int16_t> params = {}, std::uint32_t seed = 0,
        bool randomWrites = true) {
    Run x;
    omk::Interpreter vm(x.state, t);
    vm.setParams(std::move(params));
    vm.setRecordCalls(true);
    vm.setRandomWrites(randomWrites);
    if (seed) vm.seedRandom(seed);
    x.r = vm.run(bytes(c), 0);
    return x;
}

const char* status(omk::RunStatus s) {
    switch (s) {
        case omk::RunStatus::End: return "end";
        case omk::RunStatus::StackUnderflow: return "underflow";
        case omk::RunStatus::UnknownOpcode: return "unknown-opcode";
        case omk::RunStatus::PcOutOfRange: return "pc-out-of-range";
        default: return "other";
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: vm_probe <vm_opcodes.json>\n"); return 2; }
    const auto t = omk::OpcodeTable::loadJson(argv[1]);
    if (!t.valid()) { std::fprintf(stderr, "no opcode table\n"); return 1; }

    // ---- division: op 34 pops the TOP as the dividend (0x402750: eax is
    // the popped word, edi the one under it), op 22 divides the variable by
    // the popped top.
    for (const auto [a, b] : {std::pair{-7, 2}, {7, -2}, {-7, -2}, {7, 2}, {-7, 0}}) {
        Code c;
        i8(c, 9); i32(c, b);          // push.i32 divisor (under)
        i8(c, 9); i32(c, a);          // push.i32 dividend (top)
        i8(c, 34);                    // div
        i8(c, 18); i16(c, 5);         // set.var.pop 5
        i8(c, 3);
        auto x = run(t, c);
        std::printf("div34 %d %d -> %d (%s)\n", a, b, x.state.var(5), status(x.r.status));
    }
    for (const auto [a, b] : {std::pair{-7, 2}, {7, -2}, {-7, 0}}) {
        Code c;
        i8(c, 9); i32(c, a); i8(c, 18); i16(c, 5);   // var 5 = a
        i8(c, 9); i32(c, b);                         // push.i32 b
        i8(c, 22); i16(c, 5);                        // var.div 5
        i8(c, 3);
        auto x = run(t, c);
        std::printf("div22 %d %d -> %d (%s)\n", a, b, x.state.var(5), status(x.r.status));
    }

    // ---- set.var.i32 (16): the handler at 0x402070 reads 2 + 4 bytes
    // (`add esi, 4` past the fetched field) and the table says 5. No shipped
    // script uses it, so only a probe can see which. The port reads the four
    // value bytes and then advances by the TABLE's length, so the run lands
    // on the value's last byte as an opcode: 0xFF for -7, `end` for 3.
    for (const auto a : {-7, 3}) {
        Code c;
        i8(c, 16); i16(c, 5); i32(c, a);
        i8(c, 3);
        auto x = run(t, c);
        std::printf("set.var.i32 5 %d -> var5=%d (%s) [table length %d]\n",
                    a, x.state.var(5), status(x.r.status), t.operandLength(16));
    }

    // ---- push.i16 through the fetch: params = {message id, sender}
    {
        Code c;
        i8(c, 8); i16(c, 0x4000);     // push.i16 params[0] = block word 1
        i8(c, 18); i16(c, 5);
        i8(c, 8); i16(c, 0xFFFF);     // -1 passes through
        i8(c, 18); i16(c, 6);
        i8(c, 3);
        auto x = run(t, c, {3, 77});
        std::printf("push.i16 0x4000 params={3,77} -> %d ; 0xFFFF -> %d (%s)\n",
                    x.state.var(5), x.state.var(6), status(x.r.status));
    }

    // ---- set.var.i8 with an indirect VARIABLE index; set.var.i16 with an
    // indirect VALUE; set.var.var with both indirect
    {
        Code c;
        i8(c, 14); i16(c, 0x4000); i8(c, -5);      // set.var.i8 [params0] = -5
        i8(c, 15); i16(c, 3); i16(c, 0x4001);      // set.var.i16 3 = params[1]
        i8(c, 17); i16(c, 0x4002); i16(c, 0x4000); // set.var.var [params2] = var[params0]
        i8(c, 3);
        auto x = run(t, c, {0, 77, -9, 4});
        std::printf("set.var.i8 [0x4000] -5 params={0,77,-9,4} -> var77=%d var16384=%d ; "
                    "set.var.i16 3 [0x4001] -> var3=%d ; set.var.var [0x4002] [0x4000] -> var4=%d (%s)\n",
                    x.state.var(77), x.state.var(16384 & 0x1FFF), x.state.var(3),
                    x.state.var(4), status(x.r.status));
    }

    // ---- scene.load with both fields indirect: the recorded call carries
    // the RESOLVED fields
    {
        Code c;
        i8(c, 71); i16(c, 0x4000); i16(c, 0x4001);
        i8(c, 3);
        auto x = run(t, c, {0, 12, 34});
        int a = -1, sc = -1;
        for (const auto& call : x.r.calls)
            if (call.op == 71 && call.fields.size() == 2) { a = call.fields[0]; sc = call.fields[1]; }
        std::printf("scene.load [0x4000][0x4001] params={0,12,34} -> recorded %d %d ; "
                    "sceneOfArea(12)=%d (%s)\n",
                    a, sc, x.state.sceneOfArea(12), status(x.r.status));
    }

    // ---- var.set.random: 50 draws in [3,5] into var 9, each copied out
    // through set.var.var into 50 consecutive variables
    {
        Code c;
        for (int k = 0; k < 50; ++k) {
            i8(c, 120); i16(c, 3); i16(c, 5); i16(c, 9);   // var.set.random 3 5 -> var 9
            i8(c, 17); i16(c, 100 + k); i16(c, 9);        // var[100+k] = var 9
        }
        i8(c, 3);
        auto x = run(t, c, {}, 12345);
        int inrange = 0, consec = 0, recorded = 0;
        std::set<int> seen;
        for (int k = 0; k < 50; ++k) {
            const int v = x.state.var(100 + k);
            inrange += (v >= 3 && v <= 5);
            seen.insert(v);
            if (k && v == x.state.var(100 + k - 1)) ++consec;
        }
        for (const auto& call : x.r.calls) recorded += (call.op == 120);
        std::printf("random [3,5] x50 -> inrange %d/50 consecutive-equal %d distinct %zu "
                    "recorded %d first %d %d %d %d %d (%s)\n",
                    inrange, consec, seen.size(), recorded,
                    x.state.var(100), x.state.var(101), x.state.var(102),
                    x.state.var(103), x.state.var(104), status(x.r.status));
        // the same seed is the same sequence
        auto y = run(t, c, {}, 12345);
        bool same = true;
        for (int k = 0; k < 50; ++k) same = same && x.state.var(100 + k) == y.state.var(100 + k);
        std::printf("random reseeded -> %s\n", same ? "identical" : "DIFFERENT");
        // and with writes off, the variable is untouched and the call recorded
        auto z = run(t, c, {}, 12345, false);
        int rec = 0;
        for (const auto& call : z.r.calls) rec += (call.op == 120);
        std::printf("random writes-off -> var9=%d recorded %d\n", z.state.var(9), rec);
    }
    {
        Code c;
        i8(c, 120); i16(c, 9); i16(c, 9); i16(c, 9);     // lo == hi
        i8(c, 120); i16(c, 0x4000); i16(c, 0x4001); i16(c, 0x4002);  // indirect lo, hi, var
        i8(c, 3);
        auto x = run(t, c, {0, 42, 42, 11}, 7);
        int f0 = -1, f1 = -1, f2 = -1;
        for (const auto& call : x.r.calls)
            if (call.op == 120 && call.fields.size() == 3) { f0 = call.fields[0]; f1 = call.fields[1]; f2 = call.fields[2]; }
        std::printf("random lo==hi 9 -> var9=%d ; indirect [0x4000][0x4001][0x4002] params={0,42,42,11} "
                    "-> var11=%d recorded %d %d %d (%s)\n",
                    x.state.var(9), x.state.var(11), f0, f1, f2, status(x.r.status));
    }
    return 0;
}
