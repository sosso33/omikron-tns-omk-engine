# The camera obstruction pass — ONE mechanism, TWO symptoms

Measured 2026-09-18. `sub_417070` has been read (`todo/missing-ui.md` §7) and
ported three times, each attempt reverted the same evening. This file adds the
thing those attempts did not have: **a second, independent test case**, and an
objective metric that needs no play report.

---

## 1. The lift arrival is the SAME fault as the restaurant dialogue camera

> **REFUTED 2026-09-18 — read §6 before acting on anything in §1, §2 or §4.**
> The two symptoms may still share a mechanism, but it is NOT `sub_417070`:
> both cameras are ABSOLUTE, and an absolute camera never carries the flag
> that arms the pass. §1's own measurements are untouched and still worth
> having; it is the conclusion drawn from them that is wrong. The function is
> transcribed whole in §5 anyway, because reading it is what settled this.


`todo/missing-ui.md` §6b identified the lift's black arrival as the mesh
`CSPont04` and left the mechanism open between three candidates. One is now
refuted and one is confirmed.

**Refuted — inside-face culling.** `sub_4638C0` writes `SetRenderState(22, 1)`,
**CULLMODE = NONE** (`docs/ASSETS.md` §4c). The engine draws both sides of every
face, so a lens inside a closed volume does NOT see out of it. Whatever the
engine does here, it is not culling.

**Confirmed — the lens is behind an obstruction, and pulling it in clears it.**
Camera **2986** is AREA 157's, absolute (both subjects −1), and the port reads
it exactly: eye (5708, 259, −11484), target (5811, 309, −11458), fov 75. That
is 117.4 units of separation. Rendering `ACSPUITS` alone through that camera
and walking the eye in along its own view ray:

| eye distance from target | frame dark |
|---|---|
| 115 (as authored) | **100.0%** |
| 60 | **100.0%** |
| 35 | **26.2%** |
| 18 | 30.3% |

At 35 the shot is the lift arrival as it is meant to look — the shaft, its
walkway, and the level number **2** painted on the far wall. So there is solid
geometry between 35 and 60 units out, and the authored eye sits behind it.

That is exactly what the reader described, in their own words: *"the camera is
not placed correctly so a part of the environment is just in front of the
camera"* — and *"it is not the only place where this issue occurs"*, which §6b
already matched to the other levels (camera 2978 inside `CSPont06a`).

**So the lift needs no lift-specific fix.** It needs `sub_417070`.

## 2. Why this changes the odds on porting it

The three reverted attempts were tuned against ONE symptom, the restaurant
crane, and judged by one play report each. Two of the three were not wrong
about the ray at all — attempt 2 failed on `+328` being held in one variable
across cameras, and attempt 3 on the SCOPE of which cameras take the rule.
Neither could be settled from a single shot.

The lift gives a second case with a **numeric** pass/fail that needs nobody to
play it: the arrival frame is 100% dark with the rule missing and should be
around 26% with it working. A change can now be judged on two independent
scenes, one of them automatically.

## 3. What must be read before any of it is written

`todo/missing-ui.md` §7's own instruction, unchanged and now twice as
motivated: **read all 271 lines of `sub_417070`, `+208`'s three states
included, before writing any of it.** The parts never read are the ones that
decide where the lens goes when the pull alone will not clear it — `+312/+316`,
the 0.7 × height push, and the second ray from the PREVIOUS camera position
that the `+208` state machine runs. A third of that function is what makes the
other two thirds safe; porting the ray alone moves every shot it touches, which
is what happened all three times.

The ray itself, from `04_sys.c` 3370ff, for reference only:

    d    = eye - target;  len = |d|
    ray  = target .. target + d * (+300)          // +300 = 1.2
    hit? -> dist = |hit - target| / +300
            if (dist > +328 && !(flags & 1))      // eases OUT over +320 = 8
                dist = (dist - +328) * dt / +320 + +328
            +328 = dist;  eye = target + normalise(d) * dist

with a hit on a `0x20000000` mesh ignored when the camera carries `0x1000`.
`+328` is PER CAMERA, not global — that is attempt 2's lesson.

## 4. How to judge a candidate port

* **The lift, automatically.** `--area 157 --address 446`, the repro in §6e.
  Assert the arrival frame's darkness, not a picture.
