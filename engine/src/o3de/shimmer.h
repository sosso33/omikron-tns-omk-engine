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

#include <cstddef>
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

// THE LIT PATH'S SHIMMER - `sub_494180`, the last call of `sub_440CA0`'s lit
// arm (after `sub_494E80`'s ambient start and `sub_493E40`'s lights). On a
// node carrying 0x8000000 it OVERWRITES each lit vertex colour (the runtime
// vertex's BGRA dword at +44) from the model's AUTHORED colour (the source
// vertex's +28 B, +29 G, +30 R) through the clamp table read at shifted
// bases - `byte_6A2C60` is clamp(x - 128), `byte_6A2C80` clamp(x - 96)
// (`sub_494240` fills 0x6A2BE0.. with 256 zeros, the identity, 256 x 255):
//
//     R = clamp(R0 + 3w - 128)    G = clamp(G0 + 2w - 96)    B = clamp(B0 + 2w - 128)
//
// with w the same signed wave and index as the set path. The set's lights
// are LOST, and on a character - every shipped one authors (255,255,255) -
// the body turns a pale GREEN, (127,159,127) about its mean, pulsing from
// black to white along the travelling wave: the old body `player.become`
// leaves (`Player_SetActor(-1)`, flags 268) and the player underwater
// (`sub_4187B0`, flags 0x100). Colours in 0..1, as `Corner` carries them.
inline void shimmerLit(float& r, float& g, float& b, float phase, float clock) {
    if (phase < 0.0f) return;
    const int w = kShimmerWave[((static_cast<int>(clock) >> 2) + static_cast<int>(phase)) & 31];
    const auto ch = [](float c, int add, int bias) {
        int v = static_cast<int>(c * 255.0f + 0.5f) + add - bias;
        v = v < 0 ? 0 : v > 255 ? 255 : v;
        return static_cast<float>(v) / 255.0f;
    };
    r = ch(r, 3 * w, 128);
    g = ch(g, 2 * w, 96);
    b = ch(b, 2 * w, 128);
}

// ...over a CPU-posed body: every posed corner from its REST corner's
// authored colour, phased by its vertex (`2 x index`, as a set mesh's), so
// whatever the lights wrote is overwritten as `sub_494180` overwrites it.
// A template so this header keeps needing nothing but <cstdint>.
template <class Geo>
void shimmerLitBody(Geo& posed, const Geo& rest, float clock) {
    for (std::size_t i = 0; i < posed.corners.size() && i < rest.corners.size(); ++i) {
        const auto v = i < posed.cornerVertex.size() ? posed.cornerVertex[i] : -1;
        auto& c = posed.corners[i];
        c.r = rest.corners[i].r; c.g = rest.corners[i].g; c.b = rest.corners[i].b;
        shimmerLit(c.r, c.g, c.b,
                   v >= 0 ? static_cast<float>((2u * static_cast<std::uint32_t>(v)) % 32u) : 0.0f,
                   clock);
    }
}

}  // namespace omk
