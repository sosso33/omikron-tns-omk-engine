// SPDX-License-Identifier: GPL-3.0-or-later
// linesync_probe <gamedata> <vm_opcodes.json> - todo/drift-audit.md T2.
//
// A slow machine (the classic Mac at 6 fps) against a conversation line:
// frames arrive every 1/6 s of WALL time, the voice plays in real time, and
// `Game_Frame`'s delta is capped at 3 frames (0.1 s). Printed:
//   sync    - the simulated time after 60 such frames with the line sync
//             (`script/linesync.h`) and without it, and the voice's clock
//   face    - conversation 402's line position after the same frames, fed the
//             clock (`DialogPlayer::setLineClock`, what `sub_42D120` samples)
//             and not fed it
#include "formats/iam.h"
#include "platform/datafs.h"
#include "script/dialogue.h"
#include "script/gamestate.h"
#include "script/linesync.h"
#include "script/script.h"

#include <algorithm>
#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: linesync_probe <gamedata> <vm_opcodes.json>\n");
        return 2;
    }
    const std::string fr = argv[1];
    const auto table = omk::OpcodeTable::loadJson(argv[2]);
    if (!table.valid()) { std::fprintf(stderr, "no opcode table\n"); return 1; }

    const double wall = 1.0 / 6.0;                       // a 6 fps frame
    const double capped = std::min(wall, 3.0 / 30.0);    // `Game_Frame`'s clamp
    const int frames = 60;

    // ---- the simulation's time, with and without the sync
    omk::LineSync sync;
    double simSync = 0.0, simPlain = 0.0, last = 0.0;
    double first = -1.0;
    for (int f = 0; f < frames; ++f) {
        const double clock = f * wall;                   // the voice, in real time
        const double d = sync.step(clock, last, capped);
        if (f == 1) first = d;
        last = d;
        simSync += d;
        simPlain += capped;
    }
    std::printf("sync frames %d clock %.3f synced %.3f plain %.3f second_delta %.3f\n",
                frames, (frames - 1) * wall, simSync, simPlain, first);

    // ---- the line's face position, fed the clock and not
    auto state = omk::GameState::fromFile(fr + "/IAM/START");
    const auto file = omk::DataFs::readPath(fr + "/IAM/DIALOG");
    const auto arch = omk::IamArchive::open(file);
    const int id = 402;
    const auto chunk = arch.chunk(static_cast<std::size_t>(id));
    const auto conv = omk::parseConversation(id, chunk);
    double fed = -1.0, unfed = -1.0, len = 0.0;
    for (int run = 0; run < 2; ++run) {
        omk::DialogPlayer p(state, table);
        if (!p.open(conv, chunk, fr + "/MORPH")) { std::printf("face unplayable\n"); return 0; }
        len = p.lineSeconds();
        for (int f = 1; f <= 20; ++f) {
            if (run == 0) p.setLineClock(f * wall);
            p.tick(capped);
        }
        (run == 0 ? fed : unfed) = p.elapsed();
    }
    std::printf("face conversation %d line %.3f s after 20 frames: fed %.3f unfed %.3f clock %.3f\n",
                id, len, fed, unfed, 20 * wall);
    return 0;
}
