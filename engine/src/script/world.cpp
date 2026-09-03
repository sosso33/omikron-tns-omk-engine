// SPDX-License-Identifier: GPL-3.0-or-later
#include "script/world.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <cstdlib>

namespace omk {
namespace {

std::int32_t i32(std::span<const std::byte> d, std::size_t o) {
    if (o + 4 > d.size()) return 0;
    return static_cast<std::int32_t>(
        static_cast<std::uint32_t>(d[o    ])       |
        static_cast<std::uint32_t>(d[o + 1]) <<  8 |
        static_cast<std::uint32_t>(d[o + 2]) << 16 |
        static_cast<std::uint32_t>(d[o + 3]) << 24);
}

std::uint16_t u16(std::span<const std::byte> d, std::size_t o) {
    if (o + 2 > d.size()) return 0;
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(d[o]) | static_cast<std::uint16_t>(d[o + 1]) << 8);
}

double conv(std::int32_t v) { return static_cast<double>(v) * kZoneUnit - 1.0; }

}  // namespace

bool Zone::contains(double x, double z) const {
    bool inside = false;
    for (int i = 0; i < 4; ++i) {
        const auto& a = quad[i];
        const auto& b = quad[(i + 1) % 4];
        if ((a[2] > z) != (b[2] > z)) {
            const double t = (b[2] != a[2]) ? (z - a[2]) / (b[2] - a[2]) : 0.0;
            if (x < a[0] + t * (b[0] - a[0])) inside = !inside;
        }
    }
    return inside;
}

bool Zone::faces(int facing) const {
    if (arcWide == 0) return true;                 // width 0 means any facing
    const int d = (facing - static_cast<int>(arcMid) + 2048) % 4096 - 2048;
    return std::abs(d) <= arcWide / 2;
}

void Zone::centre(double out[3]) const {
    for (int k = 0; k < 3; ++k) {
        double s = 0;
        for (int i = 0; i < 4; ++i) s += quad[i][k];
        out[k] = s / 4.0;
    }
}

std::vector<Zone> zonesOf(std::span<const std::byte> b, ChunkKind kind) {
    std::vector<Zone> out;
    const std::size_t ptrAt = kind == ChunkKind::Area ? 48u : 16u;
    const std::size_t cntAt = kind == ChunkKind::Area ? 76u : 44u;
    if (b.size() < cntAt + 2) return out;
    const auto p = static_cast<std::size_t>(static_cast<std::uint32_t>(i32(b, ptrAt)));
    const auto n = static_cast<std::int16_t>(u16(b, cntAt));
    if (n <= 0 || p + kZoneStride * static_cast<std::size_t>(n) > b.size()) return out;

    for (int i = 0; i < n; ++i) {
        const auto o = p + kZoneStride * static_cast<std::size_t>(i);
        Zone z;
        for (int k = 0; k < 3; ++k) z.scripts[k] = i32(b, o + 4u * static_cast<std::size_t>(k));
        // the four corners at +12, 12 bytes each; y carries a further -9
        for (int c = 0; c < 4; ++c) {
            const auto co = o + 12u + 12u * static_cast<std::size_t>(c);
            z.quad[c][0] = conv(i32(b, co));
            z.quad[c][1] = conv(i32(b, co + 4)) - 9.0;
            z.quad[c][2] = conv(i32(b, co + 8));
        }
        z.arcMid  = u16(b, o + 60);
        z.arcWide = u16(b, o + 62);
        z.id      = static_cast<std::int16_t>(u16(b, o + 64));
        z.camera  = static_cast<std::int16_t>(u16(b, o + 66));
        out.push_back(z);
    }
    return out;
}

std::size_t startupScript(std::span<const std::byte> b) {
    if (b.size() < 8) return 0;
    const auto at = static_cast<std::uint32_t>(i32(b, 4));
    return (at != 0 && at < b.size()) ? at : 0;
}

