// SPDX-License-Identifier: GPL-3.0-or-later
//
// `Script_ScaleObjectX/Y/Z` (0x03000023..25) - the last of the eleven scene
// functions the port did nothing for (omk-play 71). All 35 shipped sites are
// in `Aapkayl.SCX`, Kay'l's apartment: the transfer tube's beam meshes grow
// along Y from 1 to 35 over 24, 36 or 60 frames, and swell on X/Z in a
// 0.3-frame ramp.
//
// This starts `00 Tube Begin End` (object 42, handle 169) - step 1 ramps
// `cent int` Y 1 -> 35 over 24 frames with a sync chain that steps `cent
// med`'s X/Z 1 -> 1.5 and `cent ext` Y over 60 frames (61 ticks), then a
// 105-frame `Script_Wait` (0x42D20000), so step 3 (35 -> 1) begins on tick 167 - and
// reads the scene's node scales back tick by tick.
//
//     scale_probe <gamedata> <vm_opcodes.json> <START> <SCPTDATA> <out.bin>
#include "script/area.h"
#include "platform/datafs.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr,
            "usage: scale_probe <gamedata> <vm_opcodes.json> <START> <SCPTDATA> <out.bin>\n");
        return 2;
    }
    if (!omk::safeOutputPath(argv[5])) return 2;
    const std::string data = argv[1];
    const auto table = omk::OpcodeTable::loadJson(argv[2]);
    auto state = omk::GameState::fromFile(argv[3]);
    omk::Session s(data + "/IAM", state, table);
    s.loadScene(argv[4], omk::ChunkKind::Area, 237);       // Aapkayl.SCX
    s.loadArea(237);
    for (int i = 0; i < 64 && s.loading(); ++i) s.frame();
    std::printf("area %d  scx '%s'\n", s.currentArea(), s.scene().file().c_str());

    auto& sc = s.sceneMutable();
    omk::Call c;
    c.op = 58;
    c.fields = {169, 0, 0};
    const int idx = sc.handle({c});
    std::printf("  00 Tube Begin End (handle 169) -> program %d\n", idx);

    const auto yOf = [&](const char* name) {
        const auto it = s.scene().nodeScales().find(name);
        return it == s.scene().nodeScales().end() ? -1.0f : it->second.s[1];
    };
    const auto xOf = [&](const char* name) {
        const auto it = s.scene().nodeScales().find(name);
        return it == s.scene().nodeScales().end() ? -1.0f : it->second.s[0];
    };
    float y1 = 0, y13 = 0, y25 = 0, y26 = 0, y49 = 0, y162 = 0, y175 = 0, x1 = 0, x2 = 0;
    int nodes = 0;
    for (int f = 1; f <= 180; ++f) {
        s.frame();
        const float y = yOf("cent int"), x = xOf("cent med");
        if (f == 1)  { y1 = y; x1 = x; }
        if (f == 2)  { x2 = x; }
        if (f == 13) y13 = y;
        if (f == 25) y25 = y;
        if (f == 26) y26 = y;
        if (f == 49) y49 = y;
        if (f == 167) y162 = y;
        if (f == 175) y175 = y;
        if (f <= 3 || f == 13 || f == 24 || f == 25 || f == 26 || f == 49 || (f >= 58 && f <= 64) || (f >= 165 && f <= 168) || f == 175)
            std::printf("  tick %3d: pc %d  cent int Y %.3f   cent med X %.3f   (%zu nodes scaled)\n",
                        f, s.scene().programPc(idx), y, x, s.scene().nodeScales().size());
    }
    nodes = static_cast<int>(s.scene().nodeScales().size());
    std::printf("cent int Y: tick 1 %.2f, 13 %.2f, 25 %.2f, 26 %.2f, 49 %.2f, 167 %.2f, 175 %.2f; cent med X: tick 1 %.2f, 2 %.2f; %d nodes\n",
                y1, y13, y25, y26, y49, y162, y175, x1, x2, nodes);
    std::ofstream o(argv[5], std::ios::binary);
    const auto r = [](float v) { return static_cast<int>(std::lround(v * 100)); };
    const int rec[10] = {r(y1), r(y13), r(y25), r(y26), r(y49), r(y162), r(y175), r(x1), r(x2), nodes};
    o.write(reinterpret_cast<const char*>(rec), sizeof rec);
    return 0;
}
