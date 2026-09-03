// SPDX-License-Identifier: GPL-3.0-or-later
// Stage 4b, ported: a launch end to end, with the SCENE OBJECTS running.
//
//     launch_scene <gamedata> <vm_opcodes.json> <START> <zoneId> <frames> <out.bin>
//
// Standing in zone 3732 runs SCENE 53's activate script, whose `scx.play*`
// must start the two objects of the scene the chunk actually plays from and
// whose `dialog.start` must load 387. Then the programs are ticked, because
// what they do over the next 120 frames is the part a format check cannot see:
// one is an entrance and must FINISH, one is a loop and must not.
//
// out.bin: int32 found, int32 scxNameLen, the name,
//          int32 nStarted, per start {int32 object, waiting, nameLen, name,
//          howLen, how}, int32 nMissed, int32 dialog, int32 nodes,
//          int32 programs, int32 running, int32 wait5secParam
//
// The last one is not about this zone. `Grid.SCX` - the title sequence's own
// scene - holds an object named `Wait5sec` whose single function is `Wait`
// with the parameter 150.0. The NAME says five seconds and the NUMBER says
// 150, so the object is an independent witness that the engine's time unit is
// a thirtieth of a second (docs/BOOT.md 4): 150 / 30 = 5. Carried here
// because it costs one scene load and it is the only place in the data where
// a designer wrote the conversion down.
#include "formats/iam.h"
#include "platform/datafs.h"
#include "script/dialogue.h"
#include "script/gamestate.h"
#include "script/scenerunner.h"
#include "script/world.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

void put32(std::vector<std::uint8_t>& o, std::int32_t v) {
    const auto u = static_cast<std::uint32_t>(v);
    for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
}
void putStr(std::vector<std::uint8_t>& o, const std::string& s) {
    put32(o, static_cast<std::int32_t>(s.size()));
    for (char c : s) o.push_back(static_cast<std::uint8_t>(c));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 7) {
        std::fprintf(stderr, "usage: launch_scene <gamedata> <vm_opcodes.json> "
                             "<START> <zoneId> <frames> <out.bin>\n");
        return 2;
    }
    const std::string root = argv[1];
    const std::string iam  = root + "/IAM";
    const auto table = omk::OpcodeTable::loadJson(argv[2]);
    auto state = omk::GameState::fromFile(argv[3]);
    const auto want   = static_cast<std::int16_t>(std::atoi(argv[4]));
    const int  frames = std::atoi(argv[5]);

    // the chunk holding that zone - SCENE first, as the reference does
    std::vector<std::byte> held;
    omk::ChunkKind kind = omk::ChunkKind::Scene;
    int chunkIdx = -1;
    omk::Zone target{};
    for (int a = 0; a < 2 && chunkIdx < 0; ++a) {
        const char* nm = a == 0 ? "SCENE" : "AREA";
        const auto k = a == 0 ? omk::ChunkKind::Scene : omk::ChunkKind::Area;
        const auto ar = omk::IamArchive::open(omk::DataFs::readPath(iam + "/" + nm));
        for (std::size_t c = 0; c < ar.size() && chunkIdx < 0; ++c) {
            const auto b = ar.chunk(c);
            if (b.empty()) continue;
            for (const auto& z : omk::zonesOf(b, k))
                if (z.id == want) {
                    held.assign(b.begin(), b.end());
                    kind = k; chunkIdx = static_cast<int>(c); target = z;
                    break;
                }
        }
    }
    std::vector<std::uint8_t> o;
    if (chunkIdx < 0) {
        put32(o, 0);
        if (!omk::safeOutputPath(argv[6])) return 2;
        std::ofstream f(argv[6], std::ios::binary);
        f.write(reinterpret_cast<const char*>(o.data()),
                static_cast<std::streamsize>(o.size()));
        std::fprintf(stderr, "zone %d not found\n", want);
        return 1;
    }

    omk::SceneRunner scene;
    scene.load(root + "/SCPTDATA", iam, table, kind, chunkIdx);

    omk::World w(held, kind, state, table);
    double c[3];
    target.centre(c);
    w.step(c, target.arcMid, false);      // walk in, facing the arc
    w.step(c, target.arcMid, true);       // press action
    scene.handle(w.calls());

    // the conversation the activate script launched
    int dialog = -1, nodes = 0;
    for (const auto& call : w.calls())
        if (call.op == 61 && !call.fields.empty()) dialog = call.fields[0];
    if (dialog >= 0) {
        const auto dd = omk::DataFs::readPath(iam + "/DIALOG");
        const auto ar = omk::IamArchive::open(dd);
        const auto conv = omk::parseConversation(
            dialog, ar.chunk(static_cast<std::size_t>(dialog)));
        nodes = static_cast<int>(conv.nodes.size());
    }

    for (int i = 0; i < frames; ++i) scene.tick(1.0f);

    put32(o, 1);
    putStr(o, scene.file());
    put32(o, static_cast<std::int32_t>(scene.started().size()));
    for (const auto& s : scene.started()) {
        put32(o, s.object); put32(o, s.waiting ? 1 : 0);
        putStr(o, s.name); putStr(o, s.how);
    }
    put32(o, static_cast<std::int32_t>(scene.missed().size()));
    put32(o, dialog); put32(o, nodes);
    put32(o, static_cast<std::int32_t>(scene.programCount()));
    put32(o, scene.programsRunning());
    {
        omk::SceneRunner grid;
        int wait = -1;
        if (grid.load(root + "/SCPTDATA", iam, table, omk::ChunkKind::Area, 118))
            for (const auto& ob : grid.scene().scene().objects)
                if (ob.name == "Wait5sec")
                    for (const auto& fn : ob.functions)
                        if (fn.id == omk::kFnWait && !fn.params.empty()) {
                            float f;
                            std::memcpy(&f, &fn.params[0], 4);
                            wait = static_cast<int>(f);
                        }
        put32(o, wait);
        std::printf("    (Grid.SCX's `Wait5sec` waits %d units - five seconds "
                    "at 30 per second)\n", wait);
    }
    if (!omk::safeOutputPath(argv[6])) return 2;
    std::ofstream f(argv[6], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));

    std::printf("zone %d in %s %d plays its objects from %s\n",
                want, kind == omk::ChunkKind::Scene ? "SCENE" : "AREA",
                chunkIdx, scene.file().c_str());
    for (const auto& s : scene.started())
        std::printf("    scx.play.%-6s object %-4d %-16s %s\n",
                    s.how.c_str(), s.object, s.name.c_str(),
                    s.waiting ? "(the caller waits)" : "");
    if (!scene.missed().empty())
        std::printf("    NOT FOUND: %zu\n", scene.missed().size());
    std::printf("    dialog %d with %d nodes\n", dialog, nodes);
    std::printf("    %zu programs, %d still running after %d frames\n",
                scene.programCount(), scene.programsRunning(), frames);
    return 0;
}
