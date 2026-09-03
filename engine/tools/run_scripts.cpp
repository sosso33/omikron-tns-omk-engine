// SPDX-License-Identifier: GPL-3.0-or-later
// Execute every world script and report what the run decided, for the
// differential against tools/sim/vm.py.
//
//     run_scripts <gamedata/IAM> <tables/vm_opcodes.json> <IAM/START> <out.bin>
//
// The corpus is the simulator's `world_scripts()`: for each AREA and SCENE
// chunk, the chunk's own STARTUP script at +4 (record -1, field 4), then the
// trigger records' three fields and the second table.
//
// Layout: int32 nSlots, then per slot
//   int32 chunk, int32 record, int32 field, int32 offset,
//   int32 status, int32 steps, int32 nCalls, then per call uint8 op.
#include "formats/iam.h"
#include "script/gamestate.h"
#include "script/interp.h"

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

std::uint32_t u32at(std::span<const std::byte> d, std::size_t o) {
    if (o + 4 > d.size()) return 0;
    return static_cast<std::uint32_t>(d[o])       |
           static_cast<std::uint32_t>(d[o + 1]) <<  8 |
           static_cast<std::uint32_t>(d[o + 2]) << 16 |
           static_cast<std::uint32_t>(d[o + 3]) << 24;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr,
            "usage: run_scripts <gamedata/IAM> <vm_opcodes.json> <START> <out.bin>\n");
        return 2;
    }
    const std::string iam = argv[1];
    const auto table = omk::OpcodeTable::loadJson(argv[2]);
    if (!table.valid()) { std::fprintf(stderr, "no opcode table\n"); return 1; }

    // one state, carried across the whole corpus - as the reference does
    auto state = omk::GameState::fromFile(argv[3]);

    std::vector<std::uint8_t> body;
    std::size_t nslots = 0, ended = 0;
    for (const char* name : {"AREA", "SCENE"}) {
        const auto kind = std::strcmp(name, "AREA") == 0 ? omk::ChunkKind::Area
                                                         : omk::ChunkKind::Scene;
        const auto d = omk::DataFs::readPath(iam + "/" + name);
        const auto ar = omk::IamArchive::open(d);
        for (std::size_t c = 0; c < ar.size(); ++c) {
            const auto b = ar.chunk(c);
            if (b.empty()) continue;

            std::vector<omk::Slot> slots;
            // the chunk's own STARTUP script at +4: in no record table, so the
            // record walk cannot reach it. Reported as record -1, field 4.
            const auto at = u32at(b, 4);
            if (at != 0 && at < b.size())
                slots.push_back(omk::Slot{-1, 4, at});
            for (const auto& s : omk::chunkSlots(b, kind)) slots.push_back(s);

            for (const auto& s : slots) {
                omk::Interpreter vm(state, table);
                vm.setRecordCalls(true);
                // the corpus sweep compares decode-and-execute against `tools/sim`, whose standalone VM stubs op 70,
                // so parking there would make the two disagree about
                // something NEITHER models. The park is on everywhere
                // that actually runs the game.
                vm.setUiOpenSuspends(false);
                // ...and the same for `var.set.random` (120): the sweep's
                // reference, `tools/sim`, stubs it, and a drawn value would
                // make 197 slots diverge on the number rather than on the
                // model. The live session draws (T1, 2026-09-02).
                vm.setRandomWrites(false);
                // ...and for the WORLD opcodes - the object lists, the prop
                // state, the timer, the stat block, the held object (T12,
                // 2026-09-02): `tools/sim` stubs all twenty, and 975
                // inventory sites alone would otherwise move the final DB
                // that this sweep compares byte for byte. Off, the opcodes
                // are recorded and inert, exactly as before.
                vm.setWorldWrites(false);
                const auto r = vm.run(b, s.offset);
                ++nslots;
                ended += (r.status == omk::RunStatus::End);
                put32(body, static_cast<std::int32_t>(c));
                put32(body, s.record);
                put32(body, s.field);
                put32(body, static_cast<std::int32_t>(s.offset));
                put32(body, static_cast<std::int32_t>(r.status));
                put32(body, static_cast<std::int32_t>(r.steps));
                put32(body, static_cast<std::int32_t>(r.calls.size()));
                for (const auto& call : r.calls) body.push_back(call.op);
            }
        }
    }
    // GLOBAL last, as the reference walks it: a plain file, its own header
    {
        const auto d = omk::DataFs::readPath(iam + "/GLOBAL");
        for (const auto& s : omk::globalSlots(d)) {
            omk::Interpreter vm(state, table);
            vm.setRecordCalls(true);
            vm.setRandomWrites(false);             // as above
            vm.setWorldWrites(false);              // as above
            const auto r = vm.run(d, s.offset);
            ++nslots;
            ended += (r.status == omk::RunStatus::End);
            put32(body, -1);                       // no chunk: GLOBAL is flat
            put32(body, s.record);
            put32(body, s.field);
            put32(body, static_cast<std::int32_t>(s.offset));
            put32(body, static_cast<std::int32_t>(r.status));
            put32(body, static_cast<std::int32_t>(r.steps));
            put32(body, static_cast<std::int32_t>(r.calls.size()));
            for (const auto& call : r.calls) body.push_back(call.op);
        }
    }

    std::vector<std::uint8_t> o;
    put32(o, static_cast<std::int32_t>(nslots));
    o.insert(o.end(), body.begin(), body.end());

    // The final game DB, appended whole. The corpus is executed against ONE
    // state carried across it, so every variable write, bit flip and
    // scene.load lands here - which makes this the strongest comparison
    // available: a divergence anywhere in the arithmetic shows up as a byte.
    const auto db = state.raw();
    o.insert(o.end(), reinterpret_cast<const std::uint8_t*>(db.data()),
             reinterpret_cast<const std::uint8_t*>(db.data()) + db.size());

    if (!omk::safeOutputPath(argv[4])) return 2;
    std::ofstream f(argv[4], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));
    std::printf("%zu slots executed, %zu reached `end`, %zu-byte state\n",
                nslots, ended, db.size());
    return 0;
}
