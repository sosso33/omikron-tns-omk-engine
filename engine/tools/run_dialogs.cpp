// SPDX-License-Identifier: GPL-3.0-or-later
// Walk every shipped conversation, for the differential against
// tools/sim/dialogue.py.
//
//     run_dialogs <gamedata/IAM/DIALOG> <vm_opcodes.json> <START> <out.bin>
//
// Layout: int32 chunks, parsed, ended, cycles, limits, outOfRange, nodes,
//         conditionsRun, actionsRun, validTargets, totalTargets.
#include "formats/iam.h"
#include "script/dialogue.h"

#include "platform/datafs.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {
}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr,
            "usage: run_dialogs <DIALOG> <vm_opcodes.json> <START> <out.bin>\n");
        return 2;
    }
    const auto table = omk::OpcodeTable::loadJson(argv[2]);
    auto state = omk::GameState::fromFile(argv[3]);
    const auto d = omk::DataFs::readPath(argv[1]);
    const auto ar = omk::IamArchive::open(d);

    // Two tests, and they are different questions - the reference keeps them
    // apart and so does this.
    //
    //   the WALK: every conversation from node 0, taking the first available
    //     branch. The invariant is that they all end.
    //   the SCRIPTS: every condition and every action executed STANDALONE,
    //     the dialogue analogue of running all 5958 world scripts. A
    //     condition that cannot be evaluated is one the reply menu could not
    //     be built from, so "they all run" is the invariant with teeth.
    //
    // Each walk gets a FRESH state, as the reference does. Carrying one across
    // the corpus would let an earlier conversation's actions change which
    // branches a later one offers - a different experiment, and a worse one,
    // because the order would then be part of the answer.
    int chunks = 0, parsed = 0, ended = 0, cycles = 0, limits = 0, oor = 0;
    long nodes = 0, conds = 0, acts = 0, validTargets = 0, totalTargets = 0;
    long scripts = 0, executed = 0;
    for (std::size_t i = 0; i < ar.size(); ++i) {
        const auto b = ar.chunk(i);
        if (b.empty()) continue;
        ++chunks;
        const auto c = omk::parseConversation(static_cast<int>(i), b);
        if (!c.valid) continue;
        ++parsed;
        for (const auto& n : c.nodes)
            for (int k = 0; k < 4; ++k) {
                if (n.param[k] < 0) continue;
                ++totalTargets;
                if (n.param[k] < static_cast<int>(c.nodes.size())) ++validTargets;
            }

        auto fresh = omk::GameState::fromFile(argv[3]);
        const auto r = omk::runConversation(c, b, fresh, table);
        nodes += static_cast<long>(r.path.size());
        switch (r.end) {
            case omk::DialogEnd::Leaf:       ++ended;  break;
            case omk::DialogEnd::Cycle:      ++cycles; break;
            case omk::DialogEnd::Limit:      ++limits; break;
            case omk::DialogEnd::OutOfRange: ++oor;    break;
        }

        for (const auto& n : c.nodes)
            for (int k = 0; k < 8; ++k) {
                const auto off = n.ptr[k];
                if (!off || off >= b.size()) continue;
                ++scripts;
                (k < 4 ? conds : acts) += 1;
                omk::Interpreter vm(state, table);
                const auto res = vm.run(b, off);
                if (res.status == omk::RunStatus::End ||
                    res.status == omk::RunStatus::Dialog) ++executed;
            }
    }

    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    for (long v : {(long)chunks, (long)parsed, (long)ended, (long)cycles,
                   (long)limits, (long)oor, nodes, scripts, conds, acts,
                   executed, validTargets, totalTargets})
        put32(static_cast<std::int32_t>(v));

    if (!omk::safeOutputPath(argv[4])) return 2;
    std::ofstream f(argv[4], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));
    std::printf("%d chunks, %d parse; walk: %d ended, %d cycles, %d limit, "
                "%ld nodes visited. scripts: %ld (%ld conditions + %ld "
                "actions), %ld ran to end; %ld/%ld targets valid\n",
                chunks, parsed, ended, cycles, limits, nodes, scripts,
                conds, acts, executed, validTargets, totalTargets);
    return 0;
}
