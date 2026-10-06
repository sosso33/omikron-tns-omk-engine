// SPDX-License-Identifier: GPL-3.0-or-later
#include "o3de/daynight.h"

#include "formats/le.h"

namespace omk {

namespace {
constexpr std::int32_t kDay = 3600000;    // g_ClockUnitsPerDay
constexpr std::int32_t kPhase = 900000;   // dword_4C2C10
constexpr int kLevel = 128;               // dword_4C2C28
}  // namespace

DayNight dayNightAt(std::int32_t clock, std::span<const std::byte> areaChunk) {
    DayNight out;
    if (areaChunk.size() < 180) return out;
    std::uint32_t col[4];
    for (int i = 0; i < 4; ++i)
        col[i] = loadLE<std::uint32_t>(areaChunk.data() + 144 + 4 * i);
    const std::uint32_t f176 = loadLE<std::uint32_t>(areaChunk.data() + 176);
    // `%` on a NEGATIVE clock is C's, as the engine's own idiv leaves it
    const std::int32_t t = clock % kDay;
    int phase = t / kPhase;
    if (phase < 0 || phase > 3) phase = 0;
    const int next = (phase + 1) % 4;
    const double u = static_cast<double>(t % kPhase) / static_cast<double>(kPhase);
    const std::uint32_t c0 = col[phase], c1 = col[next];
    for (int b = 0; b < 3; ++b) {
        const int a = static_cast<int>((c0 >> (8 * b)) & 0xFF);
        const int z = static_cast<int>((c1 >> (8 * b)) & 0xFF);
        // `(int64)((double)(next - cur) * u) + cur`, kept to a byte
        const std::int64_t v = static_cast<std::int64_t>(static_cast<double>(z - a) * u) + a;
        out.rgb[b] = static_cast<std::uint8_t>(v & 0xFF);
    }
    out.phase = phase;
    out.floorByClock = ((f176 >> 16) & 0xFF) == 1;
    if (out.floorByClock) {
        const int half = kLevel >> 1;
        const int q = static_cast<int>(static_cast<std::int64_t>(half) * (t % kPhase) / kPhase);
        switch (phase) {
        case 0: out.floorGrey = q + half; break;
        case 1: out.floorGrey = kLevel - q; break;
        case 2: out.floorGrey = half - q; break;
        default: out.floorGrey = q; break;
        }
    }
    return out;
}

}  // namespace omk
