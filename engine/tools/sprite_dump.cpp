// SPDX-License-Identifier: GPL-3.0-or-later
// Every sprite a scene's stream registers, as the PORT decodes it: the id, the
// texture it resolves to, and each frame's quad. One quad is one frame, and
// several sprites can share an atlas - so what tells two of them apart is the
// UV rectangle, not the picture.
//
//     sprite_dump <gamedata> <scene.SCX>
#include "formats/scx.h"
#include "formats/tex3dt.h"
#include "o3de/particles.h"
#include "platform/datafs.h"
#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    const omk::DataFs fs(argv[1]);
    const auto p = fs.resolve(std::string("SCPTDATA/") + argv[2]);
    if (!p) { std::fprintf(stderr, "no such scx\n"); return 1; }
    const auto d = omk::DataFs::readPath(*p);
    const auto st = omk::readScxStream(d);
    std::printf("%s: %zu sprite registrations\n", argv[2], st.sprites.size());
    for (const auto& sp : st.sprites) {
        std::printf("  id %-6d name '%s' offset %u model %u texture %u",
                    sp.id, sp.name.c_str(), sp.offset, sp.model, sp.texture);
        if (!sp.model || !sp.texture || sp.offset + sp.model + sp.texture > d.size()) {
            std::printf("   -> SKIPPED (bad extents)\n");
            continue;
        }
        const std::span<const std::byte> mo(d.data() + sp.offset, sp.model);
        const std::span<const std::byte> te(d.data() + sp.offset + sp.model, sp.texture);
        const auto t = omk::textures(mo, te);
        const auto fr = omk::spriteFrames(mo);
        std::printf("\n      textures %zu", t.size());
        if (!t.empty()) std::printf(" first %ux%u", t.front().width, t.front().height);
        std::printf(", frames %zu\n", fr.frames.size());
        for (std::size_t i = 0; i < fr.frames.size() && i < 4; ++i) {
            const auto& f = fr.frames[i];
            std::printf("        frame %zu  uv %.3f %.3f  %.3f %.3f  %.3f %.3f  %.3f %.3f\n",
                        i, f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7]);
        }
    }
    return 0;
}
