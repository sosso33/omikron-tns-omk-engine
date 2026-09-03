// SPDX-License-Identifier: GPL-3.0-or-later
// The per-line END RULE - `Dialog_TickUI` (0x0046A200) cases 4 and 2/7/8.
//
//     dialogue_probe <gamedata> <vm_opcodes.json> <out.txt>
//
// A conversation's lines do not all end the same way, and the port used to
// treat them alike. Case 4 stamps the phase global with the line's state,
// chosen from the ASSET NAME:
//
//     dword_9103DC = 2;
//     if (!strcmp(v48, a125338))   dword_9103DC = 7;    // "125338"
//     if (!memcmp(v4,  a02e19a, 7)) dword_9103DC = 8;   // "02E19A" and its NUL
//
// and case 2/7/8 is the whole of what the three states do:
//
//     if ((a2 & 0x10) != 0
//      || dword_9103DC == 7 && (a2 & 0xFFFFFFF3) != 0
//      || dword_9103DC == 8 && Morph_IsDone())
//     { Morph_Stop(); dword_9103DC = 3; }
//
// This probe reports four things, and the third is the one with teeth:
//
//   scan     every shipped node's state, so "two lines are special" is a
//            COUNT over the corpus rather than a reading of two literals.
//   state7   conversation 272's first line - the line a new game opens with -
//            and the input words that do and do not cut it.
//   self8    conversation 186 ticked with NO PRESS AT ALL. Before the fix it
//            sits there for ever, which is what the `STILL OPEN` line of
//            `play_dialog 186 ""` shows; after it, it ends itself one tick
//            after its voice runs out and the conversation - a single node
//            with no branch - closes without the player touching anything.
//   rule     the classifier on synthetic names, because the corpus has one
//            example of each and one example cannot show what is NOT matched.
#include "formats/iam.h"
#include "platform/datafs.h"
#include "script/dialogue.h"
#include "script/gamestate.h"
#include "script/script.h"

#include <cstdio>
#include <fstream>
#include <string>

using namespace omk;

namespace {

const char* stateName(LineEnd e) {
    switch (e) {
    case LineEnd::Confirm:    return "confirm";
    case LineEnd::AnyKey:     return "anykey";
    case LineEnd::SelfEnding: return "self";
    }
    return "?";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: dialogue_probe <gamedata> <vm_opcodes.json> <out.txt>\n");
        return 2;
    }
    const std::string fr = argv[1];
    const auto table = OpcodeTable::loadJson(argv[2]);
    if (!table.valid()) { std::fprintf(stderr, "no opcode table\n"); return 1; }
    if (!safeOutputPath(argv[3])) return 2;

    auto state = GameState::fromFile(fr + "/IAM/START");
    const auto file = DataFs::readPath(fr + "/IAM/DIALOG");
    const auto arch = IamArchive::open(file);
    std::ofstream f(argv[3]);

    // ---- scan: how many of the shipped lines carry each state ------------
    int convs = 0, nodes = 0, n2 = 0, n7 = 0, n8 = 0;
    int conv7 = -1, node7 = -1, conv8 = -1, node8 = -1;
    for (std::size_t i = 0; i < arch.size(); ++i) {
        const auto chunk = arch.chunk(i);
        if (chunk.empty()) continue;
        const auto c = parseConversation(static_cast<int>(i), chunk);
        if (!c.valid) continue;
        ++convs;
        for (std::size_t j = 0; j < c.nodes.size(); ++j) {
            ++nodes;
            const auto e = DialogPlayer::lineEndFor(c.nodes[j].name);
            if (e == LineEnd::AnyKey) {
                ++n7;
                if (conv7 < 0) { conv7 = static_cast<int>(i); node7 = static_cast<int>(j); }
            } else if (e == LineEnd::SelfEnding) {
                ++n8;
                if (conv8 < 0) { conv8 = static_cast<int>(i); node8 = static_cast<int>(j); }
            } else {
                ++n2;
            }
        }
    }
    f << "scan " << convs << ' ' << nodes << ' ' << n2 << ' ' << n7 << ' '
      << n8 << ' ' << conv7 << ' ' << node7 << ' ' << conv8 << ' ' << node8 << '\n';
    std::printf("scan: %d conversations, %d nodes - %d confirm, %d anykey "
                "(conv %d node %d), %d self (conv %d node %d)\n",
                convs, nodes, n2, n7, conv7, node7, n8, conv8, node8);

