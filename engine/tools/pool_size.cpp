// SPDX-License-Identifier: GPL-3.0-or-later
// How many textures a set puts in the render pool. The bucket key's low SIX
// bits are the texture slot (`slot & 0x3F`), so the whole pool - the set's
// textures, every staged model's, the player's and the sprites' - has to stay
// inside 64 or a slot aliases onto another picture.
//
//     pool_size <gamedata> <SET.3DO>...
#include "formats/mesh3do.h"
#include "formats/tex3dt.h"
#include "platform/datafs.h"
#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    const omk::DataFs fs(argv[1]);
    for (int a = 2; a < argc; ++a) {
        const auto p = fs.resolve(std::string("MESHES/DECORS/") + argv[a] + ".3DO");
        if (!p) { std::printf("%-12s (not found)\n", argv[a]); continue; }
        const auto d = omk::DataFs::readPath(*p);
        const auto t = fs.resolve(std::string("MESHES/DECORS/") + argv[a] + ".3DT");
        const auto tex = t ? omk::textures(d, omk::DataFs::readPath(*t))
                           : std::vector<omk::Texture>{};
        std::printf("%-12s %3zu textures\n", argv[a], tex.size());
    }
    return 0;
}
