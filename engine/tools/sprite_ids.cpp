// SPDX-License-Identifier: GPL-3.0-or-later
// Which sprite IDS a scene's stream registers, and which ids that scene's own
// ambient effects ask for. `Sfx_TickAmbient` resolves an effect's `+8` through
// the SCENE (`sub_4A5800`), so a viewer that loads one scene's sprites and
// then changes scene has the wrong table.
//
//     sprite_ids <gamedata> <out.bin> [scene.SCX...]
#include "formats/scx.h"
#include "formats/sfx.h"
#include "platform/datafs.h"
#include <cstdio>
#include <fstream>
#include <set>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: sprite_ids <gamedata> <out.bin> [scx...]\n"); return 2; }
    const omk::DataFs fs(argv[1]);
    if (!omk::safeOutputPath(argv[2])) return 2;
    // The GLOBAL library, which is what a viewer has if it never reloads.
    std::set<int> global;
    if (const auto gp = fs.resolve("SCPTDATA/aventure.SCX")) {
        const auto gd = omk::DataFs::readPath(*gp);
        for (const auto& sp : omk::readScxStream(gd).sprites)
            if (sp.id >= 0) global.insert(sp.id);
    }
    int scenes = 0, selfSupplied = 0, needLocal = 0, collide = 0, wantTotal = 0;
    for (const auto& path : fs.list("SCPTDATA", ".sfx")) {
        const std::string base = path.substr(path.find_last_of("/\\") + 1);
        std::string stem = base.substr(0, base.rfind('.'));
        const auto scp = fs.resolve("SCPTDATA/" + stem + ".SCX");
        if (!scp) continue;
        const auto sd = omk::DataFs::readPath(path);
        const omk::SfxFile sx = omk::readSfx(sd);
        if (sx.effects.empty()) continue;
        const auto cd = omk::DataFs::readPath(*scp);
        std::set<int> reg;
        for (const auto& sp : omk::readScxStream(cd).sprites)
            if (sp.id >= 0) reg.insert(sp.id);
        std::set<int> want;
        for (const auto& e : sx.effects) want.insert(e.sprite);
        if (want.empty()) continue;
        ++scenes;
        bool allSelf = true, anyOutsideGlobal = false;
        for (int id : want) {
            ++wantTotal;
            if (!reg.count(id)) allSelf = false;
            if (!global.count(id)) anyOutsideGlobal = true;
            else if (reg.count(id)) ++collide;   // both define it - the global would WIN wrongly
        }
        if (allSelf) ++selfSupplied;
        if (anyOutsideGlobal) ++needLocal;
    }
    std::printf("%d scenes with ambient effects; %d supply every wanted sprite id from "
                "their OWN stream; %d want at least one id the global library lacks; "
                "%d wanted ids exist in BOTH (the global would win and draw the wrong "
                "picture); %d wanted ids in all\n",
                scenes, selfSupplied, needLocal, collide, wantTotal);
    const std::int32_t out[5] = {scenes, selfSupplied, needLocal, collide, wantTotal};
    std::ofstream of(argv[2], std::ios::binary);
    of.write(reinterpret_cast<const char*>(out), sizeof out);
    for (int a = 3; a < argc; ++a) {
        const std::string name = argv[a];
        const auto p = fs.resolve("SCPTDATA/" + name);
        if (!p) { std::printf("%-16s (no such file)\n", name.c_str()); continue; }
        const auto d = omk::DataFs::readPath(*p);
        const auto st = omk::readScxStream(d);
        std::set<int> ids;
        for (const auto& sp : st.sprites) if (sp.id >= 0) ids.insert(sp.id);
        std::printf("%-16s registers sprite ids:", name.c_str());
        for (int i : ids) std::printf(" %d", i);
        std::printf("\n");
        // ...and what its own .sfx effects ask for
        std::string sfxn = name;
        const auto dot = sfxn.rfind('.');
        if (dot != std::string::npos) sfxn = sfxn.substr(0, dot) + ".sfx";
        const auto sp2 = fs.resolve("SCPTDATA/" + sfxn);
        if (!sp2) continue;
        const auto sd = omk::DataFs::readPath(*sp2);
        const omk::SfxFile sx = omk::readSfx(sd);
        std::set<int> want;
        for (const auto& e : sx.effects) want.insert(e.sprite);
        std::printf("%-16s its effects want:   ", sfxn.c_str());
        for (int i : want) std::printf(" %d", i);
        std::printf("\n");
    }
    return 0;
}
