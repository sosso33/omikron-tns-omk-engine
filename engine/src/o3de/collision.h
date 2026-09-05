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

// `meshOf`, when given, receives one entry per emitted TRIANGLE: the `.3DO`
// mesh index it came from. A scene program can MOVE a set mesh (the Impasse's
// crates fall in the tutorial), and the engine collides against each mesh's
// current matrix (`Sweep_MeshTest` moves the sweep into it) - so a soup baked
// at rest needs the index to be patched the way the render corners are.
TriangleSoup collisionSoup(std::span<const std::byte> model, SoupKind kind,
                           std::vector<int>* meshOf = nullptr);

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
// ---------------------------------------------------------- THE NARROW PHASE
//
// `Actor_Move` (0x00469580) does not only probe the floor: it SWEEPS a sphere
// along the move and stops at the first face - `Sweep_ActorMove` (0x004AD360)
// builds the swept AABB and runs `o3de_ForEachMeshInBox`, `Sweep_MeshTest`
// (0x004AD460) skips meshes flagged 0x20000000 or 0x41 and transforms the
// sweep into mesh-local space, `Sweep_MeshFaces` (0x004A9AB0) rejects faces
// by vertex outcode, and `Sweep_PolygonKernel` (0x004A9D30, 930 lines of x87)
// writes the earliest hit fraction to +136 and the surface normal to +260.
//
// The kernel is READ and deliberately not transcribed, for the reason its
// banner in readable/ gives: this tree holds no shipped fact a transcription
// could be proved against. What is here is the same algorithm SHAPE that
// tools/sim/actor.py implements - the face case continuous (the fraction at
// which the sphere's surface reaches the plane, contact point inside the
// triangle), edges and vertices by the static test at t = 0 - so the two can
// be held to the same number (`verify.py: engine: narrow phase` against
// `sim: narrow phase`). What Actor_Move does beyond that shape was read on
// 2026-09-04 and ported on 2026-09-05 (step 1b) in actor/walk.cpp: the swept
// body is the model's own sphere list (descriptor +244/+248: HO1_FN's four of
// radius 10.9 from the feet to the head), swept sphere by sphere for the
// earliest hit; the move stops ONE unit short of the contact; a contact
// already inside (d < 1) is pushed out along the clamped normal by 1 - d,
// re-swept, growing 1.1x until clear; the walking player's clamp mask is
// 0xC000C, the normal made horizontal. Still this port's and not the
// engine's: the faces swept are the STEEP soup (the engine filters by mesh
// flag 0x20000000 | 0x41, not by slope), and the mask is a constant rather
// than the actor's accumulated blocked-direction state.
struct SweepHit {
    double t = 0.0;              // fraction of the move at first contact
    double n[3] = {0, 0, 0};     // the face normal, facing the sphere
};
std::optional<SweepHit> sweepSphere(const TriangleSoup& tris, const double p0[3],
                                    const double d[3], double radius);

// `Walk_ClampNormal` (0x0046A020), transcribed. Bits 0..5 (or 16..21 when
// `high`) are +x -x +y -y +z -z; a set bit zeroes that component of the
// normal when it points that way. Renormalised; false when it collapses
// (length <= sqrt(1e-4), the engine's own test).
bool clampNormal(unsigned mask, bool high, const double n[3], double out[3]);

std::optional<GroundHit> surfaceUnder(const TriangleSoup& tris, double x,
                                      double y, double z);

}  // namespace omk
