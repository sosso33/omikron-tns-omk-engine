// SPDX-License-Identifier: GPL-3.0-or-later
// The save file and the game CLOCK.
//
// ## The file
//
// `IAM\GAMES` is 256 slots behind a fixed header.  The geometry comes from
// three literals in the writers:
//
//     slot   = 32 (profile name) + 4 (day) + 4 (time) + 8192 (the DB)
//                                          + 24576 (a frame capture) = 32808
//     file   = 3496 + 256 * 32808 = 8402344
//
// and a save the engine actually wrote is exactly 8402344 bytes, which settles
// the arithmetic.  It also settles what the header IS, which the derivation
// could not: it opens with `OMK_SAVE`, then `640 x 480`, and only 119 of its
// 3496 bytes are non-zero.  So the header is the PROFILE AND SETTINGS block
// and a slot is self-describing - `SaveDir_CountByName`'s 256 x 72 walk is
// over an IN-MEMORY directory, not over this file.  Two readings had been
// disagreeing about that, and neither was checkable until a save existed.
//
// ## The clock
//
// Omikron's calendar: 41 days a month, 13 months a year, year 7216, and a day
// of 3600000 units divided into 21 hours of 15 minutes of 33 seconds.  Every
// constant is a `dd` in the data segment; `Game_NewGame` starts the player at
// day 52, time 2000000 - 12 Nadim 7216, 11:10:00.
//
// The test the calendar could fail is in the shipped data rather than in the
// code: eight objects in `IAM\OBJECT` are in-world newspapers named for their
// date, and all eight are legal in this calendar - month one of the thirteen
// names, day never past 41, with `41 Andar` landing exactly on the month
// length.
#pragma once

#include "script/gamestate.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace omk {

inline constexpr std::size_t kSaveHeader = 3496;
inline constexpr std::size_t kSaveSlots  = 256;
inline constexpr std::size_t kSaveSlotDb = 40;      // name[32] + day + time
inline constexpr std::size_t kSaveShot   = 24576;
inline constexpr std::size_t kSaveSlotSize = kSaveSlotDb + kGameDbSize + kSaveShot;
inline constexpr std::size_t kSaveFileSize = kSaveHeader + kSaveSlots * kSaveSlotSize;

struct SaveSlot {
    std::string  name;
    std::int32_t day = 0;
    std::int32_t time = 0;
    GameState    state;
};

// One slot out of a save file.  Nothing when the buffer does not reach it -
// `traces/save-appart.bin` is a truncated fixture holding the header and
// slot 0's first 8232 bytes, so a reader that insisted on the whole 8402344
// could not read the only real save there is.
std::optional<SaveSlot> readSaveSlot(std::span<const std::byte> d, int slot);

// ------------------------------------------------------------------ the clock
inline constexpr int kDaysPerMonth = 41, kMonthsPerYear = 13, kYearZero = 7216;
inline constexpr int kDayUnits = 3600000, kHoursPerDay = 21;
inline constexpr int kMinutesPerHour = 15, kSecondsPerMinute = 33;
inline constexpr int kNewGameDay = 52, kNewGameTime = 2000000;

const std::array<const char*, 13>& monthNames();

std::string formatDate(int day);
std::string formatTime(int t);

}  // namespace omk
