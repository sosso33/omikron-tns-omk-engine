// SPDX-License-Identifier: GPL-3.0-or-later
// WHAT THE EXAMINE PAGE SHOWS - two content paths, checked against the tree.
//
// `sub_49B950` asks `sub_42B330` for the object's kind and sends kind 5 to
// `sub_478EF0`. Both raise the inventory channel's EVENT 40, which fills one
// block from the record's own `+2`:
//
//     kind 15  ->  result 4, `+0` = the loaded preview MODEL
//     kind 16  ->  result 5, `+0` = `sub_40BB40(rec + 0x0E)`, which
//                  `sub_478EF0` turns into `Images\<that>` and loads as a
//                  BITMAP
//     else     ->  result 2, no examine content at all
//
// `rec + 0x0E` is `o + 14`, exactly the `stem` this port already reads. This
// asks the shipped data whether both arms hold.
#include "platform/datafs.h"
#include "script/objects.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: exam_probe <gamedata>\n"); return 2; }
    const omk::DataFs fs(argv[1]);
    const auto objs = omk::loadObjects(fs);
    int k15 = 0, k15model = 0, k16 = 0, k16image = 0;
    for (const auto& o : objs) {
        if (o.kind == 15) {
            ++k15;
            if (!fs.read("MESHES/OBJETS/" + o.stem + ".3do").empty()) ++k15model;
        } else if (o.kind == 16) {
            ++k16;
            if (!fs.read("IMAGES/" + o.stem + ".bmp").empty()) ++k16image;
        }
    }
    std::printf("kind 15 %d of %d have a model\n", k15model, k15);
    std::printf("kind 16 %d of %d have an image\n", k16image, k16);
    int n15 = 0;
    for (const auto& o : objs)
        if (o.kind == 15 && n15++ < 5)
            std::printf("  k15 idx %4d stem '%s' name '%s'\n",
                        o.index, o.stem.c_str(), o.name.c_str());
    for (const auto& o : objs)
        if (o.name.find("MK400") != std::string::npos ||
            o.name.find("appartement Kay") != std::string::npos ||
            o.name.find("Cl\xe9 appartement") != std::string::npos)
            std::printf("\n%s (kind %d, stem %s)\n---\n%s\n---\n",
                        o.name.c_str(), o.kind, o.stem.c_str(),
                        o.description.c_str());
    return 0;
}
