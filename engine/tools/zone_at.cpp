// SPDX-License-Identifier: GPL-3.0-or-later
// Which trigger zones of an area cover a point, and what their arcs are.
// `Actor_ScanZones` tests containment first, then the facing arc at +48/+50,
// and only a zone whose arc also matches raises event 7 - the prompt slot a
// press needs. A prop is NOT a zone, so an object with no zone over it can
// never arm one.
//
//     zone_at <gamedata> <vm_opcodes.json> <START> <area> <x> <z>
#include "script/area.h"
#include "platform/datafs.h"
#include <cstdio>
#include <cmath>
#include <string>

int main(int argc, char** argv) {
    if (argc < 7) {
        std::fprintf(stderr, "usage: zone_at <gamedata> <vm_opcodes.json> <START>"
                             " <area> <x> <z>\n");
        return 2;
    }
    const std::string data = argv[1];
    const auto table = omk::OpcodeTable::loadJson(argv[2]);
    auto state = omk::GameState::fromFile(argv[3]);
    const int area = std::atoi(argv[4]);
    const float px = static_cast<float>(std::atof(argv[5]));
    const float pz = static_cast<float>(std::atof(argv[6]));
    omk::Session s(data + "/IAM", state, table);
    s.loadArea(area);
    for (int i = 0; i < 64 && s.loading(); ++i) s.frame();
    const auto& reg = s.zones().registered();
    std::printf("AREA %d: %zu zones registered\n", area, reg.size());
    int covering = 0;
    for (const auto& z : reg) {
        float lo[2] = {1e9f, 1e9f}, hi[2] = {-1e9f, -1e9f};
        for (int c = 0; c < 4; ++c) {
            const float qx = static_cast<float>(z.zone.quad[c][0]), qz = static_cast<float>(z.zone.quad[c][2]);
            if (qx < lo[0]) lo[0] = qx;  if (qx > hi[0]) hi[0] = qx;
            if (qz < lo[1]) lo[1] = qz;  if (qz > hi[1]) hi[1] = qz;
        }
        const bool in = px >= lo[0] && px <= hi[0] && pz >= lo[1] && pz <= hi[1];
        if (in) ++covering;
        std::printf("  zone %5d  x %8.0f..%-8.0f z %8.0f..%-8.0f arc %6.1f +-%-5.1f %s\n",
                    z.zone.id, lo[0], hi[0], lo[1], hi[1],
                    z.zone.arcMid * 360.0 / 4096.0, z.zone.arcWide * 360.0 / 4096.0,
                    in ? "  <- COVERS THE POINT" : "");
    }
    std::printf("%d of %zu zones cover (%.0f, %.0f)\n", covering, reg.size(), px, pz);
    return 0;
}
