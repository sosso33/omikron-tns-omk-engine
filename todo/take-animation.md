# The take animation — session state, 2026-09-04

Handoff for `omk-play` issue **69**, the take's animation. The findings live in
`todo/omk-play.md` 69; this file is the **state of the work**: where it is, what
runs, what does not, and what is untested rather than wrong.

## Where the code is

**Branch `take-height`, in the worktree `.claude/worktrees/take-height`.**
Based on `ce1a0cc`; **not merged into `main`, deliberately** — the reader's
instruction was "stay in branch, it is too broken now to merge into main",
which was about *this branch*, not about main. (I first read it the other way
round and told the sneak session main was suspect; that was my error and it
cost them a held commit.)

Eight commits, tree clean:

| commit | what |
|---|---|
| `c318a57` | two take animations, chosen by height; `scanTakeable` gains `dyOut` |
| `55d7125` | confirmed in play; `--scene-chunk` added |
| `01531ec` | a take clip is a GRID of variants |
| `0e9ba51` | the spec — the variant count is the `.CTL`'s own nibble |
| `908a81d` | `sub_4725B0` read — the blend is bilinear |
| `159d91e` | the grid IMPLEMENTED |
| `9d66724` | the anchor is the FLOOR; stop reading the rest sentinel |
| `0c281e2` | the crouch — root motion carried, the variant SEAM excluded |
| `7c12eb1` | the take is TWO STAGES — the adjust step and its direction |

## What the take does now

Press action →
**`MDACTION`** installs group **600** (`H_ADJSTP`) and the character steps into
position →
**`MDADJSTP`** re-measures the geometry and installs group **41** (`H_TAKL`,
floor) or **143** (`H_TAKH`, table) →
`H_TAK*12` reaches, the clipless **`MDGETOBJ`** hands the object over,
`H_TAK*22` stands up →
**`H_WAITOB`** *waits* →
your press picks **`MDPUTSNK`** (into the sack) or **`MDNOTAKE`** (cancel).

Each state plays **one 21-frame cell** of its clip, blended from the four cells
nearest the approach, with the pelvis dropping through root motion.

## Confirmed in play

* the height rule — `dy −1.3 → group 41`, the sign settled by the running game
  and matching `prop_probe`'s prediction (ring y −80.0, seated feet −78.7)
* the chain walks `H_TAKL12 → H_TAKL22 → H_WAITOB → H_GETOBJ`, one clip each,
  which is the reported "it plays the complete list" gone
* the wait really waits: 234 ticks held, left only when input `0x10` arrived
* the timing: 20 ticks for a 21-frame window at 30 fps, 0.7 s for a grab
* no T-pose at the start (that was the rest sentinel, key 0)
* the crouch tracks: feet lifted and root dropped stay within ~1.5 units all
  the way down, and the take returns to standing

## NOT confirmed — the honest list

1. ~~**The adjust step (`7c12eb1`) has never been driven into the world.**~~
   **Driven 2026-09-04, sixteen presses over two runs, and the fault was
   neither of the two the paragraph below guessed.** The displacement DID
   reach the walker: per frame, the step moved him 17–20 units in the
   object's direction over frames 0–18 (the `crouch:` line now prints the
   walker's x/z, the delta asked for and the walker's verdict). Then the
   window's LAST frame — the seam `poseFrame()` already holds the pose short
   of — applied the exact negative of the whole window (`dxz +17.81 -0.56`
   after a step summing to −17.8), and he stood back on the press point when
   `MDADJSTP` re-measured. So the step was visible to the eye and invisible
   to the log, which is exactly what the two reports said. Fixed in
   `player.cpp`: the grid root delta now clamps to the window's second-last
   entry, as the pose does. **The angle test this bullet proposed was the
   wrong test anyway**: a step straight at the object leaves the bearing
   alone; the distance is what shrinks, and the log does not print it yet.

   The seam fix then exposed the second fault plainly (third run, seven
   presses): the step was one authored 50 cm whatever the distance, and he
   walked *through* the rings whenever they were nearer than that (pressed
   13 units short at −1.8°, ended with them behind him at −172.7°). That
   sent the reading to `sub_465D30` whole, and it is ported now - see
   §"sub_465D30, read whole" below.

   The original text, for the record: *The build after it was launched twice;
   the first run sat on the start menu and was killed, the second was never
   reported on. Nothing about the step is play-tested, including whether the
   character now visibly squares up and whether the `MDADJSTP` line shows the
   angle shrinking across the step. If it still reads 66.4 → 66.5, the
   displacement is not reaching the walker.*
