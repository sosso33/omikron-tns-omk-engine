// SPDX-License-Identifier: GPL-3.0-or-later
// THE SESSION'S SCHEDULING RULES, exercised one at a time on the shipped data.
//
//     session_probe <gamedata> <tables>
//
// One line per fact, each the port's answer to a question the engine's own
// code settles (todo/iam-script-engine.md, issues 2, 6, 8, 11, 17, 25, 28,
// 32):
//
//   become        AREA 118's startup script opens `player.become 136` (pc
//                 1047): the DB player record's id and bio after one frame
//   message25     `postMessage(25)` runs INLINE - the frame it is posted on
//   dialog_frame  the frame `dialog.start 272` opens, and whether a second
//                 context queued for that frame still ran in it
//   shown_bit     `character.show 310` (pc 1184) writes the record's +18 bit
//   derived_none  a derived walk with NO answer resumes the script at -1
//   answer_minus1 `answerUi(-1)` from a person does the same
//   scene_load    `scene.load` on the RESIDENT area queues the SCENE's script
//   scene_unload  ...and `scene.unload` drops it and clears the DB field
//   message_area  which resident table a subscribed message resolves through
//   return        A -> B -> A creates no startup context for A the second time
//
// Nothing here is a reference value: the check that quotes these lines is
// what pins them, and the "before" column in todo/pending/T2.md is what they
// looked like when each rule was wrong.
#include "formats/iam.h"
#include "script/area.h"
#include "script/gamestate.h"
#include "script/props.h"
#include "script/script.h"
#include "script/zones.h"

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

std::string dbString(const omk::GameState& s, std::size_t off, std::size_t max) {
    std::string out;
    const auto r = s.raw();
    for (std::size_t i = 0; i < max && off + i < r.size(); ++i) {
        const auto c = static_cast<unsigned char>(r[off + i]);
        if (c == 0) break;
        if (c == 0xFF) { out = "<ff>"; break; }
        out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    }
    return out;
}

