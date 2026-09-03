// SPDX-License-Identifier: GPL-3.0-or-later
// The script VM, EXECUTED rather than disassembled.
//
// `Script_Execute` (0x00406460) clears the dry-run flag and loops while the
// context's status word is 1, fetching one opcode and dispatching through the
// 153-entry table. This is that loop, with the ~20 opcodes carrying control
// flow implemented for real against a GameState and every other opcode
// **stubbed and recorded**.
//
// Stubbing is the design, not a shortcut: what a replica must first reproduce
// is what the game DECIDES. `camera.set` (3659 sites), `media.play`, `scx.play*`
// and `dialog.start` leave the control flow alone, so a run that records them
// is a complete trace of the decisions even though nothing is drawn.
#pragma once

#include "script/gamestate.h"
#include "script/hooks.h"
#include "script/script.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace omk {

enum class RunStatus {
    End,            // reached `end`
    Dialog,         // stopped at `dialog.start`: Script_Execute returns outright
                    // there, so the script does NOT continue - 140 of the world
                    // scripts end this way and counting them as `end` would
                    // silently execute code the engine never reaches
    UiOpen,         // stopped at `ui.open`: the handler (0x00403860) parks its
                    // caller at status 6 and names a result VARIABLE, and only
                    // `Game_HandleEvent` case 5 - the screen answering - writes
                    // that variable and resumes it. So a script waiting here is
                    // waiting on a PERSON, and nothing in the pump can release
                    // it. AREA 118's startup script parks here at pc 1078 for
                    // screen 29, the start menu, and resumes into
                    // `dialog.start 272`.
    CameraWait,     // stopped at `camera.set.wait` (96) or at
                    // `camera.set.at_address` (126): both handlers issue the
                    // same mode-12 request `camera.set` (95) does and then
                    // WRITE THE STATUS WORD - 7 - with a resume id, so
                    // `Script_Execute`'s `while (status == 1)` drops out and
                    // the script is held until the camera move ends
                    // (docs/SCRIPT_VM "status is how an opcode blocks").
                    // `camWaitOp` says WHICH, and the difference is not
                    // cosmetic: 96 guards the status write with
                    // `test ebp,ebp; jz` so a 0-frame move is a cut that holds
                    // nothing, and 126 has no such guard at all - a 0-frame
                    // cut parks too. Both are released by the same event 4.
    MoveWait,       // stopped at `player.move.wait` (89, 0x004043F0): the
                    // handler is `Player_GoToMove(address, ctx+30)` - the
                    // caller's own slot where 63 passes -1 - and then
                    // `mov word ptr [esi+16h], 4`. So the same status 4 the
                    // waiting `scx.play*` variants write, released by the same
                    // `Game_HandleEvent` case 3, but on the PLAYER'S MOVE
                    // ending rather than a scene object's program.
                    // `moveAddress` is where he is being sent.
    FightWait,      // stopped at `fight.begin` (62, 0x004035D0): the handler
                    // resolves the opponent, puts both bodies into fight mode
                    // (`Fight_Begin` = sub_41A3B0), writes status **3** and
                    // requests camera mode 14 with
                    // `dword_930818 = max(field 1, 0)`. Only
                    // `Game_HandleEvent` case 2 - raised when the fight ends -
                    // walks the table and puts the FIRST context at 3 back
                    // to 1.
    ObjectWait,     // stopped at a WAITING `scx.play*` - 46, 58 or 60. Those
                    // three hand `ScriptObject_Start` the caller's own slot
                    // instead of -1, and it ends `mov [esi+16h], 4`, so the
                    // object finishing is what releases the script. It is the
                    // beat before a cutscene's dialogue: AREA 118 shows Kay'l,
                    // starts his animation with 60, and waits about five
                    // seconds while the camera travels.
    Suspended,      // the status word left RUNNING - normal, the scheduler resumes
    Runaway,
    PcOutOfRange,
    UnknownOpcode,
    StackUnderflow,
};

struct Call {                 // one stubbed subsystem call, as recorded
    std::uint8_t op = 0;
    std::vector<std::int16_t> fields;
};

