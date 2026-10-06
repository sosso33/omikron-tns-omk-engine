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

## What is left (the audit's order)

- **M3** Manuelle's collisions with vehicles and road-keeping (`sub_458880`,
  `sub_458C70`, `sub_459BD0` — the last unread). Large.
- **A9** no vehicle sound (`sub_456B40`, `sliderm01.wav` looped within 585).
- ~~**B2**~~ DONE 2026-10-06 (`engine: slider placement`). Left from it:
  the CROSS-AREA arrival (`arriveAt`) still puts the slider at the lane
  POINT and kills what stands there (`takeOverAt`) - its original path
  (`sub_4541E0` / `sub_4544B0(0)` after the load) is unread; and a FULL pool
  with nothing in the way relinks an ambient vehicle, where the engine uses
  its reserved slot-0 mover. NOT PLAYED: a call now stops 30 units short of
  where it did (1274 -6644 against 1304 -6651 on the checks' route).
- **B12 rest**: the wall pass (`sub_417070`) for the slider modes; the
  Manuelle ride camera is still rigid (its heading changes smoothly, so less
  visible).
- **M5** NPCs frozen while Manuelle drives — read, not ported (no single
  `Actors_TickAll` in the port).
- the 11-degree bank into the node matrix (`placeCalled` draws yaw only).

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
