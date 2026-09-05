# Collision, scene functions, transitions — the plan (2026-09-04)

Three slices the reader asked for in one go ("do 1 to 3"), ranked from
`todo/omk-play.md` as the most severe and most complex open items. Worked one
step at a time on branch `take-height`: each step ends in a commit and a
report, then waits for the go. The reader is away from the screen, so every
step is proved by numbers and checks, never by eye; what still needs an eye is
listed under each.

| step | what | status |
|---|---|---|
| 1 | **the collision sweep** — `Actor_Move`'s swept sphere (`Sweep_ActorMove` 0x004AD360 → `Sweep_MeshTest` → `Sweep_PolygonKernel` 0x004A9D30, 930 lines) so walls, railings and closed doors stop the walker; issues 68/75 and HANDOFF "the walker does not block on walls" | **done** — 1 (the sweep in the sim's shape) and 1b (the capsule, the stand-off, the push-out, the mask; the drop guard retired); walls confirmed by the reader's play, railings unseen |
| 2 | **the scene functions** — the eleven of seventeen `.SCX` object functions the port does nothing for (issue 71), starting with `Script_StopSound` (61 scenes, the audio that never stops) and `Script_Display3DSprite` (65 scenes) | pending |
| 3 | **the area transitions** — the two-slot decor split that leaves the player on an unlinked set (issue 70), and the tunnel's early unload / which of two overlapping zones fires (issue 75) | **in progress** (taken before step 2: the reader walked into the void from the Impasse airlock on 2026-09-05) |

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

**Step 1b (2026-09-05): the rest of `Actor_Move`.** The body is the model's
own sphere list hung off the feet (pelvis.y = feet.y − camLift), swept sphere
by sphere for the earliest hit; a hit at distance d ≥ 1 moves d − 1 (the
stand-off), d < 1 pushes out along the clamped normal by 1 − d, re-swept and
grown 1.1× until clear; the clamp mask is the walking player's 0xC000C (the
normal made horizontal); the returned displacement is the sum of the passes'
advances plus the last remainder, as the engine adds `a4·dir + a3·normal`
each pass. `kMaxUnsweptDrop` is retired: a drop is a fall, as
`Walk_GroundResponse` has it. The numbers moved as the engine's rules say:
ARESTO14's wall now stops the body at 13.0 (the sphere's 12 plus the
stand-off; the sim, with no stand-off, at 16.6), AHALL27 east blocks at
x 4817, and the ledge census descends 318/14/126 where the guard had held
315/6/48, 0 stranded. A hit whose only remainder is the push-out's jitter
under 0.05 is reported as `Blocked`, a verdict of the port's.

**Still not the engine's, listed in collision.h:** the mesh-flag filter (the
steep soup stands in) and the accumulated blocked-direction mask. The CLOSED
DOOR of issue 75 is a scene OBJECT, whose faces are not in the decor soup at
all; that is step 3's, with the transition it guards.

## Step 3 — the transitions

**The case that opened it (2026-09-05).** The reader: alley → airlock (AREA
142, no scene of its own, three doorless zones along a corridor: 2319 at the
alley end runs `area.goto 222, -1, -1`, 2329 mid, 2320 at the Anekbah end runs
`area.goto 0, -1, -1`) → the Anekbah transition fired at frame 2221 with his
feet still on 142, replacing the alley (slot 1) with Anekbah → he then walked
"outside the alley, in the void", and quit at 7717, −78.7, 3394, inside zone
2320 on the airlock's floor, never having stood on Anekbah's decor. Which zone
armed and what each goto did was not in that log; the transition and zone
logs are unconditional from `2183fbc` on, and the walker prints where a step
finds no floor.

**What issue 70 had already established** (todo/omk-play.md): both ends of
the Anekbah–Qalisar tunnel carry an overlapping pair of zones, a doorless
outer one and a door-carrying inner one; the doorless fires first, the
door-carrying goto is refused while the first transition is in flight and
retried, and when it finally runs its door objects cannot resolve because
the port keeps ONE scene runner where the engine keeps one object pool PER
RESIDENT SLOT (`Area_LoadScx` fills `slot+8`). 93% of the 894 door objects
resolve in the OUTGOING scene. The slice is a `SceneRunner` per slot, both
ticked, with the departure object resolved against the outgoing slot first.

**Sub-steps, in order:**

| | what | status |
|---|---|---|
| 3a | read the airlock case from a logged walk | **done** — it was neither the zone order nor the hide timing but a RECONSTRUCTION: `hideOutgoing` forced `playerOnArea(dest)` before hiding "the non-active slot", so an arrive that ran DURING the load (state 3 → 4) ended by flipping the row to the unloaded destination and hiding the airlock under his feet; the engine's arm (`Area_Transition` mode 3 case 4 / mode 4 case 9 / mode 1) hides the non-active slot and nothing else. Gated on `playerWalks_` (kept for walker-less runs); `engine: airlock walk` reproduces the reader's route headless. Confirmed by the reader: the void did not come back |
| 3b | a `SceneRunner` per resident slot; `startTransitionObject` resolves against the outgoing slot, then the destination's | **already there** — `sceneOut_`, the outgoing pool, and `engine: tunnel doors` assert it (a session before this one) |
| 3c | scene objects' collision in the sweep (`o3de_ForEachMeshInBox` walks every linked mesh; the port sweeps the decor soup only) — the tunnel's doors are scene objects; the airlock has none | pending |
| 3d | a check that walks a transition headless and asserts the door resolves and plays, and the outgoing set survives until the feet are on the destination | partly: `engine: airlock walk` (doorless); the door-carrying tunnel walk is still headless-untested with a walker |
| 3f | **the restaurant's void** (2026-09-05): leaving ARESTO the reader fell for ever from (6717, 0, 593). Read: not the transition — the doors resolved (handle 10 = `Porte26open`, 11 = `Porte26close`, resto.SCX's objects 0/1) and the completion hid Anekbah correctly, since his feet never reached its decor. He stepped off the dining floor's south edge (a 31.5 drop onto a sunken ledge, which the headless walk lands) and the port carried the WALK's horizontal delta through the air, drifting him off the ledge over a hole in the geometry. `Actor_ApplyMotion` moves the actor by +216/+224 × dt, velocity fields the fall state never writes (issue 68: "falling mainly on a single axis"); the walker's airborne step no longer applies the walk's delta. **Done.** Still open there: the 24-unit floorless gap under the doorway itself between the two sets (x 6647..6671), which nobody has walked yet |
| 3e | the CRATE CORNER: the reader was held between the alley's wall and the moved crates at the exit (7799, −78.7, 2895: "NO FLOOR ahead, 1 wall pass hit, ground under him" every frame). The slide's remainder points off the floor's edge and the probe reverts it, so nothing moves; the engine's accumulated blocked-direction mask (the last unported part of `Actor_Move`) is the candidate. The viewer prints `walker: STUCK` with the position when it happens | pending |
