# Shoot mode — the plan, and the decision it reopens

`todo/next-tasks.md` item 18. Started 2026-09-09 with a reading pass, because
the item's own triage says the first move is not code: it is to revisit
`todo/standing-unknowns.md` §2, the standing decision **not** to wire the AI's
brains. That decision rests on one sentence —

> the real branch needs the navigation node, the line of sight and the
> weapon's range, **none of which this tree has**

— and **all three turn out to be wrong**. The navigation data ships (§1-2),
the weapon's rate is traced and the range is a character property (§5b-5c),
and the line of sight is a ray cast the port already has the primitive for
(§5c). The premise the decision rested on is gone.

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

## 3b. The mode, ported — step 3, 2026-09-09

`engine/src/actor/shootmode.{h,cpp}`, wired into the Session at ops 80 and 81
(`area.cpp`), with `engine/tools/shootmode_probe.cpp` driving it through a
SHIPPED script rather than a harness call.

**`shoot.begin`'s operand is a WEAPON OBJECT**, which nothing here knew: the
opcode table gives it no tag and the docs had no meaning for it. The handler
(0x00403E80) hands it to `Weapon_SlotForObject` (0x0040EA50), which scans the
ten int16s at `IAM\GLOBAL +42` — slots 5..14 — and the answer goes into
`dword_4C0134` **only when that is still −1**, so a gun already in hand beats
the script's choice. An object naming no weapon falls to slot **11**.

The shipped table, and it is only seven deep:

| slot | object | |
|---|---|---|
| 5..9 | 189, 190, 192, 17, 191 | Double-Waver, Octogun, Decagun, Megazooka, Hypra |
| 10 | 40 | Bâton de pouvoir |
| 11 | 42 | **Gun Waver** — the default |
| 12..14 | −1 | unused |

and the corpus lands on it exactly: **27 of the 30** `shoot.begin` sites pass
−1, which `Weapon_SlotForObject` refuses on its first line, so they open with
the Gun Waver; the other three pass object 40, slot 10. `shoot.end`'s operand
is a CLEAR flag — 43 zeros, 30 ones and one −1, so 31 of the 74 drop the
weapon and the rest leave it in hand for the next fight.

**Ported**: the entry's eleven decisions as constants and state (records
100×192, `ACTOR_STATE` 3 in and 1 out, `.CTL` group 200, camera mode 4, input
scheme 2), the HUD screen from the player's character type (33 for a
Mecagarde, else 34), and the library swap — `shoot2.scx` on the way in,
`aventure.scx` on the way out. Ops 82/84 now feed the same object instead of
the Session's own map.

Driven through AREA 59's zone record 24 — the Shooting gallery's first
épreuve, whose enter script at 0x34ea ends `shoot.begin -1` — the port reports
`active 1, weapon 11, object 42, hud 34, library shoot2.scx`, and both exit
arms behave: `end(0)` keeps slot 11, `end(1)` puts it back to −1.
`verify.py: engine: shoot mode`, shown to fail by dropping the slot-11
default.

**Not done, and moved into step 4**: the frontend half. Nothing yet installs
camera mode 4, group 200 or scheme 2 in `omk-play`, so the mode is a decision
the Session makes and nobody draws.

## 4. The steps

Each ends in a commit and a report.

| # | step | state |
|---|---|---|
| 1 | **the reading above** — what the mode does, and the grid finding | **done 2026-09-09**; `verify.py: shoot arenas` |
| 2 | **the grid** — the cell byte, the floor box, the door table, a reader and a probe that DRAWS a floor | **done 2026-09-09**, §2b; `verify.py: map2d grid` |
| 3 | **the mode** — ops 80/81 read and ported, the weapon slot, the HUD choice, the library swap, both exit arms | **done 2026-09-09**, §3b; `verify.py: engine: shoot mode`. The frontend half (camera mode 4, group 200, scheme 2 installed in `omk-play`, and a `--shoot` harness) is NOT done and moves to step 4 |
| 4 | the FRONTEND half and the WEAPON tables | **DONE 2026-09-09** - §4b the frontend and the tables, §4c what a shot is, §4d the live arm, §4e the target scripts and the doors' refcount |
| 5 | **the brains, decision revisited** — with the grid in hand, how much of the generic shooter's 16 states is now fact rather than geometry. Gandhar is already exact; Astaroth and the generic are state graphs. **Only what the grid settles gets wired**; the rest stays labelled | **DONE 2026-09-09** - §5b the weapon floats, §5c the range, the cone and the two line-of-sight tests. All three of `standing-unknowns` §2's unknowns are read and the decision there is SUPERSEDED |
| 6 | docs, the checks, and a play test | **PART DONE 2026-09-09** - the sight predicate and the line walk PORTED (`Map2d::sightBlockedValue`, `Map2d::lineOfSight`), `verify.py: map2d sight` added and both its clauses shown to fail, and the reading written into `docs/ASSETS.md`, `docs/RECONSTRUCTION.md` and `engine/README.md`. **The PLAY TEST is owed** - nobody has watched shoot mode |

## 4b. The frontend, and the weapon tables — step 4 (part), 2026-09-09

**The mode is enterable and visible.** `omk-play --shoot` stands the player in
an arena in shoot mode, and the transition is watched every frame from the
Session's own flag, so a script's `shoot.begin` reaches it too:

```
build/omk-play ../gamedata ../tables --save ../traces/save-appart.bin \
    --area 59 --stand 5000,0,-2900,0 --shoot
frame 1: SHOOT MODE ENTER (Shoot_Enter) - weapon slot 11 (object 42),
         HUD screen 34, library shoot2.scx, ACTOR_STATE 3, scheme 2
frame 3: actor 237 VIR_FN - shoot mode, action 3, character type 3 -> a clip
```

