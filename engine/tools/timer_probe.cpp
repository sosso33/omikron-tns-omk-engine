// SPDX-License-Identifier: GPL-3.0-or-later
// THE CLOCK AND THE SCRIPT TIMER, run by the Session's own frame
// (`todo/drift-audit.md` S1): `Game_Tick` (0x004200F0) ends
// `sub_41E480(); sub_41E7A0(); Clock_Tick();`, and until 2026-10-05 the port
// had both halves and no caller.
//
//     timer_probe <gamedata> <tables>
//
// On the shipped data, through the real script: AREA 77 (Jaunpur Tetra 2),
// the player stood in zone 1532 'bombe 1', whose script places the first
// bomb and, at `2-T Bombes placées` == 1, does `timer.mode 12`, `timer.set
// 900`, op 112 - a fifteen-minute countdown. One line per fact:
//
//   clock    the clock over 300 frames at delta 1, which `Clock_Tick` steps
//            by 166 each time its accumulator passes 5 (strictly)
//   start    the frame the zone's script started the timer, its flags, its
//            value, and where the zone is (for `omk-play --stand`)
//   expiry   the frame `sub_41E480`'s tail fired, run at delta 3 - the
//            largest `Game_Frame` produces (BOOT 4: 30/fps capped at 3) - the
//            clock then, the flags, and message 18: which table answered,
//            at which handler, and whether its context ran. AREA 77's is the
//            time-out: fade, `timer.stop`, `Restart Total Tetra`, back to 61
//   pause    50 frames at delta 0. `Clock_Tick` takes ONE step a call
//            whatever the delta and carries the rest, so a frame above 5
//            leaves a debt that steps on through a pause - why the run above
//            stays at the engine's own cap. At 3 the carry is under 8, so a
//            pause moves the clock one step at most
#include "formats/iam.h"
#include "script/area.h"
#include "script/gamestate.h"
#include "script/script.h"
#include "script/zones.h"

#include <cstdio>
#include <string>

namespace {

float arcCentreDegrees(const omk::Zone& z) {
    return static_cast<float>(static_cast<int>(
        static_cast<double>(z.arcMid) * omk::kZoneArcToDegrees));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: timer_probe <gamedata> <tables>\n");
        return 2;
    }
    const std::string fr = argv[1], tb = argv[2];
    const auto table = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    if (!table.valid()) return 1;
    const std::string iam = fr + "/IAM";

    auto state = omk::GameState::fromFile(iam + "/START");
    // the raid's first zone is live from the area's load in play; START
    // predates the raid, so its bit is set here as the game would have it
    state.setBit(omk::StateArray::ZoneState, 1532, 1);
    omk::Session s(iam, state, table);
    s.loadArea(77);

    // ---- clock: 300 frames at delta 1
    const int c0 = state.clock();
    for (int f = 0; f < 300; ++f) s.frame();
    std::printf("clock before %d after %d frames 300 step %d\n",
                c0, state.clock(), state.clock() - c0);

    // ---- start: into 'bombe 1'
    const auto* z = s.zones().resolve(1532);
    if (!z) { std::printf("start zone 1532 NOT RESIDENT\n"); return 1; }
    double c[3];
    z->zone.centre(c);
    const float p[3] = {static_cast<float>(c[0]), static_cast<float>(c[1]),
                        static_cast<float>(c[2])};
    s.setPlayerPosition(p, arcCentreDegrees(z->zone));
    long started = -1;
    int startClock = 0;
    for (int f = 0; f < 400 && started < 0; ++f) {
        s.frame();
        if (!(state.timerFlags() & omk::GameState::kTimerStopped)) {
            started = s.frameNo();
            startClock = state.timerBase();
        }
        if (f == 2 && started < 0) s.pressAction();     // an ACTIVATE script
    }
    std::printf("start frame %ld flags %d value %d zone_at %.0f,%.0f,%.0f,%.0f\n",
                started, state.timerFlags(), state.timerValue(),
                c[0], c[1], c[2], static_cast<double>(arcCentreDegrees(z->zone)));
    if (started < 0) return 1;
    // out of the zone again, so nothing it runs stands in the way
    const float away[3] = {p[0] + 1e6f, p[1], p[2] + 1e6f};
    s.setPlayerPosition(away, 0.0f);

    // ---- expiry, at delta 3
    s.setFrameSeconds(3.0 / 30.0);
    const auto msgAt = s.messagesRun().size();
    long expired = -1;
    int clockAt = 0;
    for (int f = 0; f < 12000 && expired < 0; ++f) {
        s.frame();
        if (state.timerFlags() & omk::GameState::kTimerExpired) {
            expired = s.frameNo();
            clockAt = state.timerBase();   // `g_TimerStart = g_ClockTime` at the expiry
        }
    }
    // the handler's context runs from the next pump
    s.setFrameSeconds(1.0 / 30.0);
    for (int f = 0; f < 3; ++f) s.frame();
    int msg = -1;
    std::string tableName = "-";
    long ranFrame = -1;
    std::size_t offset = 0;
    for (auto i = msgAt; i < s.messagesRun().size(); ++i)
        if (s.messagesRun()[i].message == 18) {
            msg = 18;
            tableName = s.messagesRun()[i].table;
            offset = s.messagesRun()[i].offset;
            ranFrame = s.messagesRun()[i].ranFrame;
            break;
        }
    std::printf("expiry frame %ld after_start %ld clock %d elapsed %d flags %d "
                "message %d table %s offset %zu ran %d\n",
                expired, expired < 0 ? -1 : expired - started, clockAt,
                clockAt - startClock, state.timerFlags(),
                msg, tableName.c_str(), offset, ranFrame >= 0 ? 1 : 0);

    // ---- pause: delta 0
    s.setFrameSeconds(0.0);
    const int cp = state.clock();
    for (int f = 0; f < 50; ++f) s.frame();
    std::printf("pause frames 50 clock_moved %d\n", state.clock() - cp);
    return 0;
}
