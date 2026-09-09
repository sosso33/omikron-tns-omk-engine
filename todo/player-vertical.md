# The player's VERTICAL — the walk's float and the jump

A reader, 2026-09-08: *walking or running seems to rise its y position and
stopping (and so returning to the idle position) reset the y position to a
normal one*, and *jump is broken (animation play, but y position is too low so
the jump become useless)*.

Both are the same missing mechanism, and `play.cpp`'s own comment at the
player's placement named it in advance: *"the engine seats the actor by his own
origin and lets root motion move him… this is equivalent while some part of him
is on the ground and would be WRONG for a pose where both feet leave it — a
jump… when one arrives, this is the line that has to become the real
root-motion path."*

---

## 1. What the engine does, read

**`Actor_MoveBy` (0x00469450) applies X and Z only.** It adds `a2`/`a4` to the
actor record's `+232/+240` (the last safe position) and `+244/+252` (the
current one); `a3`, the Y, is never written to the record. It reaches only
`o3de_MoveNodeBy(a1, a2, a3, a4)`, which moves the DRAWN node. So a clip's root
motion never moves the actor's position vertically — it moves the picture.

**The vertical is a VELOCITY.** `Actor_ApplyMotion` (0x004672D0):

    +216 / +224   velocity x / z
    +220          vertical speed        +228   its acceleration
    +232..240     the last safe position       +244..252   the current one

    if (state >= 11 && state <= 14) return sub_4A8F30(actor);   // scripted
    +220 = +228 * dt + +220;  if (+220 > 787.40155) +220 = 787.40155;
    dx = +216 * dt;  dy = +220 * 0.033333335 * dt;  dz = +224 * dt;
    ...apply, UNDO, then Actor_Move(actor, wx, 0, wz, ..., 1, 1, 0)

so the horizontal goes through collide-and-slide with **Y zero**, and the
vertical is settled by `Walk_GroundResponse(a1, gmesh, gnorm, hit, wy, gpoint)`
— which itself writes `+220`: the step-up constant `dword_910340` on a walkable
slope, and zeroes all three velocities on one past `dword_91033C`.

**The anchor is a constant.** `Walk_ProbeGround` (0x00467030):

    v5  = Collision_BodySphere(node)          // the LARGEST-radius sphere
    v9  = groundY + (rotated sphere centre).y
    actor[264] = actor[236] - actor[248] + v9 + f32(v5, 12)

`Collision_BodySphere` (0x00444360) walks the model's sphere list at
descriptor `+244` (count) / `+248` (16 bytes each: centre, radius at `+12`) and
returns the one with the largest radius. So the seat is
**ground + sphere radius**, never the pose's own lowest point.

**The jump is an impulse into that velocity.** `tab_special_move` rows 14, 15,
16, 49, 50 are `MDJUMP0A`, `MDJUMP0B`, `MDJUMP01`, `MDJUMP02`, `MDJUMP03`.
None has a `proc` label; read from the image:

* **0x0046BB50 `MDJUMP0A`** latches the take-off position — `+244/+248/+252`
  into `dword_53AE40/44/48` — sets `dword_6A52CC = 1` (the flag
  `Actor_ApplyMotion`'s steer test excludes), and loads
  `dword_910348` = **98.4252 = 2.5 m**.
* **0x0046BD50 `MDJUMP01`** is the impulse, six instructions:

        fld  dword_53AE54 ; fmul flt_4BC934 (30.0)
        fld  dword_53AE58
        fld  dword_53AE50
        fstp [actor+0D8h]   ; +216  velocity x = dword_53AE50
        fxch st(1)
        fstp [actor+0DCh]   ; +220  VERTICAL   = dword_53AE54 * 30.0
        fstp [actor+0E0h]   ; +224  velocity z = dword_53AE58

  The 30.0 is the frame rate: `Actor_ApplyMotion` spends it again as
  `+220 * 0.033333335`, so `dword_53AE54` is a per-FRAME displacement and
  `+220` is the per-second speed.

**The tunables it sits in**, all round metres, initialised together in
`05_sys.c`:

| global | inches | metres |
|---|---|---|
| `dword_91033C` | 30.0 | the slope limit, DEGREES |
| `dword_910340` | 11.8110 | 0.30 — the step up |
| `dword_910354` | 19.6850 | 0.50 |
| `dword_910344` | 29.5276 | 0.75 |
| `dword_910350` | 59.0551 | 1.50 |
| `dword_910348` | 98.4252 | **2.50** — `MDJUMP0A` reads this |
| `dword_91034C` | 137.7953 | 3.50 |

**Still unread**: how `dword_53AE50/54/58` are computed. They are written
inside `MDJUMP0A`/`0B` past the bytes decoded so far, and they are the whole
of the jump's magnitude.

---

## 2. What the port does, measured

`omk-play` seats the body by re-deriving the anchor from the STANDING pose's
lowest corner (`playerFeet`, latched once) and then adds `rootDrop`, the
pelvis track's summed Y. Measured over idle -> walk (`OMK_PLY=1`, Y grows
downward, so a smaller number is higher):

