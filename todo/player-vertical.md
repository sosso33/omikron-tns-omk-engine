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

1. **The anchor.** Seat the body on `ground + body-sphere radius` (the
   engine's constant) instead of the standing pose's lowest corner, and let
   `trans` carry the whole vertical. The port already reads the model's sphere
   list (`Walker::setBlockers` takes the centres and radius 10.9 for HO1_FN);
   `Collision_BodySphere`'s rule is "largest radius", which for four equal
   spheres is the FIRST. Measure: the drawn feet over idle/walk/run must stop
   floating, and the check is that same three-clip sweep.
2. **The impulse.** `Walker::jump(vy)` setting `vy_` and `airborne_`, and
   `MDJUMP01` in `play.cpp`'s special-move loop calling it. The magnitude is
   `dword_53AE54 * 30.0` and that global's derivation is step 2's reading; if
   it stays unread, the reconstruction is the clip's own root-Y delta at
   take-off times 30, LABELLED.
3. **The state.** `MDJUMP0A`'s latch and `dword_6A52CC`, so the landing can be
   judged against the take-off and the steer suppressed while airborne, and
   `MDJUMP02`/`03` for the landing phases.

Each step ends in a headless measurement of the drawn feet across a walk, a
run and a jump, and a `verify.py` row that is shown to fail.
