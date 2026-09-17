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
| `MDDIVBEG` | 0x0046C180 | `+1288 |= 2` - the DIVE flag (the handler before it, 0x0046C170, is `MDROT000` - turning on the spot - which sets bit 1: not a water move) |

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

## 4. Step 3, done — the motion, and the drowning ran on the game's own script

`PlayerController::waterTick` transcribes `sub_4A8F30` for states 11..14: no
gravity, no ground snap, the clip's move vertical included (the horizontal wall
sweep and the floor under the water standing in for `Actor_Move`'s 3D collide),
the surface hold at 11.811 over the crown in state 13, and in state 14 the head
node's probe - pulled under by the surface, drifting up 0.15 a frame over the
bed with the pitch turning +0.2, surfacing into group 301 / state 13 / message 21
over any other floor - the pitch clamp and the 40-second breath with message 12.
`Walker` gained `probeFlags`, `surfaceAbove` and `floorThroughWater`.

**The root motion goes through the whole Euler matrix** (`rotateEuler`, the
`Matrix3x3_FromEulerAngles` the shove already used), because +288 carries the
pitch. **A first reading here was wrong and is corrected**: it blamed the
missing pitch for a sink through the bed at 58 units a second, but the sink was
the bed limit probing from the NEW position, already under the bed - fixed in
the same edit - and a mutation dropping the pitch rotation left the check
green. `H_WAITIN`'s root barely moves, so nothing measured yet shows the pitch
rotation matters; it stands on the matrix alone.

**Measured in the canal walk-in, 1500 frames**: in at frame 150, `MDDIVEND`
and message 22, the drift from y 123 to ~68 by frame 540 with the pitch at 340,
the breath from 40 000 ms down, and at frame 1371 - 40 s under - **message 12,
which AREA 1 answers**: `player.move.wait 162` (`H_ACIDE`, drowning),
`actor.goto_address 65` to the bank at (12202, 3, 5959), `player.move 303`
(`H_WO_SD`) and the voice-over *ZVO P574 Noyade Comment*. `Vie` ends at 5.

**Open, labelled**:
* After the rescue he falls back IN: address 65 is at y 3, the surface's
  height, and back in state 1 the walker's pass-through drops a body placed on
  the surface to the bed. Whether the engine keeps a PLACED body on a
  0x20000000 mesh is unread - the pass-through read is the descent's.
* State 13 (the surface) has not been reached by a run: nothing here surfaces
  him - the probe over "any other floor" is what does, and the canal's walk-in
  never finds one under his head.
* The head node's point is the rest pose's, not the posed node.
* ~~The pitch is not yet applied to the DRAWN body~~ - **done, 3b** below.

## 5. Step 3b, done — the pitch on the drawn body

The body was turned by the yaw alone, and the engine turns it by **actor+288**
- `Matrix3x3_FromEulerAngles(+416, +420, +424)`, rebuilt every frame by
`Actors_TickAll` and written into the node's `+156` by `Actor_LoadModel`. So the
corners, the head point, every mesh's world position AND its world rotation, and
the `.CTL`'s bone-attached sprites, now go through `rotateEuler` with the whole
Euler - the yaw still overridden while he boards a slider, which is that arm's
own matrix. The swim pitch reaches the drawn body, and so does the shove's lean
in a shoot phase, which had the same gap.

Measured in the canal walk-in: the drawn head stands **6** units over the pelvis
at pitch 272 (lying along the water) and **23** at the 340 cap, treading. The
frame at 540 draws him upright just under the surface. Which way up he lies at
270 is not settled here - the camera is inside the bed that early - and goes to
the play test.

`verify.py: engine: water entry` carries both, shown to fail by turning the
drawn body by the yaw alone (the head stands ~24 over the pelvis on every line).

**`engine: shoot hit` is RED and was red before this task** - at `5e3a471`, the
merge that opened the session, with the gunmen's roots ~90 units further along
their walk and a second gunman killed. Not the drawn body: it fails with the
Euler and with the yaw alone alike. Recorded here, unattributed.

## 6. Step 4, done — the breath gauge, `Hud_DrawBar` mode 1

Mode 1 was the one arm of `Hud_DrawBar` the port had never needed, and the
water is what asks for it: `sub_4A8F30` calls
`Hud_DrawBar(1000 * (start + 40000 - now) / 40000, 1000, 0, 1)` every
underwater tick until the forty seconds are spent, having called `Hud_Refresh`
on the first. Transcribed whole into `HudBar::breath` (`ui/hudbar.h` carries the
reading): a HORIZONTAL bar - diamonds of radius 17 at (100, 24) and (540, 24)
and the column between them in black on layer 2; two `jaugeg.bmp` blits on
layer 3, the full part reading the SCROLLING column and the empty part source
row 0..3; then on layer 4 the additive `0x103080` - a water blue - over the full
part and `0xE0E0E0` darkening the empty one. No sparks, and the vertical centre
comes from the engine's own `Hud_ScaleX(24)`, not ScaleY - kept.

