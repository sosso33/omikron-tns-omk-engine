# Handoff — SHOOT MODE (`todo/next-tasks.md` 18)

**Read §3 first if you are the reader coming back to this: it is the ordered
list of what needs your eyes, written 2026-09-12.**

Rewritten 2026-09-11, at the end of the session that made the gunmen FIGHT:
they fire, turn, aim, hold their guns, fall, walk, stop at walls, push, steer
by the floor's path field, advance and then stand to fire - and the player can
die. Everything below is **pushed** on `main` (`9d7182d`); nothing is left
uncommitted. The plan and the full record are [`shoot-mode.md`](shoot-mode.md)
(§8 items 4 and 6 are today's); what awaits a person is
[`play-test.md`](play-test.md).

---

## 1. HOW TO REACH A SHOOT PHASE — read this before anything else

```
cd engine && make play
build/omk-play "$OMK_DATA" ../tables --save ../traces/save-appart.bin \
    --area 230 --scene-chunk 56 --vulkan --radar always
```

The cutscene plays, the editing ends at frame ~385, and `SHOOT MODE ENTER`
follows at 394. **~390 frames from a cold start, no save of your own and no
playthrough.** Left standing, the player is shot by robber 77 and DIES at 689;
at 749 message 1 loses the phase, shoot mode ends at 750 and the Meditek
voice-over plays (`verify.py: engine: shoot death`).

**`--scene-chunk` is the whole trick and it is easy to miss.** The supermarket
phase is **AREA 230 + SCENE 56 or 62** (both on set `ASM49`), and AREA 230
carries **no `shoot.begin` of its own** — it is in a ZONE SLOT of the scene.
`--save --area 230` is a street start, which lands the player in the room
without running the chunk that would have been loaded on the way in, so the
trigger never exists and he stands there for ever.

The other arena, and the one most checks use, is the Shooting gallery:

```
build/omk-play "$OMK_DATA" ../tables --save ../traces/save-appart.bin \
    --area 59 --stand 5000,0,-2900,180 --shoot
```

`--shoot` is a HARNESS — it calls `shootBegin(-1)` directly. It is not how the
game enters the mode, which matters more than it sounds: see §5 trap 1. **The
gallery's gunmen kill the player in ~16 frames**, so a run that tests his own
weapon, movement or bolts needs `--shoot-health 1000` (§6).

**To FIRE headless**: `Tir` is scan code **54** in the *Tirer* scheme:

```
SDL_VIDEODRIVER=dummy build/omk-play "$OMK_DATA" ../tables \
    --save ../traces/save-appart.bin --area 59 --stand 5000,0,-2900,0 \
    --shoot --shoot-health 1000 --frames 300 --nodelay \
    --keys 54,54,54,54,54,54,54,54,54 --keydelay 30
```

is `verify.py: engine: shoot fire`. `engine/tools/shoot_trigger.cpp` answers
"which chunk starts a shoot phase", scanning zone slots **and** the `+4`
startup scripts.

## 2. Where the work stands

| step | state |
|---|---|
| 1-6 the mode read, `MAP2D`, ops 80/81, weapon tables, the brains' ranges, sight | done |
| 7a-f the geometry, the turn, all 16 states of `sub_424DE0`, the brain ticking, the pool | done |
| 7h-j the player's SHOT, its FLIGHT, the HIT on gunmen | done 2026-09-10, played |
| 8.0 the first-person ARM, GUN and RAISE | done, **CONFIRMED IN PLAY** |
| 8.1 the NOISE, 8.2 the SOUNDS, 8.3 the HUD and RADAR | done; HUD, radar **CONFIRMED** |
| 8.5 mouse look, first-person MOVE, entrances as gameplay, the phase's end, the return | done, **CONFIRMED IN PLAY** |
| **item 4, THE GUNMEN'S SHOTS**: fire (`1456838`), the turn clip (`cc2b114`), the player hit (`6e7bb87`) | done 2026-09-11, played |
| **item 6 A, the robbers ANIMATE, AIM, FACE, HOLD their guns** (`c5e17bb`, `31eb903`, `9f07ff5`) | done, played (*"Ok, better"*) |
| **A4 the FALL** (`872ddd8`) | **CONFIRMED IN PLAY** (*"the bodies reach the floor"*) |
| **A5 the WALL TEST** `sub_421140` (`9880055`) | done |
| **B1 the WALK** `sub_421370` + the height at every clip start (`8ed9999`) | done; the height fix **CONFIRMED IN PLAY** |
| **B1 the COLLIDER**: gunmen in the spatial index, the push on the player | done |
| **B2 the push SWEPT** by `Actor_Move` (`154139d`) | **CONFIRMED IN PLAY** (*"they can't push me outside the env"*) |
| **B3 the STEERING**: the path field, `sub_421CD0`'s grid turn, the 0x80 cell stamps (`046f0fe`) | done, not yet played |
| **B4 the ACTIONS** `Shoot_ActorAction`: advance, then crouch / stand to fire (`324ef32`) | done, not yet played |
| **B5 the push's OWN spheres** (`f368f07`) | **CONFIRMED IN PLAY** (*"I didn't get stuck anymore"*) |
| **item 4 step 4, THE PLAYER'S DEATH** `sub_423FC0` + the countdown (`9d7182d`) | done, not yet played |
| **THE HURT REACTION** `sub_47D1F0`: the flat sound, the band, the four-frame shove, the WHOLE-Euler camera rotation that makes it visible | done 2026-09-12, not yet played |
| **THE PATROL**, `shoot.actor.action 1` - the commonest shoot action, 116 of 319 sites ([`shoot-patrol.md`](shoot-patrol.md)) | done 2026-09-12, **WATCHED** |
| **THE NAV EDGE** - the links, `sub_436BB0`, the engage's cross-floor arm, states 1 and 2, the floor change ([`shoot-navedge.md`](shoot-navedge.md)) | done 2026-09-12, proved at unit level, NOT watched |

**The checks**: `python3 tools/verify.py --only "engine: shoot" "shoot fire"
"crowd push"` - 17, all green (run in two or three groups: see §5 trap 13).
`shoot fire` carries the unit probes - `wall test:`, `path field:`,
`grid turn:`, `actor action:`, and since 2026-09-12 the hurt reaction's
`hurt shove:`, `hurt sounds:` and `shove camera:`; `engine: shoot death` is the
death on the real path.

## 3. WHAT TO TEST WHEN YOU ARE BACK — in this order

Written 2026-09-12. Everything below is committed, green in its checks, and
**not judged by a person**. The order is by how much a wrong answer would cost
and how cheap the look is; 1 and 2 are one command each.

### 1. The slow fall — THE OPEN REPORT (a minute)

Your words: *"the character fall very slowly, in an not natural way."* The
instrument is in and it distinguishes the two things that look alike:

* `the player FALLS from y N` — accelerating, `+220 += kGravity` a frame;
* `the player SLIDES (a face past the slope limit - a CONSTANT 11.8 a frame,
  not gravity)` — a steady 0.3 m/s with no acceleration, which is what an
  unnatural descent reads like.

and on landing, `the player LANDS at y N - dropped D (M m), tier T`.

**What to do**: get into shoot mode anywhere with a drop - the catacombs route
below has plenty - walk off something, and tell me **which of the two words the
log prints**. If it says SLIDES, the fall is not the bug and the slope test is.
I could not reproduce one headlessly: walking forward for 600 frames in hames
never took the player off a ledge.

### 2. The patrol, WATCHED (two minutes)

```
build/omk-play "$OMK_DATA" ../tables --save ../traces/save-appart.bin \
    --area 141 --zone-enable 2295 --stand 42786,854,-2380,0 --vulkan \
    --hold "k200*180"
```

It walks itself in for six seconds. Ten spectres enter and nine take a patrol
route. **Confirmed in play already**: they stay in the world (you saw one leave
through a wall before the y was pinned; that is fixed) and their feet touch the
floor. **Still to judge**:

* do they look like they are **walking beats** rather than milling, now that
  the cross-floor arm puts an unlatched one back on his route?
* the two `CHD_FN` ones stand 5 units off the floor where the spectres' feet
  sit exactly on it - their model's lowest collision sphere reads 5 against the
  spectres' 40. Is that visible, or is it inside the noise?

### 3. The hurt reaction (one minute)

```
build/omk-play "$OMK_DATA" ../tables --save ../traces/save-appart.bin \
    --area 230 --scene-chunk 56 --vulkan
```

Stand still. Robber 77 hits you at frames **460** and **514**. Each should be a
four-frame **downward tip of the view** with `IMPACT03.WAV` on it. A hit from
the side would roll you and the first-person camera cannot show a roll - that is
the engine's arithmetic, so do not expect one.

### 4. Three things measured and never played

All on the supermarket route above:

* **DYING** - killed at 689, the phase lost at 749, then the Meditek
  voice-over;
* **the robbers STEERING** by the path field;
* **the actions' advance-then-stop** - they should close on you and then stand
  or crouch to fire.

### 5. Two questions I could not answer from a log

* **`engine: shoot hit` is RED on purpose** and needs a new aim so its kill
  exists again. Notes in `todo/shoot-patrol.md`; do not baseline its empty
  lists.
* **AREA 232 cannot be reached** - its `shoot.begin` is on the ACTIVATE slot
  (press scan code 28 inside the quad) and, worse, the player cannot walk
  anywhere in that area at all: `walked 0.0 over 219 ticks`. If you can get in
  there by playing, seven latched gunmen over ten stairs is what finally shows
  the nav edge working in a room rather than in a probe.

### 6. And the sweep

A full `--slow` run is owed - **5 tasks**, not done since 2026-09-09. It waits
for your go; I started one by misreading you on 2026-09-12 and stopped it.

## 3b. What a person has CONFIRMED, and what is only measured

**Confirmed in play**: the mode's entry, camera, mouse look, eye height and
first-person move; the arm, the gun and its raise; the HUD, the radar
(`radar = always`), the health kits; the phase's end and the return; the
robbers' fall to the floor, their height held while walking, the push that
cannot carry you through a wall, and the people-sized bodies that no longer
trap you.

**Played, no verdict yet**: the gunmen firing and turning round, the aim
pose, the guns held, the walk.

**Measured only - not yet played** (`play-test.md` NOT YET PLAYED): **DYING**,
**THE ROBBERS STEER**, the advance-then-stop of the actions, and now **THE
HURT REACTION** (2026-09-12). Play those four before building more on them.
The hurt is the cheapest of the four to judge: stand still on the
`--area 230 --scene-chunk 56` route and robber 77 hits you at frames 460 and
514, each a four-frame downward tip of the view with `IMPACT03.WAV` on it.

## 4. What is left

1. ~~**THE HURT REACTION `sub_47D1F0`**~~ - **DONE 2026-09-12**
   (`todo/shoot-mode.md` §9). The sound is flat, not positional; the band picks
   the angle (a bolt ACROSS him rolls, one ALONG him tips); the shove is four
   frames out and back to 2 degrees. It is visible because a subject-relative
   camera point is rotated by the subject's WHOLE Euler - which closed
   `o3de/worldcam.h`'s note about who writes the camera block's `+112..+120`.
   **Only the pitch bands show**: the roll turns about the target offset's own
   axis, so a side hit moves the first-person view by nothing, and that is the
   engine's. Two on the way: flag 0x1000's writer is `sub_47CC70` (the player
   is a Mecagarde: no pitch at all), and property 7 is now read at the entry -
   it is 9, `Incarnable`, where the port had been leaving -1.
   **Not yet played**: what a person should judge is the tip on every surviving
   hit and the `IMPACT03.WAV` that goes with it (supermarket frames 460 and
   514). **The suggested next step is now the PATROL below**, or a play pass
   over the four measured-only items in §3.
2. ~~**THE PATROL, action 1**~~ - **DONE 2026-09-12**, seven steps, and it is
   its own plan file: [`shoot-patrol.md`](shoot-patrol.md). It was the
   COMMONEST shoot action (116 of 319 sites). The routes were already in the
   `.mpt`; the flags' ping-pong bit is settled by the data; the third operand's
   high byte is the FLOOR, which the engine discards. **WATCHED**:
   `--zone-enable 2295` opens AREA 141's catacombs and ten spectres patrol, one
   closing his nine-point ring at frame 483. Four defects fell out of it (the
   `+44/+48` field collision, `Shoot_Think` never wired so
   `sub_426E00`'s floor guard had never fired, the occupancy cycle's missing
   restore, and a gunman's y drifting where the engine pins it), and one wrong
   turn was backed out: **the engage's sight is `sub_4449E0`, a RAY, not the
   grid walk**. On the way, `sub_436BB0` turned out to write the nav EDGE at
   `+4` - **the path-finder's front door, reached from the engage** - and
   character type 12 has its own arm there that returns a spectre who cannot
   see you to state 4. **`engine: shoot hit` is RED on purpose**; read
   `shoot-patrol.md` before touching it.
3. ~~**THE NAV EDGE AND THE PATH-FINDER**~~ - **DONE 2026-09-12**, five steps,
   its own plan file [`shoot-navedge.md`](shoot-navedge.md). **It was never a
   path-finder**: `sub_436BB0` is 37 lines - my floor's links filtered to the
   player's floor, nearest near end - one hop, nearest door, no search. And the
   28-byte per-floor records this tree read as "wall segments" are inter-floor
   LINKS, settled 36 of 36 three ways (a real destination, both points in their
   own floors, and every link RECIPROCAL). The engage's `!sameNode` arm, states
   1 and 2, and the floor change at the far end are all in. **THE LATCH IS THE
   GATE** - action 1 raises no `0x20`, so a patrolling gunman never follows you
   upstairs, which is the engine's rule and not a gap.
   **Left open by it**: `sub_421020`, the fallback when no stair reaches the
   player's floor; and it is NOT WATCHED, because AREA 232 - the one arena with
   latched gunmen, ten links and no patrols - has its `shoot.begin` on the
   ACTIVATE slot and a floor that refuses the walker outright
   (`shoot-navedge.md` §7).
4. **ROBBER 77'S TURN LOOP** - the supermarket's first robber, in sight of the
   player, takes the hub's FIRE arm (turn at the target), meets a wall, has
   his facing snapped by the slide (the fire arm does not raise 0x200), and is
   turned back on a type-30 clip - every ~16 frames. His advance timer runs
   only on hub ticks, so he never reaches his stop. Investigate before fixing:
   it may be the reading's own consequence.
5. **The death's remainder** - the body show / hide (`sub_436D20` /
   `sub_436CE0`), camera request 4 at recovery, `sub_47CC70`, the weapon's
   re-attach and `Shoot_InitWeapon`, and `sub_47CE70` (a global actor's
   `+416/+424` zeroed - its writer not traced).
6. **Labelled gaps** - the PLAYER's push reach still `meshes.front()` (7.1
   against his root's 42.5; correcting it moves the street crowd's confirmed
   push); only shoot gunmen are in the push index, and the dead stay in it;
   the door cells (`sub_47C1B0`) and the byte-1 memo; `shootEngage` hoisted
   before every step where the engine calls `sub_426E00` inside the arms (the
   state-3 -> hub bounce is the engine's, every other tick there, every tick
   here); the clip a type resolves to is not `List_PickRandomByType`'s random
   pick.
