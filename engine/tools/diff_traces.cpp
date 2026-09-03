// SPDX-License-Identifier: GPL-3.0-or-later
// Every golden trace, replayed in the port - the behavioural differential.
//
//     diff_traces <gamedata/IAM> <tables/> <traces/> <out.bin> [capture...]
//
// The engine has only ever been diffed against `intro.log`, 42 events, by
// comparing whole announcement streams. That works for the opening because it
// is one entry point; it cannot work for a capture of somebody PLAYING, where
// the events come from dozens of scripts running concurrently. So this does
// what `tools/goldentrace.py diff` does: anchor each event on the one slot
// that could have announced it, replay that slot, and require the prediction
// to appear as an ordered subsequence.
//
// out.bin: int32 nCaptures, then per capture
//   int32 nameLen, name, int32 events, anchors, ok, bad, skipped, unreached
#include "formats/iam.h"
#include "platform/datafs.h"
#include "script/goldendiff.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: diff_traces <gamedata/IAM> <tables> <traces> "
                             "<out.bin> [capture...]\n");
        return 2;
    }
    const std::string iam = argv[1], tbl = argv[2], trc = argv[3];
    const auto table = omk::OpcodeTable::loadJson(tbl + "/vm_opcodes.json");
    if (!table.valid()) { std::fprintf(stderr, "no opcode table\n"); return 1; }
    const auto ann = omk::AnnounceMap::loadJson(tbl + "/vm_announce.json");
    if (!ann.valid()) { std::fprintf(stderr, "no announce map\n"); return 1; }

    const auto areaFile   = omk::DataFs::readPath(iam + "/AREA");
    const auto sceneFile  = omk::DataFs::readPath(iam + "/SCENE");
    const auto globalFile = omk::DataFs::readPath(iam + "/GLOBAL");
    auto slots = omk::worldSlots(areaFile, sceneFile, globalFile);
    const auto worldCount = slots.size();
    omk::appendDialogScripts(omk::DataFs::readPath(iam + "/DIALOG"), slots);
    const auto tight = omk::tightIndex(slots, table, ann);

    std::vector<std::string> caps;
    for (int i = 5; i < argc; ++i) caps.push_back(argv[i]);
    if (caps.empty())
        caps = {"intro.log", "walkin.log", "impasse-walk.log",
                "telis-dialog.log", "resto-387.log", "fight.log"};

    std::printf("%zu world slots + %zu conversation branch scripts (indexed to "
                "disambiguate, never replayed), %zu distinct announced pairs\n",
                worldCount, slots.size() - worldCount, tight.size());
    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    put32(static_cast<std::int32_t>(caps.size()));
    for (const auto& c : caps) {
        // Each capture starts from the new-game state and carries it forward,
        // which is `goldentrace.py --evolve`: no save needed, and it tracks
        // the playthrough as far as the attribution reaches.
        auto state = omk::GameState::fromFile(iam + "/START");
        const auto r = omk::diffCapture(trc + "/" + c, slots, tight,
                                        std::move(state), table, ann);
        put32(static_cast<std::int32_t>(c.size()));
        for (char ch : c) o.push_back(static_cast<std::uint8_t>(ch));
        put32(r.events); put32(r.anchors); put32(r.ok);
        put32(r.bad); put32(r.skipped); put32(r.unreached);
        std::printf("  %-20s %4d events, %3d anchors: %3d agree, %d disagree, "
                    "%d not replayable, %d on an unreached branch\n",
                    c.c_str(), r.events, r.anchors, r.ok, r.bad, r.skipped,
                    r.unreached);
        for (const auto& m : r.mismatched)
            std::printf("      MISMATCH  %s\n", m.c_str());
    }
    if (!omk::safeOutputPath(argv[4])) return 2;
    std::ofstream f(argv[4], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));
    return 0;
}
