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
| 2 | **`sub_436BB0`** ported as `Map2d::linkTo(floor, destFloor, pos)`, with a probe | **next** |
| 3 | **The engage's arm**: `+4`, the goal, state 1 - and the edge fed to states 1 and 2 | planned |
| 4 | **Play it**: a gunman following the player between two of hames' floors | planned |
| 5 | **Docs and checks** | planned |