Drawn in the canal walk-in: 99% down as the breath goes, 5 quads and 2 blits a
frame, and the rendered frame shows the bar across the top of the picture with
the blue filling from the left. The gauge is in the viewer's GPU-present list,
the gap the fight's gauges fell into.

`verify.py: engine: water entry` carries it, shown to fail by leaving mode 1
returning without drawing (0 quads), as it did before this step.

## 7. Step 5, done — he SWIMS, and the key is the dive

The first run that pressed forward under water found nothing: he floated in
`H_WAITIN` for seven hundred frames whatever the arrow did. Group 302's own
graph says why - `H_WAITIN` -> `H_SWIMIN` matches input **0x20**, and in control
scheme **1** (`Nager`, which the entry installs) 0x20 is slot 5, **`Plonger`**,
keyboard 157. Underwater the DIVE key is what swims; `Avancer` (0x4, the up
arrow) has no edge there at all. At the surface it is the other way round -
group 301 takes 0x4 into `H_SWIMON` and 0x800 (`Crawl`, keyboard 54) into
`H_SWIM2`.

Held from frame 600 in the canal, he plays `H_SWIMIN` and crosses ~230 units
across the bed - 10251,123,10373 to 10072,69,10526 - lying flat, the drawn head
within 6 of the pelvis, until the canal's far wall stops the sweep. The check's
one run now carries the whole of it: the float and its drift, then the swim.

**Still not reached by any run**: ACTOR_STATE 13, the surface, and with it
`MDSW2SD` and `RSTAVNT`. The engine surfaces him when the probe above his head
finds neither the surface mesh nor the bed - that is, when he swims out from
under the water polygon's footprint - and the canal's walk-in keeps him under
`Eau` throughout. Finding that edge is for the play test.

## 8. Step 6, PLAYED — nine reports, and what each one was (2026-09-17)

A reader played the canal through some fifteen sessions in one evening, the log
of each kept whole. **CONFIRMED IN PLAY** at the end of it: the entry, the swim
under water and at the surface, the dive, coming up, the breath and the
drowning, the chase camera and its wall test, walking into the water, and the
climb out. Every one of the faults below was in code the headless check had
passed.

| the report | what it was |
|---|---|
| *"he is not swimming, he just walks at 90 degrees"* | the engine takes the water arm OR `Walk_GroundResponse` (`21_d3d.c` 3830, an if/else); the port ran both, and on the tick they coincide - real time, which `--nodelay` happened to split by a frame - the landing's group 4 overwrote group 300. The landing on water also posted message 10, fall damage the engine never reaches |
| he would not swim however long CTRL was held | `Plonger` is DIK_RCONTROL and a Mac laptop has no right control. The viewer's keymap sends the same code for the left one - the frontend's choice of key, the engine's table untouched |
| he stood on the bed in `H_FALL` for ever | with the landing skipped he never steps, and the entry read the walker's CACHED stand flags, which only a step writes. The entry now probes the mesh under him, as `Walk_ProbeGround` does |
| *"the camera should be behind him"* | the swim camera is not a preset offset at all. `sub_414520` gives mode 21 its own setup (`sub_413EF0`) and gives mode 0 a SWIM VARIANT (`sub_413CD0`) in states 11/13/14, both writing chase tunables (+228 = -39.37, 1.5 / 1.2 / -1.0, flags 0x4800). Now the chase camera - 3 m behind by the yaw, 1 m up. RECONSTRUCTION: the flagged passes are unported, as the land camera's are |
| *"always goes up ... the position resets each loop"* (the drawing) | for looping clips the draw adds the pelvis track's y as a bob. A swim clip is authored upright and strokes along the body's axis, so that y is the TRAVEL, already on his position: drawn again, in world y and unturned |
| *"can not go to the surface"* | the probe point is the HEAD - `actor+16` is the `Tete` node (`Actor_LoadModel`) - as posed, not a standing head's height and not the pelvis (both were tried). And the second probe goes DOWN from two metres over it: water first is clear air and he surfaces, a roof first puts him back under. It was transcribed backwards |
| *"still goes up whatever direction"* (the motion) | `Cef_ApplyRootShift`'s glide - `H_SWIMIN` frames 15..27, along the clip's own up - went through `rotateByFacing`, which turned by the yaw alone. +288 carries the pitch in the water, so that function now IS the whole Euler in states 11..14. Found by a per-tick trace (`OMK_SWIMTRACE`): he rose 1.2 a tick through the strong half of every stroke with his nose fully down |
| no way out of the water | `MDACTION`'s water arm, `sub_4A9580` - table-called, so invisible to a caller search: a platform within 80 cm ahead, no more than 29.53 over the water; the PELVIS 27.56 under it (the engine's position is the node; the port's is the feet, and putting the FEET there started him 42 too high); ACTOR_STATE 12, group 303, `H_WO_SD` to `MDSW2SD` |
| *"he falls a little each time he climbs"* | the water's floor limit was "below the nearest floor: put him on it", which SNAPPED UP a body coming up the quay's face the moment it drifted over the edge. It stops a body coming DOWN now. A scripted climb had landed exactly and a played one 11 high, because the crossing depends on the approach |
| could not walk into the water, only jump | `21_d3d.c` 2644's `|| (mesh & 0x20000000)` had been ported as a refused step. It is one probe case answering with a velocity. A step now looks THROUGH the water, as the fall does |
| the camera left the set | a ray from the target to the eye through `shotSoup`, the fight camera's own test. RECONSTRUCTION: the swim variant does not set the land camera's wall flag 8, and what its 0x4000 / 0x800 run is unread |

