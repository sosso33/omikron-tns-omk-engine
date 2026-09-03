// SPDX-License-Identifier: GPL-3.0-or-later
// The LEDGE census: where the ported walker stands next to a drop it will not
// take.
//
//     stuck_probe <model.3DO> [--max N] [--stride U] [--out out.bin]
//
// A play report says the player gets stuck on a bench and cannot move. The
// rule behind it is in `Walker::step`: a destination whose floor is more than
// `kStepDown` below the feet returns `StepResult::Refused` and the walker does
// not move, and nothing in the port ever sets `ignoreLedges`. There is no
// other way down - the walker has no vertical velocity and no airborne state -
// so every raised surface in the game is a surface the player can never leave
// on foot.
//
// This measures the population of that rule. For each standing spot it looks
// in 16 compass directions at one stride and asks two questions:
//
//   * is there a drop there at all - a floor more than `kStepDown` below?
//     That spot is ON A LEDGE.
//   * can the walker get down it - does any direction return `Moved` and end
//     up on that lower floor? In the port as it stands the answer is no, by
//     construction, and the number this prints is the number of places that
//     is true.
//
// The engine is the calibration, not this repo: `Walk_GroundResponse`
// (0x00465460) has an AIRBORNE branch. When the ground is still below the feet
// after the frame's gravity, a drop under 7.874 units is snapped and anything
// larger sets the fall byte at +1304 and enters ACTOR_STATE 18, tiered at
// 59.055 / 118.11 / 196.85 (`dword_910350`, and the two constants the port
// already carries as kFallHurt / kFallKill). A drop is a FALL there, never a
// refusal, so a port carrying that branch descends every ledge this counts.
#include "actor/walk.h"
#include "platform/datafs.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

