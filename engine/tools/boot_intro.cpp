// SPDX-License-Identifier: GPL-3.0-or-later
// Boot AREA 118 the way a new game does and print what it decides, so the
// sequence can be diffed against traces/intro.log - the real binary's own
// recording of the same opening.
//
//     boot_intro <gamedata/IAM> <vm_opcodes.json> <START> <out.bin> [--var N=V]
//                [--scenes <gamedata/SCPTDATA>] [--derive-ui <ui_widgets.json>]
//
// With `--scenes` the Session also makes each area's `.SCX` resident and runs
// its object programs, so the transition into the Impasse is followed by the
// scene that plays over it. Off by default because the existing differential
// is about the DECISIONS, and loading scenes would make it read 7 MB a hop.
//
// Conversations are closed as soon as they open: how LONG one lasts is not
// modelled (there is no dialogue UI), and it is the only thing supplied.
// Everything either side is the engine's own control flow.
#include "platform/datafs.h"
#include "script/area.h"
#include "ui/widgets.h"
#include "ui/widgets.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr,
            "usage: boot_intro <gamedata/IAM> <vm_opcodes.json> <START> <out.bin> [--var N=V]\n");
        return 2;
    }
    const std::string iam = argv[1];
    const auto table = omk::OpcodeTable::loadJson(argv[2]);
    auto state = omk::GameState::fromFile(argv[3]);
    for (int i = 5; i < argc; ++i)
        if (std::strncmp(argv[i], "--var", 5) == 0 && i + 1 < argc)
            if (const char* eq = std::strchr(argv[i + 1], '='))
                state.setVar(std::atoi(argv[i + 1]), std::atoi(eq + 1));

    const char* scenes = nullptr;
    const char* derive = nullptr;
    for (int i = 5; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], "--scenes") == 0)    scenes = argv[i + 1];
        if (std::strcmp(argv[i], "--derive-ui") == 0) derive = argv[i + 1];
    }

    omk::Session s(iam, state, table);
    // the announce map lives beside the opcode table
    std::string ann = argv[2];
    const auto slash = ann.find_last_of("/\\");
    ann = (slash == std::string::npos ? std::string() : ann.substr(0, slash + 1))
          + "vm_announce.json";
    if (!s.loadAnnounceMap(ann)) {
        std::fprintf(stderr, "cannot load %s\n", ann.c_str());
        return 1;
    }

    // `ui.open` now PARKS the context the way the handler parks its caller at
    // status 6, so the intro cannot run past pc 1078 of AREA 118's startup
    // script without something able to answer screen 29. Attaching the widget
    // tree is what answers it - by WALKING the screen, not by supplying a
    // number. Without it the run stops after three events, which is precisely
    // what `traces/menu-noinput.log` recorded the engine doing.
    omk::UiWidgets widgets;
    if (derive) {
        widgets = omk::UiWidgets::loadJson(derive);
        if (widgets.valid()) s.attachUi(widgets);
    }
    if (scenes) s.loadScene(scenes, omk::ChunkKind::Area, 118);
    const int queued = s.loadArea(118);

    int dialogs = 0;
    for (int f = 0; f < 200; ++f) {
        s.frame();
        if (s.dialogOpen()) { ++dialogs; s.endDialog(); }
    }

    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    if (scenes)
        std::printf("scenes: resident %s, %zu object programs started "
                    "(%zu unresolved), %d still running\n",
                    s.scene().file().empty() ? "<none>" : s.scene().file().c_str(),
                    s.scene().programCount(), s.scene().missed().size(),
                    s.scene().programsRunning());
    for (const auto& st : s.scene().started())
        std::printf("    scx.play.%-6s object %-4d %s\n",
                    st.how.c_str(), st.object, st.name.c_str());
    put32(queued);
    put32(dialogs);
    put32(s.areasEntered());
    put32(static_cast<std::int32_t>(s.announced().size()));
    for (const auto& a : s.announced()) {
        o.push_back(static_cast<std::uint8_t>(a.domain.size()));
        o.insert(o.end(), a.domain.begin(), a.domain.end());
        put32(a.value);
    }
    if (!omk::safeOutputPath(argv[4])) return 2;
    std::ofstream f(argv[4], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));
    std::printf("%d startup contexts, %d conversations, %d areas entered, "
                "%zu announcements\n",
                queued, dialogs, s.areasEntered(), s.announced().size());
    return 0;
}
