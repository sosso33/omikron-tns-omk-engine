// SPDX-License-Identifier: GPL-3.0-or-later
// Every scene's camera editings, out of the .SCX stream's chunk 10.
//
//     dump_camedit <gamedata/SCPTDATA> <out.bin>
//
// The invariant is the loader's own: it resolves every id to a pointer and
// returns 0 on a miss, so a dangling reference would stop the scene loading.
// Counting them is a test, not a statistic.
#include "formats/scx.h"
#include "o3de/camedit.h"
#include "platform/datafs.h"

#include <cstdio>
#include <fstream>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: dump_camedit <gamedata/SCPTDATA> <out.bin>\n");
        return 2;
    }
    omk::DataFs dir(argv[1]);
    int scenes = 0, with10 = 0, exact = 0, unresolved = 0;
    long cams = 0, keys = 0, tracks = 0, editings = 0, unlinked = 0, frames = 0;
    for (const auto& p : dir.list(".", "scx")) {
        const auto d = omk::DataFs::readPath(p);
        const auto st = omk::readScxStream(d);
        if (!st.valid) continue;
        ++scenes;
        if (!st.camSize) continue;
        ++with10;
        const auto cf = omk::readCamFile(d.size() >= st.camOffset + st.camSize
            ? std::span<const std::byte>(d).subspan(st.camOffset, st.camSize)
            : std::span<const std::byte>{});
        if (!cf.valid) continue;
        if (cf.exact) ++exact;
        unresolved += cf.unresolved;
        cams   += static_cast<long>(cf.cameras.size());
        keys   += static_cast<long>(cf.keys.size());
        tracks += static_cast<long>(cf.tracks.size());
        editings += static_cast<long>(cf.editings.size());
        for (const auto& e : cf.editings) {
            unlinked += (e.objectHandle == 0);
            frames += e.duration;
        }
    }
    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    put32(scenes); put32(with10); put32(exact); put32(unresolved);
    for (long v : {cams, keys, tracks, editings, unlinked, frames})
        put32(static_cast<std::int32_t>(v));
    if (!omk::safeOutputPath(argv[2])) return 2;
    std::ofstream f(argv[2], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));
    std::printf("%d scenes, %d with a chunk 10, %d whose walk is exact, "
                "%d unresolved refs; %ld cameras, %ld keys, %ld tracks, "
                "%ld editings (%ld unlinked), %ld frames\n",
                scenes, with10, exact, unresolved, cams, keys, tracks,
                editings, unlinked, frames);
    return 0;
}
