// SPDX-License-Identifier: GPL-3.0-or-later
// MAP2D\<AREA +106>.MPT - the 2D map, AND the shoot AI's NAVIGATION GRID.
//
// `Map2D_Load` (0x00434E30) loads it when an area names one at `+106`; 16 of
// the shipped areas do. `docs/FILE_FORMATS.md` 5b5 decoded the container as
// the in-game map screen and that is at most half of what it is: every
// function `Shoot_Think` (0x00420AB0) walks reads the globals this loader
// fills, and the AI moves on these cells (`todo/shoot-mode.md`).
//
//     u32 scale                     the CELL SIZE in world units: 39, 78 or
//                                   117 - and the world unit is an INCH, so a
//                                   cell is 1, 2 or 3 METRES
//     u32 floorCount
//     per floor:  u32 nSegs + nSegs x { u32 kind, f32[6] }      wall segments
//                 f32[6] bound + u32 W + u32 H + W*H byte cells
//     per floor:  u32 k + k x [u32 len][(len+2) dwords]         waypoints
//     per floor:  16 x 12 bytes                                 the DOOR table
//
// **`bound` is a box, not an origin**: `[minX, maxX, minY, maxY, minZ, maxZ]`,
// and `W*scale` matches `maxX-minX` (and `H*scale` `maxZ-minZ`) to within one
// cell in all 79 shipped floors - which is what establishes the field order,
// since the loader itself only ever touches +0, +12, +16, +24 and +28.
//
// WHICH FLOOR an actor is on, `sub_435020(x, y, z, exclude)`: the cell must be
// inside `W`/`H`, `y - bound[3] <= 0` (Y points DOWN, so that is "at or above
// the floor's lower edge"), and of those the GREATEST such y wins - the lowest
// floor still above him. `exclude` skips one, which is how a caller asks for
// "any floor but the one I am on".
//
// WORLD -> CELL, `sub_435770`: `((z - minZ) / scale) << 16 | ((x - minX) /
// scale)`, so a cell is packed into one int, x in the low half.
//
// THE CELL BYTE. `sub_4353E0` is the AI's own test and it refuses exactly
// four values - **0, 2, 3 and 0x80** - plus anything outside `1 <= c < W`,
// `1 <= r < H` (note the low margin of ONE, not zero). Everything else is
// passable. The shipped values are 0 (46840), 1 (22698), 2 (4556), 8 (36),
// 0x10..0x17 (142), 0xCD (715); 3 never occurs and 0x80 never appears on disk
// because it is written at RUNTIME:
//
//   * **0x80 is an ACTOR**, not terrain. Every mover saves the cell it steps
//     on into its own record `+189` and stamps 128 over it (`sub_435970(node,
//     x, z, 128)`), and puts the byte back when it leaves (`sub_420C10`). So
//     the grid is the engine's occupancy map and "blocked" means "wall or
//     somebody standing there" - which is why the same refusal set appears in
//     the mover (05_sys.c 5599) and in the think step.
//   * **0x10 is a DOOR**, and its low nibble is a slot: `sub_435270`'s default
//     arm indexes `doorTable[16 * floor + (c & 0xF)]`, whose 12 bytes are
//     `{ int arm, int openObject, int closeObject }` - SCENE OBJECT handles,
//     started through `ScriptObject_Start` (24_sys.c 4569) - and asks the
//     scene for that object's state before letting the cell be crossed. The
//     table is 0xFF on disk and filled at runtime, which is why it reads as
//     scratch.
//
// What 8 and 0xCD mean is NOT established: no branch anywhere distinguishes
// them from 1, so to the AI they are floor. They are carried through here
// rather than folded into "walkable", because a value nothing reads is a
// finding waiting to happen and flattening it would destroy the evidence.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace omk {

struct Map2dSegment {
    std::uint32_t kind = 0;
    float v[6] = {0, 0, 0, 0, 0, 0};
};

// One of the k lists per floor. The engine walks them by squared distance from
// a cell (`v7[2] & 2` skips some), so the two bytes at +12/+13 are a cell
// coordinate; the rest is unread and is kept whole.
struct Map2dWaypoint {
    std::uint32_t len = 0;
    std::vector<std::uint32_t> body;        // len + 2 dwords
    int cellX() const { return body.size() > 2 ? static_cast<int>((body[2] >> 0) & 0xFF) : -1; }
    int cellZ() const { return body.size() > 2 ? static_cast<int>((body[2] >> 8) & 0xFF) : -1; }
};

