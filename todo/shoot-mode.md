# Shoot mode — the plan, and the decision it reopens

`todo/next-tasks.md` item 18. Started 2026-09-09 with a reading pass, because
the item's own triage says the first move is not code: it is to revisit
`todo/standing-unknowns.md` §2, the standing decision **not** to wire the AI's
brains. That decision rests on one sentence —

> the real branch needs the navigation node, the line of sight and the
> weapon's range, **none of which this tree has**

— and the first of the three turns out to be wrong. **The navigation data
ships.**

---

## 1. What shoot mode IS — read 2026-09-09

`Shoot_Enter` (0x004222D0, `readable/src/05_sys.c` 3396) is 78 lines and every
one of them is a decision this port can make:

| what it does | detail |
|---|---|
| allocates the records | `Mem_Alloc(0x4B00)` = **100 × 192 bytes**, one per actor, zeroed |
| flags the mode | `g_ShootMode = 1`; the player's record gets `+188 = -1`, `+160 |= 2` |
| moves the player | actor `+404 = 3` (`ACTOR_STATE` 3), `+1302 = 1` |
| **loads its own library** | `Game_Start("shoot2.scx")` and `scptdata\shoot2.sfx` — shoot mode has a sprite/sound library of its own, the way `aventure.scx` is the adventure one |
| picks a HUD | event 44 property 7 (the character TYPE) on the PLAYER: type 5 (Mecagarde) → `UI_LoadScreen(33)`, anything else → **screen 34** + `Hud_Refresh()` |
| takes the camera | `Camera_Request(4, …)`, both camera actors = the player |
| swaps the channel | `.CTL` **group 200**, its default entry, through `SetPersoBankGroup` |
| arms the weapon | if actor `+44`, event **48** → `sub_41C490` + `Shoot_InitWeapon(actor, rec)`, `dword_90E104 = 1` |
| swaps the controls | `Input_InstallScheme(2)` — the scheme this port already runs |
| ticks once | `Shoot_TickPlayer(player, index)` |

`Shoot_TickPlayer` (0x00427AC0) has two arms. `ACTOR_STATE` **15** is the
respawn/wait arm — it counts `dword_4E975C` down by one frame a tick and then
does the whole of the entry above again, which is what makes 15 "dead, about
to come back". The other arm is the live one, and it is where the grid is.

## 2. THE FINDING: `MAP2D` is the shoot AI's navigation grid

`docs/FILE_FORMATS.md` §5b5 has the format solved and calls it "the in-game
map screens". That is at most half of what it is.

`Shoot_Think` (0x00420AB0) is 31 lines:

```c
v8  = sub_435020(x, y, z, hint);      /* which FLOOR contains him       */
u8(rec, 188) = v8;                    /* the shoot record's node        */
if (v8 == -1) return 0;
v10 = sub_435770(v8, x, z);           /* world -> cell, packed x|z<<16  */
if (sub_4353E0(v8, (uint8_t)v10, v10 >> 16)) return 0;   /* cell blocked */
u32(rec, 140) = v10 >> 16;  u32(rec, 136) = (uint16_t)v10;
```

and every one of those functions reads the table `Map2D_Load` (0x00434E30)
fills — `dword_907D00[floor]` (the floor record), `flt_907EAC` (the cell size),
`dword_907DE0[floor]` (the `W*H` cell bytes), with the row stride at floor
`+24`, which is the `W` the `.mpt` walk already decodes:

```c
sub_4358D0(f, x, z) -> i8(dword_907DE0[f] + z * u32(dword_907D00[f], 24), x)
sub_435970(f, x, z, v) -> that byte = v
```

**The data corroborates it exactly, 16 of 16 both ways.** The 14 AREAs whose
own scripts carry a shoot opcode all name a map at `+106`; the only two maps
left over are AREA 230 (`SMARKET1`) and AREA 249 (`SOUKT`), and SCENE 56
("1-10 Supermarché Shoot") and SCENE 62 ("2-16 Toits Jaunpur Antenne") load
over exactly those two. No map belongs to a place with no gunfight, and no
gunfight happens in a place with no map. `verify.py: shoot arenas`.

So the file is one grid with two consumers. **What the write is, I guessed
wrong in this paragraph and step 2 settled it**: `sub_435970(node, x, z, 128)`
is not a map reveal, it is an actor CLAIMING the cell — see §2b.

**This is the §1 shape twice over.** A format was decoded, named for the
consumer that happened to be found first, and the name then stood in for the
fact; and a negative result — "this tree has no navigation data" — was a fact
about where anyone had looked, not about the tree.

## 2b. The grid, decoded — step 2, 2026-09-09

`engine/src/formats/map2d.{h,cpp}` reads it and `engine/tools/map2d_probe.cpp`
draws it. The reader refuses anything that does not land **exactly** on the
file size, which is the walk's own self-check, and it holds for all 16.

