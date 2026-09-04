# Collision, scene functions, transitions — the plan (2026-09-04)

Three slices the reader asked for in one go ("do 1 to 3"), ranked from
`todo/omk-play.md` as the most severe and most complex open items. Worked one
step at a time on branch `take-height`: each step ends in a commit and a
report, then waits for the go. The reader is away from the screen, so every
step is proved by numbers and checks, never by eye; what still needs an eye is
listed under each.

| step | what | status |
|---|---|---|
| 1 | **the collision sweep** — `Actor_Move`'s swept sphere (`Sweep_ActorMove` 0x004AD360 → `Sweep_MeshTest` → `Sweep_PolygonKernel` 0x004A9D30, 930 lines) so walls, railings and closed doors stop the walker; issues 68/75 and HANDOFF "the walker does not block on walls" | **done, first pass** (commit below); unseen |
| 2 | **the scene functions** — the eleven of seventeen `.SCX` object functions the port does nothing for (issue 71), starting with `Script_StopSound` (61 scenes, the audio that never stops) and `Script_Display3DSprite` (65 scenes) | pending |
| 3 | **the area transitions** — the two-slot decor split that leaves the player on an unlinked set (issue 70), and the tunnel's early unload / which of two overlapping zones fires (issue 75) | pending |

## Step 1 — the collision sweep

**Read.** `Actor_Move` (0x00469580, 21_d3d.c) is a collide-and-slide of up to
three passes. Each pass fills the request block at `unk_6A5100` — start
(+112), end = start + dir × (len + 2) (+124), unit direction (+308), the mesh
flag mask (+0x160, `0xC000C` for the walking player = the y bits) — and for
mode `a9 == 1` a vertical CAPSULE from the model's own sphere list: radius
+192 = max r × `dword_910358` (1.0), bottom +376 = min(y − r), top +380 =
max(y + r). `Sweep_ActorMove` builds the AABB and walks
`o3de_ForEachMeshInBox`; `Sweep_MeshTest` skips meshes flagged `0x20000000`
or `0x41`, moves the sweep into mesh space and records the sign of
up·dir at +432; `Sweep_MeshFaces` rejects faces by vertex outcode;
`Sweep_PolygonKernel` writes the earliest fraction to +136 and the normal to
+260. Back in `Actor_Move`: the normal → world → `Walk_ClampNormal(mask)`
(zeroes the masked components, renormalises); a hit at t ≥ 1 moves `t − 1`
(ONE UNIT SHORT of the contact); t < 1 is already touching: the move is 0 and
the actor is PUSHED OUT along the normal by `1 − t`, re-sweeping through
`sub_4AD6F0` and growing 1.1× until clear; the remainder is projected onto
the wall plane (`−(n·dir)` clamped ≥ 0) and the next pass runs; a remaining
length ≤ 1e-4 is zero. The out-params return the summed push-out.

**The sphere list is the MODEL's, not the meshes'.** `*(node+40)` is the
model, `*model` its descriptor (file + 44), count at +244, 16-byte
`(x, y, z, r)` records from +248. HO1_FN: four spheres of r 10.9 stacked
from y −18.1 to 30.9 — a 28 cm capsule. The port's `collisionSpheresOf`
(the crowd push's per-mesh radii) tops out at 42.5, which as a sweep radius
would jam him in every doorway; `modelSweepSpheres` reads the engine's list.

**Ported, in the simulator's shape.** The kernel's own banner says it is
read and not transcribed, tools/sim implementing the same algorithm shape
instead. So `o3de/collision.cpp` carries `sweepSphere` (the sim's `sweep`:
face case continuous, edges/vertices by the static test at t = 0) and
`clampNormal` (a transcription), `Walker::slide` runs the three passes with
mask 0 as the sim does, and `Walker::step` calls it before the ground probe;
a move the sweep cancels is `Blocked`. `engine/tools/wall_probe.cpp` is the
sim's `wall_test` in C++ and `verify.py: engine: narrow phase` holds the
port to the sim's two numbers on ARESTO14: sweep off ends 140 behind the
partition (moved), on ends 16.6 in front (blocked). Both agree to the tenth.
`--from x,y,z --dir dx,dz` walks a reported spot: AHALL27 east from 4668,
0, −970 — the HANDOFF's wall — now blocks at x 4818 (35 of 60 steps) where
it used to walk to the floor's end. The viewer binds it with the model's
radius (10.9) against both slots' steep faces (31152 in Anekbah); the
walker checks stay green.

**Not yet the engine's, and listed in collision.h:** the capsule (a sphere at
the feet for now), the one-unit stand-off, the push-out of a penetrating
contact, the accumulated blocked-direction mask, the mesh-flag filter (the
steep soup stands in — the engine filters by flag, not slope). And the
CLOSED DOOR of issue 75 is a scene OBJECT, whose faces are not in the decor
soup at all; that is step 3's, with the transition it guards.
`kMaxUnsweptDrop` stays until the capsule lands (walk.h says why).
