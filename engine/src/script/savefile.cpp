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

std::optional<SettingsBlock> readSettingsBlock(std::span<const std::byte> d) {
    if (d.size() < kSaveHeader) return std::nullopt;
    const auto* b = reinterpret_cast<const unsigned char*>(d.data());
    // the magic `SaveDir_Load` gates on, before anything is believed
    static const char kMagic[] = "OMK_SAVE";
    for (int i = 0; i < 8; ++i)
        if (b[i] != static_cast<unsigned char>(kMagic[i])) return std::nullopt;

    auto u32 = [&](std::size_t o) {
        return static_cast<std::uint32_t>(b[o]) | (static_cast<std::uint32_t>(b[o + 1]) << 8) |
               (static_cast<std::uint32_t>(b[o + 2]) << 16) | (static_cast<std::uint32_t>(b[o + 3]) << 24);
    };
    auto i16 = [&](std::size_t o) {
        return static_cast<int>(static_cast<std::int16_t>(
            static_cast<std::uint16_t>(b[o]) | (static_cast<std::uint16_t>(b[o + 1]) << 8)));
    };

    SettingsBlock s;
    s.version           = u32(8);
    s.screenX           = i16(12);
    s.screenY           = i16(14);
    s.sky               = b[16] != 0;
    s.shadows           = b[17] != 0;
    s.clipDistance      = static_cast<int>(u32(20));
    s.volumeDialogue    = static_cast<int>(u32(24));
    s.volumeMusic       = static_cast<int>(u32(28));
    s.volumeEffects     = static_cast<int>(u32(32));
    s.sound3d           = b[36] != 0;
    s.subtitles         = b[37] != 0;
    s.fightDifficulty   = i16(38);
    s.shootDifficulty   = i16(40);
    s.combatCamera      = b[42];
    s.mouseSensitivityX = i16(44);
    s.mouseSensitivityY = i16(46);
    s.mouseInverted     = b[48] != 0;
    s.forceFeedback     = b[49] != 0;
    for (std::size_t k = 0; k < 56; ++k) {
        s.keyboard[k] = u32(kBindKeyboard + k * 4);
        s.mouse[k]    = u32(kBindMouse    + k * 4);
        s.joystick[k] = u32(kBindJoystick + k * 4);
    }
    // `dword_90E724`: the low half is the display driver's and `SaveDir_Load`
    // clears it for a 0x10000 file, so only these two bytes persist.
    s.streetActivity = b[1446];
    s.levelOfDetail  = b[1447];
    return s;
}

SettingsBlock defaultSettingsBlock() { return SettingsBlock{}; }

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
