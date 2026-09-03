// SPDX-License-Identifier: GPL-3.0-or-later
// THE SOUNDS ADVENTURE MODE PLAYS - the `.CTL` states' `+28` effect records,
// and how their sound ids reach a scene.
//
//     actor_sounds <gamedata> <vm_opcodes.json> <out.bin>
//
// A cutscene's sound rides on a scene object's program; adventure mode's rides
// on the `.CTL` state machine instead. `Cef_TickEffects` (0x0045ADF0) plays an
// effect record's `+22` once when the state's clock passes its `+12`, and
// `H_WALK` carries a pair of them - one per footfall.
//
// **The id is not an index.** `Cef_TickEffects` resolves it with
// `Scene_FindSoundIndex` (0x0048CC80), which SEARCHES the resident scene's
// 26-byte chunk-3 records for a matching `+24` and returns that record's `+22`
// handle - so the same id names different sounds in different scenes (34 is
// `AASC.WAV` almost everywhere and `STPR.WAV` in the Impasse) and a state's
// footstep only sounds where the resident scene carries its id. That is the
// opposite of the scene programs, whose param 0 IS a bounds-checked index
// (`sub_48CB30`), and reading one as the other lands on the wrong sound.
#include "formats/ctl.h"
#include "formats/scx.h"
#include "platform/datafs.h"
#include "actor/channel.h"
#include "script/program.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: actor_sounds <gamedata> <vm_opcodes.json> <out.bin>\n");
        return 2;
    }
    const std::string fr = argv[1];
    const omk::DataFs anims(fr + "/ANIMS");
    auto ctls = anims.list(".", ".CTL");
    std::sort(ctls.begin(), ctls.end());

    long records = 0, withSound = 0, withSprite = 0, neither = 0, badWindow = 0, badAttach = 0;
    std::set<int> ids;
    std::vector<std::pair<std::string, std::vector<int>>> walk;   // H_WALK's pair
    for (const auto& p : ctls) {
        const auto f = omk::readCtl(omk::DataFs::readPath(p));
        for (const auto& s : f.states) {
            std::vector<int> here;
            for (const auto& e : s.effects) {
                ++records;
                if (e.sound)  { ++withSound; ids.insert(e.sound); here.push_back(e.sound); }
                if (e.sprite) ++withSprite;
                if (!e.sound && !e.sprite) ++neither;
                if (e.to != 0.0f && e.from > e.to) ++badWindow;
                if (e.attach > 20) ++badAttach;
            }
            if (s.name == "H_WALK" && !here.empty()) {
                walk.emplace_back(s.name, here);
                if (walk.size() == 1)
                    for (const auto& e : s.effects)
                        std::printf("   H_WALK: sound %d at frame %.0f\n", e.sound, e.soundAt);
            }
        }
    }

    // ...and whether every id a state names exists in SOME scene's chunk 3
    const omk::DataFs scp(fr + "/SCPTDATA");
    auto scenes = scp.list(".", ".SCX");
    std::sort(scenes.begin(), scenes.end());
    std::set<int> sceneIds;
    for (const auto& p : scenes) {
        const omk::ScxRuntime rt(omk::DataFs::readPath(p));
        if (!rt.valid()) continue;
        for (int i = 0; i < rt.wavCount(); ++i) sceneIds.insert(rt.wavId(i));
    }
    long resolvable = 0;
    for (int i : ids) resolvable += sceneIds.count(i) ? 1 : 0;

    // ---- the RUN: hold the walk input and count the footfalls ------------
    // `H_WALK` carries two effect records, one per foot, and the state loops -
    // so a held walk must produce an ALTERNATING stream, not one sound and
    // then silence. That is what a latch cleared on state change buys, and a
    // latch never cleared (or never set) fails it in opposite directions.
    long steps = 0, alternates = 1;
    int lastId = -1, distinct = 0;
    {
        const auto ctl = omk::readCtl(omk::DataFs::readPath(
            anims.resolve("H1Avnt.CTL").value_or(fr + "/ANIMS/H1Avnt.CTL")));
        int walkState = -1, walkGroup = -1;
        for (std::size_t i = 0; i < ctl.states.size(); ++i)
            if (ctl.states[i].name == "H_WALK") {
                walkState = static_cast<int>(i);
                walkGroup = ctl.states[i].group;
                break;
            }
        if (walkState >= 0) {
            omk::CefChannel ch(ctl);
            ch.setBankGroup(walkGroup);
            std::set<int> seen;
            // 0x04 is the code on `H_STAND`'s `H_SD-WK` edge, which is how
            // `verify.py: engine actor states` walks H_STAND -> H_SD-WK ->
            // H_WALK. Held, the machine stays in the walk and loops it.
            for (int t = 0; t < 300; ++t) {
                ch.tick(1.0f, 0x04u);
                for (const auto& s : ch.sounds()) {
                    ++steps; seen.insert(s.id);
                    if (lastId == s.id) alternates = 0;
                    lastId = s.id;
                }
            }
            distinct = static_cast<int>(seen.size());
        }
    }

    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    put32(static_cast<std::int32_t>(records));
    put32(static_cast<std::int32_t>(withSound));
    put32(static_cast<std::int32_t>(withSprite));
    put32(static_cast<std::int32_t>(neither));
    put32(static_cast<std::int32_t>(badWindow));
    put32(static_cast<std::int32_t>(badAttach));
    put32(static_cast<std::int32_t>(ids.size()));
    put32(static_cast<std::int32_t>(resolvable));
    put32(static_cast<std::int32_t>(walk.size()));
    put32(walk.empty() || walk[0].second.size() < 2 ? -1 : walk[0].second[0]);
    put32(walk.empty() || walk[0].second.size() < 2 ? -1 : walk[0].second[1]);
    put32(static_cast<std::int32_t>(steps));
    put32(static_cast<std::int32_t>(alternates));
    put32(distinct);
    if (!omk::safeOutputPath(argv[3])) return 2;
    std::ofstream out(argv[3], std::ios::binary);
    out.write(reinterpret_cast<const char*>(o.data()),
              static_cast<std::streamsize>(o.size()));

    std::printf("%ld effect records: %ld carry a sound, %ld a sprite, %ld neither; "
                "%ld bad windows, %ld attach codes out of range\n",
                records, withSound, withSprite, neither, badWindow, badAttach);
    std::printf("%zu distinct sound ids, %ld of them present in some scene's chunk 3\n",
                ids.size(), resolvable);
    std::printf("a held H_WALK over 300 frames: %ld footfalls, %d distinct sounds, "
                "alternating: %s\n", steps, distinct, alternates ? "yes" : "NO");
    return 0;
}
