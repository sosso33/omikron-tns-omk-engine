// SPDX-License-Identifier: GPL-3.0-or-later
// The fight-AI profiles out of every `.CTL`, for the differential against
// tools/anim_ctl.py.
//
//     dump_fight_ai <gamedata/ANIMS> <out.bin>
//
// out.bin: int32 files, exact walks, files with profiles, profiles,
//          int32 distinct input words, allInRange, bitUnion,
//          int32 slowProfiles (move delay >= 3000 ms),
//          then per profile: int32 id, moves, words
#include "formats/ctl.h"
#include "platform/datafs.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <set>
#include <string>
#include <tuple>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: dump_fight_ai <gamedata/ANIMS> <out.bin>\n");
        return 2;
    }
    // Both cases, because two of the seven ship lowercase and they are where
    // two of the three profile sets live - a case-sensitive listing here
    // reports "one file has an AI" and looks like a finding.
    const omk::DataFs fs(argv[1]);
    auto files = fs.list(".", ".CTL");
    std::sort(files.begin(), files.end());

    int exact = 0, withAi = 0;
    std::set<std::uint32_t> words;
    std::uint32_t bits = 0;
    bool inRange = true;
    int slow = 0;
    std::vector<std::tuple<std::string, std::uint32_t, long, long>> per;
    for (const auto& path : files) {
        // `list` hands back full paths, so this is readPath, not read - which
        // takes a root-relative name and would find nothing.
        const auto f = omk::readCtl(omk::DataFs::readPath(path));
        const auto slash = path.find_last_of('/');
        const std::string nm = slash == std::string::npos ? path
                                                          : path.substr(slash + 1);
        if (f.exact) ++exact;
        if (f.ai.empty()) continue;
        ++withAi;
        for (const auto& p : f.ai) {
            long moves = 0, nw = 0;
            for (const auto& sl : p.slots) {
                moves += static_cast<long>(sl.moves.size());
                for (const auto& m : sl.moves) {
                    nw += static_cast<long>(m.size());
                    for (auto v : m) {
                        words.insert(v);
                        bits |= v & 0x3FFFu;
                        if ((v & ~0x40000000u) > 0x3FFFu) inRange = false;
                    }
                }
            }
            if (p.moveDelay[0] >= 3000) ++slow;
            per.push_back({nm, p.id, moves, nw});
        }
    }
    std::sort(per.begin(), per.end());

    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    put32(static_cast<std::int32_t>(files.size()));
    put32(exact); put32(withAi); put32(static_cast<std::int32_t>(per.size()));
    put32(static_cast<std::int32_t>(words.size()));
    put32(inRange ? 1 : 0);
    put32(static_cast<std::int32_t>(bits));
    put32(slow);
    for (const auto& [nm, id, moves, nw] : per) {
        put32(static_cast<std::int32_t>(id));
        put32(static_cast<std::int32_t>(moves));
        put32(static_cast<std::int32_t>(nw));
    }
    if (!omk::safeOutputPath(argv[2])) return 2;
    std::ofstream out(argv[2], std::ios::binary);
    out.write(reinterpret_cast<const char*>(o.data()),
              static_cast<std::streamsize>(o.size()));
    std::printf("%zu .CTL files, %d walking exactly to EOF, %d with fight AI, "
                "%zu profiles; %zu distinct input words, all in range: %s, "
                "bit union 0x%X, %d profiles with a move delay >= 3000 ms\n",
                files.size(), exact, withAi, per.size(), words.size(),
                inRange ? "yes" : "NO", bits, slow);
    for (const auto& [nm, id, moves, nw] : per)
        std::printf("    %-14s level %u: %ld moves, %ld input words\n",
                    nm.c_str(), id, moves, nw);
    return 0;
}
