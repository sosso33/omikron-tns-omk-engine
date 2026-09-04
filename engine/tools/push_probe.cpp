// SPDX-License-Identifier: GPL-3.0-or-later
// push_probe - the crowd push (docs/STREET_LIFE.md 3, actor/spatial.h).
//
//     push_probe <gamedata> <tables dir> [area] [frames]
//
// `shape`: one instance entry at the origin facing -Z, sphere radius 20, and
// a probe sphere of radius 10 walked toward it across the heading and along
// it - the push per position, which is where the ellipse shows: two radii
// long along the heading, one across. `walk`: a city, the player built on
// its set and stood ahead of a walker on its lane, facing it; the walker
// walks into him and the push moves him; the bump message posts once and is
// held. `talk`: a walker standing at an action point, the player put in
// front of it, the action press - message 13/14.
#include "actor/player.h"
#include "actor/sliders.h"
#include "actor/spatial.h"
#include "formats/ctl.h"
#include "formats/mesh3do.h"
#include "o3de/collision.h"
#include "platform/datafs.h"
#include "script/area.h"
#include "script/gamestate.h"
#include "script/savefile.h"
#include "script/script.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: push_probe <gamedata> <tables dir> [area] [frames]\n"); return 2; }
    const std::string fr = argv[1], tb = argv[2];
    const int area = argc > 3 ? std::atoi(argv[3]) : 0;
    const int frames = argc > 4 ? std::atoi(argv[4]) : 150;

    // ---- shape
    {
        std::vector<omk::CollisionSphere> theirs = {{{0, 0, 0}, 20.0f}};
        std::vector<omk::CollisionSphere> mine = {{{0, 0, 0}, 10.0f}};
        omk::SpatialIndex ix;
        const int slot = ix.add(1, 1, 20.0f, &theirs);
        const float origin[3] = {0, 0, 0};
        ix.update(slot, origin, 0.0f);            // facing 0: heading -Z
        std::printf("shape across");
        for (int x = 60; x >= 0; x -= 10) {
            const float p[3] = {static_cast<float>(x), 0, 0}; float out[3];
            ix.query(mine, 10.0f, p, 0.0f, out);
            std::printf(" x%d:%.1f", x, out[0]);
        }
        std::printf("\nshape along");
        for (int z = 60; z >= 0; z -= 10) {
            const float p[3] = {0, 0, static_cast<float>(-z)}; float out[3];
            ix.query(mine, 10.0f, p, 0.0f, out);
            std::printf(" z%d:%.1f", z, out[2]);
        }
        std::printf("\n");
    }

    // ---- walk and talk, in a city
    const auto table = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    if (!table.valid()) return 1;
    const std::string iam = fr + "/IAM";
    omk::GameState state = omk::GameState::fromFile(iam + "/START");
    omk::Session s(iam, state, table);
    s.answerUiFromPerson(true);
    s.loadTraffic(fr);
    s.loadScene(fr + "/SCPTDATA", omk::ChunkKind::Area, area);
    s.loadArea(area);
    const auto& peds = s.sliders();
    if (!peds.loaded() || peds.liveCount() == 0) { std::printf("walk no crowd in area %d\n", area); return 0; }
    // the player on the area's set, as player_probe builds him
    const omk::DataFs fs(fr);
    const std::string set = s.residentSlot(s.activeSlot()).set;
    const auto setData = fs.read("MESHES/DECORS/" + set + ".3DO");
    const auto ctlData = fs.read("ANIMS/H1AVNT.CTL");
    const auto modData = fs.read("MESHES/PERSOS/HO1_FN.3DO");
    const auto soup = omk::collisionSoup(setData, omk::SoupKind::Walkable);
    const omk::CtlFile ctl = omk::readCtl(ctlData);
    std::vector<omk::Mesh> meshes;
    if (const auto h = omk::readHeader(modData)) meshes = omk::readMeshes(modData, *h);
    if (!ctl.valid || meshes.empty() || soup.empty()) { std::printf("walk cannot build the player\n"); return 1; }
    const auto mySpheres = omk::collisionSpheresOf(meshes);
    const float myReach = meshes.front().radius;
    // run the pool a little, then pick a walker on a lane, not blocked, not in an action
    for (int f = 0; f < 60; ++f) s.frame();
    int wi = -1;
    for (std::size_t i = 0; i < peds.movers().size(); ++i) {
        const auto& w = peds.movers()[i];
        if (!w.live || (w.flags & 0x191u) || w.speed <= 0.0f) continue;
        wi = static_cast<int>(i); break;
    }
    if (wi < 0) { std::printf("walk no walker to stand in front of\n"); return 0; }
    const auto& w0 = peds.movers()[static_cast<std::size_t>(wi)];
    float stand[3] = {w0.body[0] + w0.heading[0] * 120.0f, w0.body[1], w0.body[2] + w0.heading[2] * 120.0f};
    omk::PlayerController::Setup su;
    su.ctl = &ctl; su.ctlData = ctlData; su.meshes = &meshes; su.soup = &soup;
    for (int k = 0; k < 3; ++k) su.pos[k] = stand[k];
    su.facing = std::fmod(w0.facing + 180.0f, 360.0f);
    omk::PlayerController pc(su);
    s.setPlayerPosition(pc.pos(), pc.facing());
    const float start[3] = {pc.pos()[0], pc.pos()[1], pc.pos()[2]};
    float maxPush = 0.0f, minDist = 1e9f;
    int touchedFrames = 0, firstTouch = -1;
    for (int f = 0; f < frames; ++f) {
        float push[3];
        if (s.crowdPush(mySpheres, myReach, pc.pos(), pc.facing(), push)) {
            ++touchedFrames;
            if (firstTouch < 0) firstTouch = f;
            const float m = std::sqrt(push[0] * push[0] + push[2] * push[2]);
            if (m > maxPush) maxPush = m;
            pc.nudge(push);
        }
        pc.tick(1.0f, 0);
        s.setPlayerPosition(pc.pos(), pc.facing());
        s.frame();
        const auto& w = peds.movers()[static_cast<std::size_t>(wi)];
        const float dx = w.body[0] - pc.pos()[0], dz = w.body[2] - pc.pos()[2];
        minDist = std::fmin(minDist, std::sqrt(dx * dx + dz * dz));
    }
    int bumps = 0, talks = 0;
    for (const auto& m : s.messagesRun()) { if (m.message == 15 || m.message == 16) ++bumps; if (m.message == 13 || m.message == 14) ++talks; }
    const float moved = std::sqrt((pc.pos()[0] - start[0]) * (pc.pos()[0] - start[0]) + (pc.pos()[2] - start[2]) * (pc.pos()[2] - start[2]));
    std::printf("walk area %d walker %d model %s entries %d touched_frames %d first_touch %d max_push %.2f moved %.1f min_dist %.1f bumps %d\n",
                area, wi, w0.model.c_str(), s.spatial().liveCount(), touchedFrames, firstTouch, maxPush, moved, minDist, bumps);

    // ---- talk: a walker in its action's main phase
    int ti = -1;
    for (int f = 0; f < 900 && ti < 0; ++f) {
        s.frame();
        for (std::size_t i = 0; i < peds.movers().size(); ++i)
            if (peds.movers()[i].live && peds.actionPhase(static_cast<int>(i)) == 2) { ti = static_cast<int>(i); break; }
    }
    if (ti < 0) { std::printf("talk no walker at an action point within 900 frames\n"); return 0; }
    const auto& t = peds.movers()[static_cast<std::size_t>(ti)];
    float ahead[3] = {t.body[0] + t.heading[0] * 80.0f, t.body[1], t.body[2] + t.heading[2] * 80.0f};
    const float back = std::fmod(t.facing + 180.0f, 360.0f);
    const int before = talks;
    const bool found = s.talkToPedestrian(ahead, back);
    for (const auto& m : s.messagesRun()) if (m.message == 13 || m.message == 14) ++talks;
    // the countdown holds while he is the target: phase stays 2 over a clip's length
    const int phaseBefore = peds.actionPhase(ti);
    for (int f = 0; f < 200; ++f) s.frame();
    std::printf("talk walker %d model %s sex %d found %d target %d talks %d phase_before %d phase_after %d\n",
                ti, t.model.c_str(), t.sex, found ? 1 : 0, peds.talkTarget(), talks - before, phaseBefore, peds.actionPhase(ti));
    return 0;
}
