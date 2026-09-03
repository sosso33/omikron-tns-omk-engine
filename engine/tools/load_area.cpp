// SPDX-License-Identifier: GPL-3.0-or-later
// Load an area and run what the load queues - Area_TickLoad case 9.
//
//     load_area <gamedata/IAM> <vm_opcodes.json> <START> <areaId> <out.bin>
//
// Also sweeps every chunk for a startup script at +4, so the corpus claim
// (173 of 330 carry one, and every one of them decodes) is checked by the
// ported reader rather than only by the Python.
//
// Layout: int32 nStartup, per run {int32 isScene, chunk, offset, status,
//   nCalls, then per call uint8 op, uint8 nFields, int16 fields...};
//   then int32 areaChunks, sceneChunks, withStartup, without, failed,
//   totalInstructions.
#include "formats/iam.h"
#include "script/area.h"
#include "script/world.h"

#include "platform/datafs.h"

#include <cstdio>
#include <cstdlib>
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
            "usage: load_area <gamedata/IAM> <vm_opcodes.json> <START> <areaId> <out.bin>\n");
        return 2;
    }
    const std::string iam = argv[1];
    const auto table = omk::OpcodeTable::loadJson(argv[2]);
    auto state = omk::GameState::fromFile(argv[3]);
    const int area = std::atoi(argv[4]);

    // `--var N=V` seeds a game variable before the load.
    //
    // Needed because `ui.open` is a PLAYER QUESTION: its handler parks the
    // context at status 6 and the interface later writes the answer into the
    // named variable. Nothing here models the interface, so the script runs
    // straight through and the variable has to carry what the player chose.
    // The intro's is variable 19 `Interface`, and the branch is binary - the
    // other arm is a different, shorter opening.
    for (int i = 6; i < argc; ++i) {
        if (std::strncmp(argv[i], "--var", 5) != 0 || i + 1 >= argc) continue;
        const char* eq = std::strchr(argv[i + 1], '=');
        if (!eq) continue;
        state.setVar(std::atoi(argv[i + 1]), std::atoi(eq + 1));
    }

    const auto runs = omk::loadArea(iam, area, state, table);

    std::vector<std::uint8_t> o;
    put32(o, static_cast<std::int32_t>(runs.size()));
    for (const auto& r : runs) {
        put32(o, r.isScene ? 1 : 0);
        put32(o, r.chunk);
        put32(o, static_cast<std::int32_t>(r.offset));
        put32(o, static_cast<std::int32_t>(r.status));
        put32(o, static_cast<std::int32_t>(r.calls.size()));
        for (const auto& c : r.calls) {
            o.push_back(c.op);
            o.push_back(static_cast<std::uint8_t>(c.fields.size()));
            for (auto f : c.fields) {
                const auto u = static_cast<std::uint16_t>(f);
                o.push_back(static_cast<std::uint8_t>(u));
                o.push_back(static_cast<std::uint8_t>(u >> 8));
            }
        }
    }

    // the corpus sweep: every chunk's +4
    int areaChunks = 0, sceneChunks = 0, with = 0, without = 0, failed = 0;
    long instrs = 0;
    for (const char* name : {"AREA", "SCENE"}) {
        const auto d = omk::DataFs::readPath(iam + "/" + name);
        const auto ar = omk::IamArchive::open(d);
        for (std::size_t c = 0; c < ar.size(); ++c) {
            const auto b = ar.chunk(c);
            if (b.empty()) continue;
            (std::strcmp(name, "AREA") == 0 ? areaChunks : sceneChunks) += 1;
            const auto at = omk::startupScript(b);
            if (at == 0) { ++without; continue; }
            const auto r = omk::decodeScript(b, at, b.size(), table);
            if (r.status == omk::DecodeStatus::Ok) {
                ++with; instrs += static_cast<long>(r.code.size());
            } else {
                ++failed;
            }
        }
    }
    put32(o, areaChunks); put32(o, sceneChunks);
    put32(o, with); put32(o, without); put32(o, failed);
    put32(o, static_cast<std::int32_t>(instrs));

    if (!omk::safeOutputPath(argv[5])) return 2;
    std::ofstream f(argv[5], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));
    std::printf("area %d: %zu startup scripts ran; corpus %d+%d chunks, "
                "%d with a +4 script, %d without, %d failed, %ld instructions\n",
                area, runs.size(), areaChunks, sceneChunks, with, without,
                failed, instrs);
    return 0;
}
