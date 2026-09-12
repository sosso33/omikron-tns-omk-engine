# THE PATROL — `todo/handoff-shoot-mode.md` §4 item 2

Planned 2026-09-12, after reading the mechanism end to end. **The patrol is
`shoot.actor.action`'s most common action: 116 of the 319 shipped sites**, more
than action 3 (113) and twice action 0 (56). Until now the port has treated it
as a no-op — `Shoot_ActorAction` case 1 was the one arm left unported, "no
routes here" — so every one of those 116 gunmen stands still.

Nothing below is a guess: the reading is `sub_4354E0`, `sub_435660`,
`sub_4356B0`, `sub_435650` (all in `readable/src/10_dsound.c`),
`Shoot_ActorAction` case 1 (`05_sys.c` 4079), `sub_424DE0` states 4 and 5
(`05_sys.c` 5757 and 5826) and `sub_4272B0` cases 3 and 4 (`05_sys.c` 7188),
and each half is corroborated by the shipped data.

---

## 1. What a ROUTE is

The routes are in the `.mpt` file, **per floor**, in the third section — the one
`engine/src/formats/map2d.h` already walks and calls "waypoints":

```
per floor:  u32 k + k x [ u32 len ][ u32 id ][ u32 flags ][ len x point ]
point:      u8 cellX | u8 cellZ | i16 clipId
```

`Map2D_Load` keeps a pointer to each floor's list in `dword_52B970[floor]`, and
the walk `v9 += *v9 + 3` is what fixes the three header dwords.

**The flags word, and the data settles it.** `sub_435660` reads bit 0 and bit
2; `sub_4354E0` and `sub_4356B0` read bit 1:

| bit | meaning | shipped |
|---|---|---|
| `0x1` | **PING-PONG**: walk to the end, then back, instead of wrapping to point 0 | **2 of 53** |
| `0x2` | **TAKEN** — a runtime reservation. `sub_4356B0` sets it, `sub_435650` clears it, and `sub_4354E0`'s nearest-free search skips it | 0 on disk |
| `0x4` | currently walking BACKWARDS along a ping-pong route | 0 on disk |

**The two ping-pong routes have exactly 2 points each** (`hames` floor 3 id 6,
`soukdock` floor 0 id 7 — both straight lines), and every one of the other 51 is
a closed ring of 4 to 27 cells. A 2-point route that WRAPPED would be
degenerate; a 2-point route that ping-pongs is a sentry walking up and back.
That is the corroboration, and nothing else in the reading forces it.

**Corpus**: 53 routes over 16 maps and 13 floors; lengths 2..27; ids 1..14;
**0 points carry a nonzero `clipId`**, which is why state 5 below can be ported
and can never be seen.

## 2. The THIRD OPERAND of `shoot.actor.action` — a finding

Op 84 is **6 bytes, three int16 operands** (the table said 4; corrected long
ago), and `0x00404090` pushes them as `Shoot_ActorAction(actor, action, a3)`.
For action 1, `a3` is the route to take:

* `a3 == 0` — **the NEAREST FREE route**: `sub_4354E0`'s `a4 <= 0` arm walks the
  floor's list and keeps the one whose `(cellX, cellZ)` of its FIRST point is
  nearest his cell in squared distance, skipping any with bit `0x2`. 92 of the
  116 sites.
* `a3 > 0` — **by ID**, `(uint8_t)a3`, refused if bit `0x2` is up. 24 sites.

**And the high byte is the FLOOR.** The ids asked for are 4, 257, 258, 259, 260,
513…1795 — which as `(floor << 8) | id` name a floor of the area's OWN map that
really does carry that route id, **24 of 24**. The engine ignores it: it casts
to `uint8_t` and takes the floor from the actor's own `+188` instead. So it is
an authoring annotation that happens to agree with the runtime every time, and
it is worth recording because a reader meeting `1795` will otherwise look for
route 1795.

## 3. What the action and the two states do

