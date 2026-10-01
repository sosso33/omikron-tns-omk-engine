// SPDX-License-Identifier: GPL-3.0-or-later
// THE FRAME RATE - a fade lasts the same TIME at 30 and at 60 fps.
//
//     framerate_probe <gamedata/IAM> <vm_opcodes.json> <START>
//
// The engine's delta is `30 / fps` (`docs/BOOT.md` 4), and every clock it
// advances is in frames AT 30 Hz, so presenting faster must step each clock
// by less and leave every DURATION alone. This runs a Session's own frame -
// `Session::frame()`, the path the viewer takes, not `tickFades` called by
// hand - at 1/30 s and at 1/60 s and counts the frames each fade needs to
// end: the count doubles and the time does not (`todo/sixty-fps.md`).
//
// One line per fade and rate: `fade <kind> <fps>: <frames> frames, <ms> ms`.
#include "script/area.h"

#include <cstdio>

namespace {

int framesToEnd(omk::Session& s, bool black) {
    for (int n = 1; n <= 10000; ++n) {
        s.frame();
        const bool running = black ? s.blackFade().running() : s.colourFade().running();
        if (!running) return n;
    }
    return -1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: framerate_probe <IAM> <vm_opcodes.json> <START>\n");
        return 2;
    }
    const auto table = omk::OpcodeTable::loadJson(argv[2]);
    for (const int fps : {30, 60}) {
        // the BLACK fade: 133 after 132, mode 4, the fixed 60 frames, and it
        // CLEARS at its end (a 3 holds, so it would never report done)
        {
            auto state = omk::GameState::fromFile(argv[3]);
            omk::Session s(argv[1], state, table);
            s.setFrameSeconds(1.0 / fps);
            s.startBlackFade(true);
            s.startBlackFade(false);
            const int n = framesToEnd(s, true);
            std::printf("fade black %d: %d frames, %d ms\n", fps, n, n * 1000 / fps);
        }
        // the COLOUR fade: a "from" over 25 frames, the Impasse's own length,
        // which clears at its end
        {
            auto state = omk::GameState::fromFile(argv[3]);
            omk::Session s(argv[1], state, table);
            s.setFrameSeconds(1.0 / fps);
            s.startColourFade(2, 0x00FFFFFFu, 25.0f);
            const int n = framesToEnd(s, false);
            std::printf("fade colour %d: %d frames, %d ms\n", fps, n, n * 1000 / fps);
        }
    }
    return 0;
}
