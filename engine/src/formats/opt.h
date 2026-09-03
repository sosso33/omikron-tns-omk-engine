// SPDX-License-Identifier: GPL-3.0-or-later
// TRAJECTOIRES\<stem>.OPT - a city's traffic circuit: the lanes the procedural
// pedestrians and the hover-taxis move on (docs/STREET_LIFE.md 2,
// tools/opt_track.py - the two readers are kept identical, and `verify.py:
// opt tracks` / `engine: pedestrians` hold them to the same numbers).
//
// `Slider_Init` (0x00453450) slurps the file and relocates seven offsets in
// the 19-dword header into pointers; everything after is read through them:
//
//     [0]  "V1.0"     [1] first pedestrian lane   [2] end of them
//     [3]  pedestrian spacing unit    [4] vehicle spacing unit
//     [5]  lane count - the vehicle lanes are [2]..[5]
//     [6]  lanes offset            (count [5])  24 bytes
//     [7]/[8]   keys count/offset               20 bytes
//     [9]/[10]  action points                   20 bytes
//     [11]/[12] routes                          12 bytes
//     [13]/[14] steps                           16 bytes
//     [15]/[16] reservation groups               4 bytes
//     [17]/[18] group lists                      2 bytes
//     [19] a stamp
//
// Each block starts where the previous ends and the last ends on the file
// size - the walk `loadOpt` refuses anything else. The runtime list heads
// (lane +12, key +0, route +0) and the group busy byte (+3) are zero on disk
// and are NOT carried here: the pool keeps its own lists.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace omk {

struct OptLane {
    float        origin[3] = {0, 0, 0};   // +0
    std::int16_t firstKey = 0;            // +16
    std::int16_t firstRoute = 0;          // +18
    std::int8_t  routeCount = 0;          // +20, 0 reads as 1
    std::int8_t  keyCount = 0;            // +21
};
struct OptKey {
    float        delta[3] = {0, 0, 0};    // +4, to the next key
    std::int16_t action = -1;             // +16, an action point, -1 none
};
struct OptAction {
    float        point[3] = {0, 0, 0};    // +0, relative to the mover reaching the key
    float        facing = 0.0f;           // +12, degrees, turned to on arrival
    std::int16_t clip = 0;                // +16, the clip's `slot` id in the sex's .ani group; 0 = none
    std::int8_t  count = 0;               // +18, how many times the main clip loops
    std::int8_t  one = 0;                 // +19, 1 in every shipped record
};
struct OptRoute {
    std::int16_t dest = 0;                // +4, the destination lane
    std::int16_t firstStep = 0;           // +6
    std::int16_t group = -1;              // +8, a reservation group guarding the entry, -1 none
    std::int8_t  stepCount = 0;           // +10
};
struct OptStep {
    float        delta[3] = {0, 0, 0};    // +0
    std::int16_t group = -1;              // +12
};
struct OptGroup {
    std::int16_t first = 0;               // +0, into `lists`
    std::int8_t  count = 0;               // +2
};

struct OptTrack {
    bool         valid = false;
    std::string  error;                   // why not, when !valid
    std::uint32_t pedFirst = 0, pedEnd = 0, laneCount = 0;
    std::uint32_t pedSpacing = 0, vehSpacing = 0;
    std::uint32_t stamp = 0;
    std::vector<OptLane>   lanes;
    std::vector<OptKey>    keys;
    std::vector<OptAction> actions;
    std::vector<OptRoute>  routes;
    std::vector<OptStep>   steps;
    std::vector<OptGroup>  groups;
    std::vector<std::int16_t> lists;

    bool isPedestrianLane(int lane) const {
        return lane >= static_cast<int>(pedFirst) && lane < static_cast<int>(pedEnd);
    }
    // The lane's origin plus every key delta - where a mover leaves it.
    void laneEnd(int lane, float out[3]) const;
};

// The whole walk, with the self-checks tools/opt_track.py makes: the layout,
// every reference, the runtime fields zero, no route crossing between the
// pedestrian and the vehicle lanes. `valid` false and `error` set otherwise.
OptTrack loadOpt(std::span<const std::byte> d);

}  // namespace omk
