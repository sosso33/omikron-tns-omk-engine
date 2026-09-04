// SPDX-License-Identifier: GPL-3.0-or-later
// The LIVE zone registry - `Zones_RegisterAll` (0x00406560) and
// `Actor_ScanZones` (0x00467770), the two halves the Session did not have.
//
// `script/world.h` already models a zone and the trigger lifecycle, but it
// takes ONE chunk of ONE kind: it is a harness, and the thing `omk` and
// `omk-play` actually run has no zone model at all, so in the live replica no
// zone ever arms and every conversation has to come out of a startup script
// (issue 10). This is the missing piece, and it is deliberately NOT a second
// scheduler: the `Zone` record, its containment test and the quad conversion
// are `world.h`'s and are reused verbatim. What is here is
//
//   * the four-table walk over BOTH resident slots, gated on the save bit;
//   * the PRUNE - contexts whose zone id no longer resolves because its area
//     was unloaded;
//   * the 16 prompt slots with the engine's own state words, driven by
//     `Script_Pump`'s slot loop and `Game_HandleEvent` case 7;
//   * the per-frame scan, raising the touch (event 8, which asks for the
//     zone's camera) and the arm (event 7).
//
// It DECIDES and it REPORTS; it does not run scripts. Every action the pump
// would queue comes back as a `ZoneEvent` carrying the zone, the script offset
// and the chunk it is relative to, and the Session turns that into its own
// `Script_QueueAction` - which is where the activate dedupe lives, because the
// FIFO and the context's `+32` are the context's, not the zone's.
//
// ---------------------------------------------------------------------------
// THE ORDER INSIDE A FRAME, WHICH IS LOAD-BEARING
//
// `Game_Tick` (05_sys.c) runs `Script_Pump(1)` at :2107 and `Actors_TickAll()`
// - and so `Actor_ScanZones` - at :2178. The pump therefore reads the slot
// states the PREVIOUS frame's scan left, and that one-frame offset is the
// whole of the one-shot latch: pump case 4 writes 5, case 7 writes 4, and the
// two ping-pong for as long as the player stands in the zone (T4's correction
// to issue 5, re-derived here from the same assembly).
//
// So the two halves are separate calls on purpose:
//
//     Session::frame():
//         zones_.pumpSlots(pressed, ev);     // Script_Pump's slot loop
//         ... queue ev, then run the contexts (Script_Pump's context loop)
//         ... tickCamera, scene_.tick
//         zones_.scanZones(pos, yaw, ev2);   // Actors_TickAll
//
// `scan()` does both in that order for a probe or a caller that does not care.
#pragma once

#include "script/gamestate.h"
#include "script/script.h"
#include "script/world.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace omk {

// One of the engine's two resident slots - `dword_69BC40` (the AREA block),
// `dword_69BC44` (the SCENE block over it), `dword_69BC48` (the area id) and
// `dword_69BC4C` (the scene id), 16 bytes apiece.
//
// `Zones_RegisterAll`'s two guards are exactly the id AND the block:
//
//     if (blocks[2] != -1 && blocks[0])   ... the AREA's table  (+48 / +76)
//     if (blocks[3] != -1 && blocks[1])   ... the SCENE's over it (+16 / +44)
//
// so a slot with a chunk and no id registers nothing, and vice versa. The
// spans point into buffers the CALLER owns and must keep alive for as long as
// the registry does - the same contract `World` has with its chunk.
struct ResidentSlot {
    std::span<const std::byte> area;    // the AREA chunk; empty for none
    std::span<const std::byte> scene;   // the SCENE chunk over it; may be empty
    int areaId  = -1;
    int sceneId = -1;
};

// A zone record with the provenance a caller needs to RUN its scripts: the
// three offsets are relative to `code`, which is the chunk the record came out
// of, and that is not always the resident AREA - a SCENE's zones sit over it.
struct LiveZone {
    Zone        zone;
    int         slot  = 0;                  // which resident slot, 0 or 1
    ChunkKind   kind  = ChunkKind::Area;    // which of the slot's two tables
    int         chunk = -1;                 // the AREA/SCENE chunk id
    std::span<const std::byte> code;        // that chunk
};

// `Script_Pump`'s own slot-state words (`u16i(slot, 4)`), kept as the engine
// numbers them so the docs and the code use one vocabulary.
//
//   0  empty                        the id at +10 is -1
//   1  new                          case 7 took a fresh slot; the pump makes
//                                   the context and queues the ENTER script
//   2  pressed                      the pump may queue the ACTIVATE this frame
//   3  armed                        the steady state; case 7 maps it back to 2
//                                   every frame the player is still facing in,
//                                   and a pump that finds it still 3 means the
//                                   player LEFT - leave, free, release
//   4  latched (scanned)            the one-shot half of the ping-pong
//   5  latched (pumped) / leaving   set by the pump from 2 (one-shot) or 4;
//                                   shares its body with 3 when nothing
//                                   re-armed it
enum ZoneSlotState {
    kZoneSlotEmpty   = 0,
    kZoneSlotNew     = 1,
    kZoneSlotPressed = 2,
    kZoneSlotArmed   = 3,
    kZoneSlotLatched = 4,
    kZoneSlotSpent   = 5,
};

