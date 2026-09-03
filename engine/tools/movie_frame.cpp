// SPDX-License-Identifier: GPL-3.0-or-later
// One frame out of an intro movie, as raw RGB - so a reader can LOOK at what
// the FLIS films actually contain. The Bowie opening with its credits is the
// question: whether that sequence is pre-rendered video or in-engine text.
//
//     movie_frame <gamedata> <FLIS/GAME.MPG> <skipFrames> <out.rgb>
#include "platform/datafs.h"
#include "platform/movie.h"
#include "ui/surface.h"
#include <cstdio>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: movie_frame <gamedata> <rel.mpg> <skip> <out.rgb>\n");
        return 2;
    }
    if (!omk::safeOutputPath(argv[4])) return 2;
    const omk::DataFs fs(argv[1]);
    const auto p = fs.resolve(argv[2]);
    if (!p) { std::fprintf(stderr, "no such movie\n"); return 1; }
    omk::Movie m;
    if (!m.open(*p)) { std::fprintf(stderr, "cannot open\n"); return 1; }
    const int skip = std::atoi(argv[3]);
    omk::Surface s(640, 480);   // nextFrame doubles the 320x240 into this
    int n = 0;
    while (m.nextFrame(s)) { if (++n > skip) break; }
    std::printf("decoded %d frames, surface %dx%d\n", n, s.w, s.h);
    if (s.w <= 0 || s.h <= 0) return 1;
    std::ofstream f(argv[4], std::ios::binary);
    for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < s.w; ++x) {
            const std::uint16_t px = s.px[static_cast<std::size_t>(y) *
                                          static_cast<std::size_t>(s.w) +
                                          static_cast<std::size_t>(x)];
            const char rgb[3] = {static_cast<char>(((px >> 11) & 31) << 3),
                                 static_cast<char>(((px >> 5) & 63) << 2),
                                 static_cast<char>((px & 31) << 3)};
            f.write(rgb, 3);
        }
    std::printf("wrote %dx%d rgb\n", s.w, s.h);
    return 0;
}
