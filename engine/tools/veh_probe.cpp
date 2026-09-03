// SPDX-License-Identifier: GPL-3.0-or-later
// veh_probe - the ROAD TRAFFIC of a city, spawned and driven headlessly
// (docs/STREET_LIFE.md 2b, todo/road-traffic.md, actor/vehicles.cpp).
//
//     veh_probe <gamedata> <area> [frames] [--vehicles] [--player x,y,z]
//
// It builds the pool the way `Slider_Init` does and NOT through the Session,
// so it needs no area load: the AREA chunk gives the circuit's name (+115),
// the animation library (+124) and the four model masks (+164/+168 for the
// crowd, +172/+174 for the vehicles), and the pool is loaded from those
// directly. That keeps the probe independent of `Session::loadTrafficFor`,
// which is where the masks reach the pool once that file is free to edit.
//
// It prints
//   `masks`    the four masks and the circuit's lane split
//   `spawn`    what the rule alone places on the vehicle lanes, and what the
//              pool actually placed, by kind
//   `run`      after N frames: live, moved, how many stopped/braked/bumped,
//              lane changes, the largest body-to-mover lag, the largest
//              distance from a mover to the VEHICLE network, and any NaN
//   `groups`   the reservation groups both classes reach, and how often a
//              vehicle waited on one
// `--player x,y,z` stands the player there ON THE ROAD, which is what turns
// the brake and the run-over on.
#include "actor/pedestrians.h"
#include "formats/iam.h"
#include "formats/opt.h"
#include "formats/mesh3do.h"
#include "o3de/geom3do.h"
#include "actor/player.h"
#include <map>
#include "platform/datafs.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