// What the registry decided this frame. Everything but `Touch` is a
// `Script_QueueAction(ctx, action)` the Session must make.
struct ZoneEvent {
    enum class Kind {
        Touch,      // event 8   - the player is inside the quad
        Arm,        // slot 1    - context created, action 1 (the ENTER script)
        Activate,   // slot 2    - action 2 (the ACTIVATE script, where
                    //             `dialog.start` lives)
        Leave,      // slot 3/5  - action 3 (the LEAVE script)
        Free,       // slot 3/5  - action 4, the context's own end
    };

    Kind          kind   = Kind::Touch;
    std::int16_t  zone   = 0;      // the record's +64, FLAG BIT INCLUDED - it is
                                   // what the prompt slot and the context key on
    // Touch only: the record's +66 handed to `Camera_FindWorld`, -1 for none.
    int           camera = -1;
    // `Script_QueueAction`'s argument: 1 enter, 2 activate, 3 leave, 4 free.
    // 0 on a Touch, and on an Arm whose zone has no enter script - the engine
    // still takes the slot and still sets state 3, it just queues nothing
    // (`if (zoneScripts[0]) Script_QueueAction(...)`, both arms writing 3).
    int           action = 0;
    std::size_t   script = 0;      // the offset in `code`, 0 for none
    // The zone's three slots, so a caller can build the context the engine
    // builds - `Script_NewContext(slot, s[0], s[1], s[2])` - from one event.
    std::int32_t  scripts[3] = {0, 0, 0};
    int           slot   = 0;              // resident slot
    ChunkKind     chunkKind = ChunkKind::Area;
    int           chunk  = -1;
    std::span<const std::byte> code;
    bool          oneShot = false;
};

// The facing test, in DEGREES, transcribed from `Actor_ScanZones`.
//
// This is not `Zone::faces`, and the difference is a real one that no reader
// of the file alone would find: **the loader converts the arc.** `Area_Load`
// (01_file.c) runs, over every zone record,
//
//     u16(rec, 60) = (int)(u16(rec, 60) * 0.087890625);    // 360/4096
//     u16(rec, 62) = (int)(u16(rec, 62) * 0.087890625);
//
// so what `Actor_ScanZones` compares against the actor's facing (`f32(actor,
// 420)`, a float in degrees) is whole DEGREES, truncated - while the field on
// disk is a 4096-per-turn angle. `Zone::faces` works in the stored unit and is
// therefore the same arc rounded differently; it is kept for `World`, and this
// is what the live scan uses.
//
// The wrap arms are the engine's, both of them:
//
//     if (lo >= 0) { if (hi > 360 && facing < lo) facing += 360; }
//     else if (!(facing < lo + 360 && facing <= hi)) { lo += 360; hi += 360; }
//     inside = facing >= lo && facing <= hi;
//
// A width of 0 accepts any facing - the engine tests `if (u16(zone, 50))`
// before doing any of this.
bool zoneFacesDegrees(const Zone& z, double facingDegrees);

// The loader's own arc conversion, 360/4096.
inline constexpr double kZoneArcToDegrees = 0.087890625;

class ZoneRegistry {
public:
    // ------------------------------------------------ Zones_RegisterAll
    //
    //     Zones_Clear();
    //     for (slot = 0; slot < 2; ++slot) {
    //         if (areaId  != -1 && areaBlock)  walk(+48, i16 +76);
    //         if (sceneId != -1 && sceneBlock) walk(+16, i16 +44);
    //     }
    //     ... where walk() registers `record + 12` for every record whose
    //     Zone_StateBit(u16(rec+12, 52)) - the id at file +64, masked to 15
    //     bits - is set.
    //
    // The tail then prunes; the prompt-slot half of that is done here (a slot
    // whose zone no longer resolves is released), and the CONTEXT half is
    // `prune` below, because the contexts are the Session's.
    void registerAll(const std::vector<ResidentSlot>& slots,
                     const GameState& state);

    // `Script_Pump(1)`'s prompt-slot loop, on the states the PREVIOUS frame's
    // scan left. Appends the actions it queues.
    //
    // `actionPressed` is `dword_4E6C90`, which `Game_HandleEvent` case 6 sets
    // and this clears at the end - and case 6 REFUSES when no slot is armed
    // (`if (g_DialogState == 3 || a2 != 4 || !dword_4E6B24) return 0;`), which
    // is why the press is gated on `armedCount()` here rather than by the
    // caller.
    //
    // NOT modelled, and labelled rather than hidden: the case-2 INLINE run.
    // The engine runs the context's current script as a DRY RUN before
    // queueing (`Script_Run`, or `Script_RunToOpcode75` while the player holds
    // an object), and its return decides only whether the press counts as
    // consumed (`dword_4E66B8`, issue 7) - never whether the activate is
    // queued. With empty hands both outcomes fall into the same block, so the
    // queue is unconditional and this reproduces it exactly; with a HELD
    // object it does not, and `heldObject(true)` makes the registry say so by
    // refusing the queue rather than guessing.
    void pumpSlots(bool actionPressed, std::vector<ZoneEvent>& out);