* **The restaurant, by eye and by diff.** Replay dialog 387 with and without
  the rule and diff every shot's eye frame by frame. Attempts 1 and 2 both
  moved shots that were already correct, and only a per-frame diff shows that
  before a person has to.
* **The walk.** The reverted §6 patch drew the arrival and left the player
  unable to move. Any camera change must still be checked with a few hundred
  frames of forward input.

---

## 5. `sub_417070` WHOLE — all 271 lines, transcribed 2026-09-18

Read from `tools/asmfn.py 417070 4178E0` (816 listing lines) beside
`readable/src/04_sys.c` 3370. Every offset below was checked in the raw
assembly; where the two disagree the assembly wins and the difference is
noted.

### 5a. The camera block's fields, as this function uses them

`sub_417CF0` (04_sys.c 3668) is the tick, `C` the live block. The offsets
here are all resolved against their WRITERS, not guessed:

| offset | what | established by |
|---|---|---|
| `+12` | the camera MODE | `Camera_LoadParams` copies param `+36` |
| `+20/+24/+28` | **the PREVIOUS frame's final EYE** | the tick ends `sub_4133B0(cam, cam+52)`, which is `+20..+28 = +52..+60`; the next tick opens by copying it back |
| `+32/+36/+40` | **the PREVIOUS frame's final TARGET** | `sub_4133E0(cam, cam+64)`, same shape |
| `+52/+56/+60` | the working EYE this frame | |
| `+64/+68/+72` | the working TARGET this frame | |
| `+76/+80/+84` | the LAGGED Euler triple | `sub_415E60` chases it toward the subject's at `dt/f46` |
| `+88` | the TARGET's subject (`-1` = absolute) | `Camera_LoadParams` param `+40` |
| `+92` | the target subject's ACTOR | `Camera_Request`: `cam[23] = request[2]` |
| `+100/+104/+108` | the target subject's POSITION | `sub_415D10` -> `sub_414F30(cam, 0)` |
| `+112/+116/+120` | the target subject's EULER, degrees | idem |
| `+124/+128/+132` | the TARGET's offset | `Camera_LoadParams` param `+16..+24` when subject != -1 |
| `+136` | `f42`, the target's lag | |
| `+140` | the EYE's subject | `Camera_LoadParams` param `+38`. **This is the field `sub_414520` dispatches on** |
| `+144` | the eye subject's ACTOR | `cam[36] = request[3]` |
| `+152/+156/+160` | the eye subject's POSITION | `sub_415E60` -> `sub_414F30(cam, 1)` |
| `+176/+180/+184` | the EYE's offset | param `+4..+12` when subject != -1 |
| `+188/+192` | `f44`, `f46` | |
| `+196` | a shake amplitude (`sub_418030` runs when > 0) | |
| `+208` | **the obstruction STATE, 0/1/2** | this function is its only writer besides the `memset` |
| `+276` (actor) | the subject's height | `sub_413C00` reads `f32i(actor, 69)` |
| `+300` | the ray's over-reach | 1.2 (`sub_413C00`), 1.5 on the swim arm |
| `+312` | the EYE's height push | `-0.7 x height` (mode 0); `-78.74` = -2 m (`sub_4141F0`) |
| `+316` | the TARGET's height push | `-0.7 x height`; `-39.37` = -1 m |
| `+320` | the distance ease, frames | 8.0 |
| `+324` | the height ease, frames | 4.0 |
| `+328` | **the kept distance, PER CAMERA** | it lives in the block, and the two blocks alternate |
| `+336` | the subject's height | |
| `+340`, `+344` | `+156` this frame and last | the tick does `+344 = +340; +340 = +156` immediately before the call |
| `+356` | the flags | |

`flt_4C30DC` is the frame delta (`dt`); `flt_4BC130` = 0.5, `flt_4BC178` =
1.0, `flt_4BC0E0` = 0.0. `dword_4E7CE4` and `dword_4E7CE8` are two scratch
`float[3]`.

**The two "previous" rows are the ones that were being read wrong.** They are
not the authored camera and not a subject offset — they are last frame's
answer, written back at the end of every tick. That makes `+24` and `+36` the
anchors of a real temporal ease, and it is what §5d turns on.

