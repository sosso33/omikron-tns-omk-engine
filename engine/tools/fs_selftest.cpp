// SPDX-License-Identifier: GPL-3.0-or-later
// Prove the data filesystem is genuinely case-insensitive, on real data.
//
//     fs_selftest <gamedata> <out.bin>
//
// The test is deliberately hostile: every shipped file is asked for again with
// its path MANGLED - all-upper, all-lower, and with the directory components
// cased the other way - and every one must come back resolving to the same
// real file. A resolver that only lowercased the extension, or only handled
// the last component, fails this.
#include "platform/datafs.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
// flip the case of every letter - the spelling nobody would ever type
std::string flip(std::string s) {
    for (auto& c : s) {
        const auto u = static_cast<unsigned char>(c);
        c = std::isupper(u) ? static_cast<char>(std::tolower(u))
                            : static_cast<char>(std::toupper(u));
    }
    return s;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: fs_selftest <gamedata> <out.bin>\n");
        return 2;
    }
    const std::string root = argv[1];
    omk::DataFs data(root);

    // every file under a handful of the data directories, as data-relative paths
    const char* dirs[] = {"IAM", "SCPTDATA", "ANIMS", "MESHES/DECORS",
                          "MESHES/PERSOS", "MESHES/OBJETS", "MORPH"};
    std::vector<std::string> rels;
    for (const char* d : dirs) {
        const auto real = data.resolve(d);
        if (!real) continue;
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(*real, ec)) {
            if (!e.is_regular_file()) continue;
            rels.push_back(std::string(d) + "/" + e.path().filename().string());
        }
    }
    std::sort(rels.begin(), rels.end());

    int total = 0, asis = 0, up = 0, low = 0, flipped = 0, sameFile = 0;
    for (const auto& rel : rels) {
        ++total;
        const auto a = data.resolve(rel);
        const auto b = data.resolve(upper(rel));
        const auto c = data.resolve(lower(rel));
        const auto f = data.resolve(flip(rel));
        asis += a.has_value(); up += b.has_value();
        low += c.has_value(); flipped += f.has_value();
        if (a && b && c && f && *a == *b && *a == *c && *a == *f) ++sameFile;
    }

    // and the sibling lookup the .3DO -> .3dt case needs
    int models = 0, siblings = 0;
    for (const auto& p : data.list("MESHES/DECORS", "3do")) {
        ++models;
        const auto rel = std::string("MESHES/DECORS/") + fs::path(p).filename().string();
        if (data.resolveSibling(rel, "3DT")) ++siblings;
    }

    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    for (int v : {total, asis, up, low, flipped, sameFile, models, siblings})
        put32(v);
    if (!omk::safeOutputPath(argv[2])) return 2;
    std::ofstream out(argv[2], std::ios::binary);
    out.write(reinterpret_cast<const char*>(o.data()),
              static_cast<std::streamsize>(o.size()));
    std::printf("%d data files: %d resolve as-is, %d UPPERCASED, %d lowercased, "
                "%d case-flipped, %d to the same real file\n"
                "%d decor models, %d whose .3dt sibling resolves\n",
                total, asis, up, low, flipped, sameFile, models, siblings);
    return 0;
}
