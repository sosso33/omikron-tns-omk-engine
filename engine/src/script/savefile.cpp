// SPDX-License-Identifier: GPL-3.0-or-later
#include "script/savefile.h"

#include <cstdio>

namespace omk {
namespace {

std::int32_t i32(std::span<const std::byte> d, std::size_t o) {
    if (o + 4 > d.size()) return 0;
    std::uint32_t v = 0;
    for (int k = 3; k >= 0; --k)
        v = (v << 8) | static_cast<std::uint32_t>(d[o + static_cast<std::size_t>(k)]);
    return static_cast<std::int32_t>(v);
}

}  // namespace

std::optional<SaveSlot> readSaveSlot(std::span<const std::byte> d, int slot) {
    if (slot < 0) return std::nullopt;
    const std::size_t base = kSaveHeader + kSaveSlotSize * static_cast<std::size_t>(slot);
    if (base + kSaveSlotDb + kGameDbSize > d.size()) return std::nullopt;
    SaveSlot s;
    for (std::size_t k = 0; k < 32; ++k) {
        const auto c = static_cast<unsigned char>(d[base + k]);
        if (!c) break;
        s.name.push_back(static_cast<char>(c));
    }
    s.day  = i32(d, base + 32);
    s.time = i32(d, base + 36);
    s.state = GameState::fromBytes(d.subspan(base + kSaveSlotDb, kGameDbSize));
    return s;
}

const std::array<const char*, 13>& monthNames() {
    static const std::array<const char*, 13> m = {
        "Aqed", "Nadim", "Andar", "Xenep", "Nevod", "Ganevat", "Osmydep",
        "Qomivo", "Taznevet", "Ustanevat", "Nivat", "Mozkanep", "Primevat"};
    return m;
}

std::string formatDate(int day) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d %s %d",
                  day % kDaysPerMonth + 1,
                  monthNames()[static_cast<std::size_t>(
                      day / kDaysPerMonth % kMonthsPerYear)],
                  day / kDaysPerMonth / kMonthsPerYear + kYearZero);
    return buf;
}

std::string formatTime(int t) {
    // Integer division throughout, exactly as the formatter at 0x0041E690
    // does it: an hour is 3600000/21, a minute that over 15, a second that
    // over 33 - none of which divide evenly, so a float here drifts.
    const int hour   = kDayUnits / kHoursPerDay;
    const int minute = hour / kMinutesPerHour;
    const int second = minute / kSecondsPerMinute;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d:%02d:%02d",
                  t / hour, t % hour / minute, t % hour % minute / second);
    return buf;
}

}  // namespace omk
