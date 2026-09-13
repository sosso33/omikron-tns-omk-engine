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
  it `gridLineOfSight = true` and never casts the ray, so every gunman on your
  floor "sees" you through anything.
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
3. **The grid sight, decided by measurement.** A point query in
   `map2d_probe` for the gallery's pairs, from the player's true cell, walked
   target-to-self as the engine does, doors open and shut. Clear: wire it into
   the engage. Blocked: find what differs (the door mask the viewer cannot know,
   the cells) before wiring anything.
4. **The ray, cast.** `EngageIn::rayHits` from the ported `WorldRay` between the
   two actors' node positions.
5. **The two missing arms** in `shootEngage`: the spectre (type 12) and the
   cross-floor sight (`0x800000`), with a probe element that exercises each.
6. **The ray's `0x41` skip** (`sub_444460`), measured on the shot soups before
   and after, bolts included.
7. **Measure in the arenas and hand it to a person**: the gallery's gunmen still
   fight you, the catacombs' spectres go back to their beats when rock is
   between you, the supermarket robbers - then the shoot family, re-baselines
   traced one by one, and what to watch in play.
