// SPDX-License-Identifier: GPL-3.0-or-later
// Walk every .SCX's streamed section and check what makes it self-checking:
// every record's first header word is its own file offset, so a walk that has
// the sizes right never needs to resync.
//
//     dump_stream <gamedata/SCPTDATA> <out.bin>
//
// Chunk 4's three-word header is the fact this tests: read as
// [own, model, texture] the sprite records land exactly, with 0 resyncs.
#include "formats/scx.h"
#include "platform/datafs.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: dump_stream <gamedata/SCPTDATA> <out.bin>\n");
        return 2;
    }
    omk::DataFs dir(argv[1]);
    int files = 0, exact = 0, resyncs = 0;
    long anims = 0, sprites = 0, wavs = 0, paths = 0, keyed = 0, durOk = 0;
    for (const auto& p : dir.list(".", "scx")) {
        const auto d = omk::DataFs::readPath(p);
        const auto st = omk::readScxStream(d);
        if (!st.valid) continue;
        ++files;
        if (st.end == d.size()) ++exact;
        resyncs += st.resyncs;
        anims   += static_cast<long>(st.anims.size());
        sprites += static_cast<long>(st.sprites.size());
        wavs    += st.wavs;
        for (const auto& pa : st.paths) {
            ++paths;
            if (pa.keys.empty()) continue;
            ++keyed;
            // the header's duration must equal the LAST key's frame - the
            // invariant that says the 32-byte key stride is right
            if (pa.duration == pa.keys.back().frame) ++durOk;
        }
    }
    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    put32(files); put32(exact); put32(resyncs);
    put32(static_cast<std::int32_t>(anims));
    put32(static_cast<std::int32_t>(sprites));
    put32(static_cast<std::int32_t>(wavs));
    put32(static_cast<std::int32_t>(paths));
    put32(static_cast<std::int32_t>(keyed));
    put32(static_cast<std::int32_t>(durOk));
    if (!omk::safeOutputPath(argv[2])) return 2;
    std::ofstream f(argv[2], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));
    std::printf("%d scenes, %d whose stream walk lands on EOF, %d resyncs; "
                "%ld clips, %ld sprites, %ld wavs, %ld paths (%ld keyed, "
                "%ld whose duration == the last key's frame)\n",
                files, exact, resyncs, anims, sprites, wavs, paths, keyed, durOk);
    return 0;
}
