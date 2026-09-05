# The standing unknowns, 2026-09-05

Picked up after `todo/reader-followups.md` closed. Ordered smallest and
best-evidenced first, so each lands as its own commit and the research one
does not block the rest.

| # | item | state |
|---|---|---|
| 1 | the camera pass's three unmodelled details | **DONE** 2026-09-05 |
| 2 | the shoot AI's brains are not called | **open** |
| 3 | `.3DM`'s `float[3]`, and node slots 0 and 1 | open |
| 4 | the Anekbah panel FLICKER | open |
| 5 | the player's RIDE | open |

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

## 3. `.3DM`'s `float[3]`, and node slots 0 and 1

The parser's integration is read and asm-confirmed, but the corpus refutes
"root-motion deltas" as playable semantics - near-constant near-unit in 57/60
files, so integrating it drifts every character off the map - and whatever
neutralises the integral in the engine is untraced. Slots 0 and 1 are
narrowed: uploaded with preamble ids 0/1, bound by no drawn mesh, not
rotations, and NOT the voice envelope (|r| < 0.2 against per-frame RMS in four
files). Slot 0 stays in [0,1]^4 and varies smoothly; slot 1 is a signed
low-magnitude 4-vector with one dominant component.

## 4. The Anekbah panel FLICKER

The WRONG TEXTURE half is solved - the 58-slot name cache, `verify.py:
texture name cache` - and the flicker is not. The `AApub*` prism (7 vertices,
3 quads of identical UVs) with `D3DCULL_NONE` and no depth bias is the
standing account. The neon half was WITHDRAWN: 148 of Anekbah's 153 emitters
have period 0, a particle every frame, which is a steady glow.

## 5. The player's RIDE

The road traffic is ported - the sliders and motos on the vehicle lanes - and
the player mounting one is not. `ACTOR_STATE` 7 and 8 are the mount and the
ride of one slider (`MDSLIDOU` refuses to dismount from anything but 8), and
7 has no case in `Actors_TickAll` at all.
