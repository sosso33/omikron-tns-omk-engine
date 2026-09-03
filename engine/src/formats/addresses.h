// SPDX-License-Identifier: GPL-3.0-or-later
// THE ADDRESSES TABLE - `AREA +60`, and what `actor.goto_address` teleports to.
//
// VM opcode 73 `actor.goto_address` names an id that `Address_Find` resolves
// against this table; the result is handed to `sub_41BF50` with the actor
// (docs/SCRIPT_VM.md 73). It is the engine's only way of PLACING a character
// at an authored spot, and the intro's last three instructions are
//
//     area.goto 222, -1, -1      load the Impasse
//     scene.load 222, 55
//     actor.goto_address 654     <- put the player HERE
//     area.arrive 118
//
// so without it the player has no world position at all - and 1443 of the
// 5384 world cameras take at least one of their two points as an OFFSET from
// an actor, so a quarter of the game's cameras cannot be resolved and the
// frame is black. That is the whole of the black-screen-with-audio at the end
// of the intro cutscene.
//
// The layout, corroborated 791/791 against the shipped `ADDRESSES.TAG` names
// (docs/RECONSTRUCTION.md 2026-08-27):
//
//     AREA +60   int32   the array's offset in the chunk
//     AREA +82   int16   how many records
//     record, 16 bytes:
//       +0/+4/+8  int32  position, in the engine's raw units
//       +12       int16  HEADING, 4096 per turn
//       +14       int16  the id `actor.goto_address` names
//
// The position takes the same `v * 100 / 256 / 2.54 - 1` the world cameras
// take (`rawToWorld`), and the heading the same 4096-per-turn conversion as a
// camera's roll - AREA 222's two addresses are 0 and 1035, and 1035 is 91.0
// degrees, a right angle, which is what settles the unit.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace omk {

struct Address {
    int   id = -1;
    float pos[3] = {0, 0, 0};
    float yaw = 0.0f;        // degrees, from the 4096-per-turn `+12`
    bool  valid() const { return id >= 0; }
};

// Every address an AREA chunk declares. An empty result is not an error: most
// chunks have none, and AREA 222 - the Impasse - has exactly two.
std::vector<Address> readAddresses(std::span<const std::byte> areaChunk);

}  // namespace omk
