// SPDX-License-Identifier: GPL-3.0-or-later
// Does a running scene program SURVIVE a `scene.load` on the same area?
//
// It must. The `.SCX` belongs to the AREA (`+97`), and `Area_LoadScx`
// (0x0041B4E0) - the only thing that fills the slot's object container - has
// two callers, `Area_TickLoad` and `Game_Init`. `Scene_LoadSCX` has four call
// sites in the binary and the `scene.load` opcode is none of them: handler
// 0x403950 calls `Scene_Load` (sub_40C120), which brings in the SCENE CHUNK -
// startup script, zones, props - and never touches the object pool.
//
// So the environment goes on animating through a cutscene, which is what a
// play report says the original does (`todo/omk-play.md` 52). This probe runs
// a program, loads a scene over the SAME area, and checks the clock kept
// counting; then changes the AREA, which must rebuild.
#include "script/area.h"
#include "platform/datafs.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr,
            "usage: scene_survive <gamedata> <vm_opcodes.json> <START> <SCPTDATA> <out.bin>\n");
        return 2;
    }
    const std::string data = argv[1];
    const auto table = omk::OpcodeTable::loadJson(argv[2]);
    const std::string outPath = argv[5];
    if (!omk::safeOutputPath(outPath)) return 2;

    auto state = omk::GameState::fromFile(argv[3]);
    omk::Session s(data + "/IAM", state, table);
    // AREA 222 is the Impasse; scene 55 plays over it. `loadArea` is what
    // makes it RESIDENT - `sceneLoad` walks the two slots and does nothing
    // for an area in neither, so a probe that only calls `loadScene` tests
    // nothing at all (this one did, and the mutation went unnoticed).
    if (!s.loadScene(argv[4], omk::ChunkKind::Area, 222)) {
        std::fprintf(stderr, "cannot make the Impasse resident\n");
        return 1;
    }
    s.loadArea(222);
    for (int i = 0; i < 64 && s.loading(); ++i) s.frame();
    const int resident = s.currentArea() == 222 ? 1 : 0;
    const std::string file0 = s.scene().file();

    // `scx.play 259` - `C_1_BoxMoves`, the four crates, which
    // `engine: scene steps` already follows. Deliberately NOT an
    // `scx.play.actor` object: `sceneLoad` hands the outgoing scene's ACTOR
    // slots back (`sub_40BEC0`), so an actor-driven program is entitled to
    // stop and would not test this. The crates move meshes of the SET, which
    // is what an environment animation is.
    s.sceneMutable().handle({omk::Call{58, {259, 0, 0}}});
    for (int f = 0; f < 30; ++f) s.sceneMutable().tick(1.0f);

    int idx = -1;
    for (std::size_t i = 0; i < s.scene().programCount(); ++i)
        if (s.scene().programRunning(static_cast<int>(i))) { idx = static_cast<int>(i); break; }
    const float clockBefore = idx < 0 ? -1.0f : s.scene().programClock(idx);
    const int   pcBefore    = idx < 0 ? -1 : s.scene().programPc(idx);
    const int   runBefore   = s.scene().programsRunning();

    // ---- a scene load over the SAME area: nothing may be disturbed -------
    s.sceneLoad(222, 57);
    const int   runAfter   = s.scene().programsRunning();
    const float clockAfter = idx < 0 ? -1.0f : s.scene().programClock(idx);
    const int   sameFile   = s.scene().file() == file0 ? 1 : 0;

    // ...and it must go on advancing rather than merely having survived.
    for (int f = 0; f < 10; ++f) s.sceneMutable().tick(1.0f);
    const float clockLater = idx < 0 ? -1.0f : s.scene().programClock(idx);

    // ---- the CONTROL: the runner is keyed on the AREA -------------------
    // `sceneLoad(0, -1)` is NOT the control it looks like: area 0 is not
    // resident, and the engine's own slot walk changes only the DB field for
    // an area in neither slot ("on any other area only the DB changes"), so
    // it correctly does nothing. The rebuild-on-area-change branch is walked
    // by the real transitions in `engine: area load` and `sim: area load`.
    // What is asserted here is the thing this fix rests on: a DIFFERENT area
    // is a different `.SCX` and a fresh runner, so keeping the old one across
    // an area change would be visibly wrong.
    omk::Session other(data + "/IAM", state, table);
    other.loadScene(argv[4], omk::ChunkKind::Area, 0);      // AREA 0, Anekbah
    const int rebuiltFile = other.scene().file() != file0 ? 1 : 0;
    const int rebuiltRun  = other.scene().programsRunning();

    const std::int32_t out[11] = {
        resident,                                     // AREA 222 really resident
        idx >= 0,                                     // a program was running
        runBefore, runAfter,                          // survived the load
        static_cast<std::int32_t>(clockBefore * 100.0f + 0.5f),
        static_cast<std::int32_t>(clockAfter  * 100.0f + 0.5f),
        static_cast<std::int32_t>(clockLater  * 100.0f + 0.5f),
        sameFile, pcBefore, rebuiltFile, rebuiltRun,
    };
    std::ofstream f(outPath, std::ios::binary);
    f.write(reinterpret_cast<const char*>(out), sizeof out);
    std::printf("resident %d\n", resident);
    std::printf("program %d: clock %.1f -> %.1f (load) -> %.1f (+10 frames)\n",
                idx, clockBefore, clockAfter, clockLater);
    std::printf("running %d -> %d, same file %d ('%s')\n",
                runBefore, runAfter, sameFile, s.scene().file().c_str());
    std::printf("another area: different file %d ('%s'), programs running %d\n",
                rebuiltFile, other.scene().file().c_str(), rebuiltRun);
    return 0;
}
