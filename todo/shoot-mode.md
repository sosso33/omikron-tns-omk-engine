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
  floats are `f0` the RATE (§5b, traced) and `f1` the projectile's SPEED -
  read by `Actor_TickProjectiles`' record path in `17_script.c`, which the
  `05_sys.c` scan that called it unread never reached (§7h).
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

## 7g. HOW TO REACH A SHOOT PHASE — the harness that existed all along

**One command, from a cold start, no save and no playthrough:**

```
build/omk-play "$OMK_DATA" ../tables --save ../traces/save-appart.bin \
    --area 230 --scene-chunk 56 --vulkan
```

The cutscene plays, the editing ends at frame ~385, and `SHOOT MODE ENTER`
follows two frames later. Thirteen seconds of game time.

**Why every earlier attempt failed, and it was not the game.** The supermarket
phase is **AREA 230 + SCENE 56 or 62** (both on set `ASM49`), and AREA 230
carries **no `shoot.begin` of its own** — it is in a ZONE SLOT of the scene.
`--save --area 230` is a street start, which *lands* the player in the room
without running the scene chunk that would have been loaded on the way in, so
the trigger never exists. `--scene-chunk N` does exactly that, and its own
help says so: *"A street start jumps straight to an area, so the chunk that
would have been loaded on the way in never is."*

> **THIS COST THREE FIXES THEIR CONFIRMATION.** 97d, 97e and 97g all shipped
> unverified, and a reader played the sequence three times to test them,
> because I looked for the room by grepping set names — which found a
> DIFFERENT supermarket, AREA 68 — and never read `omk-play`'s own options.
> `engine/tools/shoot_trigger.cpp` now answers "which chunk starts a shoot
> phase" directly, scanning zone slots *and* the startup script at `+4`, so
> the question does not have to be guessed at again.

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

**The aim is read too** (`sub_44D7F0`), and with it the 60 bytes account for
**exactly**: fifteen fields, no gap and no overlap. The velocity is at
`+16/+20/+24` — the slot's direction rotated into world and scaled by the
speed — with three 1.0 scales after it. Its refusals are object-graph ones:
no node, or `o3de_UnlinkObject` / `o3de_LinkObjectToParent` failing.

Where the direction comes from is worth a line: `actor + 12·slot + 100`, a
per-slot `float[3]`. Four slots of twelve bytes fill `+100..+148`, and `+148`
is where the four fire timers begin — the two arrays butt exactly, which is
what makes reading both per-slot safe.

> **AND THE TRIGGER DOES NOT REACH THIS DIRECTLY — the shortcut is refused.**
> Wiring the `Tir` bit straight to the pool was the obvious next move and it
> would have been wrong. The engine's chain is:
>
> 1. the input word drives the `.CTL` channel into a firing state;
> 2. ~~something at `loc_45C4DD` sets the latch `dword_53AE3C` (the one write
>    that is not a clear, ...)~~ — **CORRECTED §7h**: `loc_45C4DD` is a CLEAR,
>    `mov dword_53AE3C, ebp` right after `xor ebp, ebp`, inside
>    `Actor_PlayClip`. The one SET is `MDSHOOT0` itself, 0x0046B610 - which
>    is also a function with no `proc` label, and that is the only part of
>    this line that was right;
> 3. `sub_45C680`'s ACTOR_STATE cases ~~13, 14 and 16~~ **1, 3, 11, 12, 13, 14
>    and 16** see the latch - and while the entry's `+12` is -1 (shoot mode)
>    they do not raise the request themselves: they hand the latch to the
>    GATE, `sub_47C2A0`, and only its outcome 2 raises `dword_4E9744`;
> 4. the frame loop calls `Actor_TickProjectiles(player)` **once** and clears
>    the request.
>
> So the player's shot is a one-shot request raised by the STATE MACHINE, not
> a level read off the trigger. A port that fired on the bit would look right
> and would have no rate, no state gate and no channel behind it — the
> approximation `no-approximation-read-original` is about. ~~The pool is
> ported and asserted; connecting it waits on the `.CTL` firing states.~~
> **Connected 2026-09-10, §7h.**

**Not modelled**: the node clone, the matrix the rotation uses (the caller
hands in a world direction), and the packing of property 35's slot-and-count
word.

## 7h. THE SHOT, CONNECTED — 2026-09-10, `cc3d1f9`

Read link by link before anything was wired, and the reading corrected the
handoff twice (§7f above).

**The chain.**

1. `H1Avnt` group 200's entry **[132]** takes input `0x10` - the *Tirer*
   scheme's slot 4, `Tir` (keyboard 54, mouse button 12) - with clip -1, no
   GoTo and move name `MDSHOOT0`.
2. `Cef_QueueSpecialMove` queues the handler; `Actors_TickAll` drains the
   queue AFTER the actor's state tick. `MDSHOOT0` is `tab_special_move[8]`,
   0x0046B610, and its whole body, from the BYTES:

       8b 44 24 04 | 83 b8 94 01 00 00 03 | 75 0a | c7 05 3c ae 53 00 01 00 00 00 | c3
       mov eax,[esp+4]; cmp dword [eax+194h],3; jnz done; mov dword_53AE3C,1; ret

   So it arms the latch in ACTOR_STATE 3 and nowhere else.
   **`tools/asmfn.py 0x0046B610` returns a different function** - the address
   has no `proc` label, so it snaps to a neighbour whose code calls
   `sub_41C350`, `sub_4083F0(0x30)` and `sub_41C490`. That is where
   `todo/omk-play.md` 97's "MDSHOOT0 is the EQUIP, not the shot" came from:
   CLAUDE.md §1's snapping trap, written up as a finding.
3. The NEXT frame's channel tick reaches `sub_45C680` (and `sub_45CF50`, the
   same code on a clip's roll-over). Cases 1, 3, 11, 12, 13, 14, 16 test
   `sub_45AB80` - "channel `+184` (the current entry) is set and its `+12` is
   -1" - and all 24 of group 200's entries carry `flags12 = 0xFFFFFFFF`
   (`ctl_find` now prints it). That takes the SHOOT branch: reload the record's
   `+172` from `*row` when it is `<= 0`, call `sub_47C2A0(actor, -1, latch &&
   row, record)`, clear the latch. Outside it the latch is the request, with
   no rate.
4. `sub_47C2A0` is the GATE. Outcome 2 raises `dword_4E9744` for the player;
   a gunman fires `Actor_TickProjectiles` there and then.
5. The frame loop, after `Actors_TickAll`: `if (dword_4E9744)
   Actor_TickProjectiles(player), dword_4E9744 = 0`.

A press therefore reaches the gate ONE frame after its state.

**The gate** reads the record's `+160/+172/+176/+180`:

| field | what |
|---|---|
| `+172` | the rate countdown. The caller reloads it to `f0` when `<= 0`; a shot is taken only on a tick where it EQUALS `f0` - an exact float compare - and it then counts down by dt, floored at 0 while pulled and at **1.0** while released, so a lowered gun is never reloaded |
| `+176` | how far the weapon is LOWERED, 0..1. Pulled: `-= 0.2*dt`, clamped at 0. Released: `+= 0.1*dt`, capped at 1. Fire only at exactly 0 |
| `+160 0x40000` | a pull PENDING: set on the first pulled tick and held until a shot clears it, so one tap fires one round even though the weapon takes frames to come up |
| `+160 0x80000` | the pull counted: stops a held trigger re-arming 0x40000 after each shot; cleared with it on release |
| `+160 0x10000` | a shot this pull; cleared on the next pulled tick, or when fully lowered |
| `+180` | the weapon row - null, and the gate returns at its first line |

The steps are formed in double and stored back to float, which matters: from
rest (1.0) five steps of 0.2 leave a residue, so a tap from rest fires on the
SIXTH frame, not the fifth. Held, the key-1 gun fires every 10 frames (its
`f0`); the key-3 row every 2. The first tap of the mode fires at once (the
record is zeroed: weapon up, and the reload makes the countdown equal `f0`).

