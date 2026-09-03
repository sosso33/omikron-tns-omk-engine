// SPDX-License-Identifier: GPL-3.0-or-later
// The stage-3 test, ported: stand in a zone and its activate script must fire.
//
//     walk_zone <gamedata/IAM> <vm_opcodes.json> <START> <zoneId> <out.bin>
//
// Zone 3732 is record 0 of SCENE 53 and its activate slot is what launches
// dialog 387, so the outcome is checkable BY NAME rather than by "something
// executed" - which is the whole reason the plan picked this zone.
//
// Layout: int32 found, int32 arch (0 AREA, 1 SCENE), int32 chunk,
//   int32 registeredBefore, int32 bitBefore, int32 bitAfter,
//   int32 reRegisters, int32 nRan, per ran {int32 zone, action, offset, status},
//   int32 nCalls, per call {uint8 op, uint8 nFields, int16 fields...}
#include "formats/iam.h"
#include "script/gamestate.h"
#include "script/world.h"

#include "platform/datafs.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {


void put32(std::vector<std::uint8_t>& o, std::int32_t v) {
    const auto u = static_cast<std::uint32_t>(v);
    for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr,
            "usage: walk_zone <gamedata/IAM> <vm_opcodes.json> <START> <zoneId> <out.bin>\n");
        return 2;
    }
    const std::string iam = argv[1];
    const auto table = omk::OpcodeTable::loadJson(argv[2]);
    auto state = omk::GameState::fromFile(argv[3]);
    const auto want = static_cast<std::int16_t>(std::atoi(argv[4]));

    // find the chunk holding that zone - SCENE first, as the reference does
    std::vector<std::byte> held;
    omk::ChunkKind kind = omk::ChunkKind::Scene;
    int chunkIdx = -1, archIdx = -1;
    omk::Zone target{};
    for (int a = 0; a < 2 && chunkIdx < 0; ++a) {
        const char* name = a == 0 ? "SCENE" : "AREA";
        const auto k = a == 0 ? omk::ChunkKind::Scene : omk::ChunkKind::Area;
        const auto d = omk::DataFs::readPath(iam + "/" + name);
        const auto ar = omk::IamArchive::open(d);
        for (std::size_t c = 0; c < ar.size() && chunkIdx < 0; ++c) {
            const auto b = ar.chunk(c);
            if (b.empty()) continue;
            for (const auto& z : omk::zonesOf(b, k))
                if (z.id == want) {
                    held.assign(b.begin(), b.end());
                    kind = k; chunkIdx = static_cast<int>(c); archIdx = a;
                    target = z;
                    break;
                }
        }
    }

    std::vector<std::uint8_t> o;
    if (chunkIdx < 0) { put32(o, 0); }
    else {
        omk::World w(held, kind, state, table);
        const auto before = state.bit(omk::StateArray::ZoneState, want & 0x7FFF);
        bool reg = false;
        for (const auto& z : w.registered()) if (z.id == want) reg = true;

        double c[3];
        target.centre(c);
        w.step(c, target.arcMid, false);          // walk in, facing the arc
        w.step(c, target.arcMid, true);           // press action

        const auto after = state.bit(omk::StateArray::ZoneState, want & 0x7FFF);
        bool again = false;
        for (const auto& z : w.registered()) if (z.id == want) again = true;

        put32(o, 1);
        put32(o, archIdx); put32(o, chunkIdx);
        put32(o, reg ? 1 : 0);
        put32(o, before); put32(o, after);
        put32(o, again ? 1 : 0);
        put32(o, static_cast<std::int32_t>(w.ran().size()));
        for (const auto& r : w.ran()) {
            put32(o, r.zone); put32(o, r.action);
            put32(o, static_cast<std::int32_t>(r.offset));
            put32(o, static_cast<std::int32_t>(r.status));
        }
        put32(o, static_cast<std::int32_t>(w.calls().size()));
        for (const auto& call : w.calls()) {
            o.push_back(call.op);
            o.push_back(static_cast<std::uint8_t>(call.fields.size()));
            for (auto f : call.fields) {
                const auto u = static_cast<std::uint16_t>(f);
                o.push_back(static_cast<std::uint8_t>(u));
                o.push_back(static_cast<std::uint8_t>(u >> 8));
            }
        }
        std::printf("zone %d in %s %d: registered=%d, %zu scripts ran, "
                    "save bit %d -> %d, re-registers=%d\n",
                    want, archIdx ? "AREA" : "SCENE", chunkIdx, reg ? 1 : 0,
                    w.ran().size(), before, after, again ? 1 : 0);
    }
    if (!omk::safeOutputPath(argv[5])) return 2;
    std::ofstream f(argv[5], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));
    return 0;
}
