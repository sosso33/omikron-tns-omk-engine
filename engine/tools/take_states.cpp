// SPDX-License-Identifier: GPL-3.0-or-later
//
// omk-play 66: the world TAKE is a .CTL special move, not a script.
//
// `tab_special_move[]` rows 3..7 are MDACTION, MDGETOBJ, MDLETOBJ, MDPUTSNK,
// MDNOTAKE, and H1AVNT - the player's adventure bank - carries all of them.
// Pressing action in the port already fires MDACTION (state 0 -> 24); nothing
// carries the machine on to MDGETOBJ, because MDACTION's native handler is
// what scans for a nearby object and drives the next transition.
//
// This prints every entry whose move is one of those five, with the group it
// lives in, its input code and its parents - which is what says how the
// engine gets FROM the MDACTION state TO the take.
#include "formats/ctl.h"
#include "platform/datafs.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: take_states <gamedata> [bank]\n"); return 2; }
    const omk::DataFs fs(argv[1]);
    const std::string bank = argc > 2 ? argv[2] : "H1AVNT";
    const auto path = fs.resolve("ANIMS/" + bank + ".CTL");
    if (!path) { std::fprintf(stderr, "no ANIMS/%s.CTL\n", bank.c_str()); return 1; }
    const auto data = omk::DataFs::readPath(*path);
    const auto ctl = omk::readCtl(data);
    if (!ctl.valid) { std::fprintf(stderr, "%s did not parse\n", bank.c_str()); return 1; }

    std::printf("%s: %zu entries\n", bank.c_str(), ctl.states.size());
    const char* want[] = {"MDACTION", "MDGETOBJ", "MDLETOBJ", "MDPUTSNK", "MDNOTAKE"};
    for (std::size_t i = 0; i < ctl.states.size(); ++i) {
        const auto& s = ctl.states[i];
        bool hit = false;
        for (const char* w : want) if (s.moveName == w) hit = true;
        if (!hit) continue;
        std::printf("  entry %3zu  group %2d  id %u  '%s'  move %-9s flags %08x input %08x\n",
                    i, s.group, s.id, s.name.c_str(), s.moveName.c_str(), s.flags, s.inputCode);
        std::printf("             parents:");
        for (auto p : s.parents) std::printf(" %u", p);
        std::printf("\n             children:");
        for (auto c : s.children) std::printf(" %u", c);
        std::printf("\n");
    }
    // Which GROUP is id 45 - the one MDACTION installs - and what is its
    // DEFAULT ENTRY? `setBankGroup` goes straight to that entry, so if it is
    // not the MDGETOBJ one the take needs more than the group switch.
    std::printf("\ngroupList: %zu\n", ctl.groupList.size());
    for (std::size_t gi = 0; gi < ctl.groupList.size(); ++gi) {
        const auto& g = ctl.groupList[gi];
        if (g.id != 45) continue;
        const int de = g.defaultEntry;
        std::printf("group index %zu has id 45, flags %08x, defaultEntry %d\n",
                    gi, g.flags, de);
        if (de >= 0 && static_cast<std::size_t>(de) < ctl.states.size()) {
            const auto& st = ctl.states[static_cast<std::size_t>(de)];
            std::printf("   default entry %d: group %d move '%s' flags %08x input %08x\n",
                        de, st.group, st.moveName.c_str(), st.flags, st.inputCode);
        }
    }
    // group index 10's entries in full, with their children, since that is
    // where MDACTION lands and it carries no take move of its own
    std::printf("\ngroup index 10 (id 45) entries:\n");
    for (std::size_t j = 0; j < ctl.states.size(); ++j) {
        if (ctl.states[j].group != 10) continue;
        const auto& st = ctl.states[j];
        std::printf("   entry %3zu id %u move %-9s flags %08x input %08x clip %d\n",
                    j, st.id, st.moveName.empty() ? "-" : st.moveName.c_str(),
                    st.flags, st.inputCode, st.clip);
        std::printf("        children:");
        for (auto c : st.children) {
            std::printf(" %u", c);
            for (std::size_t k = 0; k < ctl.states.size(); ++k)
                if (ctl.states[k].id == c)
                    std::printf("(entry %zu, group %d, move %s)", k, ctl.states[k].group,
                                ctl.states[k].moveName.empty() ? "-" : ctl.states[k].moveName.c_str());
        }
        std::printf("\n");
    }

    // who leads INTO the MDGETOBJ entry: resolve its parents by id, and say
    // which group each lives in and what its default-entry status is
    for (std::size_t i = 0; i < ctl.states.size(); ++i) {
        if (ctl.states[i].moveName != "MDGETOBJ") continue;
        std::printf("\nMDGETOBJ entry %zu parents resolved:\n", i);
        for (auto pid : ctl.states[i].parents)
            for (std::size_t k = 0; k < ctl.states.size(); ++k)
                if (ctl.states[k].id == pid)
                    std::printf("   parent id %u = entry %zu, group %d, move '%s', "
                                "flags %08x input %08x clip %d\n",
                                pid, k, ctl.states[k].group,
                                ctl.states[k].moveName.c_str(), ctl.states[k].flags,
                                ctl.states[k].inputCode, ctl.states[k].clip);
        // and which group index 3 is, by id
        for (std::size_t gi = 0; gi < ctl.groupList.size(); ++gi)
            if (static_cast<int>(gi) == ctl.states[i].group)
                std::printf("   its group index %zu has id %d, defaultEntry %d\n",
                            gi, ctl.groupList[gi].id, ctl.groupList[gi].defaultEntry);
        break;
    }

    // every entry of whatever group the MDGETOBJ entry lives in
    for (std::size_t i = 0; i < ctl.states.size(); ++i) {
        if (ctl.states[i].moveName != "MDGETOBJ") continue;
        const int g = ctl.states[i].group;
        std::printf("\nMDGETOBJ entry %zu is in group index %d; that group's entries:\n", i, g);
        for (std::size_t j = 0; j < ctl.states.size(); ++j) {
            if (ctl.states[j].group != g) continue;
            std::printf("   entry %3zu move %-9s flags %08x input %08x\n",
                        j, ctl.states[j].moveName.empty() ? "-" : ctl.states[j].moveName.c_str(),
                        ctl.states[j].flags, ctl.states[j].inputCode);
        }
        break;
    }

    // MDNOTAKE (0x0046B530) is the dispatcher: it switches on the scan's
    // result code and installs a group BY ID - case 0 -> 140, 1 -> 6, 2 -> 7,
    // 3 -> 9. Which of those actually carries the take?
    for (int want : {140, 6, 7, 9, 41, 45}) {
        for (std::size_t gi = 0; gi < ctl.groupList.size(); ++gi) {
            if (ctl.groupList[gi].id != want) continue;
            const int de = ctl.groupList[gi].defaultEntry;
            std::printf("\ngroup id %3d = index %2zu, defaultEntry %d\n", want, gi, de);
            for (std::size_t j = 0; j < ctl.states.size(); ++j) {
                if (ctl.states[j].group != static_cast<int>(gi)) continue;
                std::printf("    entry %3zu move %-9s flags %08x input %08x clip %d%s\n",
                            j, ctl.states[j].moveName.empty() ? "-" : ctl.states[j].moveName.c_str(),
                            ctl.states[j].flags, ctl.states[j].inputCode, ctl.states[j].clip,
                            static_cast<int>(j) == de ? "   <- default" : "");
            }
        }
    }

    // the CLIPS by name, for the entries the take walks through
    std::printf("\nclips of the take path:\n");
    for (int e : {24, 51, 52, 53, 76, 77}) {
        if (e < 0 || static_cast<std::size_t>(e) >= ctl.states.size()) continue;
        const auto& st = ctl.states[static_cast<std::size_t>(e)];
        const char* cn = "(none)";
        if (st.clip >= 0 && static_cast<std::size_t>(st.clip) < ctl.clips.size())
            cn = ctl.clips[static_cast<std::size_t>(st.clip)].name.c_str();
        std::printf("   entry %3d  group %2d  move %-9s clip %3d '%s'\n",
                    e, st.group, st.moveName.empty() ? "-" : st.moveName.c_str(),
                    st.clip, cn);
    }

    // and what state 24 is, since that is where the press lands
    if (ctl.states.size() > 24) {
        const auto& s = ctl.states[24];
        std::printf("\nentry 24 (where the action press lands): group %d id %u '%s' move '%s'\n"
                    "  flags %08x input %08x\n  children:",
                    s.group, s.id, s.name.c_str(), s.moveName.c_str(), s.flags, s.inputCode);
        for (auto c : s.children) std::printf(" %u", c);
        std::printf("\n");
    }
    return 0;
}
