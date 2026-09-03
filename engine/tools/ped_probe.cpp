// SPDX-License-Identifier: GPL-3.0-or-later
// ped_probe - the procedural pedestrians of a city street, spawned and run
// headlessly (docs/STREET_LIFE.md 2, actor/pedestrians.h).
//
//     ped_probe <gamedata> <tables dir> <area> [frames] [level] [--walkers]
//
// Prints `counts` - what the spawner places at every density level on the
// area's circuit, from the rule alone - then loads the area into a Session
// with `loadTraffic`, runs it, and prints `run`: how many walkers are live,
// moved, changed lane, were blocked, overtook, visited an action point or
// idle, the largest body-to-mover lag, the largest distance from any mover
// to the lane network, and any NaN. `--walkers` lists every walker.
// `verify.py: engine: pedestrians` holds these against tools/opt_track.py's
// independent count and against the invariants the walk owes. The road
// traffic shares the pool and is skipped throughout; `veh_probe` is its
// probe.
#include "actor/pedestrians.h"
#include "formats/opt.h"
#include "platform/datafs.h"
#include "script/area.h"
#include "script/gamestate.h"
#include "script/script.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

struct Seg { float a[3], b[3]; };

float pointSegment(const float p[3], const Seg& s) {
    float d[3] = {s.b[0] - s.a[0], s.b[1] - s.a[1], s.b[2] - s.a[2]};
    float w[3] = {p[0] - s.a[0], p[1] - s.a[1], p[2] - s.a[2]};
    const float dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    float t = dd > 0.0f ? (w[0] * d[0] + w[1] * d[1] + w[2] * d[2]) / dd : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    float q[3] = {s.a[0] + d[0] * t - p[0], s.a[1] + d[1] * t - p[1], s.a[2] + d[2] * t - p[2]};
    return std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
}