7. **A LOST GUARD** - the swept push's assertion rode on a fight that no
   longer shoves him into a wall; the fix stands (mutation-shown at
   `154139d`, confirmed in play) and needs a probe of
   `PlayerController::nudge` against a wall.
8. **`sub_421020`** - the fallback the engage takes when NO stair reaches the
   player's floor, and the last unread function in the chase. Unread.
9. **`sub_4449E0`** - `sub_426E00`'s real sight, a RAY against geometry. The
   port hands the engage a hard-coded "clear", so a gunman engages through
   walls; wiring the GRID walk instead is the wrong fix and was backed out
   (`shoot-patrol.md` §7).
10. **THE SWEEP** - a full `--slow` run is owed (`todo/sweep-log.md`), not done
   since 2026-09-09's; it waits for the reader's go.

## 5. Traps that cost time, in the order they bit

1. **A HARNESS THAT DOES SOMETHING THE REAL PATH DOES NOT.** `--shoot`
   installed the camera; the script-driven entry did not, and for six steps
   every check went through the harness. **Ask what the harness does that the
   thing it stands in for does not.** (`--shoot-health` is today's harness,
   and it only writes property 1.)
2. **A BIT NUMBER IS NOT A MEANING.** `Action / Utiliser` is slot 4 in
   *Aventure* and slot **8** in *Tirer*, where slot 4 is `Tir`.
