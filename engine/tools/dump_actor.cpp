// SPDX-License-Identifier: GPL-3.0-or-later
// The actor runtime's `.CTL` half - the combat block and the transition
// fields - for the differential against verify.py's `ctl combat block` and
// `ctl transitions`.
//
//     dump_actor <gamedata/ANIMS> <out.bin>
//
// This is the data the fight AI already ported drives: a profile injects
// input words into the actor's queue, `Cef_FindTransition` matches them
// against the entries' `+4` codes, and a landed hit is resolved through the
// combat block by `Fight_ResolveHit`.
//
// out.bin: int32 combatBlocks, reactionRefs, refsResolving, idCollisions,
//          int32 damageMin, damageMax, groups, groupsWithOneDefault, roleOkFiles,
//          int32 entries, withInputCode, noInputSentinel, withCancelWindow,
//          int32 malformedWindows, priorityCount, then each priority
#include "formats/ctl.h"
#include "platform/datafs.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: dump_actor <gamedata/ANIMS> <out.bin>\n");
        return 2;
    }
    const omk::DataFs fs(argv[1]);
    auto files = fs.list(".", ".CTL");
    std::sort(files.begin(), files.end());

    long blocks = 0, refs = 0, resolve = 0, collisions = 0;
    std::int32_t dmgLo = 99, dmgHi = 0;
    long groups = 0, oneDefault = 0, roleOk = 0;
    long entries = 0, coded = 0, sentinel = 0, windows = 0, badWindows = 0;
    std::set<int> priorities;

    for (const auto& path : files) {
        const auto f = omk::readCtl(omk::DataFs::readPath(path));
        if (!f.valid) continue;

        // the low-16 id space, which is what a reaction reference names
        std::map<std::uint32_t, int> ids16;
        for (const auto& st : f.states) ++ids16[st.id & 0xFFFFu];
        for (const auto& [id, n] : ids16) { (void)id; if (n > 1) ++collisions; }

        // exactly one flag-0x20 default entry per group
        int ngroups = 0;
        std::map<int, int> defaults;
        for (const auto& st : f.states) {
            ngroups = std::max(ngroups, st.group + 1);
            if (st.flags & 0x20u) ++defaults[st.group];
        }
        groups += ngroups;
        for (int g = 0; g < ngroups; ++g) if (defaults[g] == 1) ++oneDefault;

        // Fight_Begin's six cached role codes: a combat file has exactly one
        // entry of each, and a non-combat file has none. That is the test the
        // role field could fail, and it is content rather than layout.
        bool combatFile = false;
        std::map<int, int> roles;
        for (const auto& st : f.states) {
            if (st.hasCombat) combatFile = true;
            for (int r : {3, 4, 5, 9, 18, 20}) if (st.role == r) ++roles[r];
        }
        bool ok = true;
        for (int r : {3, 4, 5, 9, 18, 20})
            if (combatFile ? roles[r] != 1 : roles[r] != 0) ok = false;
        if (ok) ++roleOk;

        for (const auto& st : f.states) {
            ++entries;
            if (st.inputCode) ++coded;
            if (st.inputCode == 0x80000000u) ++sentinel;
            if (st.cancelFrom != 0.0f || st.cancelTo != 0.0f) {
                ++windows;
                if (!(st.cancelFrom >= 0.0f && st.cancelFrom <= st.cancelTo &&
                      st.cancelTo <= 1000.0f)) ++badWindows;
            }
            priorities.insert(st.priority);
            if (!st.hasCombat) continue;
            ++blocks;
            dmgLo = std::min(dmgLo, st.combat.damage());
            dmgHi = std::max(dmgHi, st.combat.damage());
            for (auto r : {st.combat.reactionA(), st.combat.reactionB()}) {
                if (r == -1) continue;
                ++refs;
                if (ids16.count(static_cast<std::uint32_t>(r) & 0xFFFFu)) ++resolve;
            }
        }
    }

    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    for (long v : {blocks, refs, resolve, collisions})
        put32(static_cast<std::int32_t>(v));
    put32(dmgLo); put32(dmgHi);
    for (long v : {groups, oneDefault, roleOk, entries, coded, sentinel,
                   windows, badWindows})
        put32(static_cast<std::int32_t>(v));
    put32(static_cast<std::int32_t>(priorities.size()));
    for (int p : priorities) put32(p);
    if (!omk::safeOutputPath(argv[2])) return 2;
    std::ofstream out(argv[2], std::ios::binary);
    out.write(reinterpret_cast<const char*>(o.data()),
              static_cast<std::streamsize>(o.size()));

    std::printf("combat: %ld blocks, %ld reaction refs of which %ld resolve, "
                "%ld low-16 id collisions; damage %d..%d\n",
                blocks, refs, resolve, collisions, dmgLo, dmgHi);
    std::printf("groups: %ld, of which %ld have exactly one default entry; "
                "%ld files whose role codes are exactly right\n",
                groups, oneDefault, roleOk);
    std::printf("transitions: %ld entries, %ld with an input code (%ld the "
                "no-input sentinel), %ld with a cancel window (%ld malformed); "
                "priorities", entries, coded, sentinel, windows, badWindows);
    for (int p : priorities) std::printf(" %d", p);
    std::printf("\n");
    return 0;
}
