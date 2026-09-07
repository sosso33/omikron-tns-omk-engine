// SPDX-License-Identifier: GPL-3.0-or-later
// CAN THE WALKER CLIMB THE GAME'S STAIRCASES? Every set that ships one, tried.
//
//     stairs_probe <gamedata> [set.3DO ...]
//
// A reader reported being stuck on the last step of the bank's stairs
// (`todo/next-tasks.md` 20). The bank's own staircase turned out to climb, so
// the question is which staircase does not - and guessing which building was
// meant is exactly the wrong way to answer it. Thirteen shipped decor sets
// carry a mesh whose name says `escalier`; this walks all of them.
//
// The engine has no stair rule. `Walk_ProbeGround` casts from a step-height
// ABOVE the feet, so a rise the actor may climb is inside the probe's own
// window, and the two refusals are the step limit (30 cm, `dword_910340`) and
// the 30-degree slope. A staircase is climbable when every riser clears the
// first and every tread clears the second - nothing else is involved.
//
// For each staircase mesh: take its flat tread levels, place the walker on the
// lowest one, and step toward the highest along the run's own axis. Report the
// rise per step, the height reached, and the walker's own verdict on the step
// that stopped it - a step-up refusal, a slope refusal and a wall all look
// identical from outside and have three different fixes.
#include "actor/walk.h"
#include "o3de/collision.h"
#include "formats/mesh3do.h"
#include "platform/datafs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {

struct Tread {                       // one flat run of a staircase mesh
    double y = 0;
    double xlo = 1e30, xhi = -1e30, zlo = 1e30, zhi = -1e30;
    int tris = 0;
};

// The flat levels of one mesh, in the order the walker would meet them going
// up: y is DOWN in this engine's world, so "up" is decreasing y.
std::vector<Tread> treadsOf(const std::vector<float>& tri) {
    std::map<long, Tread> by;        // keyed on y to a tenth
    for (std::size_t i = 0; i + 8 < tri.size(); i += 9) {
        const double y0 = tri[i + 1], y1 = tri[i + 4], y2 = tri[i + 7];
        if (std::fabs(y0 - y1) > 0.5 || std::fabs(y0 - y2) > 0.5) continue;
        Tread& t = by[std::lround(y0 * 10.0)];
        t.y = y0; ++t.tris;
        for (int k = 0; k < 3; ++k) {
            t.xlo = std::min(t.xlo, static_cast<double>(tri[i + 3 * k]));
            t.xhi = std::max(t.xhi, static_cast<double>(tri[i + 3 * k]));
            t.zlo = std::min(t.zlo, static_cast<double>(tri[i + 3 * k + 2]));
            t.zhi = std::max(t.zhi, static_cast<double>(tri[i + 3 * k + 2]));
        }
    }
    std::vector<Tread> out;
    for (auto& [k, t] : by) out.push_back(t);
    std::sort(out.begin(), out.end(), [](const Tread& a, const Tread& b) {
        return a.y > b.y;            // lowest (least negative) first
    });
    return out;
}