    // `Actor_ScanZones`: every registered zone whose quad contains the point
    // raises event 8, BEFORE the facing test; one whose arc also matches
    // raises event 7 and arms a prompt slot.
    void scanZones(const double pos[3], double facingDegrees,
                   std::vector<ZoneEvent>& out);

    // Both, in `Game_Tick`'s order - the pump's slot loop, then the scan.
    std::vector<ZoneEvent> scan(const double pos[3], double facingDegrees,
                                bool actionPressed);

    // ------------------------------------------------ Zones_RegisterAll's tail
    //
    //     for (ctx : contexts)
    //         if (ctx.zoneId != -1 && !Zone_FindScriptsById(ctx.zoneId))
    //             detach its prompt slots and free it
    //
    // -> the ids that no longer resolve, i.e. the contexts the caller must
    // free. Call it right after `registerAll`, which has already released the
    // prompt slots. `Zone_FindScriptsById` (0x00406760) searches the resident
    // chunks' FULL record tables, not the registered subset, so a zone whose
    // save bit a script has just cleared is still resolvable and its context
    // survives - only unloading its area prunes it.
    std::vector<std::int16_t> prune(std::span<const std::int16_t> contextZones) const;

    // Whether the player has something in his hands - `Actor_HeldObjectSlot
    // (Actor_Player()) != -1`, which picks `Script_RunToOpcode75` over
    // `Script_Run` in pump case 2. See `pumpSlots`.
    void setHeldObject(bool held) { heldObject_ = held; }

    // ...and what the pump then ASKS, which is the other half of it and was
    // missing until 2026-09-04. `sub_406180`'s held-object arm does not queue
    // blind: it runs the ACTIVATE script from its start with
    // `Script_RunToOpcode75` and queues only if the script reaches opcode 75
    // (`var.set.used_object`) - i.e. only if this zone asks what is in the
    // player's hand. `sub_406120` restores the caller's pc on both exits, so
    // the dry run leaves no pc behind; what it does execute on the way there
    // is the script's own condition tests.
    //
    // This is the whole of "use the key AT THE LIFT and it opens, use it in
    // the street and nothing happens": AREA 229 (HALL27)'s activate script
    // reaches 75 at pc 1550 and compares the answer with object 6, and no
    // street zone mentions the opcode at all.
    //
    // The wrapper's OTHER arm is deliberately not ported: a script that ends
    // without reaching 75 still returns 1 when `player[+0x194] == 3` (and
    // sets `dword_4E66B8`), and that field is untraced - so this returns 0
    // there, which is the arm the shipped scripts take.
    //
    // Unset (the default) the probe answers false, which is what the port did
    // unconditionally before: a held object suppressed every activate.
    void setUsedObjectProbe(
        std::function<bool(std::span<const std::byte>, std::size_t)> p) {
        usedObjectProbe_ = std::move(p);
    }

    // ------------------------------------------------------------ accessors
    const std::vector<LiveZone>& registered() const { return live_; }
    // Every record of both slots' four tables, registered or not - what
    // `Zone_FindScriptsById` searches.
    const std::vector<LiveZone>& all() const { return all_; }
    const LiveZone* resolve(std::int16_t id) const;

    struct PromptSlot {
        std::int16_t zone  = -1;               // +10, -1 = empty
        int          state = kZoneSlotEmpty;   // +8
        int          areaSlot = 0;             // +12
    };
    const std::array<PromptSlot, 16>& promptSlots() const { return slots_; }
    // `dword_4E6B24` - how many slots are taken. Event 6 refuses the press
    // when this is 0, which is what stops a press anywhere in the world from
    // reaching the scripts.
    int armedCount() const { return armed_; }
    // The zone the player would activate - the first taken slot, or -1. The
    // engine has no such notion (it walks all 16), so this is the port's
    // handle for a prompt, and it is the first slot in TABLE order.
    std::int16_t armedZone() const;

    // What the last `registerAll` released because its zone went away.
    const std::vector<std::int16_t>& detached() const { return detached_; }

    // Cumulative, for a probe: event 8s raised, how many asked for a camera,
    // and the last camera a scan asked for (-1 if none this scan). Each event
    // 8 overwrites the same request block (`dword_4E6C40`), so within one scan
    // the last touch wins.
    int touches() const { return touches_; }
    int cameraRequests() const { return cameraRequests_; }
    int touchedCamera() const { return touchCamera_; }

private:
    std::vector<LiveZone> all_, live_;
    std::array<PromptSlot, 16> slots_{};
    int  armed_ = 0;
    bool heldObject_ = false;
    std::function<bool(std::span<const std::byte>, std::size_t)> usedObjectProbe_;
    int  touches_ = 0, cameraRequests_ = 0, touchCamera_ = -1;
    std::vector<std::int16_t> detached_;

    void addTable(const ResidentSlot& s, int slotIndex, ChunkKind kind,
                  const GameState& state);
    // `Game_HandleEvent` case 7's slot bookkeeping.
    void raiseArm(const LiveZone& z);
};

}  // namespace omk
