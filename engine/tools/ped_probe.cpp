// SPDX-License-Identifier: GPL-3.0-or-later
// ped_probe - the procedural pedestrians of a city street, spawned and run
// headlessly (docs/STREET_LIFE.md 2, actor/sliders.h).
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
#include "actor/pose.h"
#include "actor/sliders.h"
#include "formats/opt.h"
#include "platform/datafs.h"
#include "script/area.h"
#include "script/gamestate.h"
#include "script/script.h"
#include "script/scenerunner.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <set>
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
    bool listWalkers = false, listActions = false, listPrograms = false;
    for (int i = 4; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--walkers") listWalkers = true;
        else if (a == "--actions") listActions = true;
        else if (a == "--programs") listPrograms = true;
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
    // the area's scene programs, as omk-play's street start loads them
    if (listPrograms) s.loadScene(fr + "/SCPTDATA", omk::ChunkKind::Area, area);
    const auto& peds = s.sliders();
    const auto& slot = s.residentSlot(s.activeSlot());
    // the rule alone, per level, on the circuit the area names
    const omk::DataFs fs(fr);
    const auto track = omk::loadOpt(fs.read("TRAJECTOIRES/" + slot.opt + ".OPT"));
    std::printf("counts area %d opt %s valid %d", area, slot.opt.empty() ? "-" : slot.opt.c_str(), track.valid ? 1 : 0);
    for (int l = 0; l <= 4; ++l) std::printf(" level%d %d", l, track.valid ? omk::Sliders::spawnCount(track, l) : 0);
    std::printf("\n");
    const int spawned = peds.liveCount();
    std::string models;
    for (const auto& m : peds.models()) { if (!models.empty()) models += ","; models += m.name; }
    std::printf("session area %d ani %s loaded %d level %d spawned %d models %s\n", area,
                slot.ani.empty() ? "-" : slot.ani.c_str(), peds.loaded() ? 1 : 0, peds.streetActivity(),
                spawned, models.empty() ? "-" : models.c_str());
    std::vector<std::array<float, 3>> start;
    for (const auto& w : peds.movers()) start.push_back({w.body[0], w.body[1], w.body[2]});
    // `--actions`: how long each walker holds an action point (flag 0x80)
    // without a break, and where the longest holds stand - the phase and clip.
    std::vector<int> hold(peds.movers().size(), 0), longest(peds.movers().size(), 0);
    std::map<std::string, bool> progWas;
    std::map<std::string, std::string> stepWas;
    std::map<int, int> endedAt, leftAt;
    std::set<int> shownWas;
    int progEnds = 0, progEndsNoBank = 0, reentries = 0, reentriesAfterEnd = 0;
    std::vector<int> longPhase(peds.movers().size(), -2);
    std::vector<std::string> longClip(peds.movers().size());
    for (int f = 0; f < frames; ++f) {
        s.frame();
        if (listPrograms) {
            // `--programs`: an actor program that stops running, and a body
            // that leaves or re-enters the shown list (a `Staged` rebuilt in
            // omk-play starts with no remembered pose)
            for (const omk::SceneRunner* run : {&s.scene(), &s.sceneOut()}) {
                if (!run->loaded()) continue;
                const auto& st = run->started();
                for (std::size_t k = 0; k < st.size(); ++k) {
                    const auto& t = st[k];
                    if (t.clip < 0 || (t.how != "actor" && t.how != "player")) continue;
                    const std::string key = run->file() + "#" + std::to_string(k);
                    const bool on = run->programRunning(static_cast<int>(k));
                    auto it = progWas.find(key);
                    if (it != progWas.end() && it->second && !on) {
                        std::string model = "-", bank = "-"; bool shownNow = false;
                        for (const auto& sh : s.shown())
                            if (sh.actor == t.actor) { model = sh.model; bank = sh.bank.empty() ? "none" : sh.bank; shownNow = true; }
                        std::printf("prog frame %d: '%s' on actor %d ended (clip %d) - %s, bank %s%s\n", f, t.name.c_str(),
                                    t.actor, t.clip, model.c_str(), bank.c_str(), shownNow ? "" : ", NOT shown");
                        ++progEnds;
                        if (shownNow && bank == "none") ++progEndsNoBank;
                        endedAt[t.actor] = f;
                    }
                    progWas[key] = on;
                    // the program's STEP: its pc, the clip it names, whether a body
                    // animation has been reached - every change, with the anim clock
                    const int pc = run->programPc(static_cast<int>(k));
                    const std::string sig = std::to_string(pc) + "/" + std::to_string(t.clip) + "/" +
                                            (t.animReached ? "1" : "0");
                    auto sw = stepWas.find(key);
                    if (sw == stepWas.end() || sw->second != sig) {
                        if (sw != stepWas.end() && t.how == "actor")
                            std::printf("step frame %d: '%s' actor %d pc %d clip %d reached %d animClock %.1f clipBytes %zu (was %s)\n",
                                        f, t.name.c_str(), t.actor, pc, t.clip, t.animReached ? 1 : 0,
                                        run->programAnimClock(static_cast<int>(k)),
                                        t.clip >= 0 ? run->scene().clipData(t.clip).size() : 0u, sw->second.c_str());
                        stepWas[key] = sig;
                    }
                }
            }
            std::set<int> now;
            for (const auto& sh : s.shown()) now.insert(sh.actor);
            if (f > 0) {
                for (int a : shownWas) if (!now.count(a)) {
                    std::printf("prog frame %d: actor %d left the shown list\n", f, a);
                    leftAt[a] = f;
                }
                for (int a : now) if (!shownWas.count(a)) {
                    const bool back = leftAt.count(a);
                    const bool afterEnd = endedAt.count(a);
                    std::printf("prog frame %d: actor %d %s the shown list%s\n", f, a, back ? "RE-ENTERED" : "entered",
                                afterEnd ? " (its program had ended)" : "");
                    if (back) ++reentries;
                    if (back && afterEnd) ++reentriesAfterEnd;
                }
            }
            shownWas = now;
        }
        if (!listActions) continue;
        const auto& ms = peds.movers();
        for (std::size_t k = 0; k < ms.size(); ++k) {
            if (ms[k].live && ms[k].vehicle < 0 && (ms[k].flags & 0x80u)) ++hold[k]; else hold[k] = 0;
            if (hold[k] > longest[k]) {
                longest[k] = hold[k];
                longPhase[k] = peds.actionPhase(static_cast<int>(k));
                longClip[k] = ms[k].clip ? ms[k].clip->name : "-";
            }
        }
    }
    if (listPrograms) {
        // what there was to watch: a parse that reads nothing must say so
        int acts = 0;
        for (const auto& t : s.scene().started()) if (t.how == "actor" && t.clip >= 0) ++acts;
        std::printf("programs input: scene loaded %d file %s started %zu (actor+clip %d) running %d; outgoing loaded %d; shown %zu\n",
                    s.scene().loaded() ? 1 : 0, s.scene().file().c_str(), s.scene().started().size(), acts,
                    s.scene().programsRunning(), s.sceneOut().loaded() ? 1 : 0, s.shown().size());
        for (int pth : {133, 134}) {
            const auto& ps = s.scene().scene().paths();
            if (pth >= static_cast<int>(ps.size())) continue;
            const auto& pa = ps[static_cast<std::size_t>(pth)];
            if (pa.keys.empty()) { std::printf("programs path %d: no keys\n", pth); continue; }
            std::printf("programs path %d '%s': %zu keys, frames %d..%d, from %.0f %.0f %.0f to %.0f %.0f %.0f\n", pth,
                        pa.name.c_str(), pa.keys.size(), static_cast<int>(pa.keys.front().frame), static_cast<int>(pa.keys.back().frame),
                        pa.keys.front().pos[0], pa.keys.front().pos[1], pa.keys.front().pos[2],
                        pa.keys.back().pos[0], pa.keys.back().pos[1], pa.keys.back().pos[2]);
        }
        // clip 12's raw tracks: position keys, and the travel they hold
        for (int c : {12, 21}) {
            const auto d = s.scene().scene().clipData(c);
            if (d.size() < 8) continue;
            const auto rd = [&](std::size_t o) { std::int32_t v = 0; std::memcpy(&v, d.data() + o, 4); return v; };
            const auto rf = [&](std::size_t o) { float v = 0; std::memcpy(&v, d.data() + o, 4); return v; };
            const int n = rd(4);
            std::printf("programs raw clip %d: frames %d tracks %d\n", c, rd(0), n);
            for (int i = 0; i < n && 8u + 40u * static_cast<std::size_t>(i + 1) <= d.size(); ++i) {
                const std::size_t o = 8u + 40u * static_cast<std::size_t>(i);
                char nm[21] = {0}; std::memcpy(nm, d.data() + o + 4, 20);
                const int pk = rd(o + 24), po = rd(o + 28), rk = rd(o + 32);
                std::printf("  track %d node %d %s pos %d@%d rot %d", i, rd(o), nm, pk, po, rk);
                if (pk > 1 && po > 0 && static_cast<std::size_t>(po) + 12u * static_cast<std::size_t>(pk) <= d.size()) {
                    const auto key = [&](int k) { const std::size_t q = static_cast<std::size_t>(po) + 12u * static_cast<std::size_t>(k);
                                                  return std::array<float, 3>{rf(q), rf(q + 4), rf(q + 8)}; };
                    auto k0 = key(0), k1 = key(1), kl = key(pk - 1);
                    float sum[3] = {0, 0, 0};
                    for (int k = 1; k < pk; ++k) { auto kk = key(k); for (int j = 0; j < 3; ++j) sum[j] += kk[j]; }
                    std::printf(" | key0 %.2f %.2f %.2f key1 %.2f %.2f %.2f last %.2f %.2f %.2f sum1.. %.1f %.1f %.1f",
                                k0[0], k0[1], k0[2], k1[0], k1[1], k1[2], kl[0], kl[1], kl[2], sum[0], sum[1], sum[2]);
                }
                std::printf("\n");
            }
        }
        // every scene clip the street's actor programs name, through the viewer's own reader
        for (int c = 11; c <= 22; ++c) {
            const auto tk = omk::clipTracks(s.scene().scene().clipData(c));
            std::printf("programs clip %d: frames %d tracks %d valid %d root travel %.1f %.1f %.1f -", c, tk.frames, tk.count,
                        tk.valid() ? 1 : 0, tk.trans.empty() ? 0.0f : tk.trans.back()[0],
                        tk.trans.empty() ? 0.0f : tk.trans.back()[1], tk.trans.empty() ? 0.0f : tk.trans.back()[2]);
            for (std::size_t q = 0; q < tk.ids.size(); ++q)
                std::printf(" %d:%s", tk.ids[q], q < tk.names.size() ? tk.names[q].c_str() : "?");
            std::printf("\n");
        }
        for (const auto& t : s.scene().started())
            std::printf("programs started: object %d '%s' how %s actor %d clip %d\n", t.object, t.name.c_str(),
                        t.how.c_str(), t.actor, t.clip);
    }
    if (listPrograms)
        std::printf("programs ended %d (shown, no bank %d) re-entries %d (after an end %d)\n", progEnds, progEndsNoBank,
                    reentries, reentriesAfterEnd);
    if (listActions) {
        int over[4] = {0, 0, 0, 0};                 // holds past 150, 300, 600, 1200 frames
        for (std::size_t k = 0; k < longest.size(); ++k) {
            const int L = longest[k];
            if (L > 150) ++over[0];
            if (L > 300) ++over[1];
            if (L > 600) ++over[2];
            if (L > 1200) ++over[3];
            if (L > 300)
                std::printf("hold walker %zu %s longest %d now %d phase %d clip %s clock %.1f/%d\n", k,
                            peds.movers()[k].model.c_str(), L, hold[k], longPhase[k], longClip[k].c_str(),
                            peds.movers()[k].clock, peds.movers()[k].frames);
        }
        std::printf("holds over150 %d over300 %d over600 %d over1200 %d\n", over[0], over[1], over[2], over[3]);
    }
    const auto net = network(peds.track());
    int live = 0, moved = 0, laneChanges = 0, blocked = 0, overtakes = 0, actions = 0, idle = 0, inAction = 0, nan = 0;
    float maxLag = 0.0f, maxOff = 0.0f;
    std::size_t i = 0;
    for (const auto& w : peds.movers()) {
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