// every segment a mover can be on: the pedestrian lanes' keys, and each
// route's steps plus its implicit last leg to the destination's origin
std::vector<Seg> network(const omk::OptTrack& t) {
    std::vector<Seg> out;
    for (std::uint32_t li = t.pedFirst; li < t.pedEnd; ++li) {
        const auto& L = t.lanes[li];
        float p[3] = {L.origin[0], L.origin[1], L.origin[2]};
        for (int k = 0; k < L.keyCount; ++k) {
            const auto& K = t.keys[static_cast<std::size_t>(L.firstKey + k)];
            Seg s; for (int i = 0; i < 3; ++i) { s.a[i] = p[i]; p[i] += K.delta[i]; s.b[i] = p[i]; }
            out.push_back(s);
        }
        const int nr = L.routeCount > 0 ? L.routeCount : 1;
        for (int r = 0; r < nr; ++r) {
            const auto& R = t.routes[static_cast<std::size_t>(L.firstRoute + r)];
            float q[3] = {p[0], p[1], p[2]};
            for (int st = 0; st < R.stepCount; ++st) {
                const auto& S = t.steps[static_cast<std::size_t>(R.firstStep + st)];
                Seg s; for (int i = 0; i < 3; ++i) { s.a[i] = q[i]; q[i] += S.delta[i]; s.b[i] = q[i]; }
                out.push_back(s);
            }
            const auto& D = t.lanes[static_cast<std::size_t>(R.dest)];
            Seg s; for (int i = 0; i < 3; ++i) { s.a[i] = q[i]; s.b[i] = D.origin[i]; }
            out.push_back(s);
        }
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: ped_probe <gamedata> <tables dir> <area> [frames] [level] [--walkers]\n");
        return 2;
    }
    const std::string fr = argv[1], tb = argv[2];
    const int area = std::atoi(argv[3]);
    int frames = 600, level = omk::kDefaultStreetActivity;
    bool listWalkers = false;
    for (int i = 4; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--walkers") listWalkers = true;
        else if (i == 4) frames = std::atoi(argv[i]);
        else if (i == 5) level = std::atoi(argv[i]);
    }
    const auto table = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    if (!table.valid()) return 1;
    const std::string iam = fr + "/IAM";
    auto state = omk::GameState::fromFile(iam + "/START");
    omk::Session s(iam, state, table);
    s.answerUiFromPerson(true);
    s.setStreetActivity(level);
    s.loadTraffic(fr);
    s.loadArea(area);
    const auto& peds = s.pedestrians();
    const auto& slot = s.residentSlot(s.activeSlot());
    // the rule alone, per level, on the circuit the area names
    const omk::DataFs fs(fr);
    const auto track = omk::loadOpt(fs.read("TRAJECTOIRES/" + slot.opt + ".OPT"));
    std::printf("counts area %d opt %s valid %d", area, slot.opt.empty() ? "-" : slot.opt.c_str(), track.valid ? 1 : 0);
    for (int l = 0; l <= 4; ++l) std::printf(" level%d %d", l, track.valid ? omk::Pedestrians::spawnCount(track, l) : 0);
    std::printf("\n");
    const int spawned = peds.liveCount();
    std::string models;
    for (const auto& m : peds.models()) { if (!models.empty()) models += ","; models += m.name; }
    std::printf("session area %d ani %s loaded %d level %d spawned %d models %s\n", area,
                slot.ani.empty() ? "-" : slot.ani.c_str(), peds.loaded() ? 1 : 0, peds.streetActivity(),
                spawned, models.empty() ? "-" : models.c_str());
    std::vector<std::array<float, 3>> start;
    for (const auto& w : peds.walkers()) start.push_back({w.body[0], w.body[1], w.body[2]});
    for (int f = 0; f < frames; ++f) s.frame();
    const auto net = network(peds.track());
    int live = 0, moved = 0, laneChanges = 0, blocked = 0, overtakes = 0, actions = 0, idle = 0, inAction = 0, nan = 0;
    float maxLag = 0.0f, maxOff = 0.0f;
    std::size_t i = 0;
    for (const auto& w : peds.walkers()) {
        const auto& st = start[i++];
        // The pool is the engine's ONE 240-slot mover pool: the road traffic
        // shares it (actor/vehicles.cpp), and a vehicle's mover carries
        // `vehicle >= 0`. Every number below is the crowd's alone - and its
        // network is the pedestrian lanes', which a vehicle is nowhere near.
        if (!w.live || w.vehicle >= 0) continue;
        ++live;
        const float dx = w.body[0] - st[0], dz = w.body[2] - st[2];
        if (std::sqrt(dx * dx + dz * dz) > 1.0f) ++moved;
        laneChanges += w.laneChanges; blocked += w.blockedFrames; overtakes += w.overtakes; actions += w.actionsVisited;
        if (w.flags & 0x100u) ++idle;
        if (w.flags & 0x80u) ++inAction;
        bool bad = false;
        for (int k = 0; k < 3; ++k) if (std::isnan(w.body[k]) || std::isnan(w.pos[k])) bad = true;
        if (bad) { ++nan; continue; }
        const float lx = w.pos[0] - w.body[0], ly = w.pos[1] - w.body[1], lz = w.pos[2] - w.body[2];
        maxLag = std::max(maxLag, std::sqrt(lx * lx + ly * ly + lz * lz));
        float best = 1e30f;
        for (const auto& sg : net) best = std::min(best, pointSegment(w.pos, sg));
        maxOff = std::max(maxOff, best);
        if (listWalkers)
            std::printf("walker %zu %s %s sex %d lane %d route %d seg %d flags 0x%x body %.1f %.1f %.1f mover %.1f %.1f %.1f facing %.0f clip %s clock %.1f speed %.1f lag %.1f offlane %.2f remaining %.1f\n",
                        i - 1, w.model.c_str(), w.name.c_str(), w.sex, w.lane, w.route, w.seg, w.flags, w.body[0], w.body[1], w.body[2],
                        w.pos[0], w.pos[1], w.pos[2], w.facing, w.clip ? w.clip->name.c_str() : "-", w.clock, w.speed,
                        std::sqrt(lx * lx + ly * ly + lz * lz), best, w.remaining);
    }
    std::printf("run frames %d live %d moved %d lane_changes %d blocked %d overtakes %d actions %d in_action %d idle %d max_lag %.1f max_offlane %.2f nan %d\n",
                frames, live, moved, laneChanges, blocked, overtakes, actions, inAction, idle, maxLag, maxOff, nan);
    return 0;
}