// every segment a VEHICLE mover can be on
std::vector<Seg> vehicleNetwork(const omk::OptTrack& t) {
    std::vector<Seg> out;
    for (std::uint32_t li = t.pedEnd; li < t.laneCount; ++li) {
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

std::string headerName(std::span<const std::byte> c, std::size_t off, std::size_t n) {
    if (c.size() < off + n) return {};
    std::string s;
    for (std::size_t i = 0; i < n; ++i) {
        const char ch = static_cast<char>(c[off + i]);
        if (!ch) break;
        s += ch;
    }
    return s;
}
std::uint32_t u32at(std::span<const std::byte> c, std::size_t o) {
    if (c.size() < o + 4) return 0;
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(c[o + i]) << (8 * i);
    return v;
}
// `sub_40EA10` / `sub_40E9D0` read these as int16 and SIGN-EXTEND them
int i16at(std::span<const std::byte> c, std::size_t o) {
    if (c.size() < o + 2) return 0;
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(c[o]) |
                                     (static_cast<std::uint16_t>(c[o + 1]) << 8));
}

// how many reservation groups both classes can reach, expanded through the
// group lists - the state the two populations must share
int sharedGroups(const omk::OptTrack& t) {
    auto used = [&](std::uint32_t lo, std::uint32_t hi) {
        std::vector<int> g;
        for (std::uint32_t li = lo; li < hi; ++li) {
            const auto& L = t.lanes[li];
            const int nr = L.routeCount > 0 ? L.routeCount : 1;
            for (int r = 0; r < nr; ++r) {
                const int ri = L.firstRoute + r;
                if (ri < 0 || ri >= static_cast<int>(t.routes.size())) continue;
                const auto& R = t.routes[static_cast<std::size_t>(ri)];
                auto add = [&](int gi) {
                    if (gi < 0 || gi >= static_cast<int>(t.groups.size())) return;
                    g.push_back(gi);
                    const auto& G = t.groups[static_cast<std::size_t>(gi)];
                    for (int e = 0; e < G.count; ++e) {
                        const int li2 = G.first + e;
                        if (li2 >= 0 && li2 < static_cast<int>(t.lists.size())) g.push_back(t.lists[static_cast<std::size_t>(li2)]);
                    }
                };
                add(R.group);
                for (int st = 0; st < R.stepCount; ++st) {
                    const int si = R.firstStep + st;
                    if (si >= 0 && si < static_cast<int>(t.steps.size())) add(t.steps[static_cast<std::size_t>(si)].group);
                }
            }
        }
        std::sort(g.begin(), g.end());
        g.erase(std::unique(g.begin(), g.end()), g.end());
        return g;
    };
    const auto p = used(t.pedFirst, t.pedEnd), v = used(t.pedEnd, t.laneCount);
    std::vector<int> both;
    std::set_intersection(p.begin(), p.end(), v.begin(), v.end(), std::back_inserter(both));
    return static_cast<int>(both.size());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: veh_probe <gamedata> <area> [frames] [--vehicles] [--player x,y,z]\n");
        return 2;
    }
    const std::string root = argv[1];
    const int area = std::atoi(argv[2]);
    int frames = 600;
    bool list = false, hasPlayer = false;
    float player[3] = {0, 0, 0};
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--vehicles") list = true;
        else if (a == "--player" && i + 1 < argc) {
            hasPlayer = (std::sscanf(argv[++i], "%f,%f,%f", &player[0], &player[1], &player[2]) == 3);
        } else if (i == 3) frames = std::atoi(argv[i]);
    }
    const omk::DataFs fs(root);
    const auto areaFile = fs.read("IAM/AREA");
    const auto arc = omk::IamArchive::open(areaFile);
    const auto chunk = arc.chunk(static_cast<std::size_t>(area));
    if (chunk.empty()) { std::fprintf(stderr, "area %d: no chunk\n", area); return 1; }
    const std::string opt = headerName(chunk, 115, 9);
    const std::string ani = headerName(chunk, 124, 9);
    const std::uint32_t menMask = u32at(chunk, 164), womenMask = u32at(chunk, 168);
    const int sliMask = i16at(chunk, 172), motoMask = i16at(chunk, 174);
    if (opt.empty()) { std::printf("masks area %d opt -\n", area); return 0; }
    const auto track = omk::loadOpt(fs.read("TRAJECTOIRES/" + opt + ".OPT"));
    std::printf("masks area %d opt %s ani %s men %#x women %#x sli %d moto %d "
                "ped_lanes %u veh_lanes %u ped_spacing %u veh_spacing %u shared_groups %d\n",
                area, opt.c_str(), ani.empty() ? "-" : ani.c_str(), menMask, womenMask, sliMask, motoMask,
                track.pedEnd - track.pedFirst, track.laneCount - track.pedEnd,
                track.pedSpacing, track.vehSpacing, track.valid ? sharedGroups(track) : 0);
    if (!track.valid) { std::fprintf(stderr, "%s: %s\n", opt.c_str(), track.error.c_str()); return 1; }

    const omk::PedClips clips = omk::pedClipsFrom(fs.read("ANIMS/" + ani + ".ANI"));
    omk::Pedestrians pool;
    pool.load(track, clips, menMask, womenMask, omk::kDefaultStreetActivity, 1u,
              static_cast<std::uint32_t>(sliMask), static_cast<std::uint32_t>(motoMask));
    // `sub_438040`: the body radius each model carries, which the pool cannot
    // read itself - the root mesh's own, exactly as the Session hands it in
    for (const auto& m : pool.models()) {
        const auto d = fs.read("MESHES/PERSOS/" + m.name + ".3DO");
        if (const auto h = omk::readHeader(d)) {
            const auto meshes = omk::readMeshes(d, *h);
            if (!meshes.empty()) pool.setModelRadius(m.name, meshes.front().radius);
        }
    }
    for (const std::string& name : {std::string("sli_fn"), std::string("moto")}) {
        const auto d = fs.read("MESHES/PERSOS/" + name + ".3DO");
        if (const auto h = omk::readHeader(d)) {
            const auto meshes = omk::readMeshes(d, *h);
            if (!meshes.empty()) pool.setVehicleModelRadius(name, meshes.front().radius);
        }
    }
    if (hasPlayer) pool.setPlayer(player, true);

    // THE NOSE. A still frame cannot settle whether a vehicle faces where it
    // is going, but the models are elongated - a slider is about twice as
    // long as it is wide - so the placed body's extent ALONG its heading
    // against the extent ACROSS can. Built here from the model's own
    // geometry, turned by each vehicle's heading exactly as the viewer turns
    // it (`omk::rotateYaw(facing, ...)`), so a render that put the model
    // sideways would score below 1.
    // ...and only over the SUB-OBJECT ambient traffic draws. A model holds up
    // to four of them (`sub_453A70`) laid out ~200 units apart in x, so
    // measuring the whole file makes a slider look wider than it is long -
    // 0.65 where the drawn body is 1.94.
    std::map<std::string, std::vector<std::array<float, 3>>> vertsOf;
    for (const std::string& name : {std::string("sli_fn"), std::string("moto")}) {
        const auto d = fs.read("MESHES/PERSOS/" + name + ".3DO");
        const auto g = omk::buildGeometry(d, omk::DrawFilter::Engine);
        const auto hh = omk::readHeader(d);
        if (!hh) continue;
        const auto meshes = omk::readMeshes(d, *hh);
        // the heaviest root, which is `sub_453A70`'s sub-object 0
        int best = -1;
        std::size_t bestW = 0;
        auto rootOf = [&](int m) {
            for (int guard = 0; guard < 64 && m >= 0; ++guard) {
                int next = -1;
                for (std::size_t k = 0; k < meshes.size(); ++k)
                    if (meshes[k].id == meshes[static_cast<std::size_t>(m)].parent) { next = static_cast<int>(k); break; }
                if (next < 0) return m;
                m = next;
            }
            return m;
        };
        for (std::size_t i = 0; i < meshes.size(); ++i) {
            if (rootOf(static_cast<int>(i)) != static_cast<int>(i)) continue;
            std::size_t w = 0;
            for (std::size_t j = 0; j < meshes.size(); ++j)
                if (rootOf(static_cast<int>(j)) == static_cast<int>(i))
                    w += static_cast<std::size_t>(meshes[j].vertices) +
                         static_cast<std::size_t>(meshes[j].triangles) +
                         static_cast<std::size_t>(meshes[j].quads);
            if (w > bestW) { bestW = w; best = static_cast<int>(i); }
        }
        auto& v = vertsOf[name];
        for (std::size_t c = 0; c < g.corners.size(); ++c) {
            const auto mi = c < g.cornerMesh.size() ? g.cornerMesh[c] : -1;
            if (mi < 0 || rootOf(mi) != best) continue;
            v.push_back({g.corners[c].x, g.corners[c].y, g.corners[c].z});
        }
    }
    // The model's OWN axes are what settle it, and the ratio is therefore one
    // number per model rather than one per vehicle: `omk::rotateYaw(facing,
    // ...)` sends model -Z to the heading (`pedHeadingOf`: facing 0 looks
    // down -Z), so a model elongated along its own Z is a vehicle whose long
    // axis follows its travel. Rotating the corners first and taking an
    // axis-aligned box does NOT measure this - at an oblique heading the box
    // mixes the two spans, which is what made a first version of this report
    // 0.65 for a slider that draws correctly.
    auto noseRatio = [&](const std::string& model) -> float {
        const auto it = vertsOf.find(model);
        if (it == vertsOf.end() || it->second.empty()) return 0.0f;
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (const auto& c : it->second)
            for (int k = 0; k < 3; ++k) { if (c[k] < lo[k]) lo[k] = c[k]; if (c[k] > hi[k]) hi[k] = c[k]; }
        const float along = hi[2] - lo[2], across = hi[0] - lo[0];
        return across > 0.0f ? along / across : 0.0f;
    };

    int sliders = 0, motos = 0;
    for (const auto& v : pool.vehicles()) if (v.live) (v.kind == 1 ? sliders : motos)++;
    // which vehicle lanes actually carry traffic: the walk stops the moment
    // the 40th is placed, so the tail of the lane list is always empty
    int lanesUsed = 0;
    std::vector<int> seen;
    for (const auto& v : pool.vehicles()) {
        if (!v.live || v.mover < 0) continue;
        const int ln = pool.walkers()[static_cast<std::size_t>(v.mover)].lane;
        if (std::find(seen.begin(), seen.end(), ln) == seen.end()) { seen.push_back(ln); ++lanesUsed; }
    }
    std::printf("spawn rule %d uncapped %d placed %d sliders %d motos %d lanes_used %d walkers %d\n",
                omk::Pedestrians::vehicleSpawnCount(track), omk::Pedestrians::vehicleSpawnCount(track, false),
                pool.liveVehicles(), sliders, motos, lanesUsed, pool.liveCount());

    std::vector<std::array<float, 3>> start;
    for (const auto& v : pool.vehicles())
        start.push_back(v.mover >= 0 ? std::array<float, 3>{pool.walkers()[static_cast<std::size_t>(v.mover)].body[0],
                                                            pool.walkers()[static_cast<std::size_t>(v.mover)].body[1],
                                                            pool.walkers()[static_cast<std::size_t>(v.mover)].body[2]}
                                     : std::array<float, 3>{0, 0, 0});
    // THE SHARED RESERVATION GROUPS. A mover inside a route holds the group
    // its current segment names (`sub_453230`: segment 0 is the route's own
    // group, k its step k), and holding one marks every group in that group's
    // LIST busy - which is how a vehicle route and a pedestrian route that
    // cross are kept apart. Give the two classes separate busy counters and
    // that stops working, so the invariant is: no frame in which a walker and
    // a vehicle hold groups whose lists intersect.
    auto listOf = [&](int g, std::vector<int>& out) {
        out.clear();
        if (g < 0 || g >= static_cast<int>(track.groups.size())) return;
        out.push_back(g);
        const auto& G = track.groups[static_cast<std::size_t>(g)];
        for (int i = 0; i < G.count; ++i) {
            const int li = G.first + i;
            if (li >= 0 && li < static_cast<int>(track.lists.size())) out.push_back(track.lists[static_cast<std::size_t>(li)]);
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
    };
    auto heldGroup = [&](const omk::Pedestrian& m) {
        if (!(m.flags & 0x10u) || m.route < 0 || m.route >= static_cast<int>(track.routes.size())) return -1;
        const auto& R = track.routes[static_cast<std::size_t>(m.route)];
        if (m.seg == 0) return static_cast<int>(R.group);
        const int si = R.firstStep + m.seg - 1;
        if (si < 0 || si >= static_cast<int>(track.steps.size())) return -1;
        return static_cast<int>(track.steps[static_cast<std::size_t>(si)].group);
    };
    int bumpEvents = 0, conflicts = 0, sharedFrames = 0, vehHeld = 0, pedHeld = 0;
    std::vector<int> la, lb;
    for (int f = 0; f < frames; ++f) {
        pool.tick(1.0f);
        bumpEvents += static_cast<int>(pool.bumped().size());
        std::vector<int> pg, vg;
        for (const auto& m : pool.walkers()) {
            if (!m.live) continue;
            const int g = heldGroup(m);
            if (g < 0) continue;
            (m.vehicle >= 0 ? vg : pg).push_back(g);
        }
        pedHeld += static_cast<int>(pg.size());
        vehHeld += static_cast<int>(vg.size());
        if (pg.empty() || vg.empty()) continue;
        ++sharedFrames;
        bool clash = false;
        for (const int a : vg) {
            listOf(a, la);
            for (const int b : pg) {
                listOf(b, lb);
                std::vector<int> both;
                std::set_intersection(la.begin(), la.end(), lb.begin(), lb.end(), std::back_inserter(both));
                if (!both.empty()) { clash = true; break; }
            }
            if (clash) break;
        }
        if (clash) ++conflicts;
    }
    const auto net = vehicleNetwork(track);
    int live = 0, moved = 0, stops = 0, brakes = 0, bumps = 0, blocked = 0, laneChanges = 0, nan = 0, sound = 0;
    float maxLag = 0.0f, maxOff = 0.0f, maxSpeed = 0.0f;
    for (std::size_t i = 0; i < pool.vehicles().size(); ++i) {
        const auto& v = pool.vehicles()[i];
        if (!v.live || v.mover < 0) continue;
        const auto& m = pool.walkers()[static_cast<std::size_t>(v.mover)];
        ++live;
        const float dx = m.body[0] - start[i][0], dz = m.body[2] - start[i][2];
        if (std::sqrt(dx * dx + dz * dz) > 1.0f) ++moved;
        stops += v.stops; brakes += v.brakes; bumps += v.bumps;
        blocked += m.blockedFrames; laneChanges += m.laneChanges;
        if (v.sound >= 0) ++sound;
        bool bad = false;
        for (int k = 0; k < 3; ++k) if (std::isnan(m.body[k]) || std::isnan(m.pos[k])) bad = true;
        if (bad) { ++nan; continue; }
        const float lx = m.pos[0] - m.body[0], ly = m.pos[1] - m.body[1], lz = m.pos[2] - m.body[2];
        maxLag = std::max(maxLag, std::sqrt(lx * lx + ly * ly + lz * lz));
        maxSpeed = std::max(maxSpeed, m.baseSpeed);
        float best = 1e30f;
        for (const auto& sg : net) best = std::min(best, pointSegment(m.pos, sg));
        maxOff = std::max(maxOff, best);
        if (list)
            std::printf("vehicle %zu %s kind %d lane %d route %d seg %d flags 0x%x body %.1f %.1f %.1f "
                        "mover %.1f %.1f %.1f facing %.0f speed %.1f lag %.1f offlane %.2f sound %d\n",
                        i, v.model.c_str(), v.kind, m.lane, m.route, m.seg, m.flags,
                        m.body[0], m.body[1], m.body[2], m.pos[0], m.pos[1], m.pos[2], m.facing,
                        m.baseSpeed, std::sqrt(lx * lx + ly * ly + lz * lz), best, v.sound);
    }
    std::printf("run frames %d live %d moved %d lane_changes %d blocked %d stops %d brakes %d "
                "bumps %d bump_events %d sounding %d max_speed %.1f max_lag %.1f max_offlane %.2f nan %d\n",
                frames, live, moved, laneChanges, blocked, stops, brakes, bumps, bumpEvents, sound,
                maxSpeed, maxLag, maxOff, nan);
    // `conflicts` is NOT an invariant and is reported for what it is: the
    // engine's rule is asymmetric (entering tests `busy[g]` and marks
    // `list(g)`), so two movers whose lists merely intersect can legitimately
    // hold at once. What proves the sharing is the CROSS-CLASS WAIT.
    {
        float worstSli = 1e30f, worstMoto = 1e30f;
        int measured = 0;
        for (const auto& v : pool.vehicles()) {
            if (!v.live || v.mover < 0) continue;
            const auto& m = pool.walkers()[static_cast<std::size_t>(v.mover)];
            (void)m;
            const float rr = noseRatio(v.model);
            if (rr <= 0.0f) continue;
            ++measured;
            if (v.kind == 1) worstSli = std::min(worstSli, rr);
            else             worstMoto = std::min(worstMoto, rr);
        }
        std::printf("nose measured %d worst_slider %.2f worst_moto %.2f\n", measured,
                    worstSli > 1e29f ? 0.0f : worstSli, worstMoto > 1e29f ? 0.0f : worstMoto);
    }
    std::printf("groups shared %d ped_holds %d veh_holds %d both_holding_frames %d overlaps %d "
                "veh_waited_on_ped %d ped_waited_on_veh %d\n",
                sharedGroups(track), pedHeld, vehHeld, sharedFrames, conflicts,
                pool.crossClassWaits(true), pool.crossClassWaits(false));
    // the crowd must be untouched by the traffic sharing its pool
    std::printf("crowd walkers %d live_movers %zu\n", pool.liveCount(), pool.walkers().size());
    return 0;
}
