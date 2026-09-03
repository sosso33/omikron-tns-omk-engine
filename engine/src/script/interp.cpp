// SPDX-License-Identifier: GPL-3.0-or-later
#include "script/interp.h"
#include "script/props.h"

namespace omk {
namespace {

std::int32_t imm(const std::vector<std::uint8_t>& raw) {
    if (raw.size() == 1) return static_cast<std::int8_t>(raw[0]);
    if (raw.size() == 2)
        return static_cast<std::int16_t>(
            static_cast<std::uint16_t>(raw[0]) | static_cast<std::uint16_t>(raw[1]) << 8);
    if (raw.size() == 4)
        return static_cast<std::int32_t>(
            static_cast<std::uint32_t>(raw[0])       |
            static_cast<std::uint32_t>(raw[1]) <<  8 |
            static_cast<std::uint32_t>(raw[2]) << 16 |
            static_cast<std::uint32_t>(raw[3]) << 24);
    return 0;
}

std::int16_t i16at(const std::vector<std::uint8_t>& raw, std::size_t o) {
    if (o + 2 > raw.size()) return 0;
    return static_cast<std::int16_t>(
        static_cast<std::uint16_t>(raw[o]) | static_cast<std::uint16_t>(raw[o + 1]) << 8);
}

// Integer division the way the ENGINE does it: `cdq; idiv` - the op 34
// handler at 0x402750 pops the top into eax, the word under it into edi, and
// `idiv edi`; `var.div` (22, 0x402390) does the same with Var_Get's value
// over the popped top. x86 idiv truncates toward zero, so -7/2 is -3. It was
// Python's floor division here until 2026-09-02, after `tools/sim`, and
// that gave -4.
//
// The two guards are the PORT's choice, and labelled as such: a zero divisor
// and INT_MIN / -1 both raise #DE in the engine - a crash, not a decision -
// and this returns 0 for the first and the wrapped negation for the second.
std::int32_t divTrunc(std::int32_t a, std::int32_t b) {
    if (b == 0) return 0;                                       // engine: #DE
    if (b == -1) return static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(a));
    return a / b;
}

std::int32_t i32at(std::span<const std::byte> code, std::size_t o) {
    if (o + 4 > code.size()) return 0;
    return static_cast<std::int32_t>(
        static_cast<std::uint32_t>(code[o])       |
        static_cast<std::uint32_t>(code[o + 1]) <<  8 |
        static_cast<std::uint32_t>(code[o + 2]) << 16 |
        static_cast<std::uint32_t>(code[o + 3]) << 24);
}

}  // namespace

std::int32_t Interpreter::fetch16(std::span<const std::byte> code,
                                  std::size_t& pc) const {
    if (pc + 2 > code.size()) { pc = code.size(); return 0; }
    std::uint32_t v = static_cast<std::uint32_t>(code[pc]) |
                      static_cast<std::uint32_t>(code[pc + 1]) << 8;
    pc += 2;
    if (v != 0xFFFFu && (v & 0x4000u)) {
        // `movsx eax, word ptr [ecx+eax*2+2]`: the block's word AFTER the
        // one the index names - for a message handler, index 0 is the
        // sender. A context with no block at all dereferences null in the
        // engine; 0 here is the port's choice.
        const auto i = static_cast<std::size_t>(v & 0x3FFFu) + 1;
        return i < params_.size() ? params_[i] : 0;
    }
    if (v & 0x8000u) return static_cast<std::int32_t>(v) - 0x10000;
    return static_cast<std::int32_t>(v);
}

std::int32_t Interpreter::randomNoRepeat(std::int32_t lo, std::int32_t hi) {
    // Random_NoRepeat, 0x0041D6B0: `lo + rand() % (hi - lo + 1)` redrawn while
    // it equals dword_4E7E8C, the previous result; lo when lo == hi. The
    // generator is xorshift32 rather than the C library's (`particles.cpp`
    // has the argument), so a seeded run is one fixed sequence - and both
    // words are process-global, as rand()'s seed and dword_4E7E8C are.
    if (lo == hi) { lastRandom_ = lo; return lo; }
    if (hi < lo) { lastRandom_ = lo; return lo; }   // engine: a negative or
                                                    // zero modulus - #DE for
                                                    // hi == lo - 1. Port's choice.
    const auto n = static_cast<std::uint32_t>(hi - lo + 1);
    std::int32_t r;
    do {
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
        r = lo + static_cast<std::int32_t>((rng_ >> 8) % n);
    } while (r == lastRandom_);
    lastRandom_ = r;
    return r;
}

// ---- the world: the player's record is the DB, everyone else's is the hook's
//
// `Actor_FindById` (0x0040B190) opens
//
//     result = g_PlayerRecord;
//     if (i16(g_PlayerRecord, 272) == a1 || a1 == -1) return result;
//
// before it scans a chunk, and `g_PlayerRecord` is `g_GameDB + 60`. So for
// the player - named -1, or by the id the DB record carries - the stat block,
// the held-object field and the identity are all in the 8192-byte block and
// need no hook; `player.become` writes +272 (`rep movsd` of the whole record)
// and a save restores it, which is what makes `Actor_IdBySlot(Actor_Player())`
// and DB +332 the same number at every moment the script could ask.

