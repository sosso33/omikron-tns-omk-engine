// SPDX-License-Identifier: GPL-3.0-or-later
// THE SET'S OWN EMITTERS - the environment family, which the port read three
// files for and never joined.
//
//     set_emitters <gamedata> <vm_opcodes.json> <out.bin>
//
// `Sfx_BindAmbientEffects` walks the resident `.3DO`'s meshes and, for each one
// flagged 0x40000000, compares the first FOUR BYTES of its name as a dword
// against every section-D binding's tag; a match registers that binding's
// section-C effect at the mesh's own position. Neon, steam, smoke and fire all
// come out of that, and it crosses three separately authored files.
//
// `docs/ASSETS.md` 3b has the corpus from the Python side - 321 of 579 flagged
// meshes bind - so this is the port re-deriving a number it can be checked
// against, not a new claim.
#include "formats/mesh3do.h"
#include "platform/datafs.h"
#include "script/scenerunner.h"
#include "script/script.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: set_emitters <gamedata> <vm_opcodes.json> <out.bin>\n");
        return 2;
    }
    const std::string fr = argv[1];
    const auto table = omk::OpcodeTable::loadJson(argv[2]);
    const omk::DataFs decors(fr + "/MESHES/DECORS");
    const omk::DataFs scp(fr + "/SCPTDATA");
    auto sfxs = scp.list(".", ".SFX");
    std::sort(sfxs.begin(), sfxs.end());

    long flagged = 0, bound = 0, sets = 0, withAny = 0;
    long impasse = 0;
    for (const auto& sp : sfxs) {
        // the `.3DO` of the same stem, which is how a set and its effects pair
        std::string stem = sp;
        const auto slash = stem.find_last_of("/\\");
        if (slash != std::string::npos) stem = stem.substr(slash + 1);
        stem = stem.substr(0, stem.find_last_of('.'));
        const auto model = decors.resolve(stem + ".3DO");
        if (!model) continue;
        const auto md = omk::DataFs::readPath(*model);
        const auto hd = omk::readHeader(md);
        if (!hd) continue;
        const auto ms = omk::readMeshes(md, *hd);
        ++sets;
        omk::SceneRunner r;
        r.attachSfx(fr + "/SCPTDATA", stem + ".SFX");
        for (const auto& m : ms)
            if (static_cast<std::uint32_t>(m.flags) & 0x40000000u) ++flagged;
        const int n = r.bindSetEmitters(md);
        bound += n;
        if (n) ++withAny;
        (void)stem;
    }

    // THE IMPASSE, paired the way the GAME pairs them - which is not by stem.
    // `attachSfx` takes the SCENE's file with its extension swapped
    // (`Impasse.scx` -> `Impasse.sfx`) and `bindSetEmitters` is handed the
    // AREA's `+97` set (`AIMPASSE.3DO`). The stem sweep above therefore never
    // tests this pair at all, and reported 0 where the running game binds 3 -
    // which is why this row exists: a corpus walk that pairs files by name is
    // not the pairing the engine makes.
    {
        omk::SceneRunner r;
        r.attachSfx(fr + "/SCPTDATA", "Impasse.sfx");
        if (const auto mp = decors.resolve("AIMPASSE.3DO"))
            impasse = r.bindSetEmitters(omk::DataFs::readPath(*mp));
    }

    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    put32(static_cast<std::int32_t>(sets));
    put32(static_cast<std::int32_t>(flagged));
    put32(static_cast<std::int32_t>(bound));
    put32(static_cast<std::int32_t>(withAny));
    put32(static_cast<std::int32_t>(impasse));
    if (!omk::safeOutputPath(argv[3])) return 2;
    std::ofstream f(argv[3], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));
    std::printf("%ld sets with both a .SFX and a .3DO; %ld flagged meshes, "
                "%ld of them BIND, over %ld sets; and the Impasse, paired the way "
                "the GAME pairs it (Impasse.sfx x AIMPASSE.3DO), binds %ld\n",
                sets, flagged, bound, withAny, impasse);
    return 0;
}
