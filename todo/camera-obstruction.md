# The camera obstruction pass — ONE mechanism, TWO symptoms

Measured 2026-09-18. `sub_417070` has been read (`todo/missing-ui.md` §7) and
ported three times, each attempt reverted the same evening. This file adds the
thing those attempts did not have: **a second, independent test case**, and an
objective metric that needs no play report.

---

## 1. The lift arrival is the SAME fault as the restaurant dialogue camera

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