**Two lessons this file should keep.** A function with `@callers 0` is not dead -
`sub_4A9580` and `sub_465390` are both table-called, and the whole way out of the
water sat in one. And every one of these survived a green check, because the
check asserted what the port did: three of its assertions (the drift to pitch
340, the head over the pelvis at the cap) described the invented head probe and
were deleted with it.

**Labelled and open**: the head point is the rest offset turned by his Euler,
without the clip's bend; with nothing under the head the body's own floor
answers (the engine probes every mesh, this the walkable floor alone); the
climb's slope test is not ported; the step's 0x20000000 nudge is not ported;
`engine: water entry` cannot reach the climb out from its ledge, which stands on
play alone.

**The street's freezes were not the swim.** A `SLOW FRAME` line (any frame over
two periods, with his position) attributed all six of one session: two on a
MUSIC TRACK SWITCH - the new stream opened on the main thread - and one on the
frame a SET became resident, with the texture pool rebuilt and the Vulkan depth
tie walked over the new geometry. Three more, near x 13200 z 14000, were not
read. Handed to the performance task.

**CORRECTED the same evening, and the commit message of `b018c25` carries the
wrong version**: this first said the third hitch was "a new character's ...
depth-tie table built for the posed body". The performance session checked it:
the log line `depth tie - 168 of 3075 triangles` is gated on more than 1000
triangles (`corners > 3000`), which no character model passes on its way into
that line as read here - the largest, AST_FNM, is 1070 and the mean 463 - and
3075 triangles with 168 drops is exactly one file in the
tree, the decor set `MESHES/DECORS/SRest02.3DO`. So it was a LOAD cost, once,
not a per-frame body cost. The instrument was right and the attribution was
wrong - CLAUDE.md 1's "attribute before you count" - because a triangle count
was read as a body without asking what the line can ever print. If the line is
kept, it should print the geometry's NAME.

## 2. The steps

| step | what | state |
|---|---|---|
| 0 | this reading | **done 2026-09-17** |
| 1 | the entry: the 0x8000000 mesh under his feet in state 1 -> group 300, scheme 1, state 11, camera 21; rename state 11 | **done 2026-09-17** - §3 |
| 2 | the water moves: `MDDIVEND` (14, message 22), `MDSW2SD` (1, camera 0), `RSTAVNT`, `RSTNAGE`, `MDDIVBEG` | **done 2026-09-17** - in the canal walk-in `MDDIVEND` fires, ACTOR_STATE 14, message 22 answered by AREA 1; the other four are wired and not yet reached by a run |
| 3 | the motion, 11..14: no gravity and no ground snap; the surface hold; the pitch | **done 2026-09-17** - §4; the pitch turns the MOTION, the drawn body is 3b |
| 3b | the pitch on the DRAWN body: the whole Euler, actor+288 | **done 2026-09-17** - §5 |
| 4 | the breath: the 40 s timer, `Hud_DrawBar` mode 1, message 12 | **done 2026-09-17** - §6 (the timer and message 12 landed with step 3) |
| 5 | a place to swim headlessly, and the check | **done 2026-09-17** - §7: the canal walk-in, the DIVE key, and one run carrying float, drift and swim |
| 6 | play | **PLAYED AND CONFIRMED 2026-09-17** - §8: nine reports, each a port fault a green check had passed |
