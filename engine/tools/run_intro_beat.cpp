// SPDX-License-Identifier: GPL-3.0-or-later
// THE BEAT BEFORE THE INTRO'S CONVERSATION - the ~5 seconds a reader reported
// missing, and where it comes from.
//
//     run_intro_beat <gamedata> <tables> <out.txt>
//
// AREA 118's startup script, after the menu answers, runs
//
//     camera.set 2172, 0        cut
//     camera.set 2148, 130      travel, 130 frames
//     character.show 310, 1
//     scx.play.actor.wait 310, 1     <- object 1 is "1KaylArrives"
//     scx.play.actor 310, 6          <- object 6 is "2KaylStand"
//     fade.from_color -1, 255, 45
//     dialog.start 272
//
// and the hold is opcode 60's: `ScriptObject_Start` is handed the caller's own
// slot instead of -1 and ends `mov [esi+16h], 4`, so the object's program
// finishing is what releases the script. Without it the conversation opens on
// the frame after the menu and the beat is simply gone.
//
// This runs the real Session with a person's answer supplied, and reports the
// frame each thing happens on. The object NAMES are the corroboration that
// matters and are not this file's invention: `Grid.SCX` calls them
// "1KaylArrives", "2KaylStand", "3KaylLeaves" and "Wait5sec".
#include "platform/datafs.h"
#include "script/area.h"
#include "script/gamestate.h"
#include "script/script.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: run_intro_beat <gamedata> <tables> <out.txt>\n");
        return 2;
    }
    const std::string fr = argv[1], tb = argv[2];
    const auto table = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    if (!table.valid()) return 1;
    if (!omk::safeOutputPath(argv[3])) return 2;

    auto state = omk::GameState::fromFile(fr + "/IAM/START");
    omk::Session s(fr + "/IAM", state, table);
    s.answerUiFromPerson(true);
    s.setCameraWait(true);
    s.setObjectWait(true);
    const int area = state.currentArea();
    s.loadArea(area);
    const bool scene = s.loadScene(fr + "/SCPTDATA", omk::ChunkKind::Area, area);

    // Optional: the frame's length in SECONDS, which is what `Game_Frame`
    // divides 30 by. The beat's length in FRAMES must scale with it and its
    // length in SECONDS must not - that is the whole content of "the animation
    // does not run at the frame rate", and it is machine-independent in a way
    // an fps number never is.
    const double dt = argc >= 5 ? std::atof(argv[4]) : 1.0 / 30.0;
    s.setFrameSeconds(dt);

    long answered = -1, dialogAt = -1;
    for (long f = 0; f < 2000; ++f) {
        s.frame();
        if (answered < 0 && s.pendingUiScreen() >= 0) {
            // the person answers "Nouvelle partie"
            s.answerUi(1);
            answered = f;
        }
        if (s.dialogOpen()) { dialogAt = f; break; }
    }

    // What the scene started, in order - and for each, WHICH clip of the
    // scene's animation array it plays and WHICH authored path it stands on.
    // Those two are what makes the beat visible: `character.show` puts the
    // model in the world and the object's `Script_SelectRelativeBodyAnimation`
    // says how it moves (param 1) and where (param 8).
    std::ofstream f(argv[3]);
    f << "scene " << (scene ? 1 : 0) << ' ' << s.scene().started().size() << '\n';
    for (const auto& st : s.scene().started()) {
        f << "started " << st.object << ' ' << st.name << ' ' << st.how << ' '
          << (st.waiting ? 1 : 0) << ' ' << st.actor << ' ' << st.clip << ' '
          << st.path << '\n';
        if (st.clip >= 0 && s.scene().loaded())
            f << "clip " << st.clip << ' ' << s.scene().scene().clipName(st.clip)
              << ' ' << s.scene().scene().clipFrames(st.clip) << '\n';
        if (st.path >= 0 && s.scene().loaded() &&
            st.path < static_cast<int>(s.scene().scene().paths().size())) {
            const auto& pa = s.scene().scene().paths()[static_cast<std::size_t>(st.path)];
            f << "path " << st.path << ' ' << pa.name << ' '
              << (pa.keys.empty() ? 0 : static_cast<long>(pa.keys.front().pos[0])) << ' '
              << (pa.keys.empty() ? 0 : static_cast<long>(pa.keys.front().pos[1])) << ' '
              << (pa.keys.empty() ? 0 : static_cast<long>(pa.keys.front().pos[2])) << '\n';
        }
    }
    // And who is on screen when the conversation opens - `character.show`
    // fires 28 bytes before `dialog.start`, so this must not be empty.
    f << "shown " << s.shown().size();
    for (const auto& sh : s.shown()) f << ' ' << sh.actor << ' ' << sh.model;
    f << '\n';
    f << "beat " << answered << ' ' << dialogAt << ' '
      << (dialogAt - answered) << '\n';
    f << "seconds " << (dialogAt - answered) * dt << '\n';
    std::printf("answered on frame %ld, conversation opened on %ld - a beat of "
                "%ld frames (%.1f s at 30)\n", answered, dialogAt,
                dialogAt - answered, (dialogAt - answered) / 30.0);
    return 0;
}
