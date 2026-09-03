// SPDX-License-Identifier: GPL-3.0-or-later
// THE PLAYER CONTROLLER, driven by a replayable input stream - PORTING A6:
// input is a stream, and this feeds one.
//
//     player_probe <gamedata> <tables> <set> <ctl> <model> <x> <y> <z> <facing>
//                  <stream> <out.bin> [--verbose] [--list] [--frames-only]
//
//     <set>    the decor stem, MESHES/DECORS/<set>.3DO - AIMPASSE
//     <ctl>    the bank, ANIMS/<ctl>.CTL - the actor record's +72, H1AVNT
//     <model>  the character, MESHES/PERSOS/<model>.3DO - the record's +144
//     <stream> comma-separated runs: `k200*90` holds DIK scancode 200 for
//              90 frames (several with `+`: `k200+54*30`), `w4*90` holds the
//              input WORD 4, `0*30` holds nothing. Scancodes go through the
//              real path - `Input_InstallScheme(0)`, the live tables, and
//              `Game_Frame`'s edge filter with the world's mask 0.
//
// Prints one line per frame - position, facing, ACTOR_STATE, the `.CTL`
// state, clip and frame, and what the ground decided - and writes the numbers
// `verify.py: engine player walk` asserts:
//
//     int32 frames, dist*100, maxGroundErr*100, framesOffGround,
//           startsOnDefault, leftDefault, endsOnDefault,
//           camDistXZ*100, camFov*100, refusedTransitions, badLandings,
//           tracksMatched, tracksTotal, actorStateAtEnd, facingTurned*100,
//           distinctCtlStates, rootKeysFinite, presetEyeBack*10000,
//           presetFov*100, walkHeading*100 (the displacement's heading in
//           the +420 convention, -1 when he did not move), camBehind*100
//           (how far the steady eye sits BEHIND him along his facing)
//
// **TIER 5, data-constrained** (player.h has why no capture reaches it).
#include "actor/player.h"
#include "formats/ctl.h"
#include "formats/mesh3do.h"
#include "input/bindings.h"
#include "o3de/collision.h"
#include "platform/datafs.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace {

struct Run { std::vector<int> keys; std::uint32_t word = 0; int frames = 0; };

std::vector<Run> parseStream(const std::string& s) {
    std::vector<Run> out;
    std::string cur;
    for (char c : s + ",") {
        if (c != ',') { cur.push_back(c); continue; }
        if (cur.empty()) continue;
        Run r;
        const auto star = cur.find('*');
        const std::string head = star == std::string::npos ? cur : cur.substr(0, star);
        r.frames = star == std::string::npos ? 1 : std::atoi(cur.c_str() + star + 1);
        if (!head.empty() && head[0] == 'k') {
            std::string k;
            for (char h : head.substr(1) + "+") {
                if (h == '+') { if (!k.empty()) r.keys.push_back(std::atoi(k.c_str())); k.clear(); }
                else k.push_back(h);
            }
        } else if (!head.empty() && head[0] == 'w') {
            r.word = static_cast<std::uint32_t>(std::strtoul(head.c_str() + 1, nullptr, 0));
        }
        out.push_back(r);
        cur.clear();
    }
    return out;
}