const char* verdict(omk::StepResult r) {
    switch (r) {
    case omk::StepResult::Moved:    return "moved";
    case omk::StepResult::Reverted: return "no floor there";
    case omk::StepResult::Blocked:  return "BLOCKED - a rise past the step limit";
    case omk::StepResult::Fell:     return "fell";
    case omk::StepResult::Slid:     return "SLID - a face past the slope limit";
    case omk::StepResult::Refused:  return "refused";
    }
    return "?";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: stairs_probe <gamedata> [set.3DO ...]\n");
        return 2;
    }
    const std::string fr = argv[1];
    std::vector<std::string> sets;
    for (int i = 2; i < argc; ++i) sets.push_back(argv[i]);
    if (sets.empty())
        sets = {"Abank", "ACSgrot", "ACSgrotl", "Anekbah", "AToit", "L_Khonsu",
                "Lahoreh", "LMFinkar", "LMonaste", "LMprinci", "Sconcert",
                "Sprison", "Sttra01"};

    int runs = 0, climbed = 0, failed = 0;
    for (const auto& set : sets) {
        const auto data = omk::DataFs::readPath(fr + "/MESHES/DECORS/" + set + ".3DO");
        if (data.empty()) { std::printf("%-10s  no such set\n", set.c_str()); continue; }
        const auto hdr = omk::readHeader(data);
        if (!hdr) { std::printf("%-10s  unreadable\n", set.c_str()); continue; }
        const auto meshes = omk::readMeshes(data, *hdr);
        std::vector<int> meshOf;     // which mesh each triangle came from
        omk::TriangleSoup whole = omk::collisionSoup(data, omk::SoupKind::Walkable, &meshOf);

        for (std::size_t m = 0; m < meshes.size(); ++m) {
            std::string n = meshes[m].name;
            std::string low = n;
            for (auto& c : low) c = static_cast<char>(std::tolower(c));
            if (low.find("escal") == std::string::npos) continue;

            // that mesh's own walkable triangles, out of the set's soup
            std::vector<float> mine;
            for (std::size_t t = 0; t * 9 + 8 < whole.size(); ++t)
                if (t < meshOf.size() && meshOf[t] == static_cast<int>(m))
                    mine.insert(mine.end(), whole.begin() + static_cast<long>(t * 9),
                                whole.begin() + static_cast<long>(t * 9 + 9));
            const auto tr = treadsOf(mine);
            if (tr.size() < 2) {
                std::printf("%-10s %-16s  %zu tread level%s - not a run\n",
                            set.c_str(), n.c_str(), tr.size(), tr.size() == 1 ? "" : "s");
                continue;
            }
            ++runs;
            // EACH RISER ON ITS OWN. Guessing one axis for a whole flight is
            // how the first version of this probe reported failures that were
            // only its own placement: a run can turn, and a bbox centre is a
            // better aim than an axis. So walk tread n to tread n+1, from the
            // centre of one toward the centre of the next, and report the
            // riser that stops him rather than the flight.
            int ok = 0, bad = 0;
            double worstRise = 0;
            std::string firstFail;
            for (std::size_t k = 0; k + 1 < tr.size(); ++k) {
                const Tread& lo = tr[k];
                const Tread& hi = tr[k + 1];
                const double rise = lo.y - hi.y;          // y is down: up is less
                // A RISER, and not an artefact of grouping by height. Under a
                // unit is one surface the level split in two (Anekbah's top
                // platform is 5.9, 5.0 and 4.9); over twenty is a distant
                // LANDING the same mesh happens to carry, not a step. Both
                // produced "failures" in earlier versions of this probe that
                // were about the probe.
                if (rise < 1.0 || rise > 20.0) continue;
                worstRise = std::max(worstRise, rise);
                const double lx = (lo.xlo + lo.xhi) / 2, lz = (lo.zlo + lo.zhi) / 2;
                // A FAN, not an aim. The first two versions of this probe
                // walked bbox centre to bbox centre and reported failures that
                // were only their own direction - these treads are diagonal
                // parallelograms and a centre-to-centre line need not cross
                // the riser at all. The question is whether the RULE lets the
                // step be climbed, so try sixteen headings and let any of them
                // answer it.
                bool up = false;
                omk::StepResult last = omk::StepResult::Moved;
                for (int a = 0; a < 16 && !up; ++a) {
                    const double th = a * 2.0 * 3.14159265358979 / 16.0;
                    const double dx = std::cos(th), dz = std::sin(th);
                    omk::Walker w(whole, lx, lo.y, lz);
                    for (int i = 0; i < 60; ++i) {
                        const auto r = w.step(dx * 2.0, dz * 2.0);
                        if (r == omk::StepResult::Blocked || r == omk::StepResult::Slid) {
                            last = r; break;
                        }
                        if (w.pos()[1] <= hi.y + 0.5) { up = true; break; }
                    }
                }
                if (up) ++ok;
                else {
                    ++bad;
                    if (firstFail.empty()) {
                        char buf[160];
                        std::snprintf(buf, sizeof buf,
                                      "riser %zu (%.1f -> %.1f, rise %.2f): %s",
                                      k, lo.y, hi.y, rise, verdict(last));
                        firstFail = buf;
                    }
                }
            }
            if (bad == 0) ++climbed; else ++failed;
            std::printf("%-10s %-16s %2zu treads  %6.1f -> %6.1f  worst rise %5.2f  "
                        "risers %d/%d  %s\n",
                        set.c_str(), n.c_str(), tr.size(), tr.front().y, tr.back().y,
                        worstRise, ok, ok + bad,
                        bad == 0 ? "all climb" : firstFail.c_str());
        }
    }
    std::printf("\n%d staircase runs: %d climbed, %d not\n", runs, climbed, failed);
    return 0;
}
