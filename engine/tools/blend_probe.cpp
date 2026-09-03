// SPDX-License-Identifier: GPL-3.0-or-later
// THE POSE BLEND, PROBED - `qslerp`, `blendTracks` and `morphBlendFrames`
// (actor/pose.h, BLENDING TWO POSES) on synthetic tracks the answer is known
// for.
//
//     blend_probe <out.txt>
//
// A rotation of 90 degrees about Y eased halfway toward identity must be 45
// degrees about Y (w = cos 22.5 = 0.9239, y = sin 22.5 = 0.3827); the weight
// is quantised to k/256 so t = 1 lands on 255/256 of the way; the shortest
// arc is taken when the second quaternion is negated; a cancelled root blends
// as identity; and the fade is a quarter of the line's frames, at most 30.
#include "actor/pose.h"
#include "platform/datafs.h"

#include <cmath>
#include <cstdio>
#include <fstream>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: blend_probe <out.txt>\n"); return 2; }
    if (!omk::safeOutputPath(argv[1])) return 2;
    std::ofstream out(argv[1]);
    const float c45 = std::cos(0.7853981634f), s45 = std::sin(0.7853981634f);
    omk::Quatf id;
    omk::Quatf y90{c45, 0, s45, 0};
    omk::Quatf x90{c45, s45, 0, 0};
    // qslerp at 0, 0.5 and 1
    {
        const auto q0 = omk::qslerp(y90, id, 0.0f);
        const auto qh = omk::qslerp(y90, id, 0.5f);
        const auto q1 = omk::qslerp(y90, id, 1.0f);
        out << "slerp0 " << q0.w << ' ' << q0.y << "\n";
        out << "slerp05 " << qh.w << ' ' << qh.y << "\n";
        out << "slerp1 " << q1.w << ' ' << q1.y << "\n";
        // the far side: -identity is the same rotation, and the arc must be short
        omk::Quatf nid{-1, 0, 0, 0};
        const auto qn = omk::qslerp(y90, nid, 0.5f);
        out << "slerp-neg " << std::fabs(qn.w) << ' ' << std::fabs(qn.y) << "\n";
    }
    // two one-frame tracks over mesh ids 0 (root) and 1
    omk::NodeTracks a, b;
    a.count = b.count = 2; a.frames = b.frames = 1; a.rootTrack = b.rootTrack = 0;
    a.ids = {0, 1}; b.ids = {0, 1};
    a.quats = {{id, y90}};  b.quats = {{x90, id}};
    a.trans = {{0, 0, 0}};  b.trans = {{10, 20, 30}};
    {
        const auto m = omk::blendTracks(a, 0, false, b, 0, false, 0.5f);
        out << "mix ids " << m.ids.size() << " frames " << m.frames
            << " root " << m.rootTrack << "\n";
        out << "mix node1 " << m.quats[0][1].w << ' ' << m.quats[0][1].y << "\n";
        out << "mix root " << m.quats[0][0].w << ' ' << m.quats[0][0].x << "\n";
        out << "mix trans " << m.trans[0][0] << ' ' << m.trans[0][1] << ' ' << m.trans[0][2] << "\n";
        const auto mc = omk::blendTracks(a, 0, false, b, 0, true, 0.5f);
        out << "mix root-cancelled " << mc.quats[0][0].w << ' ' << mc.quats[0][0].x << "\n";
    }
    out << "blend-frames " << omk::morphBlendFrames(922) << ' ' << omk::morphBlendFrames(40)
        << ' ' << omk::morphBlendFrames(0) << "\n";
    return 0;
}
