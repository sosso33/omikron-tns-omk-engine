// SPDX-License-Identifier: GPL-3.0-or-later
#include "script/zones.h"

#include <cmath>

namespace omk {

// ---------------------------------------------------------------- the arc
//
// `Actor_ScanZones` (0x00467770), the facing half, over the arc the LOADER
// has already converted to degrees.
bool zoneFacesDegrees(const Zone& z, double facingDegrees) {
    // `if (u16(zone, 50))` - and it is the CONVERTED width that is tested, so
    // a raw width small enough to truncate to 0 degrees accepts any facing
    // just as an authored 0 does.
    const int wide = static_cast<int>(static_cast<double>(z.arcWide) * kZoneArcToDegrees);
    if (wide == 0) return true;
    const int mid  = static_cast<int>(static_cast<double>(z.arcMid) * kZoneArcToDegrees);
    const int half = wide >> 1;                 // `u16(zone,50) >> 1`
    int lo = mid - half, hi = mid + half;

    // The actor's facing is `f32(actor, 420)`. The engine does not normalise
    // it here; this does, because the port's callers hand in a yaw that has
    // been through a matrix. Labelled: it is the one line of this function
    // that is not in the assembly, and it is a no-op for any facing already
    // in [0, 360).
    double f = std::fmod(facingDegrees, 360.0);
    if (f < 0.0) f += 360.0;

    if (lo >= 0) {
        if (hi > 360 && f < lo) f += 360.0;     // the arc wraps past 360
    } else if (!(f < lo + 360 && f <= hi)) {    // the arc starts below 0
        lo += 360;
        hi += 360;
    }
    return f >= lo && f <= hi;
}

// ------------------------------------------------------ Zones_RegisterAll

void ZoneRegistry::addTable(const ResidentSlot& s, int slotIndex,
                            ChunkKind kind, const GameState& state) {
    const auto chunk = kind == ChunkKind::Area ? s.area : s.scene;
    const int  id    = kind == ChunkKind::Area ? s.areaId : s.sceneId;
    if (id == -1 || chunk.empty()) return;      // `blocks[2] != -1 && blocks[0]`

    for (const auto& z : zonesOf(chunk, kind)) {
        LiveZone lz;
        lz.zone  = z;
        lz.slot  = slotIndex;
        lz.kind  = kind;
        lz.chunk = id;
        lz.code  = chunk;
        all_.push_back(lz);
        // `if (!Zone_StateBit(u16(rec + 12, 52))) continue;` - the id at file
        // +64, masked to 15 bits because bit 15 is the one-shot FLAG.
        if (state.bit(StateArray::ZoneState, z.stateBit()))
            live_.push_back(lz);
    }
}

void ZoneRegistry::registerAll(const std::vector<ResidentSlot>& slots,
                               const GameState& state) {
    all_.clear();
    live_.clear();
    detached_.clear();

    // `Zones_Clear(); for (blocks = dword_69BC40; blocks < &dword_69BC60;
    //  blocks += 4, ++areaSlot)` - two slots, AREA table then SCENE table.
    for (std::size_t i = 0; i < slots.size(); ++i) {
        addTable(slots[i], static_cast<int>(i), ChunkKind::Area, state);
        addTable(slots[i], static_cast<int>(i), ChunkKind::Scene, state);
    }

    // The tail's prompt-slot half. The engine detaches by CONTEXT pointer -
    // it frees the context whose zone no longer resolves and clears every
    // prompt slot pointing at it - which, since a context is created per zone
    // id, is the same set as "the slot whose zone no longer resolves".
    //
    //     u32(ps, 0) = 0;  ps[5] = -1;  ps[4] = 0;  --dword_4E6B24
    for (auto& s : slots_) {
        if (s.zone == -1) continue;
        if (resolve(s.zone)) continue;
        detached_.push_back(s.zone);
        s.zone  = -1;
        s.state = kZoneSlotEmpty;
        if (--armed_ < 0) armed_ = 0;
    }
}

const LiveZone* ZoneRegistry::resolve(std::int16_t id) const {
    // `Zone_FindScriptsById` (0x00406760): both slots, AREA table then SCENE
    // table, over the FULL record list - a zone whose save bit is clear is
    // still resolvable, which is why `zone.disable` does not prune a context.
    for (const auto& z : all_)
        if (z.zone.id == id) return &z;
    return nullptr;
}

std::vector<std::int16_t>
ZoneRegistry::prune(std::span<const std::int16_t> contextZones) const {
    std::vector<std::int16_t> dead;
    for (const auto id : contextZones) {
        if (id == -1) continue;                 // `if (zid == -1 ... ) continue`
        if (resolve(id)) continue;
        dead.push_back(id);
    }
    return dead;
}

std::int16_t ZoneRegistry::armedZone() const {
    for (const auto& s : slots_)
        if (s.zone != -1) return s.zone;
    return -1;
}

// --------------------------------------------- Script_Pump's slot loop

namespace {

ZoneEvent makeEvent(const LiveZone& z, ZoneEvent::Kind kind, int action,
                    std::size_t script) {
    ZoneEvent e;
    e.kind      = kind;
    e.zone      = z.zone.id;
    e.action    = action;
    e.script    = script;
    e.scripts[0] = z.zone.scripts[0];
    e.scripts[1] = z.zone.scripts[1];
    e.scripts[2] = z.zone.scripts[2];
    e.slot      = z.slot;
    e.chunkKind = z.kind;
    e.chunk     = z.chunk;
    e.code      = z.code;
    e.oneShot   = z.zone.oneShot();
    return e;
}

}  // namespace

void ZoneRegistry::pumpSlots(bool actionPressed, std::vector<ZoneEvent>& out) {
    // `Game_HandleEvent` case 6:
    //     if (g_DialogState == 3 || a2 != 4 || !dword_4E6B24) return 0;
    //     dword_4E6C90 = 1;
    // so the press never becomes a pump input unless a slot is taken. The
    // dialogue half of that gate is the caller's - the whole pump is skipped
    // while a conversation is up.
    const bool pressed = actionPressed && armed_ > 0;

    for (auto& s : slots_) {
        if (s.zone == -1) continue;             // `u16i(slot,5) == 0xFFFF`
        const LiveZone* z = resolve(s.zone);
        // The engine calls Zone_FindScriptsById here and dereferences the
        // result in case 1 without checking it - which is safe only because
        // Zones_RegisterAll's prune released exactly these slots first. Guard
        // rather than reproduce a null dereference.
        if (!z) {
            s.zone  = -1;
            s.state = kZoneSlotEmpty;
            if (--armed_ < 0) armed_ = 0;
            continue;
        }

        switch (s.state) {
        case kZoneSlotNew: {
            // `Script_NewContext(areaSlot, zoneScripts[0..2])`, then
            // `if (zoneScripts[0]) Script_QueueAction(ctx, 1)`. BOTH arms
            // write state 3, so a zone with no enter script still holds its
            // slot and can still be activated and left.
            const auto enter = static_cast<std::size_t>(z->zone.scripts[0]);
            out.push_back(makeEvent(*z, ZoneEvent::Kind::Arm,
                                    enter ? 1 : 0, enter));
            s.state = kZoneSlotArmed;
            break;
        }
        case kZoneSlotPressed: {
            if (!pressed) { s.state = kZoneSlotArmed; break; }
            // `if (u32(ctx, 4))` - the context's slot 1, copied from the
            // record: no activate script, nothing happens, and in particular
            // NO LATCH. A one-shot zone with no activate script is spent by
            // nothing.
            if (z->zone.scripts[1]) {
                // The inline dry run (`sub_406180`). Empty-handed both of
                // its outcomes reach loc_407F93 and queue, so `ran` is 1;
                // with a held object it is `Script_RunToOpcode75`'s answer -
                // 1 only if the script reaches opcode 75 and asks what is in
                // the hand. `setUsedObjectProbe` supplies that run; with no
                // probe installed the answer is 0, which is what this line
                // returned unconditionally before 2026-09-04.
                const bool ran = !heldObject_ ? true
                    : (usedObjectProbe_ &&
                       usedObjectProbe_(z->code,
                                        static_cast<std::size_t>(z->zone.scripts[1])));
                if (ran) {
                    out.push_back(makeEvent(
                        *z, ZoneEvent::Kind::Activate, 2,
                        static_cast<std::size_t>(z->zone.scripts[1])));
                    // `test byte ptr [esi+0Bh], 80h` - AFTER the queue, and it
                    // does not look at what the queue returned. The latch is
                    // not "one activate ever": the dedupe in the context's own
                    // FIFO is what refuses a second one while the first is
                    // unfinished, and this is what refuses it once the first
                    // has ENDED (T4's correction to issue 5).
                    if (z->zone.oneShot()) { s.state = kZoneSlotSpent; break; }
                }
            }
            s.state = kZoneSlotArmed;
            break;
        }
        case kZoneSlotArmed:
        case kZoneSlotSpent: {
            // Reached only when nothing re-armed the slot since the last pump,
            // i.e. the player left the quad or turned out of the arc.
            //
            // Both queues are UNCONDITIONAL in the engine - there is no
            // `if (zoneScripts[2])` guard on the leave the way there is on the
            // enter. `Script_ProcessActions` consumes an action whose script
            // slot is 0 and leaves the context idle, so the effect is the same
            // and the FIFO accounting is not.
            out.push_back(makeEvent(*z, ZoneEvent::Kind::Leave, 3,
                                    static_cast<std::size_t>(z->zone.scripts[2])));
            out.push_back(makeEvent(*z, ZoneEvent::Kind::Free, 4, 0));
            s.zone  = -1;
            s.state = kZoneSlotEmpty;
            if (--armed_ < 0) armed_ = 0;
            break;
        }
        case kZoneSlotLatched:
            // `loc_407FD4: mov word ptr [esi+8], 5` - the other half of the
            // one-shot ping-pong. Case 7 maps 5 back to 4 every frame the
            // player is still armed, so the slot never re-enters state 2 and
            // never leaves through anything but the body above.
            s.state = kZoneSlotSpent;
            break;
        default:
            break;
        }
    }
    // `dword_4E6C90 = 0;` - the press is consumed by the pump, whatever it did
    // with it. Nothing is held here because the flag is the caller's input.
}

// ------------------------------------------------------- Actor_ScanZones

void ZoneRegistry::raiseArm(const LiveZone& z) {
    // `Game_HandleEvent` case 7. The slot is found by ZONE ID, not by record.
    for (auto& s : slots_) {
        if (s.zone != z.zone.id) continue;
        if (s.state == kZoneSlotArmed) s.state = kZoneSlotPressed;
        if (s.state != kZoneSlotSpent) return;
        s.state = kZoneSlotLatched;
        return;
    }
    for (auto& s : slots_) {
        if (s.zone != -1) continue;
        s.zone     = z.zone.id;
        s.state    = kZoneSlotNew;
        s.areaSlot = z.slot;
        ++armed_;
        return;
    }
    // Seventeen zones armed at once. The engine's search leaves `v26 = 0` and
    // then writes `v26[5] = v24` through it - a null store, so this is a crash
    // in the shipped build, not a behaviour to reproduce. Dropped, labelled.
}

void ZoneRegistry::scanZones(const double pos[3], double facingDegrees,
                             std::vector<ZoneEvent>& out) {
    touchCamera_ = -1;
    for (const auto& z : live_) {
        // `Zone_ContainsPoint` (0x0048C880) is a crossing count over the four
        // corners and ignores y entirely - the third argument is passed and
        // never used. `Zone::contains` is the same test casting its ray along
        // the other axis; they agree away from an edge, which is where every
        // check here stands.
        if (!z.zone.contains(pos[0], pos[2])) continue;

        // Event 8, raised BEFORE the facing test - a zone can be touched, and
        // can take the camera, without ever arming.
        ++touches_;
        auto e = makeEvent(z, ZoneEvent::Kind::Touch, 0, 0);
        e.camera = z.zone.camera;
        out.push_back(e);
        if (z.zone.camera != -1) {
            // `Game_HandleEvent` case 8: not -1 -> Camera_FindWorld fills the
            // request block and dword_930800 points the camera at it. Each
            // touch overwrites the same block, so the last one wins.
            ++cameraRequests_;
            touchCamera_ = z.zone.camera;
        }

        if (!zoneFacesDegrees(z.zone, facingDegrees)) continue;
        raiseArm(z);                            // event 7
    }
}

std::vector<ZoneEvent> ZoneRegistry::scan(const double pos[3],
                                          double facingDegrees,
                                          bool actionPressed) {
    std::vector<ZoneEvent> out;
    pumpSlots(actionPressed, out);               // Script_Pump(1), :2107
    scanZones(pos, facingDegrees, out);          // Actors_TickAll,  :2178
    return out;
}

}  // namespace omk
