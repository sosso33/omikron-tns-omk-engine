# Falls and health — next-tasks 14 + 15

Opened 2026-09-17, on the reader's order *"do 5"* (the review's health, then
falls). What was already done is in `todo/player-vertical.md` (the jump, the
walker's vertical) and `todo/released-spectres.md` step 6 (the landing
MESSAGES 10 and 11, posted by the port since then). This file is the fall
REACTION and what is left of health.

## 1. Read, 2026-09-17 — `Walk_GroundResponse` (0x00465460) and `sub_414DE0`

**`sub_414DE0(actor, mode, flag)` is a CAMERA request, not an ACTOR_STATE
write.** It sets both camera subjects to the actor and calls
`Camera_Request(mode, &dword_930800)` with a travel in `dword_930818`, gated on
the mode that is up (`C+12`):

| mode | only when | travel (frames) |
|---|---|---|
| 0  | camera 19 is up | 90 (`0x42B40000`), `dword_930820` = 1 |
| 16 | camera 18 is up | 30 (`0x41F00000`) |
| 18 | `C+140` is 0 | flag ? 60 (`0x42700000`) : 30 |
| 19 | camera 18 is up | 60 |

Preset rows 18 and 19 (`tables/camera_presets.json`) are an overhead shot,
eye (0, 118.11, -3.94) - three metres above him - fov 75. Mode 16 is the
travel back to the follow camera the take already uses (`play.cpp`,
`takeCamRequest(3)`). **This corrects `todo/player-vertical.md` §6 and
`PlayerController::jumpLand`**, which read `sub_414DE0(actor, 18, 1)` in
`MDJUMP03` as "ACTOR_STATE 18" - a state that does not exist (0..17).

**Stepping off a ledge** (not in a jump, `dword_6A52CC` 0), once per fall
(`+1304` 0 or 2), by the clearance to the ground below:

* under 7.87 (or `g_IgnoreLedges`): snap down, no fall;
* 5 m / 3 m / 1.5 m: `+1304` = 4 / 3 / 1, bank **group 2** (`H_FALL`) unless
  ACTOR_STATE is 2, 3 or 15, and **camera 18** (travel 30);
* under 1.5 m: `+1304` = 2, nothing.

**The landing**, by the accumulated fall `+280` (and `+284` for the short
band):

* under 1.5 m: if the current group IS group 2, **group 100** (locomotion);
  camera 16 if 18 is up;
* 1.5..3 m: **group 4** (`H_LFL`, which flows to stand / walk / run); camera 16
  if 18 is up;
* 3..5 m: group 4, **message 10** (player only); camera 16 if 18 is up;
* 5 m and over: **group 5** (`H_HFL` -> `H_SOL`, lying), **message 11**, and
  **camera 19** (player only).

**And the way back from 19**: `H_SOL-SD`, the get-up, carries `MDRAISE0`
(0x0046BED0, 18 bytes, read from the raw image - `asmfn.py` snapped to a
neighbouring block here, CLAUDE.md 1's trap): `sub_414DE0(actor, 0, 0)`, the
90-frame travel home.

## 2. The steps

| step | what | state |
|---|---|---|
| 1 | the correction: `jumpLand`'s "ACTOR_STATE 18" is camera 18; a player camera request with `sub_414DE0`'s gates and travels (`fallCamRequest` in `play.cpp`, riding the take camera's travel) | **done 2026-09-17** |
| 2 | the ledge fall: group 2 and camera 18 once 1.5 m of clearance is under him | **done 2026-09-17** - banded on EVERY airborne tick until it bands, see §3 |
| 3 | the landing: groups 100 / 4 / 5, cameras 16 / 19, `MDRAISE0`'s camera 0 | **done 2026-09-17** |
| 4 | VEHICLE hits | **done 2026-09-17** - §4 |
| 5 | health at 0 outside a fight or a shoot phase | **read 2026-09-17: nothing takes it there** - §5 |
| 6 | play | |

`verify.py: engine: fall reaction` (the crate stack and the catacombs) and
`engine: run over` (Anekbah's lane).

## 3. What running it found

* **A slide becomes a fall.** The catacombs' 5 m drop begins on a face past
  the slope limit: the walker slides for eight frames and only then falls. A
  ledge test run on the tick he leaves the ground sees a slide and never bands.
  The engine's airborne arm runs every tick while `+1304` is 0 or 2, and so
  does the port's now; shown to fail by reverting to the first tick only.
* **`Walker::land` rewrote the landing's record on every walking step.** Each
  ordinary snap onto the floor calls `land()`, and each call replaced the last
  landing's tier and fall with the snap's 0 - so a landing on a frame that also
  ran a step read back tier 1, fall 0, no message. Kept from a real landing now
  (airborne or sliding). Correct, but the fall check does not depend on it once
  the fall is banded, and mutated it stayed green - recorded rather than
  claimed.
* **He lies until a key is pressed.** `H_SOL-SD` waits on the "no input"
  sentinel `0x80000000`, which the idle word `0x40000000` never matches
  (CLAUDE.md 4's input notes), so after a 5 m fall he gets up when the player
  presses something - and `MDRAISE0` brings the camera home over 90 frames.

* **And a regression from 2026-09-15, found on the way.** Running the street
  checks this work could touch turned up `engine: street frame` and
  `engine: traffic frame` RED, and not from this work: `df32519` (multiplan 3)
  set the Session's data root for every run so IAM\OBJECT is found, and
  `loadTrafficFor` read "a data root is set" as "the street is wanted" - so
  `--no-crowd` has loaded the crowd and the traffic ever since. Fixed with a flag
  only `loadTraffic` raises; both checks green again.

## 4. Vehicle hits, read and wired

`Sliders_Tick` probes the player's ground every frame; the mesh under him is
the ROAD when its name at `+16` starts with the byte 'X' or the word "OP"
(`dword_8F5E38`). A vehicle within 195 units closing on a player on the road
takes 768 a frame off its speed, floored at 256; one still above **1706.67**
whose spatial entry touches him raises event 43 with **message 17**, latched 90
frames. (Messages 13/14 and 15/16 are the PEDESTRIANS' talk and bump, already
ported, not vehicles.)

**The port had both halves and neither ran**: `Sliders::setPlayer` had no
caller, so no vehicle knew where he was, and `bumped()` recorded run-overs
nothing posted. Now the viewer probes his ground mesh each frame and the
Session posts 17.

**Who answers 17**: not IAM\GLOBAL - only AREAs 0 (Anekbah), 1 (Jaunpur), 64
and 101. Anekbah's handler: `Vie` read, a red flash, `camera.shake 20, 30`,
`Vie -= 15` (or 5 below 16), `actor.stat.set`, `player.move.wait 118` -
`H_IMPACT`, knocked flat. Braking does not save a player standing in a lane: a
vehicle arriving at 5000 is still far above the limit when it reaches him.
Measured in Anekbah's lane at x 5466: 1 run-over, `H_IMPACT`, `Vie` 10 -> 5.

## 5. Health at 0 outside a fight or a shoot phase

Every handler that costs health outside those modes has a FLOOR of 5: IAM\GLOBAL's
landings (10 and 11), AREA 2's and SCENE 62's landings, the run-over in AREAs 0,
1, 64 and 101. The one handler without a floor, AREA 141's message 11, is the
catacombs - a shoot phase, where the shoot mode's own death applies. AREAs 41,
61 and 168 list subscriptions to 10/11 whose script offset is 0, dead records.
**So nothing in adventure mode kills the player, and there is no adventure
death to port.**
