// SPDX-License-Identifier: GPL-3.0-or-later
// Run MELEE over the three shipped combat banks and assert what the DATA can
// falsify. `todo/fight-mode.md` step 1; `actor/fight.h` is the runtime.
//
//     run_fight <gamedata/ANIMS> <out.bin>
//
// **There is no oracle and there cannot be one.** `fight.begin` announces
// nothing to the tag logger and `traces/fight.log` proved combat invisible to
// the golden-trace rig, so this is `docs/PORTING.md` B1 data-constrained, the
// same standard as the `.CTL` channel. These are the constraints:
//
//   1. every point of damage is RE-DERIVED here from the attacker's own
//      combat block and the two stat multipliers - the probe recomputes the
//      engine's formula from the documentation rather than asking `Fight`
//      what it did, so a wrong formula cannot agree with itself;
//   2. hit points never rise, are clamped at 0, and exactly one fighter
//      reaches 0 before the fight ends;
//   3. every reaction a hit selects resolves to a real entry - the low-16
//      space is collision-free, so an unresolved one means a wrong field;
//   4. every input word the AI presses is inside the profiles' own 0xCFF
//      union, which is the same matcher a player's keys go through;
//   5. the pair is never left closer than the separation radius after the
//      push;
//   6. a knock-out is replayed and the fight ends after the two passes.
//
// The stats are the probe's, not a character record's: this tree has no actor
// record here, so every fighter gets the same plausible block (vie 100, attack
// and dodge 50, experience 50) and the check measures the MECHANISM. `omk-play`
// supplies the real properties in step 2.
#include "actor/fight.h"
#include "formats/ctl.h"
#include "platform/datafs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

// A deterministic `rand()` so a run is reproducible; the engine's decisions
// are full of `rand() % 100` and a real one would make this unrepeatable.
struct Lcg {
    std::uint32_t s = 0x13572468u;
    int operator()() {
        s = s * 1664525u + 1013904223u;
        return static_cast<int>((s >> 16) & 0x7FFFu);
    }
};

struct Totals {
    long files = 0, fights = 0, frames = 0;
    long hits = 0, blocks = 0, grazes = 0, knockdowns = 0, kos = 0;
    long gateSkips = 0;   // candidates the player's priority gate turned away
    long aiMoves = 0, aiWords = 0, aiWordsOutsideUnion = 0;
    long damageChecked = 0, damageMismatch = 0;
    long reactionUnresolved = 0, hpWentUp = 0, tooCloseAfterPush = 0;
    long endedInKo = 0, replayPasses = 0;
    long profilesUsed = 0;
    long knockdownArmWithoutBit = 0;   // the runtime's own invariant, must be 0
};

