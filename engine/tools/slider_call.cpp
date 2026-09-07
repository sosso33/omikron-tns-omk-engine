// SPDX-License-Identifier: GPL-3.0-or-later
// WHERE A CALLED SLIDER COMES TO - `sub_452570`'s lane search.
//
//     slider_call <gamedata>
//
// The sneak's slider page hands a POINT to `sub_452570` - a destination's
// address, or the player's own position - and it looks for the nearest point
// on a VEHICLE lane of the resident `.OPT` circuit. This runs that search over
// the shipped circuits, against the shipped ADDRESSES, and reports how far a
// slider would have to come.
//
// The interesting number is the DISTRIBUTION, because it is a claim about the
// authoring: a destination the game offers on the slider page ought to be near
// a road, and one that is not would mean the search is reading the wrong
// lanes. Areas 0 and 1 are the two cities with vehicle lanes.
//
// One line per fact:
//   circuit ...   the .OPT, its lanes and which of them are the vehicle ones
//   nearest ...   over that area's addresses: how many find a lane, and the
//                 median and worst distance in metres
//   route ...     the round-robin route pick off the best lane
#include "actor/sliders.h"
#include "formats/addresses.h"
#include "formats/opt.h"
#include "platform/datafs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

// IAM\AREA's own directory, the walk every reader here makes.
std::vector<std::byte> areaChunk(const omk::DataFs& fs, int idx) {
    const auto d = fs.read("IAM/AREA");
    std::vector<std::byte> out;
    if (d.size() < 8) return out;
    std::size_t first = d.size();
    for (std::size_t i = 0; i * 8 + 8 <= d.size() && i * 8 < first; ++i) {
        std::uint32_t off, size;
        std::memcpy(&off, d.data() + i * 8, 4);
        std::memcpy(&size, d.data() + i * 8 + 4, 4);
        if (!off || !size || off + size > d.size()) continue;
        if (off < first) first = off;
        if (static_cast<int>(i) == idx)
            out.assign(d.begin() + off, d.begin() + off + size);
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: slider_call <gamedata>\n"); return 2; }
    const omk::DataFs fs(argv[1]);

    // The four areas the sneak's destinations name, and the `.OPT` each one
    // carries at its chunk's `+115` - which is where the engine reads it, so
    // the stem is not guessed here either.
    for (const int area : {0, 1, 64, 101}) {
        const auto chunk = areaChunk(fs, area);
        if (chunk.empty()) { std::printf("circuit %d MISSING\n", area); continue; }
        // the chunk's `.OPT` stem is its own field; the harness knows the two
        char stem[10] = {0};
        if (chunk.size() > 124)
            std::memcpy(stem, chunk.data() + 115, 9);
        if (!stem[0]) { std::printf("circuit %d names no .OPT\n", area); continue; }
        const auto raw = fs.read(std::string("TRAJECTOIRES/") + stem + ".OPT");
        if (raw.empty()) { std::printf("circuit %d no .OPT\n", area); continue; }
        const auto t = omk::loadOpt(raw);
        if (!t.valid) { std::printf("circuit %d invalid: %s\n", area, t.error.c_str()); continue; }
        std::printf("circuit %d %s lanes %u ped %u..%u vehicle %u..%u\n",
                    area, stem, t.laneCount, t.pedFirst, t.pedEnd,
                    t.pedEnd, t.laneCount);

        const auto addrs = omk::readAddresses(chunk);
        std::vector<double> d;
        int found = 0;
        for (const auto& a : addrs) {
            const auto lp = omk::nearestVehicleLane(t, a.pos);
            if (!lp.found()) continue;
            ++found;
            d.push_back(lp.dist / 39.3701);          // metres
        }
        std::sort(d.begin(), d.end());
        const double med = d.empty() ? -1.0 : d[d.size() / 2];
        const double worst = d.empty() ? -1.0 : d.back();
        std::printf("nearest %d of %zu addresses, median %.1f m worst %.1f m\n",
                    found, addrs.size(), med, worst);

        if (!addrs.empty()) {
            const auto lp = omk::nearestVehicleLane(t, addrs.front().pos);
            std::printf("route area %d lane %d key %d -> %d %d %d\n", area,
                        lp.lane, lp.key,
                        omk::laneRoute(t, lp.lane, 1),
                        omk::laneRoute(t, lp.lane, 2),
                        omk::laneRoute(t, lp.lane, 3));
        }
    }
    return 0;
}