struct RunResult {
    RunStatus status = RunStatus::End;
    std::size_t pc = 0;
    // set when status is UiOpen: which screen, its parameter, and the
    // variable the answer must be written into before resuming
    int uiScreen = -1, uiParam = -1, uiResultVar = -1;
    // set when status is CameraWait: the camera the move ends on and how many
    // frames it takes. The same two fields `camera.set` carries, which is why
    // a caller can treat 95 and 96 alike for the FRAMING and differently only
    // for the hold.
    int camId = -1, camTravel = 0;
    // ...and WHICH opcode parked it - 96 or 126 - because the two differ in
    // exactly one thing a caller must not lose: **a 0-frame travel parks under
    // 126 and does not under 96**. 96's handler guards the status-7 write with
    // `test ebp,ebp; jz loc_404CB1`; 126's writes it in a straight line. A
    // caller that only looked at `camTravel` would cut where the engine holds.
    // 0 = neither (no camera opcode parked this run).
    int camWaitOp = 0;
    // 126 only: the ADDRESSES id whose record becomes the camera's subject.
    // Where 96 calls `Actor_Player()` twice for `dword_930808`/`0930C`, 126
    // calls `Address_Find(field 1)` and stores that in both. -1 for 96.
    int camAddress = -1;
    // set when status is ObjectWait: which opcode parked it, so a caller can
    // start the right thing before parking.
    int objectWaitOp = 0;
    // set when status is MoveWait: the ADDRESSES id `Player_GoToMove` is
    // walking the player to. The Session has to START that move - the
    // interpreter only reports the decision - and only its ending releases
    // the context.
    int moveAddress = -1;
    // set when status is FightWait: the opponent's id (field 0, resolved
    // through the shared fetch) and the fight camera's travel, which is
    // `max(field 1, 0)` exactly as the handler's `jge` writes it.
    int fightOpponent = -1, fightCamTravel = 0;
    // `zone.enable`/`zone.disable` (64/65) RE-REGISTER, and that is not a
    // detail: the handler at 0x004037F0 ends
    //
    //     push 0 / push esi / call sub_40D540      <- clear the DB bit
    //     mov ecx, dword_69BC60 / push ecx
    //     call sub_406560                          <- Zones_RegisterAll()
    //
    // so the live list is rebuilt on the spot. Setting the bit alone leaves a
    // disabled zone in the scan until the next area load, and it keeps firing:
    // AREA 222's tutorial disables ITSELF as its last act, so without this it
    // re-triggers every time the player stands in the alley.
    // -> the caller must call `ZoneRegistry::registerAll` again this frame.
    bool zonesDirty = false;
    std::size_t steps = 0;
    std::vector<Call> calls;
};

class Interpreter {
public:
    Interpreter(GameState& state, const OpcodeTable& table)
        : state_(state), table_(table) {}

    // `params` is the int16 block at context +36 that an INDIRECT operand
    // indexes, laid out AS THE ENGINE FILLS IT; empty is the normal case for
    // a world script. `Message_RunHandlers` (0x00409420) allocates 8 bytes,
    // writes `args[0] = message id, args[1] = sender`, and the shared fetch
    // reads `[block + 2*i + 2]` - so operand `0x4000` (index 0) is the
    // block's SECOND word, the sender, not the message id. Every shipped
    // indirect operand is that one, and every one of them is compared
    // against an actor or object id (`push.i8 173; push.i16 0x4000; eq`), which
    // is what settles it: a script subscribed to message 3 has nothing to
    // learn from the number 3. Pass `{message id, sender}` here and the
    // fetch does the offset, the way the handler does.
    void setParams(std::vector<std::int16_t> p) { params_ = std::move(p); }

    // The generator behind `var.set.random` (120). The engine's is the C
    // library's `rand()`, which no host reproduces, so what is ported is
    // WHERE randomness enters and the no-repeat rule around it, not which
    // numbers come out (`o3de/particles.h` makes the same argument for the
    // emitters). A check seeds it so a run is a fixed sequence.
    //
    // The state is SHARED by every interpreter, as the engine's is: `rand()`
    // has one seed for the process and `dword_4E7E8C` - the previous draw -
    // is one global, so a passer-by line drawn by one script is the one the
    // next script cannot draw. Seeding also forgets the previous draw.
    static void seedRandom(std::uint32_t s) { rng_ = s ? s : 1u; lastRandom_ = 0; }

    // Whether `var.set.random` WRITES its variable (the engine's behaviour,
    // and the default) or is stubbed and only recorded.
    //
    // For the fourth time the same reason as the three switches below: the
    // corpus sweep compares the final game DB byte for byte with `tools/sim`,
    // whose standalone VM stubs 120, and 235 shipped sites write a variable
    // that the sweep would then find differing. Off compares like with like;
    // everything that RUNS the game leaves it on.
    void setRandomWrites(bool on) { randomWrites_ = on; }
    // Record the STUBBED subsystem calls - what a decision trace is made of.
    void setRecordCalls(bool on) { record_ = on; }
    // Record EVERY executed opcode's operands, implemented ones included.
    //
    // Needed to narrate the way the engine does: `Dbg_LogTagged` fires from
    // the handler whether or not the opcode does anything a stub would model,
    // so a variable write announces exactly as loudly as a `camera.set`.
    // Recording only stubs makes a port look silent where the game is not,
    // and that shows up as a diff against a capture rather than as an
    // obviously missing feature.
    void setRecordAll(bool on) { recordAll_ = on; }

