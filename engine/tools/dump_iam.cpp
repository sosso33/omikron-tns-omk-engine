// SPDX-License-Identifier: GPL-3.0-or-later
// Dump an IAM archive's directory, for the differential against
// tools/dialog_triggers.py's archive().
//
//     dump_iam <archive> <out.bin>
//
// Layout: int32 nEntries, then per PRESENT entry {int32 index, uint32 offset,
// uint32 size, uint32 fnv1a of the chunk bytes}. The hash is there so the
// comparison covers the payloads without writing megabytes.
#include "formats/iam.h"

#include "platform/datafs.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace {


std::uint32_t fnv1a(std::span<const std::byte> b) {
    std::uint32_t h = 2166136261u;
    for (auto v : b) { h ^= static_cast<std::uint8_t>(v); h *= 16777619u; }
    return h;
}

void put32(std::vector<std::uint8_t>& o, std::uint32_t v) {
    for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: dump_iam <archive> <out.bin>\n");
        return 2;
    }
    const auto d = omk::DataFs::readPath(argv[1]);
    const auto a = omk::IamArchive::open(d);

    std::vector<std::uint8_t> o;
    put32(o, static_cast<std::uint32_t>(a.populated()));
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto e = a.entry(i);
        if (!e.present()) continue;
        put32(o, static_cast<std::uint32_t>(i));
        put32(o, e.offset);
        put32(o, e.size);
        put32(o, fnv1a(a.chunk(i)));
    }
    if (!omk::safeOutputPath(argv[2])) return 2;
    std::ofstream f(argv[2], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));
    std::printf("%zu directory entries, %zu with data\n", a.size(), a.populated());
    return 0;
}
