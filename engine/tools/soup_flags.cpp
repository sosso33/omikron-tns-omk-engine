// SPDX-License-Identifier: GPL-3.0-or-later
// What the engine's two mesh exclusions remove from a set's collision soups.
//
//     soup_flags <gamedata> [set ...]
//
// `Sweep_MeshTest` (0x004AD460) never sweeps a mesh flagged 0x20000000 or
// 0x41; the mover's step refusal (21_d3d.c:2644) never steps ONTO one flagged
// 0x20000000. Both are exclusions. This counts the meshes and triangles each
// takes out, per set, so the change has a size rather than a claim.
#include "formats/mesh3do.h"
#include "platform/datafs.h"
#include <cstdio>
#include <string>
#include <vector>
int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: soup_flags <gamedata> [set ...]\n"); return 2; }
    const std::string fr = argv[1];
    std::vector<std::string> sets;
    for (int i = 2; i < argc; ++i) sets.push_back(argv[i]);
    if (sets.empty()) sets = {"Abank","ACSgrot","Anekbah","AToit","AIMPASSE","AAPKAYL",
                              "L_Khonsu","Lahoreh","LMFinkar","LMonaste","LMprinci",
                              "Sconcert","Sprison","Sttra01"};
    int setsHit = 0, meshNoStep = 0, meshNoSweep = 0;
    for (const auto& s : sets) {
        const auto d = omk::DataFs::readPath(fr + "/MESHES/DECORS/" + s + ".3DO");
        if (d.empty()) continue;
        const auto h = omk::readHeader(d);
        if (!h) continue;
        const auto ms = omk::readMeshes(d, *h);
        int nostep = 0, nosweep = 0, tri_nostep = 0, tri_nosweep = 0;
        for (const auto& m : ms) {
            const auto f = static_cast<std::uint32_t>(m.flags);
            const int t = m.triangles > 0 ? m.triangles : 0;
            if (f & 0x20000000u) { ++nostep; tri_nostep += t; }
            if (f & (0x20000000u | 0x41u)) { ++nosweep; tri_nosweep += t; }
        }
        if (nosweep) ++setsHit;
        meshNoStep += nostep; meshNoSweep += nosweep;
        std::printf("%-10s %4zu meshes | no-step %3d (%4d tris) | no-sweep %3d (%4d tris)\n",
                    s.c_str(), ms.size(), nostep, tri_nostep, nosweep, tri_nosweep);
    }
    std::printf("\n%d of %zu sets carry an excluded mesh; %d no-step, %d no-sweep in all\n",
                setsHit, sets.size(), meshNoStep, meshNoSweep);
    return 0;
}
