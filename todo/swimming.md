# Swimming

Opened 2026-09-17 on the reader's order (*"swimming - occurs rarely in the
game but mandatory to finish it"*). Before this the port knew only that
ACTOR_STATE 14 is the water state (`actor/state.cpp`). This file is the reading
and the plan.

## 1. Read, 2026-09-17

### The bank: three groups of `H1AVNT` / `F1AVNT`

| group | what | entries |
|---|---|---|
| **300** | dropping IN | `H_HFL-IN`, then `MDDIVEND` -> `H_WAITIN` |
| **301** | at the SURFACE ("ON") | `H_IN_WO` (coming up), `H_WAITON` (treading), `H_SWIMON`, `H_SWIM2` (fast), `MDDIVBEG` -> `H_WO-IN` (diving), `MDDIVEND` -> `H_SWIMIN`, `MDACTION` |
| **302** | UNDERWATER ("IN") | `H_WAITIN`, `H_SWIMIN`, `MDACTION` |

Out of the water: `MDSW2SD` and `RSTAVNT` go to `H_STAND`.

### How he gets in: a mesh under his feet

`Actor_ApplyMotion` (0x004672D0), in ACTOR_STATE 1, when the ground probe's
mesh carries flag **0x8000000** (`u8(mesh, 3) & 8`): bank **group 300**,
**control scheme 1**, **ACTOR_STATE 11**, `+1304`/`+280`/`+284` cleared, and
**camera 21** on him over 50 frames. `sub_465390` (table-called) is the same
entry. **The decompiled comment calls this "a ladder mesh", and the port's
enum names state 11 `Ladder11`; both are the WATER** - 0x8000000 is the mesh
flag CLAUDE.md 4 knows as the shimmer, the oscillating vertex colour of a water
surface.

Scripts also drive the groups directly: `player.move 300/301/302` in AREA 41,
AREA 76, SCENE 1, SCENE 2 and SCENE 62.

### The special moves (read from the raw image)

| move | handler | does |
|---|---|---|
| `MDDIVEND` | 0x0046BEF0 | ACTOR_STATE 14; `Game_RaiseEvent(43, {22, ...})` - **message 22** |
| `MDSW2SD` | 0x0046BF20 | ACTOR_STATE 1; if a camera is up, `Camera_Request(0)` on him over 50 frames |
| `RSTAVNT` | 0x0046C120 | ACTOR_STATE 1, pitch `+416` = 0, `sub_45BFF0(0)` |
| `RSTNAGE` | 0x0046C150 | ACTOR_STATE 14, pitch `+416` = 0 |
| `MDDIVBEG` | 0x0046C180 | `+1288 |= 2` - the DIVE flag (the handler before it sets bit 1) |

### The motion: `sub_4A8F30`, for states 11..14

`Actor_ApplyMotion` sends states 11..14 here INSTEAD of gravity and the ground
probe.

* **11 / 12** - `sub_4A9470(actor, 0)`: the frame's move, vertical included,
  through `Actor_Move` (the clip carries him in).
* **13, at the surface** - probe at the node: over a mesh flagged
  **0x20000000** (the water SURFACE) more than 11.81 below it, rise to 11.81
  under it; not diving (`+1288 & 2` clear), ray up to the surface and hold
  there. Then `sub_4A9470(actor, 3)`.
* **14, underwater** - probe at the node:
  * over the surface mesh (0x20000000) with clearance: stay under;
  * over a 0x8000000 mesh: drift UP `0.15*dt` and turn the pitch `+0.2*dt`;
  * the pitch is held in 200..340 degrees - under 180 it is reset to 270
    (level: `0x43870000`), under 200 raised to 200 (`0x43480000`), over 340
    lowered to 340 (`0x43AA0000`);
  * **THE BREATH**: a 40 000 ms timer from the first underwater tick
    (`Hud_Refresh` then), drawn as `Hud_DrawBar(remaining * 1000 / 40000,
    1000, 0, 1)` - **mode 1**, the horizontal gauge the port never needed -
    and when it runs out, **message 12** once (drowning);
  * out of the water region: group 301, ACTOR_STATE 13, pitch 0, the dive flag
    cleared, the timers reset, **message 21** (surfaced).

