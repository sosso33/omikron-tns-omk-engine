// SPDX-License-Identifier: GPL-3.0-or-later
// THE SHIMMER - mesh flag 0x8000000, and it is the game's, not an enhancement.
//
// 233 set meshes carry it and 0 characters, and the names put it on distant
// scenery: `berg*` (91), `fond*` (52), `paro*`, `mont*`, `mafo*`. Lahoreh has
// 132 of them - about 15% of its vertices - and Jaunpur 33, where Anekbah has
// one. So it is the far skyline of a city, and in the original it moves.
//
// `sub_4947F0`'s per-vertex loop (`26_ole.c` 2333) adds one entry of a 32-byte
// table into all three colour bytes through the engine's clamp table:
//
//     colour = clamp(src + kWave[((clock >> 2) + (vertexAddress >> 4)) & 31])
//
// **Every link was checked before this was built**, because the neighbouring
// environment-map flag looks just as alive and is DEAD (its texture stage is
// given an index initialised to -1 and never assigned):
//
//   * the flag is carried - 233 meshes;
//   * the table is real data, not zeros - read out of `Runtime 2.exe` at
//     0x004DDBB0 and asserted by `verify.py: shimmer table`;
//   * the code reads it - a live `else if` in the vertex loop, `movsx` so the
//     entries are SIGNED and it darkens as well as brightens;
//   * the clock advances - `Game_Tick` sets it every frame and then does
//     `clock += 2 * frameDelta`, wrapping at 256.
//
// So it is a full oscillation about zero on a 32-step cycle over 64 frames,
// about 2.1 s, and neighbouring vertices sit 1/16 of a cycle apart so the wave
// TRAVELS across a surface.
#pragma once

#include <cstdint>

namespace omk {

// `byte_4DDBB0`, 32 signed entries: 0 -> +64 -> 0 -> -64 -> 0.
inline constexpr std::int8_t kShimmerWave[32] = {
      0,  10,  13,  18,  28,  32,  48,  56,
     64,  56,  48,  32,  28,  18,  13,  10,
      0, -10, -13, -18, -28, -32, -48, -56,
    -64, -56, -48, -32, -28, -18, -13, -10,
};

// The clock wraps at 256 (`Game_Tick`), and the index takes its integer part
// shifted right two - so 64 frames of clock make one 32-step cycle.
inline constexpr float kShimmerWrap = 256.0f;

// -> the signed offset for a corner whose `phase` the geometry builder set.
// A phase below zero means the mesh does not shimmer.
inline float shimmerOffset(float phase, float clock) {
    if (phase < 0.0f) return 0.0f;
    const int i = ((static_cast<int>(clock) >> 2) + static_cast<int>(phase)) & 31;
    return static_cast<float>(kShimmerWave[i]) / 255.0f;
}

}  // namespace omk