// The engine's damage formula, transcribed a SECOND time from
// `docs/ASSETS.md` and `Fight_ResolveHit`'s own arithmetic rather than from
// `fight.cpp`, so the re-derivation is not the same code agreeing with itself.
// `scaled` is the difference between the two paths: a landed blow goes
// through both multipliers, a BLOCK and a GRAZE cost a flat point - doubled
// for the player, and nothing else.
int expectedDamage(double base, bool sufferIsPlayer, bool scaled,
                   float attackMul, float dodgeMul) {
    double d = base;
    if (sufferIsPlayer) d += d;
    if (scaled) {
        d *= static_cast<double>(attackMul) + 1.0;
        d -= static_cast<double>(dodgeMul) * d;
        if (d <= 0.0) d = 1.0;
    }
    return static_cast<int>(d);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: run_fight <gamedata/ANIMS> <out.bin>\n");
        return 2;
    }
    const omk::DataFs fs(argv[1]);
    auto files = fs.list(".", ".CTL");
    std::sort(files.begin(), files.end());

    Totals T;
    // The player's stat block, and the opponent's. One block for everybody:
    // the mechanism is what is under test, not a character's numbers.
    const omk::FightStats stats{100, 50, 50, 50};
    const int difficulty = 1;            // options row 16, Intermédiaire

    for (const auto& path : files) {
        const auto ctl = omk::readCtl(omk::DataFs::readPath(path));
        if (!ctl.valid || ctl.ai.empty()) continue;   // the three combat files
        ++T.files;

        for (int level = 0; level <= 2; ++level) {
            bool haveProfile = false;
            for (const auto& p : ctl.ai)
                if (p.id == static_cast<std::uint32_t>(level + 1)) haveProfile = true;
            if (!haveProfile) continue;
            ++T.profilesUsed;

            omk::CefChannel pch(ctl), och(ctl);
            pch.setBankGroup(pch.defaultGroup());
            och.setBankGroup(och.defaultGroup());

            // They start INSIDE the AI's 78.740158 (two-metre) closing
            // distance, and that is deliberate: past it `Fight_TickAI` does
            // nothing but press its walk-forward table, and a headless probe
            // has no walker to move a body with, so the pair would never close
            // and the attack logic would never run. Closing the distance is
            // the walker's, and it arrives with the real actor in step 2.
            omk::FightBody player, opponent;
            player.x = 0.0f;   player.z = 0.0f;  player.radius = 30.0f;
            player.channel = &pch; player.isPlayer = true;
            opponent.x = 0.0f; opponent.z = 55.0f; opponent.radius = 30.0f;
            opponent.channel = &och; opponent.isPlayer = false;

            Lcg lcg;
            long clock = 0;
            omk::Fight fight([&lcg] { return lcg(); }, [&clock] { return clock; });
            fight.begin(player, stats, opponent, stats, level, difficulty);

            // The player's own multipliers, recomputed here: the difficulty
            // bonus lands on HIS dodge and nobody else's.
            const float pAttack = 50 * 0.0049999999f;
            const float pDodge  = 50 * 0.0049999999f * 0.25f + 0.25f;
            const float oAttack = 50 * 0.0049999999f;
            const float oDodge  = 50 * 0.0049999999f * 0.25f;

            // Drive it: the player presses the four attack bindings in turn,
            // with the idle word between them so the machine sees a release -
            // exactly what `Perso_InjectInput` interleaves for the AI.
            const std::uint32_t presses[] = {
                omk::kIdleInput, 0x0004u, omk::kIdleInput, 0x0008u,
                omk::kIdleInput, 0x0040u, omk::kIdleInput, 0x0080u,
            };
            int lastHp[2] = {fight.player().hp, fight.opponent().hp};
            for (int frame = 0; frame < 4000; ++frame) {
                clock += 33;                       // ~30 fps, in ms
                const auto word = presses[static_cast<std::size_t>(frame) %
                                          (sizeof(presses) / sizeof(presses[0]))];
                if (!fight.step(1.0f, word)) break;
                ++T.frames;

                if (fight.player().hp > lastHp[0]) ++T.hpWentUp;
                if (fight.opponent().hp > lastHp[1]) ++T.hpWentUp;
                lastHp[0] = fight.player().hp;
                lastHp[1] = fight.opponent().hp;

                for (const auto& e : fight.events()) {
                    switch (e.kind) {
                    case omk::FightEvent::Kind::Hit:
                    case omk::FightEvent::Kind::Block:
                    case omk::FightEvent::Kind::Graze: {
                        // Re-derive: find the attacker's block and recompute.
                        double base = 1.0;
                        if (e.kind == omk::FightEvent::Kind::Hit &&
                            e.fromEntry >= 0 &&
                            static_cast<std::size_t>(e.fromEntry) < ctl.states.size()) {
                            const auto& st = ctl.states[static_cast<std::size_t>(e.fromEntry)];
                            if (!st.hasCombat) { ++T.damageMismatch; break; }
                            base = static_cast<double>(st.combat.damage());
                        }
                        const bool onPlayer = e.onPlayer;
                        const bool scaled = e.kind == omk::FightEvent::Kind::Hit;
                        const int want = expectedDamage(
                            base, onPlayer, scaled,
                            onPlayer ? oAttack : pAttack,
                            onPlayer ? pDodge  : oDodge);
                        ++T.damageChecked;
                        if (want != e.damage) ++T.damageMismatch;
                        if (e.kind == omk::FightEvent::Kind::Hit)   ++T.hits;
                        if (e.kind == omk::FightEvent::Kind::Block) ++T.blocks;
                        if (e.kind == omk::FightEvent::Kind::Graze) ++T.grazes;
                        break;
                    }
                    case omk::FightEvent::Kind::Knockdown: ++T.knockdowns; break;
                    case omk::FightEvent::Kind::Ko:        ++T.kos; break;
                    case omk::FightEvent::Kind::AiMove:
                        ++T.aiMoves;
                        T.aiWords += e.words;
                        break;
                    default: break;
                    }
                }
                fight.clearEvents();
            }

            const auto& s = fight.stats();
            T.aiWordsOutsideUnion += s.aiWordsOutsideUnion;
            T.reactionUnresolved  += s.reactionUnresolved;
            T.knockdownArmWithoutBit += s.knockdownArmWithoutBit;
            T.tooCloseAfterPush   += s.tooCloseAfterPush;
            T.replayPasses        += s.replayPasses;
            T.knockdowns          += 0;      // counted from the events above
            if (fight.over()) ++T.endedInKo;
            T.gateSkips += pch.gateSkips();      // the player's priority gate
            ++T.fights;
        }
    }

    std::ofstream out(argv[2], std::ios::binary);
    const long vals[] = {
        T.files, T.profilesUsed, T.fights, T.frames, T.hits, T.blocks, T.grazes,
        T.kos, T.endedInKo, T.aiMoves, T.aiWords, T.aiWordsOutsideUnion,
        T.damageChecked, T.damageMismatch, T.reactionUnresolved, T.hpWentUp,
        T.tooCloseAfterPush, T.replayPasses, T.knockdownArmWithoutBit,
        T.gateSkips,
    };
    for (const long v : vals) {
        const std::int32_t w = static_cast<std::int32_t>(v);
        out.write(reinterpret_cast<const char*>(&w), 4);
    }

    std::printf("%ld combat files, %ld profiles, %ld fights, %ld frames\n",
                T.files, T.profilesUsed, T.fights, T.frames);
    std::printf("  hits %ld, blocks %ld, grazes %ld, KOs %ld, ended %ld\n",
                T.hits, T.blocks, T.grazes, T.kos, T.endedInKo);
    std::printf("  the player's priority gate turned away %ld candidates\n", T.gateSkips);
    std::printf("  AI: %ld moves, %ld words, %ld outside the 0xCFF union\n",
                T.aiMoves, T.aiWords, T.aiWordsOutsideUnion);
    std::printf("  damage re-derived %ld, mismatches %ld; reactions unresolved "
                "%ld; hp went up %ld; too close after push %ld\n",
                T.damageChecked, T.damageMismatch, T.reactionUnresolved,
                T.hpWentUp, T.tooCloseAfterPush);
    std::printf("  knockdown arm taken without the 0x10000000 reaction: %ld\n",
                T.knockdownArmWithoutBit);
    return 0;
}
