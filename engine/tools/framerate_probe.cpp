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
//
// And the POSE between keys (`todo/enhancements.md` 12), on a synthetic
// one-bone clip - key 0 at rest, keys 1 and 2 turned 90 degrees about Y -
// so the arithmetic is tested without a model: `pose <smoothing> <frame>:
// <degrees>`, the bone's angle from rest. Off, a float frame is the
// truncated key, as `Anim_ApplyNodeFrame`'s `_ftol` makes it; on, it is
// slerped toward the next key, and holds at the last.
#include "actor/pose.h"
#include "formats/mesh3do.h"
#include "script/area.h"

#include <cmath>
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
    // ---- the pose between keys ----------------------------------------
    std::vector<omk::Mesh> meshes(1);
    meshes[0].index = 0; meshes[0].id = 1; meshes[0].parent = -1;
    omk::NodeTracks t;
    t.count = 1; t.frames = 3; t.rootTrack = 0; t.ids = {0};
    const float c45 = std::cos(0.7853981634f), s45 = std::sin(0.7853981634f);
    const omk::Quatf rest{}, y90{c45, 0, s45, 0};
    t.quats = {{rest}, {y90}, {y90}};
    const auto deg = [](const std::vector<omk::MeshPose>& p) {
        const float w = std::fabs(p[0].q.w) > 1.0f ? 1.0f : std::fabs(p[0].q.w);
        return 2.0 * std::acos(static_cast<double>(w)) * 180.0 / 3.14159265358979;
    };
    for (const bool on : {false, true}) {
        omk::setPoseSmoothing(on);
        for (const float f : {0.0f, 0.5f, 0.25f, 1.5f, 2.0f, 2.7f})
            std::printf("pose %s %.2f: %.1f\n", on ? "smooth" : "game", double(f),
                        deg(omk::composePose(meshes, t, f, false)));
        // an INTEGER caller is never blended, whatever the switch
        std::printf("pose %s int0: %.1f\n", on ? "smooth" : "game",
                    deg(omk::composePose(meshes, t, 0, false)));
    }
    omk::setPoseSmoothing(false);
    return 0;
}
