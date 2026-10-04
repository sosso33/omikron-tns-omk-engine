// SPDX-License-Identifier: GPL-3.0-or-later
// EVERY SHIPPED TEXTURE, AS ITS COLOURS (`verify.py: engine: indexed
// textures`, todo/ram-vs-original.md tier C). Each model under MESHES with
// its `.3DT`, and every sprite inside every `.SCX` stream, decoded and
// expanded to RGB texel by texel; one FNV-1a hash over all of it, with the
// sizes. The hash was taken from the RGB decode this replaced (2026-10-05),
// so the same hash now says the palette form gives every texel the same
// colour; the bytes each form keeps are reported beside it.
//
//     texture_hash <gamedata>   -> counts, kept bytes, the hash
#include "formats/scx.h"
#include "formats/tex3dt.h"
#include "platform/datafs.h"

#include <cinttypes>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

struct Tally {
    long textures = 0, inexact = 0;
    std::size_t texels = 0, kept = 0;
    std::uint64_t h = 1469598103934665603ull;
    void mix(std::uint8_t b) { h = (h ^ b) * 1099511628211ull; }
    void add(const omk::Texture& t) {
        ++textures;
        inexact += !t.exact;
        const std::uint32_t wh[2] = {static_cast<std::uint32_t>(t.width),
                                     static_cast<std::uint32_t>(t.height)};
        for (const std::uint32_t v : wh)
            for (int k = 0; k < 4; ++k) mix(static_cast<std::uint8_t>(v >> (8 * k)));
        const std::vector<std::uint8_t> rgb = t.rgbCopy();
        texels += rgb.size() / 3;
        kept += t.keptBytes();
        for (const std::uint8_t b : rgb) mix(b);
    }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: texture_hash <gamedata>\n");
        return 2;
    }
    const omk::DataFs fs(argv[1]);
    Tally meshes, sprites;
    // the models, in a fixed order: sorted paths under MESHES
    std::vector<std::string> models;
    for (const auto& e : std::filesystem::recursive_directory_iterator(std::string(argv[1]) + "/MESHES")) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        for (auto& c : ext) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (ext == ".3DO") models.push_back(e.path().string());
    }
    std::sort(models.begin(), models.end());
    for (const std::string& m : models) {
        std::string stem = m.substr(0, m.size() - 4);
        std::string tpath;
        for (const char* x : {".3DT", ".3dt", ".3Dt", ".3dT"})
            if (std::filesystem::exists(stem + x)) { tpath = stem + x; break; }
        if (tpath.empty()) continue;
        for (const auto& t : omk::textures(omk::DataFs::readPath(m), omk::DataFs::readPath(tpath)))
            meshes.add(t);
    }
    // every sprite in every scene stream, in sorted file order
    std::vector<std::string> scx = fs.list("SCPTDATA", "SCX");
    std::sort(scx.begin(), scx.end());
    for (const std::string& path : scx) {
        const auto d = omk::DataFs::readPath(path);
        const auto st = omk::readScxStream(d);
        for (const auto& sp : st.sprites) {
            if (!sp.model || !sp.texture || sp.offset + sp.model + sp.texture > d.size()) continue;
            const std::span<const std::byte> mo(d.data() + sp.offset, sp.model);
            const std::span<const std::byte> te(d.data() + sp.offset + sp.model, sp.texture);
            for (const auto& t : omk::textures(mo, te)) sprites.add(t);
        }
    }
    for (const auto* t : {&meshes, &sprites})
        std::printf("%s: %ld textures (%ld inexact), %zu texels, %zu KB as RGB, %zu KB kept, "
                    "hash %016" PRIx64 "\n", t == &meshes ? "meshes" : "sprites", t->textures,
                    t->inexact, t->texels, t->texels * 3 / 1024, t->kept / 1024, t->h);
    return 0;
}
