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

## 5. The steps

Each ends in a commit and a report.

| # | step | state |
|---|---|---|
| 1 | **The route data**: `Map2dWaypoint` given real fields, the runtime reservation, and the four lookups (`routeNearest`, `routeById`, `routeNextIndex`, `routePoint`, `routeRelease`). A probe and a check over the 53-route corpus and the 24/24 floor operand | **next** |
| 2 | **`Shoot_ActorAction` case 1** and the record's route fields, against the lookups | planned |
| 3 | **`Shoot_Think`'s floor**: a gunman's `+188`, which the route lookup needs and which has been the memset's 0 since the brain was wired | planned |
| 4 | **Wire it in `play.cpp`**: op 84's third operand carried, the route acquired, the target fed to `shootMoveDecision`, the advance, state 5, the release | planned |
| 5 | **Play, docs, checks** | planned |
