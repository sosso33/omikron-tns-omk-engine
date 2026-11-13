// SPDX-License-Identifier: GPL-3.0-or-later
// THE SHARED ACTOR IDS of two AREA tables (`todo/drift-audit.md` M4): for
// each pair of areas named, every actor id both place, with both records'
// positions and how far apart they stand - two records at one spot draw as
// one body in the engine too; apart, the engine draws two while both areas
// are resident and the port one.
//
//     m4_pairs <gamedata> <areaA> <areaB> [<areaA> <areaB> ...]
#include "formats/iam.h"
#include "formats/placements.h"
#include "platform/datafs.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc < 4 || (argc - 2) % 2 != 0) {
        std::fprintf(stderr, "usage: m4_pairs <gamedata> <areaA> <areaB> [...]\n");
        return 2;
    }
    const omk::DataFs fs(argv[1]);
    const auto raw = fs.read("IAM/AREA");
    const auto ar = omk::IamArchive::open(raw);
    const auto table = [&](int a) {
        if (a < 0 || static_cast<std::size_t>(a) >= ar.size()) return std::vector<omk::Placement>{};
        return omk::readPlacements(ar.chunk(static_cast<std::size_t>(a)), omk::ChunkKind::Area);
    };
    for (int k = 2; k + 1 < argc; k += 2) {
        const int a = std::atoi(argv[k]), b = std::atoi(argv[k + 1]);
        const auto ta = table(a), tb = table(b);
        int shared = 0, apart = 0;
        for (const auto& ra : ta)
            for (const auto& rb : tb) {
                if (ra.actor != rb.actor) continue;
                ++shared;
                const double d = std::hypot(ra.pos[0] - rb.pos[0], ra.pos[1] - rb.pos[1], ra.pos[2] - rb.pos[2]);
                if (d > 1.0) ++apart;
                std::printf("pair %d %d: actor %d - %d at %.0f %.0f %.0f (bit %d), %d at %.0f %.0f %.0f (bit %d), "
                            "%.0f apart\n", a, b, ra.actor, a, ra.pos[0], ra.pos[1], ra.pos[2], ra.bit, b,
                            rb.pos[0], rb.pos[1], rb.pos[2], rb.bit, d);
            }
        std::printf("pair %d %d: %d shared ids, %d of them at two spots\n", a, b, shared, apart);
    }
    return 0;
}