| clip | lowest corner |
|---|---|
| `H_STAND` | +38.78 (the latched anchor is 38.76) |
| `H_WALK` | +36.71 … +39.02 |

so the feet float up to **2.07 units** above the standing plane through the
cycle and return at the idle — the reader's first sentence. `rootDrop` swings
+0.65…+2.49 over the same cycle, which is the pelvis bob and about half of
what the legs do.

And the walker already carries the vertical machinery for FALLING — `vy_`,
`kGravity` = 12.860892, `kTerminal` = 787.40155, `airborne_` — so a jump has
somewhere to go. Nothing ever pushes into it.

---

## 3. The steps

1. ~~**The anchor.**~~ **DONE 2026-09-09**, and the cause was not where §2
   put it. See §4 - the clips are sound, and what displaced the body was the
   port measuring its anchor and its drop from two different origins.
2. ~~**The impulse.**~~ **DONE 2026-09-09**, and `dword_53AE54` did not stay
   unread - see §5. It needed no reconstruction: the magnitude comes out of the
   `.CTL` entry's own field and the actor's gravity.
3. **The state.** `MDJUMP0A`'s latch and `dword_6A52CC`, so the landing can be
   judged against the take-off and the steer suppressed while airborne, and
   `MDJUMP02`/`03` for the landing phases.

Each step ends in a headless measurement of the drawn feet across a walk, a
run and a jump, and a `verify.py` row that is shown to fail.


---

## 4. Step 1, done - and the clips were never the problem

**The float was a mismatch of ORIGINS inside the port, not a missing engine
mechanism.** `tools/vertical_probe` (new) reads the model's collision spheres
and then poses each named `.CTL` clip frame by frame and reports the body's
lowest corner. For HO1_FN:

| clip | f0 lowest corner | vs standing | f0 pelvis `trans.y` |
|---|---|---|---|
| `H_STAND` | +38.76 | 0.00 | **+0.68** |
| `H_WALK` | +36.71 | **-2.06** | **+2.09** |
| `H_RUN` | +35.88 | **-2.88** | **+3.68** |

Y grows down, so `H_WALK` f0 lifts the lowest corner 2.06 *while its pelvis
track drops 2.09*. **The authored pair cancels to 0.03** - the planted foot
stays planted. That is precisely the "authored motion netting out" that
`Walk_GroundResponse` relies on, and it means no clip in the locomotion set
needs correcting.

What moved the body was this: `playerFeet` is a **pose's** lowest corner,
latched from `H_STAND` - whose pelvis trans is **+0.68, not zero** - while the
drop was `rootAccum`, an accumulator **re-based to the entered clip's first
frame** (+2.09 walking, +3.68 running). Two origins, so the body was drawn a
*constant* offset off the floor for as long as the clip ran, and snapped back
the instant `H_STAND` reset the sum. Measured over a 220-frame headless run
(idle 40, UP 120, idle 60), as the drawn foot's height above the walker's floor
point:

| | idle | walk mean | walk worst |
|---|---|---|---|
| before | +0.02 | **+0.94** | +1.53 |
| after | +0.01 | **-0.02** | 0.63 (the authored cycle) |

**The fix, in two halves that have to move together.** The anchor now records
the pelvis trans it was taken at (`playerRootRef`) and is latched from
`H_STAND` *specifically* rather than from whatever pose was up on the first
frame; and for any clip that LOOPS - `variantCount() <= 1`, the structural
marker that already selects `gridTracks`, not a list of names - the drop is
read absolutely against that origin instead of through the accumulator. The
accumulator stays for the TAKE chain, where `H_TAKL12`/`H_TAKL22` genuinely
continue one another and an absolute reading is meaningless.

`verify.py: engine player vertical`, shown to fail: mutating the one absolute
line back to the accumulator moves the walk's mean foot from -0.02 to +0.94 and
its worst from 0.63 to 1.53 while the idle stays at +0.02. The mutation was
confirmed applied (md5 changed, binary relinked) and the run's OUTPUT compared,
not just the check's verdict.

**Two things this does NOT establish**, both labelled in `play.cpp` and in the
check:

* **The engine's absolute seat is still untraced.** `Walk_ProbeGround`
  (0x00467030) does not seat the actor at a fixed height at all - it writes a
  CLEARANCE, `actor[264] = lastSafeY - curY + groundY + rotY + radius`, and
  `Walk_GroundResponse` (0x00465460) drives that to zero against **`actor+276`,
  which nothing here has read**. So §1's "the seat is ground + sphere radius"
  is the right constant in the wrong grammar: it is a term in a clearance, not
  an offset. Read from the model, `Collision_BodySphere` gives radius 10.91 and
  `sub_4443B0`'s second-lowest centre.y is 14.98, and HO1_FN's lowest sphere
  reaches **41.81** below the node against a standing visual foot at
  38.76 + 0.68 = **39.44** - the sphere hangs **2.37 below the feet**. The two
  constants are not interchangeable, so the port does not use the sphere one as
  a seat; it is quoted only because it corroborates the scale.
