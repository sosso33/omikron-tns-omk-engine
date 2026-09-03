// SPDX-License-Identifier: GPL-3.0-or-later
// Walk every .3DM and check what the format is proved by: the frame count is
// DERIVED, and the only remainder that occurs is a last frame with no audio.
//
//     dump_morphs <gamedata/MORPH> <out.bin>
#include "formats/adpcm.h"
#include "formats/morph.h"
#include "platform/datafs.h"

#include <cstdio>
#include <fstream>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: dump_morphs <gamedata/MORPH> <out.bin>\n");
        return 2;
    }
    omk::DataFs dir(argv[1]);
    // the codec's tables come from tables/adpcm.json when it is given, and
    // are compared against the built-in copy so a drift cannot hide
    const auto tables = (argc > 3) ? omk::AdpcmTables::loadJson(argv[3])
                                   : omk::AdpcmTables::builtin();
    const auto builtin = omk::AdpcmTables::builtin();
    const bool tablesAgree = tables.valid() &&
        tables.step() == builtin.step() && tables.index() == builtin.index();

    std::vector<std::uint32_t> hashes;
    int files = 0, exact = 0, shortByAudio = 0, iota = 0, other = 0;
    long frames = 0, audio = 0, samples = 0;
    for (const auto& p : dir.list(".", "3dm")) {
        const auto d = omk::DataFs::readPath(p);
        const auto L = omk::morphLayout(d);
        if (!L.valid) continue;
        ++files;
        if (L.tail == 0) ++exact;
        else if (L.tail == L.record - L.audio) ++shortByAudio;
        else ++other;
        if (L.preambleIsIota) ++iota;
        frames += static_cast<long>(L.frames);
        const auto blocks = omk::morphAudio(d, L);
        audio += static_cast<long>(blocks.size());
        // two samples a byte, per channel-interleaved nibble pair
        const auto pcm = omk::adpcmDecode(blocks, L.channels == 2, tables);
        samples += static_cast<long>(pcm.size());
        // an FNV-1a over the decoded samples, so the differential against
        // tools/adp.py can compare every one of the 225 million without
        // writing 450 MB
        std::uint32_t h = 2166136261u;
        for (auto v : pcm) {
            const auto u = static_cast<std::uint16_t>(v);
            h ^= static_cast<std::uint8_t>(u);        h *= 16777619u;
            h ^= static_cast<std::uint8_t>(u >> 8);   h *= 16777619u;
        }
        hashes.push_back(h);
    }
    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    put32(files); put32(exact); put32(shortByAudio); put32(other); put32(iota);
    put32(static_cast<std::int32_t>(frames));
    put32(tablesAgree ? 1 : 0);
    put32(static_cast<std::int32_t>(samples == 2 * audio ? 1 : 0));
    put32(static_cast<std::int32_t>(hashes.size()));
    for (auto h : hashes) put32(static_cast<std::int32_t>(h));
    if (!omk::safeOutputPath(argv[2])) return 2;
    std::ofstream f(argv[2], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));
    std::printf("%d .3DM files: %d land on a record boundary, %d short by "
                "exactly the last audio block, %d neither; %d with a 0,1,2,... "
                "preamble; %ld frames, %ld bytes of ADPCM\n",
                files, exact, shortByAudio, other, iota, frames, audio);
    std::printf("ADPCM: tables agree with the built-in copy: %s; "
                "%ld samples decoded (%s two a byte)\n",
                tablesAgree ? "yes" : "NO",
                samples, samples == 2 * audio ? "exactly" : "NOT");
    return 0;
}
