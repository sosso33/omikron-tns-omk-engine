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
| 5 | **the brains, decision revisited** — with the grid in hand, how much of the generic shooter's 16 states is now fact rather than geometry. Gandhar is already exact; Astaroth and the generic are state graphs. **Only what the grid settles gets wired**; the rest stays labelled | |
| 6 | docs, the checks, and a play test | |

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

* **line of sight.** The grid may or may not answer it — the wall segments in
  each floor are a candidate and nothing has been traced to them yet.
* **the weapon's range.** `Shoot_InitWeapon` is READ (§4b, §5b) and its two
  floats are now half settled: **`f0` is the RATE**, traced, and `f1` has no
  reader anywhere in the shoot sources. The range is still unread.
* ~~**what a shot is.**~~ **ANSWERED, §4c**: a projectile out of a 256-slot,
  60-byte pool, fired by `Actor_TickProjectiles` on a per-slot timer and paid
  for out of property 34. What is still unread there is the AIM
  (`sub_44D7F0`) and what starts the timer.

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
either. So `f1` is *probably* the range by elimination, and it is not written
down as one.

> **A TRAP WORTH THE LINE: `+180` is two different things.** On an ACTOR it is
> the bank (`Cef_FindGroupById(u32(actor, 180), 200)`, four sites); on a SHOOT
> RECORD it is the weapon row. Same offset, different structs, and a grep for
> `, 180)` returns both mixed together.

**What this does to the decision.** `standing-unknowns` §2 rested on three
unknowns. The navigation node closed at step 2; the RATE closes here; the
RANGE and the LINE OF SIGHT do not. So the grid answers where a gunman may
stand and which doors he may cross (§4d, §4e) and it does not answer where he
may aim - which is the half the generic brain's branch needs. **The decision
stands**, on one and a half unknowns rather than three, and the next reading
is `f1`'s consumer or `sub_44D7F0`'s aim from §4c, which are likely the same
question from two ends.

Until those three are read, the decision of `standing-unknowns` §2 stands for
the generic arm. What has changed is that it is now a question with an
answer in the tree rather than a gap outside it.
