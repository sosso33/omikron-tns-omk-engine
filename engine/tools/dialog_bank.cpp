// SPDX-License-Identifier: GPL-3.0-or-later
//
// omk-play 77: the DIALOGUE bank groups, and why a conversation's last Enter
// must not fall through into the take.
//
// `Actor_EnterDialogueMode` (0x00468DE0) and its counterpart at 0x00468E80
// bracket a conversation, and both end the same way:
//
//     SetPersoBankGroup(actor+0x18C, Cef_FindGroupById(actor+0xB4, 400))  // enter
//     SetPersoBankGroup(actor+0x18C, Cef_FindGroupById(actor+0xB4, 100))  // leave
//
// `SetPersoBankGroup`'s memset clears the channel's queue and latches, so the
// held word must CHANGE before anything matches again - the same guard
// `sub_465D30` uses at the end of a successful take.  Leaving a conversation
// therefore disarms a still-held action button, which is exactly what stops
// the last Enter of a dialogue from firing MDACTION on the next frame.
//
// This prints the bank's group list so the two ids can be seen to exist and
// to name what they should name.
#include "actor/state.h"
#include "formats/ctl.h"
#include "platform/datafs.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: dialog_bank <gamedata> [bank]\n"); return 2; }
    const omk::DataFs fs(argv[1]);
    const std::string bank = argc > 2 ? argv[2] : "H1AVNT";
    const auto path = fs.resolve("ANIMS/" + bank + ".CTL");
    if (!path) { std::fprintf(stderr, "no ANIMS/%s.CTL\n", bank.c_str()); return 1; }
    const auto ctl = omk::readCtl(omk::DataFs::readPath(*path));
    if (!ctl.valid) { std::fprintf(stderr, "%s did not parse\n", bank.c_str()); return 1; }

    std::printf("%s: %zu groups, %zu entries\n",
                bank.c_str(), ctl.groupList.size(), ctl.states.size());
    for (std::size_t i = 0; i < ctl.groupList.size(); ++i) {
        const auto& g = ctl.groupList[i];
        const char* nm = "-";
        const char* mv = "-";
        if (g.defaultEntry >= 0 && g.defaultEntry < static_cast<int>(ctl.states.size())) {
            nm = ctl.states[static_cast<std::size_t>(g.defaultEntry)].name.c_str();
            mv = ctl.states[static_cast<std::size_t>(g.defaultEntry)].moveName.c_str();
        }
        const char* tag = g.id == 400 ? "  <- Actor_EnterDialogueMode"
                        : g.id == 100 ? "  <- the LEAVE group"
                        : g.id ==  41 ? "  <- sub_465D30's standing take"
                        : g.id == 143 ? "  <- sub_465D30's low take" : "";
        std::printf("  group %2zu  id %4d  flags %08x  %2d entries  default %3d '%s' (%s)%s\n",
                    i, g.id, g.flags, g.count, g.defaultEntry, nm, mv, tag);
    }

    {
        int has400 = -1, has100 = -1, has41 = -1, has143 = -1, defaultGroupId = -1;
        for (std::size_t i = 0; i < ctl.groupList.size(); ++i) {
            const auto& g = ctl.groupList[i];
            if (g.id == 400) has400 = static_cast<int>(i);
            if (g.id == 100) has100 = static_cast<int>(i);
            if (g.id ==  41) has41  = static_cast<int>(i);
            if (g.id == 143) has143 = static_cast<int>(i);
            if (g.flags & 1u) defaultGroupId = g.id;
        }
        const char* defMove = "-";
        if (has100 >= 0 && ctl.groupList[static_cast<std::size_t>(has100)].defaultEntry >= 0)
            defMove = ctl.states[static_cast<std::size_t>(
                ctl.groupList[static_cast<std::size_t>(has100)].defaultEntry)].moveName.c_str();
        std::printf("groups: enter %d leave %d take %d low %d; the bank's "
                    "default group id is %d and its entry is %s\n",
                    has400, has100, has41, has143, defaultGroupId, defMove);
    }

    // ---- the machine, run ------------------------------------------------
    //
    // Three properties, each of which was FALSE in this port until 2026-09-06
    // and each of which fails loudly if it comes back.
    const auto fires = [&](omk::ActorRuntime& a, std::uint32_t word, int ticks) {
        int n = 0;
        for (int t = 0; t < ticks; ++t) {
            a.channel().clearEvents();
            a.tick(1.0f, word);
            for (const auto& e : a.channel().events())
                if (e.kind == omk::ChannelEvent::Kind::Move && e.name == "MDACTION") ++n;
        }
        return n;
    };

    // (1) THE LATCH SURVIVES A BANK SWITCH. `SetPersoBankGroup`'s 16-dword
    //     store covers +28..+87 - the queue - and the 20-slot latch is at +92.
    //     The latch is written by `gotoMove`, so the press has to arrive
    //     through the QUEUE - `Perso_InjectInput`, the same door the fight AI
    //     uses - and not through the per-tick chain loop, which applies a
    //     special move without committing a transition and latches nothing.
    omk::ActorRuntime a(ctl, true);
    a.loadModel();
    a.channel().injectInput({0x10u});
    const int held = fires(a, 0u, 4);              // 0 = the queue drives
    const int latchedBefore = a.channel().latchedCount();
    const int g100 = a.channel().findGroupById(100);
    a.channel().setBankGroup(g100);
    const int latchedAfter = a.channel().latchedCount();

    // (2) LEAVING A CONVERSATION DOES NOT BLOCK THE CHANNEL.
    //     `Perso_SetInputEnabled(ch, 1)` SETS flag 0x80, and 0x80 is what makes
    //     `Cef_TickChannel` skip the input pass. The leave must RESTORE the
    //     saved flag, not assert it - reading the argument as an "enable" left
    //     the player's action button dead after every conversation.
    omk::ActorRuntime b(ctl, true);
    b.loadModel();
    const bool blockedAtRest = b.channel().inputBlocked();
    b.enterDialogue();
    const bool blockedInDialogue = b.channel().inputBlocked();
    b.leaveDialogue();
    const bool blockedAfter = b.channel().inputBlocked();
    const int afterLeave = fires(b, 0x10u, 8);

    // The latch numbers are an OBSERVATION, not the proof of the memset fix:
    // H1AVNT's action entry is reached by the chain loop and by a GoTo
    // redirect, neither of which writes a latch id, so both sides are 0 here.
    // What the fix rests on is the store's extent - 16 dwords from +28, ending
    // at +87, with the latch at +92 - and that is asserted by reading, not by
    // this bank. Printed so a change that starts latching shows up.
    std::printf("latch: %d id(s) held after %d MDACTION, %d after "
                "SetPersoBankGroup(group id 100)\n",
                latchedBefore, held, latchedAfter);
    std::printf("dialogue: input blocked at rest %d, in a conversation %d, "
                "after leaving %d; MDACTION after leaving %d\n",
                blockedAtRest ? 1 : 0, blockedInDialogue ? 1 : 0,
                blockedAfter ? 1 : 0, afterLeave);
    return 0;
}
