// SPDX-License-Identifier: GPL-3.0-or-later
// THE AREA TRANSITION, THE STAGED LOAD AND THE CONTEXT TABLE, exercised one
// rule at a time on the shipped data.
//
//     transition_probe <gamedata> <tables>
//
// One line per fact, each the port's answer to a question the engine's own
// code settles (todo/iam-script-engine.md issues 3, 14, 15, 16, 18, 19, 21,
// 36; docs/SCRIPT_VM.md "The area transition"):
//
//   staged        the intro's own `area.goto 222 -1 -1` (AREA 118, pc 1309):
//                 the frame it announces, the frame the caller RESUMES into
//                 `scene.load 222,55`, and the set's slice count between them
//   slices        Anekbah's set streamed at 0x20000 bytes a frame - the slice
//                 count off the file size, and the frames a request takes
//   objects       AREA 0's zone record 3 - `area.goto 201, 153, 240` in its
//                 ENTER script: the load, the departure object on the
//                 outgoing scene, the arrival object after it, the caller
//                 resuming only then
//   deferral      `area.preload 0` parks its caller at status 8 and defers a
//                 second context's `area.goto` (status 9, pc rewound) until
//                 the load lands; then the goto is accepted
//   slots         a freed table entry is reused and the pump runs INDEX
//                 order, so the later context runs earlier; the 33rd is not
//                 listed
//   camera_unknown `camera.set.wait` on a camera no resident table has does
//                 not hold; on one it has, it holds for the move
//   restart       `requestRestart()`: contexts freed, START reloaded over
//                 the DB, day 52 / 2000000, the boot script running again in
//                 the same frame
//
// Nothing here is a reference value: the check that quotes these lines is
// what pins them, and todo/pending/T11.md has what they looked like with each
// rule reverted.
#include "formats/iam.h"
#include "script/area.h"
#include "script/gamestate.h"
#include "script/script.h"
#include "script/world.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::vector<std::byte> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const auto n = static_cast<std::size_t>(f.tellg());
    std::vector<std::byte> d(n);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(d.data()), static_cast<std::streamsize>(n));
    return d;
}

// One instruction: the opcode, its int16 fields, zero-padded to the table's
// operand length. A synthetic script starts at offset 1 - `Script_ProcessActions`
// arms a slot only `if (script)`, so offset 0 is "none" - and `emit` on an
// empty buffer lays that leading byte down first.
void emit(std::vector<std::byte>& out, const omk::OpcodeTable& t, int op,
          std::initializer_list<int> fields) {
    if (out.empty()) out.push_back(std::byte{3});          // offset 0: an `end` nobody runs
    out.push_back(static_cast<std::byte>(op));
    const int n = t.operandLength(static_cast<std::uint8_t>(op));
    std::size_t k = 0;
    for (const int f : fields) {
        out.push_back(static_cast<std::byte>(f & 0xFF));
        out.push_back(static_cast<std::byte>((f >> 8) & 0xFF));
        k += 2;
    }
    while (static_cast<int>(k) < n) { out.push_back(std::byte{0}); ++k; }
}

// The frame an announcement (domain, value) first appears at or after `from`.
long frameOf(const std::vector<std::pair<long, omk::Announced>>& log,
             const char* domain, int value, long from = 0) {
    for (const auto& e : log)
        if (e.first >= from && e.second.domain == domain && e.second.value == value)
            return e.first;
    return -1;
}

