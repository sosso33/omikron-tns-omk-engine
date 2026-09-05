// SPDX-License-Identifier: GPL-3.0-or-later
//
// List a .3DO's meshes: index, name, flags and authored position - what a
// probe needs before it can stand a walker next to a named door.
//
//     mesh_list <model.3DO> [substring]
#include "formats/mesh3do.h"
#include "platform/datafs.h"

#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: mesh_list <model.3DO> [substring]\n"); return 2; }
    const auto d = omk::DataFs::readPath(argv[1]);
    const auto h = omk::readHeader(d);
    if (!h) { std::fprintf(stderr, "not a .3DO\n"); return 1; }
    const auto ms = omk::readMeshes(d, *h);
    const std::string want = argc > 2 ? argv[2] : "";
    for (const auto& m : ms) {
        if (!want.empty() && !std::strstr(m.name, want.c_str())) continue;
        std::printf("%4d  %-22s flags %08x  pos %8.1f %8.1f %8.1f  local %7.1f %7.1f %7.1f  parent %d\n",
                    m.index, m.name, static_cast<unsigned>(m.flags),
                    m.pos[0], m.pos[1], m.pos[2], m.local[0], m.local[1], m.local[2], m.parent);
    }
    return 0;
}
