// SPDX-License-Identifier: GPL-3.0-or-later
// THE SHOOT AI'S NAVIGATION GRID, read and DRAWN.
//
//     map2d_probe <gamedata> [map] [floor]      e.g. map2d_probe ../gamedata gallery 0
//     map2d_probe <gamedata> --all              every map, one line per floor
//     map2d_probe <gamedata> --sight            the LINE OF SIGHT, every map
//     map2d_probe <gamedata> --routes           the PATROL ROUTES, every map
//     map2d_probe <gamedata> --links            the INTER-FLOOR LINKS
//     map2d_probe <gamedata> --snap             `sub_4368E0`'s SPIRAL, every map
//     map2d_probe <gamedata> --sight-pair MAP FLOOR X0 Z0 X1 Z1
//                                               ONE `sub_4359A0` walk, both ways
//
// `MAP2D/*.mpt` is the map screen AND the grid `Shoot_Think` moves on
// (`engine/src/formats/map2d.h`, `todo/shoot-mode.md`). A census is not enough
// to judge a navigation grid - a person has to see whether the walkable cells
// are the shape of the room - so the default output DRAWS the floor:
//
//     '#' blocked (0/2/3)   '.' floor (1)   'D' a door cell (0x10|n)
//     '?' a value nothing branches on (8, 0xCD)
#include "formats/map2d.h"
#include "platform/datafs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: map2d_probe <gamedata> [map] [floor] | --all\n");
        return 2;
    }
    const std::string root = argv[1];
    const bool all = argc > 2 && std::string(argv[2]) == "--all";
    const bool sight = argc > 2 && std::string(argv[2]) == "--sight";
    const bool routes = argc > 2 && std::string(argv[2]) == "--routes";
    const bool links = argc > 2 && std::string(argv[2]) == "--links";
    const bool snap = argc > 2 && std::string(argv[2]) == "--snap";
    const bool sightPair = argc > 8 && std::string(argv[2]) == "--sight-pair";
    const std::vector<std::string> names = {
        "archiv03", "archiv05", "astaroth", "bar56", "CSlev-3", "gallery",
        "grotte", "hames", "smarket1", "soukdock", "soukt", "tetra2",
        "tetra3", "tetra4", "tetradou", "yrmali"};

    if (sightPair) {
        // ONE line of sight, walked both ways and in both door states - the
        // engage's call is `sub_4359A0(floor, TARGET cell, OWN cell, 1)`, so
        // "target -> self" is the engine's order; the reverse is printed beside
        // it because a stepped line need not be symmetric.
        omk::Map2d m;
        if (!m.loadFile(root + "/MAP2D/" + argv[3] + ".mpt")) {
            std::fprintf(stderr, "no map %s\n", argv[3]);
            return 1;
        }
        const int fl = std::atoi(argv[4]);
        const int x0 = std::atoi(argv[5]), z0 = std::atoi(argv[6]);
        const int x1 = std::atoi(argv[7]), z1 = std::atoi(argv[8]);
        const auto walk = [&](const char* label, int ax, int az, int bx, int bz, std::uint16_t mask) {
            int kx = -1, kz = -1;
            const bool clear = m.lineOfSight(fl, ax, az, bx, bz, mask, &kx, &kz);
            std::printf("sight %s %s (%d,%d)->(%d,%d): %s", label,
                        mask ? "doors-open" : "doors-shut", ax, az, bx, bz,
                        clear ? "CLEAR" : "BLOCKED");
            if (!clear) {
                const auto& f = m.floors()[static_cast<std::size_t>(fl)];
                std::printf(" at (%d,%d) byte %d", kx, kz, int(f.cell(kx, kz)));
            }
            std::printf("\n");
        };
        for (const std::uint16_t mask : {std::uint16_t(0xFFFF), std::uint16_t(0)}) {
            walk("target->self", x0, z0, x1, z1, mask);
            walk("self->target", x1, z1, x0, z0, mask);
        }
        return 0;
    }
    if (snap) {
        // `sub_4368E0` (`Map2d::snapToStandable`), measured so it can FAIL:
        // from the centre of every third cell of every floor of every map,
        // where does the spiral land? It never tests the start itself - its
        // first candidate is the (-1,-1) neighbour - so even a standable start
        // moves, and which neighbour it reaches is the ring order. Counted:
        // found / none, how far each moved in cells (the ring, 1..5), and any
        // landing that is NOT standable (must be 0, by the byte and bounds the
        // spiral itself tests).
        long tested = 0, found = 0, none = 0, bad = 0;
        long rings[7] = {0, 0, 0, 0, 0, 0, 0};
        for (const auto& n : names) {
            omk::Map2d m;
            if (!m.loadFile(root + "/MAP2D/" + n + ".mpt")) continue;
            const float S = static_cast<float>(m.scale());
            for (std::size_t fi = 0; fi < m.floors().size(); ++fi) {
                const auto& f = m.floors()[fi];
                for (std::uint32_t z = 0; z < f.h; z += 3)
                    for (std::uint32_t x = 0; x < f.w; x += 3) {
                        const float px = f.bound[0] + (float(x) + 0.5f) * S;
                        const float pz = f.bound[4] + (float(z) + 0.5f) * S;
                        float sx = px, sz = pz;
                        ++tested;
                        if (!m.snapToStandable(int(fi), sx, f.bound[3], sz)) { ++none; continue; }
                        ++found;
                        const long dx = std::lround((sx - px) / S), dz = std::lround((sz - pz) / S);
                        const long ring = std::max(std::labs(dx), std::labs(dz));
                        ++rings[ring < 6 ? ring : 6];
                        const long cx = long(x) + dx, cz = long(z) + dz;
                        if (cx < 1 || cz < 1 || cx >= long(f.w) || cz >= long(f.h) ||
                            omk::Map2d::blockedValue(f.cell(int(cx), int(cz))))
                            ++bad;
                    }
            }
        }
        std::printf("snap tested %ld found %ld none %ld bad %ld\n", tested, found, none, bad);
        std::printf("snap rings 0..6+: %ld %ld %ld %ld %ld %ld %ld\n", rings[0], rings[1],
                    rings[2], rings[3], rings[4], rings[5], rings[6]);
        // ...and one worked case, the supermarket player's own cell (6,11)
        omk::Map2d sm;
        if (sm.loadFile(root + "/MAP2D/smarket1.mpt") && !sm.floors().empty()) {
            const auto& f = sm.floors()[0];
            const float S = static_cast<float>(sm.scale());
            const float px = f.bound[0] + 6.5f * S, pz = f.bound[4] + 11.5f * S;
            float sx = px, sz = pz;
            const bool ok = sm.snapToStandable(0, sx, f.bound[3], sz);
            std::printf("snap smarket1 floor 0 from (6,11) byte %d -> %s (%ld,%ld)\n",
                        int(f.cell(6, 11)), ok ? "found" : "none",
                        6 + std::lround((sx - px) / S), 11 + std::lround((sz - pz) / S));
        }
        return 0;
    }
    if (sight) {
        // `sub_4359A0`'s walk (todo/shoot-mode.md 5c), measured two ways.
        //
        // (1) the PREDICATES disagree, and by how much: sight refuses only a
        //     wall and a shut door, movement refuses {0,2,3,0x80} - so cells
        //     2 and 3 are see-through and unwalkable, which is the whole
        //     point of there being two tests.
        // (2) the WALK runs, over a deterministic sample of walkable cell
        //     pairs, with every door open and then every door shut.  The two
        //     counts must differ, or the door arm is not being exercised.
        long seeNotWalk = 0, walkNotSee = 0, cells = 0;
        long pairs = 0, visOpen = 0, visShut = 0;
        int mapsWithDoors = 0;
        for (const auto& n : names) {
            omk::Map2d m;
            if (!m.loadFile(root + "/MAP2D/" + n + ".mpt")) continue;
            bool anyDoor = false;
            for (std::size_t fi = 0; fi < m.floors().size(); ++fi) {
                const auto& f = m.floors()[fi];
                std::vector<std::pair<int, int>> walkable;
                for (std::uint32_t z = 1; z < f.h; ++z)
                    for (std::uint32_t x = 1; x < f.w; ++x) {
                        const std::uint8_t c = f.cell(int(x), int(z));
                        const bool mv = omk::Map2d::blockedValue(c);
                        const bool sg = omk::Map2d::sightBlockedValue(c, 0xFFFF);
                        ++cells;
                        if (mv && !sg) ++seeNotWalk;
                        if (sg && !mv) ++walkNotSee;
                        if ((c & omk::Map2d::kDoorBit) && c != 0 && (c & 0xF0) == 0x10)
                            anyDoor = true;
                        if (!mv) walkable.push_back({int(x), int(z)});
                    }
                // a fixed stride through the walkable cells, so the sample is
                // the same on every machine and every run
                const std::size_t n2 = walkable.size();
                if (n2 < 2) continue;
                const std::size_t stride = n2 / 37 + 1;
                for (std::size_t i = 0; i + stride < n2; i += stride) {
                    const auto& a = walkable[i];
                    const auto& b = walkable[n2 - 1 - i];
                    ++pairs;
                    if (m.lineOfSight(int(fi), a.first, a.second, b.first, b.second, 0xFFFF))
                        ++visOpen;
                    if (m.lineOfSight(int(fi), a.first, a.second, b.first, b.second, 0x0000))
                        ++visShut;
                }
            }
            if (anyDoor) ++mapsWithDoors;
        }
        // the predicate itself, every byte through it, both door states -
        // so the check asserts the SET and not a count derived from it
        for (int shut = 0; shut < 2; ++shut) {
            std::printf("sight refuses doors-%s", shut ? "shut" : "open");
            for (int v = 0; v < 256; ++v)
                if (omk::Map2d::sightBlockedValue(std::uint8_t(v), shut ? 0x0000 : 0xFFFF))
                    std::printf(" %d", v);
            std::printf("\n");
        }
        std::printf("cells %ld  see-not-walk %ld  walk-not-see %ld\n", cells, seeNotWalk, walkNotSee);
        std::printf("maps with door cells %d\n", mapsWithDoors);
        std::printf("pairs %ld  visible doors-open %ld  visible doors-shut %ld\n",
                    pairs, visOpen, visShut);
        return 0;
    }

    if (links) {
        // THE INTER-FLOOR LINKS (`todo/shoot-navedge.md`): what the loader's
        // 28-byte per-floor records really are, and the three things that
        // settle it - the destination in range, both points inside their own
        // floors, and the RECIPROCITY that makes a stair a stair.
        int tot = 0, inRange = 0, placed = 0, paired = 0;
        for (const auto& n : names) {
            omk::Map2d m;
            if (!m.loadFile(root + "/MAP2D/" + n + ".mpt")) continue;
            const auto& fl = m.floors();
            bool any = false;
            for (std::size_t i = 0; i < fl.size(); ++i) any = any || !fl[i].links.empty();
            if (any) std::printf("%s: %zu floors\n", n.c_str(), fl.size());
            for (std::size_t i = 0; i < fl.size(); ++i) {
                for (const auto& L : fl[i].links) {
                    ++tot;
                    const bool ok = L.destFloor < fl.size();
                    if (ok) ++inRange;
                    // a point is "inside" its floor within one cell of the box
                    const auto in = [&](const float p[3], const omk::Map2dFloor& g) {
                        const float m2 = static_cast<float>(m.scale());
                        return g.bound[0] - m2 <= p[0] && p[0] <= g.bound[1] + m2 &&
                               g.bound[4] - m2 <= p[2] && p[2] <= g.bound[5] + m2;
                    };
                    const bool sited = ok && in(L.from, fl[i]) &&
                                       in(L.to, fl[static_cast<std::size_t>(L.destFloor)]);
                    if (sited) ++placed;
                    // ...and the twin: a link back, whose ends are these swapped
                    bool twin = false;
                    if (ok)
                        for (const auto& B : fl[static_cast<std::size_t>(L.destFloor)].links)
                            if (B.destFloor == i) {
                                float d = 0.0f;
                                for (int k = 0; k < 3; ++k)
                                    d += std::abs(B.from[k] - L.to[k]) +
                                         std::abs(B.to[k] - L.from[k]);
                                if (d < 1.0f) twin = true;
                            }
                    if (twin) ++paired;
                    std::printf("  floor %zu -> %u  from %7.0f %6.0f %7.0f  to %7.0f %6.0f %7.0f"
                                "  %s%s%s\n", i, L.destFloor,
                                double(L.from[0]), double(L.from[1]), double(L.from[2]),
                                double(L.to[0]), double(L.to[1]), double(L.to[2]),
                                ok ? "" : "DEST OUT OF RANGE ", sited ? "" : "NOT IN ITS FLOORS ",
                                twin ? "" : "NO TWIN");
                }
            }
        }
        std::printf("\nlinks %d; destination a real floor %d; both points inside their own "
                    "floors %d; reciprocal %d\n", tot, inRange, placed, paired);
        // ...and `sub_436BB0` RUN, on bar56, the map with five links between one
        // pair of floors - so the nearest-of-five is a real choice and not a
        // lookup with one answer.
        omk::Map2d bar;
        if (bar.loadFile(root + "/MAP2D/bar56.mpt")) {
            const auto& f0 = bar.floors()[0];
            std::string picks;
            // stand next to each of floor 0's own links in turn: each time the
            // nearest to that point must be that link
            int self = 0;
            for (std::size_t i = 0; i < f0.links.size(); ++i) {
                const float at[3] = {f0.links[i].from[0], f0.links[i].from[1],
                                     f0.links[i].from[2]};
                const int got = bar.linkTo(0, 1, at);
                if (got == static_cast<int>(i)) ++self;
                picks += std::to_string(got);
                picks += " ";
            }
            // a point far to one side, and the two refusals: no such
            // destination, and a floor off the end
            const float west[3] = {11700.0f, 0.0f, -100.0f};
            const int far = bar.linkTo(0, 1, west);
            // A POINT THAT TELLS THE TWO ENDS APART - and it has to be chosen
            // for it. Links 2, 3 and 4 all have their near end at x 11750, so
            // only z decides between them: their `from` z are -379, -184 and
            // -106 and their `to` z are -363, -168 and -90, each about sixteen
            // units up the stair. At z = -274 the near ends favour link 3 (90
            // against 105) and the far ends favour link 2 (89 against 106), so
            // this is the one query whose answer changes if the distance is
            // measured from the wrong end. Every other query here answers the
            // same either way, because the stairs are short and far apart.
            const float mid[3] = {11750.0f, 0.0f, -274.0f};
            const int tell = bar.linkTo(0, 1, mid);
            const int none = bar.linkTo(0, 2, west);
            const int offEnd = bar.linkTo(-1, 1, west);
            // ...and the PICKED LINK'S OWN ENDS, by value. Which index wins is
            // a weak test on this corpus - the stairs are short and far apart,
            // so measuring from the wrong end of one still ranks it first - and
            // printing the two points is what pins the field meanings down.
            char ends[128] = "none";
            if (far >= 0)
                std::snprintf(ends, sizeof ends, "from %.0f %.0f %.0f to %.0f %.0f %.0f",
                              double(f0.links[static_cast<std::size_t>(far)].from[0]),
                              double(f0.links[static_cast<std::size_t>(far)].from[1]),
                              double(f0.links[static_cast<std::size_t>(far)].from[2]),
                              double(f0.links[static_cast<std::size_t>(far)].to[0]),
                              double(f0.links[static_cast<std::size_t>(far)].to[1]),
                              double(f0.links[static_cast<std::size_t>(far)].to[2]));
            std::printf("link lookup: bar56 floor 0 has %zu links to floor 1; standing on each "
                        "picks %s(%d of %zu its own); from 11700,-100 picks %d (%s); from "
                        "11750,-274 picks %d; to floor 2 %d; from floor -1 %d\n",
                        f0.links.size(), picks.c_str(), self,
                        f0.links.size(), far, ends, tell, none, offEnd);
        }
        return 0;
    }

    if (routes) {
        // THE PATROL ROUTES (`todo/shoot-patrol.md`): the corpus, and the four
        // lookups RUN rather than described. The census is what a check holds
        // on to; the per-route line is for a person deciding whether a ring of
        // cells is the shape of a beat someone would walk.
        int tot = 0, ping = 0, clips = 0, rings = 0, minLen = 1000, maxLen = 0;
        for (const auto& n : names) {
            omk::Map2d m;
            if (!m.loadFile(root + "/MAP2D/" + n + ".mpt")) continue;
            for (std::size_t fi = 0; fi < m.floors().size(); ++fi) {
                const auto& f = m.floors()[fi];
                for (std::size_t ri = 0; ri < f.waypoints.size(); ++ri) {
                    const auto& w = f.waypoints[ri];
                    ++tot;
                    if (w.pingPong()) ++ping; else ++rings;
                    minLen = std::min<int>(minLen, static_cast<int>(w.len));
                    maxLen = std::max<int>(maxLen, static_cast<int>(w.len));
                    std::string pts;
                    for (const auto& p : w.points) {
                        if (p.clipId) ++clips;
                        char b[32];
                        std::snprintf(b, sizeof b, "%s(%d,%d%s)", pts.empty() ? "" : " ",
                                      p.cellX, p.cellZ,
                                      p.clipId ? ("/c" + std::to_string(p.clipId)).c_str() : "");
                        pts += b;
                    }
                    std::printf("%-10s floor %zu  id %-3u flags 0x%x%s  %2u pts  %s\n",
                                n.c_str(), fi, w.id, w.flags,
                                w.pingPong() ? " PING-PONG" : "", w.len, pts.c_str());
                }
            }
        }
        std::printf("\nroutes %d  ping-pong %d  rings %d  lengths %d..%d  points with a clip %d\n",
                    tot, ping, rings, minLen, maxLen, clips);
        // ...and the lookups, on the supermarket - the one arena this port can
        // reach in play (`todo/handoff-shoot-mode.md` §1).
        omk::Map2d sm;
        if (sm.loadFile(root + "/MAP2D/smarket1.mpt")) {
            // NEAREST-FREE from a cell beside route 1's first point (19,39)
            const int a = sm.routeFor(0, 19, 38, 0);
            const int byId = sm.routeFor(0, 0, 0, 2);
            std::printf("smarket1 nearest to (19,38): route %d (id %u); by id 2: route %d (id %u)\n",
                        a, a >= 0 ? sm.floors()[0].waypoints[static_cast<std::size_t>(a)].id : 0,
                        byId, byId >= 0 ? sm.floors()[0].waypoints[static_cast<std::size_t>(byId)].id : 0);
            // take it, and the SAME search must now skip it
            float wx = 0, wz = 0;
            const int clip = sm.routePoint(0, a, 0, wx, wz);
            const int again = sm.routeFor(0, 19, 38, 0);
            std::printf("point 0 of route %d is %.0f %.0f (clip %d); taken %d, the same search now "
                        "gives route %d (id %u); by id gives %d\n", a, double(wx), double(wz), clip,
                        int(sm.routeTaken(0, a)), again,
                        again >= 0 ? sm.floors()[0].waypoints[static_cast<std::size_t>(again)].id : 0,
                        sm.routeFor(0, 0, 0, static_cast<int>(
                            sm.floors()[0].waypoints[static_cast<std::size_t>(a)].id)));
            sm.routeRelease(0, a);
            std::printf("released: taken %d, the search gives route %d\n",
                        int(sm.routeTaken(0, a)), sm.routeFor(0, 19, 38, 0));
            // the WALK round the ring, and then a ping-pong one
            std::string ring;
            int idx = 0;
            for (int k = 0; k < 6; ++k) {
                ring += std::to_string(idx);
                ring += " ";
                idx = sm.routeNextIndex(0, a, idx);
            }
            std::printf("route %d (4 points) walks %s\n", a, ring.c_str());
        }
        omk::Map2d hm;
        if (hm.loadFile(root + "/MAP2D/hames.mpt")) {
            // floor 3 id 6 is one of the two PING-PONG routes, 2 points
            const int r = hm.routeFor(3, 0, 0, 6);
            std::string walk;
            int idx = 0;
            for (int k = 0; k < 7; ++k) {
                walk += std::to_string(idx);
                walk += " ";
                idx = hm.routeNextIndex(3, r, idx);
            }
            std::printf("hames floor 3 route %d (id 6, ping-pong, 2 points) walks %s\n",
                        r, walk.c_str());
        }
        return 0;
    }

    if (all) {
        int floors = 0, doors = 0, wp = 0, segs = 0, fits = 0;
        std::map<int, long> hist;
        for (const auto& n : names) {
            omk::Map2d m;
            if (!m.loadFile(root + "/MAP2D/" + n + ".mpt")) {
                std::printf("%-10s COULD NOT READ\n", n.c_str());
                continue;
            }
            for (std::size_t i = 0; i < m.floors().size(); ++i) {
                const auto& f = m.floors()[i];
                ++floors;
                segs += static_cast<int>(f.links.size());
                wp   += static_cast<int>(f.waypoints.size());
                for (auto c : f.cells) ++hist[c];
                for (auto c : f.cells)
                    if ((c & omk::Map2d::kDoorBit) && !omk::Map2d::blockedValue(c)) ++doors;
                const float dx = (f.bound[1] - f.bound[0]) - static_cast<float>(f.w * m.scale());
                const float dz = (f.bound[5] - f.bound[4]) - static_cast<float>(f.h * m.scale());
                if (std::abs(dx) <= static_cast<float>(m.scale()) &&
                    std::abs(dz) <= static_cast<float>(m.scale())) ++fits;
                std::printf("%-10s floor %2zu  %3ux%-3u cell %3u  y %8.0f  segs %2zu  wp %2zu\n",
                            n.c_str(), i, f.w, f.h, m.scale(), static_cast<double>(f.bound[3]),
                            f.links.size(), f.waypoints.size());
            }
        }
        std::printf("\nfloors %d  segments %d  waypoints %d  door cells %d"
                    "  bound-fits-grid %d/%d\n", floors, segs, wp, doors, fits, floors);
        // the refusal set MEASURED, not described: every byte 0..255 put
        // through the port's own test, so a check asserts the rule rather
        // than a sentence about it
        std::printf("refused");
        for (int v = 0; v < 256; ++v)
            if (omk::Map2d::blockedValue(static_cast<std::uint8_t>(v))) std::printf(" %d", v);
        std::printf("\n");
        std::printf("cell histogram:");
        for (const auto& kv : hist) std::printf("  %d:%ld", kv.first, kv.second);
        std::printf("\n");
        return 0;
    }

    const std::string name = argc > 2 ? argv[2] : "gallery";
    const int want = argc > 3 ? std::atoi(argv[3]) : 0;
    omk::Map2d m;
    if (!m.loadFile(root + "/MAP2D/" + name + ".mpt")) {
        std::fprintf(stderr, "cannot read %s\n", name.c_str());
        return 1;
    }
    if (want < 0 || static_cast<std::size_t>(want) >= m.floors().size()) {
        std::fprintf(stderr, "%s has %zu floors\n", name.c_str(), m.floors().size());
        return 1;
    }
    const auto& f = m.floors()[static_cast<std::size_t>(want)];
    std::printf("%s floor %d: %ux%u cells of %u units (%.1f m), x %.0f..%.0f  "
                "y %.0f..%.0f  z %.0f..%.0f\n", name.c_str(), want, f.w, f.h, m.scale(),
                m.scale() / 39.37, static_cast<double>(f.bound[0]), static_cast<double>(f.bound[1]),
                static_cast<double>(f.bound[2]), static_cast<double>(f.bound[3]),
                static_cast<double>(f.bound[4]), static_cast<double>(f.bound[5]));
    for (int i = 0; i < 16; ++i)
        if (f.doors[i].used())
            std::printf("  door slot %2d  arm %d  open object %d  close object %d\n",
                        i, f.doors[i].arm, f.doors[i].openObject, f.doors[i].closeObject);
    for (std::size_t i = 0; i < f.waypoints.size(); ++i)
        std::printf("  waypoint %2zu  len %u  cell %d,%d\n", i, f.waypoints[i].len,
                    f.waypoints[i].cellX(), f.waypoints[i].cellZ());
    for (std::uint32_t z = 0; z < f.h; ++z) {
        std::printf("%3u ", z);
        for (std::uint32_t x = 0; x < f.w; ++x) {
            const std::uint8_t c = f.cell(static_cast<int>(x), static_cast<int>(z));
            char ch = '?';
            if (omk::Map2d::blockedValue(c)) ch = '#';
            else if (c == 1) ch = '.';
            else if (c & omk::Map2d::kDoorBit) ch = 'D';
            std::putchar(ch);
        }
        std::putchar('\n');
    }
    return 0;
}
