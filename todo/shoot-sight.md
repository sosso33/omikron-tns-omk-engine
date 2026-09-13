# Gunmen seeing through walls — the engage's sight

A reader's pick of the next shoot-mode item (2026-09-13): the gunmen engage
through solid geometry. Work in steps, each ending in a commit and a report.

## 1. What the original does — READ, 2026-09-13

`sub_426E00` (05_sys.c 6830) is the engage. It is called from 8 sites, all in
the generic brain `sub_424DE0` (states 3, 8, 9/28, 11, 13), always as
`sub_426E00(rec, &v174, targetRec, v172)`: `v174` is `Actor_GetPosAndFacing` of
the gunman (5455) and `v172` of his target (5574). It has THREE sights, and
which one gates depends on who is looking:

| arm | who | the sight | on success | otherwise |
|---|---|---|---|---|
| **spectre** | character type 12 (`+80 == 12`) | cone `sub_420C70` **and** `!sub_4449E0` (the ray) | state 3, latch `0x20`, return 1 | **state 4 - back to his patrol** |
| **cross-floor sight** | record flag `0x800000` (property 37 bit 4) on ANOTHER floor | the doubled cone `sub_420D90` **and** `!sub_4449E0` | state 3, latch, return 1 | latched: state 3, return 2; else 0 |
| **general** | everyone else | **`sub_4359A0(floor, targetCell, myCell, 1)` - the GRID walk** | acquire: under `+32` and not latched -> latch, state 6 | ... |

and in the general arm the ray has a smaller job: once the grid sees and he is
inside HALF the engagement range, `sub_4449E0` hitting (or flag `0x1000000`)
keeps him in state 6, and a clear ray lets him close to state 13 or 8.

**So the sight of an ordinary gunman is the GRID walk**, not the ray - which
corrects `todo/shoot-patrol.md` §7. The ray is the sight of spectres and of the
cross-floor watchers, and a close-range tactic for the rest.

`sub_4449E0` (16_o3de.c 3095) builds the segment's box, runs `sub_444460` on
every mesh in it (`o3de_ForEachMeshInBox`) and returns 1 at a hit. `sub_444460`
skips meshes flagged `0x800000`, `0x800` when the context's `+444` is set, and
does no triangle test at all on meshes flagged `0x41`.

## 2. What the port has

* **The ray is ported** - `actor/projectile.h`'s `WorldRay`, the bolts' world
  test, built in the viewer as a `sweepSphere` over the active set's
  `shotSoup`. Its filter is `0x800000` only; the `0x41` skip is not in it, which
  touches the bolts too.
* **The general arm's shape is ported** (`shootEngage`, shoot.cpp 1171): grid
  sight for the acquire, `rayHits` choosing 6 against 13/8. But the viewer hands
  it `gridLineOfSight = true`, `gridClear = true` and `rayHits = true` (play.cpp
  11863) and never casts the ray - so every gunman on your floor "sees" you
  through anything, and one inside half his engagement range always stays in
  state 6, never closing to 13 or 8. (Corrected 2026-09-13: this said the ray
  was left false.)
* **The spectre arm and the cross-floor sight arm are NOT ported.** A spectre
  that loses sight of you never returns to his beat by this rule.
* **The player's shoot record is never kept current.** `Shoot_TickPlayer` runs
  `Shoot_Think` on the player every frame (05_sys.c 7501), so his floor `+188`
  and cell `+136/+140` - the grid walk's START - follow him. The viewer writes
  only `node = -1` at entry.
* **Why the grid wiring was backed out is unproven.** On 2026-09-12 wiring
  `Map2d::lineOfSight` stopped the Shooting gallery's gunmen firing, the walk
  saying BLOCKED (11,29)->(17,21) and (22,37)->(17,21). That attempt was never
  committed, so which cell it took for the player and which way it walked
  cannot be recovered - and the engine walks from the TARGET's cell to its own.

## 3. The steps

