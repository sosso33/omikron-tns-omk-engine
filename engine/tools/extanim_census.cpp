// SPDX-License-Identifier: GPL-3.0-or-later
// Census of the SCENE-OBJECT function ids over the whole shipped corpus, and
// in particular `Script_AnimationFromExternalScene` (0x0300001A) - the one
// that resolves its object in the EXTERNAL scenes rather than the running
// one, which is how the environment animates during a cutscene.
#include "formats/scx.h"
#include "platform/datafs.h"
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: extanim_census <gamedata>\n"); return 2; }
    const omk::DataFs fs(argv[1]);
    std::map<std::uint32_t, int> ids;
    std::map<std::uint32_t, std::set<std::string>> filesOf;
    std::set<std::string> extFiles;
    int files = 0, objects = 0, extObjects = 0;
    for (const auto& path : fs.list("SCPTDATA", ".scx")) {
        const std::string name = path.substr(path.find_last_of("/\\") + 1);
        const auto data = omk::DataFs::readPath(path);
        const omk::ScxScene s = omk::readScx(data);
        if (!s.valid) continue;
        ++files;
        for (const auto& o : s.objects) {
            ++objects;
            bool ext1a = false;
            for (const auto& f : o.functions) {
                ++ids[f.id];
                filesOf[f.id].insert(name);
                if (f.id == 0x0300001Au) ext1a = true;
            }
            if (ext1a) { ++extObjects; extFiles.insert(name); }
        }
    }
    std::printf("%d .SCX files, %d objects\n\n", files, objects);
    std::printf("%-12s %8s %7s  %s\n", "function id", "uses", "files", "name");
    for (const auto& [id, n] : ids)
        std::printf("0x%08X   %8d %7zu%s\n", id, n, filesOf[id].size(),
                    id == 0x0300001Au ? "   <- AnimationFromExternalScene" : "");
    std::printf("\nobjects using 0x0300001A: %d, across %zu scenes\n",
                extObjects, extFiles.size());
    int shown = 0;
    for (const auto& f : extFiles) { std::printf("  %s\n", f.c_str()); if (++shown >= 25) { std::printf("  ...\n"); break; } }
    return 0;
}