2. **The HIGH take (`n = 9`, group 143) has never been entered at all.** No
   object at table height was ever taken. Its `second` axis is a guess: the
   high arm of `sub_465D30` sets no `var_2C`, so what reaches `+0x1C8` there is
   unread, and the code says so.
3. **The blend fractions are the weakest link.** 256/51, 256/53, 256/50 each
   match their own clamp, which is one hypothesis fitted three times, not three
   confirmations. What would settle them is a rendered pose at a mid-cell angle
   checked to sit *between* two variants.

## Broken / owed

* **`verify.py: engine actor states` is RED**, deliberately not re-baselined.
  Six counts move — channel ticks 55182 → 57330, edges 10367 → 11599 and four
  more — because a grid state now lasts 21 frames instead of 125, so more
  transitions fit the harness's tick budget. That is the expected consequence
  of a real fix, but it is a broad behavioural oracle and re-baselining it
  silently is exactly the "green beside a wrong reading" CLAUDE.md 1 warns
  about. **Someone has to decide**, and the decision needs the animation
  confirmed by eye first.
* **No check covers any of this.** Every fault today was found by a reader
  playing the build; none by anything in `verify.py`. A check should assert the
  FOUR OFFSETS for a known geometry (`n * len == keys` is a second constraint
  the data can fail) or count DISTINCT ENTRIES per press — never sample a clip
  name, which wasted two traces.
* **A debug printf is still in** `play.cpp` (`crouch: …`, one line per frame of
  a take). It is the instrument that found the seam — twice now: the vertical
  one on 2026-09-03 and the horizontal one on 2026-09-04, after it grew the
  walker's position and delta. Keep or gate it, but do not merge it without
  deciding.

## `sub_465D30`, read whole (2026-09-04)

The chooser is not twenty lines, it is the whole take setup, and the port had
guessed everything but the group. `play.cpp`'s press handler now carries it
line for line; the short form:

* `dx, dy, dz` are object node − actor node, and the actor's node is the
  **pelvis** (`o3de_SetNodePos(node, +244, +248, +252)`, the follow camera's
  subject). `dy` counts DOWN like every y here. **The port's height test was
  feet-relative and read the engine's as up-positive; it agreed on the floor
  and on a 70 cm table by coincidence.** Now `dyP = dyFeet + cameraLift()`,
  and `dyP <= 27.47` (less than 70 cm below the pelvis, i.e. above ~36 cm off
  the floor) is the HIGH take. Only the floor is play-confirmed.
* the **target point**: 40 cm (floor) or 60 cm (table) divided by cos(angle),
  short of the object along the approach line. The LOW arm also biases the
  angle by −10°.
* refuse if |angle| > 50 and (from MDADJSTP, or already inside the target), or
  the distance error exceeds 120 cm.
* from MDACTION: within 10% of the target distance → **no step**, take at
  once. Otherwise the step SCALE `dword_6A5380 = |D − target| / 19.69`.
* `Actor_Move` to the target point outright before any take (from MDADJSTP,
  and in the no-step case); before a step only a PROBE, and a probe under
  25 cm drops the step. The port takes the requested length for the probe,
  labelled.
* if the step is coming and he is inside the target, the stored angle is
  flipped by 180: he steps BACK.
* the HIGH take's second axis is the **pitch** of the object seen from the
  target point, `asin(−dy / hypot(target, dy))` in degrees — the value the
  handoff called a guess. The LOW arm's is `dy − 27.47`, scaled by 1/29.53 in
  `sub_466390` as before.