// 12 bytes, 16 per floor: the door slot a `0x10 | n` cell names.
struct Map2dDoor {
    std::int32_t arm = 0, openObject = -1, closeObject = -1;
    bool used() const { return openObject != -1 && closeObject != -1; }
};

struct Map2dFloor {
    std::vector<Map2dSegment> segments;
    float bound[6] = {0, 0, 0, 0, 0, 0};    // minX maxX minY maxY minZ maxZ
    std::uint32_t w = 0, h = 0;
    std::vector<std::uint8_t> cells;        // w*h, row-major, row stride w
    std::vector<Map2dWaypoint> waypoints;
    Map2dDoor doors[16];

    std::uint8_t cell(int x, int z) const {
        if (x < 0 || z < 0 || static_cast<std::uint32_t>(x) >= w ||
            static_cast<std::uint32_t>(z) >= h) return 0;
        return cells[static_cast<std::size_t>(z) * w + static_cast<std::size_t>(x)];
    }
};

class Map2d {
public:
    bool load(std::span<const std::byte> raw);
    bool loadFile(const std::string& path);
    bool valid() const { return valid_; }

    std::uint32_t scale() const { return scale_; }
    const std::vector<Map2dFloor>& floors() const { return floors_; }

    // `sub_435020`. -> the floor index, or -1. `exclude` skips one floor.
    int floorAt(float x, float y, float z, int exclude = -1) const;
    // `sub_435770`, split into its two halves rather than packed.
    bool cellAt(int floor, float x, float z, int& cx, int& cz) const;
    // `sub_4353E0`: true when the AI refuses the cell. `occupied` stands in
    // for the runtime 0x80 a caller may have stamped itself.
    bool blocked(int floor, int cx, int cz) const;

    // The four values the test refuses, exported so a check can assert the
    // set rather than re-list it.
    static bool blockedValue(std::uint8_t v) {
        return v == 0 || v == 2 || v == 3 || v == 0x80;
    }
    static constexpr std::uint8_t kOccupied = 0x80;
    static constexpr std::uint8_t kDoorBit  = 0x10;

    // ---- the LINE OF SIGHT (`todo/shoot-mode.md` 5c) -------------------
    //
    // `sub_435210`, the predicate the three shipped `sub_4359A0` call sites
    // all select.  It is NOT the movement test above: only a true wall (0)
    // and a CLOSED DOOR stop it, so cells 2 and 3 - which `blockedValue`
    // refuses - are things you cannot walk on but can see and shoot across.
    //
    // `doorOpenMask` is one bit per door slot, standing in for the engine's
    // `sub_44A0F0(scene, door+4, door+8) == 16` on the object pair in
    // `dword_907EB4`, which needs a live scene the reader has not got.
    //
    // NOTE the `case 128:` the engine SOURCE carries here is dead: the walk
    // reads the cell with `movsx`, so a stamped 0x80 arrives as 0xFFFFFF80
    // and the predicate's `cmp eax, 80h` / `ja` (UNSIGNED above) sends it to
    // the default arm, where `0x80 & 0x10 == 0` reads clear.  An occupied
    // cell does not block sight.  `sub_4353E0` is the one that biases
    // (`add eax, 80h`) and so really does see it - hence `blockedValue`
    // listing 0x80 and this not.
    static bool sightBlockedValue(std::uint8_t v, std::uint16_t doorOpenMask) {
        if (v == 0) return true;                       // case 0
        if (v == 1 || v == 2 || v == 3) return false;  // cases 1-3
        if ((v & kDoorBit) == 0) return false;          // default, no door bit
        return (doorOpenMask >> (v & 0x0F) & 1) == 0;   // the door, if shut
    }

    // `sub_4359A0` with its fourth argument 1 - the only form the shipped
    // build uses.  True when the segment from (x0,z0) to (x1,z1) reaches
    // clear; the first refusing cell is left in `blockX`/`blockZ`.  The
    // engine's own return value is tri-state, but its `2` arm is dead for
    // the same reason as the `case 128:` above, so this is a bool.
    bool lineOfSight(int floor, int x0, int z0, int x1, int z1,
                     std::uint16_t doorOpenMask = 0xFFFF,
                     int* blockX = nullptr, int* blockZ = nullptr) const;

private:
    bool valid_ = false;
    std::uint32_t scale_ = 0;
    std::vector<Map2dFloor> floors_;
};

}  // namespace omk
