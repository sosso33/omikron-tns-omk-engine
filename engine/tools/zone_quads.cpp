// SPDX-License-Identifier: GPL-3.0-or-later
// A CHUNK'S TRIGGER ZONES as centres - where a door actually is.
//
//     zone_quads <gamedata>
//
// The 68-byte records of AREA 0, printed as the centre of each quad with its
// facing arc, for finding the zone behind a script you have read: the bank's
// door is AREA 0's record 8, `area.goto 223`, at (4463, -112, -2577) with an
// arc centred on 3072/4096 - so you enter it walking -x. Reading the script
// gives you the destination; this gives you the place.
#include "script/world.h"
#include "formats/iam.h"
#include "platform/datafs.h"
#include <cstdio>
int main(int argc, char** argv) {
    const auto f = omk::DataFs::readPath(std::string(argv[1]) + "/IAM/AREA");
    const auto a = omk::IamArchive::open(f);
    const auto ch = a.chunk(0);
    const auto zs = omk::zonesOf(ch, omk::ChunkKind::Area);
    for (std::size_t i = 0; i < zs.size(); ++i) {
        double cx = 0, cy = 0, cz = 0;
        for (int k = 0; k < 4; ++k) { cx += zs[i].quad[k][0]; cy += zs[i].quad[k][1]; cz += zs[i].quad[k][2]; }
        std::printf("zone %2zu id %4d  centre %7.0f %6.0f %7.0f  arc %d+-%d\n",
                    i, zs[i].id, cx/4, cy/4, cz/4, zs[i].arcMid, zs[i].arcWide);
    }
    return 0;
}
