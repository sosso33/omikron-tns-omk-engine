// SPDX-License-Identifier: GPL-3.0-or-later
// The zone harness, driven frame by frame - what `World` DECIDES when the
// player stands in a trigger and leans on the action button.
//
//     zone_probe <gamedata/IAM> <vm_opcodes.json> <START> <settle> <camwait 0|1>
//                <out.bin> <zoneId>...
//
// Each zone gets the same script, on its own GameState so one does not spend
// another's save bits:
//
//     frame 0            stand at the quad's centre facing the arc, no action
//     frames 1..5        the SAME position, action held for five frames
//     frames 6..6+settle still there, action released
//     last two frames    a long way away - the leave script, then the free
//
// Five held frames is the point. `Script_QueueAction(ctx, 2)` (0x004063D0)
// refuses a second activate while one is queued or is the context's current
// action, and `Script_Execute`'s tail clears that field only when the activate
// script reaches `end` - so a script that PARKS (a camera move, a screen, a
// conversation) holds the refusal open for as long as it is parked. A harness
// that queued one activate per frame instead ran the same script up to four
// times over those five frames.
//
// And the settle frames are where a park is either released or is not:
// `camera.set.wait` is a countdown in frames, `ui.open` waits on a person and
// never comes back, `dialog.start` stops the whole pump.
//
// out.bin: int32 nZones, then per zone
//   int32 id, found, arch (0 SCENE, 1 AREA), chunk,
//   int32 registered, oneShot, camera,
//   int32 touches, cameraRequests, lastTouchCamera,
//   int32 activatesQueued, activatesRefused, dialogOpen,
//   int32 nRan,    per ran    {int32 zone, action, offset, status}
//   int32 nParked, per parked {int32 zone, action, status, pc, framesLeft}
#include "formats/iam.h"
#include "platform/datafs.h"
#include "script/gamestate.h"
#include "script/world.h"

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

