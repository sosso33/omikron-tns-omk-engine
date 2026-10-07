# HANDOFF — the slider (call, ride, exit, cameras), 2026-10-06

Read this first to pick up the SLIDER work. The full audit, with every row's
original address and port file:line, is `todo/slider-drift-audit.md`; the
older design record is `todo/slider.md` (long — grep it, read only the
section you need).

## Where it stands

The slider was audited against the original on 2026-10-06 (four read-only
agents, one per phase) and most of the audit was fixed the same day, each fix
in its own commit with a check shown to fail on the old code (a mutation per
commit, then restored with `touch` + rebuild). The commits, in order:

| what | commit |
|---|---|
| vehicle `+60` is `2r` (queued sliders no longer inside each other) | `e6ff780` |
| B1 a call after a journey uses the vehicle it spawned | `e4e7d9f` |
| A5/B4 a journey's slider leaves as he stands; an ignored one after 600 frames; MDACTION's bit-4 gate | `71069b3` |
| A2 the coming camera behind the slider (rows, not the pool's mirrored yaw) | `93fd464` |
| M2 Manuelle nose first (+180, `sub_457270`) | `cadbeaf` |
| A8 no run-over on his own ride | `9cb7515` |
| B5 a refused call keeps the sneak up, string 42 | `2181e2c` |
| A1 the rider hidden while seated | `d11d0cf` |
| B3 the SNEAK'S OPEN forgets the remembered destination (`Ui_OpenSneakFamily`) | `6942ef9` |
| M1 + the exit: Manuelle's stop through `sub_468FA0`; cameras 9/10/17/0; MDSLIDOU's facing | `5d4b439` |
| A4 the call's hold and black-fade bands | `d875956` |
| D1 a check whose journey drives; `--address-enable` | `eaef82e` |
| B6 (dormant) + B9 a load/restart drops the ride | `62715df` |
| M4 Manuelle's slider at the ride's height | `3d3c5f1` |
| B7 boarding is MDACTION's slider arm, after the take | `dbb2a03` |
| M6 the skid recovery gate | `1a4958b` |
| B8 no call from inside a building beside the city | `ff51bc1` |
| B10 MDSLIDOU's input-queue reset | `c7a0242` |
| PLAY REPORT 4: a call arriving on its first tick left him held under the bands; a reused slot drew the slider as a moto | `49dee1a` |
| PLAY REPORT 2: Kay'l too low in the seat while the door clips play | `307e537` |
| PLAY REPORT 1 / B12: the slider cameras chase (dt/8), no more stutter | `292542e` |
| B2 `sub_452CC0` transcribed: carrot at the origin, body 39 back (was 156), speeds reset, the scan swaps or sets back and never kills | `git log --grep 'B2)'` |

The checks: `python3 tools/verify.py --jobs 4 --only "engine: slider"` — 17
checks, all green at the last run, plus `engine: city return`, `engine: road
traffic`, `engine: run over`, the sneak family, the pause/restart/load panel.

## The reader's play report of 2026-10-06 (17:36), and what came of it

1. **"the camera stutters sometime when following the slider"** — FIXED
   (B12): mode 8 was rigid on a heading that switches at once at a lane change,
   the eye swinging 112-194 units in one frame. Now the `sub_415E60` chase.
   NOT YET PLAYED.
2. **"Kay'l position is a bit low when he sits in the slider"** (screenshot:
   door open, his legs below the hull) — FIXED: the door clips' root descent was
   drawn as a drop on top of moving his position (`rootDrop` +11.5..11.9 during
   H_SLDIN/H_SLDOUT); an older fix for exactly this had been undone by the
   looping-clip rule. NOT YET PLAYED.
3. **A screenshot of the slider driving away from the camera with Kay'l
   STANDING in an open cockpit** — NOT REPRODUCED. In a headless Manuelle run
   (Appel du slider -> board -> Manuelle -> UP) he is hidden and the slider is
   drawn closed (`--dump` checked by eye). The build he played had the hiding
   fix (`d11d0cf`, 14:52). ASK THE READER how that frame was reached (Manuelle?
   a journey? after stopping and re-boarding? the `--ride` harness?). Candidates
   to look at: `seatedHidden = boarded && !leaving` in
   `playframe_world_scene.cpp` (a `leaving` left true would show him), and the
   `--ride` harness, which draws the rider by design.
4. **"went to Kay'l's apartment from Tahira's restaurant, then called a slider
   to go to Jenna's: the cutscene but without any slider on it, and no slider
   at the end"** — FIXED (`49dee1a`): the second call arrived on its first tick,
   the viewer never saw state 2 and never released the hold/bands; and a reused
   vehicle slot could draw the slider as a moto. `engine: slider second call`
   replays the route. NOT YET PLAYED.

**Next for whoever picks this up: ask the reader to play 1, 2 and 4, and how 3
was reached.**

**The reader played them (2026-10-06, after `3dce361`)**: 1 (the camera) OK;
2 (the seat) "better but it looks like it is a little too high" - look in the
original, leave it if nothing is found; 4 (the route) OK; B2's new stop point
OK. And 3 was reached like this: *"I called the slider and then I just walk
through it, and, if I go to the center, I have the text I have when I
collide with people - it looks like the slider has the colliders and the
triggers of a pedestrian"*. So the standing-in-the-cockpit frame was him
WALKING INTO the parked slider, not a seated rider shown.

  * **The collider - FIXED** (`engine: slider collider`, new): the index was
    filled once at the area load, so a called slider spawned later had no
    entry, and one taken over and rebound to `sli_fn` (every call in
    Qalisar, whose traffic is all motos) kept the moto's spheres - reach 43.7,
    no push 30 across its centre or 70 along it. Now every mover's entry is
    kept to its model each frame (dead movers' entries removed), and a touched
    VEHICLE posts no bump (`Sliders_Tick` walks only the walkers). NOT YET
    PLAYED.
  * **The seat - read, LEFT AS IT IS** (the reader's instruction). `MDACTION`'s
    snap is from the BODY (`sub_438310` reads mover `+36`, road level), pelvis
    at body - 33.15 + the slf_112 offset - the port's; H_SLDIN's descent moves
    his position once (`307e537`); the slider's own door clips carry no root
    motion (0 over 72 and 51 frames), so the hull does not settle either.
    **The hull is not too low** (the reader's second question): a root mesh's
    `local` is (0,0,0) and `sub_453A70` only collects and sorts the roots, so
    SlBassin's origin is ON the node, at body - 30.75 - what the port draws.
    **And the last frame is not compared against a seat**: `sub_457F50` (rider
    at the ride's y + 10, body at ride y + 33.15) is `Slider_TickRide`'s -
    MANUELLE's flight only - not a journey's; a called slider's rider goes
    hidden at MDSLIDIN and is placed by nothing visible. (This handoff first
    set H_SLDIN's end, pelvis ~24 above the road, against that node + 10 and
    called it a 9-unit mismatch; it was two different rides.) Both ends of
    H_SLDIN are start + the summed root deltas in the original and the port
    alike. Nothing found; LEFT, as the reader asked.

**The reader, 2026-10-06 (after `1b64713`)**: *"when Kay'l leaves the
slider, he is a bit too high, so he falls when the animation is finished"*.
(The journey route's log already showed it: `the player LANDS ... dropped
11.8 (0.30 m)` right after MDSLIDOU.)

**FIXED** (`engine: slider journey` re-aimed, shown to fail). Two readings,
both from `sub_45C680` and `Actor_PlayClip`:
* in ACTOR_STATE **6 and 8** the clip's root delta goes to the actor's x and
  z only (+244/+252, +232/+240); the **y moves only the NODE**
  (`o3de_MoveNodeBy`), and `Actor_PlayClip`'s `o3de_SetNodePos(+244..+252)`
  snaps the node back at the next clip. The port moved his position in y
  (`307e537` chose that over a drawn drop: the right total, the wrong owner);
* `Actor_PlayClip` ZEROES the previous frame (+192), so a clip's first delta
  is (0, frame] and includes **key 1** - and H_SLDOUT's key 1 is its whole
  drop into the seat, **+12.49** (net over the clip +0.68). The port measured
  from the start frame 1.0 and lost it: seated pose at standing height, then
  the stand-up's 13 more, then the 11.8 fall.
Now `PlayerController::nodeDrop()` carries the y and the viewer draws it;
`primeClipEntry()` applies key 1 on the frame the clip is set, as the
original's same-frame channel tick does (`Sliders_Tick` runs before
`Actors_TickAll`). Measured: position y 5.87 throughout, H_SLDOUT drawn
+12.49 from its first frame, +0.68 at its last, then H_STAND on the road
(+0.01), no landing. NOT YET PLAYED.

**SETTLED 2026-10-06 (the reader asked)**: the same rule holds for EVERY
clip change - `Cef_TickChannel` either ticks the clip or returns `GoToMove`,
which calls `sub_45C680(startFrame)` after `Actor_PlayClip` /
`Actor_BlendToClip` zeroed +192, a looping clip's end included - so the port
now applies (0, start] of the new clip on any tick whose transition count
moved (x and z zeroed on a seek, `CefChannel::lastSeek`), in every state.
Measured: key 1 is a pure VERTICAL offset in all of Kay'l's clips (47 of 81
over 0.5) and has x/z in 7 clips of other characters, never over 0.5 - so
where he STANDS does not move (a 300-frame walk identical to 0.01; 82 of the
movement checks unchanged, `shoot hit` red on purpose and identical on the
old code), and the DRAWN side already agreed (looping clips read their
summed y absolutely; the take's accumulator carries chained clips that are
authored to meet, H_TAK031's +19.58 into H_PUT032's key 1 of +19.58).
`docs/ASSETS.md` (the clip section) has it.

## What is left (the audit's order)

- **M3** Manuelle's collisions - DONE 2026-10-06 in three steps, PLAYED AND CONFIRMED by the reader 2026-10-07 ("Tested, good"; the instruments, should it need re-checking: drive Manuelle into a parked slider, off the road's edge, into a building; the viewer logs `Manuelle HIT a vehicle`, `SLIDES along a wall` and a `slider ride at` line every 30 frames naming the surface):
  1. DONE: `sub_458880` the vehicle push and `sub_458490`/`sub_459970` the
     walker on a crossing (`SliderRide::hover(dt, RideWorld)`; the viewer
     fills the world with the named-surface probe, the vehicles, the walkers;
     `PlayState::soupMeshName`). NOT PLAYED.
  2. DONE (NOT PLAYED): `sub_458C70` (466 lines): four hull corners at +-60.37 (fore/aft and
     across, 20 above, led by the velocity: x0 when |speed| <= 18, -v at
     18..32, -2v above) probed for ROAD; where corners disagree
     `sub_459810` bisects the hull edge (2 halvings, probes 39.37 down) for
     the crossing point; the slider is pushed back along the edge normal
     (2.5, the `flt_8F5D80/84` impulse damped 1/8 a frame afterwards),
     `dword_8F5DF0 = 8`, `settle = 0`, the yaw turned toward the edge by
     `dword_8F5E18` clamped +-2.5. All four off-road: back by -2v and a
     quarter of the speed. Decompile has two undefined flags (v17, v21)
     - read the asm at 0x4594D0 / 0x459580.
  3. DONE (NOT PLAYED): `sub_459BD0` (312): the WALL pass - the four hull edges (+-60 on the
     node's own axes) cast both ways with `sub_444810` (the port's
     `WorldRay`), the hit pair chosen, the slider moved half back out along
     the wall, the velocity laid along it at 95% of |speed|, the yaw nudged
     +-2 (`dword_8F5E18`). Two undefined flags (v19/v20) at 0x45A365.
- ~~**A9**~~ DONE 2026-10-06 (`engine: vehicle sound`): one looped
  `sliderm01.wav` per vehicle within 585 of the camera's eye, gain 39/d.
  NOT PLAYED - listen for it while the called slider comes (the camera
  follows it ~300-400 behind, so it is quiet: 39/304 = 0.13) and for passing
  traffic. Left: Doppler (the host mixer has no pitch), and the host mixer's
  cap of 8 shots against the engine's 16 voices - a busy street can push the
  oldest out.
- ~~**B2**~~ DONE 2026-10-06 (`engine: slider placement`). Left from it: a
  FULL pool with nothing in the way relinks an ambient vehicle, where the
  engine uses its reserved slot-0 mover. ~~The cross-area arrival~~ DONE
  2026-10-07: `sub_40E630` loads synchronously, `sub_4541E0` / `sub_4544B0(0)`
  carry the slider across, and `sub_452CC0` places it in the new pool as in
  the same area - `arriveAt` no longer parks it at the pickup or kills
  (`engine: slider journey area`, 40 live). NOT PLAYED. NOT PLAYED: a call now stops 30 units short of
  where it did (1274 -6644 against 1304 -6651 on the checks' route).
- ~~**B12 rest**~~ DONE 2026-10-07, NOT PLAYED: mode 8 (the coming slider
  and Manuelle) runs `sub_417070` with `sub_4141F0`'s pushes, and Manuelle's
  camera chases as the coming camera does. Modes 9 and 10 have no wall pass
  in the original (`sub_4141F0` clears flag 4 for 10, skips 9).
- ~~**M5**~~ DONE 2026-10-07, NOT PLAYED: while Manuelle flies, the actors'
  shadows are not cast and their head look-ats hold - the parts of
  `Actors_TickAll` the port has; the scene extras keep moving, as in the
  original (their programs run before the branch).
- ~~the 11-degree bank~~ DONE 2026-10-07 (`engine: slider bank`), NOT PLAYED: the hull leans INTO the turn (+X, the rider's left, rises steering right; laid beside an unbanked frame).

## Instruments added on the way

- `veh_probe --recall | --runover | --defer` (the call after a journey; the
  run-over gate; the arrival deferral).
- `omk-play --board-after N` (board N frames after the slider opens),
  `--address-enable N,...` (open a destination the save has not).
- `slider_door`'s `root turn` lines; `slider_fly`'s `skid` row.
- Log lines the checks read: `slider camera N requested over F frames (from
  M)`, `slider cam N frame ...`, `slider: come camera ...`, `slider: manual
  ride ... the drawn node X over the ground`, `player HIDDEN/SHOWN`, `MDSLIDOU:
  his facing`, `vehicle slot N restaged`, `DBG ply` (`OMK_PLY`) rootDrop.

## Traps met

- **Headless timing is not play timing.** A `--frames` run steps a fixed 1/30
  whatever `--framerate` says; use `--speed 0.5` to get a 60 fps step.
- **A state compared between frames misses a one-tick state**: the pool's
  events (`takeCameNotice`, `takeReleasedNotice`) exist for that.
- **A check edit script that dies before writing leaves the file untouched**
  while the check run that follows still passes — assert the edit landed.
- **A log regex against padded numbers** (`rootDrop  -0.00`): use `\s+`.
- The tree is SHARED with another session (3DS work); commit only this
  work's files by path.