### 5b. Structure — one entry ray, three states, two exits

    sub_417070(cam):

      D   = eye - target                                  ; +52.. minus +64..
      len = |D|                                           ; v59, the FREE distance
      P   = target + normalise(D) * (len * +300)          ; the over-reach point
      hit = cast(target -> P)                             ; sub_444810

      if (hit) ------------------------------------------------ THE PULL
          if ((flags & 0x1000) && (hitMesh->flags & 0x20000000)) return
          switch (+208):
              0: +328 = len ; +208 = 1        ; first frame blocked: keep the free length
              2: +208 = 1                     ; blocked again mid-release: KEEP +328
              1: (unchanged)
          V = hit - target
          r = |V| / +300                      ; undo the over-reach
          if (r > +328 && !(flags & 1))
              r = (r - +328) * dt / +320 + +328          ; ease OUT only (8 frames)
          +328 = r
          E = target + normalise(V) * r                  ; the pulled eye, in SCRATCH
          goto HEIGHT(r, len, E)

      else if (+208 == 1) ------------------------------- THE SECOND RAY
          A   = (+100,+104,+108) - R(+112,+116,+120) * (+124,+128,+132)
          B   = (+152,+156,+160) - R( +76, +80, +84) * (+176,+180,+184)
          D2  = B - A ; P2 = A + normalise(D2) * (|D2| * +300)
          if (cast(A -> P2) hits) --------------------------- STILL BLOCKED
              if ((flags & 0x1000) && (hit->flags & 0x20000000)) return
              eye   = target + normalise(eye - target) * +328
              dy    = +340 - +344                        ; the subject's own rise
              eye.y    = +24 + dy                        ; last frame's eye Y
              target.y = +36 + dy                        ; last frame's target Y
              return                                     ; +208 stays 1, NO height push
          +208 = 2                                       ; fall through
      /* else: +208 is 0 or 2 and nothing is in the way */

      if (+208 != 2) return ----------------------------- clear, and was clear
      if (len <= +328) { +208 = 0 ; return } ------------ fully recovered
      +328 = (len - +328) * dt / +320 + +328             ; ease back OUT
      E    = target + normalise(D) * +328                ; in SCRATCH
      goto HEIGHT(+328, len, E)

      HEIGHT(r, len, E): ------------------------------- THE 0.7 x HEIGHT PUSH
          half = len * 0.5
          t    = (r <= half) ? 0 : (r - half) / (len - half)
          ey   = (+312 + +156) * (1 - t) + (+56) * t     ; NOTE: +56, the UNPULLED eye Y
          if (!(flags & 1)) ey = (ey - +24) * dt / +324 + +24
          ty   = (+316 + +156) * (1 - t) + (+68) * t
          if (!(flags & 1)) ty = (ty - +36) * dt / +324 + +36
          +68        = ty
          +52,+56,+60 = (E.x, ey, E.z)

**A is the UNLAGGED TARGET and B the UNLAGGED EYE.** A is term for term
`sub_415D10`'s formula and B is `sub_415E60`'s, so the second ray is the
camera *without its positional lag* — B still uses the LAGGED Euler `+76..+84`,
because that is what `sub_415E60` resolves the eye with. A camera whose lagged
ray has swung clear of a wall its unlagged one is still behind does not start
recovering yet.

**Four things in HEIGHT that a shorter reading misses.**

1. `t` is 0 whenever the eye is inside half its free distance, so the push is
   at FULL strength for any serious pinch and fades linearly to nothing as the
   camera recovers. It is not a function of the hit; it is a function of how
   far in the eye ended up.
2. The blend's far end is `+56`/`+68` — the eye and target Y **as the
   resolvers left them**, not the pulled Y. The pull lives in the scratch
   until the final store, so `+56` is still this frame's lagged, unpulled
   height.
3. The ease is toward `+24`/`+36`, **last frame's final** Y. That makes it a
   real first-order filter, a quarter of the remaining way per frame at
   `dt = 1`: over ~12 frames it converges on the pushed height. Easing from
   anything recomputed this frame instead gives a fixed 25% of the push and
   never more.
4. Only the eye's X and Z come from the pull. Its Y is whatever HEIGHT says.

### 5c. The two early exits, and what they are for

* the see-through pair: a hit is IGNORED entirely — return, camera untouched —
  when the camera carries `0x1000` and the hit mesh carries `0x20000000`. The
  only writer of `0x1000` in the binary is `sub_413CD0`'s ACTOR_STATE 13 arm,
  so the ordinary follow camera can never take it.