**The angle's SIGN is the engine's negative of the port's `rel`.**
`v41 = acos(cos); if (fx*dz − fz*dx > 0) v41 = −v41`, and with the heading
recipe both facings use that cross product is `sin(bearing − facing)`, so a
positive `rel` is a negative engine angle. Found in the data before the code:
with the mirrored sign the step's side cell pushed him AWAY from the object
line (lateral offset 15.4 → 26.8 and 17.7 → 20.5 across two steps, run 4);
flipped, it shrinks or holds (19.9 → 15.1, 25.3 → 14.3, run 5), and a press
with the rings behind him at −185° stepped BACK and landed at −10.0°, which
is exactly the low arm's bias with zero lateral. The sign also picks which
side the take clip reaches to, so every earlier "confirmed by eye" pose was
mirrored left-for-right.

**Run 5, twelve presses, the whole chain as ported:** scaled steps of 20 to
42 units land 2 to 4 short of the target (the blended window's motion is a
little under the authored 19.69, the seam excluded), the pre-take move
closes the rest, the 10% no-step case fires from 18 and 24 units, and the
cone refuses at +60°.

## ~~The one unresolved contradiction~~ — CLOSED 2026-09-04

`sub_45CE90` (the channel tick's root-delta source) does not call
`Anim_RootDelta` for a grid clip: with flag bit 4 of +1252 set it calls
`sub_4725B0`, whose position half `sub_472820` runs **four** `Anim_RootDelta`
calls at the four cell offsets (`u16(sample, 8/10/12/14)`) and mixes them with
the same two 0..256 weights as the rotations. So the engine reads the
**blended cells' root motion**, which is what the port does; the "divergence"
labels in `player.cpp` are wrong and should be re-worded as agreement.

And the displacement's path is `sub_466540`: while `dword_53AE1C` (the step
flag) is set and the actor is the player, the channel tick multiplies that
delta's x and z by `dword_6A5380` before `o3de_MoveNodeBy`. Ported as
`PlayerController::setStepScale`, applied to the grid delta in `tick`.

The original text, kept for the record:


`Anim_RootDelta` (`0x004711D0`) indexes the position keys by the **raw frame**,
no cell offset — traced, not assumed — so the engine reads **cell 0's** root
motion for a grid clip. But cell 0 is a step to the *right*, and the rotations
alone leave the body floating. So the port reads the **blended** cells instead,
for both the crouch and the step's direction. Both divergences are labelled in
the source.

Either the engine starts a grid state's frame **inside its own cell** — which
would change how both the root reads and `sub_4725B0`'s offsets are modelled
here, and is the reading I would test first — or the displacement reaches the
actor by a path not yet found. Settling this is the next real piece of
reverse-engineering, and it may simplify everything above.

## Suggested order for whoever picks this up

1. ~~Play the current build and answer §"NOT confirmed" 1~~ — done 2026-09-04,
   five runs; see §"sub_465D30, read whole".
2. Take something off a table, to exercise group 143 for the first time — and
   now also the pelvis-relative height rule between 36 and 70 cm, and the
   pitch second axis.
3. ~~Settle the frame-origin question~~ — closed; it was `sub_45CE90`'s
   routing.
4. Decide `engine: actor states` (still red with the SAME six numbers after
   today's work, which touched only the player controller), and write the
   check §"Broken / owed" describes. A press-and-measure harness would need
   the props SHOWN, which `--scene-chunk 55` only gives after its ~1145
   frames of beats — a headless attempt on 2026-09-04 pressed too early.
5. The behind-the-back case: with the object behind and inside the target
   distance the target goes negative, the 10% ratio passes, and the pre-take
   move carries him THROUGH the object (run 4, 31.6 units). The engine does
   the same arithmetic; whether its `Actor_Move` collides with the prop is
   unread. Watch for it in the original before patching.

## How to run it

```bash
cd .claude/worktrees/take-height/engine && make -s play
./build/omk-play <gamedata> ../tables --speed 3 --nofmv
```

Play to the Impasse and take the rings after the tutorial cutscene. A street
start needs `--scene-chunk 55` with `--area 222` or there are **no props to
take** — the chunk that would have loaded on the way in never does, and nothing
in the viewer says so.