The three installs `Shoot_Enter` does are all there: the player's
`ACTOR_STATE` 3 with `.CTL` group 200 (`ActorRuntime::shootEnter`, which has
existed since the state machine was ported and which nothing called until
now), input scheme 2, and camera mode 4 — `camera_presets.json` row 4 is eye
(0,0,0) and target (0, 0, 787.4016) both on subject 0, fov 75, and **every
smoothing divisor zero**: the eye ON the player, the aim 20.00 m ahead, no lag.
That is an aiming camera, and it is the row the table has always carried.

**Both entries go through one door.** `Session::shootBegin` reads the weapon
table lazily and then calls the mode; the harness used to call `ShootMode`
directly and entered with slot 11 and object −1 behind it — right slot, no gun.

### The weapon tables — and I re-derived what the tree already knew

`tables/shoot_weapons.json` now carries the two compiled tables, self-checked
the way the others are (same keys, same C and index columns in both, floats
that differ). Their contents and even the meaning of `shoot.begin`'s operand
were **already recorded** — `docs/RECONSTRUCTION.md` 2026-08-27 ("opcode 80's
operand is one of these ids") and 2026-08-28 (the two table addresses), and
`docs/SCRIPT_VM.md` had a dump. Step 3's commit called the operand meaning
unrecorded; that was wrong, and it was wrong because I read the handler before
I grepped the docs, which is exactly the cost CLAUDE.md §0 is about. What was
genuinely missing is the PORT, the lift, and the check.

**And reading it again was not wasted, because the old table is shifted by one
row.** `SCRIPT_VM`'s described the record as `{class, ammo slot, A, B,
damage}` — that is the memory read starting at **+12**, so each printed row
took its class and index from one record and its floats from the NEXT. Every
number was real and the alignment was off by one, which nothing inside that
table could catch. The memory order is `{A, B, C, class, index}`, established
by the handler matching at +12 and reading +16, and the doc is corrected.

**One check caught nothing until it was rewritten**, which is worth keeping:
`engine: shoot mode` first read the viewer's own printf for the input scheme —
and that printf recomputed `shootMode ? 2 : 0` instead of reporting what was
installed, so deleting the `installScheme` call left the check GREEN. It now
prints `in.group()`, the live table's own group, and the same mutation turns
it red. A probe that recomputes the answer is not measuring the code.

## 4c. WHAT A SHOT IS — step 4 (rest), 2026-09-09

**A shot is a PROJECTILE**, and the pool it comes from is already in the tree:
`Actor_TickProjectiles` (0x0044D110) fires them and `Projectiles_Tick`
(0x0044D930) integrates them. The handoff listed this as unread and it is the
first of step 4's three debts.

**The firing arm**, from `Actor_TickProjectiles`:

```c
if (f32(actor + 4 * slot, 148) <= 0.0) {      /* the fire timer expired */
    v64[0] = 34; Game_RaiseEvent(44, v64);    /* Actor_GetProperty 34 */
    if (v65 > 0) {
        Game_RaiseEvent(45, v64);             /* Actor_SetProperty - spend it */
        ...
        Game_RaiseEvent(44, v64);             /* read it back */
        for (e = &unk_531348; *e; e += 15)    /* first FREE slot */
            if (e >= &dword_534F48) return 0; /* pool full: no shot */
        f32(actor + 4 * slot, 148) = reload;  /* the timer reloads */
```

so the rate of fire is a per-slot countdown at the actor's `+148`, the
ammunition is **property 34** through events 44/45 (`Actor_GetProperty` /
`Actor_SetProperty`), and a shot with the pool full is simply not taken.

**The slot is 15 dwords, and the pool is 256 of them.** The allocator walks
`e += 15` and addresses `60 * index`; the tick walks `v1 += 15` from
`unk_53137C` — which is the pool base plus 52, so its pointer is aimed at a
FIELD inside the entry — and stops at `flt_534F7C`. The span is the check:

    0x534F48 - 0x531348 = 0x3C00 = 15360 bytes
    15360 / 60 = 256 entries exactly;  15360 / 52 = 295.38

**so the 2026-08-28 row's "13-dword projectile pool" is wrong** - 13 dwords
does not divide the pool and 15 does, which is the walk-lands-exactly test
this repo asks for.

What the entry holds, from the writes around the allocation:

| offset | what |
|---|---|
| `+0` | the NODE - `sub_437850(scene, model, 20)`, a clone; **non-zero is what "occupied" means**, and it is what the free scan tests |
| `+8` | `property.hi * 3.9000001` - a speed |
| `+44` | the firing ACTOR (`a1`) |
| `+48` | `sub_44EEB0(model + 16)` |
| `+56` | `property.lo` |

and the node is placed at the actor's `+44/+48/+52` — the muzzle — before
`sub_44D7F0(slot, ..., actor + 92, a1 + 12 * slot + 100)` sets its direction
and can REFUSE, in which case the shot is dropped.

**Not read yet, and named rather than assumed**: `sub_44D7F0`'s aim (whether
the direction is the actor's facing, a target's position, or a spread),
`sub_4246E0` beside it (a sound, on its argument shape), and what makes the
timer at `+148` start counting in the first place - which is where
`Shoot_InitWeapon`'s event 48 arm and the weapon's range should land.

## 4d. `Shoot_TickPlayer`'s LIVE ARM — step 4 (rest), 2026-09-09

The second of step 4's debts, and the handoff's own description of it needs
one correction: **`+188` is the FLOOR index, not a node.** It is a signed byte
(`-1` = not on the grid at all) and it is the first argument of the two grid
accessors §2 already decoded, `sub_4358D0(f, x, z)` and
`sub_435970(f, x, z, v)`. `+136` and `+140` are the CELL, column and row, as
ints.

The arm, in order:

```c
Actor_GetPosAndFacing(a2, pos);
was   = i8(rec, 188);                       /* the floor last tick        */
acted = Shoot_Think(a2, pos, 0, -1);        /* the brain                  */
floor = i8(rec, 188);                       /* ...which may have moved him */
rec[160] &= ~0x1000;

if (floor != -1) {
    if (acted) { col = rec[136]; row = rec[140]; }        /* the brain's cell */
    else {                                               /* else derive it    */
        sub_4368E0(floor, pos);
        col = (pos.x - origin[floor].x) / flt_907EAC;    /* flt_907EAC is the */
        row = (pos.z - origin[floor].z) / flt_907EAC;    /*   cell size       */
    }
    if (was != floor || dword_907DCC) { sub_436260(floor, col, row); dword_907DCC = 0; }
    if (sub_436350())                  sub_436260(floor, col, row);
}
```

so the cell is **the brain's when the brain decided one and the actor's own
world position otherwise** - `Shoot_Think`'s return is the discriminator, not
a flag on the record - and `sub_436260` is re-run on a floor CHANGE and
whenever `sub_436350` asks.

**Then the occupancy pair, and it brackets one call:**

```c
if (rec[156] != 2) {                        /* stamp   */
    saved = sub_4358D0(floor, col, row);
    rec[160] = sub_47C1B0(floor, saved, rec[160]);
    if (saved == 1) { rec[72] = actor[244]; rec[76] = actor[252]; }
    u8(rec, 189) = saved;
    sub_435970(floor, col, row, 128);
}
Shoot_StartTargetScripts();
if (rec[92] > 0 && (rec[156] != 2 || rec[92] < 0)) {    /* restore */
    sub_47C230(floor, rec[189]);
    sub_435970(floor, col, row, rec[189]);
}
```

**The 128 is up only across `Shoot_StartTargetScripts`.** §2 read the pair as
"stamps on arrival, restores when it leaves"; in this arm both happen in ONE
tick, and what sits between them is the target scripts - so the mark exists so
that whatever runs in there sees the cell as taken.

**And a corpse keeps its cell.** The stamp is gated on `rec[156] != 2` and the
restore on `rec[92] > 0` as well, where `+92` is what the respawn arm fills
from event 44 - so an actor with `156 != 2` and `+92` spent stamps 128 and
never puts the byte back. The `|| rec[92] < 0` in that condition is DEAD: the
first conjunct is `> 0`, so it can never be reached. Worth recording as a
reading of the code rather than a rule of the game - the two gates are
different fields, and only play can say whether a body really does block.

**Not read**: `sub_436260` (re-run on a floor change), `sub_436350`,
`sub_47C1B0` (which folds the cell's byte into `+160`'s flags) and `sub_47C230`
beside the restore.

## 4e. `Shoot_StartTargetScripts` — and the doors are REFERENCE COUNTED

The third of step 4's debts, and it turns the bracket in §4d into a mechanism:
what sits between the stamp and the restore is the whole arena's DOORS.

```c
for (slot = doorTable, n = 16 * floorCount; n; --n, slot += 3) {
    open = slot[1]; close = slot[2]; arm = slot[0];
    if (open == -1 || close == -1) continue;          /* not a door       */
    state = sub_44A0F0(scene, open, close);
    if (state == 64 || state == 192) continue;        /* already busy     */
    if (arm  >  0) ScriptObject_Start(open,  scene, -1, 0);
    if (arm == 0) ScriptObject_Start(close, scene, -1, 0);
    /* arm < 0: nothing at all */
}
```

so **the arm is a tri-state**: above zero starts the door's OPEN object, zero
starts its CLOSE object, and below zero - the `-1` it ships as - does nothing.
Every slot of every floor is visited each tick, not just the one being
crossed, and `ScriptObject_Start(..., -1, 0)` runs them DETACHED, with no
caller slot to park.

**And the arm is a reference count of the actors standing on that door**, kept
by the same two calls that bracket the 128:

| | at the STAMP, `sub_47C1B0(floor, cell, flags)` | at the RESTORE, `sub_47C230(floor, cell)` |
|---|---|---|
| gate | the cell byte is a door (`& 0x10`) | the same |
| slot | `doorTable + 12 * (16 * floor + (cell & 0xF))` | the same |
| arm | `-1 -> 0` first, then **`++arm`** on both paths | **`--arm`** |
| the actor's `+160` | bit 10 CLEARED when `sub_44A0F0` says 16, SET otherwise | — |

So a door with somebody on its cell counts above zero and is told to open; the
moment the last of them restores its cell the count reaches zero and the same
pass tells it to close. The `64`/`192` states are what stops it being restarted
while it is already moving, and the flag the stamp writes into the actor's own
`+160` is how that actor knows the door is not ready yet.

**That is why §4d's bracket exists.** The 128 and the door count are the same
bracket: claim the cell, let the arena's doors react to who is standing where,
release the cell.

**Still not read**: `sub_44A0F0`'s state values beyond "16 is ready, 64 and 192
are busy", and `sub_436260` / `sub_436350` from §4d.

**Step 4 is now complete** - §4b the frontend and the weapon tables, §4c what a
shot is, §4d the live arm, §4e the target scripts.

## 5. What is still NOT established

Written down so step 5 cannot quietly assume it:

* ~~**line of sight.**~~ **CLOSED, §5c**, and the grid is only half of it:
  the acquisition casts `sub_4449E0` against the SET'S OWN MESHES through
  `o3de_ForEachMeshInBox`, and the brain's state 15 walks the grid with
  `sub_4359A0`'s Bresenham. The wall segments are not involved.
* ~~**the weapon's range.**~~ **CLOSED, §5c** - and it is not in the weapon
  table at all: the range is character **property 26**, in metres, read
  through event 44 into the shoot record's `+32`. `Shoot_InitWeapon`'s two
  floats are `f0` the RATE (§5b, traced) and `f1` with no reader at all.
* ~~**what a shot is.**~~ **ANSWERED, §4c**: a projectile out of a 256-slot,
  60-byte pool, fired by `Actor_TickProjectiles` on a per-slot timer and paid
  for out of property 34. What is still unread there is the AIM
  (`sub_44D7F0`) and what starts the timer.

## 7. WIRING THE GENERIC BRAIN — the plan, opened 2026-09-09

`standing-unknowns` §2 refused this while three things were unread. All three
are read (§5b, §5c), so it is now sized work rather than a guess, and it gets
its own step list because it is the size of steps 1-5 over again.

**The rule that governs it, and it is the one the whole file has followed:
only what is READ gets wired.** `sub_424DE0` is a state machine over four
geometric calls plus `Shoot_ActorAction` and `List_PickRandomByType`. Where a
state's arm has been transcribed it is ported; where it has not, the port must
say so rather than invent a plausible branch. The existing arms are already
"state graphs only" and labelled that way in `engine/README.md`; this replaces
that labelling one state at a time, and the label moves with the code.

| # | step | state |
|---|---|---|
| 7a | **the record's geometry** — `sub_422540`'s six properties into the record (both ranges, the third, the cone's cosine, health, the flag fan-out), and the acquisition pair `sub_420C70` / `sub_420D90` with the four values they leave behind |**DONE 2026-09-09**, §7a below; `verify.py: shoot range` |
| 7b | **the turn** — `sub_420EB0` on the Euler at `actor+420`, its three thresholds and its `180` / `±90` snap returns |**DONE 2026-09-09**, §7b below; `verify.py: shoot range` |
| 7c | **the states** — `sub_424DE0` read arm by arm and ported, each one carrying what it was read from; the ones not reached stay declared |**DONE 2026-09-09** — the FRAME and **all 16** states, §7c below; `verify.py: shoot generic` |
| 7d | **the frame loop** — the brain called from `Shoot_TickNpc`'s place, the occupancy stamp/restore pair around it, and `omk-play --shoot` driving it |**PART DONE 2026-09-09** — the brain TICKS on the gallery's own gunmen, §7d below; `verify.py: engine: shoot brain`. The occupancy pair and the walk are not wired: there is no path-finder on the grid yet |
| 7e | checks, and a PLAY TEST — which is the one thing steps 1-6 never got | |

Each step ends in a commit and a report, and 7e is not optional: the gunmen
have never been seen by a person, and `finished-means-usable` is the standard
this repo is held to.

## 7a. The record's geometry — done 2026-09-09

`ShootRecord` gained the four authored numbers and the weapon countdown;
`initShootRecord` is `sub_422540` and `shootAcquires` is `sub_420C70` with its
wide twin `sub_420D90` behind a flag. The four values the engine leaves in
globals for `sub_420EB0` to read are returned in an `AcquireOut` instead,
which is the whole of the difference.

**The shipped numbers are worth having on their own**, because they are the
first direct evidence of how a gunman was TUNED rather than of how the code
runs. Read off the 276-byte actor records (property 26 is `0x1A`, an int16 at
`+180`), `verify.py: shoot range`:

* **386 of 1032** records carry an acquisition range, 427 a cone;
* the ranges are round metres and they cluster — **50 m on 100 characters**,
  30 m on 86, 80 m on 54, 70 m on 49, 90 m on 28 — with nothing in between;
* the cones are round degrees, **80° on 204** characters and 90° on 98.

Three records answer **13944** to both, which nobody typed: their `+180` lands
on something that is not a range. They are left in the histogram rather than
filtered, so the count stays honest about what the walk found.

> **THE FIRST TRANSCRIPTION HAD THE TWO ENDS SWAPPED**, and the probe caught
> it rather than a re-reading. `sub_420C70`'s first argument is the one
> carrying a YAW, so it is the SHOOTER, and the vector it forms is
> `self - target` dotted against `(0,0,1)` rotated by that yaw. That reads
> backwards until the heading convention is remembered — a character faces
> **−Z** at yaw 0 — at which point the two sign conventions cancel. Written
> with either one alone wrong it builds a gunman who shoots at whatever is
> standing behind him, and both versions look equally reasonable in the
> source. The check asserts the behaviour (front taken, back and abeam
> refused, and turning him 180° swaps them) rather than the formula, which is
> why it could catch this at all.

Both halves shown to fail: swapping the ends flips front and back, and
`39 → 1` puts a 20 m range at 20 units.

## 7b. The turn — done 2026-09-09, and a sign that only MOVING could show

`shootTurnToward` is `sub_420EB0`. It works on a **signed square** —
`dotFlat * |dotFlat|` against the squared 2D distance — so its thresholds are
cosines with no square root:

| band | what it does |
|---|---|
| `> 0.99 * dist2d²` (≈5.7°) | already aimed: nothing |
| `> 0.80 * dist2d²` (≈26.6°) | creep by **one** frame delta |
| `> 0.2 * dist3d` | in front but wide: **5.0** per delta |
| otherwise | behind: **10.0** per delta, or a SNAP |

The snap returns `180` hard behind and `±90` otherwise, for the caller to play
a turn animation rather than rotate. The third threshold really does compare a
squared quantity against an unsquared distance; it is transcribed as written
rather than "corrected", because it is what runs.

**Hex-Rays loses three FPU compare flags here** and renders them as undefined
variables — the decompiled body has three `if (v3)` / `if (v8)` / `if (v11)`
on nothing. All three are recovered from the listing: each is a `fcomp`
against `flt_4BC224`, which is **0.0**, on the cross, and the branches say
`cross < 0` **adds** while `cross >= 0` **subtracts**, the snap splitting on
the same test. The five constants are `flt_4BC228` 0.99, `flt_4BC22C` 0.80,
`flt_4BC230` 0.2, `flt_4BC234` 10.0, `flt_4BC238` 5.0.

> **THE ROW-VECTOR CONVENTION, AND WHY NO STILL FRAME COULD CATCH IT.** The
> forward vector is `(0,0,1)` rotated by the yaw, and in this engine's
> row-vector convention its x component is **−sin**, not +sin. The two
> conventions **agree exactly on the cardinal axes** — so every acquisition
> assertion of §7a passes under either, and the mutation proving it changes
> not one of them. What separates them is TURNING: with `+sin` the direction
> rule and the aim test disagree by construction, so the machine converges on
> the heading that points the gunman exactly AWAY from his target and sits
> there — 400 frames from 135° and never aimed. With `−sin` it converges in
> **37 frames with 0 going the wrong way**.
>
> This is CLAUDE.md §1's *"a value verified standing still is not verified
> moving"*, met head on: the check had to run the loop, and the invariant that
> catches it is written over the transition (frames to aim, and frames spent
> going the wrong way) rather than over any single state.

## 7c. The generic brain's frame, and six of its sixteen states

**The shape first, because it is the thing to hold on to.** `sub_424DE0` is an
outer switch on the state at record `+156` — 1..15 and 28 — where each arm
computes an **outcome** and every arm then funnels through one shared
epilogue. The states decide; the epilogue acts.

The prologue (05_sys.c 5572-5580) fetches the TARGET — record `+96` — and its
position and facing once for the whole switch, clears flag bit `0x200`, and
drops the current clip pointer at `+16`. An arm that wants `0x200` sets it
back, which is how the epilogue knows who ran.

The **epilogue** moves the body (`sub_421770` when flag 8 is set, otherwise
`sub_421370`), **wraps the Euler into (−360, 360)** — the one place that
happens — clears flag `0x80`, and dispatches on the outcome:

| outcome | what it is |
|---|---|
| **0** | fire when the weapon countdown has expired |
| **1** | the full firing arm: reload `+172` from the weapon row's `f0`, then `Actor_TickProjectiles` |
| 2, 3, 4 | **NOT READ** |

Outcome 1 is where §5b's weapon-rate line lives, so the rate reading and the
machine meet here.

### All sixteen states transcribed

| state | what its arm does |
|---|---|
| **3** | fires on bit 0 of `sub_426E00`, turns without the snap |
| **5** | plays a clip out; at its end → **4** |
| **7** | counts the timer at `+168` down; on expiry asks for action 0 |
| **8** | turns WITH the snap — and the snap picks a turn ANIMATION |
| **10** | aims while a clip runs; outcome **2** before halfway, **3** after; at the end → **11** |
| **11** | the mirror: with the predicate and >5 frames run, back to **10** |
| **1** | NAVIGATE — contact, or commit to an edge |
| **2** | TRAVERSE that edge, climbing its slope |
| **4** | PATROL a route |
| **6** | the HUB — three arms, one of which fires |
| **9**, **28**, **13** | state 8's arm; 13 adds a clip-end tail |
| **12** | converts a position to a GRID CELL itself |
| **14** | drops its route and falls back to **4** |
| **15** | ACQUIRE — cone AND grid line of sight, and the 0x20 latch |

**The movement loop is 1 → 2 → 6.** State 1 tests whether the gunman stands on
his target's own nav node, and commits to an edge otherwise; state 2 walks the
edge out; the hub takes over on arrival. Three transcribed formulas come with
it: the heading is `atan2(dz, dx) * 180/pi + 90`, `+68` is seeded with the
edge's horizontal length, and state 2's vertical step is
`dy / horizontalLength * distanceMovedThisFrame`, so a gunman climbs stairs.

> **THE ORDER INSIDE STATE 1 IS LOAD-BEARING.** The engine writes `+156 = 6`
> for the contact case and the step branch below then **overwrites** it with 2.
> So a gunman standing on his target's node who can also take a step ends in 2,
> and the contact test loses. Transcribed as written, and the check asserts
> both halves so it cannot be tidied into the order that reads better.

> **AND STATE 1 IS A SECOND, INDEPENDENT SITE READING THE OCCUPANCY STAMP AS
> SIGNED.** Its cell test refuses `{-128, 0, 2, 3}` — note the **-128**, not
> 128 — which confirms the `movsx` finding behind `todo/omk-play.md` 96's
> neighbour from a different function, and is why `blockedValue` lists `0x80`
> while the sight predicate does not.

State 4's tail is the one place the port has to say **unknown** out loud: when
something changed the state, the engine takes the outcome from `sub_4272B0`,
which is unread. The step returns `outcomeFromUnread` rather than a plausible
value, and the check asserts that flag — so *we do not know* stays visible
instead of decaying into *nothing happened*.

**State 8 is the interesting one.** `sub_420EB0`'s `180` / `±90` returns are
not a rotation, they are a request for a CLIP: type **30** for −90°, **31**
for +90°, **32** for 180°, picked out of the library by
`List_PickRandomByType`, with `+184` set to the degrees that clip must cover
per frame (`total / Anim_Frames`). If the library has no such clip the body
simply rotates at a fallback rate instead. (The 180 arm calls `rand()` and
discards it — a quirk, recorded rather than reproduced.)


### The hub, state 6 — and a rule that turned out to be general

Both movement loops end here, and the arm is three branches that all turn
**with** the snap and pick a turn clip from it; only the first also fires:

1. `flags & 0x8000` — **finishing**. Either drop flag `0x20` (when `+144` is 8)
   or ask for the default action, and then go **straight to the epilogue**.
2. the predicate holds and the actor can fire — outcome **1**, and turn.
3. otherwise turn, unless `sub_421CD0` says hold still.

Arms 2 and 3 fall into a shared tail that counts the timer at `+168` down and
asks for the default action when it expires. **Arm 1 does not**, and that
asymmetry is asserted: mutating its early exit into a fall-through leaves the
timer at −0.5 instead of 0.5 and turns the check red.

> **THE TAIL IS GENERAL, and reading state 6 is what showed it.** States 1, 3,
> 6, 8, 10 and 11 all reach `LABEL_222` / `LABEL_181` / `LABEL_58`, and every
> one of them does the same thing: **if the arm changed the state, the outcome
> is recomputed by `sub_4272B0`** — which is unread. So a transition always
> leaves the outcome unknown, whatever the arm had set before it. The port now
> applies that once, in the epilogue, and reports `outcomeFromUnread` rather
> than letting a stale outcome ride out of a state change.


### State 15, and what the three authored distances are for

15 is the ACQUIRE state and it is where §5c's reading pays off. It needs the
cone-and-range test **and** the grid line of sight — `sub_4359A0`, which the
port already has as `Map2d::lineOfSight` — or flag `0x20`, the latch it sets
once it has seen you and which keeps a gunman engaged **through a wall**
afterwards. And it fires on the **inner** range `+28`, not the acquisition
range `+32`: that is the only place in the machine where two of the three
authored distances are told apart, and mutating one into the other turns the
check red.

> **A CORRECTION READING THE LAST FIVE FORCED: the default outcome is 0, not
> "none".** `sub_424DE0` opens with `v180 = 0.0` before its switch, so an arm
> that sets nothing still leaves the epilogue asking to fire-if-ready. The
> port had been defaulting to None, which quietly turned every silent arm
> into "do nothing". `None` now means exactly one thing — the outcome was
> recomputed by `sub_4272B0`, which nobody has read.


### `sub_426C20`, the move decision — and a sign read the wrong way twice

The fourth supplier is now read too (`shootMoveDecision`), leaving only
`sub_435900`'s "am I there yet" as a parameter. It confirms 7b's rotation
convention from a second, independent site: the vector it builds is
`(-sin(yaw), cos(yaw))`, the same `-sin` that only the convergence loop could
establish for `sub_420C70`.

Its five bands:

| | |
|---|---|
| arrived | **1** — take the step |
| destination ahead or abeam | **0** — steer 10°/delta, clamped to the exact bearing |
| behind, off to one side | **±90** |
| directly behind | **180** |

> **THE COMPONENT IT TESTS IS THE BACKWARD ONE.** `b = -sin(yaw)·dx +
> cos(yaw)·dz` over `dest - self` is the **+Z** component, and a character
> faces **−Z** at yaw 0 — so read as "forward" the whole table comes out
> inverted, and this was written up that way first: *"180 when the
> destination is straight ahead"*. The probe settled it in one line
> (`move ahead 0 … behind 180`), which is the second time in this file that
> a running test caught a sign a careful reading had gone past.

**And that briefly cost the clip types their names, wrongly.** On the
inverted reading, 180 selecting a clip for a destination *straight ahead*
made "32 is a half-turn" impossible, so §7c's naming was struck out. With the
sense the right way round the naming is **consistent** again — 180 is the
directly-behind case. But consistent is not established, and
`docs/ASSETS.md` deliberately leaves the behaviour types unnamed (30 of the
34 have no clip at all), so the port now says the honest thing: the codes
select clip **types** 30, 31 and 32, and what those animate is not
established here.


### `sub_426E00` — and the third range finally gets a job

It is **not the predicate its eight callers make it look like**: it reads the
geometry and then *drives the machine*, writing `+156` itself. And it is where
§5c's three authored distances stop being two-and-a-spare:

| field | property | what it does |
|---|---|---|
| `+32` | 26 | **acquire** — inside it, with the line of sight, engage and go to the hub **6** |
| `+28` | 27 | **engage** — the return is non-zero only inside it, and its HALF and QUARTER pick between states **8** and **13** |
| `+36` | 30 | **disengage** — beyond it, in state 6, flip a coin: **3** or back to patrolling in **4**, clearing the `0x20` latch either way |

Until this function was read, property 30 had no consumer at all. The coin
flip is the engine's own `rand() & 1`, and clearing the latch is what makes a
gunman re-acquirable rather than permanently disengaged.

`312.0` appears hardcoded in the state-8-versus-13 test — 8 metres at the
engine's own 39 to the metre.

> **A CORRECTION: `sub_4272B0` IS read.** The port had been labelling the
> post-transition outcome "recomputed by a function nobody has read". Its
> banner in `readable/src/05_sys.c` says `@status READ`: it is the 304-line
> **behaviour selector**, which picks an animation by type and was
> deliberately left un-renamed because its case labels are an enum with no
> names anywhere in the binary. So the outcome after a transition is *a clip
> chosen by the behaviour selector*, which this port does not model — not
> *unknown in principle*. Two different things, and the comment now says
> which.

### What is not ported, and the guard that is kept anyway

**Nothing sets `unread` any more.** The assertion is kept rather than deleted:
every state outside `genericStatesRead()` must still leave the record and the
Euler untouched, so adding a state to the machine without reading its arm
goes red instead of quietly inventing a branch.

That refusal is asserted, not promised. `verify.py: shoot generic` drives
every unread state and requires all five to leave the record and the Euler
untouched, and **mutating the default arm to invent a transition turns it
red** (every one of them changed something). A machine that guessed the missing nine
would be indistinguishable from one that had them right, and this is the only
thing standing between the port and that.

## 7d. The brain, ticking on real gunmen

One `ShootRecord` per gunman, built the first frame he is staged out of his
own six properties exactly as `sub_422540` does, then `shootEngage` and
`shootGenericStep` every frame. The Shooting gallery's three `VIR_FN` say:

```
actor 237 VIR_FN - shoot brain: acquire 1950 engage 585 disengage 702
                   cone 0.000 health 15
```

which at the engine's own 39 units to the metre is **50 m, 15 m, 18 m and a
90° cone** — round numbers a person typed in 1999, arriving through the
property reader and the `39 *` conversion with nothing in between. This is
the first thing in the whole subsystem that is not a synthetic case.

**And the ranges then do their jobs on the real layout.** From the player's
spot two gunmen are 417 and 495 units off — 10.7 and 12.7 m, inside the 15 m
engagement range — and reach outcome **1, fire**. The third is 660 units,
16.9 m, outside it, and does not. Mutating the engagement range to the
acquisition range makes all three fire and turns the check red, so that
boundary is the difference between a machine that runs and one that runs on
the right numbers.

> **WHAT IS SUPPLIED RATHER THAN COMPUTED — the honest limit of this wiring.**
> `sub_421020`, `sub_421CD0` and `sub_435900` are unread and arrive as false,
> and no route or nav edge is handed over because the viewer has **no
> path-finder on the grid** yet. So the machine acquires, turns, engages and
> disengages on real distances, and it does not yet **walk**: a gunman aims
> at the player and holds his ground. States 1, 2 and 4 are transcribed and
> ported but nothing feeds them.

## 7f. The projectile pool, and what a shot IS

`Actor_TickProjectiles` (0x0044D110) is the whole of firing, and it is a pool
allocator with a gate in front of it: **four weapon slots per actor**, each
with its own weapon object at `actor+84 + 4·slot`, its own countdown at
`actor+148 + 4·slot`, and its own ammunition (property 35, indexed by slot).

The entry, 60 bytes:

| | |
|---|---|
| `+0` | the NODE — **non-zero is what OCCUPIED means**; the free scan tests this and nothing else |
| `+8` | the speed, `property34.hi × 3.9` |
| `+44` | the firing actor |
| `+48` | derived from the weapon's own node name (`sub_44EEB0`) |
| `+56` | `property34.lo` |

The node is placed at the WEAPON's `+44/+48/+52` — the muzzle — and
`sub_44D7F0` then sets its direction and may refuse.

**Three behaviours a tidier port would quietly lose**, each asserted:

* **a gap in the slots hides everything behind it.** The walk is
  `if (!weapon) return`, not `continue`, so a weapon in slot 1 behind an
  empty slot 0 never fires at all. Mutating `break` to `continue` turns the
  check red.
* **a full pool takes no shot and spends no round.** The free scan returns
  −1 and the function returns outright, so the ammunition is still there
  when a slot frees up.
* **a refused aim spends the round anyway.** `sub_44D7F0` may refuse, and
  when it does the engine returns with the entry already allocated and the
  node already cloned — so the shot does not happen, the round is gone, and
  the pool entry is occupied.

And the pool's geometry is a test the data can fail, which this tree failed
once: `0x534F48 − 0x531348 = 15360` is **256 × 60** exactly, where the 52
first recorded gives 295.38. A stride that does not divide the pool is not a
stride.

**Not modelled**: the node clone, the direction `sub_44D7F0` sets (unread),
and the packing of property 35's slot-and-count word. And **nothing calls
this yet** — the pool exists and is asserted; connecting it to the trigger
and to the brain's outcome 1 is the next step.
## 5b. The two weapon floats — step 5's first reading, 2026-09-09

`tables/shoot_weapons.json`'s lifter says of the two floats: *"they are a range
and a rate in shape, and nothing traced says which"*. **One of them is now
traced.**

```c
v134 = *(uint32_t **)(rec + 180);        /* the weapon row */
if (!v134) goto LABEL_314;
if (f32(rec, 172) <= 0.0)
    u32(rec, 172) = *v134;               /* <- row[0], f0, reloads the timer */
```

(`05_sys.c` 6055-6057.) **So `f0` is the RATE** — the value a countdown at the
shoot record's `+172` is reloaded with when it expires, the same shape as the
per-slot fire timer at the actor's `+148` in §4c. The corpus reads that way
too: the player's f0 is LOWER than the NPCs' for the four light weapons
(10/8/2/2 against 15/10/4/4) and identical for the two heavy ones (20 and 25),
which is the player shooting faster with a pistol and no faster with a cannon.

**`f1` has no reader**, and that is a measurement rather than an impression:
the weapon row is reachable only through the shoot record's `+180`, and the
whole of `05_sys.c` reads that pointer exactly twice - `u16(row, 12)`, the KEY,
in the property-35 handler at 4350, and `*row`, f0, at 6057. `i2` has none
either. **And the elimination that would have made `f1` the range is closed
off by §5c below**: the range is a per-character PROPERTY and never comes from
this table at all, so `f1`'s meaning is simply open.

> **A TRAP WORTH THE LINE: `+180` is two different things.** On an ACTOR it is
> the bank (`Cef_FindGroupById(u32(actor, 180), 200)`, four sites); on a SHOOT
> RECORD it is the weapon row. Same offset, different structs, and a grep for
> `, 180)` returns both mixed together.

## 5c. The line of sight and the range — step 5's second reading, 2026-09-09

Both of the remaining unknowns are closed, and neither answer was where the
plan expected it.

### The RANGE is a character PROPERTY, not a weapon field

`sub_422540` (0x00422540), called once from `Shoot_ActorEnter`, builds the
shoot record out of the character's own properties. It raises **event 44**,
`Actor_GetProperty` — the same event §4c found the ammunition on — six times
in a chain where each read gates the next:

| property | field | what it is |
|---|---|---|
| 1 | `rec+92` | **health**, and `0` is rewritten to **10** |
| 26 | `rec+32` | `39 * v` — **the ACQUISITION RANGE, in METRES** |
| 27 | `rec+28` | `39 * v` — a second, inner range, in metres |
| 30 | `rec+36` | `39 * v` — a third, in metres |
| 29 | `rec+40` | `cos(v * pi/180)` — **the SIGHT CONE's half-angle, in DEGREES** |
| 37 | `rec+160` | five behaviour bits (`0x10`, `4`, `8`, `2`, `0x20` fanned out to `0x4000000`, `0x800000`, `0x2000000`, `0x1000000`, `0x100000`) |

`39` is the inch-per-metre factor this repo has met twice already — the
pedestrian spawn's `39*(5-density)*h[3]` and the projectile speed's
`property.hi * 3.9`. So a designer authors a gunman's reach and his field of
view in **metres and degrees**, per character, and the weapon table has
nothing to do with either. That is what shuts the door on `f1`.

### The CONE, and the two globals `sub_420EB0` turns on

`sub_420C70(rec, target[4], me[3])` is the acquisition test, and it is one
line:

```c
return v4 > f32(rec, 40) * dist3d && dist3d < f32(rec, 32);
```

with `v4` the dot of (target - me) against the target's own forward vector,
built by rotating `(0,0,1)` through the yaw in `target[3]` — so `a2` is
`Actor_GetPosAndFacing`'s four floats and the cone is measured **from the
target outward**. On the way it leaves four globals behind, and they are what
the turn helper reads rather than recomputing:

* `flt_90E118` the squared **2D** distance, `flt_90E108` the **3D** one;
* `flt_90E0F0` the forward dot, `flt_90E114` its horizontal part;
* `flt_90E0F4` the **cross** — the left/right sign.

`sub_420EB0(actor, allowSnap)` then turns the body: it compares the dot
against `0.99` of the distance (already facing — do nothing), `0.80`
(one frame-step of creep), and otherwise steps the Euler at **actor+420** by
`5` or `10` units of `flt_4C30D8`, the frame delta. With `allowSnap` set and
the target behind the cone it returns `180`, or `+/-90` off the sign of
`flt_90E0F4`, for the caller to play a turn animation instead. Hex-Rays loses
the FPU compare flags here (`variable 'v3' is possibly undefined` three
times), so the *thresholds* are read off the constants and the *sense* off
the caller — worth saying out loud, because that is the one part of this
section that is a reading rather than a transcription.

`sub_420D90` beside it is the same function with the range **doubled**
(`f32(rec,32) + f32(rec,32) > dist`) — a wider "still interested" test, used
on the arm where `rec+160 & 0x800000` (property 37's bit 4) is set.

### The LINE OF SIGHT is TWO tests, and one of them is a real ray cast

**The geometric one**, `sub_4449E0` (0x004449E0, `16_o3de.c`), is the one the
acquisition uses: normalise the segment from me to the target, build the AABB
of the two endpoints, hand it to `o3de_ForEachMeshInBox` with `sub_444460` as
the visitor, and keep the nearest hit — returning `1` with the distance, the
hit point and two ids, or `0` when the segment reaches clear. The site reads

```c
if (!sub_4449E0(me, target, hit, -1, &dist, &id1, &id2) && insideCone)
    /* acquire: state 3, latch rec+160 |= 0x20 */
```

so **nothing hit AND inside the cone** is what acquires, and `0x20` is the
latch that keeps a gunman engaged once he has seen you. This is the answer to
the plan's "the wall segments in each floor are a candidate": they are not
involved, the AI casts against the **set's own meshes** through the renderer's
box query.

**The grid one**, `sub_4359A0` (0x004359A0), is a **Bresenham walk over the
`MAP2D` cells** — both octants written out, stepping `base + row*stride + col`
in `dword_907DE0[floor]` — and it is used by the brain's state 15 and by
`Shoot_ActorEnter`'s placement. Its cell predicate is a function pointer
chosen by the fourth argument, and the two candidates are **not** the same
test:

| cell | `sub_435210` (arg 1) | `sub_435310` (arg 0) |
|---|---|---|
| `0` | **blocks** | **blocks** |
| `1` | clear | clear |
| `2`, `3` | **clear** | **blocks** |
| `0x10..0x17` | the DOOR: clear only if `sub_44A0F0(scene, door+4, door+8) == 16` | clear |
| `0x80`, `0xCD` | clear — see below | clear — see below |

`sub_435310`'s refusal set `{0, 2, 3}` is the MOVEMENT one — exactly what
`sub_4353E0` carries and the port already implements — and `sub_435210` is
the SIGHT test: only a true wall stops it, a **closed door** stops it, and
cells `2` and `3` do not. So `2` and `3` are things you cannot walk on but
can see and shoot across. **All three shipped call sites pass `1`**, so
`sub_435310` is dead in this build and the live walk is the sight one.

> **THE `case 128:` ARMS ARE DEAD, and only the assembly says so.** All three
> predicates are *written* with a `case 128` returning "occupied", and the
> walk is *written* to record the first such cell and return **2**. It cannot
> happen. The walk reads the cell with **`movsx`** (four sites, checked in the
> listing), so `0x80` arrives as `0xFFFFFF80`; `sub_435210` and `sub_435310`
> both open `cmp eax, 80h` / **`ja`** — *unsigned* above — so a stamped cell
> takes the DEFAULT arm, where `0x80 & 0x10 == 0` returns **clear**. The
> movement test `sub_4353E0` is the one that gets it right, and the difference
> is one instruction: it does **`add eax, 80h`** before `cmp eax, 83h`, which
> is why IDA labels its jump table "cases **-128**,0,2,3" and the other two's
> "case 128". So **`sub_4359A0` returns only 1 or 0**, an occupied cell never
> blocks or flags sight (which is the sensible behaviour — you can see a man
> standing there), and its four report globals `dword_52BA4C`/`52BA54` and
> `dword_52C400`/`52C404` are written by nothing else and **read by nothing at
> all**: the walk's only live output is its return value.
>
> This is `CLAUDE.md` §1's rule paying for itself twice in one function. The
> decompiler's `i8` was right and its `case 128:` was a faithful rendering of
> unreachable source; reading the C alone would have put a whole occupancy
> path into the port that the game does not have.

### What this does to the decision

`standing-unknowns` §2 rested on three unknowns and **all three are now
read**: the navigation node closed at step 2, the rate at §5b, and the range,
the cone and the line of sight here. The grid answers where a gunman may
stand and which doors he may cross; the ray cast and the two character
properties answer where he may aim; and the port already owns the primitive
the ray needs (`sweepSphere` at zero radius, over `soupInBox` —
`engine/src/o3de/collision.h`).

**So the decision of `standing-unknowns` §2 can be revisited, and the answer
is that the generic brain is worth wiring.** What made it look unaffordable
was the belief that its sixteen states each hid an unread geometric rule;
what the reading shows is that the geometry is four calls — `sub_420C70`,
`sub_420EB0`, `sub_4449E0`, `sub_4359A0` — and every one of them is now
transcribed above. `sub_424DE0`'s 1500 lines are a state machine over those
four, `Shoot_ActorAction` and `List_PickRandomByType` (11 and 12 calls), and
the animation frame counts.

**Wiring it is a NEW TASK, not the rest of step 6.** Step 6 is docs, checks
and a play test; a 1500-line state machine is not that — it is the size of
steps 1-5 over again. What has changed is only that it is now sized rather
than guessed, and that nothing inside it is unread.