double xzDist(const float a[3], const float b[3]) {
    const double dx = a[0] - b[0], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dz * dz);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 12) {
        std::fprintf(stderr,
            "usage: player_probe <gamedata> <tables> <set> <ctl> <model> <x> <y> <z> "
            "<facing> <stream> <out.bin> [--verbose] [--list]\n");
        return 2;
    }
    const std::string fr = argv[1], tb = argv[2], set = argv[3], ctlName = argv[4],
                      model = argv[5];
    float pos[3] = {static_cast<float>(std::atof(argv[6])),
                    static_cast<float>(std::atof(argv[7])),
                    static_cast<float>(std::atof(argv[8]))};
    const float facing = static_cast<float>(std::atof(argv[9]));
    const std::string stream = argv[10];
    const std::string outPath = argv[11];
    bool verbose = false, list = false;
    for (int i = 12; i < argc; ++i) {
        if (std::strcmp(argv[i], "--verbose") == 0) verbose = true;
        if (std::strcmp(argv[i], "--list") == 0) list = true;
    }
    if (!omk::safeOutputPath(outPath)) return 2;

    const omk::DataFs fs(fr);
    const auto setPath = fs.resolve("MESHES/DECORS/" + set + ".3DO");
    const auto ctlPath = fs.resolve("ANIMS/" + ctlName + ".CTL");
    const auto modPath = fs.resolve("MESHES/PERSOS/" + model + ".3DO");
    if (!setPath || !ctlPath || !modPath) {
        std::fprintf(stderr, "cannot resolve set/ctl/model (%d %d %d)\n",
                     setPath.has_value(), ctlPath.has_value(), modPath.has_value());
        return 1;
    }
    const auto setData = omk::DataFs::readPath(*setPath);
    const auto ctlData = omk::DataFs::readPath(*ctlPath);
    const auto modData = omk::DataFs::readPath(*modPath);
    const auto soup = omk::collisionSoup(setData, omk::SoupKind::Walkable);
    const omk::CtlFile ctl = omk::readCtl(ctlData);
    if (!ctl.valid || !ctl.exact) { std::fprintf(stderr, "bad .CTL\n"); return 1; }
    std::vector<omk::Mesh> meshes;
    if (const auto h = omk::readHeader(modData)) meshes = omk::readMeshes(modData, *h);
    if (meshes.empty()) { std::fprintf(stderr, "no meshes in %s\n", model.c_str()); return 1; }

    const auto schemes = omk::ControlSchemes::loadJson(tb + "/key_bindings.json");
    omk::Input in(schemes);
    in.installScheme(0);            // Game_Init: Aventure
    in.setRepeatMask(0);            // no screen open: the world gets held keys

    if (list) {
        std::printf("%s: %d groups, %zu states, %zu clips\n", ctlName.c_str(),
                    ctl.groups, ctl.states.size(), ctl.clips.size());
        const int dg = [&]{ for (std::size_t i = 0; i < ctl.groupList.size(); ++i)
                                if (ctl.groupList[i].flags & 1u) return static_cast<int>(i);
                            return -1; }();
        for (std::size_t g = 0; g < ctl.groupList.size(); ++g) {
            const auto& G = ctl.groupList[g];
            if (static_cast<int>(g) != dg && G.id != 100) continue;
            std::printf("group %zu id %d flags 0x%x%s: %d states\n", g, G.id, G.flags,
                        static_cast<int>(g) == dg ? " (DEFAULT)" : "", G.count);
            for (int i = 0; i < G.count; ++i) {
                const auto& s = ctl.states[static_cast<std::size_t>(G.first + i)];
                std::string clip = "-";
                int frames = 0, rootKeys = 0;
                if (s.clip >= 0 && s.clip < static_cast<int>(ctl.clips.size())) {
                    clip = ctl.clips[static_cast<std::size_t>(s.clip)].name;
                    frames = ctl.clips[static_cast<std::size_t>(s.clip)].frames;
                    if (const auto d = omk::animDescriptor(ctlData, ctl.clips[static_cast<std::size_t>(s.clip)].offset))
                        for (const auto& t : d->tracks) if (t.posOffset) rootKeys = t.posKeys;
                }
                std::printf("  [%3d] %-12s in 0x%08x flags 0x%08x pri %d clip %-9s %3d fr root %d keys",
                            G.first + i, s.name.c_str(), s.inputCode, s.flags, s.priority,
                            clip.c_str(), frames, rootKeys);
                if (s.hasTurn)  std::printf("  TURN[%g..%g] %g %g %g", s.turn[0], s.turn[1], s.turn[2], s.turn[3], s.turn[4]);
                if (s.hasShift) std::printf("  SHIFT[%g..%g] %g %g %g", s.shift[0], s.shift[1], s.shift[2], s.shift[3], s.shift[4]);
                if (!s.moveName.empty()) std::printf("  move %s", s.moveName.c_str());
                std::printf("  goto %d  children", s.gotoIdx);
                for (int c : s.childIdx) std::printf(" %d", c);
                std::printf("  blend %d\n", s.blendFrames);
            }
        }
    }

    omk::PlayerController::Setup su;
    su.ctl = &ctl; su.ctlData = ctlData; su.meshes = &meshes; su.soup = &soup;
    for (int k = 0; k < 3; ++k) su.pos[k] = pos[k];
    su.facing = facing;
    omk::PlayerController pc(su);
    std::printf("player: %s on %s (%zu walkable triangles), %d/%d tracks resolve to "
                "meshes of %s; seated at %.1f %.1f %.1f facing %.1f; ctl state %d '%s'\n",
                ctlName.c_str(), set.c_str(), soup.size() / 9, pc.tracksMatched(),
                pc.tracksTotal(), model.c_str(), pc.pos()[0], pc.pos()[1], pc.pos()[2],
                pc.facing(), pc.ctlState(), pc.ctlStateName().c_str());

    // Cef_DefaultGroup's group, and Cef_DefaultEntry's entry in it - the
    // state `SetPersoBankGroup` lands on and the one the walk must return to.
    int defaultState = -1;
    for (const auto& G : ctl.groupList) if (G.flags & 1u) { defaultState = G.defaultEntry; break; }
    const bool startsOnDefault = defaultState >= 0 && pc.ctlState() == defaultState;
    if (list) {
        // which track names have no mesh - said once, so a mismatch is a
        // number with names behind it rather than a number
        std::set<std::string> missing;
        for (const auto& c : ctl.clips)
            if (const auto d = omk::animDescriptor(ctlData, c.offset))
                for (const auto& t : d->tracks) {
                    bool ok = false;
                    for (const auto& m : meshes) {
                        std::string a = t.name, b = m.name;
                        for (auto& ch : a) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                        for (auto& ch : b) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                        if (a == b) { ok = true; break; }
                    }
                    if (!ok) missing.insert(t.name);
                }
        std::printf("tracks with no mesh in %s:", model.c_str());
        for (const auto& n : missing) std::printf(" '%s'", n.c_str());
        std::printf("\n");
    }
    const float facing0 = pc.facing();
    float startPos[3] = {pc.pos()[0], pc.pos()[1], pc.pos()[2]};

    const auto runs = parseStream(stream);
    int frames = 0, offGround = 0;
    double maxGroundErr = 0.0;
    bool leftDefault = false, rootFinite = true;
    std::set<int> visited;
    for (const auto& r : runs) {
        for (int f = 0; f < r.frames; ++f) {
            omk::DeviceState st;
            for (int k : r.keys) st.keyboard.push_back(k);
            std::uint32_t word = r.keys.empty() ? r.word : in.frame(st);
            pc.tick(1.0f, word);
            ++frames;
            const auto& L = pc.last();
            visited.insert(pc.ctlState());
            if (pc.ctlState() != defaultState) leftDefault = true;
            const double err = std::fabs(L.ground - pc.pos()[1]);
            if (err > maxGroundErr) maxGroundErr = err;
            if (!L.onGround) ++offGround;
            for (int k = 0; k < 3; ++k) if (!std::isfinite(L.rootDelta[k])) rootFinite = false;
            if (verbose) {
                const char* stepName = !L.stepped ? "still"
                    : L.step == omk::StepResult::Moved ? "moved"
                    : L.step == omk::StepResult::Reverted ? "REVERTED"
                    : L.step == omk::StepResult::Blocked ? "BLOCKED" : "REFUSED";
                std::printf("%4d word %04x  pos %8.2f %8.2f %8.2f  face %6.1f  actor %d  "
                            "ctl %3d %-10s clip %-9s f %5.1f  d %6.2f %6.2f %6.2f  %s%s\n",
                            frames, word, pc.pos()[0], pc.pos()[1], pc.pos()[2], pc.facing(),
                            static_cast<int>(pc.state()), pc.ctlState(),
                            pc.ctlStateName().c_str(), pc.clipName().c_str(), pc.clipFrame(),
                            L.rootDelta[0], L.rootDelta[1], L.rootDelta[2], stepName,
                            L.onGround ? "" : "  OFF GROUND");
            }
        }
    }
    const bool endsOnDefault = pc.ctlState() == defaultState;
    const auto cam = pc.followCameraSteady();
    const double camDist = xzDist(cam.eye, pc.pos());
    // forward is (sin yaw, -cos yaw) - the ADDRESSES convention, and the
    // heading recipe `atan2(z, x) + 90` is its inverse
    const double fy = pc.facing() * 0.0174532925199433;
    const double fwd[2] = {std::sin(fy), -std::cos(fy)};
    const double camBehind = -((cam.eye[0] - pc.pos()[0]) * fwd[0] +
                               (cam.eye[2] - pc.pos()[2]) * fwd[1]);
    double walkHeading = -1.0;
    if (pc.distanceWalked() > 1.0) {
        walkHeading = std::atan2(pc.pos()[2] - startPos[2], pc.pos()[0] - startPos[0])
                      * 57.29577951308232 + 90.0;
        while (walkHeading < 0.0) walkHeading += 360.0;
        while (walkHeading >= 360.0) walkHeading -= 360.0;
    }
    float turned = pc.facing() - facing0;
    while (turned > 180.0f) turned -= 360.0f;
    while (turned <= -180.0f) turned += 360.0f;

    std::printf("%d frames: walked %.2f (from %.1f %.1f %.1f to %.1f %.1f %.1f), facing "
                "%.1f -> %.1f, ground error max %.3f, %d frames off the ground, %zu ctl "
                "states visited, %s -> %s -> %s\n",
                frames, pc.distanceWalked(), startPos[0], startPos[1], startPos[2],
                pc.pos()[0], pc.pos()[1], pc.pos()[2], facing0, pc.facing(), maxGroundErr,
                offGround, visited.size(), startsOnDefault ? "default" : "NOT default",
                leftDefault ? "left it" : "never left", endsOnDefault ? "default" : "NOT default");
    std::printf("camera (steady): eye %.1f %.1f %.1f  at %.1f %.1f %.1f  fov %.1f  - %.2f "
                "from him in the ground plane, %.2f of it BEHIND his facing; smoothed eye "
                "%.1f %.1f %.1f; walk heading %.1f against facing %.1f\n",
                cam.eye[0], cam.eye[1], cam.eye[2], cam.at[0], cam.at[1], cam.at[2], cam.fov,
                camDist, camBehind, pc.followCamera().eye[0], pc.followCamera().eye[1],
                pc.followCamera().eye[2], walkHeading, pc.facing());
    std::printf("machine: ACTOR_STATE %d, %ld refused transitions, %ld bad landings, "
                "%ld transitions\n", static_cast<int>(pc.state()), pc.runtime().refused(),
                pc.runtime().channel().stats().badLanding,
                pc.runtime().channel().stats().transitions);

    std::vector<std::int32_t> out = {
        frames,
        static_cast<std::int32_t>(std::lround(pc.distanceWalked() * 100.0)),
        static_cast<std::int32_t>(std::lround(maxGroundErr * 100.0)),
        offGround,
        startsOnDefault ? 1 : 0, leftDefault ? 1 : 0, endsOnDefault ? 1 : 0,
        static_cast<std::int32_t>(std::lround(camDist * 100.0)),
        static_cast<std::int32_t>(std::lround(cam.fov * 100.0)),
        static_cast<std::int32_t>(pc.runtime().refused()),
        static_cast<std::int32_t>(pc.runtime().channel().stats().badLanding),
        pc.tracksMatched(), pc.tracksTotal(),
        static_cast<std::int32_t>(pc.state()),
        static_cast<std::int32_t>(std::lround(turned * 100.0)),
        static_cast<std::int32_t>(visited.size()),
        rootFinite ? 1 : 0,
        static_cast<std::int32_t>(std::lround(omk::kFollowEyeBack * 10000.0f)),
        static_cast<std::int32_t>(std::lround(omk::kFollowFov * 100.0f)),
        static_cast<std::int32_t>(std::lround(walkHeading * 100.0)),
        static_cast<std::int32_t>(std::lround(camBehind * 100.0)),
    };
    std::ofstream o(outPath, std::ios::binary);
    for (auto v : out) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) { const char c = static_cast<char>(u >> (8 * k)); o.write(&c, 1); }
    }
    return 0;
}
