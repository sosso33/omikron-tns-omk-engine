// SPDX-License-Identifier: GPL-3.0-or-later
// THE NARROW PHASE, held to the simulator's number.
//
//     wall_probe <gamedata> <set> [--out out.bin]
//
// tools/sim/actor.py `wall_test`: find the biggest wall in the set that has
// FLOOR ON BOTH SIDES (most walls have none behind them, so the ground probe
// alone refuses them and a test there passes with no sweep at all), stand 100
// units in front of it, and walk 40 steps of 6 straight at it - once with the
// sweep off and once with it on, radius 12, ledges ignored. The measurement is
// the signed distance from the wall's plane at the end: negative is INSIDE the
// room the actor should not be in. `verify.py: engine: narrow phase` asserts
// the port lands on the same two numbers the sim does (-140.0 moved,
// 16.6 blocked on ARESTO14), which is the agreement test the kernel's own
// banner asks for in place of a transcription.
#include "actor/walk.h"
#include "o3de/collision.h"
#include "platform/datafs.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

namespace {
constexpr double kRadius = 12.0;      // the sim's RADIUS stand-in

bool triNormal(const float* a, const float* b, const float* c, double n[3]) {
    const double ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
    const double vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
    n[0] = uy * vz - uz * vy; n[1] = uz * vx - ux * vz; n[2] = ux * vy - uy * vx;
    const double L = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (L <= 0.0) return false;
    n[0] /= L; n[1] /= L; n[2] /= L;
    return true;
}

int verdictCode(omk::StepResult r) { return static_cast<int>(r); }
const char* verdictName(omk::StepResult r) {
    switch (r) {
        case omk::StepResult::Moved: return "moved";
        case omk::StepResult::Reverted: return "reverted";
        case omk::StepResult::Blocked: return "blocked";
        case omk::StepResult::Fell: return "fell";
        case omk::StepResult::Slid: return "slid";
        case omk::StepResult::Refused: return "refused";
    }
    return "?";
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: wall_probe <gamedata> <set> [--out out.bin]\n"); return 2; }
    const char* out = nullptr;
    // `--from x,y,z --dir dx,dz [--steps N]`: walk from a point instead of
    // the found partition - for a REPORTED spot (HANDOFF: AHALL27, east
    // past x 4840 leaves the geometry). Prints where each run ends.
    bool haveFrom = false; double from[3] = {0, 0, 0}, dir[2] = {1, 0}; int steps = 40;
    for (int i = 3; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!std::strcmp(argv[i], "--from") && i + 1 < argc)
            haveFrom = std::sscanf(argv[++i], "%lf,%lf,%lf", &from[0], &from[1], &from[2]) == 3;
        else if (!std::strcmp(argv[i], "--dir") && i + 1 < argc)
            std::sscanf(argv[++i], "%lf,%lf", &dir[0], &dir[1]);
        else if (!std::strcmp(argv[i], "--steps") && i + 1 < argc) steps = std::atoi(argv[++i]);
    }
    const omk::DataFs fs(argv[1]);
    const auto path = fs.resolve(std::string("MESHES/DECORS/") + argv[2] + ".3DO");
    if (!path) { std::fprintf(stderr, "no set %s\n", argv[2]); return 1; }
    const auto d = omk::DataFs::readPath(*path);
    const auto floor = omk::collisionSoup(d, omk::SoupKind::Walkable);
    const auto walls = omk::collisionSoup(d, omk::SoupKind::Steep);