* `+208 == 2 && len <= +328`: the kept distance has grown back past the free
  distance, so the camera is out. State 0, and the function stops touching it.

### 5d. Where the existing port diverged, before this task

`PlayerController::cameraCollide` (`engine/src/actor/player.cpp`) already
carried this function for the follow camera (ported 2026-09-05). Against the
transcription above it had four faults, all in the third of the function §3
says nobody had read:

1. **the height ease anchored on the wrong Y.** It eased from
   `eyeWas`/`atWas` — values recomputed this frame — so the result was a
   constant `dt/4` fraction of the push, and the camera only ever rose a
   quarter of the way. The engine anchors on `+24`/`+36`, last frame's answer,
   and converges.
2. **the height blend's far end was the PULLED eye Y**, where the engine uses
   `+56`, the unpulled one.
3. **the second-ray-still-blocked arm fell through to the height push.** The
   engine re-places the eye at `+328`, sets both Y from last frame plus the
   subject's own rise, and RETURNS.
4. **the second ray used the actor's Euler for the eye**, where the engine
   uses the LAGGED triple `+76..+84`.

None of the four can move a camera through a wall, which is why
`engine: camera collision` never saw them: that check tests only the invariant
"nothing solid between the target and the eye", and all four are about the
HEIGHT and the recovery. All four are fixed as of 2026-09-18, plus a fifth
found beside them: `Camera_Request`'s `memset` clears `+208` and `+328` on
every camera change, and the port kept both across a new camera (attempt 2's
fault, still latent in the controller). `engine: camera obstruction` asserts
the first; the second-ray arm (3) is transcribed but NOT separately asserted
— the probe does not report which ray fired, and no measurement here was
built to reach that arm, so it is labelled unmeasured.

---

## 6. THE SCOPE — settled, and it EXCLUDES both of §1's test cases

§1 above says the lift "needs `sub_417070`". **That is refuted.** The
obstruction pass is gated on camera flag `4` (the section) and flag `8` (the
arm), and the flags are rebuilt from scratch on every camera change:

* `Camera_LoadParams` (0x004146C0) ends `+356 = 0; +356 |= 1`. Every camera
  starts with flag 1 and nothing else.
* `Camera_Request` then `memset(cam + 208, 0, 0x94)` — which clears `+300`,
  `+312`, `+316`, `+320`, `+324` and `+328` as well as `+208` — and calls
  `sub_414520`.
* `sub_414520` (0x00414520) is the ONLY thing that puts flag 4 or 8 back, and
  it dispatches on the mode `+12` and then on the EYE subject `+140`:

| route | flags set | reached when |
|---|---|---|
| `sub_413C00` | `4 \| 8 \| 0x10` | mode 0, or mode 12/20 with **eye subject 0** |
| `sub_413CD0` state 13 | `4 \| 8 \| 0x1000` | same, player on a ladder |
| `sub_413CD0` states 11/14 | `4 \| 0x4800` | same, player swimming — **not bit 8** |
| `sub_4141F0` | `4 \| 8` | modes 8/9/10, or mode 12/20 with **eye subject 5** |
| `sub_414100` | — | eye subject 6 |
| **nothing at all** | flags stay `1` | **any other eye subject, `-1` included** |

So an ABSOLUTE camera — both subjects `-1` — leaves `sub_414520` with flags
`1`, the tick's `if (flags & 4)` is false, and no collision pass of any kind
runs. It is also the only consistent reading: the `memset` has just set `+300`
to **zero**, and `sub_417070` divides by `+300`.

**And a dialogue camera is excluded twice over.** `Dialog_ApplyLineCameras`
(0x004013B0) follows each `Camera_Request(12, ...)` with, in the raw listing,

    push ebx / push 2 / push ecx / call sub_4137D0      ; flags |= 2
    push 0   / push 4 / push edx / call sub_4137D0      ; flags &= ~4

— it CLEARS flag 4 explicitly, on both of the two camera ids it issues.

### 6a. Measured against the two cases

* **dialog 387: all 44 cameras are `subjects -1 -1`** (`build/dlgcam
  <gamedata> 387`). Absolute, and flag 4 cleared by the issuer. The crane
  4194 -> 4195 cannot reach `sub_417070` in the original engine.
