# The standing unknowns, 2026-09-05

Picked up after `todo/reader-followups.md` closed. Ordered smallest and
best-evidenced first, so each lands as its own commit and the research one
does not block the rest.

| # | item | state |
|---|---|---|
| 1 | the camera pass's three unmodelled details | **DONE** 2026-09-05 |
| 2 | the shoot AI's brains are not called | **DONE (as a decision)** 2026-09-05 |
| 3 | `.3DM`'s `float[3]`, and node slots 0 and 1 | **NARROWED** 2026-09-05 |
| 4 | the Anekbah panel FLICKER | **candidate REFUTED** 2026-09-05 |
| 5 | the player's RIDE | **READ, not ported** 2026-09-05 |

The reader cannot watch the screen, so anything that needs an eye is settled
by sending frames rather than by asking them to look.

---

## 1. The camera pass's three unmodelled details

`PlayerController::cameraCollide` transcribes `sub_417070` and names three
things it leaves out:

* **the recovery branch's second ray.** With no hit on the over-reach ray and
  `+208 == 1`, the engine rebuilds target and eye from the subject's UNLAGGED
  euler and offsets and casts again; only if THAT misses does it go to state
  2. It decides how fast recovery starts.
* **flag 1**, the "just changed" bit `Camera_LoadParams` sets and the tick
  clears on its way out: the engine skips both easings for that one frame and
  SNAPS. One frame at a camera change.
* **the face set.** `sub_444810` walks the scene's meshes and can skip one by
  flag - a mesh flagged `0x20000000` is see-through to a camera carrying
  `0x1000` - where this casts against the walker's walkable and steep soups
  and has no per-face flag.

**DONE, and it turned out to be TWO, not three.**