3. **THE ROOM WAS THE WRONG ROOM.** `SMARKET1` grep found AREA 68, a different
   supermarket; the reader's log said `last set ASM49` throughout.
4. **A TOOL GIVEN A PATH RELATIVE TO THE WRONG DIRECTORY** answers "0 slots"
   rather than failing.
5. **`--scene-id` IS NOT A FLAG** - silently ignored.
6. **A SIGN THAT ONLY MOVING CAN SHOW** - `resolveOffsets` subtracts; the
   acquisition's forward is `-sin`.
7. **THREE WRONG HEIGHTS BEFORE THE RIGHT ONE** - *"instead of guessing, look
   at the original code"*.
8. **A HANDOFF'S OWN ADDRESS WAS A CLEAR** - check the address before
   building on it.
9. **`asmfn.py` SNAPS** to a neighbour when an address has no `proc` label.
10. **`timeout` IS NOT A macOS COMMAND** - save the log to a file and grep it.
11. **A LABELLED READING OUTLIVES ITS REASON** (2026-09-11). The push used the
    per-mesh bounding spheres because the model's list "was not traced back to
    a writer" - long after `modelSweepSpheres` read that very list from the
    file for the walker. Every body pushed like a 90-unit ball and trapped the
    player. When a labelled stand-in has a real source elsewhere in the tree,
    switch.
