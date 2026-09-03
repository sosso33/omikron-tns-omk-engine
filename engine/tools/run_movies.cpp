// SPDX-License-Identifier: GPL-3.0-or-later
// THE THREE INTRO MOVIES, DECODED - headless, so the suite can check them.
//
//     run_movies <gamedata> <out.bin>
//
// `docs/BOOT.md`: the launch chain is the three FLIS movies, then
// `Game_Start("aventure.scx")`. The port has always FOUND and stepped them;
// nothing decoded one until pl_mpeg was vendored (2026-09-01), so the first
// thing a player sees was missing from the replica.
//
// What is asserted is what the shipped files could fail: the pack header that
// makes them MPEG-1 program streams, the geometry every one of them agrees on,
// and - the part a wrong decode would break - that a real number of frames
// comes out and the first one is not blank.
#include "platform/datafs.h"
#include "platform/movie.h"
#include "ui/surface.h"

#include <cstdio>
#include <fstream>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: run_movies <gamedata> <out.bin>\n");
        return 2;
    }
    const omk::DataFs fs(argv[1]);
    std::vector<std::int32_t> out;

    // The image spells them `.mpg` and the disc `.MPG`, which is exactly the
    // case `DataFs` exists for (docs/BOOT.md: the boot path itself needs a
    // case-insensitive filesystem).
    const char* names[3] = {"FLIS/EIDOS.mpg", "FLIS/QUANTIC.mpg", "FLIS/GAME.mpg"};
    for (const char* n : names) {
        const auto real = fs.resolve(n);
        omk::Movie m;
        const bool opened = real && m.open(*real);
        const auto& i = m.info();

        // Decode a bounded prefix - GAME.MPG is 107 seconds and the suite is
        // not the place to decode 3200 frames.
        //
        // And measure the PEAK over that prefix, not the last frame. The first
        // version looked at frame 24 and reported 0 painted pixels for two of
        // the three, which is not a broken decode: QUANTIC's mean sample value
        // is 0.7 of 255 for its first second, and a value that small
        // legitimately quantises to black in 565 - (3*31+127)/255 is 0. A
        // fade-in and a dead decoder look identical at one frame, so the check
        // has to look across several.
        omk::Surface fb(640, 480, 0);
        long peak = 0;
        long samples = 0, loud = 0;
        for (int k = 0; k < 120 && m.nextFrame(fb); ++k) {
            long lit = 0;
            for (auto px : fb.px) if (px) ++lit;
            if (lit > peak) peak = lit;
            // ...and the audio alongside it. Counting SAMPLES alone would pass
            // on a silent decode, so count the ones that are not near zero:
            // a stream that demuxed but produced silence is the failure this
            // has to tell apart from one that works.
            for (auto blk = m.nextAudio(); !blk.empty(); blk = m.nextAudio()) {
                samples += static_cast<long>(blk.size());
                for (float v : blk) if (v > 0.001f || v < -0.001f) ++loud;
                if (samples > 200000) break;
            }
        }
        const long painted = peak;

        out.insert(out.end(), {opened ? 1 : 0, i.width, i.height,
                               static_cast<std::int32_t>(i.framerate * 1000.0 + 0.5),
                               static_cast<std::int32_t>(i.duration + 0.5),
                               i.audioStreams, i.sampleRate,
                               static_cast<std::int32_t>(m.framesDecoded()),
                               static_cast<std::int32_t>(painted),
                               static_cast<std::int32_t>(samples),
                               static_cast<std::int32_t>(loud)});
        std::printf("%-18s %dx%d %.3f fps %6.2f s  audio %d @%d  -> %ld frames, "
                    "peak %ld of %d px lit, %ld samples (%ld audible)\n",
                    n, i.width, i.height, i.framerate, i.duration,
                    i.audioStreams, i.sampleRate, m.framesDecoded(), painted, 640 * 480,
                    samples, loud);
    }

    if (!omk::safeOutputPath(argv[2])) return 2;
    std::ofstream o(argv[2], std::ios::binary);
    const std::int32_t n = static_cast<std::int32_t>(out.size());
    o.write(reinterpret_cast<const char*>(&n), 4);
    o.write(reinterpret_cast<const char*>(out.data()),
            static_cast<std::streamsize>(out.size() * 4));
    return 0;
}