std::int16_t i16(std::span<const std::byte> b, std::size_t o) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(b[o]) |
                                     (static_cast<std::uint16_t>(b[o + 1]) << 8));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: session_probe <gamedata> <tables>\n");
        return 2;
    }
    const std::string fr = argv[1], tb = argv[2];
    const auto table = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    if (!table.valid()) return 1;
    const std::string iam = fr + "/IAM";

    // ---- A: the intro's session, a person answering, no waits (no scene)
    {
        auto state = omk::GameState::fromFile(iam + "/START");
        omk::Session s(iam, state, table);
        s.answerUiFromPerson(true);
        const int before = s.playerActor();
        s.loadArea(118);
        s.frame();                                   // 1040 .. ui.open at 1078
        std::printf("become player_before %d player_after %d model %s bio0 \"%s\" bio1 \"%s\"\n",
                    before, s.playerActor(),
                    dbString(state, 60 + 144, 20).c_str(),
                    dbString(state, 336, 24).c_str(),
                    dbString(state, 592, 24).c_str());

        // message 25: inline. GLOBAL +72 names the variable its handler sets
        // (`Game_HandleEvent` case 42 reads it straight back).
        const auto g = readFile(iam + "/GLOBAL");
        const int v25 = g.size() > 74 ? i16(g, 72) : -1;
        const auto vBefore = state.var(v25);
        const bool found25 = s.postMessage(25, -1);
        const auto& m25 = s.messagesRun().back();
        std::printf("message25 found %d table %s offset %zu posted %ld ran %ld var%d %d -> %d\n",
                    found25 ? 1 : 0, m25.table.c_str(), m25.offset, m25.postedFrame,
                    m25.ranFrame, v25, vBefore, state.var(v25));

        // the answer, and a second context queued for the SAME frame the
        // startup script reaches `dialog.start`
        s.answerUi(1);
        s.postMessage(26, -1);                      // GLOBAL's "nothing here"
        const auto& m26 = s.messagesRun().back();
        s.frame();                                   // resumes 1085 .. 1212
        std::printf("dialog_frame open %d frame 2 message %d table %s posted %ld ran %ld\n",
                    s.dialogOpen() ? 1 : 0, m26.message, m26.table.c_str(),
                    m26.postedFrame, m26.ranFrame);
        const int bit310 = s.shownBitOf(310);
        std::printf("shown_bit actor 310 index %d value %d shown %zu", bit310,
                    bit310 >= 0 ? state.bit(omk::StateArray::ObjectShown, bit310) : -1,
                    s.shown().size());
        for (const auto& sh : s.shown()) std::printf(" %d:%s", sh.actor, sh.model.c_str());
        std::printf("\n");
    }

    // ---- B: the same park with NO screens attached and no person: the
    // derived walk has nothing, which is the screen being LEFT
    {
        auto state = omk::GameState::fromFile(iam + "/START");
        omk::Session s(iam, state, table);
        s.loadArea(118);
        state.setVar(19, 77);                        // a sentinel, so -1 is a write
        s.frame();                                   // parks, walks, no answer
        const auto v19 = state.var(19);
        const auto live = s.liveContexts();
        s.frame();
        std::printf("derived_none var19 %d live %zu dialog_open_after %d\n",
                    v19, live, s.dialogOpen() ? 1 : 0);
    }
    {
        auto state = omk::GameState::fromFile(iam + "/START");
        omk::Session s(iam, state, table);
        s.answerUiFromPerson(true);
        s.loadArea(118);
        state.setVar(19, 77);
        s.frame();
        const int screen = s.pendingUiScreen();
        s.answerUi(-1);
        s.frame();
        std::printf("answer_minus1 screen %d var19 %d pending_after %d dialog_open_after %d\n",
                    screen, state.var(19), s.pendingUiScreen(), s.dialogOpen() ? 1 : 0);
    }

    // ---- C: scene.load / scene.unload on the resident area (222, the
    // Impasse, whose scene 55 the intro loads over it)
    {
        auto state = omk::GameState::fromFile(iam + "/START");
        omk::Session s(iam, state, table);
        s.loadAnnounceMap(tb + "/vm_announce.json");
        s.answerUiFromPerson(true);
        s.loadArea(222);
        const auto c0 = s.contextsCreated();
        s.sceneLoad(222, 55);
        const auto c1 = s.contextsCreated(), l1 = s.liveContexts();
        const auto db1 = state.sceneOfArea(222);
        s.sceneUnload(222);
        const auto l2 = s.liveContexts();
        const auto db2 = state.sceneOfArea(222);
        std::printf("scene_load area 222 scene 55 db %d created %zu -> %zu live %zu\n",
                    db1, c0, c1, l1);
        std::printf("scene_unload area 222 db %d live %zu -> %zu\n", db2, l1, l2);
        // ...and loaded again, one frame: SCENE 55's startup script fires
        // its beats, which the announce map turns into SCENES lines
        s.sceneLoad(222, 55);
        const auto a0 = s.announced().size();
        s.frame();
        std::size_t nScenes = 0;
        for (const auto& a : s.announced()) if (a.domain == "SCENES") ++nScenes;
        std::printf("scene_script announced %zu -> %zu SCENES %zu\n",
                    a0, s.announced().size(), nScenes);
        // a message the SCENE's table subscribes, if it has one, else the area's
        const auto sceneFile = readFile(iam + "/SCENE");
        const auto scenes = omk::IamArchive::open(sceneFile);
        const auto subs = omk::chunkSubscriptions(scenes.chunk(55), omk::ChunkKind::Scene);
        int msg = 23;                                // AREA 222's own: 23 and 24
        for (const auto& su : subs) if (su.script > 0) { msg = su.message; break; }
        const bool found = s.postMessage(msg, -1);
        const auto& m = s.messagesRun().back();
        std::printf("message_area message %d found %d table %s offset %zu scene55_subs %zu\n",
                    msg, found ? 1 : 0, m.table.c_str(), m.offset, subs.size());
    }

    // ---- E: THE SUPERMARKET'S SCORE (`todo/shoot-mode.md` 8.5d). A gunman's
    // death clip playing out posts message 3 with HIM as the sender
    // (`sub_424DE0`'s dead arm); SCENE 56's subscription for it sets
    // `Braqueur N Dead` per robber and, for actor 84, enables zone 3931, whose
    // enter script runs `shoot.end` and the end cutscene. Nothing posted it
    // until 2026-09-10, so the phase could not be finished.
    {
        auto state = omk::GameState::fromFile(iam + "/START");
        omk::Session s(iam, state, table);
        s.answerUiFromPerson(true);
        s.loadArea(230);
        s.sceneLoad(230, 56);
        s.frame();
        const auto live3931 = [&s]() {
            for (const auto& lz : s.zones().registered())
                if ((lz.zone.id & 0x7FFF) == 3931) return 1;
            return 0;
        };
        const auto sceneFile = readFile(iam + "/SCENE");
        const auto scenes = omk::IamArchive::open(sceneFile);
        const auto subs = omk::chunkSubscriptions(scenes.chunk(56), omk::ChunkKind::Scene);
        int sub3 = -1;
        for (const auto& su : subs) if (su.message == 3) { sub3 = static_cast<int>(su.script); break; }
        const int v640a = state.var(640), v644a = state.var(644), z0 = live3931();
        const bool f37 = s.postMessage(3, 37);
        const auto m37 = s.messagesRun().back();
        s.frame();
        const int v640b = state.var(640), v644b = state.var(644), z1 = live3931();
        const bool f84 = s.postMessage(3, 84);
        s.frame();
        std::printf("shoot_score sub3 %d found37 %d table %s offset %zu var640 %d -> %d "
                    "var644 %d -> %d -> %d zone3931 %d -> %d -> %d found84 %d subs %zu\n",
                    sub3, f37 ? 1 : 0, m37.table.c_str(), m37.offset, v640a, v640b,
                    v644a, v644b, state.var(644), z0, z1, live3931(), f84 ? 1 : 0,
                    subs.size());
    }

    // ---- F: A SHOOT MEDIKIT (`script/hooks.h` `shootStatSet`). In a shoot
    // phase a kit is a ZONE: its script hides the kit and writes the player's
    // health (`var.set.actor_stat`, `var.add`, `actor.stat.set`), and
    // `Actor_SetProperty`'s tail `sub_423A40` hands the clamped value to the
    // shoot record - the gauge. Zone 3935 is the supermarket's medium kit,
    // `+0x32`. Stand in it with shoot mode on and read what was queued.
    {
        auto state = omk::GameState::fromFile(iam + "/START");
        omk::Session s(iam, state, table);
        s.answerUiFromPerson(true);
        s.loadArea(230);
        s.sceneLoad(230, 56);
        s.frame();
        const bool began = s.shootBegin(-1);
        const auto rec = [&state]() {
            return state.raw().subspan(static_cast<std::size_t>(omk::GameState::kPlayerRecord),
                                       static_cast<std::size_t>(omk::GameState::kPlayerRecordSize));
        };
        std::int32_t before = -1, after = -1;
        omk::readActorProperty(rec(), 1, before);
        const omk::LiveZone* kit = nullptr;
        for (const auto& lz : s.zones().registered())
            if ((lz.zone.id & 0x7FFF) == 3935) { kit = &lz; break; }
        int facing = 0;
        if (kit) {
            double c[3];
            kit->zone.centre(c);
            for (int d = 0; d < 360; d += 5)
                if (omk::zoneFacesDegrees(kit->zone, d)) { facing = d; break; }
            const float at[3] = {static_cast<float>(c[0]), static_cast<float>(c[1]),
                                 static_cast<float>(c[2])};
            s.takeShootStatWrites();                 // nothing before the kit counts
            s.setPlayerPosition(at, static_cast<float>(facing));
            for (int f = 0; f < 4; ++f) s.frame();
        }
        omk::readActorProperty(rec(), 1, after);
        const auto writes = s.takeShootStatWrites();
        std::printf("shoot_stat began %d zone3935 %d before %d after %d writes %zu",
                    began ? 1 : 0, kit ? 1 : 0, int(before), int(after), writes.size());
        for (const auto& w : writes)
            std::printf(" [%d %d %d]", w.actor, w.property, int(w.value));
        std::printf("\n");
    }

    // ---- D: A -> B -> A
    {
        auto state = omk::GameState::fromFile(iam + "/START");
        omk::Session s(iam, state, table);
        s.answerUiFromPerson(true);
        s.loadArea(118);
        const auto cA = s.contextsCreated();
        s.requestArea(222);
        s.frame();
        const auto cB = s.contextsCreated();
        const int otherAtB = s.otherArea();
        s.requestArea(118);
        s.frame();
        std::printf("return A 118 B 222 back 118 created %zu -> %zu -> %zu other_at_B %d other_back %d current %d\n",
                    cA, cB, s.contextsCreated(), otherAtB, s.otherArea(), s.currentArea());
    }
    return 0;
}
