// SPDX-License-Identifier: GPL-3.0-or-later
// The title card. `media.play 715` (`ZVO G001 TITRE`) is a kind-16 DOCUMENT,
// so the handler takes the other arm and shows `IMAGES\<stem>.BMP` instead of
// speaking - which is why its `+280` description is `{X030040}{f3}` with no
// text at all. The bitmap is `zvog001.bmp`, and nothing in the port drew it.
//
//     title_logo <gamedata> <out.rgb>
#include "audio/voiceover.h"
#include "ui/surface.h"
#include "platform/datafs.h"
#include <cstdio>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: title_logo <gamedata> <out.rgb>\n"); return 2; }
    if (!omk::safeOutputPath(argv[2])) return 2;
    const omk::DataFs fs(argv[1]);
    omk::VoiceOverLibrary lib;
    lib.load(fs);
    const omk::VoiceOver vo = lib.resolve(fs, 715);
    std::printf("media.play 715 '%s': image %d, stem '%s', file '%s'\n",
                vo.objectName.c_str(), (int)vo.image, vo.stem.c_str(), vo.file.c_str());
    if (!vo.image) { std::fprintf(stderr, "not an image object\n"); return 1; }
    std::string stem = vo.stem;
    const auto p = fs.resolve("IMAGES/" + stem + ".BMP");
    if (!p) { std::fprintf(stderr, "no IMAGES/%s.BMP\n", stem.c_str()); return 1; }
    const omk::Surface s = omk::surfaceFromBmp(omk::DataFs::readPath(*p));
    std::printf("IMAGES/%s.BMP -> %dx%d\n", stem.c_str(), s.w, s.h);
    if (s.w <= 0) return 1;
    std::ofstream f(argv[2], std::ios::binary);
    for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < s.w; ++x) {
            const std::uint16_t px = s.px[static_cast<std::size_t>(y) * static_cast<std::size_t>(s.w) + static_cast<std::size_t>(x)];
            const char rgb[3] = {static_cast<char>(((px >> 11) & 31) << 3),
                                 static_cast<char>(((px >> 5) & 63) << 2),
                                 static_cast<char>((px & 31) << 3)};
            f.write(rgb, 3);
        }
    return 0;
}