* **camera 2986, the lift's arrival: `eyeSubject -1, atSubject -1`**
  (`build/dump_world_cameras`, AREA 157 row). Same.
* over the whole world-camera table, 5381 records: `(-1,-1)` 3941,
  `(-1, 0)` 959, `(0, 0)` 406, `(1, 1)` 38, `(-1, 1)` 32, `(9, 9)` 4,
  `(3, 3)` 1 — printed as `(eyeSubject, atSubject)`. **Only the 406 with eye
  subject 0 take the pass** — and those are exactly the follow-camera cameras
  the port already routes through `PlayerController`. Eye subject 5 and 6
  never ship.

### 6b. What this says about the three reverted attempts

Attempt 1 put the rule on every camera and attempt 3 scoped it to "follow and
dialogue". Both included cameras the engine excludes, which is why the reader
saw shots that had been right move — the rule was not wrong, it was being
asked about cameras the engine never asks it about. Attempt 2's `+328` was a
separate, real fault (§2), and it stays fixed: `+328` lives in the camera
block and the blocks alternate, so it is per camera by construction.

### 6c. So what DOES clear the lift's lens?

Not this function. §6b of `todo/missing-ui.md` listed three candidates and
called `sub_417070` one of them; this closes that one NEGATIVELY, which leaves
the third — the engine not drawing the car that is not in use. The first
(inside-face culling) was already refuted by `SetRenderState(22, 1)`.

`CSPont04` is one of a stack of eight `CSPont` meshes, one per level of the
shaft, and the camera at every level sits inside the one at that level. That
is the shape of a mesh the engine shows and hides, and it is where the next
read should go: what `Area_LoadSet` / the scene program does with the `CSPont`
family, and whether `lev-2.SCX`'s own program hides it. **Nothing here
establishes it** — it is the surviving candidate, not a finding.

The measurement in §1's table stands and is worth keeping for whoever takes
that up: the lens is 100% blocked at the authored 117 units and 26% at 35, so
the geometry in the way is close, and a renderer that draws it will draw a
black frame however the camera is placed within a few tens of units.

### 6d. Measured, so the negative result is on the record

With the port as it stands the §1/§4 repro gives **90.3% dark** (277445 of
307200 pixels at R+G+B <= 24), and the run's own tail says
`last camera 2986` — an absolute world camera, no `PlayerController`, so
nothing in the obstruction family is even in the path. Fixing the four faults
in §5d moves that number by **0.0 points** — the same 277445 pixels — which is
the correct outcome and not a failure of the fix.

### 6e. The rest of the measurements, 2026-09-18

* **dialog 387, frame by frame.** Reached headlessly from
  `traces/games-resto.bin` slot 2, standing in zone 3732
  (`--stand 2547,22,-6930,314`, action at frame 30, NEXT every 120 frames):
  2859 dialogue frames, 22 distinct camera pairs, the crane 4194 -> 4195
  among them. Two identical runs differ in 0 frames (determinism), and the
  pass switched off entirely (`OMK_NO_CAM_COLLIDE=1`) against on also differs
  in **0 frames of 22 shots** — so no version of this pass can move a
  dialogue shot, which is what §6 predicts.
* **the follow camera, AREA 46's sixteen views.** 11 never block, and their
  eye and target are **bit-identical** with the pass off (max difference
  0.000 over 119 frames each). 5 block, as `engine: camera collision` has
  always counted. On the deepest pinch (`6980,30,880,0`) the eye rises to
  **93.3%** of the pushed height over ~20 frames and holds there — a smooth,
  monotone curve, frame 1 being flag 1's snap. With the ease anchored on
  in-frame values (the code before this task) it settles at **60.3%**.
* **the walk.** The lift, `k*40,k200*300` after the arrival: **377.7 units**,
  the §6a figure unchanged. AREA 46 with forward held for 360 frames:
  `6980,30,450,180` walks 389.7 and `7100,30,600,0` 82.7, **identical** with
  the pass on and off; `6980,30,880,0` walks 0.0 either way, because that
  stand faces him into a wall (the pass cannot be what stops him — it is off
  in one of the two runs).
* `engine: camera obstruction` asserts the scope and the two follow-camera
  facts, and was shown to fail on that 60.3% mutation.
