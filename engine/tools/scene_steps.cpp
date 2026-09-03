// SPDX-License-Identifier: GPL-3.0-or-later
// A SCENE OBJECT'S PROGRAM IS A SEQUENCE, and the pose must follow it.
//
//     scene_steps <gamedata> <vm_opcodes.json> <out.bin>
//
// `Started::clip` was filled once at start from the object's FIRST body
// animation and never refreshed, so any object whose program has more than one
// animation step posed its whole run with step 0's clip - and, since the body
// is snapped to that clip's root key 0, stood in step 0's place for the whole
// shot.
//
// `Impasse.SCX`'s `A_2_DemonLook` is the case a reader reported: clip 15
// (`1-02DEM`, the demon perched on the wall, 91 frames) and then clip 17
// (`1-03DEM`, his jump down, 41). Frozen on clip 15 the demon never descends,
// and the next beat's clip teleports him to the ground - which is what "the
// shot launches too late and part of the animation is missing" looks like.
//
// The invariant this prints is one the DATA can fail and no reader can fake:
// the three clips of the beat are authored to CHAIN, each starting where the
// last one ends -
//
//     15  6953 -121 3188 -> ends 6861 -267 3195      (perched, then shuffles)
//     17  6861 -121 3195 -> ends 6642 -105 3211      (the jump)
//     25  6642 -121 3211 -> ends 6643 -120 3211      (standing, on the ground)
//
// - so a run that plays them in order walks a continuous path and one that
// stops after the first does not. The gaps below are the test.
//
// It also prints the SOUND cues the beat fires as it goes, which is a courtesy
// rather than the test: `scene_sounds` is what asserts them.
#include "actor/pose.h"
#include "formats/mesh3do.h"
#include "platform/datafs.h"
#include "script/scenerunner.h"
#include "script/script.h"

#include <cctype>
#include <cmath>
#include <map>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