`sub_4A9470(actor, mode)` is the collide: undo the frame's move and hand the
3D delta to `Actor_Move` with `mode` (0 underwater, 3 at the surface).

### Where the water is, settled by the loader (Jaunpur, 2026-09-17)

`build/mesh_list` on `jaunpur.3DO` reverses the first guess: **0x8000000 is the
canal's BED and BANKS** (`Fondcanal1..8`, `BergeEsc..`, `bassin03/04`, the buoys
`BOUI..`, the levers `J_uwlevier..`), and **0x20000000 is the SURFACE**
(`Eau`, `Eau05`, `eaubass`, `eaubassin2..4`, flags `0x20003004` - additive
transparent). The quay stands at y ~-120, the surface at ~0..4, the bed at ~120
(Y down).

### Where the water is (the first guess, kept for the record)

Meshes flagged 0x20000000 in `MESHES/DECORS` include `A_eau` (Anekbah),
`Eau11`.. (Lahoreh), `lac01..03` (Jangir), `eaubassin2/3` (Jaunpur),
`MAwater..` (Mayerem), `KHwater10` (Khonsu), `CSeau1..4` (the sewer vents),
`PAwater`, `JTwarter`. **A lead, not a result**: this was a quick parse (name
at record +16), several names came out as garbage, and 0x20000000 also marks
non-water (`fume07` - smoke, the flag the camera sees through, CLAUDE.md 1).
The loader decides the offsets before anything is built on them.

## 3. Step 1, done — and why he could walk on water

The entry itself was a few lines. What it needed first were two rules of
`Walk_GroundResponse` the port had never had, both about 0x20000000:

* **the step refuses it** whatever its height (`21_d3d.c` 2644, the third arm
  `docs/ASSETS.md` 4 read and deliberately left: porting it faithfully meant
  carrying mesh flags per TRIANGLE into the step, which is what
  `Walker::setFloorFlags` now does - the viewer builds the table beside
  `playerSoup` through each slot's `soupMesh`);
* **a falling body does not land on it** (`21_d3d.c` 2548, `if (v68 <= 0.0 &&
  (mesh & 0x20000000) == 0)`): the landing probe skips flagged triangles and
  probes again from under them.

Before them the port's walker stood on the canal's surface and walked across
it at y 3.4. After them a walk off the ledge at (10524, -40, 10284) falls 3.6 m
THROUGH the surface onto the bed, and the bed's 0x8000000 takes him in: group
300, scheme 1, ACTOR_STATE 11, camera 21 - and the channel plays through
`H_HFL-IN` and `MDDIVEND` to `H_WAITIN`. Other walks off the same ledge land on
an unflagged floor at y 3.4 (triangle 11739, flags 0) - the engine would too.

`verify.py: engine: water entry`, shown to fail by dropping the pass-through
(he lands on the water, no entry). Twelve walker, landing and actor checks
green, `engine: actor states` among them after the rename of state 11.

## 2. The steps

| step | what | state |
|---|---|---|
| 0 | this reading | **done 2026-09-17** |
| 1 | the entry: the 0x8000000 mesh under his feet in state 1 -> group 300, scheme 1, state 11, camera 21; rename state 11 | **done 2026-09-17** - §3 |
| 2 | the water moves: `MDDIVEND` (14, message 22), `MDSW2SD` (1, camera 0), `RSTAVNT`, `RSTNAGE`, `MDDIVBEG` | |
| 3 | the motion, 11..14: no gravity and no ground snap; the surface hold; the pitch | |
| 4 | the breath: the 40 s timer, `Hud_DrawBar` mode 1, message 12 | |
| 5 | a place to swim headlessly, and the check | |
| 6 | play | |
