// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/boot.h"

#include "platform/datafs.h"
#include "formats/scx.h"
#include "script/area.h"
#include "script/gamestate.h"
#include "ui/widgets.h"

#include <cstdlib>
#include <cstring>

namespace omk {

BootOptions parseCommandLine(int argc, char** argv) {
    BootOptions o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        // The engine's own two words, matched the way `03_win32.c:339` does -
        // a plain strcmp against the whole argument.
        if (a == "NOFMV" || a == "nofmv")   { o.playMovies = false; continue; }
        if (a == "WINDOW" || a == "window") { o.windowed = true; continue; }
        if (a == "--frames" && i + 1 < argc) { o.frames = std::atoi(argv[++i]); continue; }
        if (a == "--scenes") { o.scenes = true; continue; }
        if (a == "--no-derive-ui") { o.deriveUi = false; continue; }
        if (a == "--tables" && i + 1 < argc) { o.tables = argv[++i]; continue; }
        if (o.root.empty()) o.root = a;
    }
    return o;
}

BootReport boot(const BootOptions& o) {
    BootReport r;
    if (o.root.empty()) { r.why = "no gamedata/ directory given"; return r; }
    const DataFs fr(o.root);

    // --- Game_Init -------------------------------------------------------
    const auto table = OpcodeTable::loadJson(o.tables + "/vm_opcodes.json");
    if (!table.valid()) { r.why = "no VM opcode table"; return r; }

    // --- the three movies ------------------------------------------------
    //
    // Nothing decodes them; what is modelled is the SEQUENCE and the two
    // skips. NOFMV skips the block entirely, and at run time any key ends the
    // movie playing while left Alt latches and ends all three.
    if (o.playMovies) {
        for (const char* m : {"FLIS/EIDOS.mpg", "FLIS/QUANTIC.mpg",
                              "FLIS/GAME.mpg"})
            if (fr.resolve(m)) ++r.moviesFound;
        // NOTE the case: the executable spells these `.mpg` and the disc
        // ships `.MPG`, so the boot path is the FIRST thing in the game that
        // cannot work without a case-insensitive filesystem.
    } else {
        r.moviesSkipped = true;
    }

    // --- Game_Start("aventure.scx") --------------------------------------
    //
    // **This is not the menu, and not a location.** An earlier reading here
    // said "the main menu is a scene like any other, started by the same call
    // that starts a location" - inferred from the call without opening the
    // file. `aventure.scx` holds **20 effect sprites and 53 sound
    // registrations** and no menu logic whatever: the smoke, glows, impacts
    // and stars, plus the player's own footsteps, breathing, jumps and
    // eat/drink. It is the GLOBAL library, loaded once so every scene can
    // reference it - which is why it is 3 MB with a 39 KB block and why
    // `Game_Start` takes it before anything else.
    //
    // Where the menu is opened from is a separate question and is NOT
    // answered here.
    if (const auto p = fr.resolve("SCPTDATA/aventure.scx")) {
        r.bootScene = "aventure.scx";
        const auto d = DataFs::readPath(*p);
        const auto stream = readScxStream(d);
        r.bootSprites = static_cast<int>(stream.sprites.size());
        const auto block = readScx(d);
        // the sound registrations live in chunk 3; the block reader counts
        // records per tag
        const auto it = block.chunkCounts.find(3);
        r.bootSounds = it == block.chunkCounts.end() ? 0
                     : static_cast<int>(it->second);
    }

    auto state = GameState::fromFile(o.root + "/IAM/START");

    // --- Game_RunLoop ----------------------------------------------------
    Session s(o.root + "/IAM", state, table);
    // **The SCRIPT opens the menu, not this function.** AREA 118's startup
    // script reaches `ui.open(29, -1, -> variable 19)` at pc 1078, parks
    // there, and resumes into `dialog.start 272`. An earlier version of this
    // walked screen 29 HERE and seeded variable 19 before the script ran -
    // which produced the right number by doing the right thing in the wrong
    // place, and would have gone on producing it if the script had asked for
    // a different screen. Attaching the widget tree lets the Session answer
    // the `ui.open` the script actually reaches.
    UiWidgets widgets;
    if (o.deriveUi) {
        widgets = UiWidgets::loadJson(o.tables + "/ui_widgets.json");
        if (widgets.valid()) s.attachUi(widgets);
    }
    if (!s.loadAnnounceMap(o.tables + "/vm_announce.json")) {
        r.why = "no announce map";
        return r;
    }
    // WHICH area is in the DB, not in this file. `Game_NewGame` loads
    // `IAM\START` over a zeroed game database and applies it, and the block's
    // `+1414` is where the player is - 118, "Introduction Kay'l". Reading it
    // rather than naming it is the difference between booting the game and
    // booting a hard-coded level: an earlier version of this had the 118 as a
    // literal here while claiming nothing was hand-wired.
    r.startArea = state.currentArea();
    if (r.startArea < 0) { r.why = "IAM/START names no area"; return r; }
    if (o.scenes)
        s.loadScene(o.root + "/SCPTDATA", ChunkKind::Area, r.startArea);
    r.startupContexts = s.loadArea(r.startArea);

    // The idle path, bounded: a headless build has no message queue, so
    // `while (!PeekMessage())` becomes a frame budget. The delta is 1.0
    // because that is what `Game_Frame` computes at 30 fps.
    const int budget = o.frames > 0 ? o.frames : 200;
    for (int f = 0; f < budget; ++f) {
        s.frame();
        ++r.framesRun;
        if (s.dialogOpen()) { ++r.conversations; s.endDialog(); }
    }
    r.areasEntered = s.areasEntered();
    for (const auto& a : s.uiAnswers())
        if (a.derived) { r.interfaceAnswer = a.value; r.uiScreen = a.screen;
                         r.uiVariable = a.variable; }
    for (const auto& a : s.announced()) r.announced.push_back({a.domain, a.value});
    r.ok = true;
    return r;
}

}  // namespace omk
