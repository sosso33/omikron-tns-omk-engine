// SPDX-License-Identifier: GPL-3.0-or-later
// One directed walk across a set's collision soup, step by step, with the
// walker's own verdict on each - for a place where a walker stops and the
// reason has to be told apart from a wall, a ledge and a hole.
//
//     step_probe <model.3DO> <x> <y> <z> <dx> <dz> <steps> [radius]
//
// With a radius, the STEEP soup is set as blockers and the capsule sweep runs
// - which is the live player's path and not the bare walker's.
#include "actor/walk.h"
#include "o3de/collision.h"
#include "platform/datafs.h"
#include <cstdio>
#include <cstdlib>
#include <string>
namespace {
const char* verdict(omk::StepResult r) {
    switch (r) {
    case omk::StepResult::Moved:    return "moved";
    case omk::StepResult::Reverted: return "REVERTED - no floor at the destination";
    case omk::StepResult::Blocked:  return "BLOCKED - a rise past the step limit";
    case omk::StepResult::Fell:     return "fell";
    case omk::StepResult::Slid:     return "SLID - past the slope limit";
    case omk::StepResult::Refused:  return "refused";
    }
    return "?";
}
}
int main(int argc, char** argv) {
    if (argc < 8) {
        std::fprintf(stderr, "usage: step_probe <model.3DO> <x> <y> <z> <dx> <dz> <steps>\n");
        return 2;
    }
    const auto d = omk::DataFs::readPath(argv[1]);
    if (d.empty()) { std::fprintf(stderr, "no such model\n"); return 1; }
    const omk::TriangleSoup soup = omk::collisionSoup(d, omk::SoupKind::Walkable);
    double x = std::atof(argv[2]), y = std::atof(argv[3]), z = std::atof(argv[4]);
    const double dx = std::atof(argv[5]), dz = std::atof(argv[6]);
    const int n = std::atoi(argv[7]);
    omk::TriangleSoup steep = omk::collisionSoup(d, omk::SoupKind::Steep);
    omk::Walker w(soup, x, y, z);
    const double radius = argc > 8 ? std::atof(argv[8]) : 0.0;
    if (radius > 0.0) {
        // HO1_FN's four spheres hang off the FEET, so their centres are above
        // him; the lowest is one radius up, which is what a bare `setBlockers`
        // assumes when it is given no centres.
        w.setBlockers(&steep, radius);
        std::printf("(capsule on: radius %.1f against %zu steep triangles)\n",
                    radius, steep.size() / 9);
    }
    std::printf("start %.1f %.1f %.1f, stepping %.1f %.1f\n", x, y, z, dx, dz);
    double lastY = y;
    for (int i = 0; i < n; ++i) {
        const auto r = w.step(dx, dz);
        const double* p = w.pos();
        const bool interesting = r != omk::StepResult::Moved ||
                                 std::abs(p[1] - lastY) > 0.05 || i < 3 || i == n - 1;
        if (interesting)
            std::printf("  %3d  %8.2f %8.2f %8.2f   rise %6.2f   %s\n",
                        i, p[0], p[1], p[2], lastY - p[1], verdict(r));
        lastY = p[1];
        if (r == omk::StepResult::Blocked || r == omk::StepResult::Reverted ||
            r == omk::StepResult::Slid) break;
    }
    return 0;
}
