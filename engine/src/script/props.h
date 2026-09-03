// SPDX-License-Identifier: GPL-3.0-or-later
// The chunk-side records the world opcodes edit, as PURE functions over a
// chunk span - no Session, no archive, no state. A Session calls them on its
// resident chunks; a probe calls them on one chunk it copied out.
//
// Three tables, one per loader that fixes it (docs/FILE_FORMATS.md 5b):
//
//   the prop table       AREA +44 / count +74   SCENE +12 / +42   24 bytes
//                        `Scene_LoadProps` (0x00409FC0); ops 67, 68, 76
//   the object table     AREA +40 / count +72   SCENE +8  / +40   20 bytes
//                        `Scene_FindObjectRecord` (0x0040D6A0); ops 86, 93
//   the actor table      AREA +56 / count +80   SCENE +24 / +48   276 bytes
//                        `Actor_FindById` (0x0040B190); the stat block
//
// and the stat block itself, which `Actor_GetProperty` (0x0040B360) reads
// one case per property and `Actor_SetProperty` (0x0040B8D0) writes with its
// clamps - the two functions ops 86 and 93 are the script's ends of.
#pragma once

#include "script/script.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace omk {

inline constexpr std::size_t kActorRecordSize = 276;

// A prop record: `+0` runtime slot (-1 on disk, `Scene_LoadProps` fills it),
// `+2` OBJECTS id, `+22` the index into the DB's 2-bit PropState array.
struct PropRecord {
    std::size_t offset = 0;
    int slot = -1, id = -1, stateIndex = -1;
};
// The 24-byte walk of the 67/68/76 handlers over ONE chunk: 68 compares the
// held slot with `+0` (`movsx edi, word ptr [edx]`), 67 and 76 the id with
// `+2` (`movsx ebx, word ptr [edx+2]`). The engine walks the AREA and then
// the SCENE over it; a caller does the two calls.
std::optional<PropRecord> findPropById(std::span<const std::byte> chunk,
                                       ChunkKind kind, int id);
std::optional<PropRecord> findPropBySlot(std::span<const std::byte> chunk,
                                         ChunkKind kind, int slot);

// A prop's PLACEMENT, converted the way `Area_Load` converts it in place.
//
// `Scene_LoadProps` reads the record as an `int*` and steps `i += 6`, so the
// 24 bytes are `+0` i16 slot, `+2` i16 id, `+4/+8/+12` **int32** position
// (`v19 = (float)i[1]`), `+16/+18/+20` i16 rotation and `+22` i16 the state
// index. The position ints I first read at +16 are the ROTATION.
//
// `Area_Load` walks the table at `propTable + 8` in 24-byte steps and rewrites
// it before anything reads it::
//
//     u32(v22 - 4) = (100 * v) * 0.00390625 * 0.3937007874015748 - 1.0   // +4
//     u32(v22)     = ... same ...                                        // +8
//     u32(v22 + 4) = ... same ...                                        // +12
//     u16(v22 + 8)  = v * 0.087890625                                    // +16
//     u16(v22 + 10) = ...                                                // +18
//     u16(v22 + 12) = ...                                                // +20
//
// So a position is `v * 100 / 256 / 2.54 - 1` - hundredths of a 256th of a
// centimetre into the engine's INCH - and a rotation is a 4096-per-turn
// integer into DEGREES, `360/4096`, the same convention as the world cameras.
// CLAUDE.md's warning applies to these too: an angle near 4096 is a small
// negative one and only differs once something interpolates it.
//
// The Impasse's rings (OBJECTS 162, `3 Anneaux magiques`) are stored
// `(47397, -514, 19614)` and land at `(7288, -80, 3015)`, which is where the
// player walks - the check that the conversion is the right one.
struct PropPlacement {
    float pos[3] = {0, 0, 0};
    float rotDeg[3] = {0, 0, 0};
};
PropPlacement propPlacement(std::span<const std::byte> chunk, const PropRecord& rec);

// A 20-byte object-table record: `+0` runtime index, `+2` id, `+18` the
// ObjectShown bit. What 86/93 resolve a non-player actor through before the
// actor table - an id in the actor table but not here reads garbage in the
// engine (`mov ax, [eax]` on a null record).
struct CharacterRecord {
    std::size_t offset = 0;
    int index = -1, id = -1, stateBit = -1;
};
std::optional<CharacterRecord> findCharacterRecord(std::span<const std::byte> chunk,
                                                   ChunkKind kind, int id);

// The 276-byte actor record with `id` at +272 -> its offset in the chunk.
// `Actor_FindById` walks AREA then SCENE and returns the DB player record
// first when the id matches its +272 (or is -1); that first test is the
// caller's, since only the caller has the DB.
std::optional<std::size_t> findActorRecord(std::span<const std::byte> chunk,
                                           ChunkKind kind, int id);

// ---- the stat block --------------------------------------------------------
//
// `Actor_GetProperty`'s VALUE cases - the ones that write the argument
// block's +8, which is what op 86 stores. Properties that write the POINTER
// slot (+12: 0 `Sexe`, 6, 9..15) and the ones that use the +8 slot as an
// INPUT index (0x15, 0x16, 0x22..0x24) return false: through op 86 the engine
// stores whatever was on the stack for the first group and indexes by stack
// garbage for the second, and neither can be reproduced. Op 86 leaves the
// variable alone on false.
//
//   1 i16 +170 Vie     2 i16 +156 Mana    3 i16 +158 Speed    4 u16 +172 Argent
//   5 i16 +174 Anneaux 7 u32 +176 Type Spectre   8 i16 +154
//   16..20 i16 +160..+168 (Attack, Body Shield, Dodge, Fight Experience, +168)
//   0x17..0x21 i16 +200, +198, +192, +180, +186, +184, +182, +188, +190, +196, +194
//   0x25 i16 +250
bool readActorProperty(std::span<const std::byte> record, int property,
                       std::int32_t& out);

// `Actor_SetProperty`'s cases, clamps included, on a record of 276 bytes:
//   1, 2, 3, 16..20  `cmp esi, 0C8h ; jbe` - UNSIGNED, so a negative value
//                    becomes 200 too - then a u16 store
//   4                `cmp esi, 0FFFFh ; jbe`, the same shape at 65535
//   5                no clamp
//   0x23 (35)        `u16(rec + 2*HIWORD(v), 260) = v` - the ammunition
//                    array, slot in the high word, count in the low
// Any other property writes nothing (the switch's default) - false.
bool writeActorProperty(std::span<std::byte> record, int property,
                        std::int32_t value);

// The record's `+270`: the OBJECTS id it holds, -1 for none.
int  heldObjectOf(std::span<const std::byte> record);
void setHeldObjectOf(std::span<std::byte> record, int objectId);

}  // namespace omk