const char* statusName(omk::RunStatus s) {
    switch (s) {
    case omk::RunStatus::End:            return "end";
    case omk::RunStatus::Dialog:         return "dialog";
    case omk::RunStatus::UiOpen:         return "ui.open";
    case omk::RunStatus::CameraWait:     return "camera.wait";
    case omk::RunStatus::ObjectWait:     return "object.wait";
    case omk::RunStatus::Suspended:      return "suspended";
    case omk::RunStatus::Runaway:        return "runaway";
    case omk::RunStatus::PcOutOfRange:   return "pc-out-of-range";
    case omk::RunStatus::UnknownOpcode:  return "unknown-opcode";
    case omk::RunStatus::StackUnderflow: return "stack-underflow";
    }
    return "?";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 8) {
        std::fprintf(stderr, "usage: zone_probe <gamedata/IAM> <vm_opcodes.json> "
                             "<START> <settle> <camwait 0|1> <out.bin> "
                             "<zoneId>...\n");
        return 2;
    }
    const std::string iam = argv[1];
    const auto table  = omk::OpcodeTable::loadJson(argv[2]);
    const std::string startPath = argv[3];
    const int  settle  = std::atoi(argv[4]);
    const bool camWait = std::atoi(argv[5]) != 0;

    // the two archives, read once
    struct Arch { const char* name; omk::ChunkKind kind; std::vector<std::byte> data; };
    Arch archs[2] = {
        {"SCENE", omk::ChunkKind::Scene, {}},
        {"AREA",  omk::ChunkKind::Area,  {}},
    };
    for (auto& a : archs) a.data = omk::DataFs::readPath(iam + "/" + a.name);

    std::vector<std::uint8_t> o;
    put32(o, argc - 7);

    for (int ai = 7; ai < argc; ++ai) {
        const auto want = static_cast<std::int16_t>(std::atoi(argv[ai]));

        std::vector<std::byte> held;
        omk::ChunkKind kind = omk::ChunkKind::Scene;
        int chunkIdx = -1, archIdx = -1;
        omk::Zone target{};
        for (int a = 0; a < 2 && chunkIdx < 0; ++a) {
            const auto ar = omk::IamArchive::open(archs[a].data);
            for (std::size_t c = 0; c < ar.size() && chunkIdx < 0; ++c) {
                const auto b = ar.chunk(c);
                if (b.empty()) continue;
                for (const auto& z : omk::zonesOf(b, archs[a].kind))
                    if (z.id == want) {
                        held.assign(b.begin(), b.end());
                        kind = archs[a].kind;
                        chunkIdx = static_cast<int>(c);
                        archIdx = a;
                        target = z;
                        break;
                    }
            }
        }
        if (chunkIdx < 0) {
            put32(o, want); put32(o, 0);
            for (int k = 0; k < 11; ++k) put32(o, 0);
            put32(o, 0); put32(o, 0);
            std::printf("zone %d: NOT FOUND\n", want);
            continue;
        }

        auto state = omk::GameState::fromFile(startPath);
        omk::World w(held, kind, state, table);
        w.setCameraWait(camWait);

        bool reg = false;
        for (const auto& z : w.registered()) if (z.id == want) reg = true;

        double c[3];
        target.centre(c);
        const double away[3] = {c[0] + 1e6, c[1], c[2] + 1e6};
        const int facing = static_cast<int>(target.arcMid);

        int lastTouchCam = -1;
        w.step(c, facing, false);                                // walk in
        if (w.touchedCamera() != -1) lastTouchCam = w.touchedCamera();
        for (int f = 0; f < 5; ++f) {                            // hold action
            w.step(c, facing, true);
            if (w.touchedCamera() != -1) lastTouchCam = w.touchedCamera();
        }
        for (int f = 0; f < settle; ++f) {                       // let it settle
            w.step(c, facing, false);
            if (w.touchedCamera() != -1) lastTouchCam = w.touchedCamera();
        }
        w.step(away, facing, false);                             // leave
        w.step(away, facing, false);                             // and free

        const auto parked = w.parked();
        put32(o, want); put32(o, 1); put32(o, archIdx); put32(o, chunkIdx);
        put32(o, reg ? 1 : 0);
        put32(o, target.oneShot() ? 1 : 0);
        put32(o, target.camera);
        put32(o, w.touches());
        put32(o, w.cameraRequests());
        put32(o, lastTouchCam);
        put32(o, w.activatesQueued());
        put32(o, w.activatesRefused());
        put32(o, w.dialogOpen() ? 1 : 0);
        put32(o, static_cast<std::int32_t>(w.ran().size()));
        for (const auto& r : w.ran()) {
            put32(o, r.zone); put32(o, r.action);
            put32(o, static_cast<std::int32_t>(r.offset));
            put32(o, static_cast<std::int32_t>(r.status));
        }
        put32(o, static_cast<std::int32_t>(parked.size()));
        for (const auto& p : parked) {
            put32(o, p.zone); put32(o, p.action);
            put32(o, static_cast<std::int32_t>(p.status));
            put32(o, static_cast<std::int32_t>(p.pc));
            put32(o, p.framesLeft);
        }

        std::printf("zone %d in %s %d: registered=%d one-shot=%d camera=%d\n",
                    want, archIdx ? "AREA" : "SCENE", chunkIdx,
                    reg ? 1 : 0, target.oneShot() ? 1 : 0, target.camera);
        std::printf("  touches=%d cameraRequests=%d lastTouchCamera=%d\n",
                    w.touches(), w.cameraRequests(), lastTouchCam);
        std::printf("  activates queued=%d refused=%d  dialogOpen=%d\n",
                    w.activatesQueued(), w.activatesRefused(),
                    w.dialogOpen() ? 1 : 0);
        std::printf("  %zu run(s):", w.ran().size());
        for (const auto& r : w.ran())
            std::printf(" [z%d a%d @%zu %s]", r.zone, r.action, r.offset,
                        statusName(r.status));
        std::printf("\n");
        for (const auto& p : parked)
            std::printf("  parked: zone %d action %d at %zu on %s (%d frames left)\n",
                        p.zone, p.action, p.pc, statusName(p.status), p.framesLeft);
    }

    if (!omk::safeOutputPath(argv[6])) return 2;
    std::ofstream f(argv[6], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));
    return 0;
}
