// SPDX-License-Identifier: GPL-3.0-or-later
// The .3DO light table's COUNT, over whatever models are named.
//
//     mesh_lights <model.3DO>...
//
// Prints one summary line: files withLights totalLights maxLights maxFile
//
// The 1999 spec sheet claims "Multilights"; the header has carried `lightOff`
// (+40) and `lights` (desc+232) since the format was decoded and nothing has
// ever read the RECORDS. This measures how much is there.
// `verify.py: mesh lights` drives it. See `todo/engine-spec-1999.md`.
#include "platform/datafs.h"
#include "formats/mesh3do.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    long files = 0, withLights = 0, total = 0, mx = 0;
    std::string mxName;
    for (int i = 1; i < argc; ++i) {
        const auto d = omk::DataFs::readPath(argv[i]);
        if (d.empty()) continue;
        const auto h = omk::readHeader(d);
        if (!h) continue;
        ++files;
        total += h->lights;
        if (h->lights > 0) {
            ++withLights;
            if (h->lights > mx) { mx = h->lights; mxName = argv[i]; }
        }
    }
    const auto slash = mxName.find_last_of("/\\");
    std::printf("%ld %ld %ld %ld %s\n", files, withLights, total, mx,
                slash == std::string::npos ? mxName.c_str() : mxName.c_str() + slash + 1);
    return 0;
}
