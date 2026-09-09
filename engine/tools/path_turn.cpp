// SPDX-License-Identifier: GPL-3.0-or-later
// A PATH TURNED INTO THE SET - `Script_MoveObjectOnPath`'s params 12/13/14.
//
//     path_turn <gamedata> <tables>
//
// One lift is authored once and every hall installs it at its own angle, so
// the handler carries three degrees and turns the path about its own first key
// before either of its two arms places anything. The port dropped them, and
// every lift door in the game then slid along the UNTURNED axis - out of its
// wall and into the room. A reader, 2026-09-09: *some doors do not open
// correctly*, with Hall 27 in the shot.
//
// Hall 27's `levDOORopen` is `euler 0 230 0`, and its two leaves stand in one
// wall at x 4512, z -765 and -741. So the test is a shape and not a
// coordinate: a leaf may travel along the wall and must not leave it. The
// DISPLACEMENT (`pos - from`) is what this prints, which needs no set geometry
// and no anchor - it is the whole of what the arm contributes.
#include "platform/datafs.h"
#include "script/area.h"
#include "script/gamestate.h"

#include <map>

#include "formats/scx.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace {
float bitsToFloat(std::int32_t b) { float f; std::memcpy(&f, &b, 4); return f; }
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: path_turn <gamedata> <tables>\n");
        return 2;
    }
    const std::string fr = argv[1], tb = argv[2];
    const auto table = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    if (!table.valid()) { std::fprintf(stderr, "no opcode table\n"); return 1; }
    const std::string iam = fr + "/IAM";
    auto state = omk::GameState::fromFile(iam + "/START");
    omk::Session s(iam, state, table);
    s.loadAnnounceMap(tb + "/vm_announce.json");
    s.loadArea(229);                                   // Anekbah Hall 27
    s.loadScene(fr + "/SCPTDATA", omk::ChunkKind::Area, 229);
    for (int f = 0; f < 5; ++f) s.frame();

    // `levDOORopen`, handle 0x000b - the ground lift's doors opening. Started
    // the way a zone script starts one (op 58, the waiting scene variant).
    omk::Call c;
    c.op = 58;
    c.fields = {0x000b, 0, 0};
    const int prog = s.sceneMutable().handle({c});
    std::printf("Hall 27 levDOORopen -> program %d\n", prog);

    struct Ext { float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f}; int n = 0; };
    std::map<std::string, Ext> ext;
    for (int f = 0; f < 60; ++f) {
        s.frame();
        for (const auto& mo : s.scene().motions()) {
            if (!mo.hasFrom) continue;
            Ext& e = ext[mo.name];
            ++e.n;
            for (int k = 0; k < 3; ++k) {
                const float d = mo.pos[k] - mo.from[k];
                if (d < e.lo[k]) e.lo[k] = d;
                if (d > e.hi[k]) e.hi[k] = d;
            }
        }
    }
    // ...AND THE CORPUS, so the runtime shape above is not one hall's accident:
    // how many calls carry an angle at all, and on which axis.
    long calls = 0, euler = 0, xz = 0;
    std::set<std::string> files;
    namespace fs = std::filesystem;
    for (auto& de : fs::directory_iterator(fr + "/SCPTDATA")) {
        auto p = de.path();
        auto e = p.extension().string();
        for (auto& ch : e) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (e != ".scx") continue;
        std::ifstream in(p, std::ios::binary);
        std::vector<char> d((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
        auto sc = omk::readScx({reinterpret_cast<const std::byte*>(d.data()), d.size()});
        if (!sc.valid) continue;
        for (const auto& o : sc.objects)
            for (const auto& fn : o.functions) {
                if (fn.id != 0x03000008u) continue;
                ++calls;
                const float ex = fn.params.size() > 12 ? bitsToFloat(fn.params[12]) : 0.0f;
                const float ey = fn.params.size() > 13 ? bitsToFloat(fn.params[13]) : 0.0f;
                const float ez = fn.params.size() > 14 ? bitsToFloat(fn.params[14]) : 0.0f;
                if (ex == 0.0f && ey == 0.0f && ez == 0.0f) continue;
                ++euler;
                files.insert(p.filename().string());
                if (ex != 0.0f || ez != 0.0f) ++xz;
            }
    }
    std::printf("corpus MoveObjectOnPath %ld, turned %ld in %zu files, "
                "X or Z %ld\n", calls, euler, files.size(), xz);

    for (const auto& kv : ext) {
        const Ext& e = kv.second;
        std::printf("%-14s samples %3d  travel x %6.1f  y %6.1f  z %6.1f\n",
                    kv.first.c_str(), e.n,
                    std::max(std::fabs(e.lo[0]), std::fabs(e.hi[0])),
                    std::max(std::fabs(e.lo[1]), std::fabs(e.hi[1])),
                    std::max(std::fabs(e.lo[2]), std::fabs(e.hi[2])));
    }
    return 0;
}