    // Whether `ui.open` PARKS the context (the engine's behaviour, and the
    // default) or is stubbed like any other subsystem call.
    //
    // It exists for one caller: the corpus sweep that decodes and executes
    // every slot and compares with `tools/sim`, whose standalone VM stubs
    // opcode 70 because it has no screens and no scheduler. Parking there
    // would make the two disagree about something neither is modelling, so
    // the sweep turns it off and compares like with like. Everything that
    // actually RUNS the game leaves it on.
    void setUiOpenSuspends(bool on) { uiSuspends_ = on; }

    // Whether `camera.set.wait` (96) and `camera.set.at_address` (126) PARK
    // the context for the length of the move, which is what both handlers do -
    // they write status 7 and are released by the same event 4. ONE switch for
    // the two because it is one status word and one release; `camWaitOp` in
    // the result says which opcode it was.
    //
    // **Default off, and the reason is the same as the switch above's.** The
    // corpus sweeps decode and execute every one of the 5785 slots and compare
    // with `tools/sim`, whose standalone VM stubs 96 because it has no camera
    // and no frame clock; parking here would make the two disagree about
    // something neither is modelling. So the default keeps them comparing like
    // with like, and everything that actually RUNS with a clock - the live
    // session - turns it on. What is NOT true is that the engine cuts through:
    // it holds, and this file says so.
    void setCameraWaitSuspends(bool on) { camWaitSuspends_ = on; }

    // Whether `player.move.wait` (89) PARKS the context until the walk it
    // ordered ends - status 4, released by `Game_HandleEvent` case 3.
    //
    // Default off, for the same reason as the two switches around it: the
    // corpus sweep compares against `tools/sim`, whose standalone VM stubs 89
    // because it has no player and no walker, and 548 shipped sites would
    // otherwise make the two disagree about something neither models. A
    // Session with a walker turns it on. **Without it the port runs the
    // instruction after the walk on the frame the walk is ordered** - "walk
    // there, THEN the conversation" becomes both at once
    // (todo/iam-script-engine.md 23).
    void setMoveWaitSuspends(bool on) { moveWaitSuspends_ = on; }

    // Whether `fight.begin` (62) PARKS the context until the fight ends -
    // status 3, released by `Game_HandleEvent` case 2.
    //
    // Default off, same reason again; 108 shipped sites. Note what turning it
    // ON commits a caller to: nothing in the pump can release status 3, so a
    // Session that parks here without a fight runtime to end the fight parks
    // for ever. That is the engine's shape - `Script_ProcessActions` refuses
    // to arm anything behind a non-zero status - but a replica with no combat
    // must leave this off rather than invent a deadlock, exactly as
    // `ObjectWait` refuses to park on a program that never started.
    void setFightWaitSuspends(bool on) { fightWaitSuspends_ = on; }

    // Whether the WAITING `scx.play*` variants (46, 58, 60) park the context
    // until the object's program ends, which is what their handler does.
    //
    // Default off, and for the third time the same reason: the corpus sweeps
    // compare against `tools/sim`, whose VM stubs all three because it has no
    // resident scene to run a program on. A caller that HAS one turns it on.
    // 58 alone is 1697 sites, so this is not a small change to a run.
    void setObjectWaitSuspends(bool on) { objWaitSuspends_ = on; }

    // ------------------------------------------------ THE WORLD OPCODES
    //
    // Twenty opcodes that read or write GAME STATE beyond the variables and
    // the bitmaps, stubbed until 2026-09-02 (todo/iam-script-engine.md 22,
    // 24, 29, 30, 33, 34, 37). They split by where the state lives:
    //
    //   in the DB, so on `GameState` and needing nothing else:
    //     49 `var.set.has_object`, 50 `inventory.add`, 51 `inventory.remove`,
    //     52 `inventory.remove_all`, 128 `inventory.transfer` - the object
    //     lists at +848/+884/+1396;
    //     110..115 - the timer, and 115 `var.set.timer` reading it;
    //     91 `var.set.player_id` - the DB player record's +272;
    //     86 `var.set.actor_stat` / 93 `actor.stat.set` when the actor is the
    //     PLAYER (-1, or the DB's own id: `Actor_FindById`'s first test), and
    //     the player's held-object field +270 that 67/68/69 write;
    //   in the engine's actor table, object slots and loaded blocks, which
    //   only a Session has - reached through `WorldHooks`:
    //     75 `var.set.used_object`, 67 `object.hold.actor`, 68
    //     `object.release`, 69 `object.release.actor`, 76 `object.show`, 98
    //     `object.place_at`, and 86/93 on any OTHER actor.
    //
    // `setWorldWrites(false)` turns ALL of them back into stubs - recorded and
    // inert - for the fifth time for the same reason as the switches above:
    // the corpus sweep compares its final DB byte for byte with `tools/sim`,
    // which stubs every one of them, and 975 inventory sites alone would
    // otherwise make the two differ on state neither side is checking
    // against the engine. `run_scripts` turns it off; everything that RUNS
    // the game leaves it on. A null hook stubs only the hook-dependent
    // opcodes: the DB half runs regardless, because the DB is always there.
    void setWorldWrites(bool on) { worldWrites_ = on; }
    void setHooks(WorldHooks* h) { hooks_ = h; }

