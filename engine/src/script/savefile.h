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
// ## The header is the SETTINGS, field by field
//
// The 117 non-zero bytes after the magic are now all named (GAME_STATE 8a).
// The header is the global `byte_90E180`; `sub_41F4C0` writes every field of
// it in one run, `Game_WriteSave` copies all 3496 over the file's head on
// EVERY slot save, and `SaveDir_Load` reads them back behind the magic and a
// version dword at +8.  So there is one copy of the settings for all 256
// slots, and saving a game saves the player's options.
//
// Each field is named by the option row that reads it: +12 the resolution,
// +16 sky, +17 shadows, +20 clip distance, +24/+28/+32 the three volumes,
// +36 3D sound, +37 subtitles, +38/+40 the two difficulties, +42 the combat
// camera, +44/+46 mouse sensitivity, +48 inverted, +49 force feedback,
// +1446 CROWD DENSITY and +1447 LEVEL OF DETAIL.  The last two matter here
// because they are the two graphical options with no `[Preferences]` ini key,
// and they persist anyway.
//
// +52 / +276 / +500 are the three control-scheme tables verbatim - the same
// 4 x 14 x u32 the port carries in `tables/key_bindings.json`, at the offsets
// the globals' own addresses give (0x90E1B4/0x90E294/0x90E374 minus
// 0x90E180).  All 168 cells match in both shipped saves, and the two saves
// differ in exactly two bytes of the 3496: +20 (200 vs 150, two of row 3's
// five choices) and +1446 (4 vs 3, two of row 6's five).
// `verify.py: settings block`.
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

// ------------------------------------------------- the header: the SETTINGS
//
// The 3496 bytes are the global `byte_90E180`, and `sub_41F4C0` writes every
// field of it in one run (GAME_STATE 8a).  `Game_WriteSave` copies the whole
// block over the file's head on EVERY slot save, so there is one copy of the
// settings for all 256 slots and saving a game saves the player's options.
//
// Each field is named by the option row whose read hook reads it - the rows
// and the defaults function are in the same order.  Two of them, the crowd
// density and the level of detail, are the graphical settings with no
// `[Preferences]` ini key, and they persist here.
struct SettingsBlock {
    // +8, `dword_90E188`.  `SaveDir_Load` accepts 0x10000 and 0x10001, and
    // clears the low half of the +1444 dword for the older one.
    std::uint32_t version = 0x10001u;
    int screenX = 640, screenY = 480;   // +12/+14, row 2
    bool sky = true;                    // +16,  row 4
    bool shadows = true;                // +17,  row 5
    int  clipDistance = 50;             // +20,  row 3 - METRES, one of 25/50/100/150/200
    int  volumeDialogue = 0;            // +24,  row 10
    int  volumeMusic = 0;               // +28,  row 11
    int  volumeEffects = 0;             // +32,  row 12
    bool sound3d = true;                // +36,  row 13
    bool subtitles = true;              // +37,  row 15
    int  fightDifficulty = 1;           // +38,  row 16
    int  shootDifficulty = 1;           // +40,  row 17
    int  combatCamera = 1;              // +42,  row 18
    int  mouseSensitivityX = 20;        // +44,  row 23
    int  mouseSensitivityY = 15;        // +46,  row 24
    bool mouseInverted = false;         // +48,  row 25
    bool forceFeedback = false;         // +49,  row 27
    // +52 / +276 / +500 - the three control-scheme tables VERBATIM, in the
    // 4 groups x 14 actions layout of `tables/key_bindings.json`, at the
    // offsets the globals' own addresses give (0x90E1B4/0x90E294/0x90E374
    // minus 0x90E180).  Three contiguous 224-byte tables.
    std::array<std::uint32_t, 56> keyboard{};
    std::array<std::uint32_t, 56> mouse{};
    std::array<std::uint32_t, 56> joystick{};
    int  streetActivity = 0;            // +1446, row 6 - the CROWD DENSITY
    int  levelOfDetail = 0;             // +1447, row 7
};

inline constexpr std::size_t kBindKeyboard = 52;
inline constexpr std::size_t kBindMouse    = 276;
inline constexpr std::size_t kBindJoystick = 500;

// Parse the block.  Nothing unless the buffer reaches 3496 bytes AND opens
// with `OMK_SAVE` - the magic is what `SaveDir_Load` gates on, and without it
// the engine keeps the defaults rather than reading garbage.
std::optional<SettingsBlock> readSettingsBlock(std::span<const std::byte> d);

// The values `sub_41F4C0` writes - a default-constructed SettingsBlock, minus
// the binding tables, which come from `tables/key_bindings.json`.
SettingsBlock defaultSettingsBlock();

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
