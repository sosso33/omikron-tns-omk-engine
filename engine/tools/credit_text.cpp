// SPDX-License-Identifier: GPL-3.0-or-later
// The Bowie title sequence's credit blocks, as text. AREA 0 record 78 fires
// twenty `media.play` calls at `ZVO G0xx` objects, and each one's `+280`
// description is a credit - so the credits are ORDINARY SUBTITLES, not a
// dedicated system. What places them around the screen is the `{X<6 digits>}`
// markup, "move to (xxx, yyy) as percentages" (docs/UI.md 5).
//
//     credit_text <gamedata>
#include "audio/voiceover.h"
#include "platform/datafs.h"
#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: credit_text <gamedata>\n"); return 2; }
    const omk::DataFs fs(argv[1]);
    omk::VoiceOverLibrary lib;
    lib.load(fs);
    const int ids[] = {715,716,717,718,719,720,739,740,741,742,743,744,745,746,747,748,749,750,751};
    for (int id : ids) {
        if (id < 0 || static_cast<std::size_t>(id) >= lib.objects().size()) continue;
        const auto& o = lib.objects()[static_cast<std::size_t>(id)];
        std::printf("%3d %-26s %s\n", id, o.name.c_str(), o.description.c_str());
    }
    return 0;
}