    // ---- state 7: conversation 272's first line --------------------------
    //
    // The input words are the engine's own: `Dialog_TickUI(2, ...)` is called
    // with `dword_90E0E0 | dword_4E9718 & 0xC`, so bits 4 and 8 - up and down,
    // the pair the reply menu moves on - reach the test and are the two the
    // mask excludes.
    {
        const auto chunk = arch.chunk(272);
        const auto conv = parseConversation(272, chunk);
        DialogPlayer p(state, table);
        if (p.open(conv, chunk, fr + "/MORPH")) {
            const std::uint32_t words[] = {0x00u, 0x04u, 0x08u, 0x0Cu, 0x10u,
                                           0x01u, 0x20u};
            f << "state7 " << p.node() << ' ' << p.voice() << ' '
              << static_cast<int>(p.lineState()) << ' '
              << (p.anyKeyCuts() ? 1 : 0);
            for (const auto w : words) f << ' ' << (p.cutBy(w) ? 1 : 0);
            f << '\n';
            std::printf("state7: conv 272 node %d asset %s -> %s; cut by "
                        "0/4/8/C/10/1/20 = ",
                        p.node(), p.voice().c_str(), stateName(p.lineState()));
            for (const auto w : words) std::printf("%d", p.cutBy(w) ? 1 : 0);
            std::printf("\n");
        }
    }

    // ---- state 8: it ends ITSELF, and nothing here ever presses -----------
    //
    // Deliberately written against the API the port had BEFORE this fix -
    // `tick`, `phase`, `node`, `lineSeconds` - so the same source runs either
    // side of it and the difference is the port's behaviour, not the probe's.
    {
        const auto chunk = arch.chunk(186);
        const auto conv = parseConversation(186, chunk);
        DialogPlayer p(state, table);
        if (!p.open(conv, chunk, fr + "/MORPH")) {
            f << "self8 unplayable\n";
        } else {
            const double dt = 1.0 / 30.0;
            const double len = p.lineSeconds();
            const int nodeAt0 = p.node();
            const int phase0 = static_cast<int>(p.phase());
            // 60 seconds of frames, eight times the line - longer than any
            // line in the game, which is the dwell `play_dialog` uses too.
            int advancedAt = -1;
            for (int k = 0; k < 1800; ++k) {
                p.tick(dt);
                if (p.node() != nodeAt0 || p.phase() != DialogPhase::Speaking) {
                    advancedAt = k + 1;
                    break;
                }
            }
            f << "self8 " << nodeAt0 << ' ' << phase0 << ' '
              << static_cast<long>(len * 100) << ' ' << advancedAt << ' '
              << static_cast<long>(advancedAt < 0 ? -1 : advancedAt * 100 / 30)
              << ' ' << static_cast<int>(p.phase()) << ' '
              << (p.playing() ? 1 : 0) << ' ' << p.linesPlayed() << '\n';
            std::printf("self8: conv 186 node %d, %.2f s of voice, no press - "
                        "%s (tick %d, %.2f s), phase now %d, %s\n",
                        nodeAt0, len,
                        advancedAt < 0 ? "STILL SPEAKING" : "ended itself",
                        advancedAt, advancedAt < 0 ? -1.0 : advancedAt / 30.0,
                        static_cast<int>(p.phase()),
                        p.playing() ? "still open" : "closed");
        }
    }

    // ---- the classifier on names the corpus does not have ----------------
    {
        const char* names[] = {"02E19A", "125338", "", "02E19AB", "1253380",
                               "02E19", "125339", "12533A"};
        f << "rule";
        std::printf("rule:");
        for (const auto* n : names) {
            const auto e = DialogPlayer::lineEndFor(n);
            f << ' ' << static_cast<int>(e);
            std::printf(" %s=%s", *n ? n : "(empty)", stateName(e));
        }
        f << '\n';
        std::printf("\n");
    }
    return 0;
}
