// SPDX-License-Identifier: GPL-3.0-or-later
//
// `Script_StopSound` (0x004A16D0) - the counterpart `Script_PlaySound` never
// had. 86 sites across 61 scenes started audio the port could not silence,
// which is a reader's "audio not stopping" (omk-play 71).
//
// The tunnel between Anekbah and Qalisar carries the shipped pair and is the
// clearest example in the game: `Tunnel01.SCX` object 10 is `AmbianceSound`
// (one `Script_PlaySound`) and object 11 is `stopambsound` (one
// `Script_StopSound`), naming the same sound. This starts each and reports the
// cues the scene runner produced.
//
//     stopsound_probe <gamedata> <vm_opcodes.json> <START> <SCPTDATA> <out.bin>
#include "script/area.h"
#include "platform/datafs.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr,
            "usage: stopsound_probe <gamedata> <vm_opcodes.json> <START> <SCPTDATA> <out.bin>\n");
        return 2;
    }
    if (!omk::safeOutputPath(argv[5])) return 2;
    const std::string data = argv[1];
    const auto table = omk::OpcodeTable::loadJson(argv[2]);
    auto state = omk::GameState::fromFile(argv[3]);
    omk::Session s(data + "/IAM", state, table);
    s.loadScene(argv[4], omk::ChunkKind::Area, 224);       // TUNELAQ1 -> Tunnel01.SCX
    s.loadArea(224);
    for (int i = 0; i < 64 && s.loading(); ++i) s.frame();

    std::printf("area %d  scx '%s'\n", s.currentArea(), s.scene().file().c_str());

    // start `AmbianceSound` (10) then `stopambsound` (11), a frame each, and
    // count what came out. `scx.play.wait`'s shape: object first.
    int plays = 0, stops = 0, loops = 0, stopWav = -1, playWav = -1;
    for (const int obj : {10, 11}) {
        omk::Call c;
        c.op = 58;
        c.fields = {static_cast<std::int16_t>(obj), 0, 0};
        const int idx = s.sceneMutable().handle({c});
        std::printf("  object %d -> program %d\n", obj, idx);
        for (int f = 0; f < 4; ++f) {
            s.frame();
            for (const auto& fs : s.scene().sounds()) {
                if (fs.cue.stop) {
                    ++stops;
                    stopWav = fs.cue.wav;
                } else {
                    ++plays;
                    playWav = fs.cue.wav;
                    if (fs.cue.loop) ++loops;
                }
            }
        }
    }
    std::printf("play cues %d (looping %d, wav %d), STOP cues %d (wav %d)\n",
                plays, loops, playWav, stops, stopWav);
    const int same = (stops > 0 && stopWav == playWav) ? 1 : 0;
    std::printf("the stop names the SAME sound the play started: %d\n", same);

    std::ofstream o(argv[5], std::ios::binary);
    const int rec[5] = {plays, loops, stops, stopWav, same};
    o.write(reinterpret_cast<const char*>(rec), sizeof rec);
    return 0;
}
