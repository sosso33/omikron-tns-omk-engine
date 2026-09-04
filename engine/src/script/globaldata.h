// SPDX-License-Identifier: GPL-3.0-or-later
// `IAM\GLOBAL` - a plain file with a fixed header, NOT an archive.
//
// `sub_40DE60` fopen's it and reads:
//
//     +8   int32  the script / message-subscription table (file-relative)
//     +12  int32  the OBJECT COMBINATION table
//     +16  int32  THE SLIDER DESTINATIONS, 36 bytes each  (count +28)
//     +20  int32  the record array, 44 bytes each
//     +24  int16  the subscription count
//     +28  int16  the destination count
//     +30  int16  the record count
//     +32  int16  the weapon/ammunition table, 2 bytes a slot
//     +64  int32  ...
//
// Reading it as an archive - which an earlier pass did - finds a plausible
// chunk and then has to guess where the table ends; that guess overran by one
// entry and lost two scripts (CLAUDE.md 1).
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace omk {

// One combination recipe: two ingredients make a product, gated on a value.
//
// The gate is the finding: **it is never 8**, and nothing in the game writes
// 8 - so six of the eleven recipes can never fire, and five spell items are
// unobtainable (one survives as a world prop).
struct Recipe {
    std::int16_t a = 0, b = 0, product = 0, gate = 0;
};

std::vector<Recipe> globalRecipes(std::span<const std::byte> d);

// GLOBAL +64 (int16) - the object whose selection sets the recipe gate to 1.  The
// global it is compared against has exactly four references in the binary, all
// inside `Game_HandleEvent` case 37: 1 for this object, 0 for anything else,
// -1 after a combine.  Nothing anywhere writes 8.
std::int32_t globalSpellItem(std::span<const std::byte> d);

// THE SLIDER'S DESTINATIONS - `GLOBAL +16`, count `+28`, 36 bytes each, and
// neither field was in this header until 2026-09-04.
//
// The sneak's slider page shares the device's nine row widgets with the
// inventory and memory pages, and which SOURCE fills them is one global,
// `dword_670CB8`, written by each page's builder: 0 inventory, 2 memory,
// **4 slider**. `sub_42ADD0` treats 4 specially - it does NOT raise the
// inventory channel's event 25, and instead sets flag `0x1000` on every row
// widget, which is the flag `sub_42AA00` tests to take its text from
// `sub_40E540(tag)` rather than from the channel's case 33.
//
// `sub_40E540` and `sub_40E8E0` (the count) walk this array and keep only the
// entries whose bit is set in the game DB's `+24` array - which is
// `StateArray::AddressEnabled`, the 791-bit map VM ops 87 `address.enable`
// and 88 `address.disable` write. So the slider lists the places you have
// been given, and `sub_40E540` returns `record + 4`, an inline 32-byte name.
//
// The parse is self-checking on the shipped file: 39 records ending at 6584
// of 6760 bytes, every name NUL-terminated inside its 32, and all 39 bit
// indices distinct. The names are the ones a player's capture shows -
// "Anekbah - Appartement de Kay'l", "Anekbah - Sas vers Qalisar",
// "Anekbah - Centre de securite".
struct Destination {
    int         bit = -1;      // index into StateArray::AddressEnabled
    std::string name;          // the record's `+4`, up to 32 bytes
};
std::vector<Destination> globalDestinations(std::span<const std::byte> d);

}  // namespace omk
