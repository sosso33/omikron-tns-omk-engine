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
5. ~~**The two missing arms**~~ - **DONE 2026-09-13.** In `shootEngage`, in
   the engine's order - after the dead/`0x8000` gate, BEFORE the floor split:

   * **the spectre**, type 12: `sub_420C70`'s cone and a clear `sub_4449E0`
     -> state 3, latch 0x20, return 1; otherwise **state 4**, return 0. His arm
     never asks which floor either of them is on.
   * **the cross-floor watcher**, `0x800000` with the target on ANOTHER floor:
     `sub_420D90` (the doubled cone) and a clear ray -> 3, latch, 1; not seen
     and latched -> 3 and 2; unlatched -> 0, nothing written. It returns before
     the stair search, so a watcher never goes looking for a staircase.

   Both cones were ported long ago (`shootAcquires`, `doubleRange`) and the
   0x800000 fan-out of property 37 bit 4 was already in `initShootRecord`; the
   viewer now computes the wide cone, and casts the ray for these two arms at
   ANY range (the general arm casts it only inside half its inner range).

   **Where the engage is called, corrected.** `sub_424DE0` calls `sub_426E00`
   from brain states **3, 4, 6, 8, 11, 13, 14 and 28** (05_sys.c 5747..6664:
   3/4/6 under the `sub_4358D0` switch, the rest under `v180`) - §1 said "3, 8,
   9/28, 11, 13", missing the patrol (4) and 14. The viewer calls it EVERY
   tick in every state, a superset that has not mattered for the general arm
   and stays OPEN; but the two new arms write the state (3 or 4) on every
   call, so they run only in the engine's calling states. Outside them nothing
   reads the result (the port's consumers are 3, 6, 8 and 11).

   **The probe** (`shoot_range`, `verify.py: shoot generic`): a spectre in his
   cone with a clear ray 1 / state 3 / latched; the set in the way 0 / 4;
   behind him 0 / 4; seen from another floor 1 / 3. A watcher seen by the wide
   cone 1 / 3 / latched; blocked 0 and his state left at 6; blocked but
   latched 2 / 3; on the player's own floor the general arm (0 / 6).
   SHOWN TO FAIL, each restored by editing the line back, the object deleted by
   name and the probe's output compared with the committed one: the spectre's
   fallback written as 3 instead of 4 (blocked and behind read 0 / 3,
   `shoot generic` red); the watcher on the NARROW cone (his sighting reads
   0 / 6 unlatched, red); and the corpus reading the type four bytes early,
   at +172 (type 12 0, both 0, `shoot range` red).

   **Who reaches them** (`verify.py: shoot range`), over the 1032 shipped
   actor records: **24 are type 12, and every one of them also carries bit
   4** - harmless, since the spectre arm is tested first and returns; **275
   carry bit 4**, in 49 AREA and 9 SCENE chunks - 28 in the catacombs (AREA
   141), 2 in the supermarket's SCENE 56.

   **The catacombs** (the patrol check's route, 500 frames): the four staged
   spectres take the arm from their first tick. 589's ray is blocked by the set
   while he is IN his cone at 1078 units (frame 40), and outside it again by
   71; the others never see the player; none of them leaves his patrol. The
   six CHD_FN gunmen stand on floors 1, 4, 5 and 7 against the player's 0 and
   log no watcher sight, so they do not carry bit 4 - no watcher is on this
   route. (A first-tick line reads 42863 units: the spectres are not yet
   DRAWN on the tick they are staged, and `drawAt` is still zero - the port's
   one-frame staging, not a sight.)
6. ~~**The ray's `0x41` skip**~~ - **DONE 2026-09-13, and it was TWO skips.**
   Both world rays walk the linked set through `sub_444460` - the bolts'
   `sub_444810` (16 callers) as much as the engage's `sub_4449E0` - and it
   skips a mesh flagged 0x800000, gives one with either bit of 0x41 NO
   triangle test, and skips 0x800 when its context's +444 is set. Read in the
   assembly: `sub_444810` zeroes +440 and +444 (`var_38`/`var_34`, the context
   `var_1F0`); **`sub_4449E0` writes +444 = 1**. So the bolts test the 0x800
   CUTOUTS and the engage's sight does not: a gunman sees through what a bolt
   still stops on.

   Ported as two soup kinds beside `Render` (which the walker's comparison
   with `tools/sim` keeps): **`Shot`** drops 0x800000 and 0x41 - the bolts'
   world - and **`Sight`** drops 0x800 as well - the engage's.

   **Measured** (`shot_ray --kinds`, `verify.py: shoot ray soups`), over all
   220 decor models: **0x41 is on 3 meshes, all in `AResto14`** - the
   lone-triangle markers `G1Epauled`, `G1Epauleg`, `G1Ventre`, flagged 0x5,
   whose bit 0 is also "do not draw" - and **0x800 on 363 meshes in 39
   models**. Per set:

   | set | render | shot | sight | what a segment through a dropped mesh meets |
   |---|---|---|---|---|
   | `hamestag` (the catacombs) | 5510 | 5510 | 5402 | the three cutouts `HAliane02`, `HAopacite`, `HAtete` stop a bolt and **not** a sight |
   | `AResto14` | 2815 | 2812 | 2524 | the six foliage cutouts `RE14feui*` let the sight through to the planters `RE14bac*`; the three 0x5 markers are gone from both |
   | `A_shootg` (the gallery) | 4000 | 4000 | 4000 | - |
   | `ASm49` (the supermarket) | 4178 | 4178 | 4178 | - |

   SHOWN TO FAIL, each restored by editing the line back, `collision.o`
   deleted by name and the probe's output compared byte for byte with the
   committed one: `Sight` keeping the 0x800 cutouts (hamestag's sight back to
   5510, a segment through `HAliane02` meeting it, `shoot ray soups` red); and
   both ray soups keeping 0x41 (AResto14's shot back to 2815, red).

   So neither skip can move the gallery or the supermarket; the catacombs'
   spectres are the only arena sight it touches. **Replayed** on the patrol
   check's route: with the render soup every spectre ray stayed blocked; with
   the sight soup 591's clears at 109-126 and 150-282 and 595's at 143-262 -
   through the cutouts - but the player is outside their cones each time, so
   no spectre changes state and every one keeps his patrol.
7. ~~**Measure in the arenas and hand it to a person**~~ - **DONE 2026-09-13**,
   handed to play as `todo/play-test.md` 11.

   * **The gallery and the supermarket** are pinned by the shoot family's own
     checks as re-baselined in steps 3b and 4 (`shoot brain`, `fire`, `move`,
     `entrance`, `noise`, `gunfire`, `death`), and steps 5 and 6 moved none of
     them: the gunmen come out of cover and close in, robber 77 walks round the
     counter, rushes, hits at 442 and 471 and kills at 513.
   * **The catacombs**, where the spectres live. The patrol check's own route
     never brings the player inside a spectre's reach (acquire 1170, cone
     cos 0.174 - the beats are ~1600 away), so two routes were walked: into
     zone 2295, a 90-degree turn, and west. On both, from frame 232 to 506,
     spectres 589, 590, 591 and 595 each have the player INSIDE their cone
     several times with the set between (`HITS the set, inside his cone`), and
     none leaves his patrol - the engine's rule for rock between. **Neither
     route produced a clear-ray sighting**, so a spectre seeing the player and
     going back to his beat when the line breaks is asserted by the probe
     (`shoot generic`) and NOT yet seen in an arena: it is what play-test 11
     asks a person to watch. (Standing still elsewhere is not a route: shoot
     mode only begins when the player walks into zone 2295.)
   * **Seen on the way, not a sight fault:** the west walk drops the player
     ~204 units onto floor 3 near x 42235, where a 563-unit step moves 2.39 and
     `Shoot_Think` finds no standable cell in reach - a walker or grid question
     for later, filed in play-test 11 for the reader's eye.

   **What stays open from this plan:**
   * the viewer calls `sub_426E00` every tick in every state, where the engine
     calls it from 3, 4, 6, 8, 11, 13, 14 and 28 (step 5) - the two new arms
     are gated, the general arm is not;
   * the doors: the grid sight counts every door OPEN (step 3b), because the
     viewer cannot ask a door object's state;
   * `engine: shoot hit` is still red on purpose - its expectations predate
     the grid sight and it needs re-aiming at a gunman who can be reached;
   * a scene program moving a set mesh is not followed by the baked shot and
     sight soups (labelled since the bolts).
