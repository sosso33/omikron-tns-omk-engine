// SPDX-License-Identifier: GPL-3.0-or-later
// PLAYING a conversation - `DialogPlayer`, driven by scripted presses.
//
//     play_dialog <gamedata> <vm_opcodes.json> <conversation id> <out.txt> [presses]
//
// The point of this tool is that it can only advance by PRESSING. `presses` is
// a string of `n` (NEXT) and `0`..`3` (choose that reply); anything the player
// does not press for does not happen, which is the property the check is
// about. Run with no presses at all and a conversation must sit on its first
// line for ever - a timer would show up as a walk that finishes anyway.
#include "formats/iam.h"
#include "platform/datafs.h"
#include "script/dialogue.h"
#include "script/gamestate.h"
#include "script/script.h"

#include <cstdio>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: play_dialog <gamedata> <vm_opcodes.json> "
                             "<id> <out.txt> [presses]\n");
        return 2;
    }
    const std::string fr = argv[1];
    const auto table = omk::OpcodeTable::loadJson(argv[2]);
    if (!table.valid()) { std::fprintf(stderr, "no opcode table\n"); return 1; }
    const int id = std::atoi(argv[3]);
    if (!omk::safeOutputPath(argv[4])) return 2;
    const std::string presses = argc > 5 ? argv[5] : "";

    auto state = omk::GameState::fromFile(fr + "/IAM/START");
    const auto file = omk::DataFs::readPath(fr + "/IAM/DIALOG");
    const auto arch = omk::IamArchive::open(file);
    const auto chunk = arch.chunk(static_cast<std::size_t>(id));
    const auto conv = omk::parseConversation(id, chunk);

    omk::DialogPlayer p(state, table);
    std::ofstream f(argv[4]);
    if (!p.open(conv, chunk, fr + "/MORPH")) {
        f << "unplayable\n";
        std::printf("conversation %d is not playable\n", id);
        return 0;
    }

    // 60 seconds of frames between presses, which is longer than any line in
    // the game - so if anything here advanced on its own, it would.
    const double dt = 1.0 / 30.0;
    long ticks = 0;
    double voiced = 0.0;
    std::size_t at = 0;
    int lines = 0, menus = 0;
    for (int guard = 0; guard < 200; ++guard) {
        if (p.phase() == omk::DialogPhase::Finished) break;
        if (p.lineChanged()) {
            p.clearLineChanged();
            ++lines;
            voiced += p.lineSeconds();
            const auto* ca = p.cameraA();
            const auto* cb = p.cameraB();
            f << "line " << p.node() << ' ' << p.voice() << ' '
              << static_cast<long>(p.lineSeconds() * 100) << ' '
              << p.lineText().size() << ' '
              << (ca ? ca->id : -1) << ' ' << (cb ? cb->id : -1) << ' '
              << (ca ? static_cast<long>(ca->eye[0]) : -9999) << ' '
              << (ca ? static_cast<long>(ca->roll) : -9999) << ' '
              << (ca && ca->absolute() ? 1 : 0) << '\n';
        }
        // Let the voice run out and then keep running for a full minute -
        // longer than any line in the game. If anything advanced on its own,
        // it would happen here.
        const int before = p.node();
        const auto phaseBefore = p.phase();
        for (int k = 0; k < 1800; ++k) { p.tick(dt); ++ticks; }
        if (p.node() != before || p.phase() != phaseBefore) {
            f << "ADVANCED WITHOUT A PRESS\n";
            break;
        }
        if (p.phase() == omk::DialogPhase::Menu) {
            ++menus;
            const auto* mca = p.cameraA();
            const auto* mcb = p.cameraB();
            // The menu cuts to the node's REPLY pair and restarts the move.
            f << "menu " << (mca ? mca->id : -1) << ' ' << (mcb ? mcb->id : -1)
              << ' ' << static_cast<long>(p.cameraProgress() * 100) << '\n';
            for (const auto& r : p.replies())
                f << "  reply " << r.branch << ' ' << r.target << ' '
                  << (r.available ? 1 : 0) << ' ' << r.text.size() << '\n';
        }
        if (at >= presses.size()) break;          // out of presses: it must stop
        const char c = presses[at++];
        if (c == 'n') p.next();
        else if (c >= '0' && c <= '3') p.choose(c - '0');
    }
    f << "end " << lines << ' ' << menus << ' '
      << static_cast<long>(voiced * 100) << ' '
      << (p.phase() == omk::DialogPhase::Finished ? 1 : 0) << '\n';
    std::printf("%d lines, %d menus, %.1f s of voice, %s after %zu presses\n",
                lines, menus, voiced,
                p.phase() == omk::DialogPhase::Finished ? "finished" : "STILL OPEN",
                at);
    return 0;
}
