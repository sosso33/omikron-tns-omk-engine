# THE NAV EDGE — `todo/handoff-shoot-mode.md` §4 item 3

Planned 2026-09-12. The handoff has called this "the path-finder" and "the
largest thing in shoot mode" since the brain was first read. **It is neither.**
There is no graph search anywhere in it: `sub_436BB0` is a 37-line nearest-link
lookup over a table the port already parses, and the two states that walk the
result are already ported and waiting on their input.

---

## 1. The 28-byte records are INTER-FLOOR LINKS, not wall segments

`engine/src/formats/map2d.h` reads, per floor, `u32 nSegs` then `nSegs` records
of `{ u32 kind; f32 v[6] }` and calls them **wall segments**, with every field
but the count unread. `sub_436BB0` and `sub_424DE0`'s state 2 name all of them:

```
u32 destFloor     +0    the floor this link ARRIVES on
f32 from[3]       +4    where you step on,  on THIS floor
f32 to[3]        +16    where you step off, on destFloor
```

`sub_436BB0(myFloor, targetFloor, pos)` walks my floor's list, keeps only the
records whose `+0` is the target's floor, and returns the one whose
**`from`** is nearest my position in squared XZ distance — or 0. That is the
whole of the "path-finding": **one hop, nearest door.**

**The data settles it, 36 of 36.** Every record's `+0` is a real floor of its
own map, its `from` lies inside the SOURCE floor's bounds and its `to` inside
the DESTINATION's. And they come in **reciprocal pairs** - `floor 0 -> 1 from A
to B` always has a `floor 1 -> 0 from B to A` beside it - which is what a
staircase described from both ends looks like and is not something a wall
segment would do. `bar56` has five such pairs between its floors 0 and 1,
`hames` four, `tetradou` six, `tetra3` four, `tetra2` one; the other eleven maps
have none, which is why there are only 36 records across 79 floors - a number
that never made sense for walls.

## 2. How the brain uses it

`sub_426E00` (`05_sys.c` 6922), the arm that runs when the gunman has the 0x20
latch - he has SEEN the player - **and the two are on different floors**:

```c
v16 = sub_436BB0(myFloor, targetFloor, myPos);   // the nearest link
u32(rec, 4) = v16;                                // +4 = THE LINK
if (v16) {
    u32(rec, 44) = u32(v16, 4);                   // the goal x = link.from.x
    u32(rec, 48) = u32(v16, 12);                  // the goal z = link.from.z
    u32(rec, 156) = 1;                            // STATE 1
    return 0;
}
v18 = sub_421020(rec);                            // the fallback, still UNREAD
```

So he walks to the near end of the nearest stair, and the two walking states do
the rest - **both already ported**:

* **state 1** steers at `+44`/`+48` with `sub_426C20` and, when the step is
  available and the cell walkable, writes the heading and length and goes to
  **state 2** (`actor/shoot.cpp` case 1, waiting on `in.hasEdge`,
  `in.edgeFrom`, `in.edgeTo`);
* **state 2** walks the edge itself, climbing `(to.y - from.y)/dist * moved`
  with `+60` following, and at the far end places him at `to` minus his height,
  takes the link's `+0` as his new `+188`, and returns him to the hub.

## 3. What this task is NOT

`sub_421020` - the fallback when no link reaches the target's floor - stays
unread. So does what happens when the player is two floors away: there is no
chaining here, and a gunman whose floor has no direct link to the player's gets
the fallback instead. Both are named, not glossed.

---

## 4. The steps

| # | step | state |
|---|---|---|
| 1 | **The link table named**: `Map2dSegment` becomes the link, its six floats given their meaning, and a check over the 36 records - the destination in range, both points inside their floors, and the reciprocity | done |
| 2 | **`sub_436BB0`** ported as `Map2d::linkTo(floor, destFloor, pos)`, with a probe | done |
| 3 | **The engage's arm**: `+4`, the goal, state 1 - and the edge fed to states 1 and 2 | done |
| 4 | **Play it**: AREA 232's seven latched gunmen over `bar56`'s ten links - and its zones are on the ACTIVATE slot, so it needs a button press (§6) | **next** |
| 5 | **Docs and checks** | planned |

---

## 5. A weak check, twice, and what fixed it

`verify.py: shoot links` asserts `sub_436BB0`'s pick, and the first two
mutations of the very line it is about **passed**:

* `>` to `>=` - the tie-break - changes nothing, because no two links share a
  near end and a distance of 0 cannot be tied;
* measuring the distance from the link's FAR end instead of its near one
  changes nothing either, on any query the probe first had: the stairs are
  short and far apart, so the wrong end of the right link still ranks first.
  Printing the picked link's two points by value did not help, because those
  come from the struct and not from the measurement.

What fixed it was **choosing a query for the purpose**. Links 2, 3 and 4 of
`bar56` floor 0 all have their near end at x 11750, so only z decides between
them; their `from` z are -379, -184 and -106 and each `to` is about sixteen
units up the stair. At **z = -274** the near ends favour link 3 (90 against 105)
and the far ends favour link 2 (89 against 106) - so that one query turns 3 into
2 the moment the wrong end is measured, and nothing else in the probe moves.

The general form, and it is CLAUDE.md 1's rule from the other side: **a check
over a corpus can be insensitive to the field it is named after.** Ranking tests
are especially prone to it, because a wrong metric usually agrees with the right
one everywhere except in a band you have to go looking for.

---

## 6. THE LATCH IS THE GATE — why the arm is reached and does nothing (yet)

Step 3 is in: the record carries `+4`, `sub_426E00`'s cross-floor arm looks up
the stair and sends him to state 1, states 1 and 2 get the edge, and state 2's
far end places him and takes the link's `destFloor` as his new `+188`.

**It is reached in hames and does nothing there, and that is the engine's rule
rather than a gap.** All ten of the catacombs' spectres hit the `!sameNode`
branch at frame 29 - the player is on floor 0 and they are on 1, 2, 4, 5 and 7 -
and **not one of them is LATCHED**. The arm's first test is `flags & 0x20`, "he
has seen the player", and the latch is raised by

* `sub_426E00`'s own same-floor arms - which a gunman on another floor never
  reaches, so it cannot bootstrap itself;
* `Shoot_ActorAction` cases **2 and 3**, which set `flags |= 0x20` at the
  action; and
* the NOISE, `sub_4246E0`, whose alert sets `0x60` - but only for a gunman on
  the **shooter's own floor**.

Action **1**, the patrol, raises nothing. So a patrolling gunman never chases
you upstairs: he waits on his beat until you come to his floor. What he does
instead is the arm's own `LABEL_24` - **state 4, back to patrolling** - and that
is measurable: wiring the arm took the catacombs from 31 waypoint advances in
500 frames to **38**, and from one closed ring to **two**. A faithful arm made
them patrol MORE.

### Where it CAN fire, and what stands in the way

Of the four areas whose map carries links and whose script latches a gunman:

| area | map | links | action 2 | action 3 | patrol |
|---|---|---|---|---|---|
| 232 | `bar56` | 10 | 0 | 7 | 0 |
| 141 | `hames` | 8 | 8 | 4 | 19 |
| 61 | `tetradou` | 8 | 0 | 7 | 1 |
| 77 | `tetra2` | 2 | 1 | 10 | 17 |

**AREA 232 is the test**: seven gunmen, every one on action 3 and so latched at
entry, ten links between two floors, and no patrols to confuse it. Its two
`shoot.begin` zones are 3977 (centre 12062, -1, -483) and 3978 (11644, -1,
-232), and `--zone-enable` plus standing in them fires **neither** - because
both are on slot **+4**, the ACTIVATE slot, where hames' 2295 was on `+0`, the
enter. An activate needs the player to PRESS the action button inside the quad
(`Script_Pump` state 2's press cycle), not merely to walk in. That is what step
4 has to arrange.

Also worth noting: hames has **8 action-2 gunmen** of its own, so the catacombs
do contain latched ones - they are simply not the ten the 'Start Shoot' zone
stages, and some other zone brings them.
