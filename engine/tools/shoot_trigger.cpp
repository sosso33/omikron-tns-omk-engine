// SPDX-License-Identifier: GPL-3.0-or-later
// WHAT STARTS A SHOOT PHASE - the slot that carries `shoot.begin` (op 80) in
// one chunk, and which kind of slot it is.
//
//     shoot_trigger <gamedata> <tables dir>
//
// A reader reached the supermarket phase "in adventure mode, run from the
// cutscene" and this port could not reproduce it: a street start into the
// same area leaves the player standing, because landing in a room is not the
// same as arriving in it. This says which slot the engine would have run.
#include "formats/iam.h"
#include "platform/datafs.h"
#include "script/script.h"
#include "platform/json.h"

#include <cstdio>
#include <cstring>
#include <string>

int g_chunks = 0, g_slots = 0, g_op82 = 0;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: shoot_trigger <gamedata> <tables dir>\n");
        return 2;
    }
    omk::DataFs fs(argv[1]);
    const auto table = omk::OpcodeTable::loadJson(std::string(argv[2]) + "/vm_opcodes.json");
    for (const char* arch : {"AREA", "SCENE"}) {
        const auto raw = fs.read(std::string("IAM/") + arch);
        const auto ar = omk::IamArchive::open(raw);
        const auto kind = std::strcmp(arch, "AREA") == 0 ? omk::ChunkKind::Area
                                                         : omk::ChunkKind::Scene;
        for (std::size_t c = 0; c < ar.size(); ++c) {
            const auto span = ar.chunk(c);
            if (span.size() < 8) continue;
            int zone80 = 0, start80 = 0;
            extern int g_chunks, g_slots, g_op82; ++g_chunks;
            for (const auto& sl : omk::chunkSlots(span, kind)) {
                const auto d = omk::decodeScript(span, sl.offset, span.size(), table);
                if (d.status != omk::DecodeStatus::Ok) continue;
                ++g_slots;
                for (const auto& in : d.code) { if (in.op == 80) ++zone80; if (in.op == 82) ++g_op82; }
            }
            // ...AND THE STARTUP SCRIPT AT +4, which `chunkSlots` never
            // reaches: the zone walk comes from the zone records and the
            // message subscriptions, and a cutscene runs from `+4`.
            std::uint32_t off = 0;
            std::memcpy(&off, span.data() + 4, 4);
            if (off > 0 && off < span.size()) {
                const auto d = omk::decodeScript(span, off, span.size(), table);
                if (d.status == omk::DecodeStatus::Ok)
                    for (const auto& in : d.code) if (in.op == 80) ++start80;
            }
            if (zone80 || start80)
                std::printf("%s %zu: shoot.begin in %d zone slot(s), %d in the "
                            "startup script\n", arch, c, zone80, start80);
        }
    }
    std::printf("scanned %d chunks, %d slots, %d shoot.actor.enter sites\n",
                g_chunks, g_slots, g_op82);
    return 0;
}
