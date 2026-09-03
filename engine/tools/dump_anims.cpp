// SPDX-License-Identifier: GPL-3.0-or-later
// Walk every .ani library and check the one thing the data could fail: every
// rotation key must be a UNIT quaternion.
//
//     dump_anims <gamedata/ANIMS> <out.bin>
//
// A wrong track offset lands on numbers that are not unit, which is what makes
// this a test rather than a tally - and it is exactly how the "offsets are
// relative to the descriptor" reading was confirmed.
#include "formats/anim.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include "platform/datafs.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
bool isAni(const fs::path& p) {
    auto e = p.extension().string();
    for (auto& c : e) c = static_cast<char>(std::tolower(c));
    return e == ".ani";
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: dump_anims <gamedata/ANIMS> <out.bin>\n");
        return 2;
    }
    int files = 0, clips = 0;
    long keys = 0, unit = 0, posKeys = 0;
    std::vector<fs::path> paths;
    for (const auto& e : fs::directory_iterator(argv[1]))
        if (e.is_regular_file() && isAni(e.path())) paths.push_back(e.path());
    std::sort(paths.begin(), paths.end());

    for (const auto& p : paths) {
        const auto d = omk::DataFs::readPath(p.string());
        const auto cs = omk::animClips(d);
        if (cs.empty()) continue;
        ++files;
        for (const auto& c : cs) {
            const auto desc = omk::animDescriptor(d, c.descriptor);
            if (!desc) continue;
            ++clips;
            for (const auto& t : desc->tracks) {
                posKeys += t.posKeys;
                for (const auto& q : omk::animRotations(d, t)) {
                    ++keys;
                    const double n = std::sqrt(static_cast<double>(q.w) * q.w +
                                               static_cast<double>(q.x) * q.x +
                                               static_cast<double>(q.y) * q.y +
                                               static_cast<double>(q.z) * q.z);
                    if (std::fabs(n - 1.0) < 1e-3) ++unit;
                }
            }
        }
    }
    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    put32(files); put32(clips);
    put32(static_cast<std::int32_t>(keys));
    put32(static_cast<std::int32_t>(unit));
    put32(static_cast<std::int32_t>(posKeys));
    if (!omk::safeOutputPath(argv[2])) return 2;
    std::ofstream f(argv[2], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));
    std::printf("%d libraries, %d clips, %ld rotation keys of which %ld unit, "
                "%ld position keys\n", files, clips, keys, unit, posKeys);
    return 0;
}
