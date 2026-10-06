# The SLIDER drift audit — 2026-10-06

READ-ONLY. Where the port's slider can behave differently from the original:
the call, the wait, getting in, the journey, *Manuelle*, getting out, and every
camera on the way. Asked by the reader after `e6ff780` (the vehicle `+60` was
the walkers' `r/2`, not `2r`). Four read-only agents each took one phase, read
the original in `readable/src` and `asmfn`, and ran the built `omk-play`
headlessly on `engine: slider journey`'s `--board` route. Logs from the runs
are in that session's scratchpad and are NOT kept. Two single-source claims
were re-read by hand before this was written: S-B1 (the called slot) and M2
(the +180).

Each row gives the ORIGINAL (address), the PORT (file:line at `e6ff780`), what
a player sees differently, the confidence, and a size. Rows are ranked by
exposure: **A** is every slider use, **B** is sometimes, **M** is *Manuelle*,
**D** is checks and docs. Nothing here is fixed yet.

## Status — 2026-10-06, the cheap certain ones DONE

Each with a check shown to fail on the old code (a mutation per commit):

| id | commit | check |
|---|---|---|
| B1 the wrong vehicle after a journey | `e4e7d9f` | `engine: slider recall` (new; `veh_probe --recall`) |
| A5 a journey's slider leaves as he stands; B4 the 600-frame give-up, the bit-4 gate | `71069b3` | `engine: slider journey` (release 1-2 frames after MDSLIDOU), `engine: slider arrives` (case 1's hand-back), `engine: slider call` |
| A2 the coming camera behind the slider | `93fd464` | `engine: slider arrives` (eye 270-282 behind, within 5 across) |
| M2 Manuelle nose first, the ride camera behind | `cadbeaf` | `engine: slider manual` (new; Appel du slider -> board -> Manuelle, the real path) |
| A8 no run-over on his own ride, the on-road flag off in mode 6 | `9cb7515` | `engine: slider runover` (new; `veh_probe --runover`) |
| B5 a refused call keeps the sneak up, string 42 | `2181e2c` | `engine: slider refused` (new) |
| A1 the rider hidden while seated (the reader: the door shuts over him) | `d11d0cf` | `engine: slider journey` |
| B3 the sneak's open forgets the destination | `6942ef9` | `engine: slider forget` (new) |
| M1 Manuelle's stop through `sub_468FA0` (no longer stuck), the slider put back where the drive began; A7 camera 9 blends and holds; A3 a journey ends on camera 10; B11 camera 17's fixed eye and 6.00 m release; A6 MDSLIDOU's facing | `5d4b439` | `engine: slider manual` (drives to the stop and walks away), `engine: slider journey` (the camera request chain), `engine: slider journey area` |
| A4 the call's hold and black-fade bands, released at the arrival / H_SLDOUT's first tick; `--board-after N` | `d875956` | `engine: slider arrives` (UP held while it comes: walked 0.4), `engine: slider journey`, `refused`/`forget` re-timed past the arrival |
| D1 a journey that drives (Jaunpur -> Tetra, 75 frames); `--address-enable N,...` | `eaef82e` | `engine: slider journey drive` (new) |
| B6 the arrival deferral (0x10 -> 0x400) - transcribed, measured DORMANT: `veh_probe --defer`, 81 calls at every vehicle lane's end in three cities, 0 deferred, 0 on a connector (a call relinks at the top of the pickup's own lane); no check can fail | `62715df` | - |
| B9 a load or restart drops the ride, the boarding, the slider camera and the hold (`sliderForget`) | `62715df` | `engine: slider restart` (new) |
| M4 Manuelle's slider at the ride's height (it flew 61.5 over the road) | `3d3c5f1` | `engine: slider manual` (the drawn node 20-45 over the ground) |
| B7 boarding as MDACTION's slider arm, after the take, before the zone press | `dbb2a03` | none discriminates (needs an object in reach of a door); the families green |
| M6 the skid recovery only with no thrust key, the slow skid cancelled | `1a4958b` | `engine: slider fly` (`skid` row; `none` re-pinned -0.120 with its reason) |
| B8 no call from inside a building beside the city | `ff51bc1` | `engine: city return` (inside refused, the street accepted) |
| B10 MDSLIDOU's input-queue reset (the ground probe left to the walker) | `c7a0242` | none can tell it apart |
| M5 `Actors_TickAll` not run while Manuelle drives - READ AND CONFIRMED (05_sys.c 2170), NOT PORTED: no single counterpart in the port (scene clocks, zone scan, head aim, shadows, effects are separate calls); exposure: scripted extras pause while you drive | - | - |
| PLAY REPORT 2026-10-06: a call arriving on its first tick left him held; a reused slot drew the slider as a moto | `49dee1a` | `engine: slider second call` (new) |
| PLAY REPORT: Kay'l too low in the seat during the door clips (rootDrop drawn twice) | `307e537` | `engine: slider journey` (rootDrop 0 on H_SLDIN/OUT) |
| B12 (part) the slider cameras chase their place (modes 8 and 10, `sub_415E60`) - the reader's stutter | `292542e` | `engine: slider arrives` (largest eye step < 45) |
| A9 the vehicles' engine sound: `sliderm01.wav` looped per vehicle within 585 of the camera's EYE (the listener, `sub_46D080`), re-placed from the drive step, stopped beyond; gain 39/d (DirectSound's documented law, labelled); no Doppler | `git log --grep 'A9)'` | `engine: vehicle sound` (new) |
| M3 step 1 (of 3): `sub_458880` the vehicle push - the first vehicle `sub_4583D0` overlaps (box 1.5r, then sqrt(dx^2 + 2dz^2) < 1.5r), pushed out to the two ellipses' edge ONLY onto road (`X` / `OP`), velocity bounced by 2/3 of the closing speed, speed re-signed against the nose, `settle` (8F5E08) 40, the vehicle hit handed its speed (0 -> 7, `sub_4382D0`); and the OP damping gated on `sub_458490` (a WALKER within 300 ahead on an `O` mesh) with `sub_459970`'s side-step - closing M6's OP note. Left: `sub_458C70` (road edges), `sub_459BD0` (walls) | `git log --grep 'M3 step 1'` | `engine: slider fly` (collide / offroad / crossing rows, two mutations red) |
| M3 step 2: `sub_458C70` the ROAD EDGES - four hull corners (+-60.37, 20 up, led by the velocity: none to |speed| 18, -v to 32, -2v above) probed for `X`/`OP`; all on road: the edge shove `flt_8F5D80/84` damped 1/8, the turn `8F5E18` 1/16; all off: back by 2v, speed x0.25; else `sub_459810` (four halvings 39.37 down) finds the boundary on the disagreeing hull edges (the end edges for the second point, the decompile's selection kept), the ride shoved 2.5 off its normal (the motion turned off it, or with the centre off-road a step back and x-5 / x-10 by |speed| 30), turned along it at a rate clamped +-2.5, noThrust 8, settle 0. Flags read from 0x4594D0 / 0x459580 | `git log --grep 'M3 step 2'` | `engine: slider fly` (`road` row; red with the edges off: x 5741 off the road); `engine: slider ride` re-pinned -3238 -> -1795 (the harness rides from a pavement onto `YX`, not road) |
| B2 `sub_452CC0` transcribed: carrot at the lane origin, body 39 behind (not 39 + 117), speeds 0/256, flat heading, the route drawn twice; the 40-slot scan SWAPS a vehicle in or sets back behind one, never kills (`takeOverAt` kept for the cross-area arrival only) | `git log --grep 'B2)'` | `engine: slider placement` (new; `veh_probe --place`); `slider arrives`/`journey` re-pinned to the new stop 1274 -6644, `slider recall` now asserts 0 killed |

The port's mode order is now the engine's: case 2 -> **1** (open, bit 4,
600 frames) -> MDACTION sets **3** -> MDSLIDIN **4** -> a journey's case 6
-> 4, `sub_468FA0` **5**, MDSLIDOU **7** -> case 7 at once (no 0x200) or
after 300-and-in-front (Manuelle). M1 and the exit are DONE (`5d4b439`); A4 DONE (`d875956`); D1 and the small rows DONE (M5 read, not ported); left: M3 (Manuelle's
collisions and road-keeping, L), B12 (mode 8's lag and wall pass), A9
~~(vehicle sound)~~ (DONE 2026-10-06), ~~B2~~ (DONE 2026-10-06), M6's bank into the node matrix,
M5.

**B3 DONE, and the reader was right** (`engine: slider forget`). The
reset of `dword_6A17CC` read as "new game" is the OPEN CALLBACK of the
sneak-family screens, `Ui_OpenSneakFamily` (0x0049B400, slot +20 of
screens 0, 7 and 9), in its parameter-0 arm - the SNEAK's. So every time
the sneak opens, the remembered row is forgotten: a destination is kept
from its row to the boarding and no further, and "Appel du slider" (which
never writes it) always boards into the menu - destination or Manuelle.
The port cleared it at the dismount instead; it now clears it where the
sneak opens. (A first reading this same day took the reset for the
new-game path because of the setek/anneau previews beside it - those are
the sneak's own inventory previews.)

## Settled on the way: the camera-request block

`sub_4187B0` adds dt to a counter until it reaches `dword_930818`, so
**`dword_930818` is a BLEND DURATION in frames**, and 0 means a cut.
`1114636288` = `0x42700000` = **60.0**. `sliders.h:190` and `todo/slider.md:165`
call it 56, which is wrong. `dword_930820` is the blend type; 0 keeps the
source camera ticking through the blend. In state 6 the arrival blends over 60
only if the current mode IS 8, and cuts otherwise (`dword_930818 = 60; if (mode
!= 8) dword_930818 = 0`).

## A — every slider use

| id | what | original | port | size |
|---|---|---|---|---|
| A1 | **The rider is drawn while seated; the original HIDES him.** He is hidden from the end of `H_SLDIN` to the start of `H_SLDOUT`. The port also turns him about 75° when he sits, because he takes `calledYaw()`, the pool's mirrored convention, where boarding drew `atan2(-row2.x,row2.z)` | `MDSLIDIN` 0x0046B7F0: `push 8; push [esi+8]; call sub_436E70` (`o3de_DisableObject`, bit 0x40 on the node tree); `sub_457040` does the same; `sub_468FA0` re-enables | `playframe_control_adventure.cpp:2316-2342` hides nothing; `drawPlayer` (`playframe_world_scene.cpp:791`) excludes only shoot; seated facing ~:1370 | S. **Contradicts the played picture: confirm against the original game first** |
| A2 | **The "coming" camera (mode 8) is MIRRORED** by the slider's heading. It is correct along −X, about 68° in front of the slider at heading (−0.58, 0.81), and level with it at (0.79, 0.61). Measured. The subject height is also probably 78 cm low (`m.body` is the road, not the node) | preset 8 eye (0, 118.11, −275.59) on subject 5 = the slider node, placed by its matrix rows | `playframe_world_scene.cpp:306-345` rotates by `calledYaw()`. The same branch serves state 6 and seated state 4; `--ride`/Manuelle (`:346-376`) is suspected the same | S: place from `calledFrame` rows, as the boarding camera already does |
| A3 | **Mode 10 is never implemented: a journey's arrival shows camera 17.** The original shows a wide side shot, 8 m beside the slider and 4 m up, looking at the destination's address. Its side is flipped by the address's bearing (`sub_4141F0`). `MDSLIDOU` then returns to mode 0 over 60 frames | `sub_456530` case 6 → `Camera_Request(10, {sub_40E630(dword_6A17CC), slider})`; mode 17 is requested ONLY by `sub_4570F0` (the manual stop) | `playframe_control_adventure.cpp:1446-1456` requests preset 17. `RideMachine::camera = 10` (`vehicles.cpp:568`) is never read | M |
| A4 | **No hold, no bands, no glide back, on a call or a journey.** The original freezes the player and shows the bands while the slider comes. On arrival it travels back to him over 60 frames. The port leaves him walkable and blind, shows no letterbox, and cuts back | `sub_452570`: `Screen_Fade(1); Actor_HoldAnimation(player,1)`; case 2's arrival: `Camera_Request(0)` with 60.0, guarded `mode != 0 && != 17`, then `Screen_Fade(0)` and release | `RideMachine::tick` sets `fadeIn`/`released`, which nothing reads; `startBlackFade` is never called on the slider path; `playerCamRequest` is not used on arrival | M |
| A5 | **After an AUTOMATIC journey the slider should leave at once.** The port waits until the player is 300 units away and in front of it. Measured: parked from frame 559 to 1434 | `sub_456530` case 7: `if ((v3 & 0x200) == 0) { mode 0; ...; dword_8F5E44 = 0 }`. Bit 0x200 is set only by `sub_457040` (Manuelle) | `vehicles.cpp:573` (and 247-280) always runs the 300-and-in-front test | S: carry the manual bit |
| A6 | **`MDSLIDOU` writes his facing; the port never does.** His heading pops at the end of `H_SLDOUT`, he walks off on the mirrored seat yaw, and A5's "in front" test reads that wrong facing | `MDSLIDOU` 0x0046B890: `sub_440C80(node)`, `sub_442D70(0,0,1,node+5Ch,...)`, `fpatan`, `fstp [esi+1A4h]`, then `sub_437140(node, actor+120h)` | `playframe_control_adventure.cpp:2343` calls only `clearRootFrame()`; the comment at :2389 "+420 never written" is wrong for this move | M |
| A7 | **Camera 9's 60 is a BLEND, not a hold.** Measured: a one-frame pop at frame 506, a cut into preset 9, the follow camera from frame 568 while he is still climbing in, then the mirrored mode 8 while seated. The original blends in and holds mode 9 until the next request | `MDACTION` requests 9 with 60.0; nothing requests another camera through `H_SLDIN` or seated state 4 | `boardCam = 60` (`:471`) counted down (`playframe_world_scene.cpp:328-338`) | S-M |
| A8 | **The run-over is not suppressed for the player's own ride.** Passing traffic, or his own slider during a driven journey, can raise H_IMPACT (−15 Vie) while he boards or rides | `sub_456C70`: `v10 = 0` while the called slider is in states 3..6; case 3 latches `dword_538E20`; every arm re-arms `flt_536C28 = 90`; `Sliders_Tick` turns the on-road test off in state 6 | `vehicleDrive` (`vehicles.cpp:345-347`) has no state gate; `kLatchFrames` is declared and unused; the comment at :1278 admits it | S |
| A9 | **No vehicle sound at all.** The original loops `sliderm01.wav` in 3D within 585 units, with a velocity term | `sub_456B40` (`word_4C8894`) | `vehicleSound` books `v.sound` and nothing reads it | M |

## B — sometimes

| id | what | original | port | size |
|---|---|---|---|---|
| B1 | **After any journey, the next call can take the WRONG vehicle**, and the coming camera follows it for as long as it is in state 2. **Re-read by hand.** | `sub_452570` takes the reserved slot | `vehicles.cpp:626-633`: after `spawnVehicle` (which fills the FIRST dead slot) it sets `called_` to the HIGHEST live state-0 slot. Slots die in `takeOverAt`, which every journey runs | S: make `spawnVehicle` return its slot |
| B2 | **The placement on the lane differs.** The carrot and body set-backs are swapped (156 back instead of 39). The speeds are not reset, so a journey launches at the arrival's leftover speed. Ambient vehicles within 400 units are KILLED, so traffic visibly vanishes, where the original swaps them or sets back behind them | `sub_452CC0`: carrot = lane origin, body = origin − 39·dir, `+52 = 0`, `+56 = 256`; the vehicle in the way is swapped in, or the start is set back by its `+60` | `planSliderCall` / `spawnVehicle` / `placeOnLane` (`vehicles.cpp:678`) use the ambient 117 on top of −39; `takeOverAt` kills (`:706-725`) | M. The e6ff780 shape again: a constant copied from the sibling path |
| B3 | **The remembered destination is cleared on getting out.** In the original, a later "Appel du slider" plus boarding goes straight to the last destination. The port also writes it before the call has succeeded | `dword_6A17CC` is reset only on a new game (`26_ole.c:6350`) | `playframe_control_adventure.cpp:1460` sets −1; `playframe_modes_parts.cpp:1507` writes before the call returns | S. **The reader's memory says otherwise: confirm** |
| B4 | **An ignored slider never leaves.** The original drives it off after 600 frames (~20 s). A slider left in mode 7 after a manual ride can also be re-boarded there | case 2's arrival: mode **1**, bit 4, `flt_8F5E90 = 600`; case 1 counts it down. `MDACTION` tests bit 4 (`sub_438290`) and SETS mode 3 itself | `vehicles.cpp:298-307` forces 3; `canMount` requires 3 | S |
| B5 | **A failed call, or a second call while one is coming, closes the sneak silently.** The original keeps the page up and shows text 42 | 0x0049D400: `screen[+8] = 3` only on success, `sub_4767E0(...,0x2A)` otherwise; `sub_452570` returns 0 while a call is live (`state != 4`) | `widgets.cpp:2367-2369`, `2406-2411` close unconditionally; `callSlider` returns true when `called_ >= 0` (`vehicles.cpp:605`) | S-M |
| B6 | **The arrival deferral is missing**, so the slider can stop inside a junction curve | cases 2/6: if mover flag 0x10 (on a route connector) is set, set 0x400 and drive on; stop once back on a lane | distance-only test in `RideMachine::tick` | S |
| B7 | **The MDACTION gate is raw input.** There is no ACTOR_STATE switch, no priority for the object take, and it fires mid-jump | `MDACTION` 0x0046AEC0: states 4/11 nothing, 13 `sub_4A9580`, 14 `sub_4083F0`; the take (`sub_41C810`) first, then the slider arm | `playframe_control_adventure.cpp:401` on `bits & 0x10` | S |
| B8 | **A call from a resident but unlinked set is not refused** (suspected) | `sub_452570`: `if (!dword_8F5E44 && dword_8F5E34 != dword_93076C) return 0` | `callSlider` checks only `loaded_`/`track_.valid` | S |
| B9 | **A load or restart while aboard leaves `boarding`/`boarded`/`leaving` set**, so the walker is blocked (suspected, port read only) | — | `playframe_modes_parts.cpp:1840ff` and :1918 | S |
| B10 | **`MDSLIDOU`'s small parts** are missing: `g_IgnoreLedges=1`, a ground probe and fall, the input queue reset (`sub_45A9A0`), the mode 7→5→7 nudge. Exposure: getting out into a wall or off the road | 0x0046B890 | — | S |
| B11 | **Mode 17 behaves differently where it is used.** The original's eye is resolved once and FIXED in the world, the target lags the player (f42 = 5), and the camera holds until he is 6.00 m from the eye. The port re-resolves it on him every frame and releases it about 51 frames in | `sub_414520` case 0x11, `sub_4187B0`; `MDSLIDOU` skips its mode-0 request when the mode is 17 | the take camera; `MDSLIDOU` `:2351` | M (needed for M1) |
| B12 | **Mode 8 has none of its dynamics**: the eye and yaw chase at dt/8, plus the wall pass (`sub_417070`) with `sub_4141F0`'s tunables. The port places it rigidly, so it snaps on turns and can pass through buildings | `sub_4141F0`, `sub_417070` | — | M-L |

## M — *Manuelle* (manual driving) is mostly unported

| id | what | original | port | size |
|---|---|---|---|---|
| M1 | **Stopping a manual ride leaves him STUCK on the slider.** There is no `H_SLDOUT`, no ACTOR_STATE 8, no camera 17, and `boarded` is never cleared, so `adventureSeated` re-seats him every frame. The slider is also never put back where the drive began. Two agents found this independently; not run (the sneak header's keys were not reached headlessly) | `sub_4570F0`: drops the slider to the ground, restores `dword_53999C`/`dword_539970`, rider `+248 = y − 33.149605`, `sub_4521E0`, `sub_468FA0`, mode 7, `Camera_Request(17, 60.0)`; case 7 with 0x200 then snaps the slider to `dword_8F5E2C/28/30` | `playframe_control_adventure.cpp:1498-1518`: `placeAt`, `dismountCalled()`, `ride.reset()`, nothing else | M |
| M2 | **It drives tail-first.** **Re-read by hand.** | `sub_457270`: `dword_8F5DD8 = atan2(dir.x, dir.z)·deg − (−180.0)` | `playframe_modes_parts.cpp:1482` `r.yaw = calledYaw()`, with no +180 | S |
| M3 | **It has no collisions and no road-keeping**: it drives through vehicles and buildings and leaves the road freely | `sub_458600` → `sub_458880` (ellipse push between vehicles, bounce at 2/3, `dword_8F5E08 = 40`, speed handed over) and `sub_458C70` (hull corners against the `X`/`OP` road surfaces, push back with `sub_459810`); `sub_459BD0` is unread | `slider.cpp` `hover()`: `settle` (`dword_8F5E08`) is never set, so its gates are dead; `braking = false` is hard-coded (:1488) | L |
| M4 | **It is drawn 30.75 too high, and the rider gets no lift** | `sub_457F50`: the node AT the ride's y | `placeCalled` + `kVehNodeLift` (`playframe_world_crowd.cpp:542`); `riderAt` | S |
| M5 | **NPCs should freeze while he drives** | `05_sys.c:2170`: `if (dword_8F5E04) Slider_TickRide(); else Actors_TickAll()` | nothing is gated on `ride` | S |
| M6 | **Flight details.** The skid recovery runs only with no thrust held (`v58 == 0`), and below |speed| 16 it reverts to the simple arm. The 11° bank goes into the node matrix. The "OP" damping also needs `sub_458490() != 0` | `sub_4573E0`, `sub_442160(0, −yaw, −(360−roll))` | `slider.cpp:153`: `driving` is computed and never tested; `placeCalled` draws yaw only | S |
| M7 | **The rider is also hidden in Manuelle, and the model toggles to the shell** | `sub_457040`, `sub_4521E0` | — | S (with A1) |

## D — checks and docs

* **D1. No check drives state 6.** Both Anekbah destinations in the fixture save arrive in the SAME frame (558 → 558), because the nearest lane point is next to the lane's start. A Souk destination (nearest key 9) would drive. `engine: slider journey` passes without the journey's drive ever running.
* **D2. Two checks assert intentions, not outputs** (the CLAUDE.md §1 trap): `engine: slider call`'s "camera 10" is `RideMachine::camera`, which nothing consumes.
* **D3. Doc errors:**
  * 56 → **60.0**: `sliders.h:190-191`, `todo/slider.md:165`.
  * `todo/slider.md:357` calls `flt_536C28` a field of view; it is the 90-frame latch.
  * `MDSLIDIN` writes ACTOR_STATE **7**, not 8 (the port is right).
  * `todo/slider.md:580`'s "1 m behind" for camera 17 is wrong: the eye is along local X.
  * `arriveAt`'s "relinked AT the lane point" is wrong: `sub_452CC0` always uses the lane top, and `placeOnLane` overwrites it anyway.

## Checked and MATCHING (do not redo)

* The lane search: `sub_452A80`'s 3900 box, projection and endpoint rule, the vehicle lanes `header[2]..[5]`, the route round-robin.
* The 117 arrival against the carrot, in 3D.
* `sub_456C70`'s ±256/−768·dt, the 5000 cap, the 195 player brake, the run-over threshold of 1706.67, and the 90 latch value.
* `MDACTION`'s side test (row 0, `dot < 0` refuses), the strict reach of 157.48, the +33.15 fold, and the door snap (the group-60/61 root0 minus `slf_112`/`slf_113`).
* `MDSLIDIN`: ACTOR_STATE 7, mode 4, screen 7. `MDSLIDOU`: 8 → 1. `sub_468FA0`: state 8. The exit happens where the slider stopped.
* No push while boarding or leaving.
* Case 7's formula, and the 300 measured on the body.
* The presets 8/9/17 (offsets and fov), and mode 9 placed from the matrix rows.
* The flight model's thrust ladder, steering, bank, both drags, dead bands, hover bob and halved delta.
* `sub_40E630`.
* **Frame rate**: every per-frame constant read in all four phases is scaled by dt as the original scales by `flt_4C30D8`. No 60 fps drift was found.

## Not covered

`sub_459BD0`/`sub_459AA0` (unread). How subject 5 resolves the slider's pitch and bank into the eye. `sub_417070` for mode 8 in detail. The camera mode after a cross-area load. The Manuelle hook 0x0049D4A0's own camera. `sub_456B40`'s Doppler. No run at `--framerate 60`, and no Manuelle run end to end. A carried-over call across an area load (`sub_4541E0`/`sub_4544B0(0)`): the original seems to FREEZE it and fail every later call, so decide rather than copy.

## Suggested order

1. **The cheap, certain ones**, one commit each with a check that is shown to fail:
   * B1 (the wrong vehicle)
   * A5 (leave at once after a journey)
   * A2 (the mirrored coming camera)
   * M2 (tail-first)
   * A8 (the run-over)
   * B4 (the 600-frame give-up and the bit-4 gate)
   * B5 (the failed call)
2. **The exit, as one piece**:
   * A6 (`MDSLIDOU`'s facing)
   * A7 (camera 9 as a blend)
   * A3 (mode 10)
   * B11 (mode 17 fixed-eye)
   * M1 (the manual stop through the same exit)
3. **The call's presentation**: A4 (the hold, the bands, the 60-frame return).
4. **After confirming with the reader** against the original game: A1/M7 (the hidden rider) and B3 (the remembered destination).
5. **D1** (a Souk journey that actually drives) before trusting anything in state 6.
6. **Then**: M3 (Manuelle collisions, L), B12 (mode-8 dynamics), A9 (sound), B2, the rest.
