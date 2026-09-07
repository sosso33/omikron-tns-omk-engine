// SPDX-License-Identifier: GPL-3.0-or-later
// HOW LONG A CAMERA EDITING DRIVES THE CAMERA, run over the Impasse cutscene.
//
// `Script_PlayScript` computes `ediPlaying` before it walks the object's
// function chain, returns `ediPlaying + busy`, and stops the object only under
// `if (!(ediPlaying + busy))` - so an object whose steps have all run out goes
// on running, and goes on advancing the clock the editing is sampled at, until
// the editing's own `+24` duration expires. A shot is as long as its editing
// says, never as long as the animation happens to be.
//
// The replica required the program to be RUNNING, so `C_1_BoxMoves` - whose
// steps end at frame 110 of a 185-frame editing - dropped the last 75 frames
// of its shot and left a 73-frame hole before the next beat, which drew as
// black (docs/CUTSCENES.md, "What happens when the editing ENDS").
//
// The invariant is on the CLOCK, not on a frame count: an observer outside the
// frame cannot count the shot's frames, because the object's first tick happens
// inside the frame that starts it and a beat hand-over can cost another. What
// is exact is where the shot ENDS - `activeEditing()` must stop naming an
// editing on the tick its program clock reaches the editing's duration, and on
// no earlier one.
//
//     editing_hold <gamedata> <vm_opcodes.json> <START> <SCPTDATA> <out.bin>
//
// out.bin: int32 editings that ran to an end, int32 of those ending exactly at
//          their duration, int32 boxblow's end clock, int32 boxblow's duration.
#include "script/area.h"
#include "platform/datafs.h"
#include <cstdio>
#include <fstream>
#include <map>
#include <string>

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr,
            "usage: editing_hold <gamedata> <vm_opcodes.json> <START> <SCPTDATA> <out.bin>\n");
        return 2;
    }
    if (!omk::safeOutputPath(argv[5])) return 2;
    const std::string data = argv[1];
    const auto table = omk::OpcodeTable::loadJson(argv[2]);
    auto state = omk::GameState::fromFile(argv[3]);
    omk::Session s(data + "/IAM", state, table);
    // The beats are a CHAIN: `scx.play.wait` parks the startup script on each
    // object, so without these the sixteen run concurrently and rebind one
    // another's actors. `omk-play` sets both for the same reason.
    s.setCameraWait(true);
    s.setObjectWait(true);
    s.loadScene(argv[4], omk::ChunkKind::Area, 222);
    s.loadArea(222);
    for (int i = 0; i < 64 && s.loading(); ++i) s.frame();
    s.sceneLoad(222, 55);

    struct Shot { std::string name; int duration = 0; float endClock = -1.0f;
                  int first = -1, last = -1; };
    std::map<int, Shot> seen;                 // by editing id
    int liveEditing = -1, liveProgram = -1;
    for (int f = 0; f < 2200; ++f) {
        s.frame();
        const auto* a = s.scene().activeEditing();
        // The tick the editing stops driving: its program's clock is the shot's
        // end, and it must be the duration exactly.
        if (liveEditing >= 0 && (!a || a->editing != liveEditing)) {
            seen[liveEditing].endClock = s.scene().programClock(liveProgram);
            liveEditing = -1;
        }
        if (a) {
            Shot& sh = seen[a->editing];
            if (sh.first < 0) { sh.first = f; sh.name = a->editingName;
                                sh.duration = static_cast<int>(a->duration); }
            sh.last = f;
            liveEditing = a->editing;
            liveProgram = a->program;
        }
    }
    int ended = 0, exact = 0;
    float boxEnd = -1.0f; int boxDur = 0;
    for (const auto& [id, sh] : seen) {
        if (sh.endClock < 0.0f) {
            std::printf("editing %3d '%-10s' frames %4d..%-4d  of %4d  still running\n",
                        id, sh.name.c_str(), sh.first, sh.last, sh.duration);
            continue;
        }
        ++ended;
        const bool ok = sh.endClock == static_cast<float>(sh.duration);
        if (ok) ++exact;
        if (sh.name == "boxblow") { boxEnd = sh.endClock; boxDur = sh.duration; }
        std::printf("editing %3d '%-10s' frames %4d..%-4d  ended at clock %6.1f of %4d  %s\n",
                    id, sh.name.c_str(), sh.first, sh.last,
                    static_cast<double>(sh.endClock), sh.duration, ok ? "" : "SHORT");
    }
    std::printf("%d editings ran to an end, %d of them at their duration exactly; "
                "boxblow ended at %.1f of %d\n",
                ended, exact, static_cast<double>(boxEnd), boxDur);
    const std::int32_t out[4] = {ended, exact, static_cast<std::int32_t>(boxEnd), boxDur};
    std::ofstream of(argv[5], std::ios::binary);
    of.write(reinterpret_cast<const char*>(out), sizeof out);
    return 0;
}
