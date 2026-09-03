// SPDX-License-Identifier: GPL-3.0-or-later
// Walk every .CTL and report the invariant the format is checked by: the walk
// must land EXACTLY on the file size.
//
//     dump_ctl <gamedata/ANIMS> <out.bin>
//
// Nothing in the file points at the next section - the reader adds up nine
// variable-length blocks, each gated by a flag - so landing on the size is not
// a tidiness check, it is the whole proof that every gate was read right.
#include "formats/ctl.h"

#include <algorithm>
#include <cctype>
#include "platform/datafs.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace {
bool isCtl(const fs::path& p) {
    auto e = p.extension().string();
    for (auto& c : e) c = static_cast<char>(std::tolower(c));
    return e == ".ctl";
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: dump_ctl <gamedata/ANIMS> <out.bin>\n");
        return 2;
    }
    std::vector<fs::path> paths;
    for (const auto& e : fs::directory_iterator(argv[1]))
        if (e.is_regular_file() && isCtl(e.path())) paths.push_back(e.path());
    std::sort(paths.begin(), paths.end());

    int files = 0, exact = 0, clips = 0, states = 0;
    int edges = 0, badEdges = 0, gotos = 0, badGotos = 0;
    for (const auto& p : paths) {
        const auto d = omk::DataFs::readPath(p.string());
        const auto f = omk::readCtl(d);
        if (!f.valid) continue;
        ++files;
        exact += f.exact ? 1 : 0;
        clips += static_cast<int>(f.clips.size());
        states += static_cast<int>(f.states.size());
        for (const auto& s : f.states) {
            edges += static_cast<int>(s.children.size() + s.parents.size());
            if (!s.childOk || !s.parentOk) ++badEdges;
            if (s.gotoId) { ++gotos; if (!s.gotoOk) ++badGotos; }
        }
        std::printf("  %-14s %7zu bytes, walk ends %7zu  %s  %d clips, %d states\n",
                    p.filename().string().c_str(), f.size, f.end,
                    f.exact ? "EXACT" : "off  ",
                    static_cast<int>(f.clips.size()),
                    static_cast<int>(f.states.size()));
    }
    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    for (int v : {files, exact, clips, states, edges, badEdges, gotos, badGotos})
        put32(v);
    if (!omk::safeOutputPath(argv[2])) return 2;
    std::ofstream f(argv[2], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));
    std::printf("%d files, %d walk exactly to EOF; %d clips, %d states; "
                "%d graph edges (%d states with a bad one), %d gotos (%d bad)\n",
                files, exact, clips, states, edges, badEdges, gotos, badGotos);
    return 0;
}
