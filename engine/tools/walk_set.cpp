// SPDX-License-Identifier: GPL-3.0-or-later
// Walk a spiral across a set and report what the ground decided - the stage-5
// test, ported.
//
//     walk_set <model.3DO> <x> <y> <z> <steps> <out.bin>
//
// The reverts are as much the point as the moves: a walker that always
// succeeded would be one that had stopped testing.
#include "actor/walk.h"

#include <cmath>
#include "platform/datafs.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

namespace {
}  // namespace

int main(int argc, char** argv) {
    if (argc < 7) {
        std::fprintf(stderr,
            "usage: walk_set <model.3DO> <x> <y> <z> <steps> <out.bin>\n");
        return 2;
    }
    const auto d = omk::DataFs::readPath(argv[1]);
    // `--render` probes the RENDER soup, which is what tools/sim's stage-5
    // walker uses; the default is the walkable collision soup, which is what
    // the engine probes. Both are run so the difference is visible rather
    // than chosen.
    auto kind = omk::SoupKind::Walkable;
    for (int i = 7; i < argc; ++i)
        if (std::strcmp(argv[i], "--render") == 0) kind = omk::SoupKind::Render;
    const auto soup = omk::collisionSoup(d, kind);

    // start on the floor under the authored position, not at it: probing from
    // far below finds the nearest surface below THAT, which is the ceiling -
    // the reference's first version started the walker on the roof.
    double sy = std::atof(argv[3]);
    if (const auto g = omk::floorUnder(soup, std::atof(argv[2]), sy - 1.0,
                                       std::atof(argv[4])))
        sy = *g;

    omk::Walker w(soup, std::atof(argv[2]), sy, std::atof(argv[4]));
    const int steps = std::atoi(argv[5]);

    int moved = 0, reverted = 0, blocked = 0, refused = 0, fell = 0, slid = 0;
    double ymin = w.pos()[1], ymax = w.pos()[1];
    double worstFall = 0.0;
    // an outward spiral: covers the room without needing a route
    // the reference's own spiral: 2*pi*i/64 with a 25-unit stride, slow
    // enough that the walk stays inside the room
    for (int i = 0; i < steps; ++i) {
        const double a = 2.0 * 3.14159265358979323846 * i / 64.0;
        const double r = 25.0;
        switch (w.step(r * std::cos(a), r * std::sin(a))) {
            case omk::StepResult::Moved:    ++moved; break;
            case omk::StepResult::Reverted: ++reverted; break;
            case omk::StepResult::Blocked:  ++blocked; break;
            case omk::StepResult::Refused:  ++refused; break;
            case omk::StepResult::Fell:     ++fell; break;
            case omk::StepResult::Slid:     ++slid; break;
        }
        // the vertical half: the controller owes the walker one of these a
        // frame, so a spiral that never ticks would leave a fall hanging
        w.tick(1.0);
        ymin = std::min(ymin, w.pos()[1]);
        ymax = std::max(ymax, w.pos()[1]);
        worstFall = std::max(worstFall, w.fall());
    }
    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    put32(static_cast<std::int32_t>(soup.size() / 9));
    put32(moved); put32(reverted); put32(blocked); put32(refused);
    put32(static_cast<std::int32_t>(std::lround((ymax - ymin) * 100)));
    put32(static_cast<std::int32_t>(std::lround(worstFall * 100)));
    if (!omk::safeOutputPath(argv[6])) return 2;
    std::ofstream f(argv[6], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));
    std::printf("%zu walkable triangles; moved %d, reverted %d, blocked %d, "
                "refused %d, fell %d, slid %d; height span %.2f, "
                "worst fall %.2f\n",
                soup.size() / 9, moved, reverted, blocked, refused, fell, slid,
                ymax - ymin, worstFall);
    return 0;
}
