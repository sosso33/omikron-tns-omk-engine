// SPDX-License-Identifier: GPL-3.0-or-later
// A set's collision surfaces - the soup the walker probes against.
//
// Two differences from the render geometry o3de/geom3do.h builds, and both
// matter:
//
//   * **every** mesh is included, the CollisionOnly (0x800000) volumes too.
//     The ground probe runs on collision geometry; dropping those meshes is
//     the RENDER pass's job, not the walker's.
//   * for the walkable set, only triangles the walker could stand on are kept.
//     `Actor_Move` (0x00469580) rejects slopes past the tunable at 0x91033C -
//     shipped value **30 degrees** - so a steeper face never answers a ground
//     probe. Game Y grows downward, so a floor's normal has a negative y and
//     the test is on |ny| either way.
//
// The unwalkable complement is what the narrow phase sweeps against, and it is
// a different question: `Sweep_MeshTest` filters by MESH FLAG, not by slope.
#pragma once

#include "formats/mesh3do.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace omk {

// 30 degrees, as cos - the walker's slope limit
inline constexpr double kSlopeCos30 = 0.86602540378;

// A flat triangle list in world space: x0,y0,z0, x1,y1,z1, x2,y2,z2, ...
using TriangleSoup = std::vector<float>;

enum class SoupKind {
    Walkable,   // only faces flatter than 30 degrees - the engine's probe set
    Steep,      // the complement: every collision face STEEPER than 30 degrees.
                // Not a wall set - it is what the actor SLIDES down.
                // `Walk_GroundResponse` (0x00465460) reaches the ground it was
                // given, tests `cos(30) > -normal.y`, and on a steep face adds
                // the face normal to the horizontal velocity and writes
                // `dword_910340` (11.811) into the vertical one instead of
                // stopping. So a steep face is a surface the engine stands the
                // actor on for one frame and then slides him off; it is not a
                // hole, and a walker that cannot see it has nowhere to put a
                // player who reaches one.
    All,        // every collision face, for the narrow phase
    Render,     // what the RENDER pass draws: CollisionOnly dropped, no slope
                // test. Not what the engine probes - it is here because
                // tools/sim's stage-5 walker uses it, so porting the walker
                // against it is what makes the two comparable at all. The
                // difference is worth knowing: ARESTO14 has 2875 render
                // triangles against 1107 walkable collision ones.
};

TriangleSoup collisionSoup(std::span<const std::byte> model, SoupKind kind);

// The surface under a point, or nothing.
//
// The game's Y grows downward, so "below" means a LARGER y, and this answers
// with the nearest such surface - which is what a ground probe wants and is
// not the same question as "the room's floor" (see omkdata.ground_under: under
// a seated character the nearest surface below is the chair).
std::optional<double> floorUnder(const TriangleSoup& tris, double x, double y,
                                 double z);

// The same probe, with the face's own NORMAL - which is what the ground
// response actually decides on. `Walk_GroundResponse` is handed the normal by
// `Walk_ProbeGround` and tests `cos(30 degrees) > -normal.y`, so the normal is
// oriented here the way the engine's test reads it: Y grows downward, an
// upward-facing floor has a NEGATIVE y, and the returned normal is flipped
// where needed so that is always true. A caller wanting the slope compares
// `-n[1]` against `kSlopeCos30`, exactly as the engine does.
// Named `GroundHit` and not `Surface`: `ui/surface.h` already has an
// `omk::Surface` (the 2D pixel buffer), and the two only meet in a
// translation unit that includes both - which the core build does not and
// `backends/sdl/play.cpp` does. The core `make` was clean and `make play`
// was not.
struct GroundHit {
    double y;        // the height of the surface under the point
    double n[3];     // its unit normal, pointing up (n[1] < 0)
};
std::optional<GroundHit> surfaceUnder(const TriangleSoup& tris, double x,
                                      double y, double z);

}  // namespace omk
