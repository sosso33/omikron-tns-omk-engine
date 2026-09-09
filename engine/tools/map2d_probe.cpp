// SPDX-License-Identifier: GPL-3.0-or-later
// THE SHOOT AI'S NAVIGATION GRID, read and DRAWN.
//
//     map2d_probe <gamedata> [map] [floor]      e.g. map2d_probe ../gamedata gallery 0
//     map2d_probe <gamedata> --all              every map, one line per floor
//     map2d_probe <gamedata> --sight            the LINE OF SIGHT, every map
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

#include <cstdio>
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
    const std::vector<std::string> names = {
        "archiv03", "archiv05", "astaroth", "bar56", "CSlev-3", "gallery",
        "grotte", "hames", "smarket1", "soukdock", "soukt", "tetra2",
        "tetra3", "tetra4", "tetradou", "yrmali"};

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
                segs += static_cast<int>(f.segments.size());
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
                            f.segments.size(), f.waypoints.size());
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