**The weapon's TYPE** (`Shoot_InitWeapon`) is the held object's kind - event
46 property 3 is the OBJECTS record's `+2` - with one exception: kind 1 with a
`B` eleven characters from the end of the model name is -2. That name is
descriptor `+48`, which `Scene_Load3DO` fills with its own PATH, built by
`Object_Load` as `MESHES\OBJETS\` + `Object_ModelPath(stem)`, which appends
`.3DO` (the five bytes at 0x4C0D1C). So position -11 is the first letter of a
seven-letter stem, and of the seven shipped weapons only `BATPOUV`, the Baton
de pouvoir, qualifies: the -2 row is the baton's. All seven objects the ten
`GLOBAL +42` slots name resolve to a row.

**The shot** takes `Actor_TickProjectiles`' RECORD path (actor `+164`, the
held object, carries a node at its `+12`), not the four-slot walk §7f
ported. Free scan first - a full pool spends nothing - then the magazine
(property 35 slot `index - 1`, read here with `readAmmoSlot`; a count at or
below 0 asks event 48 for a weapon change and the round is TAKEN anyway), then
entry `+8` = the row's `f1` (the SPEED, no x3.9 on this path) and `+56` = `i2`
(the DAMAGE). **So `tables/shoot_weapons.json`'s "f1 and i2 have NO reader"
was a scan of `05_sys.c` alone** - both are read in `17_script.c`. The aim is
`sub_442160(0, (yaw + 90) deg, -pitch deg)` through (-1, 0, 0): at pitch 0 it
is exactly the (0, 0, -1) forward the brain aims with, and a positive pitch
rises.

**Checks**: `shoot fire` (the probe; every value produced FIRST by an
independent float32 model of the same lines, then matched) and `engine: shoot
fire` (--slow: nine taps of 54 in the gallery, eight latches, eight shots each
SEVEN frames after its latch - one for the queue drain, six to raise the
weapon). **Shown to fail**, each reverted by writing the bytes back: the raise
step 0.2 -> 0.25 (both red: 4 and 5 for 6 and 7), the held pull removed (both
red), the pitch sign (probe red; the gallery fires at pitch 0), the baton's
`B` (probe red; the gallery holds the Waver), and `MDSHOOT0` unwired in the
viewer (engine red, 0 shots).

**And the mouse, found on the way and NOT acted on**: the engine does read
mouse motion. `sub_47D370` (one caller, `19_dsound.c:4383`) turns `+420` by
`-word_90E1AC * 0.01 * dx` and the pitch `dword_657A10` by `word_90E1AE * 0.01
* dy * dt`, its sign by the invert byte `0x90E1B0`, clamped at **+-45**
degrees - and those three sit in the options header (`byte_90E180` + 44/46/48).
So the handoff's "nothing shipped governs them" is wrong: this port's 0.18/0.14
degrees a pixel and its +-70 clamp are its own, and the original's are
readable. The keyboard look, `sub_47CFC0`, is 2 degrees a frame on the same
clamp.

## 7i. THE FLIGHT — 2026-09-10, `96fab56` (step 2a)

`Projectiles_Tick` (0x0044D930) runs BEFORE `Actors_TickAll`, so a shot fired
this frame first moves on the next. Per live entry:

* `+52 > 0` holds it at the muzzle - the WIND-UP - counted down by dt;
* else the segment `a = pos - 0.5*h`, `pos += vel*dt`, `b = pos + 0.5*h`,
  where `h` is the NODE's -X axis read back through its matrix, not the
  velocity;
* while `+40 > 0` (with a sprite) it GROWS: `+40 -= dt` and the three scales
  by the sprite's step;
* `+12 += speed*dt`;
* the ACTOR sweep (§7j), then - only if no actor was hit - the WORLD ray
  `sub_4449E0` over every mesh `sub_444460` does not skip, which is exactly
  those flagged 0x800000: the port's RENDER soup, at radius 0;
* retired when `+12 > 1968.5039` (50 m) or on a hit.

`+40`, `+52` and the scale step come from **`shoot2.sfx` section A** - 14
40-byte rows, which `Shoot_Enter` loads for them and `sub_44EEB0` looks up by
the held gun's ROOT mesh name (`FindNodeByName(model, 0)`, the first node):

| row | +20 grow | +24 wind-up | +28..36 step |
|---|---|---|---|
| `Waver` | 8 | 0 | 5.9 0.2 0.2 |
| `BATpouv` | 9 | 0 | 4.7 0 0.1 |
| `Dwaver` | 11 | 0 | 7.4 -0.1 4.1 |
| `Megazok` | 0 | 12 | - |
| `Gigazok` | 0 | 14 | - |

so the Waver's bolt stretches into a streak over its first eight frames, and
the two rocket launchers wait 12 and 14 frames at the muzzle.

**The bolt IS the gun's `tir` mesh** (the string at 0x4C2E4C, flags 0x3000 -
the additive bucket). `Object_Load` unlinks it from the gun; the shot clones
it, links it under the HAND with its own `+128` local - so the muzzle is the
hand's pose applied to `tir`'s local, which is also `tir.pos - root.pos +
root.local` (-15.8, -3.2, -1.7 in the Waver) - and draws it in the node's
matrix with its scales.

In the gallery the eight bolts each hit the back wall at z -3158, three frames
and 374.4 units out, and the pool empties between taps. Seen headless: a green
streak from low in the view toward the crates - and it leaves slightly RIGHT
of centre although `Maing` is the left hand, which is a handedness question
only a person watching can settle.

**Checks**: `shoot fire` gains the flight - a wall at z -499.5 met on frame 4
(chosen so the half-unit front edge is what makes it 4 and not 5), the range
on frame 16, a 12-frame wind-up first moving on 13, the Waver's growth ending
at (48.2, 2.6, 2.6), and section A read by the port - and `engine: shoot fire`
the eight retirements. **Shown to fail**: the range, the front edge, the
wind-up (probe red each) and the viewer's ray (engine red: every bolt "out of
range" on frame 16).

**Labelled**: the shot soup is baked at rest (a mesh a scene program moves is
not followed); the resident but unlinked set is not tested (it is not in the
scene graph); the entry's `+40/+52` without a sprite are zeroed where the
engine leaves the last user's values.

## 7j. THE HIT — ported 2026-09-10 (step 2b)

**Ported** (`actor/shoothit.{h,cpp}`): the sweep, the damage, the reaction and
the death clip. In the Shooting gallery three Waver bolts kill gunman 240 -
15, 10, 5, 0, every hit band 2, death clip type 5 - and the bolts after them
stop at the corpse doing nothing (`verify.py: engine: shoot hit`). Three
things came out of porting it that the reading below did not say:

* **The engine's box test omits a bound, and it is kept.** `sub_4986B0`
  checks the non-chosen axes against ONE bound each: an axis whose start
  lay above the box needs the hit at or above its minimum, one whose start
  lay below needs it at or below its maximum, and an axis whose start lay
  BETWEEN its planes is not checked at all. So a line 40 above a pelvis -
  outside the head's sphere, inside the body's 44 - still counts on the
  pelvis box. Read from the assembly (0x004986DC..0x00498852), not the
  decompiler; the probe asserts it.
* **`+76..+84` is each mesh's sphere CENTRE**, in its own frame - half a
  limb's length along it - and the ROOT's radius is the whole body's (44.4
  on VIR_FN, 42.5 on HO1_FN), which is what makes the node's sphere a
  whole-body first test. `omk::Mesh::centre`, read now.
* **A placement-only body was drawn turned about its MODEL ORIGIN.** VIR_FN
  is authored at x 546, so at a facing of 89 its pelvis was drawn 770 units
  from its placement - where its own brain did not think it was, and where
  no bolt through the placement could meet it. The engine puts the node
  (the pelvis) at the placement and hangs everything off it, so a body turns
  about its pelvis; the viewer does that now. The per-mesh positions the
  shadows read (`meshAt`, `headAt`) had a second fault of the same family -
  offset first, turned after, about the WORLD origin - and are fixed with
  it. **This moves every placement-turned body in the game**, not only in
  shoot mode: it wants a person's eye.

Still not ported, labelled: the noise alert (`sub_4246E0`), the explosion
over damage 10 (`sub_424470`), the reactions' `+148` (its writer is
unread), types 13/10 among 0x4000 victims, the player being hit (nothing
fires at him yet), and the death clip's pick is a fixed function where the
engine's is `rand()`.

**And the bystanders** (found by the first supermarket play, 2026-09-10): the
engine's sweep list is every ATTACHED actor, not the gunmen alone, so a bolt
that meets a bystander stops on him and `sub_4240E0` refuses the damage (not
ACTOR_STATE 3). The viewer had given the sweep only the shoot-mode gunmen;
it gives it every drawn staged actor now - 9 bodies in the supermarket
where it had 1, `V5H_FNM` beside the gunman among them. Labelled: the
street crowd and the vehicles are the slider system's nodes and are not
in it, and a crowd model's three unposed LOD skeletons are swept at rest.
The play itself: 49 shots and no hit, because only one was aimed near the
gunman - high (pitch +4.2, 46 units off his pelvis against a 45.8
sphere) and stopped by the world at 127 of his 146.

**Shown to fail** (`f1a58cc`), each reverted by writing the bytes back:

| mutation | `shoot fire` (probe) | `engine: shoot hit` (gallery) |
|---|---|---|
| the box test given BOTH bounds, standard Woo | red | **red** |
| the band threshold 0.5 -> 0.3 | red | green - head-on bolts are band 2 at either |
| gunmen allowed to hurt each other | red | green - every gallery shot is the player's |
| placement bodies turned about the model origin again | - | red: roots back at (3963, -2266), no hits |

The first row is a finding and not only a proof: with the textbook two-bound
test only the three killing bolts meet actor 240, and the two fired at his
body after the death clip miss it. So the bound the engine leaves out is
what decides real hits in this very scene - keeping it as transcribed is
observable in play, not a curiosity of the listing.

The reading it was built from:

* **What can be hit** - `sub_45E9C0` sweeps two lists `sub_45DF50(1, 320)`
  sizes: 320 20-byte records that `Actor_Attach` fills with every attached
  actor's node (`sub_45DFF0`), and one pointer `Player_SetActor` fills with
  the player's (`sub_45E140`). The shooter's node is excluded. Props are not
  in either - they stop a bolt only through the world ray.
* **Per actor**: the node's sphere (descriptor `+36..44`, the transform
  pass's world position, radius `+88`) against the segment (`sub_498860`),
  then `o3de_Traverse` with `sub_45ECA0` per MESH: its sphere (centre
  `+76..84` rotated by the mesh matrix, radius `+88`), then the segment
  carried into the mesh's frame and slab-tested against its local box
  `+92..+112` (`sub_4986B0`). Nearest hit wins. Boxes, not triangles.
* **The damage**, `sub_4240E0(victim, i2, entry, segment)`: refused unless the
  victim OR the shooter is the player (gunmen do not hurt each other), the
  victim is in ACTOR_STATE 3, and its record `+160` lacks 0x800. Damage 6 -
  the baton's `i2` - reaches ONLY victims flagged 0x4000 and everything else
  is refused by them (types 13 and 10 go through `sub_47FD90` /
  `sub_47DF60`). The player's Body Shield, property 17, takes up to 100% off,
  leaving at least 1. `+160 |= 0x1020`; health `+92 -= damage`.
* **The direction**, `sub_423E20`: the dot of the bolt's horizontal heading
  with the victim's forward, in four bands - `|dot| <= 0.5` gives 0 (dot <=
  0) or 1, else 2 (dot <= 0) or 3. The constants are 0.5 and 0.0, read from
  0x4BC274 / 0x4BC224; the decompiler's version lost the x87 branches.
* **A gunman hit and alive** (`sub_423EF0`): unless `+144` is 8, `+160` has 2
  or 0x4000, or the type is 12 - property 24 is a health threshold: above it
  `Shoot_ActorAction(+148)`, at or below it action 4. **Dead**: the enemy
  count `dword_4E9764` drops (unless `+160` has 2), a death clip of type 6 /
  7 / 5 / 8 by the four bands, `+160 |= 8`.
* **The player hit**: alive, `sub_47D1F0` (the hurt camera and sound by
  band) and property 1 written back; dead, `sub_423FC0` - every attacker's
  action to 0, ACTOR_STATE 15, `.CTL` group 201.
* **The port's missing piece** is per-mesh world transforms for a staged
  gunman, which the viewer composes only into drawn corners. They should be
  captured at the draw: the engine flies BEFORE the actors tick, so a bolt
  meets each body as it stood at the end of the previous frame, which is
  exactly what a pose kept from the draw is.

## 8. WHAT IS LEFT, IN ORDER — planned 2026-09-10, after the first firing play

The reader played the gallery: *"Ok, note: no fire sound effect, no UI, don't
forget to plan it."* The session's own log agrees with the gate: 245 latches
(`MDSHOOT0` arms one on every held frame) gave 36 shots, never two closer than
10 frames, seven frames from rest, and all 36 stopped by the world.

0. **THE FIRST-PERSON WEAPON VIEW — reported with two frames of the original,
   2026-09-10.** The original draws the LEFT ARM and the GUN in first person:
   low at bottom right at rest, raised to the centre when firing, and the bolt
   leaves from the gun there. The port draws neither. Not a script failing to
   load: the port finds the gun (`the gun WAVER ... tir found`) but never draws
   it, and it hides the player whole (97a's guess). Read so far:
   * **The hide rule has an EXEMPTION, and the data names it**: the player's
     tree is hidden except nodes flagged `0x200000` - in HO1_FN exactly
     `UAvantg`, `UBrasg`, `UMaing`, the LEFT forearm, upper arm and hand - and
     `Object_Load` sets `0x200000` on every object node it loads (`04_sys.c`
     6779), so the gun in that hand stays too. The tests are at `10_dsound.c`
     1635/1655 (not yet read).
   * **The arm's pose is a LAYER**: the shoot record's `+84` is the bone table
     at 0x4C3798 (40 words by bone id, 1 = the upper body); `sub_471070` binds
     those bones to H1Avnt group 202's default, `S_AUTOLK` (clip 49), over the
     channel's clip; `sub_434C30` then hands the aim angles (`dword_6A4720/24`)
     and the lowered amount (`+176` via `dword_6A4728`) to `sub_471950` - 430
     lines, unread - which bends them. That is the raise.
   * **The gun** rides `Maing` through `sub_41C490` with `tir` unlinked
     (`Object_Load`), which the held-prop path already draws for a take - so it
     is the same drawing with the weapon object and `tir`'s corners left out.
   Order: ~~the hide rule and the gun first~~ **DONE 2026-09-10**: the viewer
   keeps posing the player in first person and draws only the meshes flagged
   0x200000 (225 of HO1_FN's 1626 corners), and draws the held Waver on
   `Maing` without `tir`; the headless frame puts the gun low at the bottom
   right at 75-90% of the width, where the original's first frame has it at
   72-87%. **THE RAISE, DONE 2026-09-10** (reported: *"The animation of the
   arm when firing is missing"*) - `actor/shootaim.h`. **CONFIRMED IN PLAY
   2026-09-10** in the supermarket (*"ok, good"*): 62 shots, the muzzle
   rising 25 units as the look went from -22 to +33 degrees, gunman 77 hit
   twice and killed. `sub_471950` read
   whole: per bone the table marks (by the mesh's SLOT, `+12`), four of the
   `S_AUTOLK` track's keys picked by the band of sin(yaw) (±0.707) and the
   sign of sin(pitch), slerped by `|pitch| x 488.924` then `|yaw| x 366.693`
   in 256ths - NOT clamped, so 45 degrees runs past its keys - then slerped
   toward the stance's (group 200's default) KEY 1 by `lowered x 256`, and set
   as the bone's local rotation. The angles slew 30 degrees a frame toward
   their targets in the gate's PULLED arm only (yaw 0, pitch the look's).
   Headless, the Gun Waver sits low at the right with the weapon down (frame
   25) and at the centre as it fires (36); the bolt leaves it there (39). The
   muzzle moved 5.8 toward the eye's line and 16.9 forward, which moved both
   engine checks: `shoot fire`'s bolts now retire at z -3161 (was -3155), and
   `shoot hit`'s fourth bolt meets the corpse (was a miss). **Shown to fail**:
   with the layer forced off both go back to their old values, and the
   muzzle back to 4990.6 / -2924.7. The AUTO-LOCK (`sub_47CEE0`, which aims
   the arm's pitch at a target in the cone - the clip is named for it) is not
   modelled. NOT yet: the HUD the same frames show - a health bar on the
   left, a turning pentagon with a number (244) top left, the weapon's icon
   and name bottom left.
1. ~~**THE HIT (§7j) — step 2b, next.**~~ **PORTED 2026-09-10, §7j.** And the NOISE, read 2026-09-10 and **PORTED the same
   day** (`actor/shoot.h` `shootHearNoise`; `verify.py: shoot fire` `noise:`,
   `engine: shoot noise`):
   `sub_4246E0` is called at every shot (with the muzzle) and every impact,
   and it is not a sound - it ALERTS gunmen. Each one flagged 0x40, not yet
   0x20, not in STATE 14 (`+156` - this said "type" until the loop was
   read against its offsets) and not the player, within `property 28 × flt_907EAC`
   (a hearing range in grid cells) and on the same floor as the noise's cell
   (`sub_435020`) gets `+160 |= 0x20` and `Shoot_ActorAction(+148, or 2)`.
   It is the brain's input. Read to the end against the LISTING (the
   decompiler hides one arm): on the noise's floor the gunman gets `|= 0x20`
   and, unless his script step `+144` is 8, `Shoot_ActorAction(him, +148, 0)`
   or action 2 when `+148` is -1; on ANOTHER floor no alert, only
   `Shoot_ActorAction(him, +148, 0)`, -1 included. `+188` is his FLOOR (the
   port's `ShootRecord::node` misnames it). The five call sites: the shot's
   muzzle (0x44D5CC / 0x44D7A5, the shooter excluded), a bolt on a body
   (0x44DD0E / 0x44DD40, the VICTIM excluded - `sub_4240E0`'s first argument)
   and a bolt on the world (0x44DE15, after `sub_44F0D0`, the owner
   excluded); a bolt running out of range makes none. The viewer now loads
   `MAP2D\<+106>.MPT` beside the radar. Stand-ins, labelled: a gunman's
   floor is `sub_435020` of where he stands (the viewer's gunmen never walk
   the grid, so nothing keeps `+188`); a record the port marks dead (`& 8`)
   is passed over for the death arm's `& ~0x40`; and `Shoot_ActorAction` is
   RECORDED on the Session as the hit's is - its arms are not ported, and a
   -1 action (case 0) is not recorded. In the supermarket the first shot
   alerts actor 77 (hearing 20 cells of 39); in the gallery all three gunmen
   are alerted already by sight.
2. ~~**THE FIRE SOUND — reported missing in play.**~~ **PORTED 2026-09-10**, and
   the lead below held. Section A's three ints are EFFECT ids: +0 the MUZZLE
   (`sub_44EF80` as the entry is made, with its last argument 0, and every
   frame of the wind-up with 1), +4 the FLIGHT (`sub_44F030`, twice a frame),
   +8 the IMPACT (`sub_44F0D0`, on a body or the world, not on the range
   running out). `Sfx_RegisterEmitter` arms the effect's sound countdown
   (+36) only when that last argument is 0, and `Sfx_TickAmbient` plays the
   sound as the countdown goes negative - the same frame, +36 being 0.0 in all
   42 rows - through `Scene_FindSoundIndex` in the RESIDENT library, which in
   the mode is `shoot2.scx` (`Game_Start`), and `Sound_Play3D` with distances
   78 and 584. `shoot2.scx` carries 66 sounds and the names corroborate the
   ids: the Waver fires WAVER2.WAV (687) and hits WIMP1.WAV (689), the
   Megazooka MEGAZ3.WAV (703) and IMPZ2.WAV (683). Ported as the fire and the
   impact sounds (`verify.py: shoot fire`, `engine: shoot fire`); the curve
   between 78 and 584 is DirectSound's and labelled. NOT yet: the effects'
   SPRITES (the muzzle flash, the trail, the impact; effect 93 on an accepted
   hit), which the port's particle field can draw. The original lead: Not `sub_4246E0`. The lead
   is in the data: each `shoot2.sfx` section-A row opens with THREE ints -
   the Waver's 1, 2, 3, the Megazooka's 7, 8, 9 - and those are section-C
   EFFECT ids. Row 1 carries sound **687** (a 2-frame life), row 2 none (12
   frames), row 3 sound **689** (6 frames); 7 carries 703 and 9 carries 683.
   So a shot very likely spawns three effects - at the muzzle with the fire
   sound, along the flight, at the impact with the hit sound - through
   `sub_44EF80` (called while the entry waits at the muzzle and on the node),
   `sub_44F030` (each step of the flight) and `sub_44F0D0` (an impact). To
   confirm: read those three and how the row `sub_44EEB0` returns reaches
   them. The sounds resolve in the resident library, `shoot2.scx`
   (`Scene_FindSoundIndex`), which the port already loads for the mode.
3. **THE SHOOT HUD — reported missing in play. ALL FOUR PARTS PORTED
   2026-09-10 and CONFIRMED IN PLAY** (the radar as `radar = always`; the
   game's own switch hides it in seven arenas, below).
   Screen 34 (`SHOOT HUMAN`) is ONE panel of six items (0x4C4618), and every
   callback is native with no `proc` label - read from the IMAGE with the
   system's `objdump` (llvm, i386 COFF), which settles the whole layout:
   * 0x4C43D0 (48, 0) 100x100 - `meshes\objets\anneau.3do`, loaded by the
     open (0x42E4A0), turned 10 degrees a frame and drawn in the box: the
     RING, what a reader's frames show as a turning pentagon;
   * 0x4C4418 (48, 100) - 0x42E9E0 prints `sub_42B1C0(5)` (player property
     5, ANNEAUX) into its buffer after `{C}`: the ring count, 244 in the
     frames;
   * 0x4C4460 (48, 380) 100x100 - 0x42EA10 reloads the HELD object's model
     (descriptor +48) on the shot's refresh flag, sets 0x200000, unlinks
     `tir`, turns it 25 degrees a frame; its text (0x42EB20) is the object's
     name, event 46;
   * 0x4C44A8 (48, 360) - 0x42EB60 prints `dword_90E11C`, or a .bss string
     nothing writes by address when it is -1; `Shoot_InitWeapon` sets it to
     the magazine's count (property 35) or -1 for a row without one;
   * 0x4C44F0 full-screen - 0x42E870 submits the four quads at 0x4C4680
     offset by half the display (the CROSSHAIR, 2 x 8 at +-4..12), then
     `Hud_DrawBar(health, 200, 0, 0)` - the health bar - and shows the
     top-right rectangle only when a map is loaded;
   * 0x4C4388 (450, 8) 180x180 - 0x42F000, the MINIMAP, drawn from
     `RADAR\<level>.WRE` when one ships: SOUKT, SMARKET1 (the supermarket),
     SOUKDOCK, HAMES, ARCHIV03/05, TETRADOU, TETRA2/3, each with its own
     scale (0x42EE70).
   The vertical bar at (12, 20) 12x434 that 0x42E670 resizes by health is
   screen 33's - the MECAGARDE's HUD - not this one.
   **Parts 1-3 CONFIRMED IN PLAY 2026-09-10** (the supermarket, *"ok, good"*).
   **Ported (part 1)**: the panel composed over the frame from a walk of its
   own (it takes no input), the three texts as row text by item address, the
   items' fills, and the crosshair - `verify.py: engine: shoot hud`.
   **Ported (part 2)**: the two TURNING MODELS, through the sneak previews'
   own path (`ui/models.h`) - `sub_478DE0`'s literal arm (a positive
   distance, 10.0 for the ring, 25.0 for the weapon, no box fit) and
   `sub_478EC0`'s turn, which is oscillator 4 exactly as the sneak's; the
   weapon loaded by its stem with `tir` left out, reloaded when it changes
   (the refresh flag is raised by `Shoot_Enter` 0x422500, the shot and
   `MDGUN`).
   **Ported (part 3)**: the HEALTH GAUGE, `Hud_DrawBar(dword_90E100, 200,
   0, 0)` - the call is 0x42E870's last, read from the image - in
   `ui/hudbar.h`. `dword_90E100` is the player's record +92
   (`Shoot_SyncHudHealth`), which `Shoot_Enter`'s own `sub_422540(player)`
   fills from his property 1, a 0 rewritten to 10; the save's player has
   10, so 5% of 200. `sub_4480D0` draws a vertical gauge at x = 24, y 22 to
   458: layer 2 the black frame (a diamond of radius 17 at each end, the
   14-wide column), layer 3 two key-source blits from `jauge1.bmp` into the
   6-wide channel (the empty part from source columns 0..3, the full part
   from a column that SCROLLS one a frame, `++dword_531050 % (W - 3)`),
   layer 4 a 0xE0E0E0 flags-2 quad over the empty part; `sub_446E20` then
   walks 35 sparks (x drifting out 0..2, y rising 0..3, age +0x04000000 a
   frame, reborn at the fill's top), radius-2 diamonds of `age | 0x18AD8C`
   on layer 5. **The D3D back end**, not the software one: `sub_480AC0`'s
   blend pairs make flags 4 `src*(1-a) + dst*a` (the frame opaque, a spark
   FADING as it ages) and flags 2 `dst*(1-src)`, and the diamonds are
   rasterised as polygons, where mode 2 would fill their bounding box -
   the same choice as `screendraw`'s item fill, which a player's
   screenshot confirmed. Reconstruction, labelled: `rand()` on a seed of
   its own, so the sparks' sequence cannot match the engine's; the
   swim-state rescale left out. Mode 1 (the horizontal bar, `jaugeg.bmp`)
   and mode 2's `sub_447000` are not ported - shoot mode asks for neither.
   And note `sub_446E20`'s `top == 458` test is a LITERAL: at any display
   but 640x480 an empty gauge still sparks, in the engine as here.
   **Ported (part 4), 2026-09-10: the MINIMAP** - `ui/radar.h`, drawn by
   item 0x4C4388's own callback; `verify.py: shoot radar files` (the data)
   and `engine: shoot radar` (the draw). What it is:
   * **Which areas.** `Area_TickLoad` copies the AREA's `+106` (the MAP2D
     stem, now read into the resident slot) and appends ".MPT" (0x4C0D34);
     `Map2D_Load`'s tail 0x42EE70 - which the decompilation calls
     `Ambience_Load`, wrongly - rewrites the last three characters to "WRE",
     clears the enable flag `4EB8C8` and the file `4EB8C4`, and on an exact
     match with one of nine literals sets the height `90E0B4` and loads
     `RADAR\<name>`. Of the 16 areas that name a map, **9** get a radar
     (TETRADOU, ARCHIV03, ARCHIV05, SOUKDOCK, TETRA2, TETRA3, HAMES, SMARKET1,
     SOUKT); the Shooting gallery names GALLERY and gets none. Six shipped
     files can never load.
   * **The file**: `u32 vertices, u32 edges, float3[vertices], u16
     pair[edges]`, exact for 11 of 15 - all nine reachable - and the four
     short ones unreachable. A vertex's Y is UP.
   * **Enabling - and it is effectively CUT in seven of the nine** (found
     2026-09-10 on the reader's *"I don't remember having it in the original
     game"*; this bullet first said screen 34's open turned it on, and that
     was screen 33's). The switch `dword_4EB8C8` is the SCRIPTS': ops 146 and
     147 set and clear it - the opcode table names them `ambience.on/off`,
     and the name is wrong. 20 shipped sites, all in shoot phases: an `on`
     after `shoot.begin`, BARE in the Archives (AREA 63, 67) and in the other
     seven arenas behind `var.set.has_object 0, 980, 20` - object 980, "Radar
     activé" (kind 0, stem `BATTERIE`, 100 seteks, "Le Radar est activé."),
     which no script, conversation, native code, starting inventory, DB list
     or save gives. The HUMAN HUD's open 0x42E4A0 leaves the switch alone and
     puts the item at (450, 8) 180x180 with a 354.33 (9 m) camera; the
     ROBOT's (screen 33, "SHOOT MECA") 0x42E3A0 sets the switch itself, moves
     it to (456, 8) 174x131 and 275.59 (7 m). The static 236.22 is never
     used. 0x42E870 hides the item whenever the switch or the file is
     missing. **Ported as the game has it** - the supermarket's radar stays
     hidden - with `radar = always` (`todo/enhancements.md` 10, off by
     default, the reader's choice) to restore it in every arena. The item has no
     fill (bank B `0x44000000`, and the fill arm wants `0x10`), so the lines
     go straight over the scene.
   * **The draw, 0x42F000** - a small wireframe renderer: `sub_442160(pi/2,
     facing + 180, 0)` (the third angle `0x4EB8CC` is never written), the
     live camera's focal lengths (`sub_4943D0`) scaled to the box and by 0.7,
     every point offset by `(D cos t - px, py - height, D sin t - pz)` with
     `t = facing + 90`, `sub_442F00` and `sub_441E50`, and a depth of at least
     10.0. An edge with both ends in front is clipped by `sub_42EC80` and
     drawn as an opaque GOURAUD line between its ends' greys (`0x80 +- 0x60`
     by height against the player's, linear inside 3 m), on layer 4 plus one
     per end above him. The player is an 8x8 **BLUE** square (0x0000FF, layer
     6 - not the green arrow this record guessed before the draw was read);
     each actor attached to the scene (state byte `+1307` = 2, `Actor_Attach`)
     in ACTOR_STATE 3 (`Shoot_ActorEnter`) is a RED square banded by height
     inside 8 m and marked seen, and one seen and now in state 0 (the brain's
     `health <= 0` arm) a dim 0x10 - so a killed gunman leaves a faint mark.
     Layer 5. The I2D walk draws layers ascending and each in reverse
     submission order.
   * **Labelled**: the seen flags by actor id (the engine's by slot); the
     line's pixel rule Bresenham without its last pixel; the min/max height
     the draw tracks and never reads left out.
   * In the supermarket's first shoot frame, with the item or `radar =
     always`: all 1789 edges in front, 252 lines in the box, one gunman, his
     square on the box's centre column low in it - the 9 m behind him.
   The first reading, kept: `Shoot_Enter` opens screen
   34 (33 for the Mecagarde) and calls `Hud_Refresh` (0x00448FA0, the
   player's properties 16/19/17/3/18/2 into `dword_530CB0..C4`).
   `Hud_DrawBar(value, 200, slot, mode)` (0x00447B10) draws the bars - its
   callers are `16_o3de.c` 3463/3464 (two bars), `24_sys.c` 7150/7657 (a
   shoot record's `+92`, a gunman's health) and `21_d3d.c` 4669.
   `Shoot_SyncHudHealth` copies the player's record `+92` into `dword_90E100`;
   the shot writes the ammo counter `dword_90E11C` and sets the refresh flag
   `dword_90E104`. None of it is drawn: the viewer prints the screen number
   and stops.
4. **THE GUNMEN'S SHOTS.** (First, found 2026-09-10 on the way: the NOISE's
   floor for a gunman was `sub_435020` of where he stood, which is -1 for
   the two supermarket gunmen standing off the grid, so 22 of a reader's
   noises were heard "from another floor" on a one-floor map; it now reads
   his record's `+188`, a signed byte, as the engine does - 0 for every
   gunman here, the value the viewer already gave him (`fresh.node = 0`,
   the memset's) since `Shoot_Think` is not wired. **Correction, same day**:
   the commit that fixed it (`d74c35f`) also claimed gunmen started at -1
   and that `shootEngage` therefore refused them all. That was false - a
   grep missed `fresh.node  = 0` for its double space - and the mutation
   meant to prove the fix is what showed it: setting -1 changed nothing,
   because the older line overwrote it. The floor fix itself has no check:
   the headless route never brings an off-grid gunman into hearing range,
   and the evidence is the reader's session log. And the MEDIKITS, after a reader's
   correction: a kit found in a shoot phase is a ZONE script that applies it
   at once (+50 / +100 / +16), and `Actor_SetProperty`'s tail `sub_423A40`
   is what carries that to the gauge - PORTED 2026-09-10, `script/hooks.h`
   `shootStatSet` (`todo/handoff-shoot-mode.md`). The carried kits are the
   hurt handler's, below 40 health; that runs once the player can be hit.)
   **STEP 1, THE GUNMEN FIRE - PORTED 2026-09-11.** `sub_424DE0`'s epilogue
   on the brain's outcome (readable 05_sys.c 6031): outcome 1 runs
   `Shoot_InitWeapon` on the object in HIS hand through the others' table
   0x4C36F8, reloads an empty `+172` with f0 and pulls the gate with a
   target; outcome 0 pulls it released (a pending pull still fires). Both sit
   behind the **FIRE TEST** `sub_4348B0` - bit 0 of his `.ani` group record's
   `+8`, a word the port's reader had never decoded (`formats/anim.h`
   `animGroupWord8`): over the 11 libraries it is set on ONE group, index 3,
   in braqueur.ani, dock.ani and ztech.ani. **CORRECTION to the plan above**:
   the gate's other arm with its `r/4 - rand() % (r/2)` spread aims only the
   arm POSE (`sub_434C30`, not ported for gunmen); the BOLT is aimed by
   `Actor_TickProjectiles`' own other arm (17_script.c 1375) - straight at the
   player's `+244..+252` from the `tir` node on his `Maing`, each axis moved
   by `(r - (rand() % 100) * r * 0.02) / 3`, r the player's root radius
   (`actor/shootfire.h` `shootGunmanAim`). In the gallery both VIR_FN gunmen
   fire from frame 4, every RATE frames (DBWAVER 10, HEXAGUN 4), and every
   bolt stops on the world - the player is not a hit body yet (step 2).
   `verify.py: engine: shoot gunfire`, `shoot fire` (`gunman aim:`).
   Labelled: the jitter is the CRT's formula from seed 1, not the engine's
   place in its one shared `rand()` stream; the player's `+244` is `pos()`,
   the feet (player.h's open note); HIS property 35 is not written back; the
   `+190` shot count and its LABEL_359 reader are not ported.
   **WHY THE SUPERMARKET'S ROBBERS DO NOT FIRE YET** (measured, a brain trace
   of actor 77): he stands 169 units from the player with the player dead
   BEHIND him (dot/dist -0.971), so the cone refuses and he never engages. The
   hub asks for a TURN - `shootTurnToward` returns 180, the brain picks clip
   type 32 with `+184 = -180 / frames` - and the viewer never applies a
   brain's picked clip: `sub_421A20` starts it (flag 8, `+16`), and
   `sub_421770` turns `+420` by `+184 * dt` each tick until it has played
   out, then clears flag 8. braqueur.ani's group 3 HAS the turn clips (type 32
   25 frames, 30/31 15 - every node there is named `NULL`, so a clip's name
   says nothing). And note the collision this exposes: the viewer reads flag 8
   as DEAD, and the engine's flag 8 is "a picked clip is playing" - death is
   one such clip. **STEP 1b, THE TURN - PORTED 2026-09-11.** Two faults, not
   one: the viewer never played a picked clip, AND the port's hub tail
   (`+168 <= 0`, true every tick) wrote the default action into the same
   `clipType` the turn had just set - the engine stores the turn at `+16`
   and the tail's `Shoot_ActorAction(him, 0, 0)` leaves it alone. So
   `ShootStep::turnClip` now carries the turn apart, and the viewer runs
   `sub_424DE0`'s prologue: with flag 8 up, `sub_421770` advances his frame
   and turns `+420` by `+184` a tick, the whole brain holding until the clip
   runs out; LABEL_359 starts a picked turn clip at frame 1.0 (flag 8,
   `+184 = total / frames`), or, with no clip of the type, turns him at the
   fallback rate. He is posed on the clip while it plays. DEAD is now flag 8
   WITH health at or below 0, in the viewer's brain gate and in the noise.
   Robber 77 turns 0 -> 187.2 over frames 394-418 and fires every 15 from
   418 (`engine: shoot gunfire`). Labelled: only the TURNS are played - the
   other clips an arm or `Shoot_ActorAction` picks are not, and the action
   channel itself (363 lines) is still a stored integer; the clip's root
   motion and the `+460` interrupt; the pick among several clips of a type is
   a fixed function, not `rand()`. **And a third fault, found in a reader's
   session log the same day**: robber 519 restarted a turn clip every 14
   frames with his facing stuck at 68.0, and 521 turned 7.2 of his 180. The
   viewer's per-frame PLACEMENT put a placed actor's record facing back every
   frame unless a program had moved him - so a turn kept only its last tick
   (77 escaped it because his scene clip had run). A shoot brain now owns the
   heading; the gallery's 238, out of his engage range and only turning to
   aim, ends at 347 where he was pinned at 357. The same session shows every
   robber firing - 111 gunman shots over the phase, 4 of their bolts meeting
   bodies (three on the hostage, one on another robber, refused).
   **STEP 2, THE PLAYER IS HIT - PORTED 2026-09-11.** First the AIM: the
   player's `+244..+252` is his ROOT NODE's position, not his feet -
   `sub_4800C0` copies `+248` from the root node's `+40` every tick, and the
   generic brain's edge walk moves the two together (`o3de_MoveNodeBy(node, 0,
   climb, 0)`, `+248 += climb`) - so the bolts aim at his root mesh (the
   pelvis) as drawn, and robber 77's rise to him where they fell short toward
   his feet. Then the BODY: he joins the sweep's list as actor -1, his meshes
   as drawn with their world rotations (`playerMeshRot`, captured beside
   `playerMeshAt`); his own bolts skip him, the sweep passing over a bolt's
   owner. Then `sub_4240E0`'s PLAYER ARM, read line by line and ported in its
   order: down already -> nothing; the Body Shield (property 17, 30 on the
   save: 5 -> 4); `+92 -= dmg`; at `+92 <= 0` the death `sub_423FC0` and
   RETURN - before the gauge, the property and the message; otherwise the shove
   `sub_47D1F0`, `dword_90E100 = +92`, event 45 (property 1) and event 43 with
   0 - MESSAGE 0, the scene's hurt handler, which uses a carried kit below 40.
   The gauge is now its own value (`hudHealth`, `dword_90E100`), so a killing
   hit leaves the bar at its last value as the engine's does. In the
   supermarket 77 takes the player 10 -> 6 -> 2 and kills him at frame 480 -
   495 since 6A animates his hand, the muzzle riding it -
   (`engine: shoot gunfire`). **Corrected on the way**: a first cut wrote
   property 1 and posted message 0 on EVERY hit, and so "stored 200" through
   the property's unsigned clamp once he was below 0 - the engine never writes
   a negative health there, because the death returns first. Not ported,
   labelled: the death `sub_423FC0` (he plays on at -2) and the shove
   `sub_47D1F0`. The save carries no kit, so the hurt handler's heal is not
   yet seen on a route. **What the unported death costs, measured in a
   reader's session (2026-09-11)**: killed at frames 1968 and 2887 - the gauge
   frozen at 4 each time - he was each time REVIVED by a floor medikit, to 20
   and to 104. The kit's zone script reads property 1, which the killing hit
   never writes, so it adds to the last survivable health and `sub_423A40`
   hands the result to +92 and the gauge. In the engine the death comes first
   and there is no one left to pick it up. So `sub_423FC0` is step 4's first
   job, before the shove.
   **STEP 4, THE DEATH - READ 2026-09-11, not ported.** It is not a game-over
   screen: it is the scene's own failure path, in two halves.
   * `sub_423FC0(him)` (05_sys.c 4618): every entered gunman still alive
     (`+160 & 0x40`, not `& 0x4002`, `+92 > 0`) is stood down -
     `Shoot_ActorAction(i, 0, 0)`, or his 0x20 cleared on script step 8; the
     player's ACTOR_STATE `+404 = 15`; `sub_436D20` walks his node tree
     clearing mesh flag 2 on every mesh WITHOUT 0x200000 - the whole body
     shown again, not only the first-person arms; `sub_47CE70` releases the
     actor at `dword_6579CC` (its +416/+424 zeroed - unread what it holds);
     `Game_RaiseEvent(43, {9, him})` - MESSAGE 9; then his `.CTL` group 201 is
     played and `dword_4E975C` = its default clip's length, a countdown.
   * `Shoot_TickPlayer` (0x00427AC0, 05_sys.c 7456): in state 15 the countdown
     runs down by the frame delta; at 0 - `+404 = 3`, `Game_RaiseEvent(43,
     {1, him})` - MESSAGE 1, then property 1 re-read into `+92`,
     `sub_436CE0` (the body hidden again), `Camera_Request(4)` with both
     camera actors him, group 200's default played, the mover re-armed
     (`sub_47CC70`), and his weapon re-initialised through event 48 if he
     holds one; `dword_910358 = 1.0`.
   * **What the supermarket does with them** (SCENE 56's subscription table,
     4 records of 8 bytes: script +0, MESSAGE ID +4): message 9 is entry 2 -
     `fade.to_color(0, 0, 80)` and `camera.set 31` ('cam Player Dead'); message
     1 is entry 0, and it ENDS THE PHASE as lost - `shoot.end 1`, every robber
     hidden, every zone and medikit disabled, 'Vie' set back through
     `actor.stat.set -1, 1, 637`, then the MEDITEK sequence (`scx.play`,
     `media.play 251` 'ZVO M011 Méditek Supermarché', the player carried
     out), camera 0, zone 3903 enabled and `scene.unload 230`. (Message 3 is
     entry 1, the robbers' deaths; message 0 entry 3, the hurt handler.)
   So step 4 is the whole failure path, not a flag: the death clip, the dead
   camera, the countdown, message 1, and the phase ending in the Méditek
   scene. The shove `sub_47D1F0` is then step 5.
6. **THE ROBBERS ARE STILL - a reader, 2026-09-11: *"Outside the appearance
   events, ennemies have no animation and do not move"*.** Two halves, and
   they are very different sizes.
   * **A, the ANIMATION** - PORTED 2026-09-11 (`engine: shoot gunfire`: the
     gallery's three loop their type-10 clip, 19 frames, at frames 21-22;
     77 his from the turn clip's hand-back, at 435). Every action
     starts a clip (`Shoot_ActorAction` -> `sub_421A20`: action 0/5 type 11
     or 25, action 1 type 9, the fight actions 10 or 9) and `sub_421370`
     advances it every brain tick that no picked clip plays - `+188 += dt`,
     wrapping to `dt + 1.0` at `Anim_Frames` - so idle and aim LOOP. The
     viewer already chose those clips (`shootClipFor`) and held frame 0.
   * **A2, THE AIM POSE** - PORTED 2026-09-11 after *"They shoot but without
     a shooting animation (their weapon is not aiming at me)"*. The fire
     gate's TARGET arm (`sub_47C2A0`, 0x0047C820 on) computes his aim every
     tick it runs: the target node minus his `Buste` (actor +20), jittered on
     the fired tick by `r/4 - rand() % int(r/2)`; the yaw `acos` of the flat
     cosine against `(0, 0, -1)` turned by +420, NEGATED when `fx*dz - fz*dx`
     exceeds `flt_4BCAEC` (0.0 - the assembly's `test ah, 41h`, which the
     decompiler lost); the pitch `-atan2(dy, 2|d|)`; released, both 0. They
     reach `sub_471950` through `sub_434C30` exactly as the player's do, over
     his group's type-18 clip (15 frames in braqueur.ani, `sub_434590` - a
     random one of the type) with type 17 as the stance, on the bones his
     `+84` table marks (0x4C3798 for all but type 13, whose 0x4C37E8 is not
     lifted). The sign was checked against the data too: with it, 237's hand
     settles 9 degrees off the line to the player; negated, 41 to 53.
     **What it changed, and why that is faithful**: the bolts now miss more
     in the gallery. A sweep trace showed every miss passing INSIDE the
     body's root sphere (5.8 to 22.6 from the hip joint, r 42.5) and no box:
     the boxes are small - HO1_FN's pelvis box ~14 x 11 x 9 around the joint
     the gunmen aim at - and the ±14 jitter threads them; the gate's spread
     also now draws its `rand()`s first, so each bolt gets different jitter.
     The engine's `sub_45ECA0` has no hidden-mesh test and the port's frames
     match its (joint origin, `+76` centre, `+92/+104` box).
   * **A3, THE FACING AND THE GUNS** - fixed 2026-09-11 after screenshots:
     *"They are not always turned to the correct position and they don't have
     any weapons in their hands"*. Neither was the aim. (1) A gunman who had
     come on through an ENTRANCE PROGRAM was drawn at the program's rest yaw
     for the whole phase, while his brain turned and aimed by its own heading
     - the viewer's body-yaw chain took `restYaw` before the brain's `+420`.
     The engine has one heading, the node's: now a gunman whose brain runs is
     drawn at it, and the brain is seeded from the yaw he is drawn at, so he
     does not snap at the mode's start (77 now turns 336.9 -> 164.1, and his
     aim layer's -0.371 covers the rest). The gallery's gunmen, placed and not
     scripted, were never affected - which is why they looked right. (2) His
     GUN was never drawn: the port drew the player's only. Now each gunman's
     held object rides his `Maing` as the player's does (`sub_41C490`'s link,
     `tir` left out), from his hand as drawn last frame. And where the gun
     points is measured, not assumed: the barrel (hand to `tir`) against the
     line to the player, -0.2 / -3.9 / -4.2 degrees at each gunman's second
     shot; with the yaw negated, 12 to 13 off. Labelled: a gunman's FIRST bolt
     leaves an arm not yet bent - the port's `meshAt` is last frame's, where
     the engine bends the arm in the same gate call before the shot.
   * **A4, THE FALL** - fixed 2026-09-11 after screenshots: a dead robber
     *"float in the air"*, lying flat at waist height. `sub_421770` moves the
     node by the picked clip's root delta every tick, the vertical included,
     and the death clips carry the fall there - braqueur.ani's root keys sum
     32.9 to 39.9 DOWN (a standing pelvis is ~42 above the feet) and slide 28
     to 110 across. The viewer seated the body once on the upright first frame
     and added no root motion for a shoot clip, so the body rotated flat about a
     pelvis that stayed up. Now a death clip's root motion, summed from frame 1
     (`pedRootDelta`) and turned by his heading, joins the placement and holds
     at the clip's end: the gallery's 240 falls 35.6 and slides 101.7, his
     pelvis left 6.3 above the floor. ~~Labelled: `sub_421140`'s wall test (a
     body may slide into a wall the engine would stop it at).~~ **The wall test
     is PORTED** (`9880055`, `omk::shootWallTest`): one clip frame a tick through
     `sub_421140(rec, delta, 1)`, and 240's fall meets a wall on 3 of 39 ticks -
     -101.0 / 12.2 where the free sum was -101.7 / 12.7. And NOTE what the
     same reading implies for the rest: `sub_421370` applies the CURRENT clip's
     root motion the same way, and the fight clip (type 10) carries -139.7 per
     19-frame loop - walking pace. That is part of item B.
   * **B, the MOVEMENT** - large, and mostly reading. The brain's walking
     states 1, 2 and 4 are ported, with `sub_426C20`'s move decision, but
     nothing FEEDS them: `Shoot_Think` (which floor and cell he is on) is read
     and not wired; `sub_421020` and `sub_421CD0` - the second turns him to a
     grid HEADING from `sub_435C40` / `sub_435E90` of his floor and his
     `+136/+140` cell - and `sub_435900` ("am I there yet") are unread; and
     no route or nav edge reaches the brain, so there is no path-finder. The
     order: read those three and whatever fills the cell targets, then wire
     `Shoot_Think` and the grid heading, then the edges states 1/2 walk -
     with the clip root motion `sub_421370` applies (not ported in A) moving
     the body while a walk clip plays.
   * **B1, THE WALK - ported 2026-09-11** (a reader: *"continue with robbers
     walking and the wall detection"*). `sub_421370` after its frame step takes
     the current clip's root delta (`sub_434D30` -> `Anim_RootDelta(+192,
     +188)`, turned by the facing) through the wall test with TWO steps, and the
     asm at 0x421520..0x421756 gives the slides: free -> the whole delta;
     blocked -> test `{0, dz > 0 ? |d| : -|d|}` with `+420 = dz > 0 ? 180 : 0`,
     free -> x snaps to the landing cell's centre and z takes the ORIGINAL dz;
     else `+160 |= 0x100`, test `{dx > 0 ? |d| : -|d|, 0}` with `+420 = dx > 0
     ? 90 : 270`, free -> z snaps and x takes dx; else `+420 += 180` (no wrap)
     and only the vertical. `dx == dz == 0` moves nothing at all. The facing
     writes are skipped under `+160 & 0x200`. The loop wrap SETS the node to
     `(x, rec+60, z)`; the port zeroes the walk's vertical there. In play.cpp
     it is `s.walkMove`, summed into the drawn position (and the death slide
     starts from it). Measured: the gallery's gunmen walk their type-10 fight
     clip at ~7.3 a frame toward the player, 238 sliding along x at frame 23
     and 237 along z at 25; the supermarket's 77 is blocked on his first step
     at 418 and slides along z.
     **WHAT IT EXPOSES, and it is the path-finder's to fix.** The hub turns
     him toward the PLAYER (`shootAcquires`' bearing, as in the engine), his
     action's clip walks him there, and a wall slide snaps his facing to 90 /
     270 - which the hub then turns back on a type-30 clip, into the same
     wall: 77 repeats turn -> blocked -> 270 every ~16 frames and closes to 65
     units. The engine's answer is what is still missing: `sub_421CD0`'s HOLD
     (`fin.holdStill`, always false here), the grid heading of `sub_435C40`
     over the distance field, and the cell OCCUPANCY (`sub_420B80` stamps
     0x80 into his cell after the move, the brain's prologue restores the
     saved `+189` byte before it thinks - so each gunman is a wall to the
     others; here they walk through one another). Labelled in play.cpp with
     the 0x400 cell arm (`sub_47C1B0`, which freezes the clip on a bit-0x10
     cell) and the one-tick lag of the facing matrix.
     **PLAYED 2026-09-11** (*"Ok, good progress"*), two reports:
     1. *"an ennemy had a buggy animation loop and was going higher each time
        the loop restart"* - screenshots of a robber climbing to the ceiling.
        The port reset the walk's vertical only at the loop WRAP, and
        `sub_421A20` (the clip start, 41 callers) ends BOTH its arms in
        `o3de_SetNodePos(node, +244, rec+60 + d(0->1).y, +252)`: every clip
        start puts the height back. A robber whose walk a turn clip keeps
        restarting before the wrap (77: turn -> blocked -> turn every ~16
        frames) kept each cycle's 0.38-a-frame rise. Reset now at the change of
        clip (to the clip's frame-0->1 dy), the turn clip's start, the picked
        clip's end and the death; 77 ends 1200 frames at y -128, where 500
        frames had him at -140.
     2. *"the ennemies were going to the exact same position as me, like I had
        no collider"* - asked how the original handles it. **It PUSHES THE
        PLAYER, and nothing stops the robber.** In shoot mode
        `Actor_TickShoot` (0x00466840) runs the player's tick as
        `Shoot_TickPlayer` then `Actor_TickNpc`, whose `SpatialIndex_Query`
        shoves him out of every body he overlaps (`sub_45E390`, sphere against
        sphere - the street crowd's push, docs/STREET_LIFE.md 3), and a
        gunman's as `Shoot_TickNpc` then `SpatialIndex_Update`. The grid does
        not stop a robber at the player: `Shoot_TickPlayer` stamps the player's
        cell 0x80 only around `Shoot_StartTargetScripts` (the DOOR scripts,
        `sub_44A0F0` on the door table) and restores it in the same tick.
        Gunmen are walls to EACH OTHER through their own stamps (not ported).
        The port's index held only the street walkers; the gunmen are now
        registered (`Session::actorBody`) and refreshed after their tick, and
        the gallery's first push is at frame 57 (15.58 7.14 out of 237).
        Two faults found on the way, both of the reading of the model: a scene
        actor's meshes are authored hundreds of units off its origin (VIR_FN's
        first sphere at x 564.7), so the spheres are RE-HUNG from the
        model-space point that stands at the entry (pelvis x/z, feet y), the
        body being drawn turned about the pelvis; and `modelReach` took
        `meshes.front()` for the model's `+88` where the ROOT mesh is meant -
        7.0 against 44.4 for VIR_FN, which shut the reach box at 14 units. The
        gunmen take the root's now. **The PLAYER's reach has the same fault and
        is LEFT, labelled**: `playerReach` is HO1_FN's mesh 0 (7.1) where his
        root is 42.5, and correcting it moves the street crowd's confirmed
        push.
        **What it shows next**: robbers that walk at the player now SHOVE him -
        two of them moved him ~380 units across the gallery in 140 frames -
        which is the reading's consequence too. What keeps the original's
        robbers off the player is their steering (`sub_421CD0`'s hold and the
        path-finder), not the push.
     3. **PLAYED again 2026-09-11**: the climb is gone (*"I didn't see a robber
        rising off the floor"* - CONFIRMED), but *"they kept pushing me to the
        point I finished outside the environnement"*. Half expected (the
        steering), and half a PORT fault: `PlayerController::nudge` placed the
        push with the walker's `moveTo`, a teleport that tests nothing, where
        the engine's `Actor_ApplyMotion` - right after `Actor_TickNpc` adds the
        push to +244..252 - takes everything since the last SAFE position
        (+232..240), push included, undoes it and hands it to `Actor_Move`, the
        collide-and-slide, and puts him back at the safe position if no floor
        is under him. So a push never crosses a wall in the original. `nudge`
        now sweeps it through `Walker::step` (LABELLED: the engine sweeps the
        push and the frame's own motion as one delta, this sweeps the push
        first). Measured: in the gallery at yaw 215 a push of -0.62 -7.83 meets
        a wall at 104 and slides -0.59 -2.44; at yaw 258 the gunmen PIN him to
        a wall at x 5290.8. Still seen and labelled: one push of 65.9 in a
        single frame (240, frame 130 at yaw 258) - the per-mesh sphere list is
        this port's reading of the model's `+244` list (docs/STREET_LIFE.md 3),
        whose own four 10.9 spheres would overlap far less.
   * **B3, THE STEERING - ported 2026-09-11** (a reader: *"continue to the
     robbers' steering"*). `sub_421CD0` (05_sys.c 3177) read whole: the hub's
     middle arm asks `sub_435C40` for the path field's downhill heading at his
     floor and cell; none (-1) -> false, and the hub turns him at the target as
     before; one -> +160 |= 0x200 (which also keeps the wall slides off his
     facing) and he is TURNED ALONG THE FIELD - within 5 degrees a snap, under
     90 a sixth of the gap a frame, 90..160 a type-31 / type-30 clip, 160 and
     over a discarded `rand()` and type 32 - and it returns true, the hub's
     "hold". The field is `sub_436260` / `sub_436350` (the BFS already read,
     ported exactly: two grids, the finished one read, 100 pops a tick over
     the engine's 1024-pair ring, distances saturating at 254) seeded at the
     PLAYER's cell by `Shoot_TickPlayer` - at his PELVIS (+244..252), since
     at his feet the supermarket's player stands on its grid's upper bound
     and `floorAt` finds nothing. The heading reads the field with the
     ROBBER's floor's dimensions, as the engine does, and draws `rand() & 1`
     only on the x+1 tie. The OCCUPANCY is ported with it: each gunman's cell
     stamped 0x80 after his tick (`sub_420B80`, `sub_421770`'s tail) and the
     saved byte (+189) put back at his prologue (`sub_424DE0` 5456), so the
     field and every other gunman's wall test route around him.
     `omk::ShootField`, `omk::shootGridTurn`, `Map2d::setCell`;
     `verify.py: shoot fire` (`path field:`, `grid turn:`).
     **Measured**: the gallery's field runs dry 5 ticks after its seed at the
     player's cell (17,21); 238 steers from (22,36) at heading 45, 240 from
     (25,31). **LABELLED**: the player's cell is `cellAt` on the floor
     `floorAt` finds (the engine's `Shoot_Think`, and `sub_4368E0` when that
     refuses - unread); a gunman's cell is taken the first tick his drawn
     position lands on the grid, and until then he gets no heading (the
     engine's is written at `Shoot_ActorEnter`); both grids start 0xFF; the
     door arm `sub_47C1B0` and the byte-1 memo at rec+72/76 are not ported.
     **WHAT IT DOES NOT CHANGE, and it is the next question**: a robber who
     has the player in sight and in range never reaches the middle arm - the
     hub's FIRST arm turns him at the target and fires - so he still walks
     straight at the player on his action's clip (the supermarket's 77 never
     steers). The grid steers only those who cannot shoot.
     **WHAT STOPS A ROBBER WITH A CLEAR SHOT - read 2026-09-11, NOT PORTED,
     the next step.** `sub_424DE0` puts him on ACTION 0: the hub's timer tail
     (05_sys.c 5997: `+168 -= dt; if (<= 0) Shoot_ActorAction(him, 0, 0)`) and
     the epilogue's fire / target arms (5846, 6111, 6133, 6172: `if (+144 ==
     8) flags &= ~0x20; else if (!(flags & 0x4000)) Shoot_ActorAction(him, 0,
     0)`) all ask for action 0 - so a robber who fires is taken off his walking
     action. The port misread this twice: `ShootFrameIn::defaultClipType` is
     documented as "`a2`, the action the caller asked for" and play.cpp passes
     the scene's action (3, the walking fight clip) - but `sub_424DE0`'s `a2`
     is the ACTOR and the action is the literal 0; and `out.clipType` is never
     applied at all (`Shoot_ActorAction`, 05_sys.c 3938, 363 lines, unread past
     its head: with flag 8 up it only PARKS the request at +152/+132 for when
     the picked clip ends, `sub_424DE0` 5497). What action 0's clip is has not
     been checked against the action rows.
   * **B4, THE ACTIONS - ported 2026-09-11** (a reader: *"ok, continue"*).
     `Shoot_ActorAction` (05_sys.c 3938) read whole and ported as
     `omk::shootActorAction`: under flag 8 the request is PARKED at +152/+132
     (flag 0x8000000) and applied when the picked clip ends (`sub_424DE0`
     5497); otherwise the a3 path, +148 written (the action, or -1), 0x200000
     cleared, and the switch - 0 / 5 type 11 or 25 (+88 counts down; at 0 a
     `rand() & 1`, else flags 0x19 pick the 25) into state 3; 2 / 3 type 10
     (else 9) into the hub, +168 = 30 * property 31; 4 state 7 on property
     23; 6, 7 (state 14), 8 (type 11, state 15), 9 (state 7, property 25, and
     turned 180), 10 (type 25, state 15). NOT PORTED: 1, the patrol - no
     routes - so the gallery's 240, whose scene action is 1, gets no clip and
     goes straight to action 0. The brain's requests are now
     `ShootStep::actionRequest` (hub finishing, hub timer, state 7), and the
     misread `defaultClipType` is documented for what it is (LABEL_177's clip
     pick with the ACTOR's index). A gunman's current clip is the type his
     last action started - re-started at 1.0 on every request, the same clip
     too. `verify.py: shoot fire` (`actor action:`).
     **Measured**: the gallery's 237 and 238 enter on action 3 with 450
     frames of advance (property 31 = 15 s) and switch to action 0 at 478 and
     514 - the count runs only on hub ticks - after which nothing pushes the
     player; they CROUCH mostly (type 25, whose first key drops the pelvis
     22.23 - flag 0x10, close and in sight, picks it) and stand on the coin
     every tenth request (type 11). The supermarket's 77 enters with 750
     frames (25 s) and does not reach the switch in 1200: his turn -> blocked
     -> turn loop keeps his brain on turn clips, and the count does not run
     there. **NOTE THE LOOP**, which is the engine's own: `sub_426E00` (the
     port's `shootEngage`) writes state 6 whenever the target is in range and
     sight, so a state-3 gunman goes back to the hub, finds his timer spent
     and asks for action 0 again - and each request restarts his clip. The
     port runs `shootEngage` before every step where the engine calls it from
     inside the arms, so it bounces every tick where the engine bounces every
     other; either way he holds the stance and fires.
   * **B5, THE PUSH'S OWN SPHERES - ported 2026-09-11, CONFIRMED IN PLAY the
     same day** (*"I didn't get stuck anymore"*) (played before it: *"the robbers
     continue to push and can stuck me in places I can't get out (probably
     because of colliders being too close from each other), but they can't
     push me outside the env"* - the second half CONFIRMED IN PLAY). The push
     had used `collisionSpheresOf`, the per-mesh bounding spheres - a labelled
     reading from the days when the model's list "was not traced back to a
     writer" - and its root sphere alone is 42.5 (HO1_FN) to 44.4 (VIR_FN):
     every body pushed like a ball ~90 units across. Read now: `sub_45E390`
     takes its list through `**(node + 40)` - count at +244, records at +248 -
     for the entry AND the querying body, and `sub_45E690` the same for an
     instance (19_dsound.c 5285 / 5434): the very list `Actor_Move` sweeps
     with and `modelSweepSpheres` already reads from the file. Four of radius
     10.91 (HO1_FN, BRM_FN) / 11.56 (VIR_FN, BRA_FN) up the body, around the
     node - and not off at the meshes' authored origin. `omk::pushSpheresOf`
     hangs it from the feet, as the walker's sweep does (Kay'l's lowest bottom
     41.81 against his pelvis-to-feet 41.8), so every push position stays the
     floor point it was; `Session::modelSpheres` and the player's own list
     take it, the per-mesh spheres only when a file has none. Measured: the
     gallery's pushes fall to one, 4.60 3.87 from 237 at 61 where 237, 238 and
     240 all pushed (8.72 -0.70 the first); `engine: crowd push` unchanged.
     LABELLED: the feet-hung anchor stands in for the node's own height; the
     reach box keeps the root radius (gunmen) and `meshes.front()` (the player
     and the walkers). **A GUARD LOST, and owed**: the swept push (B2,
     `154139d`) was asserted by `engine: shoot hit`'s route, where a push met a
     wall at frame 104; with people-sized bodies no scripted fight here shoves
     him into a wall (a stand against the gallery's east wall drew no robber
     in 630 frames), so the assertion went with the route. The fix itself
     stands - shown to fail by mutation at `154139d`, CONFIRMED IN PLAY - and
     what re-establishes its check is a probe of `PlayerController::nudge`
     against a wall (a `--nudge` step in `player_probe`, say).
     **THE PATH-FINDER, read 2026-09-11: a distance field toward the PLAYER.**
     `sub_436260(floor, x, z)` (10_dsound.c 1234) seeds a breadth-first search
     at a cell: two byte grids at `dword_52BA40[2]`, the back one cleared to
     0xFF with the seed cell 0, a ring-buffer queue of (x, z) byte pairs at
     `byte_52BA60`, and `dword_52C3F0` flipped to the OTHER grid - the last
     finished field, which is what everyone reads. `sub_436350` expands it: four
     neighbours per popped cell, a neighbour entered only if its floor byte is
     NOT in {0, 2, 3, -128} - the same refusal set the walking state tests -
     and only to lower its distance; it returns true when the search is done.
     `Shoot_TickPlayer` (05_sys.c 7519) restarts it at the PLAYER's cell when
     his floor changes or `dword_907DCC` is set, runs a step every tick, and
     restarts it again the moment one completes - so the field follows him
     continuously. `sub_435C40(floor, x, z)` - behind `sub_421CD0` - reads it:
     the neighbour with the smallest distance (ties by `rand() & 1`) as a
     heading 0 / 90 / 180 / 270, -1 unreachable; `sub_435E90` is the 204-line
     variant with an out-parameter (`rec+104`), unread. `sub_435900` is "am I
     there": within one cell size of the node on x and z. `sub_421020` picks,
     among up to four actor ids at `actor+112`, the nearest in property 21's
     range - not yet placed. Still unread: the EDGE at the record's `+4` that
     states 1/2 walk (`from`/`to` floats at +4..+24) and who hands it over. Shown to fail: the player's body left out of the
   sweep, the killing hit writing the gauge, and the aim put back at the feet
   each turn `engine: shoot gunfire` red.
5. ~~**THE MOUSE LOOK.**~~ **PORTED 2026-09-10, CONFIRMED IN PLAY** (*"Good"*). `sub_47D370` read whole:
   YAW `+420 -= row23 * 0.01 * dx`, no frame delta; PITCH `+= row24 * 0.01 *
   dy * delta`, dy negated unless row 25 ("Souris inversée") is set, clamped
   at ±45 (0x4BCB34 / 0x4BCB38) and handed to the camera as radians by
   `sub_47C260`. Its one caller is `sub_45D1D0`, reached by the mouse's own
   arm - 0x4A8278, the cursor's offset from the window centre, re-centred
   every frame, or 0x4A82AA, the DirectInput deltas - and by four special
   moves: MDRG / MDRD (`dx` ∓50) and MDLUP / MDLDO (`dy` ∓25, *Regarder
   En-Haut / En-Bas*, now wired). Rows 23-25 come from the save header
   (defaults 20, 15, off) or the ini's MouseSensX / MouseSensY; the turn keys
   share row 23. `actor/shootmove.h` `shootPitchStep`; `verify.py: shoot
   fire` (`look:`). **Kept as the port's, labelled**: the pitch's absolute
   SIGN - this viewer's camera, anchored on the reader's confirmed sense at
   row 25 off - and `--invert-x`, since the engine has no yaw invert;
   `--invert-y` now flips row 25. Not reached: the arm that zeroes the pitch
   on mover flag 0x1000 (no traced writer) and the gate `dword_4E9728` (set
   by 0x4ADE40, unread).
5b. ~~**MOVING IN FIRST PERSON — asked for 2026-09-10**~~ (*"don't forget the
   integration of moving while in fps mode"*) **PORTED 2026-09-10,
   `actor/shootmove.h`, CONFIRMED IN PLAY the same day in the supermarket
   (*"ok, the move is good"*: 34 runs, 1555 units, walls and bodies
   stopping the step).** The supermarket session's log ended `walked 0.0
   over 1246 ticks` while its keys queued MDAV 20 times, MDDG 16, MDDD 13 and
   MDAR 6: the moves arrived and nothing answered them. What was read:
   * **group 200 has NO walk clip.** Every movement entry is `clip -1` and
     queues a special move - Avancer MDAV, Reculer MDAR, the two *Glisser*
     MDDG / MDDD, the two *Tourner* MDRG / MDRD, crouch MDDO / MDUP;
   * **the handlers are 16-byte stubs** at 0x0046B630.. with no `proc` label,
     read from the BYTES: MDAV / MDAR are `sub_47CFC0(1 / 0)`, MDDG / MDDD
     `sub_47CF70(0 / 1)`, MDRG / MDRD `sub_45D1D0(0, -/+50, 0)` =
     `sub_47D370`, the mouse's own turn; MDDO / MDUP set and clear flag 0x20
     through `sub_47CF50` / `sub_47CF40`. They only raise INTENTS in the block
     at `dword_6579B0`;
   * **`sub_47D4D0` is the mover**, called by `Actor_TickShoot` before the
     channel tick: a forward and a side velocity accelerate while their
     intent is up and brake toward 0 when it is not (the side at twice the
     rate), clamp at the top speed with their sign - the assembly's `fchs`,
     which the decompiler lost - are turned by LAST frame's facing, and go to
     `o3de_MoveNodeBy` and into +244/+252, the motion `Actor_ApplyMotion` tries.
     The intents are one-shot (`flags &= 0x17F0`);
   * **the speeds are the actor's SPEED** (property 3, record +158) through the
     30-row table at 0x004CF7D0, with an INTEGER division in the top speed.
     Kay'l's is 70: 10.4 a frame, 0.39 to get there, 2.08 to stop.
   Ported as intents -> mover -> `PlayerController::addShootMotion`, which the
   tick adds to the step the walker tries, so the walls and the bodies stop him.
   Headless in the gallery (`verify.py: engine: shoot move`): 40 frames of
   Avancer go 303.29 along +Z, 30 of *Glisser a droite* 256.10 along +X, nine
   of *Tourner a gauche* turn him 90 degrees, and 20 more of Avancer go 92.82
   along -X - every number summed by hand first, and every asked distance gone
   in full. NOT modelled, labelled: the head bob and footsteps (which also hold
   the stance at frame 1 while he stands), the shove `sub_47D1F0`, the jump in
   shoot mode (MDJP), the HEAD mode's key; MDCO (run) and MDTR are ported and
   queued by no H1Avnt entry.
5c. **GAMEPLAY EVENTS ARE NOT CUTSCENES — reported 2026-09-10** (*"be careful,
   some events (like some ennemie appearing with a special animation) are
   considered as cutscenes, stops move and change camera, even though they are
   just gameplay events"*). What the engine does, read:
   * **the input** - only `player.anim.hold` (op 104, `Actor_HoldAnimation`'s
     0x81) blocks the player's input. `Actor_TickShoot` runs the mover
     (`sub_47D4D0`), `Shoot_TickPlayer` and the channel for the player every
     frame, gated on nothing but ACTOR_STATE 3 - no parked script, no running
     program and no camera mode stops him;
   * **the camera** - `camera.set` (op 95) is `Camera_Request(12)` with the
     CAMERAS record (its second operand the travel, its third read and
     DISCARDED), and every `scx.play*` requests mode 13 only when
     `ScriptObject_HasCamEditing` says the object has a chunk-10 editing
     linked (95 of 4511 objects). `Camera_RequestChanged` refuses a change in
     ONE case: the player in ACTOR_STATE 2 (melee) and a mode other than 14.
     Shoot mode is not protected, so a scripted `camera.set` does take the
     view there - SCENE 62's ladders (`shoot.player.suspend`, `camera.set
     .at_address`, `shoot.player.resume`) are built on that;
   * **the data** - shoot.SCX has four editings: `e1` on `_1reSceneKayl` (the
     opening, before the mode), `med2` on `5MeditekRoom1`, and two linked to
     nothing. None is on a SCENE 56 entrance: each entrance is
     `scx.play.actor.wait N` then `shoot.actor.enter N`, no camera, no hold.
   **The port's divergence**: its adventure gate also drops on
   `sc.activeEditing()` and on `session.parkedOnProgram()` - a script parked
   on ANY body-driving program, which an enemy's `scx.play.actor.wait` is.
   The engine's `Actor_TickShoot` has neither gate. Instrumented 2026-09-10:
   in shoot mode the viewer prints `adventure ON/OFF in shoot mode - <terms>`
   whenever the gate flips, so a freeze met in play names its cause.
   **FOUND AND FIXED the same day - the log line did its job - and CONFIRMED
   IN PLAY** (*"Ok, this event issue is fixed"*: twelve entrances, no stop).
   The reader's next supermarket session printed `adventure OFF in shoot mode -
   parkedOnProgram` five times, 20-46 frames each, and every one was an
   ENTRANCE: actor 86's `BRA_05A2` (47 frames) at 717, actor 85's `BRA_04A1M`
   (26) at 929, and so on - their zone scripts parked on programs bound to a
   NAMED actor (ops 59/60, `how` "actor"). In shoot mode the gate now counts
   only a program bound to the PLAYER (ops 46/90,
   `Session::parkedOnPlayerProgram`); outside it the wider test stands,
   labelled, since the hand-overs it was written for (omk-play 78) were not
   re-read. `verify.py: engine: shoot entrance` walks the reader's own route
   into zone 12 (id 3922, (13117, 1818)) and asserts the walk carries through
   actor 86's entrance with the gate never dropping. The camera half of the
   report went with it: the first-person camera is applied under the same
   gate.
5d. **THE PHASE COULD NOT BE FINISHED — reported 2026-09-10, FIXED the same
   day, CONFIRMED IN PLAY** (*"The event is triggered, and the ending
   cutscene is triggered"*: nineteen deaths reported, actor 84's at 3357,
   zone 3931's script two frames later) (*"i can't finish the supermarket shoot sequence, even when every
   ennemies are killed, the end cutscene is not triggered (look at how events
   are managed in shoot sequence)"*). How the supermarket ends, read out of
   SCENE 56 and the brain:
   * **the score is a MESSAGE.** `Shoot_TickNpc` calls a gunman's brain even
     when he is dead, and the generic brain's first arm (`sub_424DE0`, flag 8
     up) plays his reaction clip through `sub_421770`; the frame it has played
     out, `sub_421770` clears flag 8 and returns 0, and with his health at 0
     the brain posts `Game_RaiseEvent(43, {3, him})` - ONCE, since the next
     tick takes the other dead arm (ACTOR_STATE 0, no post). The Gandhar and
     Astaroth callbacks post the same message 3 from their own prologues;
   * **SCENE 56 subscribes it** (table entry 1, offset 0x433F): `param[0]` -
     the dead man's actor id, which `Message_RunHandlers` maps from his slot -
     is matched against 37, 38, 39, 82 and 84, each setting its `Braqueur N
     Dead` (VARIABLES 640..644), and for 84 - *Braqueur 15* - also `zone.enable
     3931`, a random victory track (`music.play 65..70`);
   * **zone 3931 ends it.** Its enter script (record 21) tests `Braqueur 15
     Dead`, then `shoot.end 1`, releases and hides every gunman, and plays the
     ending: `player.anim.hold`, cameras 4372 and 4373, `Supermarché Joué`.
   The port stopped ticking a killed gunman's brain (`+160 & 8`), so message 3
   was never posted and zone 3931 never opened. Now the viewer posts it once
   when a gunman's death clip reaches its last frame. `verify.py: session
   probe` loads AREA 230 + SCENE 56 and posts {3, 37} then {3, 84}: the handler
   is the scene's at 17215, var 640 goes 0 -> 1, var 644 0 -> 1, zone 3931 from
   not live to live. To finish the phase in play: kill actor 84, then walk into
   zone 3931 - centre printed by `zone_quads <data> 56 scene`. NOT modelled:
   the zone's enter script only matters once he is in it, and the gunmen still
   do not walk or shoot, so 84 must be reached on foot.
5e. **THE RETURN FROM THE MODE — reported 2026-09-10, FIXED the same day,
   CONFIRMED IN PLAY** (*"ok, good"*)
   (*"the return to adventure mode (after the cutscene) is buggy: invisble
   character, impossible to move, weird camera"*). The session's own last line:
   ACTOR_STATE 1 on `.CTL` state 125 `S_STAND` - group 200, the shoot stance -
   and `walked 0.0`. `Shoot_Leave` (0x00422730), read to its end: library back
   to `aventure.scx`; the player's +404 = 1 and `Anim_BindToHierarchy(node,
   +168)`; every gunman in state 3 to 0; the records freed; `sub_436D20`
   (SHOW him); **`SetPersoBankGroup(bank, Cef_DefaultGroup(+180))`**;
   `sub_47CE70` (the mover off); `sub_41C540(player, 1)` when he holds
   something; `Input_InstallScheme(0)` - and **no camera request**: the view
   comes back through the script's next `camera.set`. The port missed two:
   * the default group - `leaveShootMode` set the state and left the machine
     on group 200, whose move keys queue the shoot mover's moves with no mover
     to answer. Now it installs `defaultGroup()`, as the constructor does;
   * the camera - the mode wrote preset row 4's offsets (the eye ON him) into
     the controller, and the viewer re-applies a world camera's offsets only
     when a DIFFERENT id is named; the ending names camera 0, already applied
     before the phase, so the eye stayed inside him - the weird camera and the
     body that looked gone. Leaving now forgets which one was applied.
   A labelled difference: the viewer re-applies the current world camera the
   frame after the leave even when no script names one (the gallery harness);
   the engine would stay in mode 4 until a `camera.set`. The supermarket's
   ending names its own cameras, so it plays the same. `--shoot-end N` is the
   harness's way out (op 81's `shoot.end 1` at frame N); `verify.py: engine:
   shoot leave` enters, leaves at 60 and walks: camera 0 on 61, `H_STAND` on
   group 0 at the end, 160 units walked.
6. **THE SWEEP.** `--slow`, owed.

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
