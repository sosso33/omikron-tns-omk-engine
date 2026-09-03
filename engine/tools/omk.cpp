// SPDX-License-Identifier: GPL-3.0-or-later
// `omk` - the replica, booted.
//
//     omk <gamedata> [--tables <dir>] [NOFMV] [WINDOW] [--frames N] [--scenes]
//
// The engine's own chain, headless: parse the command line the way `WinMain`
// does, step through the three FLIS movies, `Game_Start("aventure.scx")`, then
// `Game_RunLoop`'s idle path calling `Game_Frame`.
//
// This is the first program in this tree that is an ENGINE rather than a
// library with tests. Everything it uses was proved in slices; what it adds is
// that they fit together.
#include "platform/boot.h"
#include "ui/iamtext.h"
#include "ui/widgets.h"
#include "ui/text.h"
#include "platform/datafs.h"

#include <cstdio>
#include <fstream>
#include <vector>
#include <string>

int main(int argc, char** argv) {
    auto o = omk::parseCommandLine(argc, argv);
    if (o.root.empty()) {
        std::fprintf(stderr, "usage: omk <gamedata> [--tables <dir>] [NOFMV] "
                             "[WINDOW] [--frames N] [--scenes]\n");
        return 2;
    }
    if (o.tables.empty()) o.tables = o.root + "/../tables";

    // `--dump <file>`: the decisions this boot announced, for the differential
    // against traces/intro.log. The point of dumping from HERE rather than
    // from a harness is that nothing is hand-wired - it is what the engine
    // decides when it is simply run.
    std::string dump;
    for (int i = 1; i < argc - 1; ++i)
        if (std::string(argv[i]) == "--dump") dump = argv[i + 1];

    const auto r = omk::boot(o);
    if (!r.ok) { std::fprintf(stderr, "boot failed: %s\n", r.why.c_str()); return 1; }

    std::printf("Omikron - the replica, booted %s\n",
                o.windowed ? "windowed" : "fullscreen");
    if (r.moviesSkipped)
        std::printf("  movies   skipped (NOFMV)\n");
    else
        std::printf("  movies   %d of 3 present in FLIS/\n", r.moviesFound);
    std::printf("  boot     Game_Start(\"%s\") - %d effect sprites, "
                "%d sounds (the GLOBAL library, not a menu)\n",
                r.bootScene.empty() ? "<missing>" : r.bootScene.c_str(),
                r.bootSprites, r.bootSounds);
    std::printf("  menu     the script asked for screen %d -> variable %d; "
                "walking it answered %d\n",
                r.uiScreen, r.uiVariable, r.interfaceAnswer);
    std::printf("  area     %d, from IAM/START +1414\n", r.startArea);
    std::printf("  loop     %d startup contexts, %d frames, %d conversations, "
                "%d areas entered, %zu decisions announced\n",
                r.startupContexts, r.framesRun, r.conversations,
                r.areasEntered, r.announced.size());

    // and the start menu with its labels resolved, which is what the IAM text
    // archives are for - without them every item is a number
    auto w = omk::UiWidgets::loadJson(o.tables + "/ui_widgets.json");
    w.loadScreens(o.tables + "/ui.json");
    const omk::DataFs iam(o.root + "/IAM");
    const auto strings = omk::iamStrings(iam, w.textFile(29));
    std::printf("  screen 29 (%s): ", w.textFile(29).c_str());
    if (const auto* p = w.screen(29)) {
        bool first = true;
        for (const auto& l : p->lists)
            for (const auto& it : l.items) {
                if (it.string < 0 ||
                    static_cast<std::size_t>(it.string) >= strings.size()) continue;
                if (!it.selectable(l.broadcast)) continue;
                std::printf("%s%s", first ? "" : " | ",
                            strings[static_cast<std::size_t>(it.string)].c_str());
                first = false;
            }
    }
    std::printf("\n");

    if (!dump.empty()) {
        std::vector<std::uint8_t> b;
        const auto put32 = [&b](std::int32_t v) {
            const auto u = static_cast<std::uint32_t>(v);
            for (int k = 0; k < 4; ++k)
                b.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
        };
        put32(r.moviesFound); put32(r.moviesSkipped ? 1 : 0);
        put32(r.bootScene.empty() ? 0 : 1);
        put32(r.bootSprites); put32(r.bootSounds);
        put32(r.interfaceAnswer); put32(r.uiScreen); put32(r.uiVariable);
        put32(r.startArea);
        put32(r.startupContexts); put32(r.conversations); put32(r.areasEntered);
        put32(static_cast<std::int32_t>(r.announced.size()));
        for (const auto& [dom, v] : r.announced) {
            b.push_back(static_cast<std::uint8_t>(dom.size()));
            for (char c : dom) b.push_back(static_cast<std::uint8_t>(c));
            put32(v);
        }
        std::ofstream f(dump, std::ios::binary);
        f.write(reinterpret_cast<const char*>(b.data()),
                static_cast<std::streamsize>(b.size()));
    }
    return 0;
}
