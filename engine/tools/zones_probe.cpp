// SPDX-License-Identifier: GPL-3.0-or-later
// The LIVE zone registry, driven scenario by scenario - what
// `ZoneRegistry` registers over BOTH resident slots' four tables, and what it
// decides as a player walks through a real zone.
//
//     zones_probe <gamedata/IAM> <START> <out.bin>
//
// `zone_probe` (T4, one `d`) drives the `World` HARNESS: one chunk, one kind,
// and it runs the scripts. This drives the thing the Session is missing - the
// four-table walk, the prune, the 16 prompt slots and the per-frame scan - and
// it runs NOTHING: every action comes back as a `ZoneEvent` for the Session to
// queue, which is where the activate dedupe lives.
//
// Six scenarios, each on its own `GameState` so one cannot spend another's
// save bits:
//
//   0  AREA 118 alone          the start area: 0 records, 0 registered
//   1  AREA 222 + SCENE 55     the Impasse: 12 + 2 records, 5 + 1 registered
//   2  the same, walked        zone 3791: touch, arm, a press with no
//                              activate script, a turn OUT of the arc (touch
//                              and no arm), then leave and free
//   3  AREA 146 | AREA 76      TWO SLOTS, and the one-shot latch: zone 35141
//                              activates ONCE over five held frames, zone
//                              1466 - in the other slot - activates five times
//   4  AREA 25                 zone 626 carries camera 540 at +66
//   5  the PRUNE               arm 3791, unload its area, and both its prompt
//                              slot and its context go
//
// out.bin: int32 nScenarios, then per scenario
//   int32 id, allRecords, registered, touches, cameraRequests,
//         lastTouchCamera, armedAtEnd
//   int32 nLiveIds,   int32 id...
//   int32 nEvents,    per event {int32 kind, zone, action, script}
//   int32 nDetached,  int32 id...
//   int32 nPruned,    int32 id...
// kind: 0 touch, 1 arm, 2 activate, 3 leave, 4 free.
#include "formats/iam.h"
#include "platform/datafs.h"
#include "script/gamestate.h"
#include "script/zones.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

void put32(std::vector<std::uint8_t>& o, std::int32_t v) {
    const auto u = static_cast<std::uint32_t>(v);
    for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
}

const char* kindName(omk::ZoneEvent::Kind k) {
    switch (k) {
    case omk::ZoneEvent::Kind::Touch:    return "touch";
    case omk::ZoneEvent::Kind::Arm:      return "arm";
    case omk::ZoneEvent::Kind::Activate: return "activate";
    case omk::ZoneEvent::Kind::Leave:    return "leave";
    case omk::ZoneEvent::Kind::Free:     return "free";
    }
    return "?";
}

int kindCode(omk::ZoneEvent::Kind k) { return static_cast<int>(k); }

// One archive, held for the whole run - the registry's spans point into it.
struct Archives {
    std::vector<std::byte> area, scene;
    omk::IamArchive        areaAr, sceneAr;
};

std::span<const std::byte> chunkOf(const omk::IamArchive& a, int id) {
    if (id < 0) return {};
    return a.chunk(static_cast<std::size_t>(id));
}

// The facing the arc's centre names, in the DEGREES `Actor_ScanZones` works in
// - the loader has already multiplied the stored 4096-per-turn angle by
// 360/4096, so a probe that handed in the raw number would be 4x out.
double arcCentreDegrees(const omk::Zone& z) {
    return static_cast<double>(static_cast<int>(
        static_cast<double>(z.arcMid) * omk::kZoneArcToDegrees));
}

struct Scenario {
    int id = 0;
    std::vector<omk::ZoneEvent> events;
    std::vector<std::int16_t>   liveIds, detached, pruned;
    int allRecords = 0, registered = 0;
    int touches = 0, cameraRequests = 0, lastCamera = -1, armedAtEnd = 0;
};

void snapshot(Scenario& s, const omk::ZoneRegistry& r) {
    s.allRecords = static_cast<int>(r.all().size());
    s.registered = static_cast<int>(r.registered().size());
    s.liveIds.clear();
    for (const auto& z : r.registered()) s.liveIds.push_back(z.zone.id);
    s.touches        = r.touches();
    s.cameraRequests = r.cameraRequests();
    s.armedAtEnd     = r.armedCount();
}