World::World(std::span<const std::byte> chunk, ChunkKind kind,
             GameState& state, const OpcodeTable& table)
    : code_(chunk), state_(state), table_(table),
      all_(zonesOf(chunk, kind)), live_(registered()) {}

std::vector<Zone> World::registered() const {
    std::vector<Zone> out;
    for (const auto& z : all_)
        if (state_.bit(StateArray::ZoneState, z.stateBit())) out.push_back(z);
    return out;
}

// Script_QueueAction (0x004063D0), transcribed:
//
//     v2 = count(+28);
//     if (v2 >= 4)  printf("%d\n", v2);          // FULL: nothing is stored,
//     else {                                     // and it still returns 1
//         for (i = 0; i < 4; ++i)
//             if (a2 == fifo[i] && a2 == 2) return 0;
//         if (a2 == current(+32) && a2 == 2) return 0;
//         fifo[v2] = a2; ++count;
//     }
//     ...
//     return 1;
//
// The two refusals are the same rule twice: a second ACTIVATE while one is
// still queued or is the one being run. Nothing dedupes 1, 3 or 4.
int World::queueAction(Context& c, int act) {
    if (c.queue.size() >= 4) return 1;      // the printf arm - full, and says 1
    if (act == ActActivate) {
        for (int q : c.queue) if (q == ActActivate) return 0;
        if (c.act == ActActivate) return 0;
    }
    c.queue.push_back(act);
    return 1;
}

void World::step(const double pos[3], int facing, bool action) {
    live_ = registered();

    // Actor_ScanZones (0x00467770): containment FIRST, and every contained
    // zone raises event 8 whether or not the facing test then passes.
    touchCamera_ = -1;
    std::vector<const Zone*> armed;
    for (const auto& z : live_) {
        if (!z.contains(pos[0], pos[2])) continue;
        ++touches_;                                     // event 8
        if (z.camera != -1) {                           // Game_HandleEvent case 8
            ++cameraRequests_;
            touchCamera_ = z.camera;
        }
        if (z.faces(facing)) armed.push_back(&z);       // event 7
    }

    // Script_Pump's own first test: `if (g_DialogState != 1) return 1;`. The
    // scan above is in the actor tick and is not part of the pump, so touches
    // keep coming; nothing else in the frame happens.
    //
    // The engine does still FILL a prompt slot during a conversation (case 7
    // is in Game_HandleEvent, not in the pump) and acts on it when the
    // conversation closes. Deferring the fill here reaches the same state
    // unless the player leaves the zone mid-conversation, which is a
    // difference this does not model.
    if (dialogState_ == 3) return;

    // event 7: an armed zone takes one of the prompt slots and queues ENTER
    for (const auto* z : armed) {
        if (slots_.count(z->id)) continue;
        Context c;
        c.zone = *z;
        for (int k = 0; k < 3; ++k) c.slots[k] = z->scripts[k];
        c.oneShot = z->oneShot();
        if (c.slots[0]) queueAction(c, ActEnter);
        slots_.emplace(z->id, std::move(c));
    }

    std::vector<std::int16_t> armedIds;
    for (const auto* z : armed) armedIds.push_back(z->id);

    for (auto& [zid, c] : slots_) {
        const bool still = std::find(armedIds.begin(), armedIds.end(), zid) != armedIds.end();
        if (!still) {
            // slot states 3 and 5 share a body: queue the leave script, queue
            // the free, release the slot. A latched one-shot lands here too -
            // its 4/5 ping-pong only lasts while event 7 keeps firing.
            if (c.slots[2]) queueAction(c, ActLeave);
            queueAction(c, ActFree);
        } else if (action && !c.spent && c.slots[1]) {
            // slot state 2. The engine tests `[ctx+4]` - the activate script -
            // before doing anything at all, then queues, then latches.
            if (queueAction(c, ActActivate)) ++activatesQueued_;   // dword_4E6B20
            else                             ++activatesRefused_;
            // `test byte ptr [esi+0Bh], 80h` is AFTER the queue and does not
            // look at what it returned.
            if (c.oneShot) c.spent = true;
        }
    }
    pump();
}

