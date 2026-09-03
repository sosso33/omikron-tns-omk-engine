// SPDX-License-Identifier: GPL-3.0-or-later
// The Bowie credits, PARSED: every `{X}` move a credit block carries, with the
// face and alignment that follow it. `AREA 0` record 78 fires twenty
// `media.play` calls and each object's `+280` description is one block, so the
// credits are ordinary subtitles positioned by markup (docs/UI.md 5).
//
//     credit_layout <gamedata> <out.bin>
#include "audio/voiceover.h"
#include "ui/text.h"
#include "platform/datafs.h"
#include <cstdio>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: credit_layout <gamedata> <out.bin>\n"); return 2; }
    if (!omk::safeOutputPath(argv[2])) return 2;
    const omk::DataFs fs(argv[1]);
    omk::VoiceOverLibrary lib;
    lib.load(fs);
    const int ids[] = {715,716,717,718,719,720,739,740,741,742,743,744,745,746,747,748,749,750,751};
    int blocks = 0, withText = 0, rightAligned = 0, faces = 0, outOfRange = 0;
    for (int id : ids) {
        if (id < 0 || static_cast<std::size_t>(id) >= lib.objects().size()) continue;
        const auto pt = omk::parseMarkup(lib.objects()[static_cast<std::size_t>(id)].description, 'V');
        for (std::size_t m = 0; m < pt.moves.size(); ++m) {
            const auto& mv = pt.moves[m];
            ++blocks;
            if (mv.xPct > 100 || mv.yPct > 100) ++outOfRange;
            if (mv.align == omk::kAlignRight) ++rightAligned;
            const std::size_t to = m + 1 < pt.moves.size() ? pt.moves[m + 1].at : pt.run.size();
            if (to > mv.at) ++withText;
            if (to > mv.at && pt.run[mv.at].face != 'V') ++faces;
        }
    }
    std::printf("%d credit blocks, %d carrying text, %d right-aligned, %d with a "
                "face of their own, %d positioned outside the screen\n",
                blocks, withText, rightAligned, faces, outOfRange);
    const std::int32_t out[5] = {blocks, withText, rightAligned, faces, outOfRange};
    std::ofstream f(argv[2], std::ios::binary);
    f.write(reinterpret_cast<const char*>(out), sizeof out);
    return 0;
}
