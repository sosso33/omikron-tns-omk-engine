// SPDX-License-Identifier: GPL-3.0-or-later
// THE SCREEN FADES - two independent state machines, neither of them ported
// until 2026-09-03.
//
//     fade_probe <gamedata/IAM> <vm_opcodes.json> <START> <out.bin>
//
// `Screen_StartColorFade` (0x00451DC0) owns the colour fade and `Screen_Fade`
// (0x0041E1B0) the black one, and the interesting parts are the rules rather
// than the ramp: the colour fade REFUSES a new one while another runs unless
// the new one is a "from" over a running "to", and the black fade goes back
// only from state 3.
//
// The decode is the other half. 118/119 pack their colour as a DWORD out of
// the first FOUR operand bytes, so the Impasse's opening fade - which a four
// int16 view reads as `-1, 255, 25, 0` - is really colour **0x00FFFFFF**, and
// it is WHITE rather than the red a play report went looking for. The red in
// this family is the engine's own override: a fade issued by the MESSAGE-0
// handler is forced to 0xFF0000 (`byte_4C012C` against the context's +30).
#include "platform/datafs.h"
#include "script/area.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: fade_probe <IAM> <vm_opcodes.json> <START> <out.bin>\n");
        return 2;
    }
    const auto table = omk::OpcodeTable::loadJson(argv[2]);
    auto state = omk::GameState::fromFile(argv[3]);
    omk::Session s(argv[1], state, table);

    // ---- the colour fade's refusal rule ---------------------------------
    s.startColourFade(1, 0x00FF0000u, 10.0f);
    const int afterTo = s.colourFade().mode;
    s.startColourFade(1, 0x0000FF00u, 10.0f);          // refused: to over to
    const int refusedToOverTo = s.colourFade().colour == 0x00FF0000u ? 1 : 0;
    s.startColourFade(2, 0x000000FFu, 10.0f);          // allowed: from over to
    const int allowedFromOverTo = s.colourFade().mode == 2 ? 1 : 0;
    s.startColourFade(2, 0x00FFFFFFu, 10.0f);          // refused: from over from
    const int refusedFromOverFrom = s.colourFade().colour == 0x000000FFu ? 1 : 0;

    // ...and the ramp's direction. A "from" starts full and ends clear.
    const int fromStartsFull = s.colourFade().weight() > 0.99f ? 1 : 0;
    for (int i = 0; i < 5; ++i) s.tickFades();
    const int halfway = static_cast<int>(s.colourFade().weight() * 100.0f + 0.5f);
    for (int i = 0; i < 6; ++i) s.tickFades();
    const int fromEnds = s.colourFade().running() ? 0 : 1;

    // ---- the black fade, and its DIRECTION ------------------------------
    // 132 fades IN and 133 fades OUT, the opposite of what the opcode table
    // calls them. The direction is the whole of this: `fade.to_black` is the
    // first thing SCENE 55 does, so if it painted black the cutscene would be
    // black from two seconds in until its partner 167 instructions later -
    // which is exactly what a play report described.
    omk::Session b(argv[1], state, table);
    b.startBlackFade(false);                            // refused: no state 3
    const int backWithoutIn = b.blackFade().mode;
    b.startBlackFade(true);                             // 132: the fade IN
    const int fadeInMode = b.blackFade().mode;
    const int fadeInDur = static_cast<int>(b.blackFade().duration);
    // it starts FULLY BLACK and ends showing the scene - the other way round
    // and every cutscene goes dark
    const int inStartsBlack = b.blackFade().weight() > 0.99f ? 1 : 0;
    for (int i = 0; i < 100; ++i) b.tickFades();
    const int inEndsClear = b.blackFade().weight() < 0.01f ? 1 : 0;
    b.startBlackFade(false);                            // 133: the fade OUT
    const int fadeOutMode = b.blackFade().mode;
    const int outStartsClear = b.blackFade().weight() < 0.01f ? 1 : 0;
    for (int i = 0; i < 30; ++i) b.tickFades();
    const int outHalf = static_cast<int>(b.blackFade().weight() * 100.0f + 0.5f);
    for (int i = 0; i < 40; ++i) b.tickFades();
    const int outClears = b.blackFade().running() ? 0 : 1;

    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    for (int v : {afterTo, refusedToOverTo, allowedFromOverTo, refusedFromOverFrom,
                  fromStartsFull, halfway, fromEnds,
                  backWithoutIn, fadeInMode, fadeInDur, inStartsBlack, inEndsClear,
                  fadeOutMode, outStartsClear, outHalf, outClears})
        put32(v);
    if (!omk::safeOutputPath(argv[4])) return 2;
    std::ofstream f(argv[4], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));

    std::printf("colour fade: to -> mode %d; to over to refused %d; from over to "
                "allowed %d; from over from refused %d\n",
                afterTo, refusedToOverTo, allowedFromOverTo, refusedFromOverFrom);
    std::printf("  a `from` starts full %d, is %d%% at the halfway tick, and ends %d\n",
                fromStartsFull, halfway, fromEnds);
    std::printf("black fade: out before in -> %d; the fade IN is mode %d over %d "
                "frames, starts black %d, ends clear %d\n",
                backWithoutIn, fadeInMode, fadeInDur, inStartsBlack, inEndsClear);
    std::printf("  the fade OUT is mode %d, starts clear %d, is %d%% black "
                "halfway, and CLEARS at the end %d\n",
                fadeOutMode, outStartsClear, outHalf, outClears);
    return 0;
}