void World::process(Context& c) {
    // Script_ProcessActions arms the NEXT action; it does not execute. Keeping
    // the two apart is what makes a park possible - a context whose status is
    // already 1 keeps its own pc and stack until it finishes.
    if (dialogState_ == 3) return;       // its own `if (g_DialogState == 3) return`
    if (c.status || c.queue.empty()) return;
    c.act = c.queue.front();             // +32 = act
    c.queue.pop_front();
    if (c.act == ActFree) { c.freed = true; return; }
    const int slot = c.act - 1;          // action n -> script slot n-1
    const auto off = (slot >= 0 && slot < 3) ? c.slots[slot] : 0;
    // `case n: ctx->pc = slots[n-1]; if (slots[n-1]) status = 1;` - an action
    // whose slot is 0 is consumed and leaves the context idle.
    if (off > 0) {
        c.entry = static_cast<std::size_t>(off);
        c.pc = c.entry;
        c.started = false;
        c.vm.reset();
        c.park = RunStatus::End;
        c.status = 1;
    }
}

void World::pump() {
    // Script_Pump: for each context, ProcessActions then Execute.
    for (auto& [zid, c] : slots_) {
        process(c);
        // Script_Execute's tail, reached whenever it is called on a context
        // that is not running: `if (+32 == 2 && !status) +32 = 0`. Clearing it
        // is what lets the NEXT press queue another activate, and not clearing
        // it is what a one-shot zone's latch stands in for.
        if (!c.status) {
            if (c.act == ActActivate) c.act = 0;
            continue;
        }
        if (c.waitingForUi) continue;                  // waiting on a person
        if (c.waitingForCamera > 0) { --c.waitingForCamera; continue; }

        if (!c.vm) {
            c.vm = std::make_unique<Interpreter>(state_, table_);
            c.vm->setRecordCalls(true);
            c.vm->setCameraWaitSuspends(camWait_);
            c.vm->setObjectWaitSuspends(objWait_);
        }
        if (c.pc == 0 || c.pc >= code_.size()) { c.status = 0; continue; }
        const auto r = c.started ? c.vm->resume(code_, c.pc)
                                 : c.vm->run(code_, c.pc);
        c.started = true;
        c.pc = r.pc;
        c.park = r.status;
        ran_.push_back(RanScript{zid, c.act, c.entry, r.status});
        for (const auto& call : r.calls) calls_.push_back(call);

        switch (r.status) {
        case RunStatus::UiOpen:
            c.waitingForUi = true;
            break;
        case RunStatus::CameraWait:
            // status 7 for the length of the move the same instruction issued.
            c.waitingForCamera = r.camTravel;
            break;
        case RunStatus::Dialog:
            // The context stays RUNNING with its pc past the operand - the
            // handler writes no status - and the whole pump stops instead.
            dialogState_ = 3;
            break;
        case RunStatus::ObjectWait:
            // status 4, released by the object's program ending. `World` has
            // no resident scene to run one, so it resumes next pump rather
            // than inventing a deadlock - the same call area.cpp makes.
            break;
        case RunStatus::Suspended:
            break;                                     // the scheduler resumes it
        default:
            // `end`, and the error statuses. ONLY these finish the action.
            c.status = 0;
            c.started = false;
            c.vm.reset();
            if (c.act == ActActivate) c.act = 0;       // Script_Execute's tail
            break;
        }
    }
    for (auto it = slots_.begin(); it != slots_.end();)
        it = it->second.freed ? slots_.erase(it) : std::next(it);
}

std::vector<World::Parked> World::parked() const {
    std::vector<Parked> out;
    for (const auto& [zid, c] : slots_) {
        if (!c.status || !c.started) continue;
        if (c.park == RunStatus::End) continue;
        out.push_back(Parked{zid, c.act, c.park, c.pc, c.waitingForCamera});
    }
    return out;
}

}  // namespace omk
