// SPDX-License-Identifier: GPL-3.0-or-later
// IS A DOOR COLLIDABLE? - which meshes reach each collision soup.
//
//     soup_probe <set.3DO> <mesh name>...
//
// The viewer patches the collision soups from the same corners it patches the
// render from, so a door drawn in the wrong place was collided in the wrong
// place too. That is fixed; what this asks is the question underneath, which
// nothing had asked: does the door mesh contribute collision triangles AT ALL,
// and to which soup. `Walkable` is the floor probe (flatter than 30 degrees),
// `Steep` its complement - a surface the walker slides off rather than a wall
// it stops against - and `All` is the narrow phase.
#include "o3de/collision.h"
#include "o3de/geom3do.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: soup_probe <set.3DO> <mesh>...\n");
        return 2;
    }
    std::ifstream f(argv[1], std::ios::binary);
    const std::vector<char> raw((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
    if (raw.empty()) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    const std::span<const std::byte> d(
        reinterpret_cast<const std::byte*>(raw.data()), raw.size());

    const auto header = omk::readHeader(d);
    if (!header) { std::fprintf(stderr, "not a .3DO\n"); return 1; }
    const auto ms = omk::readMeshes(d, *header);

    struct Kind { const char* name; omk::SoupKind k; };
    const Kind kinds[] = {{"walkable", omk::SoupKind::Walkable},
                          {"steep",    omk::SoupKind::Steep},
                          {"all",      omk::SoupKind::All}};
    for (int a = 2; a < argc; ++a) {
        const std::string want = argv[a];
        int idx = -1;
        for (std::size_t i = 0; i < ms.size(); ++i)
            if (ms[i].name == want) idx = static_cast<int>(i);
        if (idx < 0) { std::printf("%-14s NOT IN THE SET\n", want.c_str()); continue; }
        std::printf("%-14s mesh %3d flags %08X", want.c_str(), idx, ms[static_cast<std::size_t>(idx)].flags);
        for (const auto& kk : kinds) {
            std::vector<int> meshOf;
            const auto soup = omk::collisionSoup(d, kk.k, &meshOf);
            long n = 0;
            for (int m : meshOf) n += (m == idx);
            std::printf("  %s %ld", kk.name, n);
        }
        std::printf("\n");
    }
    return 0;
}
