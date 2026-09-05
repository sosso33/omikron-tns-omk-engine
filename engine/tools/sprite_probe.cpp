// SPDX-License-Identifier: GPL-3.0-or-later
//
// The SPRITE FAMILY of scene functions - `Script_Display3DSprite` (232 sites
// in 65 scenes) and the five that set an instance's type, frame, scales and
// roll - which the port did nothing for until 2026-09-05 (omk-play 71). A
// scene that showed a flash, a glow or a smoke puff through them drew
// NOTHING.
//
// `Impasse.SCX` carries both shapes: object 10 `effects2_glow` (handle 26) is the full
// chain (SetSpriteType 4, SetSpriteFrame 0, SetSpriteRolling, ScaleSpriteOnX,
// ScaleSpriteOnY, Display3DSprite 60 frames) on sprite row 1, and object 21
// `fx2_smoke1` (handle 134) is a bare Display3DSprite on row 0. This starts
// each by its `scx.play` operand, which is the HANDLE >> 16, feeds
// the runner a camera target, and reads the scene's instances back.
//
//     sprite_probe <gamedata> <vm_opcodes.json> <START> <SCPTDATA> <out.bin>
#include "script/area.h"
#include "platform/datafs.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr,
            "usage: sprite_probe <gamedata> <vm_opcodes.json> <START> <SCPTDATA> <out.bin>\n");
        return 2;
    }
    if (!omk::safeOutputPath(argv[5])) return 2;
    const std::string data = argv[1];
    const auto table = omk::OpcodeTable::loadJson(argv[2]);
    auto state = omk::GameState::fromFile(argv[3]);
    omk::Session s(data + "/IAM", state, table);
    s.loadScene(argv[4], omk::ChunkKind::Area, 222);       // AIMPASSE -> Impasse.SCX
    s.loadArea(222);
    for (int i = 0; i < 64 && s.loading(); ++i) s.frame();
    std::printf("area %d  scx '%s'  sprite rows %d\n", s.currentArea(),
                s.scene().file().c_str(), s.scene().loaded() ? 12 : 0);

    auto& sc = s.sceneMutable();
    const auto start = [&](int obj) {
        omk::Call c;
        c.op = 58;                       // scx.play: the object's handle >> 16 first
        c.fields = {static_cast<std::int16_t>(obj), 0, 0};
        return sc.handle({c});
    };

    // object 10, the full chain on row 1
    const int p10 = start(26);
    std::printf("  effects2_glow (10) -> program %d\n", p10);
    float anchor[3] = {100.0f, 200.0f, 300.0f};
    int linkedAfter1 = 0, frame = -1, type = -1, id1 = -1;
    float sx = 0, sy = 0, roll = 0;
    int trackingTicks = 0, doneAt = -1;
    for (int f = 1; f <= 70; ++f) {
        sc.setSpriteAnchor(anchor);
        s.frame();
        const auto it = s.scene().sprites().find(1);
        if (it == s.scene().sprites().end()) continue;
        const auto& sp = it->second;
        if (f == 1) {
            linkedAfter1 = sp.linked ? 1 : 0;
            frame = sp.frame; type = sp.type; id1 = sp.id;
            sx = sp.sx; sy = sp.sy; roll = sp.roll;
            std::printf("  tick 1: linked %d frame %d type %d id %d scale %.3f/%.3f roll %.3f at %.0f,%.0f,%.0f\n",
                        sp.linked, sp.frame, sp.type, sp.id, sp.sx, sp.sy, sp.roll,
                        sp.pos[0], sp.pos[1], sp.pos[2]);
        }
        if (sp.tracking) ++trackingTicks;
        else if (doneAt < 0) doneAt = f;
    }
    // the sprite is done: move the camera and see that it STAYS
    float moved[3] = {-500.0f, 0.0f, 900.0f};
    sc.setSpriteAnchor(moved);
    s.frame();
    int frozen = 0, stillLinked = 0;
    {
        const auto it = s.scene().sprites().find(1);
        if (it != s.scene().sprites().end()) {
            const auto& sp = it->second;
            frozen = (std::fabs(sp.pos[0] - anchor[0]) < 0.01f &&
                      std::fabs(sp.pos[2] - anchor[2]) < 0.01f && !sp.tracking) ? 1 : 0;
            stillLinked = sp.linked ? 1 : 0;
        }
    }
    std::printf("  positioned for %d ticks, first idle tick %d, frozen after the camera moved %d, still linked %d\n",
                trackingTicks, doneAt, frozen, stillLinked);

    // object 21, a bare Display3DSprite on row 0: the instance's DEFAULTS
    const int p21 = start(134);
    std::printf("  fx2_smoke1 (21) -> program %d\n", p21);
    sc.setSpriteAnchor(moved);
    s.frame();
    int row0linked = 0, row0type = -1, row0frame = -1, row0id = -1;
    {
        const auto it = s.scene().sprites().find(0);
        if (it != s.scene().sprites().end()) {
            row0linked = it->second.linked ? 1 : 0;
            row0type = it->second.type; row0frame = it->second.frame; row0id = it->second.id;
        }
    }
    std::printf("  row 0: linked %d type %d frame %d id %d; %zu instances\n",
                row0linked, row0type, row0frame, row0id, s.scene().sprites().size());

    std::ofstream o(argv[5], std::ios::binary);
    const int rec[14] = {linkedAfter1, frame, type, id1,
                         static_cast<int>(std::lround(sx * 1000)),
                         static_cast<int>(std::lround(sy * 1000)),
                         static_cast<int>(std::lround(roll * 1000)),
                         trackingTicks, doneAt, frozen, stillLinked,
                         row0linked, row0type, row0id};
    o.write(reinterpret_cast<const char*>(rec), sizeof rec);
    return 0;
}