int Interpreter::playerId() const {
    const auto b = state_.raw();
    const std::size_t o = static_cast<std::size_t>(GameState::kPlayerRecord) + 272;
    if (o + 2 > b.size()) return -1;
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(b[o]) |
                                     static_cast<std::uint16_t>(b[o + 1]) << 8);
}

int Interpreter::heldField(int actor) const {
    if (isPlayer(actor))
        return heldObjectOf(state_.raw().subspan(
            static_cast<std::size_t>(GameState::kPlayerRecord), kActorRecordSize));
    return hooks_ ? hooks_->heldObjectField(actor) : -1;
}

void Interpreter::setHeldField(int actor, int objectId) {
    if (isPlayer(actor)) {
        setHeldObjectOf(state_.rawMutable().subspan(
            static_cast<std::size_t>(GameState::kPlayerRecord), kActorRecordSize), objectId);
        return;
    }
    if (hooks_) hooks_->setHeldObjectField(actor, objectId);
}

bool Interpreter::getProperty(int actor, int property, std::int32_t& out) const {
    if (isPlayer(actor))
        return readActorProperty(state_.raw().subspan(
            static_cast<std::size_t>(GameState::kPlayerRecord), kActorRecordSize), property, out);
    return hooks_ && hooks_->getActorProperty(actor, property, out);
}

bool Interpreter::setProperty(int actor, int property, std::int32_t value) {
    if (isPlayer(actor))
        return writeActorProperty(state_.rawMutable().subspan(
            static_cast<std::size_t>(GameState::kPlayerRecord), kActorRecordSize), property, value);
    return hooks_ && hooks_->setActorProperty(actor, property, value);
}

RunResult Interpreter::run(std::span<const std::byte> code, std::size_t at) {
    stack_.clear();
    return resume(code, at);
}