12. **`a2` WAS THE ACTOR.** `ShootFrameIn::defaultClipType` was documented as
    "the action the caller asked for"; `sub_424DE0`'s `a2` is the actor's
    index and every `Shoot_ActorAction` it makes is the literal action 0. The
    same name read as a clip TYPE in LABEL_177. Read what a parameter IS at
    the call sites, not what it would be convenient for it to be.
13. **A 15-CHECK RUN IS KILLED BY MEMORY** - `verify.py --only "engine:
    shoot"` in the background was stopped for low memory on this machine; run
    the family in groups of 3-12, one invocation at a time.
14. **`--only` IS A SUBSTRING MATCH** - `"shoot fire"` also runs `engine: shoot
    fire`; judge a mutation by the elements of the check it targets.
15. **A LOG LINE BEFORE THE BODY IS DRAWN** - a gunman's brain is built on the
    tick he is staged, when `drawAt` is still 0: his cell read (0,0) and his
    first brain line gave a distance from the world origin. Both now wait for
    his first position on the grid.
16. **A BOLT FIRED WITH THE SAME `rand()`** - the brain's own draws (the action
    coin, the grid turn's tie and discard) come from the CRT the bolts' jitter
    uses; a new draw shifts every later jitter by a triple, and `engine: shoot
    gunfire` shows it.

