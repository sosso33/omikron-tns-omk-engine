// SPDX-License-Identifier: GPL-3.0-or-later
// THE TAKE's `.CTL` shape - what ends `H_WAITOB`, and what the take states are.
//
//     take_probe <gamedata/ANIMS/H1Avnt.CTL>
//
// The Enter-repeat bug is a consequence of the world TAKE being unported
// (`todo/next-tasks.md` 1): the action's bank switch lands the actor in
// `H_WAITOB` and only the take's own special move gets him out. This prints
// that corner of the graph so the slice can be planned against the data.
#include "platform/datafs.h"
#include "formats/ctl.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: take_probe <H1Avnt.CTL>\n"); return 2; }
    const auto d = omk::DataFs::readPath(argv[1]);
    const omk::CtlFile c = omk::readCtl(d);
    std::printf("%zu states, %zu groups\n", c.states.size(), c.groupList.size());
    for (std::size_t i = 0; i < c.states.size(); ++i) {
        const auto& s = c.states[i];
        const bool takeish = s.name.rfind("H_WAITOB", 0) == 0 ||
                             !s.moveName.empty();
        if (!takeish) continue;
        std::printf("  state %-3zu %-10s group %-2d flags 0x%08X clip %-4d "
                    "input 0x%08X move '%s'\n",
                    i, s.name.c_str(), s.group, s.flags, s.clip,
                    s.inputCode, s.moveName.c_str());
    }
    return 0;
}
