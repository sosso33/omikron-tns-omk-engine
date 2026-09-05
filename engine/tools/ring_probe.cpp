// SPDX-License-Identifier: GPL-3.0-or-later
//
// How many particles of one SPRITE a scene's set pieces put up, at their
// peak over N frames - for the `ttt` star rings: Impasse's two are anchored
// to a one-record row (undefined heading in the engine, no ring in the
// capture) and GRID's to a three-record row (the capture's dark spiky ring).
//
//     ring_probe <gamedata> <vm_opcodes.json> <START> <SCPTDATA> <area> <scene chunk|-1> <object handle|-1> <sprite id> <frames>
#include "script/area.h"
#include "platform/datafs.h"

#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
    if (argc < 10) {
        std::fprintf(stderr, "usage: ring_probe <gamedata> <vm_opcodes.json> <START> <SCPTDATA> <area> <scene chunk|-1> <object handle|-1> <sprite id> <frames>\n");
        return 2;
    }
    const std::string data = argv[1];
    const auto table = omk::OpcodeTable::loadJson(argv[2]);
    auto state = omk::GameState::fromFile(argv[3]);
    const int area = std::atoi(argv[5]), chunk = std::atoi(argv[6]), handle = std::atoi(argv[7]);
    const int sprite = std::atoi(argv[8]), frames = std::atoi(argv[9]);
    omk::Session s(data + "/IAM", state, table);
    s.loadScene(argv[4], omk::ChunkKind::Area, area);
    s.loadArea(area);
    for (int i = 0; i < 64 && s.loading(); ++i) s.frame();
    if (chunk >= 0) s.sceneLoad(area, chunk);
    if (handle >= 0) {
        omk::Call c; c.op = 58; c.fields = {static_cast<std::int16_t>(handle), 0, 0};
        std::printf("object handle %d -> program %d\n", handle, s.sceneMutable().handle({c}));
    }
    int peak = 0, peakAt = -1, total = 0;
    for (int f = 0; f < frames; ++f) {
        s.frame();
        int n = 0;
        for (const auto& p : s.scene().effects().particles()) if (p.sprite == sprite) ++n;
        if (n > peak) { peak = n; peakAt = f; }
        total += n;
    }
    std::printf("area %d scene chunk %d: sprite %d peaks at %d particles (frame %d), %d particle-frames over %d frames\n",
                area, chunk, sprite, peak, peakAt, total, frames);
    return 0;
}