## 6. Instruments, so they are not rebuilt

| | |
|---|---|
| `engine/tools/shoot_trigger.cpp` | which chunk starts a shoot phase |
| `engine/tools/shoot_range.cpp` | the record's geometry, the turn, the brain's states, the engagement |
| `engine/tools/projectile_probe.cpp` | the pool, the fire gate, the entry's bytes |
| `engine/tools/shoot_fire.cpp` | the gate, the latch, the aim, the flight, the weapons - and today's unit probes: `wall test:`, `path field:`, `grid turn:`, `actor action:` |
| `engine/tools/sphere_dump.cpp` | a model's own collision list, `desc+244/+248` |
| `omk::shootWallTest` | `sub_421140` |
| `omk::ShootField`, `omk::shootGridTurn` | the path field and `sub_421CD0` |
| `omk::shootActorAction` | `Shoot_ActorAction` |
| `omk::shootHurt`, `shootMoveTick`'s 0x400 arm | `sub_47D1F0` and the four frames that spend it |
| `omk::rotateEuler` (`actor/player.h`) | `Matrix3x3_FromEulerAngles` WHOLE - what `resolveOffsets` rotates by |
| `omk::shootMovePitches` | `sub_47D370`'s Mecagarde arm, mover flag 0x1000 |
| `omk::pushSpheresOf`, `Session::actorBody` | the push's spheres, the gunmen in the spatial index |
| `Map2d::setCell` | the runtime 0x80 stamps |
| `--shoot-health N` | TEST HARNESS: property 1 written as N at shoot entry |
| `--shoot-end N`, `--invert-x/-y`, `--shoot-eye N` | leave the mode; the mouse senses; the eye lift |
| the viewer's lines | `SHOOT TYPE`, `THE SHOVE (sub_47D1F0)`, `walks (sub_421370)`, `the wall test ... stops his walk`, `STEERS by the path field`, `ACTION n (Shoot_ActorAction, why)`, `clip type N starts`, `the player is pushed out of actor N's body`, `the push ... met a wall`, `the path field ... ran dry`, `KILLED (sub_423FC0)`, `the player's death clip is over`, `GUNMAN SHOT`, `PLAYER HIT`, `AIM LAYER`, `HIS BARREL points` |

## 7. The sweep

**A full `--slow` run is OWED**, not done since 2026-09-09 (`f92231a`), which
predates all of shoot mode's firing and fighting. `todo/sweep-log.md` still
counts 3 tasks, shoot mode being one. It waits for the reader's go; run it in
groups if memory is short (§5 trap 13).