**`Shoot_ActorAction` case 1** (`05_sys.c` 4079): pick a **type-9** clip (the
walk); `flags &= ~2`; `+144 = 1`; **`+156 = 4`**; `+24 = sub_4354E0(+188, +136,
+140, (u8)a3)`; **`+100 = 0`** (the point index); `sub_4356B0(route, +188, 0,
out)` → `+44`/`+48`, the TARGET x and z; start the clip. No clip, no patrol.

**State 4** (`05_sys.c` 5757), the walk:

* no route at `+24` → straight to the tail;
* `flags |= 0x200` (which is also what keeps a wall slide off his facing);
* `sub_426C20` — already ported as `shootMoveDecision` — returns 1 ARRIVED,
  0 keep going, or 90 / 180 / -90 to turn;
* **arrived**: `+100 = sub_435660(route, +100)` — the next index, wrapping to 0
  or ping-ponging — then `sub_4356B0` writes the new `+44`/`+48` and **returns
  the new point's `clipId`; nonzero → state 5**;
* a turn picks clip type 31 / 32 / 30 and sets `+184 = turn / Anim_Frames`, or,
  with no such clip, steps the facing ±5 / ±10 a frame directly;
* the tail releases the route whenever the state has left 4 and 5.

**State 5** (`05_sys.c` 5826), the pause: `sub_4272B0` case 4 starts
`sub_434630(clipList, sub_435750(route, +100))` — the point's own clip BY ID —
and state 5 waits until the clip's frame count is reached, then goes back to 4.
**Unreachable in the shipped data** (§1), and it is ported and labelled as that.

**Releasing.** `sub_435650` clears bit `0x2`, and there are five sites: state
4's tail, `Shoot_ActorAction`'s head when the action being replaced is 1, and
the three that take an actor out of shoot mode.

## 4. What this task is NOT

The handoff bundles the patrol with "the brain's walking states 1, 2 and 4 need
the nav EDGE at the record's `+4`". **States 1 and 2 are a different
mechanism** — they walk a navigation GRAPH edge that something else has to
supply, and the port already has their arms waiting on `ShootFrameIn::hasEdge`.
The patrol needs no edge at all: its target is a cell from the route. So the
nav edge and its path-finder stay an open item, and this task does not touch
them.

---

## 4b. Two defects the reading turned up on the way

**`+44/+48` and `+136/+140` were one pair of fields.** `shootMoveDecision` -
the port of `sub_426C20` - read `destX`/`destZ`, which the struct documents as
`+136/+140`, the CELL; the engine reads `f32(rec, 44)` and `f32(rec, 48)`, a
world point. Nothing caught it because the two consumers never ran together:
the cell's readers are the wall test and the steering, and `shootMoveDecision`
had **no live caller at all** - only a probe, which passed world values into
the cell fields and so agreed with itself. Fixed by giving the record
`goalX`/`goalZ`; the probe now sets those.

**`verify.py: shoot generic` had been RED for a day.** `324ef32` (B4) corrected
the hub's two expiry arms from `out.clipType = in.defaultClipType` to
`out.actionRequest = 0` - the right fix, trap 12's - and its expectation still
said the probe's sentinel `77`. The check went red at that commit and nothing
ran it. The expectation now asserts the corrected behaviour and the probe
reports the ACTION beside the clip, so the same slip cannot read as a hole.

**The occupancy stamp was laid once and never lifted.** `sub_420B80` stamps a
gunman's cell 0x80 after his tick and saves the byte it covered at `+189`; the
brain's PROLOGUE puts that byte back (`sub_424DE0` 5456) before anything reads
the grid again. The port had only the stamp, behind a `!cellStamped` gate, so a
gunman who walked left a 0x80 on the cell he started from - for ever - and
every other gunman's wall test and the path field routed around a ghost. It was
invisible while no gunman's cell ever moved, which was true until `Shoot_Think`
went in: 237 reads cell (11,28) at frame 8 and (17,21) by frame 85.

**`sub_426E00`'s first line had never been able to fire.** `shootEngage` opens
`if (r.node == -1) return 0` - a gunman not on a floor does not engage - and
with `+188` stuck at the memset's 0 that guard was dead. It now works, and it
moves the arenas visibly: the gallery's gunmen used to engage and shoot from
frame 4, the frame after they are staged, and now do it at frame 8, the frame
their bodies first land on the grid.