RunResult Interpreter::resume(std::span<const std::byte> code, std::size_t at) {
    RunResult r;
    std::size_t pc = at;
    constexpr std::size_t kLimit = 20000;

    const auto pop = [&](bool& under) -> std::int32_t {
        if (stack_.empty()) { under = true; return 0; }
        const auto v = stack_.back(); stack_.pop_back(); return v;
    };

    for (;;) {
        if (++r.steps > kLimit) { r.status = RunStatus::Runaway; r.pc = pc; return r; }
        if (pc >= code.size()) { r.status = RunStatus::PcOutOfRange; r.pc = pc; return r; }

        const auto start = pc;
        const auto op = static_cast<std::uint8_t>(code[pc]);
        const int n = table_.operandLength(op);
        if (n < 0) { r.status = RunStatus::UnknownOpcode; r.pc = pc; return r; }
        if (pc + 1 + static_cast<std::size_t>(n) > code.size()) {
            r.status = RunStatus::PcOutOfRange; r.pc = pc; return r;
        }
        std::vector<std::uint8_t> raw;
        raw.reserve(static_cast<std::size_t>(n));
        for (int k = 0; k < n; ++k)
            raw.push_back(static_cast<std::uint8_t>(code[pc + 1 + static_cast<std::size_t>(k)]));
        pc += 1 + static_cast<std::size_t>(n);

        // Narrate first, then act: the handler's Dbg_LogTagged call comes
        // before whatever it does, so an opcode that halts still announces.
        if (recordAll_) {
            Call c; c.op = op;
            for (std::size_t k = 0; k + 2 <= raw.size(); k += 2)
                c.fields.push_back(i16at(raw, k));
            r.calls.push_back(std::move(c));
        }

        bool under = false;

        // ---- flow ------------------------------------------------------
        if (op <= 2) continue;                      // dbg.dump_ctx/code, nop
        if (op == 3) { r.status = RunStatus::End; r.pc = pc; return r; }
        if (op == 4 || op == 5 || op == 6) {
            std::size_t q = start + 1;
            const auto v = fetch16(code, q);
            // the target is relative to the pc AFTER the operand
            const auto target = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(pc) + v);
            if (op == 4) { pc = target; continue; }
            const auto cond = pop(under);
            if (under) { r.status = RunStatus::StackUnderflow; r.pc = start; return r; }
            if (op == 5) { if (cond) pc = target; }      // jmp_if_true
            else         { if (!cond) pc = target; }     // jmp_if_false
            continue;
        }
        if (op == 42) {                              // case label, target
            std::size_t q = start + 1;
            const auto v = fetch16(code, q);
            const auto label = static_cast<std::int32_t>(
                static_cast<std::int8_t>(q < code.size()
                                         ? static_cast<std::uint8_t>(code[q]) : 0));
            // PEEK, never pop: the switch value stays for the next case, and
            // the jump is taken when they DIFFER
            const auto top = stack_.empty() ? 0 : stack_.back();
            pc = (top == label) ? pc
                                : static_cast<std::size_t>(static_cast<std::ptrdiff_t>(q) + v);
            continue;
        }

        // ---- the stack -------------------------------------------------
        // `push.i16` (8, 0x401D70) is the shared fetch and a push: every
        // shipped operand with the indirect bit is one of these, in a
        // message handler, comparing the SENDER against an actor or object
        // id. Read raw it pushed the constant 16384.
        if (op == 7 || op == 9) { stack_.push_back(imm(raw)); continue; }
        if (op == 8) {
            std::size_t q = start + 1;
            stack_.push_back(fetch16(code, q));
            continue;
        }
        if (op == 10) {
            std::size_t q = start + 1;
            stack_.push_back(state_.var(fetch16(code, q)));
            continue;
        }
        if (op == 11) {
            pop(under);
            if (under) { r.status = RunStatus::StackUnderflow; r.pc = start; return r; }
            continue;
        }

        // ---- variable writes -------------------------------------------
        if (op == 12 || op == 13) {
            std::size_t q = start + 1;
            state_.setVar(fetch16(code, q), op == 12 ? 0 : 1);
            continue;
        }
        // `set.var.i8/i16/i32` (14/15/16, 0x401F70/0x401FE0/0x402070) fetch
        // the variable index; the VALUE is a raw int8 for 14
        // (`mov al,[ecx]; movsx esi,al`) and a raw int32 for 16 (four bytes
        // assembled by hand), but for 15 it is the SAME FETCH AGAIN -
        // `cmp ax,0FFFFh; test ah,40h` a second time - so an i16 value can be
        // indirect too. `set.var.var` (17, 0x402110) fetches both.
        //
        // NOTE the table gives 16 a length of 5 and the handler reads 6
        // (`add esi, 4` past the 2-byte field). No shipped script uses 16,
        // so the corpus cannot see which; the four value bytes are read from
        // the code here regardless, and the pc advances by the table's word.
        if (op >= 14 && op <= 16) {
            std::size_t q = start + 1;
            const auto v = fetch16(code, q);
            std::int32_t x = 0;
            if (op == 14)      x = q < code.size() ? static_cast<std::int8_t>(code[q]) : 0;
            else if (op == 15) x = fetch16(code, q);
            else               x = i32at(code, q);
            state_.setVar(v, x);
            continue;
        }
        if (op == 17) {
            std::size_t q = start + 1;
            const auto dst = fetch16(code, q);
            const auto src = fetch16(code, q);
            state_.setVar(dst, state_.var(src));
            continue;
        }
        if (op == 18) {                              // set.var.pop
            std::size_t q = start + 1;
            const auto v = fetch16(code, q);
            const auto x = pop(under);
            if (under) { r.status = RunStatus::StackUnderflow; r.pc = start; return r; }
            state_.setVar(v, x);
            continue;
        }
        if (op >= 19 && op <= 24) {
            std::size_t q = start + 1;
            const auto v = fetch16(code, q);
            const auto x = pop(under);
            if (under) { r.status = RunStatus::StackUnderflow; r.pc = start; return r; }
            const auto cur = state_.var(v);
            std::int32_t f = 0;
            switch (op) {
                case 19: f = cur + x; break;
                case 20: f = cur - x; break;
                case 21: f = cur * x; break;
                case 22: f = divTrunc(cur, x); break;
                case 23: f = cur & x; break;
                default: f = cur | x; break;
            }
            state_.setVar(v, f);
            continue;
        }

        // ---- arithmetic and comparison ---------------------------------
        if (op >= 25 && op <= 30) {
            const auto a = pop(under), b = pop(under);
            if (under) { r.status = RunStatus::StackUnderflow; r.pc = start; return r; }
            bool f = false;
            switch (op) {
                case 25: f = a == b; break;  case 26: f = a <  b; break;
                case 27: f = a >  b; break;  case 28: f = a <= b; break;
                case 29: f = a >= b; break;  default: f = a != b; break;
            }
            stack_.push_back(f ? 1 : 0);
            continue;
        }
        if (op >= 31 && op <= 38) {
            const auto a = pop(under), b = pop(under);
            if (under) { r.status = RunStatus::StackUnderflow; r.pc = start; return r; }
            std::int32_t f = 0;
            switch (op) {
                case 31: f = a + b; break;   case 32: f = a - b; break;
                case 33: f = a * b; break;   case 34: f = divTrunc(a, b); break;
                case 35: f = a & b; break;   case 36: f = a | b; break;
                case 37: f = (a && b) ? 1 : 0; break;
                default: f = (a || b) ? 1 : 0; break;
            }
            stack_.push_back(f);
            continue;
        }
        if (op >= 39 && op <= 41) {
            const auto a = pop(under);
            if (under) { r.status = RunStatus::StackUnderflow; r.pc = start; return r; }
            stack_.push_back(op == 39 ? -a : op == 40 ? (a ? 0 : 1) : ~a);
            continue;
        }

        // ---- the state bitmaps -----------------------------------------
        // No subsystem needed, only a bit - and without them a spent trigger
        // never retires and the zone lifecycle is a fiction.
        if (op == 64 || op == 65) {                  // zone.enable / disable
            std::size_t q = start + 1;
            const auto v = fetch16(code, q);
            state_.setBit(StateArray::ZoneState, v & 0x7FFF, op == 64 ? 1 : 0);
            // ...and the handler's own tail: `call sub_406560`, which is
            // `Zones_RegisterAll`. The bit alone changes nothing until the
            // next area load, because the live list is a SNAPSHOT filtered at
            // registration (`zones.cpp`, `if (state.bit(ZoneState, ...))`).
            r.zonesDirty = true;
            if (record_ && !recordAll_) r.calls.push_back({op, {static_cast<std::int16_t>(v)}});
            continue;
        }
        if (op == 87 || op == 88) {                  // address.enable / disable
            std::size_t q = start + 1;
            const auto v = fetch16(code, q);
            state_.setBit(StateArray::AddressEnabled, v, op == 87 ? 1 : 0);
            if (record_ && !recordAll_) r.calls.push_back({op, {static_cast<std::int16_t>(v)}});
            continue;
        }
        if (op == 71) {                              // scene.load(area, scene)
            // 0x403950 fetches BOTH fields, so what is recorded is what the
            // handler resolved, not the raw halves.
            std::size_t q = start + 1;
            const auto a  = static_cast<std::int16_t>(fetch16(code, q));
            const auto sc = static_cast<std::int16_t>(fetch16(code, q));
            state_.setSceneOfArea(a, sc);
            if (record_ && !recordAll_) r.calls.push_back({op, {a, sc}});
            continue;
        }

        // `var.set.random` (120). The REAL handler is at 0x405480 - the
        // pre-split block the opcode's number carries is the function after
        // it (CLAUDE.md 1's trap; `tools/asmfn.py --op 120` prints both).
        // Three fetches - lo, hi, then the VARIABLE - and, unless the dry-run
        // flag is set, `Var_Set(var, Random_NoRepeat(lo, hi))`. 235 shipped
        // sites, each followed by a `case` on the variable: stubbed, every
        // one of those branches took the same arm for ever.
        if (op == 120) {
            std::size_t q = start + 1;
            const auto lo  = fetch16(code, q);
            const auto hi  = fetch16(code, q);
            const auto var = fetch16(code, q);
            if (randomWrites_) state_.setVar(var, randomNoRepeat(lo, hi));
            if (record_ && !recordAll_)
                r.calls.push_back({op, {static_cast<std::int16_t>(lo),
                                        static_cast<std::int16_t>(hi),
                                        static_cast<std::int16_t>(var)}});
            continue;
        }

        // `dialog.start` is where Script_Execute RETURNS - it does not fall
        // through to the next instruction, so the run stops here.
        if (op == 61) {
            if (record_ && !recordAll_) {
                Call c; c.op = op;
                for (std::size_t k = 0; k + 2 <= raw.size(); k += 2)
                    c.fields.push_back(i16at(raw, k));
                r.calls.push_back(std::move(c));
            }
            r.status = RunStatus::Dialog; r.pc = pc; return r;
        }

        // `ui.open` (70) parks its caller the same way, but for a different
        // reason: `dialog.start` returns outright, while this one SUSPENDS at
        // status 6 with a result variable named, and is resumed only when a
        // person answers the screen. The operands are
        // [screen][parameter][result variable].
        if (op == 70 && uiSuspends_) {
            if (record_ && !recordAll_) {
                Call c; c.op = op;
                for (std::size_t k = 0; k + 2 <= raw.size(); k += 2)
                    c.fields.push_back(i16at(raw, k));
                r.calls.push_back(std::move(c));
            }
            if (raw.size() >= 6) {
                r.uiScreen    = i16at(raw, 0);
                r.uiParam     = i16at(raw, 2);
                r.uiResultVar = i16at(raw, 4);
            }
            // `pc` already points PAST the instruction, so resuming from here
            // continues after the `ui.open` rather than re-running it.
            r.status = RunStatus::UiOpen; r.pc = pc; return r;
        }

        // 46, 58 and 60 start a scene object and then HOLD. The handler is
        // passed the caller's own slot rather than -1, so what releases the
        // script is the object's program ending - which the caller has to
        // arrange, since only it knows whether the object was resident.
        if ((op == 46 || op == 58 || op == 60) && objWaitSuspends_) {
            if (record_ && !recordAll_) {
                Call c; c.op = op;
                for (std::size_t k = 0; k + 2 <= raw.size(); k += 2)
                    c.fields.push_back(i16at(raw, k));
                r.calls.push_back(std::move(c));
            }
            r.objectWaitOp = op;
            // `pc` is past the operand, so resuming continues after it.
            r.status = RunStatus::ObjectWait; r.pc = pc; return r;
        }

        // `camera.set.wait` (96) is `camera.set` (95) plus a HOLD. Both issue
        // the same `Camera_Request` mode 12; 96 additionally writes the
        // context's status word to 7 with a resume id, and `Script_Execute`
        // loops only while that word is 1. Field 0 is the camera, field 1 the
        // move's length in frames - and a 0-frame move is a cut, which holds
        // nothing, so this suspends only when there is something to wait for.
        if (op == 96 && camWaitSuspends_) {
            Call c; c.op = op;
            for (std::size_t k = 0; k + 2 <= raw.size(); k += 2)
                c.fields.push_back(i16at(raw, k));
            const int travel = c.fields.size() >= 2 ? c.fields[1] : 0;
            r.camId = c.fields.empty() ? -1 : c.fields[0];
            r.camTravel = travel > 0 ? travel : 0;
            if (record_ && !recordAll_) r.calls.push_back(std::move(c));
            if (r.camTravel > 0) {
                // `pc` is already past the operand, so the resume continues
                // after the instruction rather than re-issuing the move.
                r.camWaitOp = 96;
                r.status = RunStatus::CameraWait; r.pc = pc; return r;
            }
            continue;
        }

        // `camera.set.at_address` (126, 0x00405630) is 96 with a different
        // SUBJECT and without 96's guard. Three fetches - [camera][address]
        // [travel] - then, once `Camera_FindWorld(camera)` (sub_40B220)
        // resolves (`test esi,esi; jz loc_4057EF` skips the whole handler if
        // it does not), the request block is filled from the camera record,
        // `Address_Find(field 1)` (sub_40E5E0) goes into BOTH subject
        // pointers where 96 puts `Actor_Player()` twice, the travel is
        // `fstp dword_930818` raw - no `max`, no halving - the resume id
        // `dword_930824` is the context's slot, and then:
        //
        //     mov word ptr [edi+16h], 7        <- unconditional
        //     call sub_414BF0                  <- Camera_Request(12)
        //
        // The status write is in a straight line. 96 reaches its own only
        // through `test ebp,ebp; jz`, so **a 0-frame travel cuts under 96 and
        // PARKS under 126**, and that difference has to survive into the
        // result rather than being folded into `camTravel`: it is `camWaitOp`.
        // (The shipped corpus cannot show it - all 84 sites travel exactly 20
        // frames - so this is the handler's word, not the data's.)
        if (op == 126 && camWaitSuspends_) {
            std::size_t q = start + 1;
            const auto cam    = fetch16(code, q);
            const auto addr   = fetch16(code, q);
            const auto travel = fetch16(code, q);
            if (record_ && !recordAll_)
                r.calls.push_back({op, {static_cast<std::int16_t>(cam),
                                        static_cast<std::int16_t>(addr),
                                        static_cast<std::int16_t>(travel)}});
            r.camId      = cam;
            r.camAddress = addr;
            r.camTravel  = travel;
            r.camWaitOp  = 126;
            // `pc` is past all three operands, so the resume continues after
            // the instruction. The park is unconditional here.
            r.status = RunStatus::CameraWait; r.pc = pc; return r;
        }

        // `player.move.wait` (89, 0x004043F0). One fetch - the ADDRESSES id -
        // then `Player_GoToMove(address, byte [esi+1Eh])`, the caller's own
        // context slot, and `mov word ptr [esi+16h], 4`. Op **63**
        // `player.move` (0x00403730) is byte for byte the same handler with
        // `push 0FFFFFFFFh` in place of the slot and NO status write: the pair
        // is the same "hand it your slot and it will report back" shape as
        // `scx.play.wait` against `scx.play`. So this parks and 63 runs on,
        // and 63 needs no arm here at all - the generic stub recorder already
        // carries its address as field 0, which is all a Session needs to
        // start the move.
        //
        // What releases it is `Game_HandleEvent` case 3 - the same event the
        // waiting `scx.play*` variants use - raised when the move ends, so a
        // Session without a walker must leave `setMoveWaitSuspends` off.
        if (op == 89 && moveWaitSuspends_) {
            std::size_t q = start + 1;
            const auto addr = fetch16(code, q);
            if (record_ && !recordAll_)
                r.calls.push_back({op, {static_cast<std::int16_t>(addr)}});
            r.moveAddress = addr;
            // `pc` is past the operand: the resume continues after the walk.
            r.status = RunStatus::MoveWait; r.pc = pc; return r;
        }

        // `fight.begin` (62, 0x004035D0): the opponent, the fight, status 3,
        // and the fight camera.
        //
        //     mov word ptr [esi+16h], 3
        //     ...
        //     fild dword ptr [esp+18h]      <- field 1, zeroed first if < 0
        //     push offset dword_930800
        //     push 0Eh                      <- camera mode 14
        //     mov dword_93081C, 1
        //     mov dword_930828, 0FFFFFFFFh
        //     fstp dword_930818             <- the travel
        //     call sub_414BF0               <- Camera_Request
        //
        // Only `Game_HandleEvent` case 2 - raised by the fight code when the
        // fight ends - returns the FIRST context at status 3 to 1.
        //
        // **The handler makes THREE 2-byte fetches and the VM table gives this
        // opcode 4.** `tools/vm_oplen.py` has flagged `62: 4 -> 6` all along
        // and the corpus adjudicates for 6 exactly the way it did for op 103
        // (CLAUDE.md 1): decoding the 5785-slot corpus at 4 leaves **216
        // phantom instructions** - 144 `dbg.dump_ctx`, 36 `dbg.dump_code`, 36
        // `nop`, two per site over 108 sites - which vanish at 6, with the
        // same 108 sites and the same 5785/5785 clean decode either way. They
        // are all inert, which is *why* nothing has failed: the two surplus
        // bytes decode as zero-operand no-ops and the stream resynchronises on
        // the next instruction, so the port's pc is 2 bytes short of the
        // engine's for two harmless steps. Correcting the table is a change to
        // `tables/vm_opcodes.json` and `dialog_disasm.LEN_FIX` together and is
        // NOT made here; until it is, `raw` carries two fields and the third -
        // `Fight_Begin`'s own second argument - is unreachable. Field 1 (the
        // camera travel) is 0 at all 108 shipped sites.
        if (op == 62 && fightWaitSuspends_) {
            std::size_t q = start + 1;
            const auto opponent = fetch16(code, q);
            const auto travel   = static_cast<int>(fetch16(code, q));
            if (record_ && !recordAll_)
                r.calls.push_back({op, {static_cast<std::int16_t>(opponent),
                                        static_cast<std::int16_t>(travel)}});
            r.fightOpponent  = opponent;
            r.fightCamTravel = travel > 0 ? travel : 0;   // the handler's `jge`
            // `pc` is past the operands the TABLE gives, which is where the
            // resume continues from - see the note above.
            r.status = RunStatus::FightWait; r.pc = pc; return r;
        }

        // ---- the world (2026-09-02) -------------------------------------
        //
        // Twenty opcodes that read or write game state the sweep's reference
        // stubs. Every one fetches its fields through the shared fetch (each
        // handler opens with the `cmp ax,0FFFFh / test ah,40h` idiom, or
        // `Script_FetchOperand`), and every one is skipped by the dry-run
        // flag `dword_6A05E0`, which this interpreter never sets. The
        // recorded call carries the RESOLVED fields in the handler's own
        // order, which is what `vm_announce` indexes. `worldWrites_` off
        // falls through to the stub recorder below, byte for byte as before.
        const auto recordCall = [&](std::uint8_t o, std::initializer_list<std::int32_t> fs) {
            if (!(record_ && !recordAll_)) return;
            Call c; c.op = o;
            for (const auto f : fs) c.fields.push_back(static_cast<std::int16_t>(f));
            r.calls.push_back(std::move(c));
        };
        if (worldWrites_) {
            // ---- the object lists (docs/GAME_STATE.md 3) ----
            // 49 `var.set.has_object` (0x40A440): list, object, VARIABLE -
            // `lea eax,[esi+esi*2]` indexes the list table with the FIRST
            // fetch and the second is what is announced to OBJECTS. The scan
            // runs over the stored count; -1 when none; 1 or 0 stored.
            if (op == 49) {
                std::size_t q = start + 1;
                const auto list = fetch16(code, q);
                const auto id   = fetch16(code, q);
                const auto var  = fetch16(code, q);
                state_.setVar(var, state_.listHas(list, id) ? 1 : 0);
                recordCall(op, {list, id, var});
                continue;
            }
            // 50 `inventory.add` (0x40A4D0): list, object. `cmp edi,3 / cmp
            // edi,2` - lists 2 and 3 scan for the id first and refuse a
            // duplicate; 0 and 1 do not. Then `Archive_ReadChunk("IAM\OBJECT",
            // id, 1304, 2048)` -> `Inventory_Insert(rec, list, playerId)` ->
            // `ObjectList_InsertFront`. The FRONT insert and the full-list
            // refusal are `listAdd`'s. NOT modelled, and labelled in
            // gamestate.h: `Inventory_Insert`'s kind gate - seteks (kind 12)
            // and rings (13) go to Argent/Anneaux through `Object_ApplyEffect`
            // and take no slot; ammunition (7..11) merges into a carried gun
            // (kind-5) when there is one; a gun (2..6) already carried takes
            // no second slot. Every shipped `inventory.add` takes a slot here.
            if (op == 50) {
                std::size_t q = start + 1;
                const auto list = fetch16(code, q);
                const auto id   = fetch16(code, q);
                state_.listAdd(list, id, GameState::listRefusesDuplicates(list));
                recordCall(op, {list, id});
                continue;
            }
            // 51 `inventory.remove` (0x40A5A0): one copy, tail shifted down,
            // 0xFFFF into the LAST slot, count decremented.
            if (op == 51) {
                std::size_t q = start + 1;
                const auto list = fetch16(code, q);
                const auto id   = fetch16(code, q);
                state_.listRemove(list, id);
                recordCall(op, {list, id});
                continue;
            }
            // 52 `inventory.remove_all` (0x40A6A0): the same removal until
            // the id is gone. Its id == -1 arm (`loc_40A7D8`, 37 shipped
            // sites, all `0, -1`) walks the cache for entries with `+0x24`
            // bit 1 - the OBJECT record's "lost on reincarnation" flag - and
            // then looks for the entry to remove with `cmp word ptr [ecx],
            // 0FFFFh` over the first `count` ids: the compiler folded the
            // operand -1 into the find, so it searches for the TERMINATOR
            // inside the counted run and never finds it. Nothing is ever
            // removed by that arm; `listRemoveAll(list, -1)` returns 0 too.
            if (op == 52) {
                std::size_t q = start + 1;
                const auto list = fetch16(code, q);
                const auto id   = fetch16(code, q);
                state_.listRemoveAll(list, id);
                recordCall(op, {list, id});
                continue;
            }
            // 128 `inventory.transfer` (0x405810): from, to, the COUNT
            // variable, the RESULT variable. `n = Var_Get(count)`, -1 meaning
            // everything; clamped to the source count and to the free room in
            // the destination (`ObjectList_Capacity - ObjectList_Count`); then
            // n times `ObjectList_InsertFront(to, ids[from][0])` and
            // `ObjectList_RemoveAt(from, 0)` - so the moved run arrives
            // REVERSED at the front of `to`; and `Var_Set(result, n)` unless
            // the result variable is -1. A negative n moves nothing and is
            // stored as is (`test ebp,ebp ; jle`). 3 shipped sites, all SCENE
            // 7, between lists 0 and 1 with variable 270 on both ends.
            if (op == 128) {
                std::size_t q = start + 1;
                const auto from = fetch16(code, q);
                const auto to   = fetch16(code, q);
                const auto cv   = fetch16(code, q);
                const auto rv   = fetch16(code, q);
                const auto cap  = [](int l) { return l >= 0 && l < GameState::kLists
                                                  ? GameState::kListCapacity[l] : 0; };
                std::int32_t n = state_.var(cv);
                if (n == -1) n = state_.listCount(from);
                if (n > state_.listCount(from)) n = state_.listCount(from);
                const auto room = cap(to) - state_.listCount(to);
                if (n > room) n = room;
                for (std::int32_t k = 0; k < n; ++k) {
                    const int id = state_.listAt(from, 0);
                    state_.listAdd(to, id, false);       // InsertFront, no duplicate test
                    state_.listRemove(from, id);         // RemoveAt(from, 0): index 0 is
                                                         // the first occurrence
                }
                if (rv != -1) state_.setVar(rv, n);
                recordCall(op, {from, to, cv, rv});
                continue;
            }

            // ---- the timer (docs/GAME_STATE.md, "The script timer") ----
            // 110 (0x405340 -> sub_41E260) resets; 111 (0x405350 ->
            // loc_41E2B0) STOPS and 112 (0x405360 -> loc_41E2D0) STARTS -
            // the table's names are the wrong way round, see gamestate.h;
            // 113 (0x405370) is `Timer_SetValue(field * 1000)` - the three
            // `lea` x5 and the `shl 3` - and 114 (0x4053D0) `Timer_SetMode
            // (field)`, both refused unless stopped; 115 (0x405420) is
            // `Timer_Elapsed(&v); Var_Set(field, v)`, stored whether or not
            // the timer runs.
            if (op == 110) { state_.timerReset(); recordCall(op, {}); continue; }
            if (op == 111) { state_.timerStop();  recordCall(op, {}); continue; }
            if (op == 112) { state_.timerStart(); recordCall(op, {}); continue; }
            if (op == 113) {
                std::size_t q = start + 1;
                const auto v = fetch16(code, q);
                state_.timerSet(v * 1000);
                recordCall(op, {v});
                continue;
            }
            if (op == 114) {
                std::size_t q = start + 1;
                const auto v = fetch16(code, q);
                state_.timerMode(v);
                recordCall(op, {v});
                continue;
            }
            if (op == 115) {
                std::size_t q = start + 1;
                const auto var = fetch16(code, q);
                std::int32_t v = 0;
                state_.timerElapsed(v);
                state_.setVar(var, v);
                recordCall(op, {var});
                continue;
            }

            // ---- the player and the stat block ----
            // 91 `var.set.player_id` (0x404530): `Var_Set(field,
            // Actor_IdBySlot(Actor_Player()))` - the DB player record's +272
            // (see `playerId()` above for why that is the same number).
            if (op == 91) {
                std::size_t q = start + 1;
                const auto var = fetch16(code, q);
                state_.setVar(var, playerId());
                recordCall(op, {var});
                continue;
            }
            // 86 `var.set.actor_stat` (0x404230): actor, property, VARIABLE.
            // `-1` -> `Actor_Player()`, else `Scene_FindObjectRecord`'s index;
            // then `Actor_GetProperty({property, ?, index})` and `Var_Set
            // (var, block+8)`. For a property that fills the pointer slot
            // instead (0 `Sexe` at 5 sites) or an actor no record names,
            // block+8 is whatever the stack held: the port leaves the
            // variable. 459 of 460 sites name the player.
            if (op == 86) {
                std::size_t q = start + 1;
                const auto actor = fetch16(code, q);
                const auto prop  = fetch16(code, q);
                const auto var   = fetch16(code, q);
                std::int32_t v = 0;
                if (getProperty(actor, prop, v)) state_.setVar(var, v);
                recordCall(op, {actor, prop, var});
                continue;
            }
            // 93 `actor.stat.set` (0x404790): actor, property, VARIABLE -
            // `Var_Get(var)`, then `Hud_ShowValue` for the player (the HUD
            // flash, output) and `Actor_SetProperty({property, value,
            // index})` for both arms, clamps included (`props.h`). Named
            // `hud.show_var` until 2026-09-02 after the flash rather than
            // the write.
            if (op == 93) {
                std::size_t q = start + 1;
                const auto actor = fetch16(code, q);
                const auto prop  = fetch16(code, q);
                const auto var   = fetch16(code, q);
                setProperty(actor, prop, state_.var(var));
                recordCall(op, {actor, prop, var});
                continue;
            }

            // ---- the held object and the props: hook-dependent ----
            // 75 `var.set.used_object` (0x40AC90): VARIABLE; `Actor_HeldObject
            // Slot(Actor_Player())`, and `word_4E6CA0[slot]` when the slot is
            // in 0..49 (`jl` / `cmp eax,32h ; jge`), else -1 - so with nothing
            // held the variable reads -1, not its old value. It is the opcode
            // `Script_RunToOpcode75` stops at when an inventory item is used
            // on a zone; the activate script branches on it. 235 sites.
            if (op == 75 && hooks_) {
                std::size_t q = start + 1;
                const auto var  = fetch16(code, q);
                const int  slot = hooks_->heldObjectSlot(-1);
                const int  id   = slot >= 0 && slot < 50 ? hooks_->objectIdInSlot(slot) : -1;
                state_.setVar(var, id);
                recordCall(op, {var});
                continue;
            }
            // 67 `object.hold.actor` (0x40A9D0): actor, object. The prop
            // record by id (+2) in the AREA then the SCENE; then `Actor_Find
            // ById(actor)->+270 == object` means nothing to do; else, when a
            // record was found and its `+0` slot is not -1, `Actor_HoldObject
            // (index, slot)`; and `+270 = object` either way. No prop-state
            // write. 159 sites.
            if (op == 67 && hooks_) {
                std::size_t q = start + 1;
                const auto actor = fetch16(code, q);
                const auto obj   = fetch16(code, q);
                PropRef p;
                const bool have = hooks_->propById(obj, p);
                if (heldField(actor) != obj) {
                    if (have && p.slot != -1) hooks_->holdObject(actor, p.slot);
                    setHeldField(actor, obj);
                }
                recordCall(op, {actor, obj});
                continue;
            }
            // 68 `object.release` (0x40AAF0), no operand: the PLAYER drops
            // what it holds. `Actor_HeldObjectSlot(Actor_Player())`, -1 ->
            // nothing at all. Then the prop record whose `+0` is that SLOT
            // (`movsx edi, word ptr [edx] ; cmp edi, ebx`), AREA then SCENE.
            // Found with state bit 0 set (`test al,1`): `Actor_ReleaseObject
            // (player, 0)` - the prop is dropped where it was - then
            // `ObjectState_Set(idx, state & ~2)` and `Object_HideFromScene
            // (slot)`. Otherwise `word_4E6CA0[slot] = -1` and `Actor_Release
            // Object(player, 1)` - freed. Both arms end with the player
            // record's +270 = -1. 226 sites.
            if (op == 68 && hooks_) {
                const int slot = hooks_->heldObjectSlot(-1);
                if (slot != -1) {
                    PropRef p;
                    const bool have = hooks_->propBySlot(slot, p);
                    if (have && (state_.propState(p.stateIndex) & 1)) {
                        const int st = state_.propState(p.stateIndex);
                        hooks_->releaseObject(-1, false);
                        state_.setPropState(p.stateIndex, st & ~2);
                        hooks_->hideObject(slot);
                    } else {
                        hooks_->clearObjectSlot(slot);
                        hooks_->releaseObject(-1, true);
                    }
                    setHeldField(-1, -1);
                }
                recordCall(op, {});
                continue;
            }
            // 69 `object.release.actor` (0x40AC20): actor. `Scene_FindObject
            // IndexById` -> `Actor_HeldObjectSlot`, -1 -> nothing; else
            // `Actor_ReleaseObject(index, 0)` - dropped, not freed; the
            // issue's `Actor_SetState(actor, 0)` is that function under its
            // old name - and `Actor_FindById(actor)->+270 = -1`. No prop
            // state. 499 sites.
            if (op == 69 && hooks_) {
                std::size_t q = start + 1;
                const auto actor = fetch16(code, q);
                if (hooks_->heldObjectSlot(actor) != -1) {
                    hooks_->releaseObject(actor, false);
                    setHeldField(actor, -1);
                }
                recordCall(op, {actor});
                continue;
            }
            // 76 `object.show` (0x40ACF0): object. The prop record by id,
            // AREA then SCENE; `ObjectState_Get(+22)` bit 0 set (`test al,1`)
            // -> `ObjectState_Set(+22, state | 2)` and `Object_ShowInScene
            // (+0)`. Bit 0 clear: nothing. No record: the engine reads
            // `[0+16h]` - the Win9x null page - and the port does nothing.
            // 251 sites.
            if (op == 76 && hooks_) {
                std::size_t q = start + 1;
                const auto obj = fetch16(code, q);
                PropRef p;
                if (hooks_->propById(obj, p) && (state_.propState(p.stateIndex) & 1)) {
                    state_.setPropState(p.stateIndex, state_.propState(p.stateIndex) | 2);
                    hooks_->showObject(p.slot);
                }
                recordCall(op, {obj});
                continue;
            }
            // 98 `object.place_at` (0x404DB0): object, address. `sub_40AF00`
            // finds the object's slot in the id table, `Address_Find` the
            // ADDRESSES record, `sub_41CF50` moves the node - and nothing in
            // the DB or a chunk record changes. 6 sites.
            if (op == 98 && hooks_) {
                std::size_t q = start + 1;
                const auto obj  = fetch16(code, q);
                const auto addr = fetch16(code, q);
                hooks_->placeObjectAt(obj, addr);
                recordCall(op, {obj, addr});
                continue;
            }
        }

        // ---- everything else: stubbed, and recorded --------------------
        if (record_ && !recordAll_) {
            Call c; c.op = op;
            for (std::size_t k = 0; k + 2 <= raw.size(); k += 2)
                c.fields.push_back(i16at(raw, k));
            r.calls.push_back(std::move(c));
        }
    }
}

}  // namespace omk
