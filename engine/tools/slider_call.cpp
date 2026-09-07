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
            // ...and where `sub_452CC0` puts the slider: the chosen lane's
            // ORIGIN, set back 39 units along its own direction in x and z,
            // with the node 30.75 under it - the ride's own hover height.
            const auto c = omk::planSliderCall(t, addrs.front().pos, 1);
            std::printf("call area %d lane %d route %d at %.0f %.0f %.0f "
                        "dir %.2f %.2f node %.0f\n", area, c.at.lane, c.route,
                        c.place[0], c.place[1], c.place[2],
                        c.dir[0], c.dir[2], c.nodeY);
        }
    }

    // ---- THE RIDE STATE MACHINE, driven ----------------------------
    //
    // `sub_456530`'s switch, walked the way a call actually goes: COMING
    // until the slider is inside 117 units of the pickup point, then the
    // camera hands back to the player at mode 0 with the fade and the hold
    // released, and the slot idles for 600 before giving up. Then the
    // FETCHING arm, which ends at mode 10 and state 4 instead; and LEAVING,
    // which needs the player both 300 units clear AND in front of it.
    {
        omk::RideMachine m;
        m.state = 2;
        int frames = 0;
        float d = 2000.0f;
        for (; frames < 400 && m.state == 2; ++frames) {
            m.tick(1.0f, d, 0.0f, false);
            d -= 20.0f;                       // it drives in at 20 a frame
        }
        std::printf("coming %d frames -> state %d camera %d fade %d hold %d\n",
                    frames, m.state, m.camera, m.fadeIn ? 1 : 0,
                    m.released ? 1 : 0);
        int idle = 0;
        for (; idle < 1000 && m.state == 1; ++idle) m.tick(1.0f, 0.0f, 0.0f, false);
        std::printf("idle %d frames -> state %d\n", idle, m.state);
    }
    {
        omk::RideMachine m;
        m.state = 6;
        m.tick(1.0f, 200.0f, 0.0f, false);
        const int away = m.camera;
        m.tick(1.0f, 100.0f, 0.0f, false);
        std::printf("fetching away %d arrived state %d camera %d\n",
                    away, m.state, m.camera);
    }
    {
        omk::RideMachine m;
        m.state = 7;
        m.tick(1.0f, 0.0f, 400.0f, false);        // far, but behind it
        const int behind = m.state;
        m.tick(1.0f, 0.0f, 200.0f, true);         // ahead, but too close
        const int close = m.state;
        m.tick(1.0f, 0.0f, 400.0f, true);         // clear and ahead
        std::printf("leaving behind %d close %d clear %d latch %.0f\n",
                    behind, close, m.state, omk::RideMachine::kLatchFrames);
    }
    return 0;
}
