// SPDX-License-Identifier: GPL-3.0-or-later
// Decode every world script in an AREA or SCENE archive, for the differential
// against tools/dialog_triggers.py + tools/dialog_disasm.py.
//
//     dump_scripts <gamedata/IAM/AREA|SCENE> AREA|SCENE <tables/vm_opcodes.json> <out.bin>
//
// Layout: int32 nSlots, then per slot
//   int32 chunk, int32 record, int32 field, int32 offset,
//   int32 status (0 ok, else a failure), int32 nInstructions,
//   then per instruction: int32 pc, uint8 op, uint8 operandLen, operand bytes
#include "formats/iam.h"
#include "script/script.h"

#include "platform/datafs.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace {


void put32(std::vector<std::uint8_t>& o, std::int32_t v) {
    const auto u = static_cast<std::uint32_t>(v);
    for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr,
            "usage: dump_scripts <archive> AREA|SCENE <vm_opcodes.json> <out.bin>\n");
        return 2;
    }
    const auto kind = std::strcmp(argv[2], "AREA") == 0 ? omk::ChunkKind::Area
                                                        : omk::ChunkKind::Scene;
    const auto table = omk::OpcodeTable::loadJson(argv[3]);
    if (!table.valid()) {
        std::fprintf(stderr, "cannot load the opcode table from %s\n", argv[3]);
        return 1;
    }

    const auto d = omk::DataFs::readPath(argv[1]);
    const auto ar = omk::IamArchive::open(d);

    std::vector<std::uint8_t> o;
    std::size_t nslots = 0, ok = 0;
    std::vector<std::uint8_t> body;
    for (std::size_t c = 0; c < ar.size(); ++c) {
        const auto b = ar.chunk(c);
        if (b.empty()) continue;
        for (const auto& s : omk::chunkSlots(b, kind)) {
            const auto r = omk::decodeScript(b, s.offset, b.size(), table);
            ++nslots;
            ok += (r.status == omk::DecodeStatus::Ok);
            put32(body, static_cast<std::int32_t>(c));
            put32(body, s.record);
            put32(body, s.field);
            put32(body, static_cast<std::int32_t>(s.offset));
            put32(body, static_cast<std::int32_t>(r.status));
            put32(body, static_cast<std::int32_t>(r.code.size()));
            for (const auto& ins : r.code) {
                put32(body, static_cast<std::int32_t>(ins.pc));
                body.push_back(ins.op);
                body.push_back(static_cast<std::uint8_t>(ins.operand.size()));
                body.insert(body.end(), ins.operand.begin(), ins.operand.end());
            }
        }
    }
    put32(o, static_cast<std::int32_t>(nslots));
    o.insert(o.end(), body.begin(), body.end());

    if (!omk::safeOutputPath(argv[4])) return 2;
    std::ofstream f(argv[4], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));
    std::printf("%zu slots, %zu decode to `end`\n", nslots, ok);
    return 0;
}