// One frame, appending what it decided.
void frame(omk::ZoneRegistry& r, Scenario& s, const double p[3], double facing,
           bool pressed) {
    for (auto& e : r.scan(p, facing, pressed)) s.events.push_back(e);
    if (r.touchedCamera() != -1) s.lastCamera = r.touchedCamera();
}

// Find one zone by id across a set of resident slots that have already been
// registered - `all()` holds every record, registered or not.
const omk::LiveZone* find(const omk::ZoneRegistry& r, std::int16_t id) {
    return r.resolve(id);
}

void walk(omk::ZoneRegistry& r, Scenario& s, std::int16_t id, int holdFrames) {
    const auto* z = find(r, id);
    if (!z) { std::printf("  zone %d NOT RESIDENT\n", id); return; }
    double c[3];
    z->zone.centre(c);
    const double away[3] = {c[0] + 1e6, c[1], c[2] + 1e6};
    const double f = arcCentreDegrees(z->zone);
    frame(r, s, c, f, false);                       // walk in, arm the slot
    frame(r, s, c, f, false);                       // the pump makes the context
    for (int k = 0; k < holdFrames; ++k)            // lean on the button
        frame(r, s, c, f, true);
    frame(r, s, away, f, false);                    // leave
    frame(r, s, away, f, false);                    // and free
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: zones_probe <gamedata/IAM> <START> <out.bin>\n");
        return 2;
    }
    const std::string iam = argv[1];
    const std::string startPath = argv[2];

    Archives ar;
    ar.area  = omk::DataFs::readPath(iam + "/AREA");
    ar.scene = omk::DataFs::readPath(iam + "/SCENE");
    ar.areaAr  = omk::IamArchive::open(ar.area);
    ar.sceneAr = omk::IamArchive::open(ar.scene);

    auto slot = [&](int areaId, int sceneId) {
        omk::ResidentSlot s;
        s.areaId  = areaId;
        s.sceneId = sceneId;
        s.area    = chunkOf(ar.areaAr, areaId);
        s.scene   = chunkOf(ar.sceneAr, sceneId);
        return s;
    };

    std::vector<Scenario> out;

    // ---- 0: the start area alone. AREA 118 declares ZERO zone records, which
    // is why the intro can only come from a startup script (CLAUDE.md 6).
    {
        auto st = omk::GameState::fromFile(startPath);
        omk::ZoneRegistry r;
        r.registerAll({slot(118, -1)}, st);
        Scenario s; s.id = 0; snapshot(s, r); out.push_back(s);
    }

    // ---- 1: the Impasse, AREA 222 with SCENE 55 over it. Both tables of the
    // one slot, which is the half `World` could already do, plus the SCENE's
    // over the AREA's, which it could not.
    {
        auto st = omk::GameState::fromFile(startPath);
        omk::ZoneRegistry r;
        r.registerAll({slot(222, 55)}, st);
        Scenario s; s.id = 1; snapshot(s, r); out.push_back(s);
    }

    // ---- 2: walked. Zone 3791 has an ENTER script and no activate, so the
    // press does nothing - and that is the point: the engine's case 2 tests
    // `u32(ctx, 4)` before anything else.
    {
        auto st = omk::GameState::fromFile(startPath);
        omk::ZoneRegistry r;
        r.registerAll({slot(222, 55)}, st);
        Scenario s; s.id = 2;
        const auto* z = find(r, 3791);
        if (z) {
            double c[3];
            z->zone.centre(c);
            const double away[3] = {c[0] + 1e6, c[1], c[2] + 1e6};
            const double in  = arcCentreDegrees(z->zone);
            const double out_ = in + 180.0;         // contained, facing OUT
            frame(r, s, c, in, false);
            frame(r, s, c, in, false);
            frame(r, s, c, in, true);
            frame(r, s, c, out_, false);
            frame(r, s, c, out_, false);
            frame(r, s, away, in, false);
            frame(r, s, away, in, false);
        }
        snapshot(s, r); out.push_back(s);
    }

    // ---- 3: TWO SLOTS. AREA 146 in slot 0 holds one-shot zone 35141
    // (-30395 as an int16); AREA 76 in slot 1 holds ordinary zone 1466. Both
    // must register, and the two must behave differently under the same five
    // held frames.
    {
        auto st = omk::GameState::fromFile(startPath);
        omk::ZoneRegistry r;
        r.registerAll({slot(146, -1), slot(76, -1)}, st);
        Scenario s; s.id = 3;
        walk(r, s, static_cast<std::int16_t>(-30395), 5);
        walk(r, s, 1466, 5);
        snapshot(s, r); out.push_back(s);
    }

    // ---- 4: the touch camera. Zone 626 of AREA 25 carries 540 at +66, and
    // the touch is raised BEFORE the facing test - so standing in it with the
    // back turned still asks for the camera.
    {
        auto st = omk::GameState::fromFile(startPath);
        omk::ZoneRegistry r;
        r.registerAll({slot(25, -1)}, st);
        Scenario s; s.id = 4;
        const auto* z = find(r, 626);
        if (z) {
            double c[3];
            z->zone.centre(c);
            const double back = arcCentreDegrees(z->zone) + 180.0;
            frame(r, s, c, back, false);
            frame(r, s, c, back, false);
        }
        snapshot(s, r); out.push_back(s);
    }

    // ---- 5: the PRUNE. Arm 3791, then unload its area: the prompt slot goes
    // in `registerAll`, and the context id comes back from `prune`.
    {
        auto st = omk::GameState::fromFile(startPath);
        omk::ZoneRegistry r;
        r.registerAll({slot(222, 55)}, st);
        Scenario s; s.id = 5;
        const auto* z = find(r, 3791);
        if (z) {
            double c[3];
            z->zone.centre(c);
            const double f = arcCentreDegrees(z->zone);
            frame(r, s, c, f, false);               // arm
            frame(r, s, c, f, false);               // the context exists now
        }
        const int armedBefore = r.armedCount();
        r.registerAll({slot(118, -1)}, st);         // the Impasse is gone
        const std::int16_t ctxZones[] = {3791, 3803};
        for (auto id : r.prune(ctxZones)) s.pruned.push_back(id);
        for (auto id : r.detached()) s.detached.push_back(id);
        snapshot(s, r);
        std::printf("scenario 5: armed %d -> %d\n", armedBefore, r.armedCount());
        out.push_back(s);
    }

    // ---------------------------------------------------------------- report
    std::vector<std::uint8_t> b;
    put32(b, static_cast<std::int32_t>(out.size()));
    for (const auto& s : out) {
        std::printf("scenario %d: records=%d registered=%d touches=%d "
                    "cameraRequests=%d lastCamera=%d armed=%d\n",
                    s.id, s.allRecords, s.registered, s.touches,
                    s.cameraRequests, s.lastCamera, s.armedAtEnd);
        std::printf("  live:");
        for (auto id : s.liveIds) std::printf(" %d", id);
        std::printf("\n  events:");
        for (const auto& e : s.events)
            std::printf(" [%s z%d a%d @%zu]", kindName(e.kind), e.zone,
                        e.action, e.script);
        std::printf("\n");
        if (!s.detached.empty()) {
            std::printf("  detached:");
            for (auto id : s.detached) std::printf(" %d", id);
            std::printf("\n");
        }
        if (!s.pruned.empty()) {
            std::printf("  pruned:");
            for (auto id : s.pruned) std::printf(" %d", id);
            std::printf("\n");
        }

        put32(b, s.id);
        put32(b, s.allRecords);
        put32(b, s.registered);
        put32(b, s.touches);
        put32(b, s.cameraRequests);
        put32(b, s.lastCamera);
        put32(b, s.armedAtEnd);
        put32(b, static_cast<std::int32_t>(s.liveIds.size()));
        for (auto id : s.liveIds) put32(b, id);
        put32(b, static_cast<std::int32_t>(s.events.size()));
        for (const auto& e : s.events) {
            put32(b, kindCode(e.kind));
            put32(b, e.zone);
            put32(b, e.action);
            put32(b, static_cast<std::int32_t>(e.script));
        }
        put32(b, static_cast<std::int32_t>(s.detached.size()));
        for (auto id : s.detached) put32(b, id);
        put32(b, static_cast<std::int32_t>(s.pruned.size()));
        for (auto id : s.pruned) put32(b, id);
    }

    if (!omk::safeOutputPath(argv[3])) return 2;
    std::ofstream f(argv[3], std::ios::binary);
    f.write(reinterpret_cast<const char*>(b.data()),
            static_cast<std::streamsize>(b.size()));
    return 0;
}
