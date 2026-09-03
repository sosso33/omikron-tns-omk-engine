// SPDX-License-Identifier: GPL-3.0-or-later
// Decode every texture of one .3DO/.3dt pair and write them where the
// differential test can compare them byte for byte against tools/tex3dt.py.
//
//     dump_textures <model.3DO> <outdir>
//
// Per texture i it writes <outdir>/<stem>.<i>.rgb (raw width*height*3) and
// prints one line of metadata:  i name width height bpp exact
//
// Raw RGB rather than PNG on purpose: the comparison must be of the decoded
// pixels, not of two encoders agreeing.
#include "formats/tex3dt.h"

#include "platform/datafs.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {



// The `.3dt` beside a `.3DO`. Resolved case-INSENSITIVELY, not by trying two
// spellings: the game ran on a case-insensitive filesystem and its data
// references were never spelled consistently - eight of the shipped `.SFX`
// files prove it. Two guesses is a bug waiting for a third spelling.
fs::path texturePath(const fs::path& model) {
    auto c = model;
    c.replace_extension(".3dt");
    if (const auto r = omk::DataFs(model.parent_path().string()).resolve(c.filename().string())) return *r;
    return {};
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: dump_textures <model.3DO> <outdir>\n");
        return 2;
    }
    const fs::path model = argv[1];
    const fs::path outdir = argv[2];

    const auto tpath = texturePath(model);
    if (tpath.empty()) return 0;              // no .3dt: nothing to compare

    const auto d = omk::DataFs::readPath(model.string());
    const auto t = omk::DataFs::readPath(tpath.string());
    if (d.empty() || t.empty()) {
        std::fprintf(stderr, "cannot read %s\n", model.string().c_str());
        return 1;
    }

    std::error_code ec;
    fs::create_directories(outdir, ec);

    const auto stem = model.stem().string();
    const auto txs = omk::textures(d, t);
    for (std::size_t i = 0; i < txs.size(); ++i) {
        const auto& tx = txs[i];
        const auto out = outdir / (stem + "." + std::to_string(i) + ".rgb");
        std::ofstream f(out, std::ios::binary);
        f.write(reinterpret_cast<const char*>(tx.rgb.data()),
                static_cast<std::streamsize>(tx.rgb.size()));
        std::printf("%zu\t%s\t%d\t%d\t%d\t%d\n", i, tx.name.c_str(),
                    tx.width, tx.height, tx.bpp, tx.exact ? 1 : 0);
    }
    return 0;
}