* **The reader's word was "rise" and the measured pre-fix error is a SINK** of
  about one unit through the walk. The magnitude, the constancy and the release
  at the idle all match the report; the direction does not. Either the
  description is of the apparent height (the head drops ~0.5 through the same
  window) or there is a second effect this has not reached. Worth one look in
  play before step 2 rather than assuming it is closed.

**Still to do**, unchanged: steps 2 and 3 - the impulse and the state. And the
engine's own answer to this whole class is the ground probe under the drawn
body, which absorbs any vertical a clip authors and would retire the
accumulator and its three guards altogether; that needs `actor+276` first.


---

## 5. Step 2, done - the impulse, and where its magnitude comes from

§1 left `dword_53AE50/54/58` unread and called them "the whole of the jump's
magnitude". They are computed in `MDJUMP0A` (0x0046BB50), which has no `proc`
label and was read from the image:

    N   = u32(entry, 12)                     -- the LIVE `.CTL` entry
    sub_47DF00(entry, N >> 1)                -- "SetITPNbFrames", the binary's
                                                own error string: N/2 IS frames
    Matrix3x3_RotateVector(0, 0, -dword_910348, actor+288, &X, &Y, &Z)
                                             -- 98.4252 = 2.5 m along -Z, which
                                                is forward
    flt_53AE50 = X / N                       -- per frame
    flt_53AE54 = -(actor[228] * (N/2) * (1/30))
    flt_53AE58 = Z / N

`MDJUMP01`'s six instructions then copy those into `+216`, `+220` (times 30.0)
and `+224`. **The 30 and the 1/30 cancel**, so the launch is exactly

    +220 = -g * N/2

the ballistic speed for **N frames of hang** - zero at the apex on N/2, back on
the floor at N. Note that the vertical is *not* taken from the rotated vector;
only X and Z are. The 2.5 m is horizontal reach, not height.

**N is `entry+12`, and that field has a second consumer** - this is the one
part left labelled rather than settled. `ctl.h` reads its low half as a ROLE,
because `Fight_Begin` caches six codes off it in the combat banks. Measured
across `H1AVNT` with `tools/vertical_probe --jump`, it is **14 on exactly the
five jump entries** - `H_SDJUMP`, `H_WKJUMPL`, `H_WKJUMPR`, `H_RLJUMP`,
`H_RRJUMP` - and **0 on every other state that owns a clip**, with the high
half 0 throughout. Two things corroborate a duration: `SetITPNbFrames` takes
its half, and the engine has no other route to a length, since a clip's frame
count lives on the clip and not on the entry. And the competing reading was
tested and fails: "N is the clip's own frame count" cannot be right, because
those five clips run **8, 8, 10, 10 and 19** frames and none of them is 14.

**What it produces for Kay'l**, measured frame by frame in the port and
matching the closed form to three figures:

| | |
|---|---|
| hang | **14 frames** (13 airborne + the landing), 0.47 s |
| apex | **9.00 units = 22.9 cm**, which is `0.7 * kGravity` exactly |
| reach | **93.6 of the authored 98.43 units** (2.38 m of 2.5 m) |
| arc | symmetric to 0.01 about the apex |

So it is a flat running LEAP, not a vertical hop. Whether 22.9 cm of lift is
what the original shows is **not** something this tree can answer - no capture
reaches any of it (Tier 5) - so the numbers are recorded for a person to judge
at the keyboard.

**Two faults the measurement caught, both worth keeping.**

* **The compute and the apply land on DIFFERENT entries.** A first version read
  N inside `MDJUMP01` and every jump came back `refused`. The trace says why:
  `MDJUMP0A` fires on `H_WKJUMPR`, whose `+12` is 14, and `MDJUMP01` fires some
  frames later on `H_JUMPONR`, whose `+12` is 0. **That is what the three
  globals are for** - the engine parks the launch between the two handlers -
  and the port now has the same two stages (`jumpPrepare` / `jumpLaunch`).
* **The horizontal must follow the VELOCITY, not the `sliding_` flag.**
  `tick`'s gate was correct while the port had only two airborne cases: a slide
  writes `+216/+224` every frame and a fall leaves them zero, which is the
  reader's *"falling mainly on a single axis (just Y)"*. A jump is the third
  case and breaks the gate, because `MDJUMP01` **writes** those fields. Driving
  the horizontal off the velocity covers all three with no flag - a fall still
  moves on Y alone because its velocity really is zero. With the old gate the
  leap travels **2.37 units (6 cm)** instead of 93.6: a hop on the spot, with
  the vertical arc completely unchanged, which is exactly the shape that would
  have read as "the jump works" from the Y column alone.

`verify.py: engine player jump`, shown to fail on that second one.

**Step 3 is untouched**: `MDJUMP0A`'s take-off latch (`dword_53AE40/44/48`),
`dword_6A52CC`'s suppression of the steer while airborne, and `MDJUMP02`/`03`
for the landing phases.
