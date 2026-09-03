// SPDX-License-Identifier: GPL-3.0-or-later
//
// `tab_special_move[]` READ AGAINST THE SHIPPED BANKS.
//
// `engine/src/actor/moves.h` turns the table into rows a dispatch can look a
// fired move up in. This exercises it the only way that means anything: every
// move NAME the shipped `.CTL` files actually carry must resolve to a row.
//
// The table is the engine's own 66 entries; the banks name a subset. A name in
// a bank that resolves to NOTHING would be a move the engine can fire and the
// port cannot attribute - which is what the port did until 2026-09-04, when
// nothing consumed `ChannelEvent::Kind::Move` at all (omk-play 66).
//
// It also prints the five rows the port now consumes, so a reader can see
// which handlers are wired: 0 MDSNEAK0 (the sneak) and 3..6 MDACTION,
// MDGETOBJ, MDLETOBJ, MDPUTSNK (the world take).
#include "actor/moves.h"
#include "formats/ctl.h"
#include "platform/datafs.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: moves_used <gamedata> <special_moves.json>\n");
        return 2;
    }
    const omk::DataFs fs(argv[1]);
    const auto table = omk::SpecialMoves::loadJson(argv[2]);
    if (!table.valid()) { std::fprintf(stderr, "table did not load\n"); return 1; }

    static const char* kBanks[] = {"H1AVNT", "F1AVNT", "H1CMBT", "F1CMBT",
                                   "D1CMBT", "MECA", "SHAM"};
    std::set<std::string> used, unresolved;
    int banks = 0, entries = 0, withMove = 0;
    for (const char* b : kBanks) {
        const auto p = fs.resolve(std::string("ANIMS/") + b + ".CTL");
        if (!p) continue;
        const auto ctl = omk::readCtl(omk::DataFs::readPath(*p));
        if (!ctl.valid) continue;
        ++banks;
        for (const auto& st : ctl.states) {
            ++entries;
            if (st.moveName.empty()) continue;
            ++withMove;
            used.insert(st.moveName);
            if (!table.find(st.moveName)) unresolved.insert(st.moveName);
        }
    }
    std::printf("%zu rows in the table; %d banks, %d entries, %d naming a move\n",
                table.size(), banks, entries, withMove);
    std::printf("%zu distinct names used, %zu of them UNRESOLVED\n",
                used.size(), unresolved.size());
    for (const auto& u : unresolved) std::printf("  unresolved: %s\n", u.c_str());
    std::printf("the rows the port consumes:\n");
    for (const char* w : {"MDSNEAK0", "MDACTION", "MDGETOBJ", "MDLETOBJ", "MDPUTSNK"}) {
        if (const auto* r = table.find(w))
            std::printf("  [%2d] %-9s 0x%08x\n", r->index, r->name.c_str(), r->handler);
        else
            std::printf("  [--] %-9s MISSING FROM THE TABLE\n", w);
    }
    std::printf("%zu %d %zu %zu\n", table.size(), banks, used.size(), unresolved.size());
    return 0;
}
