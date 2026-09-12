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
//     per floor:  u32 nLinks + nLinks x { u32 destFloor, f32 from[3], f32 to[3] }
//                                                           INTER-FLOOR LINKS
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

// ONE INTER-FLOOR LINK - a staircase, a ramp, a ladder (`todo/shoot-navedge.md`).
// The loader reads these per floor and this file called them "wall segments"
// with every field but the count unread until 2026-09-12; `sub_436BB0` and
// `sub_424DE0`'s state 2 between them name all seven dwords:
//
//     u32 destFloor    +0    the floor this link ARRIVES on
//     f32 from[3]      +4    where you step ON, on this floor
//     f32 to[3]       +16    where you step OFF, on destFloor
//
// **The data settles it, 36 of 36**: every `destFloor` is a real floor of its
// own map, every `from` lies inside the SOURCE floor's bounds and every `to`
// inside the DESTINATION's - and they come in RECIPROCAL PAIRS, `0 -> 1 from A
// to B` always beside `1 -> 0 from B to A`, which is a stair described from
// both ends and is not something a wall would do. Only five of the sixteen maps
// carry any (`bar56` 5 pairs, `tetradou` 3, `hames` 4, `tetra3` 4, `tetra2` 1),
// which is why there are 36 records across 79 floors - a count that never made
// sense for walls.
struct Map2dLink {
    std::uint32_t destFloor = 0;           // +0
    float from[3] = {0, 0, 0};             // +4    on this floor
    float to[3]   = {0, 0, 0};             // +16   on destFloor
};

// ONE PATROL ROUTE (`todo/shoot-patrol.md`). The three header dwords are fixed
// by the loader's own walk, `v9 += *v9 + 3`, and the readers name the rest:
// `sub_4354E0` compares `u8i(v7, 12)` and `u8i(v7, 13)` - the first point's two
// cell bytes - and tests `v7[2] & 2`; `sub_4356B0` turns point `i` into a world
// point and returns `i16i(v6, 1)`, the int16 in its upper half.
//
//     u32 len        how many points
//     u32 id         what `shoot.actor.action 1`'s third operand names
//     u32 flags      see below
//     len x { u8 cellX, u8 cellZ, i16 clipId }
//
// **The flags, and the shipped data settles the first one.** Bit `0x1` is
// PING-PONG: `sub_435660` walks a plain route forward and wraps to 0 at the
// end, but a `0x1` route turns round instead, raising `0x4` while it comes
// back. **Both of the two `0x1` routes have exactly TWO points** (`hames`
// floor 3 id 6 and `soukdock` floor 0 id 7, each a straight line), where the
// other 51 are closed rings of 4 to 27 cells - and a two-point route that
// wrapped would be degenerate, while one that turns round is a sentry walking
// up and back. Bit `0x2` is not authored at all: it is the RESERVATION a
// walker takes (`sub_4356B0` sets it, `sub_435650` clears it), which is why
// it lives in `Map2d`'s runtime overlay below and not in this struct.
struct Map2dRoutePoint {
    std::uint8_t cellX = 0, cellZ = 0;
    std::int16_t clipId = 0;      // `sub_435750`; 0 in all 53 shipped routes
};
struct Map2dWaypoint {
    std::uint32_t len = 0;
    std::vector<std::uint32_t> body;        // len + 2 dwords, kept whole
    std::uint32_t id = 0;                   // body[0]
    std::uint32_t flags = 0;                // body[1]
    std::vector<Map2dRoutePoint> points;    // body[2..]
    bool pingPong() const { return (flags & 0x1u) != 0; }
    int cellX() const { return points.empty() ? -1 : points[0].cellX; }
    int cellZ() const { return points.empty() ? -1 : points[0].cellZ; }
};

// 12 bytes, 16 per floor: the door slot a `0x10 | n` cell names.
struct Map2dDoor {
    std::int32_t arm = 0, openObject = -1, closeObject = -1;
    bool used() const { return openObject != -1 && closeObject != -1; }
};

