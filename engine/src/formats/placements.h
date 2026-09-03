// SPDX-License-Identifier: GPL-3.0-or-later
// THE CHARACTER PLACEMENT TABLE - `AREA +40` / `SCENE +8`, and where the
// world's people stand.
//
// `Actors_SpawnFromTables` (0x0040BB90, `readable/src/01_file.c:4815`, NAMED)
// walks these two tables once per area load - `Area_TickLoad` case 5
// (`01_file.c:5371`), after the set and the misc model and BEFORE the props
// at case 6 - and it is the walk that establishes every field, because it
// consumes each one:
//
//     +0   int16   the runtime slot, WRITTEN BACK here
//     +2   int16   the actor id  -> Actor_FindById -> the 276-byte record
//     +4   int32   x  \
//     +8   int32   y   > Actor_SetPlacement's float[4]
//     +12  int32   z  /
//     +16  int16   facing, 4096 per turn
//     +18  int16   the ObjectShown bit index; SET means attached
//
// The counts are int16 at **`AREA +72`** and **`SCENE +40`** - 830 and 202
// records over the shipped corpus - and both tables hold the same record,
// which is why `Scene_FindObjectIndexById` searches the area's and then the
// scene's (docs/FILE_FORMATS.md, "the object table"; every operand of opcodes
// 69, 82 and 84 names a record here, 1135/1135).
//
// `Area_Load` (0x0040CC90) converts all four IN PLACE before anything reads
// them - `v28 = table + 8`, then per record
//
//     u32(v28-4) = (int64)((double)(100 * u32(v28-4)) * 1/256 * 1/2.54 - 1)   x
//     u32(v28)   = ... y      u32(v28+4) = ... z
//     u16(v28+8) = (int64)((double)i16(v28+8) * 0.087890625)                  facing
//
// and `Actors_SpawnFromTables` reads the results straight back as floats
// (`v20 = (float)i32(i, 4) ... v23 = (float)i16(i, 16)`). So every one of the
// four is TRUNCATED, not rounded: the Demon's 49457/-511/19386/4073 is
// (7604, -79, 2980) facing 357, where the same arithmetic rounded would give
// (7605, -80, 2980) and 358. `rawToWorld` already truncates; the facing is
// truncated here for the same reason.
//
// The facing is NOT wrapped to (-180, 180] the way a camera's roll is:
// nothing interpolates two of these, which is the case CLAUDE.md 1's wrapping
// rule exists for, and 357 is what the engine stores.
//
// This is the character's WORLD position, not a per-conversation one: Telis
// is placed at (3635, 1278, -638), on another floor of the flat, while dialog
// 402 stages around (3503, 1081, -901).
#pragma once

#include "script/script.h"       // ChunkKind

#include <cstddef>
#include <span>
#include <vector>

namespace omk {

struct Placement {
    int   slot  = -1;            // +0, the runtime slot the spawn writes back
    int   actor = -1;            // +2
    float pos[3] = {0, 0, 0};    // +4/+8/+12, through rawToWorld
    float facing = 0.0f;         // +16, degrees, 4096 per turn, unwrapped
    int   bit = -1;              // +18, the ObjectShown index
};

// Every character an AREA or SCENE chunk places, in table order. An empty
// result is not an error: 118 places two, 222 four, SCENE 55 three, and most
// chunks none at all.
std::vector<Placement> readPlacements(std::span<const std::byte> chunk,
                                      ChunkKind kind);

}  // namespace omk