namespace {
constexpr double kPi = 3.14159265358979323846;
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: stuck_probe <model.3DO> [--max N] "
                             "[--stride U] [--out out.bin]\n");
        return 2;
    }
    int         maxSpots = 4000;
    double      stride   = 14.0;   // a running frame: the widest step asked for
    const char* out      = nullptr;
    // `--no-tick` never calls `Walker::tick`, so the fall has to resolve
    // through `step`'s own frame delta alone. That is the state of a caller
    // which has not been taught the vertical half yet - `PlayerController` as
    // it stands - and the two runs agreeing is what says the fallback works.
    bool noTick = false;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--no-tick") == 0) noTick = true;
        if (std::strcmp(argv[i], "--max") == 0 && i + 1 < argc)
            maxSpots = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--stride") == 0 && i + 1 < argc)
            stride = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc)
            out = argv[++i];
    }

    const auto data = omk::DataFs::readPath(argv[1]);
    if (data.empty()) {
        std::fprintf(stderr, "stuck_probe: cannot read %s\n", argv[1]);
        return 2;
    }
    const auto soup  = omk::collisionSoup(data, omk::SoupKind::Walkable);
    const auto steep = omk::collisionSoup(data, omk::SoupKind::Steep);
    const std::size_t tris = soup.size() / 9;
    if (tris == 0) {
        std::fprintf(stderr, "stuck_probe: %s has no walkable faces\n", argv[1]);
        return 2;
    }
    const std::size_t step =
        tris > static_cast<std::size_t>(maxSpots)
            ? (tris + static_cast<std::size_t>(maxSpots) - 1) /
                  static_cast<std::size_t>(maxSpots)
            : 1;

    int    spots = 0;        // standing spots tested
    int    onLedge = 0;      // ... with a drop past the step limit beside them
    int    descended = 0;    // ... from which some direction actually goes down
    int    stranded = 0;     // ... on a ledge AND with no direction that moves
    double worstDrop = 0.0;
    // The drop histogram, in the engine's own fall tiers.
    int tierStep = 0, tierShort = 0, tierHurt = 0, tierKill = 0;

    for (std::size_t t = 0; t < tris; t += step) {
        const std::size_t b = t * 9;
        const double cx = (soup[b + 0] + soup[b + 3] + soup[b + 6]) / 3.0;
        const double cy = (soup[b + 1] + soup[b + 4] + soup[b + 7]) / 3.0;
        const double cz = (soup[b + 2] + soup[b + 5] + soup[b + 8]) / 3.0;
        // Stand ON this face: probe from just above it, so the surface found
        // is this one and not whatever lies under it.
        const auto g = omk::floorUnder(soup, cx, cy - 2.0, cz);
        if (!g) continue;
        ++spots;

        bool ledge = false, wentDown = false, movedAnywhere = false;
        double drop = 0.0;
        for (int d = 0; d < 16; ++d) {
            const double a  = 2.0 * kPi * d / 16.0;
            const double dx = stride * std::cos(a);
            const double dz = stride * std::sin(a);
            // What is under the destination, measured from the FEET rather
            // than through the step window, so a real drop is visible however
            // deep it is.
            const auto below = omk::floorUnder(soup, cx + dx, *g, cz + dz);
            const bool isDrop = below && (*below - *g) > omk::kStepDown;
            if (isDrop) {
                ledge = true;
                drop = std::max(drop, *below - *g);
            }
            omk::Walker w(soup, cx, *g, cz);
            w.setSteep(&steep);
            auto r = w.step(dx, dz);
            // A fall is not resolved by the step - the controller owes the
            // walker a tick a frame, and this is that loop. 300 frames is ten
            // seconds, past terminal velocity and past any drop in the game.
            for (int f = 0; f < 300 && (w.airborne() || w.sliding()); ++f)
                r = noTick ? w.step(0.0, 0.0) : w.tick(1.0);
            if (r == omk::StepResult::Moved) {
                movedAnywhere = true;
                if (w.pos()[1] - *g > omk::kStepDown) wentDown = true;
            }
        }
        if (!ledge) continue;
        ++onLedge;
        if (wentDown) ++descended;
        if (!movedAnywhere) ++stranded;
        worstDrop = std::max(worstDrop, drop);
        if (drop < 7.8740158)          ++tierStep;
        else if (drop < 59.055118)     ++tierShort;
        else if (drop < 118.11024)     ++tierHurt;
        else                           ++tierKill;
    }

    std::printf("%s: %zu walkable triangles (%zu steep), %d standing spots, "
                "stride %.1f\n",
                argv[1], tris, steep.size() / 9, spots, stride);
    std::printf("  on a ledge  %d  (%.1f%% of spots have a drop past the "
                "%.2f-unit step limit beside them)\n",
                onLedge, spots ? 100.0 * onLedge / spots : 0.0, omk::kStepDown);
    std::printf("  DESCENDED   %d  (a direction the walker actually takes "
                "down)\n", descended);
    std::printf("  stranded    %d  (on a ledge and no direction moves at all)\n",
                stranded);
    std::printf("  worst drop  %.1f units (%.0f cm)\n", worstDrop,
                worstDrop * 2.54);
    std::printf("  engine tiers of the drop beside them: snap(<7.87) %d, "
                "fall(<59.06) %d, hurt(<118.11) %d, kill(>=118.11) %d\n",
                tierStep, tierShort, tierHurt, tierKill);

    if (out) {
        std::vector<std::uint8_t> o;
        const auto put32 = [&o](std::int32_t v) {
            const auto u = static_cast<std::uint32_t>(v);
            for (int k = 0; k < 4; ++k)
                o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
        };
        put32(static_cast<std::int32_t>(tris));
        put32(spots);
        put32(onLedge);
        put32(descended);
        put32(stranded);
        put32(static_cast<std::int32_t>(std::lround(worstDrop * 100)));
        if (!omk::safeOutputPath(out)) return 2;
        std::ofstream f(out, std::ios::binary);
        f.write(reinterpret_cast<const char*>(o.data()),
                static_cast<std::streamsize>(o.size()));
    }
    return 0;
}
