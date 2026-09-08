// SPDX-License-Identifier: GPL-3.0-or-later
// THE SLIDER'S OWN DOOR CLIPS - `SLF_112.3DA` and `SLF_113.3DA`.
//
//     slider_doorclip <gamedata>
//
// `Cef_TickChannel`'s ACTOR_STATE switch plays these on the SLIDER while the
// character plays `H_SLDIN` / `H_SLDOUT`, on the same clock (19_dsound.c,
// cases 6 and 8):
//
//     sub_437FC0(sub, dword_90EF28);              // bind
//     sub_437FE0(sub, dword_90EF28, 0.0, a2, &d); // sample at the SAME time
//     sub_438310(slider, &sp);
//     sub_437F80(sub, sp + d, sp.y - 33.149605 + d.y, sp.z + d.z);
//
// so the door is an ANIMATION and not the two-state model swap `sub_4521E0`
// does. This prints each clip's tracks, which mesh of `SLI_FN.3DO` each one
// drives, and how far each turns over the clip - the door is the one that
// moves, and a track that never leaves identity is the body holding still.
#include "actor/pose.h"
#include "formats/mesh3do.h"
#include "platform/datafs.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: slider_doorclip <gamedata>\n"); return 2; }
    const omk::DataFs fs(argv[1]);
    const auto model = fs.read("MESHES/MISC/SLI_FN.3DO");
    std::vector<std::string> names;
    if (const auto h = omk::readHeader(model))
        for (const auto& m : omk::readMeshes(model, *h)) names.push_back(m.name);
    std::printf("SLI_FN.3DO: %zu meshes\n", names.size());
    int fails = 0;
    for (const char* stem : {"SLF_112", "SLF_113"}) {
        const auto d = fs.read(std::string("ANIMS/") + stem + ".3DA");
        if (d.empty()) { std::printf("%s MISSING\n", stem); ++fails; continue; }
        const omk::NodeTracks t = omk::clipTracks(d);
        if (!t.valid()) { std::printf("%s does not decode\n", stem); ++fails; continue; }
        std::printf("%s.3DA: %d frames, %d tracks, root track %d\n",
                    stem, t.frames, t.count, t.rootTrack);
        for (int k = 0; k < t.count; ++k) {
            // the largest angle this track turns away from its own frame 0
            const omk::Quatf& q0 = t.quats[0][static_cast<std::size_t>(k)];
            double worst = 0.0;
            for (int f = 1; f < t.frames; ++f) {
                const omk::Quatf& q = t.quats[static_cast<std::size_t>(f)][static_cast<std::size_t>(k)];
                double dot = q0.w * q.w + q0.x * q.x + q0.y * q.y + q0.z * q.z;
                dot = dot < -1.0 ? -1.0 : (dot > 1.0 ? 1.0 : dot);
                const double a = 2.0 * std::acos(std::fabs(dot)) * 57.29577951308232;
                if (a > worst) worst = a;
            }
            const int id = k < static_cast<int>(t.ids.size()) ? t.ids[static_cast<std::size_t>(k)] : -1;
            const char* nm = (id >= 0 && id < static_cast<int>(names.size()))
                                 ? names[static_cast<std::size_t>(id)].c_str() : "?";
            std::printf("   track %d -> mesh %d %-12s turns %6.1f deg%s\n",
                        k, id, nm, worst, worst > 5.0 ? "   <- MOVES" : "");
        }
        const auto rm = omk::clipRootMotion(d);
        if (!rm.empty())
            std::printf("   root travels %.2f %.2f %.2f over the clip\n",
                        rm.back()[0], rm.back()[1], rm.back()[2]);
    }
    return fails ? 1 : 0;
}
