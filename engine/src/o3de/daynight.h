// SPDX-License-Identifier: GPL-3.0-or-later
// THE DAY/NIGHT CYCLE - `sub_41E7A0` (0x0041E7A0), transcribed.
//
// `Game_Tick` calls it every frame, just before `Clock_Tick`. For every SHOWN
// decor slot it lerps the scene's `+336` - the FOG colour, and through
// `dword_90EFB0` the colour the screen is CLEARED to (`Game_Tick`'s
// `dword_90E0A0(Color_Sum(dword_90EFB0), ...)`) - between four colours the
// AREA chunk carries at `+144` (`Area_TickLoad` case 9 -> `sub_41D380` copies
// them to the slot's `+112`), one per quarter of the game day:
//
//     t     = clock % 3600000                    (g_ClockUnitsPerDay)
//     phase = t / 900000                         (dword_4C2C10)
//     u     = (t % 900000) / 900000.0
//     c     = (int64)((next - cur) * u) + cur    per byte, `next` = phase + 1 mod 4
//
// And in the 19 of 242 areas whose chunk sets the byte at `+178`
// (`sub_41D3F0` -> slot `+109`) it ALSO rewrites the scene's `+416` - the
// ambient grey every lit vertex starts from and every unlit one is floored at
// (`docs/FILE_FORMATS.md`) - from the clock, with `dword_4C2C28` = 128:
//
//     q = 64 * (t % 900000) / 900000            (integer)
//     phase 0: 64 + q   (64 -> 128)    phase 1: 128 - q (128 -> 64)
//     phase 2: 64 - q   (64 -> 0)      phase 3: q       (0 -> 64)
//
// and `+420` to `grey - 129`, below zero, so the unsigned ceiling compare
// never bites. In every other area `+416` stays the set's `.3DO` ambient.
//
// The colours are stored `0x00BBGGRR` - byte 0 is RED: the greyscale bank
// weighs `+336` as `(299 * byte0 + 587 * byte1 + 114 * byte2) / 1000`, so the
// cave's `0x1cdf` is (223, 28, 0), a lava red, and Anekbah's night `0x1a1309`
// a dark blue. NOT ported: the `dword_93082C` mode (`0x405028`, a 15 m fog -
// `docs/ASSETS.md` "The fog"). todo/drift-audit.md L1 step 4.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace omk {

struct DayNight {
    std::uint8_t rgb[3] = {0, 0, 0};   // the fog and clear colour
    bool floorByClock = false;          // the area's +178 flag
    int  floorGrey = 0;                 // the +416 grey, when floorByClock
    int  phase = 0;                     // 0..3, for the log
};

// The cycle for one area chunk at game-clock time `clock`. A chunk too short
// to carry the fields answers the black, static default.
DayNight dayNightAt(std::int32_t clock, std::span<const std::byte> areaChunk);

}  // namespace omk
