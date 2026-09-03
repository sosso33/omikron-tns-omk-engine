// SPDX-License-Identifier: GPL-3.0-or-later
// THE SOUNDS A SCENE OBJECT'S PROGRAM PLAYS - chunk 3, and the two functions
// that fire it.
//
//     scene_sounds <gamedata> <vm_opcodes.json> <out.bin>
//
// An object's animation carries its sound: `Script_PlaySound` (0x004A12D0) and
// `Script_PlaySyncSound` (0x004A14D0) hang off the body animation through the
// `+12` sync link, and `Script_PlayScript` runs them in the same chain walk. So
// they were unreachable while the sync link was read flat - a leading
// `sync = 0` became a self-loop and dropped the whole chain - and then still
// silent, because nothing in the port had ever played them: `ScxStream`
// counted the chunk-3 records and discarded their payloads.
//
// The RUN half is `Impasse.SCX`'s `A_1_KaylArrives`, which is the clean test
// because its cues are a WALK: `Wait 60`, then a 270-frame arrival clip
// carrying STPR at 170, STPL at 200, STPL at 210 and STPR at 280 - right,
// left, left, right - plus a cloth movement at 170 and an ambient at the
// start. Cue times are on the OBJECT's clock (`if (param1 > obj+88) return 1`),
// not the clip's, so the 60-frame wait shifts none of them.
//
// The CORPUS half is the self-checking one: every sound index a program names
// must resolve to a chunk-3 record of its own scene, and the payload must
// begin "RIFF". A wrong index or a mis-walked stream fails both.
#include "audio/mixer.h"
#include "platform/datafs.h"
#include "script/scenerunner.h"
#include "script/script.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: scene_sounds <gamedata> <vm_opcodes.json> <out.bin>\n");
        return 2;
    }
    const std::string fr = argv[1];
    const auto table = omk::OpcodeTable::loadJson(argv[2]);

    // ---- the run: Kay'l's arrival, and where its cues land ---------------
    omk::SceneRunner sc;
    if (!sc.load(fr + "/SCPTDATA", fr + "/IAM", table, omk::ChunkKind::Scene, 55)) {
        std::fprintf(stderr, "cannot make Impasse.SCX resident\n");
        return 1;
    }
    sc.handle({omk::Call{46, {221, 0}}});          // scx.play.player.wait
    std::vector<std::pair<int, std::string>> fired;
    for (int f = 0; f < 400 && sc.programsRunning() > 0; ++f) {
        sc.tick(1.0f);
        for (const auto& s : sc.sounds())
            fired.emplace_back(f, sc.scene().wavName(s.cue.wav));
    }

    // ---- the corpus: every named sound resolves, and is a RIFF -----------
    const omk::DataFs fs(fr + "/SCPTDATA");
    auto files = fs.list(".", ".SCX");
    std::sort(files.begin(), files.end());
    long named = 0, resolved = 0, riff = 0, accepted = 0, records = 0;
    for (const auto& p : files) {
        const auto raw = omk::DataFs::readPath(p);
        const omk::ScxRuntime rt(raw);
        if (!rt.valid()) continue;
        records += rt.wavCount();
        for (const auto& o : rt.scene().objects)
            for (const auto& fn : o.functions) {
                if (fn.id != omk::kFnPlaySound && fn.id != omk::kFnPlaySyncSound) continue;
                if (fn.params.empty()) continue;
                ++named;
                const auto d = rt.wavData(fn.params[0]);
                if (d.empty()) continue;
                ++resolved;
                if (d.size() >= 4 && std::memcmp(d.data(), "RIFF", 4) == 0) ++riff;
                if (omk::audio::loadWav(d).reject == omk::audio::WavReject::Ok) ++accepted;
            }
    }

    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    put32(static_cast<std::int32_t>(fired.size()));
    for (const auto& [f, n] : fired) put32(f);
    put32(static_cast<std::int32_t>(named));
    put32(static_cast<std::int32_t>(resolved));
    put32(static_cast<std::int32_t>(riff));
    put32(static_cast<std::int32_t>(accepted));
    put32(static_cast<std::int32_t>(records));
    if (!omk::safeOutputPath(argv[3])) return 2;
    std::ofstream out(argv[3], std::ios::binary);
    out.write(reinterpret_cast<const char*>(o.data()),
              static_cast<std::streamsize>(o.size()));

    std::printf("A_1_KaylArrives fires %zu cues:\n", fired.size());
    for (const auto& [f, n] : fired) std::printf("   frame %-4d %s\n", f, n.c_str());
    std::printf("corpus: %ld chunk-3 records; %ld sounds named by a program, "
                "%ld resolve, %ld begin RIFF, %ld accepted by Wav_LoadToBuffer\n",
                records, named, resolved, riff, accepted);
    return 0;
}