    // Start a script: the stack begins empty.
    RunResult run(std::span<const std::byte> code, std::size_t at);

    // Continue one that stopped. `dialog.start` does NOT suspend its context -
    // the handler leaves it running with the pc past the operand and
    // Script_Execute returns for the frame; the whole pump then refuses to run
    // until the conversation closes, and the script picks up at the next
    // instruction. So the STACK has to survive, which is why a context owns
    // its interpreter rather than making a new one each frame.
    RunResult resume(std::span<const std::byte> code, std::size_t at);

    void resetStack() { stack_.clear(); }

    // The top of stack, for a caller that needs the VALUE a script left
    // rather than only whether it ran - `Dialog_EvalBranchCondition` takes
    // exactly this.
    std::optional<std::int32_t> stackTop() const {
        return stack_.empty() ? std::nullopt
                              : std::optional<std::int32_t>(stack_.back());
    }

private:
    GameState&         state_;
    const OpcodeTable& table_;
    std::vector<std::int16_t> params_;
    std::vector<std::int32_t> stack_;
    bool record_ = false;
    bool recordAll_ = false;
    bool uiSuspends_ = true;
    bool camWaitSuspends_ = false;
    bool objWaitSuspends_ = false;
    bool moveWaitSuspends_ = false;
    bool fightWaitSuspends_ = false;
    bool randomWrites_ = true;
    bool worldWrites_ = true;
    WorldHooks* hooks_ = nullptr;
    inline static std::uint32_t rng_ = 1u;       // xorshift32 state, never 0
    inline static std::int32_t  lastRandom_ = 0; // dword_4E7E8C: Random_NoRepeat's
                                                 // last draw (.bss: 0 before the
                                                 // first call)

    // The shared 16-bit operand fetch, indirection included. Read u16 LE,
    // advance 2; unless the value is 0xFFFF (which passes through as -1), bit
    // 0x4000 means INDIRECT - clear it and take the parameter block's word
    // at `2 * (value & 0x3FFF) + 2` instead, sign-extended
    // (`movsx eax, word ptr [ecx+eax*2+2]`). It is ONE function because it
    // is one idiom in the binary: the port runs it for 4, 5, 6, 8, 10,
    // 12-24, 42, 64, 65, 71, 87, 88 and 120 - every 16-bit field, not only
    // the variable index: `set.var.i16` (15) fetches its VALUE too, and
    // `set.var.var` (17) and `scene.load` (71) fetch both of theirs. What
    // does NOT go through it is the 1- and 4-byte immediate of 7, 9, 14 and
    // 16, read raw. The handlers of the stubbed 45, 47, 61, 70 and 96 run it
    // as well; their recorded fields here are the raw halves, and no
    // shipped operand of theirs carries the bit (measured 2026-09-02).
    std::int32_t fetch16(std::span<const std::byte> code, std::size_t& pc) const;

    // `Random_NoRepeat` (0x0041D6B0): an integer in [lo, hi] that is never
    // the one it returned last time; lo when lo == hi.
    std::int32_t randomNoRepeat(std::int32_t lo, std::int32_t hi);

    // ---- the world opcodes' helpers (interp.cpp, "the world") ----
    // The DB player record's +272 - `Actor_IdBySlot(Actor_Player())`.
    int  playerId() const;
    // `Actor_FindById`'s first test: -1, or the id the DB record carries.
    bool isPlayer(int actor) const { return actor == -1 || actor == playerId(); }
    // The record's +270 (the player's is DB +330; anyone else's is the hook's).
    int  heldField(int actor) const;
    void setHeldField(int actor, int objectId);
    // `Actor_GetProperty` / `Actor_SetProperty`, the player on the DB record
    // and anyone else through the hook. false = the engine reads garbage.
    bool getProperty(int actor, int property, std::int32_t& out) const;
    bool setProperty(int actor, int property, std::int32_t value);
};

}  // namespace omk