bool iequalsName(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

// The clip's world position at frame f: root key 0 plus the motion summed to
// there, which is `Anim_RootDelta` -> `Actor_MoveBy`.
bool at(const omk::ScxRuntime& rt, int clip, int f, float out[3]) {
    if (!omk::clipRootStart(rt.clipData(clip), out)) return false;
    const auto rm = omk::clipRootMotion(rt.clipData(clip));
    if (rm.empty()) return true;
    if (f < 0) f = 0;
    if (f >= static_cast<int>(rm.size())) f = static_cast<int>(rm.size()) - 1;
    for (int k = 0; k < 3; ++k) out[k] += rm[static_cast<std::size_t>(f)][static_cast<std::size_t>(k)];
    return true;
}
int gap(const omk::ScxRuntime& rt, int a, int b) {
    float pa[3], pb[3];
    if (!at(rt, a, 1 << 20, pa) || !at(rt, b, 0, pb)) return -1;
    double d = 0;
    for (int k = 0; k < 3; ++k) d += double(pa[k] - pb[k]) * (pa[k] - pb[k]);
    return static_cast<int>(std::lround(std::sqrt(d)));
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: scene_steps <gamedata> <vm_opcodes.json> "
                             "<out.bin>\n");
        return 2;
    }
    const std::string fr = argv[1];
    const auto table = omk::OpcodeTable::loadJson(argv[2]);
    omk::SceneRunner sc;
    // SCENE 55 is the Impasse, played over AREA 222 - `resolveScx`'s own job.
    if (!sc.load(fr + "/SCPTDATA", fr + "/IAM", table, omk::ChunkKind::Scene, 55)) {
        std::fprintf(stderr, "cannot make Impasse.SCX resident\n");
        return 1;
    }
    const omk::DataFs fs(fr + "/SCPTDATA");
    // `scx.play.actor.wait 57, 223, 0` - the beat the script fires before the
    // demon's line, and the object `sautdemon` is linked to.
    sc.handle({omk::Call{60, {57, 223, 0}}});

    std::printf("sound cues the beat fires:\n");
    int firstClip = -1, secondClip = -1, changeAt = -1, animAtChange = -1;
    int steps = 0;
    for (int f = 0; f < 200 && sc.programsRunning() > 0; ++f) {
        const int clip = sc.started().empty() ? -1 : sc.started()[0].clip;
        if (clip != firstClip && firstClip == -1) firstClip = clip;
        else if (clip != firstClip && changeAt < 0) {
            secondClip = clip;
            changeAt = f;
            animAtChange = static_cast<int>(sc.programAnimClock(0));
        }
        if (clip >= 0) ++steps;
        sc.tick(1.0f);
        for (const auto& fs : sc.sounds())
            std::printf("   f%-4d wav %-3d '%s'%s%s\n", f + 1, fs.cue.wav,
                        sc.scene().wavName(fs.cue.wav).c_str(),
                        fs.cue.sync ? "  (sync)" : "  (plain)",
                        fs.cue.loop ? "  looping" : "");
    }
    const int ran = static_cast<int>(sc.programClock(0));

    // ---- and the CRATES, which are the path family's case -------------
    // `C_1_BoxMoves` is four `Script_MoveObjectOnPath` in one chain, and the
    // paths are named `CaisseA`..`CaisseD` - the crates. Each is 146 frames,
    // so the object is busy 146; its editing `boxblow` holds 185, the extra
    // 39 being the shot staying on them after they land.
    int crateFrames = -1, crateDur = -1, crateNodes = 0, crateFell = 0, crateNamed = 0;
    // the Impasse's own set, so the four node names can be shown to be MESHES
    // of it rather than strings that resolve to nothing
    std::vector<std::string> setMeshNames;
    {
        const omk::DataFs decors(fr + "/MESHES/DECORS");
        if (const auto mp = decors.resolve("AImpasse.3DO")) {
            const auto mdd = omk::DataFs::readPath(*mp);
            if (const auto mh = omk::readHeader(mdd))
                for (const auto& mm : omk::readMeshes(mdd, *mh))
                    setMeshNames.push_back(mm.name);
        }
    }
    {
        omk::SceneRunner r;
        r.load(fr + "/SCPTDATA", fr + "/IAM", table, omk::ChunkKind::Scene, 55);
        r.handle({omk::Call{58, {259, 0, 0}}});
        int n = 0;
        std::map<std::string, std::pair<float, float>> ytravel;   // first, last
        while (r.programsRunning() > 0 && n < 4000) {
            r.tick(1.0f);
            ++n;
            for (const auto& mo : r.motions()) {
                if (!mo.placed) continue;
                auto it = ytravel.find(mo.name);
                if (it == ytravel.end()) ytravel[mo.name] = {mo.pos[1], mo.pos[1]};
                else it->second.second = mo.pos[1];
            }
        }
        crateFrames = n;
        crateNodes = static_cast<int>(ytravel.size());
        // Y points DOWN, so a crate that FALLS ends at a LARGER y. Two of the
        // four do - `Caisse01` and `Caisse 13` start about 80 units up - and
        // two start on the ground and slide. That split is the test: a motion
        // that never ran leaves 0 nodes, and one stuck at t=0 leaves 0 fallen.
        for (const auto& [nm, yy] : ytravel) {
            if (yy.second - yy.first > 40.0f) ++crateFell;
            const auto* set = &setMeshNames;
            bool found = false;
            for (const auto& mn : *set) if (iequalsName(mn, nm)) { found = true; break; }
            if (found) ++crateNamed;
        }
        if (const auto* e = r.editingOf(259)) crateDur = static_cast<int>(e->duration);
    }

    const omk::ScxRuntime rt(omk::DataFs::readPath(
        fs.resolve("Impasse.SCX").value_or(fr + "/SCPTDATA/Impasse.SCX")));
    const int gap1517 = gap(rt, 15, 17);
    const int gap1725 = gap(rt, 17, 25);

    // WHERE THE DEMON IS at program frame 120 - three quarters through the
    // shot, and after `sautdemon`'s camera has cut to the low angle that
    // watches him come down - under the two readings. Live he has landed;
    // frozen on step 0 he is still stuck on the wall, 200 units up, because
    // clip 15 clamps at its last frame and never descends.
    float live[3] = {0, 0, 0}, frozen[3] = {0, 0, 0};
    at(rt, secondClip < 0 ? firstClip : secondClip,
       120 - (changeAt < 0 ? 0 : changeAt), live);
    at(rt, firstClip, 120, frozen);
    double sep = 0;
    for (int k = 0; k < 3; ++k) sep += double(live[k] - frozen[k]) * (live[k] - frozen[k]);
    const int apart = static_cast<int>(std::lround(std::sqrt(sep)));

    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    put32(firstClip); put32(secondClip); put32(changeAt);
    put32(animAtChange); put32(ran); put32(gap1517); put32(gap1725);
    put32(crateFrames); put32(crateDur);
    put32(crateNodes); put32(crateNamed); put32(crateFell);
    put32(static_cast<std::int32_t>(std::lround(live[1])));
    put32(static_cast<std::int32_t>(std::lround(frozen[1])));
    put32(apart);
    if (!omk::safeOutputPath(argv[3])) return 2;
    std::ofstream f(argv[3], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));

    std::printf("A_2_DemonLook: clip %d then clip %d at frame %d (its own clock "
                "back to %d); the program ran %d frames\n",
                firstClip, secondClip, changeAt, animAtChange, ran);
    std::printf("C_1_BoxMoves (the crates) runs %d frames; its editing holds %d; "
                "%d nodes moved, %d of them meshes of AImpasse, %d FELL\n",
                crateFrames, crateDur, crateNodes, crateNamed, crateFell);
    std::printf("the beat's clips chain: 15->17 gap %d units, 17->25 gap %d\n",
                gap1517, gap1725);
    std::printf("at program frame 120 the demon is at y %d (following the pc) "
                "against y %d frozen on step 0 - %d units apart\n",
                static_cast<int>(std::lround(live[1])),
                static_cast<int>(std::lround(frozen[1])), apart);
    (void)steps;
    return 0;
}