**The floor's `bound` is a BOX, not an origin**: `[minX, maxX, minY, maxY,
minZ, maxZ]`. The loader only ever touches `+0`, `+12`, `+16`, `+24` and
`+28`, so the order is established from the data instead — `W * scale` matches
`maxX − minX` and `H * scale` matches `maxZ − minZ` to within one cell in
**79 of 79** floors. And the scale is 39, 78 or 117 world units, which against
the engine's own inch (`"Hauteur : %f meter"`, 39.37) is **1, 2 or 3 metres a
cell**.

**The cell byte.** `sub_4353E0` refuses exactly `{0, 2, 3, 0x80}`, plus
anything outside `1 <= x < W`, `1 <= z < H` — note the low margin of ONE. The
shipped census over 74987 cells:

| value | cells | what it is |
|---|---|---|
| 0 | 46840 | outside the room — blocked |
| 1 | 22698 | floor |
| 2 | 4556 | an obstacle — blocked |
| 3 | 0 | refused by the code, never shipped |
| 8 | 36 | **passable, and nothing branches on it** |
| 0x10..0x17 | 142 | a DOOR, low nibble = its slot |
| 0xCD | 715 | **passable, and nothing branches on it** |
| 0x80 | 0 on disk | an ACTOR, written at runtime |

* **0x80 is occupancy, not reveal.** Every mover reads the cell it is about to
  stand on into its own record `+189`, stamps 128 over it, and writes the
  saved byte back when it leaves (`sub_435970` / `sub_420C10`, and the same
  pair inside the shoot mover at `05_sys.c` 5599). So "blocked" means *wall or
  somebody standing there*, and the AI's collision avoidance is this grid.
* **0x10 is a door.** `sub_435270`'s default arm indexes
  `doorTable[16 * floor + (c & 0xF)]` — the 192 bytes per floor that
  `FILE_FORMATS` called runtime scratch are **16 slots of 12 bytes**, each
  `{arm, openObject, closeObject}` naming SCENE OBJECTS that
  `ScriptObject_Start` runs (24_sys.c 4569) — and asks the scene for that
  object's state before letting the cell be crossed. The same door pairs this
  repo was fixing an hour earlier in `omk-play` 94.
* **8 and 0xCD are carried through unfolded.** Nothing in the binary
  distinguishes them from floor, so to the AI they are floor; folding them
  into "walkable" would destroy the evidence for whatever does read them.
  (0xCD is also MSVC's uninitialised fill, which is a hypothesis and not a
  finding.)

**And it was LOOKED at before it was measured.** `map2d_probe <data> gallery
0` draws AREA 59's floor as 71x40 characters and it is unmistakably a floor
plan — rooms, corridors, doorways, pillars. A census cannot tell a navigation
grid from noise; a picture settles it in five seconds.

One incident worth keeping: restoring the mutation used to show
`map2d grid` failing left `make` saying **"up to date"** with a stale object,
because the header edit and the previous build landed in the SAME SECOND. The
check then reported the mutated refusal set after the fix was back. That is
CLAUDE.md §1's stale-object trap wearing a different hat, and `touch` on the
header is the whole of the fix.

## 3. Where shoot mode happens

30 `shoot.begin`, 74 `shoot.end`, 317 `shoot.actor.enter`, 319
`shoot.actor.action`, 108 `shoot.player.suspend` / 104 resume, over **21
chunks** — and they are the places the names promise: the Shooting gallery,
the four Jaunpur Tetra towers, the CS Archives 03/04/05, Gandhar's cave, the
Docks, the Supermarket, Mayerem Hamestagan, Ix Astaroth, Bar 56, the Toits
antenna. `verify.py: shoot mode` already pins the opcode side.

## 4. The steps

Each ends in a commit and a report.

| # | step | state |
|---|---|---|
| 1 | **the reading above** — what the mode does, and the grid finding | **done 2026-09-09**; `verify.py: shoot arenas` |
| 2 | **the grid** — the cell byte, the floor box, the door table, a reader and a probe that DRAWS a floor | **done 2026-09-09**, §2b; `verify.py: map2d grid` |
| 3 | **the mode**: enter and leave ported into the Session — the records, `ACTOR_STATE` 3, group 200, camera mode 4, scheme 2, `shoot2.scx`, the 33/34 HUD choice — with ops 80/81 and suspend/resume wired, and an `omk-play --shoot` harness that stands in an arena in shoot mode | |
| 4 | **the player's half**: `Shoot_TickPlayer`'s live arm on the grid, `Shoot_StartTargetScripts`, `Shoot_InitWeapon` and event 48, and what a shot actually IS | |
| 5 | **the brains, decision revisited** — with the grid in hand, how much of the generic shooter's 16 states is now fact rather than geometry. Gandhar is already exact; Astaroth and the generic are state graphs. **Only what the grid settles gets wired**; the rest stays labelled | |
| 6 | docs, the checks, and a play test | |

## 5. What is still NOT established

Written down so step 5 cannot quietly assume it:

* **line of sight.** The grid may or may not answer it — the wall segments in
  each floor are a candidate and nothing has been traced to them yet.
* **the weapon's range.** `Shoot_InitWeapon` and event 48 are unread.
* **what a shot is.** `Projectiles_Tick` exists in the frame loop; whether a
  shot is a projectile, a ray, or a scripted event is unread.

Until those three are read, the decision of `standing-unknowns` §2 stands for
the generic arm. What has changed is that it is now a question with an
answer in the tree rather than a gap outside it.
