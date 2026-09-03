// SPDX-License-Identifier: GPL-3.0-or-later
// `IAM\GLOBAL` - a plain file with a fixed header, NOT an archive.
//
// `sub_40DE60` fopen's it and reads:
//
//     +8   int32  the script / message-subscription table (file-relative)
//     +12  int32  the OBJECT COMBINATION table
//     +20  int32  the record array, 44 bytes each
//     +24  int16  the subscription count
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

}  // namespace omk