    if (haveFrom) {
        const double L = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1]);
        if (L > 0) { dir[0] /= L; dir[1] /= L; }
        const auto g = omk::floorUnder(floor, from[0], from[1] - 2.0, from[2]);
        if (!g) { std::printf("no floor under %.0f %.0f %.0f\n", from[0], from[1], from[2]); return 1; }
        for (int mode = 0; mode < 2; ++mode) {
            omk::Walker w(floor, from[0], *g, from[2]);
            w.setSteep(&walls);
            w.setBlockers(&walls, mode ? kRadius : 0.0);
            omk::StepResult r = omk::StepResult::Moved;
            int blocked = 0, firstBlock = -1;
            for (int st = 0; st < steps; ++st) {
                r = w.step(dir[0] * 6.0, dir[1] * 6.0);
                for (int f = 0; f < 300 && (w.airborne() || w.sliding()); ++f) w.tick(1.0);
                if (r == omk::StepResult::Blocked) { ++blocked; if (firstBlock < 0) firstBlock = st; }
            }
            std::printf("sweep %s: from %.0f %.0f %.0f heading %+.2f,%+.2f x %d steps of 6 -> ends %.1f %.1f %.1f, "
                        "last verdict %s, %d blocked (first at step %d)\n",
                        mode ? "ON " : "OFF", from[0], *g, from[2], dir[0], dir[1], steps,
                        w.pos()[0], w.pos()[1], w.pos()[2], verdictName(r), blocked, firstBlock);
        }
        return 0;
    }
    // find_partition: the biggest vertical face with floor on both sides
    // whose y range contains the sphere's centre
    bool found = false; double bestArea = 0.0, cen[3] = {0, 0, 0}, nrm[3] = {0, 0, 0}, fl = 0.0;
    for (std::size_t i = 0; i + 9 <= walls.size(); i += 9) {
        const float* a = &walls[i]; const float* b = &walls[i + 3]; const float* c = &walls[i + 6];
        double n[3];
        if (!triNormal(a, b, c, n) || std::fabs(n[1]) > 0.2) continue;
        const double ce[3] = {(a[0] + b[0] + c[0]) / 3.0, (a[1] + b[1] + c[1]) / 3.0,
                              (a[2] + b[2] + c[2]) / 3.0};
        const auto f1 = omk::floorUnder(floor, ce[0] + n[0] * 50, ce[1] - 60, ce[2] + n[2] * 50);
        const auto f2 = omk::floorUnder(floor, ce[0] - n[0] * 50, ce[1] - 60, ce[2] - n[2] * 50);
        if (!f1 || !f2 || std::fabs(*f1 - *f2) > 12.0) continue;
        const double ymin = std::min({a[1], b[1], c[1]}), ymax = std::max({a[1], b[1], c[1]});
        if (!(ymin <= *f1 - kRadius && *f1 - kRadius <= ymax)) continue;
        const double ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
        const double vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
        const double cr[3] = {uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx};
        const double area = 0.5 * std::sqrt(cr[0] * cr[0] + cr[1] * cr[1] + cr[2] * cr[2]);
        if (area < 10000.0) continue;
        if (!found || area > bestArea) {
            found = true; bestArea = area;
            for (int k = 0; k < 3; ++k) { cen[k] = ce[k]; nrm[k] = n[k]; }
            fl = *f1;
        }
    }
    if (!found) { std::printf("no partition with floor on both sides in %s\n", argv[2]); }

    double dist[2] = {0, 0}; int verdict[2] = {0, 0};
    if (found) {
        std::printf("partition: centroid %.1f %.1f %.1f normal %.3f %.3f %.3f area %.0f floor %.1f\n",
                    cen[0], cen[1], cen[2], nrm[0], nrm[1], nrm[2], bestArea, fl);
        for (int mode = 0; mode < 2; ++mode) {
            omk::Walker w(floor, cen[0] + nrm[0] * 100.0, fl, cen[2] + nrm[2] * 100.0);
            w.ignoreLedges = true;
            w.setBlockers(&walls, mode ? kRadius : 0.0);
            omk::StepResult r = omk::StepResult::Moved;
            for (int s = 0; s < 40; ++s) r = w.step(-nrm[0] * 6.0, -nrm[2] * 6.0);
            dist[mode] = nrm[0] * (w.pos()[0] - cen[0]) + nrm[2] * (w.pos()[2] - cen[2]);
            verdict[mode] = verdictCode(r);
            std::printf("sweep %s: ends %+.1f from the wall's plane, last verdict %s\n",
                        mode ? "ON " : "OFF", dist[mode], verdictName(r));
        }
    }
    if (out) {
        if (!omk::safeOutputPath(out)) return 2;
        std::vector<std::uint8_t> o;
        const auto put32 = [&o](std::int32_t v) {
            const auto u = static_cast<std::uint32_t>(v);
            for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
        };
        put32(found ? 1 : 0);
        put32(static_cast<std::int32_t>(std::lround(dist[0] * 10.0)));
        put32(verdict[0]);
        put32(static_cast<std::int32_t>(std::lround(dist[1] * 10.0)));
        put32(verdict[1]);
        std::ofstream f(out, std::ios::binary);
        f.write(reinterpret_cast<const char*>(o.data()), static_cast<std::streamsize>(o.size()));
    }
    return 0;
}