* the **second ray** is in, resolved through `resolveSteady` - which is
  exactly what the engine rebuilds, the camera with no lag in it. On a
  second-ray hit this holds the distance rather than re-placing the eye the
  way the engine does (at `+328` along the current direction, shifted by the
  subject's own vertical movement `+340 - +344`); labelled in the code.
* **flag 1** is in: `camFresh_` already carried it for the eye/target
  smoothing, and `camJustChanged_` now carries it into the collision pass, so
  both easings snap on the frame a camera changes.
* **the see-through flag is UNREACHABLE for this camera**, which is why it
  stays out. Both arms guard their hit with
  `if ((cam[356] & 0x1000) && (hit->flags & 0x20000000)) return;` and the
  SURFACE half is live - **93 of the 12203 shipped decor meshes carry
  0x20000000**, 41 of them in Lahoreh and 10 in Jangir. The CAMERA half is
  not: `Camera_LoadParams` writes `+356 = 1` on every request, `sub_413C00`
  then ORs `0x1C` through a **LOBYTE** write that cannot reach bit 12, and the
  only write in the binary that sets 0x1000 is `sub_413CD0`'s **ACTOR_STATE
  13** arm - and `sub_413CD0` runs only for states 11, 13 and 14. The ordinary
  follow camera never carries 0x1000, so the test cannot fire for it. That
  puts it with `nullsub_9` and the six dead spell recipes: unreachable
  content, recorded rather than implemented.

## 2. The shoot AI's brains

`Shoot_TickNpc` calls one of four brains every frame and they go on asking for
actions; `actor/shoot.h` models them and nothing calls it. Today's pose work
takes the SCRIPT's last action and holds it, so a gunman keeps one clip.
Also `List_PickRandomByType` picks at RANDOM and this takes the first match.

**CLOSED as a decision, not as a port.** The brains stay unwired, and the
reason is in `shoot.cpp` itself: the generic arm - **302 of the 306 shipped
sites** - "takes the first edge and RECORDS the choice rather than pretending
to compute it", because the real branch needs the navigation node, the line
of sight and the weapon's range, none of which this tree has. Driving a
frontend from it would draw a deterministic first-edge walk **as if it were
the game's behaviour**, which is putting a guess where a fact belongs. It is
recorded here so the next person does not mistake the gap for an oversight.

**What WAS a fact and is now faithful: the pick.** `List_PickRandomByType`
returns a random one of the matches, and the choice is real - across the
eleven shipped libraries there are **195** (library, group, behaviour type)
buckets and **40 hold more than one clip** (two in 26, three in 3, four in 7,
five in 3, six in 1). The roll is seeded per (group, action) rather than
re-rolled per frame, because the engine rolls once per `Shoot_ActorAction`
and there is no AI here asking again; labelled as that in the code. The cave
sees no difference - group 11's type 9 is one of the 155 single-clip buckets.
`verify.py: engine: shoot pose` now asserts the 195 and the 40.

## 3. `.3DM`'s `float[3]`, and node slots 0 and 1

The parser's integration is read and asm-confirmed, but the corpus refutes
"root-motion deltas" as playable semantics - near-constant near-unit in 57/60
files, so integrating it drifts every character off the map - and whatever
neutralises the integral in the engine is untraced. Slots 0 and 1 are
narrowed: uploaded with preamble ids 0/1, bound by no drawn mesh, not
rotations, and NOT the voice envelope (|r| < 0.2 against per-frame RMS in four
files). Slot 0 stays in [0,1]^4 and varies smoothly; slot 1 is a signed
low-magnitude 4-vector with one dominant component.

**NARROWED, not solved - and two recorded numbers were wrong.** Measured over
all **777 files, 5327 frame samples** (`verify.py: morph unknowns`):

* the trailing `float[3]` is **unit to 0.001 in 5327 of 5327**. That refutes
  the translation reading from the DATA rather than from the drift it
  predicts: a per-frame displacement is not exactly unit length every frame of
  every file. It is a **direction**, and the parser integrates a direction -
  which is precisely why applying its output as displacement walks every
  character off the map. What it points at is still open; it is **not** any
  node's -Z forward (best mean |dot| 0.66 over the nodes of two files).
* **the two node slots were recorded backwards.** FILE_FORMATS had node 0 unit
  in 15 of 80 and node 1 in 2, from one seeded sample. Over the corpus it is
  the reverse and much sharper: **slot 0 is NEVER unit (0/5327) and slot 1 is
  unit in 71% (3790/5327)**. Slot 1 is a quaternion most of the time; slot 0
  is not one at all, and its third and fourth components sit near 1.30 and
  -1.29 with the first two swinging a unit either side - parameters rather
  than a vector.

Still open: what the direction is for, and what slot 0 holds.

## 4. The Anekbah panel FLICKER

The WRONG TEXTURE half is solved - the 58-slot name cache, `verify.py:
texture name cache` - and the flicker is not. The `AApub*` prism (7 vertices,
3 quads of identical UVs) with `D3DCULL_NONE` and no depth bias is the
standing account. The neon half was WITHDRAWN: 148 of Anekbah's 153 emitters
have period 0, a particle every frame, which is a steady glow.

**The prism account is now REFUTED too, so the flicker has none left.** The
UVs half was right and the geometry half was not. Over all **36** of
Anekbah's three-quad `AApub` meshes: every one carries its three quads on
**one material**, **none** has any two of them coincident, and the closest two
face centres are **13.8 / 14.2 / 15.3** units apart (min / median / max).
`AApub04`'s quads index (0,2,3,1), (1,3,5,4) and (4,5,2,0) - a ring of six
vertices with a seventh above them, which is a triangular **prism** with a
cap: three SIDE faces about fourteen units apart, each carrying the same
advert. A real trivision hoarding, not two adverts on one quad. They cannot
z-fight.

The coincident faces that DO exist in Anekbah are the shop signs - 18 pairs,
exact to the float, in the same mesh - and their tie-break is already known
(ASSETS 4b, the texture slot in material order). Those are the WRONG-TEXTURE
half, which is solved.

Result: a candidate removed rather than a fault fixed, which is worth more
than leaving it to be built on. `verify.py: aapub prism`.

## 5. The player's RIDE

The road traffic is ported - the sliders and motos on the vehicle lanes - and
the player mounting one is not. `ACTOR_STATE` 7 and 8 are the mount and the
ride of one slider (`MDSLIDOU` refuses to dismount from anything but 8), and
7 has no case in `Actors_TickAll` at all.

**READ, and deliberately not ported - with the size measured rather than
guessed.** `Slider_TickRide` (0x00458150) is what runs, and it needs
`sub_4573E0` (**387 lines, RAW**), `sub_458600` (75) and `sub_457F50` (66):
about 600 lines of undecompiled machinery with its own globals. That is a
slice of its own.

Three facts from the read are pinned by `verify.py: slider ride` so they are
not lost in the meantime:

* **the ride runs at HALF SPEED** - `Slider_TickRide`'s first act is
  `flt_4C30D8 = flt_4C30D8 * 0.5`, the engine's own frame delta (the one
  `Game_Frame` sets to `30.0 / fps`), saved in a local first. Everything
  ticked inside the ride advances at half a frame per frame.
* **camera mode 8 is the ride camera and its SUBJECT is the slider**, not the
  player: `eyeSubject` and `targetSubject` are both 5. Its offsets are exact
  metres, the same authoring mode 0 shows - eye 118.1102 up and 275.5905
  back, target 78.7402 up, which is **3.00 m, 7.00 m and 2.00 m**. `f42` is 0
  so the target does not lag; `f44`/`f46` are 8, mode 0's eighth-per-frame.
* **the mount binds the player's `node+156` to the SLIDER's matrix** -
  `sub_437140(playerNode, sub_438450(slider))` - which is the same field
  `Anim_RootDelta` turns a clip's root motion by. So a mounted player's root
  motion is rotated into the vehicle's frame by machinery this port already
  has, rather than by anything new.
