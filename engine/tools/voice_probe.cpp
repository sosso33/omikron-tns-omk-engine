// SPDX-License-Identifier: GPL-3.0-or-later
// `voice_probe` - what `media.play` (VM opcode 92) resolves to, and what the
// ADPCM behind it decodes to.  Drives `src/audio/voiceover.{h,cpp}`; read by
// `verify.py: engine voice over`.
//
// TIER (PORTING B1/B2): **4** for the resolution rule, whose ids come from the
// golden traces' own `media.play` announcements (the `"OBJECTS"` literal at
// 0x004C0844 has one reference in the image); **3** for the decode, which is
// differenced against `tools/adp.py`.  See the header for the full statement.
//
//     build/voice_probe <gamedata> <tables> [objectId ...]
//
// Prints one `key value ...` line per fact, the shape the other probes use.
// It takes NO output path and writes NOTHING - so there is nothing for
// `omk::safeOutputPath` to guard here, and the one path it builds
// (`VOICEOFF/...`) is only ever READ, through DataFs.
#include "audio/voiceover.h"
#include "formats/adpcm.h"
#include "platform/datafs.h"

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: %s <gamedata> <tables> [objectId ...]\n"
                     "  with no ids, the four the Impasse cutscene plays\n",
                     argv[0]);
        return 2;
    }
    const omk::DataFs fs(argv[1]);
    const auto tables = omk::AdpcmTables::loadJson(std::string(argv[2]) + "/adpcm.json");
    if (!tables.valid()) {
        std::fprintf(stderr, "tables/adpcm.json did not load\n");
        return 1;
    }

    omk::VoiceOverLibrary lib;
    if (!lib.load(fs)) {
        std::fprintf(stderr, "IAM/OBJECT did not read under %s\n", argv[1]);
        return 1;
    }

    // ---- the corpus counts -------------------------------------------------
    std::printf("objects records %zu\n", lib.objects().size());
    std::printf("counts zvo %d shipped %d jingle %d silent %d files %d\n",
                lib.zvoObjects(fs), lib.shippedVoices(fs), lib.jingleVoices(fs),
                lib.silentVoices(fs), omk::VoiceOverLibrary::voiceFiles(fs));

    // ---- one line per requested object -------------------------------------
    //
    // The default set is `traces/impasse-walk.log`'s first four media.play
    // announcements, in the order the engine made them: 142 and 141 ship,
    // 404 and 410 are ZVOT stems and take the JINGOFF3 substitution.
    std::vector<int> ids;
    for (int i = 3; i < argc; ++i) ids.push_back(std::atoi(argv[i]));
    if (ids.empty()) ids = {142, 141, 404, 410};

    // FNV-1a over the decoded little-endian int16 stream.  A sample COUNT is
    // 2x the file size and would pass on any decoder at all; this is what
    // makes the Python difference sample-exact without writing a file out.
    const auto fnv = [](const std::vector<std::int16_t>& p) {
        std::uint32_t h = 2166136261u;
        for (const std::int16_t s : p) {
            const auto u = static_cast<std::uint16_t>(s);
            h = (h ^ static_cast<std::uint8_t>(u & 0xFF)) * 16777619u;
            h = (h ^ static_cast<std::uint8_t>(u >> 8)) * 16777619u;
        }
        return h;
    };

    for (const int id : ids) {
        const auto v = lib.resolve(fs, id);
        const auto pcm = lib.decode(fs, tables, v);
        // `samples` is MONO int16 at 22050; `%.3f` seconds beside it so a
        // reader can tell a 2.08 s jingle from a 17.5 s line at a glance.
        std::printf("voice %d stem %s file %s substituted %d shipped %d "
                    "image %d samples %zu seconds %.3f fnv %08x name \"%s\"\n",
                    id,
                    v.stem.empty() ? "-" : v.stem.c_str(),
                    v.file.empty() ? "-" : v.file.c_str(),
                    v.substituted ? 1 : 0, v.shipped ? 1 : 0, v.image ? 1 : 0,
                    pcm.size(),
                    static_cast<double>(pcm.size()) / omk::kAdpcmRate,
                    fnv(pcm), v.objectName.c_str());
    }

    // ---- the announcement filter -------------------------------------------
    //
    // `Session::announced()` is not built here - the probe must not depend on
    // a running Session - so the filter is exercised on a stand-in with the
    // SAME shape (`.domain` / `.value`, no `.op`), which is the branch the
    // port takes today.  Two media ids with an `inventory.add` of a non-voice
    // object and a `SCENES` entry between them: the poll must return the two,
    // in order, and the cursor must not re-report them.
    struct Ann { std::string domain; int value; };
    const std::vector<Ann> feed = {
        {"OBJECTS", 142}, {"OBJECTS", 12}, {"SCENES", 55}, {"OBJECTS", 404}};
    omk::VoiceOverPlayer player(lib);
    const auto first = player.poll(feed);
    const auto again = player.poll(feed);
    std::printf("poll got %zu", first.size());
    for (const int id : first) std::printf(" %d", id);
    std::printf(" repoll %zu cursor %zu\n", again.size(), player.cursor());
    return 0;
}
