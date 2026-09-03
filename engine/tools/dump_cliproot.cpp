// SPDX-License-Identifier: GPL-3.0-or-later
// THE SCENE CLIP'S ROOT MOTION - a `.3DA`'s position track, through the port's
// own reader.
//
//     dump_cliproot <gamedata> <areaChunk> <out.txt>
//
// A `.3DA` track carries POSITION keys at `+24`/`+28` beside its rotation keys
// at `+32`/`+36`. `Anim_RootDelta` (0x004711D0) reads that array as 12 bytes a
// key and SUMS it between the previous frame and the current one:
//
//     v15 = anim[+28];                      // the position keys
//     ...ceil(prev) .. floor(cur), summing f32[3] at 12 * k, from key 1...
//
// and `Script_SelectRelativeBodyAnimation` hands the result to `Actor_MoveBy`
// every tick, having placed the node ONCE from `Path_Sample(path, 1.0, ..., 1)`
// plus the inch offset in params 9/10/11. So key 0 is a sentinel exactly as it
// is for the rotations, and what moves a character is the SUM of keys 1..frame.
//
// This prints what `omk::clipTracks` accumulates, not a second read of the
// file, so the check behind it tests the ported path.
#include "platform/datafs.h"
#include "actor/pose.h"
#include "formats/mesh3do.h"
#include "script/scenerunner.h"

#include <cstdio>
#include <fstream>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: dump_cliproot <gamedata> <areaChunk> <out.txt>\n");
        return 2;
    }
    if (!omk::safeOutputPath(argv[3])) return 2;
    const std::string fr = argv[1];
    omk::OpcodeTable tab;
    omk::SceneRunner sc;
    if (!sc.load(fr + "/SCPTDATA", fr + "/IAM", tab, omk::ChunkKind::Area,
                 std::atoi(argv[2]))) {
        std::fprintf(stderr, "no scene for area %s\n", argv[2]);
        return 1;
    }
    // Start every object the scene declares, so the placements are the ones
    // the script would author. `handle()` is the public entry, and op 60 is
    // `scx.play.actor.wait` - actor first, object second.
    {
        std::vector<omk::Call> calls;
        for (const auto& o : sc.scene().scene().objects)
            calls.push_back({60, {0, static_cast<std::int16_t>(o.handle >> 16)}});
        sc.handle(calls);
    }

    std::ofstream f(argv[3]);
    f << "scene " << sc.file() << '\n';
    for (int c = 0; c < 64; ++c) {
        const auto d = sc.scene().clipData(c);
        if (d.empty()) break;
        const auto t = omk::clipTracks(d);
        if (!t.valid()) continue;
        const auto& last = t.trans.back();
        f << "clip " << c << ' ' << sc.scene().clipName(c)
          << " frames " << t.frames << " tracks " << t.count
          << " move " << last[0] << ' ' << last[1] << ' ' << last[2] << '\n';
        std::printf("clip %d %-14s %3d frames, root moves %7.1f %7.1f %7.1f\n",
                    c, sc.scene().clipName(c).c_str(), t.frames,
                    last[0], last[1], last[2]);
    }

    // THE POSTURE, over the clip's own transition - which is the only way to
    // see the fault that `upright` caused. `composePose`'s `upright` cancels
    // the ROOT's rotation, and that is a dialogue-staging convenience: the
    // engine's `Anim_ApplyNodeFrame` applies every node's quaternion, the
    // root's included. Cancelled, a man crawling on the floor is forced
    // vertical and appears to float; kept, INTRO1 starts him HORIZONTAL and
    // ends him STANDING, which no single frame can show.
    if (argc >= 5) {
        const omk::DataFs fs(fr);
        const auto mp = fs.resolve(std::string("MESHES/PERSOS/") + argv[4] + ".3DO");
        if (mp) {
            const auto md = omk::DataFs::readPath(*mp);
            const auto mh = omk::readHeader(md);
            if (mh) {
                const auto meshes = omk::readMeshes(md, *mh);
                const auto tr = omk::clipTracks(sc.scene().clipData(0));
                for (int fr : {0, 30, 90, 184}) {
                    if (fr >= tr.frames) continue;
                    for (int up = 0; up < 2; ++up) {
                        const auto ps = omk::composePose(meshes, tr, fr, up != 0);
                        float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
                        for (const auto& q : ps)
                            for (int k = 0; k < 3; ++k) {
                                if (q.pos[k] < lo[k]) lo[k] = q.pos[k];
                                if (q.pos[k] > hi[k]) hi[k] = q.pos[k];
                            }
                        const float dx = hi[0] - lo[0], dy = hi[1] - lo[1],
                                    dz = hi[2] - lo[2];
                        const float horiz = dx > dz ? dx : dz;
                        f << "posture " << fr << ' ' << (up ? "upright" : "asis")
                          << ' ' << int(dy + 0.5f) << ' ' << int(horiz + 0.5f) << '\n';
                        std::printf("posture f%-4d %-8s height %5.1f  spread %5.1f"
                                    "  %s\n", fr, up ? "upright" : "asis", dy, horiz,
                                    dy > horiz ? "STANDING" : "down");
                    }
                }
            }
        }
    }

    // ...and the PLACEMENT the same call authors: the path it samples, the
    // inch offset in params 9/10/11 and the Euler in 4/5/6, which
    // `Actor_SetEuler` applies every tick.
    for (const auto& st : sc.started()) {
        if (!st.relative) continue;
        f << "place " << st.object << ' ' << st.name << " path " << st.path
          << " off " << st.offset[0] << ' ' << st.offset[1] << ' ' << st.offset[2]
          << " euler " << st.euler[0] << ' ' << st.euler[1] << ' ' << st.euler[2]
          << '\n';
        std::printf("place %-16s path %d  off %6.1f %6.1f %6.1f  euler %.1f %.1f %.1f\n",
                    st.name.c_str(), st.path, st.offset[0], st.offset[1],
                    st.offset[2], st.euler[0], st.euler[1], st.euler[2]);
    }
    return 0;
}