1. ~~**Read**~~ - DONE (§1, §2).
2. ~~**The player's record**~~ - **DONE 2026-09-13.** The viewer now runs
   `Shoot_Think` on the player's own record every frame from his pelvis (the
   point the path field always used), clears flag `0x1000`, and takes the
   record's cell - or, when the think refuses it, the cell of the point
   `sub_4368E0` snaps to. The path field is seeded from that cell, and the
   engage reads the player's floor from the record's `+188`, which is the
   engine's own comparison. `sub_4368E0` is transcribed as
   `Map2d::snapToStandable`.

   **Measured in the three arenas.** Gallery: floor 0, cell **(17,21)** from
   frame 2 - exactly the cell the backed-out grid attempt used, so its BLOCKED
   was not a wrong start cell. Supermarket: floor 0, cell (6,11) at 395, robber
   77's hits at 460 and 514 unchanged. Catacombs: the cell follows the player
   step by step, (15,15) to (15,6); the patrols unchanged.

   **The snap, checked** (`verify.py: map2d snap`, `map2d_probe --snap`): from
   the centre of every third cell of every map, 8753 starts, 6528 landings, 2225
   with nothing in reach and 0 landings unstandable; by ring 0 / 3983 / 896 /
   707 / 599 / 343 / 0 - nothing stays put (the start is never tested) and
   nothing goes past 5 (four growths). The supermarket player's standable cell
   (6,11) goes to (5,10), the (-1,-1) neighbour tried first. SHOWN TO FAIL:
   three growths empties ring 5 and drops the landings to 6185. (The restore
   after that mutation first came back RED with the mutated numbers - a stale
   `map2d.o` inside the timestamp granularity - and passed only once the object
   was deleted by name: CLAUDE.md's trap, met again.)

   **What moved: `engine: shoot fire`**, and it was traced rather than
   re-baselined blind. The same route run with and without step 2 is identical
   until frame 102, where gunman 240 at cell (18,20) steers on heading 270
   instead of 225. Before that, at frames 84 and 94, the gunmen push the player
   onto a cell `Shoot_Think` refuses and `sub_4368E0` snaps him to (16,20); the
   old viewer seeded the field from the refused cell itself. So 240 turns
   another way, and 237 ends in the player's fourth bolt. The rest of the shoot
   family held.
3. ~~**The grid sight, decided by measurement**~~ - **DONE 2026-09-13: the
   walls are REAL.** `map2d_probe --sight-pair` walks one line both ways and in
   both door states, and from the player's true cell (17,21):

   | gunman | cell | target -> self | blocked at |
   |---|---|---|---|
   | 237 | (11,29), and (11,28) | BLOCKED | (14,25), byte 0 - a wall |
   | 238 | (22,37), and (22,36) | BLOCKED | (20,29), byte 0 - a wall |
   | 240 | (6,23) | CLEAR | - |
   | (240 later) | (18,20) | CLEAR | - |

   All four walks agree for every pair, so neither the walk's direction nor the
   door mask the viewer cannot know is involved; the gallery's floor drawing
   shows the wall blocks themselves (x 13-16 on rows 25-27; row 31 solid from
   x 10 to 23). By the engine's own rule 237 and 238 cannot acquire the player
   from where they stand, and 240 can. `verify.py: map2d gallery sight` holds
   it; shown to fail by a wall that does not block sight.

   So the backed-out attempt was right about the SIGHT. What it could not show
   is the consequence, which is the next step's question: do 237 and 238 walk
   out from behind the walls and acquire then, as a shooting gallery's gunmen
   would - or does something keep them hidden?
3b. ~~**Wire the grid sight and watch the gallery**~~ - **WIRED 2026-09-13.**
   `fin.gridLineOfSight` and `ein.gridClear` now come from
   `Map2d::lineOfSight(his floor, the player record's cell, his own)` - his
   floor -1 seeing nothing, every door counted open (the viewer cannot ask a
   door object's state; labelled). `rayHits` stays forced until step 4. A log
   line reports each gunman's sight, and its first refusing cell, when it
   changes.

   **The Shooting gallery, the same 300 frames before and after:**

   | gunman | before (sight forced clear) | after (the grid) |
   |---|---|---|
   | 237 | first shot 8, 10 shots | blocked by the wall at (13-14,25) while he walks (11,28) -> (12,24); sight CLEAR at **31**, first shot 31, 8 shots |
   | 238 | first shot 18, 21 shots | walks (22,36) -> (24,30) toward the gap in row 29, still blocked when the player dies at **42** (237's two hits of 7) and stands down: **no shots** |
   | 240 | none | first shot 72 - the player outlives the first seconds now, so 240's delayed action 3 (record 15, after `scx.play.wait obj 9`) gets to fire |

   Gunmen coming out of cover before they shoot - what a gallery's should do,
   and the engine's own rule. 238 is not stuck: his blocking cell slides along
   the wall, (20,29) -> (23,29), as his angle opens.

   **The supermarket:** robber 77's sight is BLOCKED at (6,9) by the counter
   from (6,6); he walks round, (5,8), (4,10), and it clears at **441** - he
   fires at once and hits at 442. Before, he engaged at 418 and hit at 460.

   **And a bug the wiring exposed at the phase's first frame.** The record was
   zeroed by the mode's entry AFTER that frame's player think, so the gunmen's
   first grid sight walked from cell (0,0) - 77 read CLEAR through the counter
   at 394. `Shoot_Enter` ends with its own `Shoot_TickPlayer`, so the entry now
   thinks once. The first try thought from `playerMeshAt` and was REFUSED: a
   phase begun at the end of a scene program has not been drawn by the player
   controller since before the program (the intro drew his body as a staged
   actor), so `playerMeshAt` still held his street-start spot (13058, 1089), a
   counter cell. The hand-back at the program's end had already moved the
   CONTROLLER to the body's place, so the entry thinks from the controller at
   pelvis height: record (6,11) at entry, 77's first sight BLOCKED at (6,9).
4. ~~**The ray, cast**~~ - **DONE 2026-09-13.** `sub_426E00` casts
   `sub_4449E0(target +244, self +244)` only once the grid sees him and he is
   inside HALF his inner range, and the viewer now does the same: from the
   player's root mesh to the gunman's, both as drawn last frame (the player's
   pelvis `pos - cameraLift` until he has been drawn), over the active set's
   shot soup at radius 0. `sub_444BB0` tests a face only when the segment's two
   ends straddle its plane, so the ray stops at the gunman, as `sweepSphere`'s
   `t <= 1` does. It logs each gunman's verdict when it changes.

   **What the ray walks, read to the end.** `o3de_ForEachMeshInBox` walks the
   models in `dword_530C10`, and the only writer is `sub_443300`, called from
   `sub_4195C0` for a slot of `g_DecorSlots` (removed by `Area_LoadSet` and
   `sub_419890` when a set goes). **The list is the linked decor set, and no
   body is in it** - so a pelvis-to-pelvis ray cannot hit either end's own body,
   and the set's shot soup is the right world. The old forced `true` was not
   an accidental match for a ray that starts inside a body; it was wrong.

   **The Shooting gallery** (the brain check's run, the player's real health):
   237's ray from 231 is CLEAR, so instead of the hub's fire arm he goes
   **6 -> 13 at 31** and closes, firing at 43 (it was 31). The player lives
   until **51**, which gives 238 time to clear the row-29 gap and reach outcome
   1 from 447 at 44; 240's late action 3 now arrives after the death. With the
   fire check's 1000 health, 237 and 240 close to the player's cell (23,21) or
   beside it by frames 217-218, and the player's sixth and seventh bolts meet
   THEM after one frame.

   **The supermarket:** robber 77's grid sight clears at 441 as before, and his
   ray from **95** units is clear, so he RUSHES - **6 -> 8** - onto the player's
   cell. He hits at 442 (from the side, band 0) and at **471 from the front,
   band 2: the tip of the view** that the handoff asks a person to judge - and
   kills at **513** (was 595). Under flag 8 his stand-down is PARKED, as
   `Shoot_ActorAction` parks any request, and applied at 526 when his picked
   clip is over; the recovery is at 573 and the phase is left at 574. Two runs
   agree frame for frame.

   **Re-baselined, each traced first:** `engine: shoot brain` (238 from 447,
   alone), `shoot fire` (two bolts on 237 and 240, six impacts), `shoot death`
   (513 / 573 / 574, the parked stand-down), `shoot move` (240 and 237, closed
   in, push the last leg to -84.23 / 38.93) and `shoot entrance` (77, rushed
   onto the player's cell (2,11) by 475 and turning in place, pushes the second
   leg's first 14 frames 10.28 sideways), `shoot gunfire` (237's first shot at
   43, 238 arming at 44 inside the 48 frames and turning to 344, 77's second
   shot at 470 and the frontal hit at 471 inside the 500) and `shoot noise`
   (77's engage at 441 read as ANY state out of 6 with outcome 1 - he now
   leaves it for 8 - and the gallery window 48 frames, 240 hearing the shots at
   43 and 44). `shoot hit` stays red for the reason it was red before this
   step; the rest of the family - map2d, projectile pool, mode, leave, hud,
   radar, patrol, pose, AI - held.

   **A trap met on the way.** Two supermarket runs looked EMPTY - no frame
   lines, no hits - and read like a run cut short at 182 ticks. They were
   complete: the log holds a byte that makes `grep` treat it as binary and
   print nothing (and `cut` refuse it), and "182 ticks" is the adventure
   walker's count, not the frame count. `grep -a` and `LC_ALL=C cut` read it.
5. **The two missing arms** in `shootEngage`: the spectre (type 12) and the
   cross-floor sight (`0x800000`), with a probe element that exercises each.
6. **The ray's `0x41` skip** (`sub_444460`), measured on the shot soups before
   and after, bolts included.
7. **Measure in the arenas and hand it to a person**: the gallery's gunmen still
   fight you, the catacombs' spectres go back to their beats when rock is
   between you, the supermarket robbers - then the shoot family, re-baselines
   traced one by one, and what to watch in play.