struct Map2dFloor {
    std::vector<Map2dLink> links;          // the floor's EXITS, see above
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
    // THE RUNTIME STAMP: the engine writes its own loaded grid - `sub_435970
    // (floor, x, z, byte)`, 15 callers - most of them the 0x80 a gunman puts
    // in his cell after he moves and the byte (record +189) put back before
    // he thinks. Out of range is ignored.
    void setCell(int floor, int x, int z, std::uint8_t v) {
        if (floor < 0 || floor >= static_cast<int>(floors_.size())) return;
        Map2dFloor& f = floors_[static_cast<std::size_t>(floor)];
        if (x < 0 || z < 0 || static_cast<std::uint32_t>(x) >= f.w ||
            static_cast<std::uint32_t>(z) >= f.h) return;
        f.cells[static_cast<std::size_t>(z) * f.w + static_cast<std::size_t>(x)] = v;
    }

    // ---- THE PATROL ROUTES (`todo/shoot-patrol.md`) ---------------------
    //
    // The reservation bit `0x2` is RUNTIME: the engine sets and clears it in
    // the loaded words themselves, so these mutate `Map2dWaypoint::flags`.
    // `body` keeps the file's dwords unchanged beside it, so the two can be
    // compared and a check can assert what shipped.
    //
    // `sub_4354E0(floor, cellX, cellZ, id)`. `id` 0 (the engine's `a4 <= 0`)
    // takes the NEAREST route whose first point is closest to the cell in
    // squared distance and whose `0x2` is clear; `id` > 0 takes the one with
    // that id, refusing it if `0x2` is up. NOTE the engine casts the operand
    // to `uint8_t` first, and the caller here must do the same - the shipped
    // operands carry the FLOOR in their high byte (`todo/shoot-patrol.md` §2).
    // -> an index into the floor's `waypoints`, or -1.
    int routeFor(int floor, int cellX, int cellZ, int id) const;
    // `sub_435660(route, index)`: the next point. A plain route wraps to 0 at
    // the end; a PING-PONG one (`0x1`) turns round instead, raising `0x4` on
    // the way back and clearing it at the start. Mutates that bit.
    int routeNextIndex(int floor, int route, int index);
    // `sub_4356B0(route, floor, index, out)`: point `index` as a WORLD point -
    // the cell's centre, `(cell + 0.5) * scale + bound` - and the route MARKED
    // taken (`|= 2`). -> the point's `clipId`, which sends the walker into
    // state 5 when it is not 0; -1 for a bad index.
    int routePoint(int floor, int route, int index, float& x, float& z);
    // `sub_435750(route, index)`: the same `clipId` without taking the route.
    int routeClipId(int floor, int route, int index) const;
    // `sub_435650(route)`: the reservation cleared. Five call sites, all of
    // them a walker giving up a patrol (`todo/shoot-patrol.md` §3).
    void routeRelease(int floor, int route);
    // Whether a route is currently reserved - for a probe, not the engine.
    bool routeTaken(int floor, int route) const;

    // ---- THE INTER-FLOOR LINKS (`todo/shoot-navedge.md`) -----------------
    //
    // `sub_436BB0(myFloor, destFloor, pos)`: my floor's list, filtered to the
    // links that ARRIVE on `destFloor`, and of those the one whose `from` is
    // nearest `pos` in SQUARED XZ distance - the y is not in it. Strictly
    // less-than, so the earliest of equals wins, as the engine's does.
    // -> an index into that floor's `links`, or -1 for none.
    //
    // This is the whole of shoot mode's "path-finding": ONE HOP. A gunman whose
    // floor has no link straight to the player's gets nothing back, and the
    // engine then falls to `sub_421020`, which is unread. There is no chaining
    // and no search.
    //
    // The engine indexes its per-floor arrays with a SIGNED BYTE straight out
    // of the record's `+188` and would read off the front of them for -1; this
    // refuses instead.
    int linkTo(int floor, int destFloor, const float pos[3]) const;

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