// Run `frames` frames, tagging each announcement with its frame.
void run(omk::Session& s, int frames,
         std::vector<std::pair<long, omk::Announced>>& log) {
    for (int f = 0; f < frames; ++f) {
        const auto before = s.announced().size();
        s.frame();
        for (auto i = before; i < s.announced().size(); ++i)
            log.push_back({s.frameNo(), s.announced()[i]});
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: transition_probe <gamedata> <tables>\n");
        return 2;
    }
    const std::string fr = argv[1], tb = argv[2];
    const auto table = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    if (!table.valid()) return 1;
    const std::string iam = fr + "/IAM";
    const auto areaFile = readFile(iam + "/AREA");
    const auto areas = omk::IamArchive::open(areaFile);

    // ---- staged: the intro's own transition, frame by frame
    {
        auto state = omk::GameState::fromFile(iam + "/START");
        omk::Session s(iam, state, table);
        s.loadAnnounceMap(tb + "/vm_announce.json");
        s.answerUiFromPerson(true);
        s.setCameraWait(true);
        s.loadArea(118);
        std::vector<std::pair<long, omk::Announced>> log;
        run(s, 1, log);                              // parks at ui.open 29
        s.answerUi(1);
        int statusDuring = -1;
        long gotoFrame = -1, resumeFrame = -1;
        for (int f = 0; f < 800 && resumeFrame < 0; ++f) {
            run(s, 1, log);
            if (s.dialogOpen()) s.endDialog();
            if (gotoFrame < 0) gotoFrame = frameOf(log, "AREAS", 222);
            if (gotoFrame >= 0 && s.frameNo() == gotoFrame) statusDuring = s.contextStatus(0);
            resumeFrame = frameOf(log, "SCENES", 55);
        }
        std::printf("staged goto_frame %ld status_after_goto %d slices %d resume_frame %ld "
                    "delta %ld other_at_resume %d current %d arrive_frame %ld entered %d\n",
                    gotoFrame, statusDuring, s.loadSlicesFor(222), resumeFrame,
                    resumeFrame - gotoFrame, s.otherArea(), s.currentArea(),
                    frameOf(log, "AREAS", 118), s.areasEntered());
    }

    // ---- slices: a big set, requested and timed
    {
        auto state = omk::GameState::fromFile(iam + "/START");
        omk::Session s(iam, state, table);
        s.answerUiFromPerson(true);
        s.loadArea(118);
        s.frame();
        const int slices = s.loadSlicesFor(0);
        const auto c0 = s.contextsCreated();
        s.requestArea(0);
        int frames = 0;
        while (s.contextsCreated() == c0 && frames < 200) { s.frame(); ++frames; }
        std::printf("slices area 0 set %s slices %d frames %d created %zu -> %zu current %d other %d\n",
                    s.residentSlot(1).set.c_str(), slices, frames, c0, s.contextsCreated(),
                    s.currentArea(), s.otherArea());
    }

    // ---- objects: a zone ENTER script carrying both objects
    {
        auto state = omk::GameState::fromFile(iam + "/START");
        omk::Session s(iam, state, table);
        s.loadAnnounceMap(tb + "/vm_announce.json");
        s.answerUiFromPerson(true);
        s.setObjectWait(true);
        s.loadScene(fr + "/SCPTDATA", omk::ChunkKind::Area, 0);
        s.loadArea(0);
        const auto chunk = areas.chunk(0);
        const auto zones = omk::zonesOf(chunk, omk::ChunkKind::Area);
        const auto& z = zones.at(3);
        const int idx = s.newContext(0, chunk, z.scripts, z.id, 0);
        s.queueAction(idx, 1);
        long gotoF = -1, f1F = -1, f2F = -1, doneF = -1, endF = -1;
        int f1 = -1, f2 = -1, dest = -1;
        std::string f1Name, f2Name;
        // The objects are found in the scene's started list the frame they
        // appear; the list is the OUTGOING scene's and goes with it when the
        // transition completes and the destination's `.SCX` replaces it.
        for (int f = 0; f < 3000 && endF < 0; ++f) {
            s.frame();
            const auto& tr = s.transition();
            if (gotoF < 0 && tr.state != 0) { gotoF = s.frameNo(); dest = tr.dest; f1 = tr.f1; f2 = tr.f2; }
            for (const auto& st : s.scene().started()) {
                if (st.object == f1 && f1F < 0) { f1F = s.frameNo(); f1Name = st.name; }
                if (st.object == f2 && f2F < 0) { f2F = s.frameNo(); f2Name = st.name; }
            }
            if (doneF < 0 && gotoF >= 0 && tr.state == 0 && s.contextStatus(idx) == 1) doneF = s.frameNo();
            if (doneF >= 0 && s.contextStatus(idx) == 0) endF = s.frameNo();
        }
        std::printf("objects zone %d dest %d f1 %d f2 %d goto_frame %ld slices %d f1_frame %ld "
                    "f1_name %s f2_frame %ld f2_name %s resume_frame %ld end_frame %ld "
                    "current %d other %d shown_scx %s\n",
                    z.id & 0x7FFF, dest, f1, f2, gotoF, s.loadSlicesFor(dest), f1F,
                    f1Name.empty() ? "-" : f1Name.c_str(), f2F,
                    f2Name.empty() ? "-" : f2Name.c_str(), doneF, endF,
                    s.currentArea(), s.otherArea(), s.scxName().c_str());
    }

    // ---- deferral: preload parks, a second goto is refused until it lands
    {
        auto state = omk::GameState::fromFile(iam + "/START");
        omk::Session s(iam, state, table);
        s.answerUiFromPerson(true);
        s.loadArea(118);
        s.frame();                                   // 118's script parks at ui.open
        std::vector<std::byte> a, b;
        emit(a, table, 45, {0});     emit(a, table, 3, {});     // area.preload 0
        emit(b, table, 47, {222, -1, -1}); emit(b, table, 3, {}); // area.goto 222
        const std::int32_t sa[3] = {1, 0, 0}, sb[3] = {1, 0, 0};
        const int ia = s.newContext(0, a, sa, -1, 118);
        const int ib = s.newContext(0, b, sb, -1, 118);
        s.queueAction(ia, 1);
        s.queueAction(ib, 1);
        s.frame();
        const int stA = s.contextStatus(ia), stB = s.contextStatus(ib);
        const int deferred = s.deferredContext();
        const int slices = s.loadSlicesFor(0);
        long aResumed = -1, bAccepted = -1, bResumed = -1;
        for (int f = 0; f < 200 && bResumed < 0; ++f) {
            s.frame();
            if (aResumed < 0 && s.contextStatus(ia) != 8) aResumed = s.frameNo();
            if (bAccepted < 0 && s.contextStatus(ib) == 10) bAccepted = s.frameNo();
            if (bAccepted >= 0 && s.contextStatus(ib) == 0) bResumed = s.frameNo();
        }
        std::printf("deferral a_status %d b_status %d deferred %d a_index %d slices %d "
                    "a_resumed %ld b_accepted %ld b_done %ld preloaded_other %d current %d\n",
                    stA, stB, deferred, ia, slices, aResumed, bAccepted, bResumed,
                    s.residentSlot(1).area, s.currentArea());
    }

    // ---- slots: reuse order, and the 33rd
    {
        auto state = omk::GameState::fromFile(iam + "/START");
        omk::Session s(iam, state, table);
        s.loadAnnounceMap(tb + "/vm_announce.json");
        s.answerUiFromPerson(true);
        s.loadArea(118);
        s.frame();
        const auto mk = [&](int cam) {
            std::vector<std::byte> c;
            emit(c, table, 95, {cam, 0, 0}); emit(c, table, 3, {});
            const std::int32_t sc[3] = {1, 0, 0};
            const int i = s.newContext(0, c, sc, -1, 118);
            if (i >= 0) s.queueAction(i, 1);
            return i;
        };
        const int p = mk(1001), q = mk(1002), r = mk(1003);
        s.freeContext(q);
        const int t = mk(1004);
        const auto a0 = s.announced().size();
        s.frame();
        std::string order;
        for (auto i = a0; i < s.announced().size(); ++i)
            if (s.announced()[i].domain == "CAMERAS")
                order += (order.empty() ? "" : ",") + std::to_string(s.announced()[i].value);
        int made = 0, last = 0;
        while (last >= 0 && made < 40) { last = mk(2000 + made); if (last >= 0) ++made; }
        std::printf("slots p %d q %d r %d t %d order %s filled %d created %zu unlisted %zu\n",
                    p, q, r, t, order.c_str(), made, s.contextsCreated(), s.contextsUnlisted());
    }

    // ---- camera_unknown: no hold on a camera Camera_FindWorld cannot resolve
    {
        auto state = omk::GameState::fromFile(iam + "/START");
        omk::Session s(iam, state, table);
        s.loadAnnounceMap(tb + "/vm_announce.json");
        s.answerUiFromPerson(true);
        s.setCameraWait(true);
        s.loadArea(118);
        s.frame();
        std::vector<std::byte> u, k;
        emit(u, table, 96, {29999, 30, 1}); emit(u, table, 95, {2148, 0, 2}); emit(u, table, 3, {});
        emit(k, table, 96, {2148, 30, 1});  emit(k, table, 95, {2172, 0, 2}); emit(k, table, 3, {});
        const std::int32_t z[3] = {1, 0, 0};
        const int iu = s.newContext(0, u, z, -1, 118);
        s.queueAction(iu, 1);
        std::vector<std::pair<long, omk::Announced>> log;
        run(s, 1, log);
        const long f1 = frameOf(log, "CAMERAS", 29999), f2 = frameOf(log, "CAMERAS", 2148);
        const int stU = s.contextStatus(iu);
        const int ik = s.newContext(0, k, z, -1, 118);
        s.queueAction(ik, 1);
        const long from = s.frameNo() + 1;
        run(s, 40, log);
        const long g1 = frameOf(log, "CAMERAS", 2148, from), g2 = frameOf(log, "CAMERAS", 2172, from);
        std::printf("camera_unknown unknown_frame %ld next_frame %ld status %d "
                    "known_frame %ld known_next %ld hold %ld\n",
                    f1, f2, stU, g1, g2, g2 - g1);
    }

    // ---- restart
    {
        auto state = omk::GameState::fromFile(iam + "/START");
        omk::Session s(iam, state, table);
        s.loadAnnounceMap(tb + "/vm_announce.json");
        s.answerUiFromPerson(true);
        s.loadArea(118);
        s.frame();
        s.answerUi(1);
        s.frame();                                   // to dialog.start 272
        const bool dlg = s.dialogOpen();
        s.endDialog();
        for (int f = 0; f < 3; ++f) s.frame();
        const int playerBefore = s.playerActor();
        const auto liveBefore = s.liveContexts();
        const int otherBefore = s.otherArea();
        state.setVar(19, 77);
        s.requestRestart();
        const auto a0 = s.announced().size();
        s.frame();
        std::string first;
        for (auto i = a0; i < s.announced().size() && i < a0 + 3; ++i)
            first += (first.empty() ? "" : ",") + s.announced()[i].domain + ":" +
                     std::to_string(s.announced()[i].value);
        std::printf("restart dialog %d player_before %d live_before %zu other_before %d "
                    "player_after %d day %d clock %d var19 %d live_after %zu current %d "
                    "other %d status0 %d first %s\n",
                    dlg ? 1 : 0, playerBefore, liveBefore, otherBefore, s.playerActor(),
                    state.clockDay(), state.clock(), state.var(19), s.liveContexts(),
                    s.currentArea(), s.otherArea(), s.contextStatus(0), first.c_str());
    }
    return 0;
}
