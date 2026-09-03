// SPDX-License-Identifier: GPL-3.0-or-later
// Zones, the script scheduler and the trigger lifecycle.
//
// script/interp.h executes ONE script. This is what decides *which* runs and
// *when*: the resident chunks' trigger zones, the per-context action FIFO and
// the pump that drains it. From Zones_RegisterAll (0x00406560), Script_Pump
// (0x00407DC0), Script_ProcessActions (0x00408220), Script_QueueAction
// (0x004063D0), Script_Execute (0x00406460) and Actor_ScanZones (0x00467770).
//
// The shape, small once they are read together:
//
//   * a zone is registered only while its **save bit** is set (Zone_StateBit,
//     the game-DB bitmap `zone.enable`/`disable` flips) - which is how a spent
//     trigger stays retired across a save;
//   * standing inside its quad is a TOUCH (event 8, which asks for the zone's
//     camera); facing into its arc as well is event 7, which arms one of the
//     16 prompt slots;
//   * the pump turns an armed slot into a context holding the zone's three
//     script slots and queues action 1. Script_ProcessActions drains ONE
//     action a frame and Script_Execute then runs it:
//
//         1 -> slot +0, the enter script
//         2 -> slot +4, the activate script (where dialog.start lives),
//              queued only when the player presses action inside the zone
//         3 -> slot +8, the leave script
//         4 -> free
//
//   * an action does not finish because its script returned. The context keeps
//     the engine's status word, and a handler that PARKS it - `ui.open` (6),
//     `camera.set.wait` (7), a waiting `scx.play*` (4), `dialog.start` (which
//     leaves it at 1 and stops the world instead) - holds the action open
//     across frames. The pump RESUMES it from the saved pc; only `end` ends it.
#pragma once