**A LABELLED LIMIT of this port, found by the same change and left open.** The
engine's `Shoot_Think` asks `Actor_GetPosAndFacing`, which is the node's
position and always answers. This port's brain thinks from `drawAt`, which is
written only where a body is DRAWN - so a gunman the camera never sees (the
gallery's 240 stands behind the player at every yaw tried) has no position at
all, gets floor -1, and the guard above then keeps him out of the fight.
Feeding him his PLACEMENT instead was tried on 2026-09-12 and is worse: he
engages and fires from a body with no pose, so the muzzle - the posed `tir`
node - is the world origin. The two have to agree and agreeing on the drawn
point is the conservative half. Closing it properly means posing a staged body
whether or not it is drawn, which is a task of its own.

**And a stale object file made one diagnosis twice as long.** Comparing my tree
against `HEAD` by stashing gave three differing numbers that my change could
not possibly explain; `make` had relinked `shoot_range` against a
`build/obj/src/actor/shoot.o` from the other tree. Deleting the object files by
name and rebuilding showed the hub identical on every shared element. CLAUDE.md
§1 already warns about this; the practical form is **delete the object files by
name after switching trees, before believing any measurement**.

## 5. The steps

Each ends in a commit and a report.

| # | step | state |
|---|---|---|
| 1 | **The route data**: `Map2dWaypoint` given real fields, the runtime reservation, and the four lookups (`routeNearest`, `routeById`, `routeNextIndex`, `routePoint`, `routeRelease`). A probe and a check over the 53-route corpus and the 24/24 floor operand | done |
| 2 | **`Shoot_ActorAction` case 1** and the record's route fields, against the lookups | done |
| 3 | **`Shoot_Think`'s floor**: a gunman's `+188`, which the route lookup needs and which has been the memset's 0 since the brain was wired - and with it the occupancy cycle's missing RESTORE | done |
| 4 | **Wire it in `play.cpp`**: op 84's third operand carried, the route acquired, the target fed to `shootMoveDecision`, the advance, state 5, the release | done |
| 5 | **Play, docs, checks** - including the two whole-run checks step 3 left RED (below), and REACHING an arena that patrols (5a) | **next** |

### 5a. NO ARENA THIS PORT CAN REACH STAGES A PATROL — measured

Step 4 wired the patrol into the frame loop and then could not show it running.
**Section 5b reaches one**; this is what had to be got past, and it is worth
writing down rather than discovering twice.

* **The Shooting gallery has no routes at all.** `gallery.mpt` carries 0
  waypoints, and its 240 — the one gunman whose scene action is 1 — is also
  never DRAWN (he stands behind the player at every yaw tried), so he has no
  floor either. He logs `NO ROUTE on his floor` and stands, which is what the
  engine would do with an empty list.
* **The supermarket stages only robber 77**, whose action is 3. SCENE 56's two
  action-1 sites name actors 516 and 518, and neither is staged on this route.
* **The arenas that DO patrol need a zone the player must walk into.** AREA 141
  (`hames`, 26 routes, 19 action-1 sites) is the richest; AREA 71 (`soukdock`,
  11/12), AREA 2 (`grotte`, 9/10) and AREA 63 (`archiv03`, 3/3) follow. Each
  carries its `shoot.begin` in zone slots, not in a `+4` startup script, and
  standing at the centre of all five of AREA 141's shoot zones fires none of
  them — so something else gates them, and finding it is the next step's.

**And a third of the shipped patrols have nowhere to walk.** Of the 116
`shoot.actor.action 1` sites, 44 are in areas whose map carries routes; **32
are in areas whose map carries NONE** (`tetra2` 17, `archiv05` 9, `tetra3` 5,
`tetradou` 1) and 10 more name `archiv04`, the one map that was cut and does
not ship. That is the engine's own data, not a gap in the reading: those
gunmen get a null route and stand, exactly as the gallery's 240 does.

So the walk is shown at unit level instead, over the supermarket's own route 1,
in `verify.py: shoot fire`'s `patrol walk:` — the ring walked 0-1-2-3 and
wrapped back to 0 in 54 ticks, the route held reserved throughout.

### 5b. REACHED — the catacombs patrol, and the one-opcode harness that got there

**`--zone-enable N` is `zone.enable`, the opcode and nothing else.** AREA 141's
`ZONES[2295]` 'Start Shoot' is enabled by the Nout book cutscene, which a
headless run cannot reach, and the zone is what holds the whole phase: with the
bit set the player walks in and the zone runs **its own** enter script, which
is what calls `shoot.begin`, ten `shoot.actor.enter` and ten
`shoot.actor.action <who>, 1, <route>`. Nothing else is faked. (`Session::
enableZoneById` does the two lines `interp.cpp`'s op-64 arm does: the state bit
and `Zones_RegisterAll`, because the live zone list is a snapshot filtered at
registration.)

```
build/omk-play "$OMK_DATA" ../tables --save ../traces/save-appart.bin \
    --area 141 --zone-enable 2295 --stand 42786,854,-2380,0 --hold "k200*180"
```

**The zone quad needs the loader's unit conversion**, and reading it raw is how
this went looking in the wrong place first: `(100 * v) * 0.00390625 * 0.3937 -
1`, which `engine/src/script/world.h` already carries with a comment saying an
actor once came out 43000 units from the zone he was standing in. Zone 2295's
raw corners read 278217; converted they are 42786, which is where hames is.

**What it shows.** Nine spectres take a route each, and **every operand's high
byte is the floor `Shoot_Think` found for its actor** - `0x201` on floor 2,
`0x104` on floor 1, `0x401` on floor 4, `0x703` on floor 7, `0x502` on floor
5 - which is §2's corpus finding confirmed at runtime rather than in the data.
Then they walk: 20 waypoint advances in 500 frames, and actor **595 closes his
nine-point ring - 8 back to 0 - at frame 483** and starts a second lap. Actor
591 reaches point 9 of 10. Nobody reaches state 5, because no shipped waypoint
carries a clip.

`verify.py: engine: shoot patrol` (--slow) holds all of it.

**Two things the run cost, and both are worth the warning.**

* **The entry action had to WAIT for the grid.** `Shoot_ActorEnter` thinks
  first and acts second, and the port cannot always honour that: a body staged
  this frame has not been drawn, so it has no position (§4b), and the ten
  spectres were taking action 1 with floor -1 and getting no route at all. The
  action is now held until the first tick `Shoot_Think` succeeds - one frame -
  and the log says so.
* **A floor index outlives the floor, and it SEGFAULTED.** `Shoot_Think` writes
  `+188 = -1` the moment a walker leaves the grid, while his `+24` route index
  stays as it was; a log line indexing `floors()[(size_t)-1]` killed the viewer
  at frame 63 of the catacombs. Every read of a floor by that byte is guarded
  now. The engine is not exposed to this because its lists are pointers, not
  indices - a null pointer is a test it already makes.

### `engine: shoot hit` IS STILL RED, and it must NOT be baselined as it stands

`engine: shoot fire`, `engine: shoot gunfire` and `engine: shoot patrol` are
re-baselined green. **`engine: shoot hit` is not, and the reason matters more
than the red.**

That check is the port's only proof that a gunman DIES from the player's fire:
it stands at a fixed yaw, taps `Tir` forty times and asserts three bolts into
actor 240 taking his 15 health to 10, 5 and 0, with the death clip's band and
the enemy count. With the gunmen's y pinned and their cells now following them,
240 is no longer where that aim points: the run hits 237 and 238 once each
(15 -> 10 apiece) and **kills nobody**, so four of its elements come back as
empty lists.

**Baselining those empties would gut the check** - it would then assert that
the player kills nothing, pass for ever, and never notice if the kill path
broke. That is exactly the vacuous pass CLAUDE.md 1 warns about, and it is worth
refusing.

What it needs instead is a new AIM, so the kill exists again. Notes for whoever
does it:

* the check's own docstring already says **yaw 258** where its code passes
  **215**, and 258 is what the port's own convention gives for 240's placement
  from (5000, -2900): `atan2(103, -484) * 180/pi + 90`. The docstring was right
  and the code drifted;
* at 258 the gunmen do fire on him and his own bolts fly, but which body they
  meet has not been pinned down - that is the remaining work;
* alternatively aim at 237 or 238 deliberately and re-write the element as
  "three bolts into <him>", since nothing about the check requires it to be 240.

### Owed at step 5: THREE checks left red on purpose

`engine: shoot gunfire`, `engine: shoot hit` and - since the y was pinned
(§6) - `engine: shoot fire` all replay a whole arena, so
every number in them moves when the gunmen move - and step 3 made them move for
two right reasons (`sub_426E00`'s floor guard now works, and the gallery's 240
patrols instead of standing). They are left RED rather than re-baselined twice,
because step 4 wires the patrol into the frame loop and will move them again.
What changed so far, so the re-baseline is a check and not a copy:

* the gallery's 237 fires at frame **8**, not 4 - he engages when he reaches
  the grid;
* **240 never fires**: his scene action is 1, so he patrols;
* the three gunmen's positions differ by tens of units, because they walk;
* `engine: shoot hit`'s second kill is band 3 where it was band 2, the bolt
  meeting a gunman who has turned.

One rot was fixed on the spot rather than deferred: the gunfire check's
first-shot pattern had `^frame 4:` written into it, so when the shot moved the
element went to **None** - "the line is gone" rather than "it moved". The frame
is captured now.

---

## 6. `sub_421370`'s VERTICAL — why a spectre walked out through a wall

A reader watching the catacombs: *"I see one go out of a wall."* Read
2026-09-12, and the answer is that **a shoot gunman has no vertical at all.**

`sub_421370` (05_sys.c 2704) moves him from the clip's root delta
(`sub_434D30` into `v46`, `v47[0]`, `v47[1]` = dx, dy, dz), and the two arms
that can move him both drop dy:

* `dx == 0 && dz == 0` — nothing horizontal — moves ONLY the vertical, and only
  when `a1+460` is set: `o3de_MoveNodeBy(node, 0, dy, 0)`;
* otherwise the wall test `sub_421140` is given `{dx, 0, dz}` and its two
  slides `{0, ±d}` and `{±d, 0}`. **dy never reaches the node.**

His y comes from one place, the CLIP WRAP:

```c
if (frame + dt >= Anim_Frames(...) && !a1[460]) {
    f32(a1, 188) = dt + 1.0;                       // the frame back to 1
    o3de_SetNodePos(node, a3[0], f32(rec, 60), a3[2]);   // y PINNED to +60
}
```

and `+60` is set once, at `Shoot_ActorEnter` (05_sys.c 3788):

```c
v24 = max over the model's OWN collision spheres of (sphere.y + sphere.r);
f32(rec, 64) = v24;                                   // +64, his HEIGHT
f32(rec, 60) = f32(dword_907D00[floor], 12) - v24;    // +60 = floor.bound[3] - it
o3de_SetNodePos(node, x, f32(rec, 60), z);
```

The sphere list is the descriptor's `+244`/`+248`, 16 bytes of `(x, y, z, r)` —
the same list `modelSweepSpheres` already reads. Y points DOWN, so
`max(y + r)` is the LOWEST point of the body below its origin: his feet. `+60`
is the floor's own lower edge minus that, which stands his feet on the plane.
**So his y is a constant, chosen at entry and re-asserted every clip loop.**

**What the port did instead, and what it cost.** `Shoot_Think` was handed
`drawAt[1]`, the ground probe plus the clip's accumulated root motion. Two of
the catacombs' nine spectres sank about three quarters of a metre by frame 72 —
605 went from y 1025 to just past **1054**, which is floor 5's own `bound[3]` —
dropped out of their floor's box, and `floorAt` then answered -1. With no floor
there is no wall test and no steering, so the walk clip carried them in a
straight line for ever: 605 ended 3000 units west and 2600 north of `hames`.
Nothing in x or z was ever wrong — he was inside floor 5's footprint the whole
time.

Ported: `Session::modelFeetDrop` is `+64`, and `+60` is computed the first tick
`Shoot_Think` finds him a floor (his ENTRY has none, because a body staged that
tick has not been drawn), then used as his y from then on. **Measured**: off-grid
gunmen 2 → **0**, and waypoint advances over 500 frames 20 → **31**, because a
gunman who keeps his floor keeps steering.

`sub_4368E0` was read on the way and is NOT needed for this: it is the engine's
spiral search for the nearest cell that is not 0, 2, 3 or 0x80, out to four
rings, which it writes back into the position — "put him where he can stand".
`Shoot_ActorEnter` runs it once at entry and `Shoot_TickPlayer` every frame for
the PLAYER; a gunman mid-walk never needs it, because with his y pinned the
wall test never lets him leave.

---

## 7. THE ENGAGE'S SIGHT IS A RAY, NOT THE GRID — a wrong turn, backed out

A reader, after the y was pinned: *"Nobody hits me, is it normal?"* and, of the
catacombs, *"No enemy moves."* The second was real and the diagnosis was half
right; the fix was wrong and is **reverted**.

**Why nothing moved.** `sub_426E00`'s first engage arm is
`if (dist < acquireRange && los) { flags |= 0x20; state = 6; }`, and the port
handed it `los` hard-coded TRUE. So every gunman within 30 m engaged on his
first tick — through solid rock — went to the hub, found the timer 0 (a patrol
sets none) and asked for action 0, *stand and fire*. The catacombs' 589 and 601
did it at frames 39 and 43 and stood for the rest of the phase.

**Why the fix was wrong.** Wiring `Map2d::lineOfSight` - the `sub_4359A0` walk
the port already owns - into that gate did make the spectres patrol (advances
65 -> 117, all nine travelling, 0 pulled off) and **stopped the Shooting
gallery's gunmen firing at all**: its range really does have barriers between
their placements and the player's, so the grid walk says BLOCKED at
(11,29)->(17,21) and (22,37)->(17,21), and `engine: shoot fire`,
`engine: shoot gunfire` and `engine: shoot hit` all went red together.

**What `sub_426E00` actually reads**, gone back to after the regression:

* its sight test is **`sub_4449E0`** - a RAY against geometry, the port's
  `EngageIn::rayHits` - and NOT `sub_4359A0`. The grid walk answers a different
  question and belongs to the noise and the path field;
* **character type 12 has its own arm**, and type 12 is the SPECTRE: cone plus
  `!sub_4449E0` sends him to state 3 with the 0x20 latch, and **anything else
  sends him back to state 4, the patrol** (`LABEL_24`). So a spectre who cannot
  see you returns to his beat by the engine's own rule, without the grid ever
  being consulted;
* `sub_436BB0(myFloor, targetFloor, pos)` writes the record's **`+4`** - the
  NAV EDGE the handoff has been calling unread. That is the path-finder's front
  door, and it is reached from the engage.

So the sight is left as it was, loudly, and the three checks are back where they
were. Closing it properly means porting `sub_4449E0` (the ray) and type 12's
arm, which is its own task and wants a reader in front of it.

### And the answer to "nobody hits me"

**It is the engine's own rule for these enemies, and the port says so in its
log.** The fire epilogue gates on `sub_4348B0(+20)`, which is the animation
group's `+8 & 1`, and SPECTRE's group carries 0 - so a spectre takes the aim
pose and never shoots. The port prints exactly that: *"the fire test is clear
(sub_4348B0: ANIMS\SPECTRE.ANI group 12, +8 0) - he aims and never shoots"*.

**And nothing else in shoot mode can hurt you**: `sub_4240E0`, the hit, has two
callers and both are in `Actor_TickProjectiles`. There is no melee path in the
mode at all. So in the port the catacombs' spectres are harmless, and whether
the game means them to be - a stealth stretch - or hurts you through some other
system is NOT settled by anything read here.
