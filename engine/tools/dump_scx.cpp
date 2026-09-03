// SPDX-License-Identifier: GPL-3.0-or-later
// Walk every .SCX and report the invariant: the chunk walk must land inside
// the block, and chunk 2 must land exactly on the next chunk tag.
//
//     dump_scx <gamedata/SCPTDATA> <out.bin>
#include "formats/scx.h"

#include <algorithm>
#include <cctype>
#include "platform/datafs.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace {
bool isScx(const fs::path& p) {
    auto e = p.extension().string();
    for (auto& c : e) c = static_cast<char>(std::tolower(c));
    return e == ".scx";
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: dump_scx <gamedata/SCPTDATA> <out.bin>\n");
        return 2;
    }
    std::vector<fs::path> paths;
    for (const auto& e : fs::directory_iterator(argv[1]))
        if (e.is_regular_file() && isScx(e.path())) paths.push_back(e.path());
    std::sort(paths.begin(), paths.end());

    int files = 0, complete = 0, withObjects = 0;
    long objects = 0, functions = 0, links = 0, params = 0;
    for (const auto& p : paths) {
        const auto d = omk::DataFs::readPath(p.string());
        const auto s = omk::readScx(d);
        if (!s.valid) continue;
        ++files;
        complete += s.complete ? 1 : 0;
        if (!s.objects.empty()) ++withObjects;
        objects += static_cast<long>(s.objects.size());
        for (const auto& ob : s.objects) {
            functions += static_cast<long>(ob.functions.size());
            links += ob.hasLink ? 1 : 0;
            for (const auto& fn : ob.functions)
                params += static_cast<long>(fn.params.size());
        }
    }
    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    put32(files); put32(complete); put32(withObjects);
    put32(static_cast<std::int32_t>(objects));
    put32(static_cast<std::int32_t>(functions));
    put32(static_cast<std::int32_t>(links));
    put32(static_cast<std::int32_t>(params));
    if (!omk::safeOutputPath(argv[2])) return 2;
    std::ofstream f(argv[2], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));
    std::printf("%d scenes, %d whose chunk walk stays in the block, %d with "
                "objects; %ld objects, %ld functions, %ld linked pairs, "
                "%ld params\n",
                files, complete, withObjects, objects, functions, links, params);
    return 0;
}