#include "script/gamestate.h"
#include "script/interp.h"
#include "script/script.h"

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace omk {

inline constexpr std::size_t kZoneStride = 68;

// The loaders' own unit conversion: (100 * v) * 0.00390625 * 0.3937... - 1.
// Reading the quad raw is self-consistent until something else is in the same
// frame, which is why it survived until an actor at an authored ADDRESSES
// position came out 43000 units from the zone he was standing in.
inline constexpr double kZoneUnit = 100.0 * 0.00390625 * 0.3937007874015748;

struct Zone {
    std::int32_t scripts[3] = {0, 0, 0};   // +0 enter, +4 activate, +8 leave
    double       quad[4][3] = {};          // four corners, converted
    std::uint16_t arcMid  = 0;             // +60, 0..4095
    std::uint16_t arcWide = 0;             // +62; width 0 means any facing
    std::int16_t  id      = 0;             // +64
    std::int16_t  camera  = 0;             // +66

    // The id's top bit is a FLAG, not part of the id, and the engine reads it
    // both ways in the same frame:
    //
    //   * `Zone_StateBit` (0x0040D500) indexes the save bitmap with
    //     `(id & 0x7FFF) / 8` - so the bit is masked away for registration;
    //   * `Script_Pump` state 2 tests `byte ptr [esi+0Bh], 80h` - the high
    //     byte of the u16 id the prompt slot copied - right after queueing
    //     the activate, and latches the slot out of the press cycle when it
    //     is set (see `Context::spent`).
    //
    // 37 of the 4558 shipped zones carry it.
    bool         oneShot()   const { return (static_cast<std::uint16_t>(id) & 0x8000) != 0; }
    std::int16_t stateBit()  const { return static_cast<std::int16_t>(id & 0x7FFF); }

    // Point in the quad's XZ footprint, by the crossing count - the quad is
    // authored as four corners in order.
    bool contains(double x, double z) const;
    // Is `facing` inside the arc?
    bool faces(int facing) const;
    void centre(double out[3]) const;
};

// Every zone record of one chunk, registered or not.
std::vector<Zone> zonesOf(std::span<const std::byte> chunk, ChunkKind kind);

// The chunk's own STARTUP script, at `+4`. It is in no record table, so the
// zone pump can never reach it - `Area_TickLoad` (0x0040C7E0) case 9 is the
// only path that does:
//
//     mov  esi, dword_69BC40      ; the AREA block (then 69BC44, the SCENE)
//     mov  ecx, [esi+4]           ; <-- the startup script
//     call sub_406290             ; Script_NewContext(slot, script, 0, 0)
//     mov  [esi], eax             ; the context goes to block +0
//     call sub_4063D0             ; Script_QueueAction(ctx, 1)
//
// which also explains the header's `+0`: it is 0 on disk because that is
// where the running context goes.
//
// -> the offset, or 0 for a chunk with none (157 of the 330 shipped).
std::size_t startupScript(std::span<const std::byte> chunk);

// Script_NewContext's block, reduced to what the pump uses.
struct Context {
    Zone zone;
    std::int32_t slots[3] = {0, 0, 0};
    std::deque<int> queue;               // the 4-deep action FIFO (+24, count +28)
    // The engine's status word at +22: 0 idle, 1 running. A PARKED script is
    // still 1 (or 4/6/7, which this reduces to "not idle" plus the specific
    // wait below), which is what stops Script_ProcessActions arming the next
    // action underneath a script that has not finished.
    int  status = 0;
    bool freed  = false;
    // The engine's +32, the CURRENT action - written by Script_ProcessActions
    // when it dequeues one and cleared by Script_Execute's tail (LABEL_8:
    // `if (+32 == 2 && !status) +32 = 0`) when an ACTIVATE finishes. It is
    // half of Script_QueueAction's refusal test, so it is a real field and not
    // a convenience: while it reads 2 no second activate can be queued, and it
    // reads 2 for exactly as long as the activate script has not ended -
    // parked at a camera move, a screen or a conversation included.
    int  act    = 0;
    // Zone id bit 15, carried off the record.
    bool oneShot = false;
    // The one-shot LATCH - `Script_Pump` slot states 4 and 5.
    //
    // Read from the assembly rather than from the issue list, because the two
    // disagree: state 2 sets the slot to 5 after queueing the activate, but
    // `Game_HandleEvent` case 7 - raised by Actor_ScanZones every frame the
    // player is still armed - maps 5 back to 4, and pump case 4 maps 4 to 5.
    // The pump runs BEFORE the actor tick in Game_Tick, so those two ping-pong
    // for as long as the player stands there and the slot never re-enters
    // state 2. It is therefore not "freed after activating": it is latched out
    // of the press cycle, and freed on leaving like any other zone.
    //
    // What the latch buys is exactly the difference the LABEL_8 clear opens
    // up: an ordinary zone whose activate script reaches `end` can be
    // activated again by the next press, and a one-shot one cannot.
    bool spent   = false;

    // ---- the parked script (Script_Execute's status word, kept) ----------
    std::unique_ptr<Interpreter> vm;     // owned, so the STACK survives a park
    std::size_t entry = 0;               // the action's script offset
    std::size_t pc    = 0;               // where it stopped
    bool started = false;
    // `ui.open` parks at status 6 and only `Game_HandleEvent` case 5 - a
    // PERSON answering the screen - releases it. Nothing in `World` can, and
    // inventing a release would be inventing an answer.
    bool waitingForUi = false;
    // `camera.set.wait` parks at status 7 for the length of the move it
    // issued. A COUNTDOWN in frames, not a flag - the same model area.h's
    // `Session::Ctx` uses, and for the same reason.
    int  waitingForCamera = 0;
    RunStatus park = RunStatus::End;     // the status it stopped at
};

enum Action { ActEnter = 1, ActActivate = 2, ActLeave = 3, ActFree = 4 };

struct RanScript {
    std::int16_t zone = 0;
    int          action = 0;
    std::size_t  offset = 0;
    RunStatus    status = RunStatus::End;
};

class World {
public:
    World(std::span<const std::byte> chunk, ChunkKind kind,
          GameState& state, const OpcodeTable& table);

    // Zones_RegisterAll: only those whose save bit is set.
    std::vector<Zone> registered() const;

    // One frame at that position: raise the touch/arm events, queue the
    // actions they imply, then pump once.
    //
    // `action` is the engine's `dword_4E6C90` - the flag `Game_HandleEvent`
    // case 6 sets and the pump clears at the end of every frame. Whether the
    // input layer sets it on a press EDGE or on the level is outside `World`;
    // passing it true on consecutive frames is the worst case, and the
    // activate dedupe below is what the engine answers it with.
    void step(const double pos[3], int facing, bool action);

    const std::vector<RanScript>& ran() const { return ran_; }
    const std::vector<Call>& calls() const { return calls_; }

    // ---- the TOUCH camera (Game_HandleEvent case 8) ----------------------
    //
    // Actor_ScanZones raises event 8 for every registered zone the player's
    // point is inside, BEFORE the facing test, and case 8 reads the zone's
    // `+66`: when it is not -1 it calls `Camera_FindWorld` and points the
    // camera at the request block. 54 of the 4558 shipped zones carry one,
    // over 40 distinct cameras.
    //
    // Recorded, not modelled: `World` has no camera. The last touch of the
    // step wins, because each event 8 overwrites the same request block.
    int touchedCamera() const { return touchCamera_; }     // -1 if none this step
    int touches() const { return touches_; }               // event 8 count, cumulative
    int cameraRequests() const { return cameraRequests_; } // of which asked for a camera

    // ---- what Script_QueueAction(ctx, 2) actually did ---------------------
    //
    // `activatesQueued` is the engine's own `dword_4E6B20`: incremented once
    // per call that returns 1. `activatesRefused` counts the returns of 0 -
    // the dedupe firing.
    int activatesQueued() const { return activatesQueued_; }
    int activatesRefused() const { return activatesRefused_; }

    // ---- a conversation stops the world ----------------------------------
    //
    // `Script_Pump` opens `if (g_DialogState != 1) return 1;`, so while a
    // conversation is up NOTHING pumps - not the prompt slots, not the
    // contexts. Actor_ScanZones is in the actor tick and is not part of that,
    // so touches keep being recorded. Nothing in `World` ends a conversation,
    // which is why closing it is the caller's (`Game_HandleEvent` case 63).
    bool dialogOpen() const { return dialogState_ == 3; }
    void endDialog() { dialogState_ = 1; }

    // Whether `camera.set.wait` (96) and the waiting `scx.play*` park their
    // context. Off by default for the same reason area.h's pair is - see
    // `Interpreter::setCameraWaitSuspends`.
    void setCameraWait(bool on) { camWait_ = on; }
    void setObjectWait(bool on) { objWait_ = on; }

    // Every context currently held mid-script, and on what.
    struct Parked {
        std::int16_t zone = 0;
        int          action = 0;
        RunStatus    status = RunStatus::End;
        std::size_t  pc = 0;
        int          framesLeft = 0;     // a camera move's remaining length
    };
    std::vector<Parked> parked() const;

private:
    std::span<const std::byte> code_;
    GameState&         state_;
    const OpcodeTable& table_;
    std::vector<Zone>  all_;
    std::vector<Zone>  live_;
    std::map<std::int16_t, Context> slots_;   // the 16 prompt slots, by zone id
    std::vector<RanScript> ran_;
    std::vector<Call>      calls_;
    int  touchCamera_ = -1;
    int  touches_ = 0;
    int  cameraRequests_ = 0;
    int  activatesQueued_ = 0;
    int  activatesRefused_ = 0;
    int  dialogState_ = 1;
    bool camWait_ = false;
    bool objWait_ = false;

    // Script_QueueAction (0x004063D0), returning what it returns.
    int  queueAction(Context& c, int act);
    void pump();
    void process(Context& c);
};

}  // namespace omk
