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

}  // namespace omk
