# The game state — what the game remembers

Start at [`../CLAUDE.md`](../CLAUDE.md) for the working practice these findings
depend on, and [`RECONSTRUCTION.md`](RECONSTRUCTION.md) for where this sits in
the plan: it is **phase 6 stage 2**, the schema everything else in the
simulator is written against.

The question this answers is narrow and it decides the shape of a replica:
*which bytes carry the game forward?* Not what is on screen, not what is
loaded — what survives an area change, a reincarnation and a save.

The answer is one allocation.

```c
sub_408E00 (0x00408E00):  Mem_Calloc(0x2000, 1)    /* 8192 bytes, once */
```

Everything else — the resident area and scene blocks, the 100 actor records,
the script contexts, the loaded meshes — is rebuilt from the archives on
demand. Two globals sit beside the block and are saved with it (the clock,
§6). Nothing else is persistent.

---

## 1. Where the block comes from

```
IAM\START ────► the 8192-byte game DB ────► State_Apply ────► playing
   5686 bytes       g_GameDB (0x4E6D94)      0x0040DB00

IAM\GAMES slot ──►      the same 8192      ──►     ditto
```

`Game_NewGame` (0x0040E060) allocates the block zeroed, copies `IAM\START` over
the front of it, and hands it to `State_Apply`. `Game_LoadSave` (0x00408FC0)
does exactly the same with a save slot's copy. **`IAM\START` is not a container
and not a script file** — it is the new-game save, byte for byte the same
structure a save slot holds. (The earlier reading of it as a script archive is
the third row of `CLAUDE.md` §1's table.)

The file is 5686 bytes and the live block 8192: everything past the file is
zero and stays zero. The save writes all 8192 back.

## 2. The layout

Offsets are from the start of the block. The walk below is
`tools/gamestate.py --raw`, and it **lands exactly on the file size**.

```
   +0    u32    version                      103
   +4    u32    stamp                        19991004 - the build date
   +8    i32 x6 offsets of the six arrays    file-relative; State_Apply
                                             turns them into pointers
   +32   u16 x6 their entry counts           694 259 670 1032 791 4558
   +44   i32 x3 the player's position        x, y, z  (raw units, §5)
   +56   i32    the player's facing          0..4095
   +60   ...    the player's character record  276 bytes - §4
   +336  char[256]  bio string 0             what the record's +0 points at
   +592  char[256]  bio string 1             ... and its +4
   +848  i16 x18    object list 0            what the player is carrying
   +884  i16 x256   object list 1            the second list - §3
   +1396 i16 x9     object list 2            the memo journal
   +1414 i16    the current area
   +1416 i16    the current scene            -1 for none
   +1418        (2 bytes of alignment)
   +1420 i32 x694    the variables           VARIABLES.TAG names them
   +4196 i16 x259    scene resident per area
   +4716 2 bits x670   prop state
   +4884 1 bit x1032   character/object shown
   +5016 1 bit x791    address enabled
   +5116 1 bit x4558   zone state
   =5686 end of file
```

Every one of the fixed offsets is `State_Apply`'s own arithmetic, read out of
`Runtime.exe.asm` rather than inferred:

```asm
lea     ecx, [eax+3Ch]      ; dword_69BC6C = the player's record, +60
lea     edx, [eax+150h]     ; and its two bio pointers, +336 ...
mov     [ecx], edx
lea     edx, [eax+250h]     ; ... and +592
mov     [ecx+4], edx
add     eax, 350h           ; Src        = object list 0, +848
add     eax, 24h            ; +36  = 18 x i16
mov     dword_69BD70, eax   ;            list 1, +884
add     eax, 200h           ; +512 = 256 x i16
mov     dword_69BD7C, eax   ;            list 2, +1396
push    12h  / 100h / 9     ; the three capacities: 18, 256, 9
movsx   edx, word [eax+586h]  ; +1414 the area
movsx   ecx, word [eax+588h]  ; +1416 the scene
lea     ebp, [ecx+2Ch]        ; +44   the position
```

### The six arrays, and how they are checked

Their offsets are stored; their **lengths** are not read by the runtime at all,
so the six `u16` counts at `+32` are a data-side identification. What makes it
safe is that all six agree with numbers established elsewhere, from six
independent sources, and that the arrays then tile the file with no slack:

| # | ptr | count | entry | count | corroborated by | span |
|---|---|---|---|---|---|---|
| 0 | +8 | +32 | i32 | 694 | `VARIABLES.TAG`'s highest index is **693** | 2776 |
| 1 | +12 | +34 | i16 | 259 | `AREAS.TAG`'s highest index is **258** | 518 |
| 2 | +16 | +36 | 2 bits | 670 | the prop table's **670** dense state indices | 168 |
| 3 | +20 | +38 | 1 bit | 1032 | the object table's **830 AREA + 202 SCENE** records | 129 |
| 4 | +24 | +40 | 1 bit | 791 | `ADDRESSES.TAG` holds **791** entries | 99 |
| 5 | +28 | +42 | 1 bit | 4558 | the **4558** trigger zones | 570 |

Each array starts on a 4-byte boundary, so the spans plus 8 bytes of padding
reach 5686 = the file size. And a second, sharper test the data could fail:
**no bit is set past a declared count**. If array 5 were really longer than
4558 zones, or array 4 shorter than 791 addresses, the two spare bits in the
last byte of one of them would have to carry something; 0 of the 3 spare bits
across the three one-bit maps is set. `verify.py: game state`.

### The accessors, which is what fixes each array's meaning

Not one of these is guessed from the contents:

| array | function | what it does |
|---|---|---|
| variables | `Var_Get` / `Var_Set` 0x0040E510 | `u32(ptr + 4 * i)` — about twenty VM opcodes |
| scene per area | `Area_GetLoadedScene` 0x0040B140 | `i16(ptr + 2 * area)`; `scene.load` / `scene.unload` write it |
| prop state | `ObjectState_Get` / `_Set` 0x0040B010 | `(byte[i/4] >> 2*(i%4)) & 3` |
| object shown | `State_SetBit` 0x0040AF30 | one bit; `Scene_LoadProps` replays `object.show` from it |
| address enabled | `Address_SetEnabled` 0x0040B090 | one bit; VM ops 87/88 `address.enable`/`.disable` |
| zone state | `Zone_StateBit` 0x0040D500 | one bit, index masked `& 0x7FFF` |

**`ObjectState_Get` sign-extends.** The byte is loaded `movsx`, the mask
`3 << 2*(i % 4)` is built in a byte register and sign-extended as well (0xC0 at
`i % 4 == 3`), and the result is `sar`'d — so for indices ≡ 3 (mod 4) state 3
reads back as **−1** and state 2 as **−2**. Nothing in the game can see it: op
68, op 76 and `Scene_LoadProps` all mask the low byte (`test al, 1`,
`or al, 2`). It is recorded because a replica that "corrects" it and a replica
that keeps it are indistinguishable until something compares the value rather
than masking it.

### Which VM opcode writes which array

**read from the handlers**, 2026-09-02: every handler block in
`clean/_vmhandlers.json` scanned for a call to each accessor above, with op 120
read from the raw assembly because its block is the function after its handler.
Counts are over the 5785 world-script slots (`IAM\AREA` + `IAM\SCENE` +
`IAM\GLOBAL`), the convention the rest of `docs/` uses — see SCRIPT_VM's note
on site counts, because the review in `todo/iam-script-engine.md` counts a
smaller corpus and quotes lower figures.

| array | opcodes that write it | sites |
|---|---|---|
| **variables** `+1420` | 12 `set.var` · 13 `set.var2` · 14 `set.var.i8` · 15 `set.var.i16` · 16 `set.var.i32` · 17 `set.var.var` · 18 `set.var.pop` · 19 `var.add` · 20 `var.sub` · 21 `var.mul` · 22 `var.div` · 23 `var.and` · 24 `var.or` | 713 · 705 · 217 · 9 · **0** · 150 · 73 · 354 · 403 · **0** · 74 · **0** · **0** |
| | 49 `var.set.has_object` · 75 `var.set.used_object` (both through `Script_StoreVar` 0x00401AE0) | 222 · 235 |
| | 86 `var.set.actor_stat` · 91 `var.set.player_id` · 115 `var.set.timer` · 120 `var.set.random` · 128 `inventory.transfer` | 459 · 63 · 5 · 255 · 3 |
| **scene per area** `+4196` | 71 `scene.load` · 72 `scene.unload` (`Area_SetLoadedScene`) | 78 · 82 |
| **prop state** `+4716` | 68 `object.release` · 76 `object.show` · 77 `object.hide` (`ObjectState_Set`) | 226 · 249 · 183 |
| **object shown** `+4884` | 78 `character.show` · 79 `character.hide` (`State_SetBit`) | 1256 · 1980 |
| **address enabled** `+5016` | 87 `address.enable` · 88 `address.disable` (`Address_SetEnabled`) | 41 · 22 |
| **zone state** `+5116` | 64 `zone.enable` · 65 `zone.disable` (`Zone_SetStateBit`, each followed by `Zones_RegisterAll`) | 1510 · 1969 |
| **object lists** `+848/+884/+1396` | 50 `inventory.add` · 51 `inventory.remove` · 52 `inventory.remove_all` (49 reads them) | 472 · 78 · 210 |

Four things the table is worth reading for:

* **op 93 `actor.stat.set` is not here.** It reads a variable (`Var_Get`) and
  writes an *actor record*, which is not in the block — except for the player,
  whose 276-byte record is (§4). Its 781 sites are the largest stat channel in
  the game and every one of them lands outside the saved arrays unless the
  target is the player.
* **The `ObjectShown` bit index is the character record's `+18`**, not the
  object id: ops 78/79 resolve the actor with `Scene_FindObjectRecord`, read
  `movsx eax, [esi+12h]` and hand that to `State_SetBit`. `character.hide -1`
  writes **no** bit at all — it is `sub_41CED0(Actor_Player(), 1)`, hiding the
  player.
* **Five of these opcodes are the only writers of their array**, so a replica
  that stubs them leaves the array at whatever `IAM\START` shipped: the prop
  state, the shown bitmap, the address bitmap and the object lists have no
  other script-side route in.
* The scene-per-area entry is what makes `scene.load` survive a save: the DB
  names the scene, and the next load of that area brings it back.

Port status as of 2026-09-02: the variable writers 12–24 and the zone,
address and scene-of-area arrays are ported; 49, 75, 86, 91, 93, 115, 120, 128
and the prop/shown/object-list writers are the subject of
`todo/iam-script-engine.md` 22, 24, 28, 29, 30, 33 and 35 — 78/79's bit and
120 landed with this batch.

## 3. The object lists

`State_Apply` points three of the four lists into the block and gives them
their capacities; the fourth is a scratch list. The list is a plain array of
`OBJECTS` ids terminated by `-1`, and `ObjectList_SetCapacity` recovers the
count by scanning for that terminator — which is why the count is not stored.

| list | slots | where | what it is |
|---|---|---|---|
| 0 | 18 | +848 | **carried inventory.** `Game_HandleEvent` case 10 — picking a prop up off the floor — inserts here, and only here |
| 1 | 256 | +884 | a second list the inventory screen moves items into (case 36 action 7) and back out of (action 8); objects of kind 1, the hand weapons, are refused |
| 2 | 9 | +1396 | **the memo journal.** All **78** `inventory.add` sites naming it name an object called `Memo NNN …`, and nothing else is ever added |
| 3 | 16 | — | **the shop stock**, rebuilt from the resident area's own object array (`AREA +8`) whenever a shop screen opens. Not stored |

`verify.py: object lists` holds the memo test and the starting inventory.

### What the four opcodes do to a list

The list is a plain array with a terminator and no stored count, so every
operation has to leave that recoverable. All four are transcribed in
`engine/src/script/gamestate.cpp`; `verify.py: engine game state` runs them.

| op | function | what it does |
|---|---|---|
| 49 `var.set.has_object` | 0x0040A440 | scans the first `count` entries of the list in **field 0** for the object in **field 1**, and writes 1 or 0 into the variable in field 2 |
| 50 `inventory.add` | 0x0040A4D0 → `Inventory_Insert` 0x004098E0 → `ObjectList_InsertFront` 0x00409CB0 | inserts at the **front**, refusing a full list |
| 51 `inventory.remove` | 0x0040A5A0 | removes one copy, shifting the tail down and writing `0xFFFF` into the **last** slot |
| 52 `inventory.remove_all` | 0x0040A6A0 | the same removal repeated until the id is gone |

**The insert is at the FRONT, not the end.** `ObjectList_InsertFront` is
`memmove(base + 2, base, 2 * capacity - 2)` and then `base[0] = id`, so the
newest item is index 0 and the terminator slides up one slot; dropping the last
slot is safe because the function's first line is
`if (capacity == count) return 0`. An append is self-consistent — it keeps the
terminator scan true and every count right — and it puts the wrong item under
the inventory screen's cursor, which is the shape of error CLAUDE.md §1 warns
about.

**The duplicate refusal belongs to the list, not to the opcode.** The 50
handler scans for the id first only when its list selector is 2 or 3
(`cmp edi, 3 / cmp edi, 2`), so the memo journal and the shop stock cannot hold
two of anything and the two carried lists can.

Each list also has a **parallel 56-byte record array** (`dword_69BD68`, copied
out of `IAM\OBJECT` on insert) that the removals shift in step. It is a runtime
cache, not state: it is not in the DB, it is not saved, and a replica rebuilds
what it holds from the object table by id.

No script has reached any of this yet — the four opcodes are stubs in the port
(`todo/iam-script-engine.md` 29); the accessors under them landed 2026-09-02.

List 1 has no name in the code, and it is the one thing here worth being
careful about. What is established:

* the inventory screen moves an item from list 0 into it and back by hand —
  `Game_HandleEvent` case 36, actions 7 and 8 — refusing kind 1, the hand
  weapons, and refusing either direction when the destination is full;
* **no script ever adds to it.** `inventory.add` runs at 472 sites — 369 on
  list 0, 78 on list 2, 25 on the shop — and **0** on list 1;
* the three `inventory.transfer` sites in the game are all in
  `2-32 Prison Pamoka` — 0→1 twice on the way in, 1→0 on the way out. The
  prison confiscates what you are carrying into exactly this list;
* the scripts read it 27 times with `var.set.has_object`, always about a plot
  item (`Carte de Police Kay'l`, `Dossier Jenna`, `Bombes Tetra`, `Sort
  Désincarnation`), and 9 of the 10 sites in the whole corpus that store into
  a variable called `Inventaire 2` are reading it. That is the data's own name
  for it, offered as such and not as a finding.

A second carried list, then, at fourteen times the size — but what the fiction
calls it is not established, so it is left as "list 1".

The new game's lists are §7.

**`inventory.remove_all list, −1` does nothing, and that is the engine's.** Its
`id == −1` arm (`loc_40A7D8`; 37 shipped sites, every one `0, −1` — the
reincarnation clean-out, since the flag it tests is the OBJECT record's "lost on
reincarnation" bit copied into the cache at `+0x24`) finds the entry to remove
with `cmp word ptr [ecx], 0FFFFh` over the first `count` ids: the compiler folded
the opcode's own operand into the inlined find, so it looks for the terminator
inside the counted run and never reaches the removal. `GameState::listRemoveAll
(list, −1)` returns 0 from the other side, so the port and the game agree — by
two different routes. `verify.py: engine world ops`, `remove_all.minus1.count`.

**`inventory.transfer` (128, 0x405810)** moves the first *n* ids of one list to
the FRONT of another, one `ObjectList_InsertFront` + `ObjectList_RemoveAt(from, 0)`
at a time — so they arrive reversed — with *n* from a variable (−1 = all), clamped to
the source's count and the destination's free room, and written back to a second
variable unless that is −1; a negative *n* moves nothing and is stored as it is.
Its 3 sites are all SCENE 7, between lists 0 and 1, variable 270 on both ends.

**Port status (2026-09-02):** 49, 50, 51, 52 and 128 execute in `engine/`
(`Interpreter`, behind `setWorldWrites`), with `Inventory_Insert`'s kind gate
still unmodelled: 27 of the 473 `inventory.add` sites add seteks and 14 add
ammunition, which the engine folds into Argent or a carried gun's count instead of
a slot.

## 4. The player's character record

`+60` is a **276-byte character record**, the same structure the `AREA +56` and
`SCENE +24` tables hold — see [FILE_FORMATS 5e](FILE_FORMATS.md). It is not a
copy of one; it *is* the player, and `Actor_FindById` (0x0040B190) says so by
returning `g_GameDB + 60` before it scans anything, whenever the id asked for
matches the id at `+272` of this record.

Two consequences:

* **Reincarnation is a memcpy.** VM opcode 56 `player.become` copies 268 bytes
  of the target's record to `+68` and the two bio strings into the buffers at
  `+336` / `+592`. Everything about "who you are" then persists, because it is
  inside the saved block. Read at the handler 2026-09-02: `strcpy` of the actor
  record's two bio pointers into `+336` and `+592`, then `rep movsd` of 0x43
  dwords from the record's `+8` into `+68` — everything but the two pointers
  `State_Apply` plants. So `+272` **is** the player's actor id: `IAM\START`
  ships it as −1 and AREA 118's startup script writes 136 (`KUM_FN`) before
  anything else. Ported 2026-09-02 (`todo/iam-script-engine.md` 32).
* **The two pointer fields are runtime-only.** `State_Apply` overwrites `+60`
  and `+64` (the record's own `+0` and `+4`) with pointers to the two bio
  buffers, and the serializer does not put them back — so those 8 bytes are
  dead in a save file and hold a stale address from the session that wrote it.
  They are `0xFFFFFFFF` in `IAM\START`. This is exactly the 8 bytes the round
  trip in §8 is allowed to differ at.

Fields established here that FILE_FORMATS did not yet carry, all from
`Actor_GetProperty` (0x0040B360) and the inventory screen's item labeller:

```
+202  i16[4]  \
+210  i16[4]   |  six parallel 4-entry arrays, +202..+250. Property 0x15 looks
+218  i16[4]   |  an input value up in +226 and returns the matching +234;
+226  i16[4]   |  0x16 returns +242 instead; 0x22 returns +210<<16 | +218 and
+234  i16[4]   |  the matching +202. What they hold is not established.
+242  i16[4]  /
+250  i16[10]  ammunition, indexed by the GLOBAL weapon-table slot. Slots
               5..9 are the five guns: the inventory screen appends
               `u16(rec + 256 + 2*kind)` to a gun's label for kinds 2..6,
               which is this array at 5..9, and the fire path refuses when
               `i16(rec + 250 + 2*slot)` is 0.
+270  i16      the OBJECTS id being held, -1 for none
+272  i16      the character id
```

**Every script-side write to the player lands here, and needs no actor table.**
`Actor_FindById` returns this record before scanning a chunk whenever the id is
−1 or matches `+272`, so `actor.stat.set −1, …` (736 of 784 sites), `var.set.
actor_stat −1, …` (459 of 460), `var.set.player_id` (the `+272` itself) and the
held-object field at `+270` (DB `+330`, written by `object.hold.actor`, cleared by
both releases) are all edits to the saved block. `engine/`'s interpreter does them
on `GameState` directly; only another actor's record goes through `WorldHooks`.
And `Actor_SetProperty`'s clamps are unsigned (`cmp esi, 0C8h ; jbe`): a Vie
written as −5 reads back 200.

## 5. Position, facing, area — written once, by the serializer

`+44..+56` and `+1414`/`+1416` are **not live state**. Each is written at
exactly one site, `State_Save` (0x0040D950), and read at exactly one site,
`State_Apply`. During play the answer to "where am I" lives in the area-slot
globals (`dword_69BC48[4 * dword_69BC60]` and its scene twin) and in the
player's actor record; these fields are the snapshot taken when the game is
written out.

The two conversions, quoted exactly because a replica has to reproduce them:

```
save   raw = nearest_int(world * 0.0254 * 256)         flt_4BC050, flt_4BC054
                comparing against raw * 0.15378937     flt_4BC058
load   world = raw * 100 * 0.00390625 * 0.3937007874015748 - 1.0
                                        dbl_4BC030  dbl_4BC038  dbl_4BC040

save   raw = int(degrees * 11.37777777777778) & 0xFFF     ( = 4096 / 360 )
load   degrees = raw * 0.087890625                        ( = 360 / 4096 )
```

The scale factors are inverses — `0.15378937 == 100/(256*2.54)` — but **the
load subtracts a whole world unit that the save never added**, so a position
round trip is not the identity: it moves the player `(-1, -1, -1)` per save and
reload. The `- 1.0` is not local to this path; `Global_Load` applies the same
`x * 100 / 256 / 2.54 - 1` to the world-camera coordinates, so it is the
engine's standard raw→world conversion and it is the *save* side that is
missing its half. Recorded as observed; nothing here says it was intended.

The facing pair is a true inverse to within rounding.

## 6. The clock, and the Omikron calendar

Two globals, saved beside the block and restored with it:

| | | new game | |
|---|---|---|---|
| `dword_4C2BD4` | the day counter | **52** | `Game_NewGame` calls `sub_41E670(52)` |
| `dword_4C2BD0` | the time within the day | **2000000** | ... and `sub_41E750(2000000)` |

The two formatters at 0x0041E690 and 0x0041E6E0 spell the calendar out, and
every constant is a `dd` in the data segment:

```
41 days per month   (dword_4C2C14 = 29h)      13 months  (dword_4C2C18 = 0Dh)
year zero 7216      (a literal in the sprintf)
3600000 units per day (dword_4C2C0C)
   / 21 hours (4C2C1C)  / 15 minutes (4C2C20)  / 33 seconds (4C2C24)

Aqed  Nadim  Andar  Xenep  Nevod  Ganevat  Osmydep
Qomivo  Taznevet  Ustanevat  Nivat  Mozkanep  Primevat
```

So the new game begins on **12 Nadim 7216 at 11:10:00**, and time advances in
`sub_41E600` by 166 units per tick with the day rolling over on the divide.

**The shipped content confirms the calendar.** Eight objects in `IAM\OBJECT`
are in-world newspapers whose names carry a date — and all eight are legal
dates in this calendar: their month is one of the thirteen names and their day
never exceeds 41, with `Omikron News - 41 Andar 7216` sitting exactly on the
month length and `37 Aqed` ruling out anything shorter. The first of them,
**`Omikron News - 11 Nadim 7216`, is dated the day before the game starts** —
the paper in Kay'l's apartment on the morning of day 52. Nothing in the code
and nothing in the object table knows about the other; they agree anyway.
`verify.py: game clock`.

**The unit is a millisecond**, derived rather than assumed: `Clock_Tick`
(0x0041E600) adds **166** units per 5 frame units — a sixth of a second at
30 fps, so 996 units a real second — `Timer_Format` divides by 1000 to get
seconds, and VM op 113 multiplies its operand by 1000. A day is 3600000 units,
which is 3600 seconds, an hour of real time.

## The script timer

*(Unnumbered on purpose: the sections after this one are cited by number.)*

Five opcodes and one read, over three globals — `g_TimerFlags`
(`dword_930768`), `g_TimerValue` (`dword_930764`) and `g_TimerStart`
(`dword_930760`). **None of them is in the 8192-byte block and none is saved**:
the timer does not survive a reload, while the clock it counts against does
(day and time travel in the save slot's own header, §8).

| op | address | jumps to | what it does |
|---|---|---|---|
| 110 | 0x00405340 | `sub_41E260` | `flags = 1` — a reset. Unnamed in the table, **0 shipped sites** |
| 111 | 0x00405350 | `loc_41E2B0` | requires `!(flags & 1)`; `flags \|= 1` — **STOP** |
| 112 | 0x00405360 | `loc_41E2D0` | requires `flags & 1`; `start = clock`; `flags &= ~0x11` — **START** |
| 113 | 0x00405370 | `Timer_SetValue` 0x0041E270 | `value = field * 1000`, refused unless stopped |
| 114 | 0x004053D0 | `Timer_SetMode` 0x0041E290 | `flags = field \| 1`, refused unless stopped |
| 115 | 0x00405420 | `Timer_Elapsed` 0x0041E430 | `Var_Set(field, elapsed)` |

**The names in `tables/vm_opcodes.json` for 111 and 112 are inverted**, and are
being corrected with this reading. The table calls 111 `timer.start` and 112
`timer.stop`; the two functions they jump to say the opposite, and neither
carries a `proc` label — they are reached only by a tail `jmp`, so they are
absent from the decompilation and have to be read at the addresses the handlers
name (CLAUDE.md §1's "nothing calls it" trap). Three things settle the
direction:

* `Timer_Elapsed` returns **0** when the flags are *exactly* 1, so bit 0 set is
  a stopped timer — and 112, which stamps `g_TimerStart = g_ClockTime` and
  clears the bit, is the one that starts it running;
* `Timer_SetValue` and `Timer_SetMode` both bail unless `flags & 1`, so the
  configure pair can only precede the start;
* every one of the 12 Tetra-bomb sites reads `timer.mode 12`, `timer.set 900`,
  **then op 112** — under the table's old names, "configure the timer, then
  stop it" — and the shooting range does `shoot.end`, **op 111**, then
  `var.set.timer` into a `TEMPS n` variable it shows with `ui.highscore`.

Sites: 13 of 111, 15 of 112, 12 each of 113 and 114 (always `900` and `12`),
5 of 115, 0 of 110.

**The flags**, from `Timer_Format` (0x0041E300) and the HUD tick
(`sub_41E480`): bit 0 halted; bit 2 count **down** (the display shows
`value - elapsed`, and `Timer_Elapsed` does **not** apply it); bit 3 draw the
`HH:MM:SS` readout; bit 4 expired — set when the clock passes `start + value`,
which also **posts message 18** (`Game_RaiseEvent(43, {18})`, so a subscribed
script is what a running-out timer actually does) and freezes the reading at
the value. Mode 12 is `countdown | visible`, and with the `| 1` the setter adds
it is the only mode the shipped scripts ever ask for.

Ported as state and accessors 2026-09-02; no opcode reaches it yet
(`todo/iam-script-engine.md` 37).

Ported 2026-09-02: ops 110–115 execute on `GameState`'s timer (111 → `timerStop`,
112 → `timerStart`), and `var.set.timer` stores `Timer_Elapsed`'s three-way read.
Op 110 has **one** shipped site over the 5958-slot corpus (AREA 59: reset, mode 12,
set 900, end).

## 7. The new game — what `IAM\START` actually says

`tools/gamestate.py`:

```
area      118  Introduction Kay'l
scene     -1
player    id 0xFFFF - no body yet
position  unset (0xFFFFFFFF)
clock     day 52 = 12 Nadim 7216, 11:10:00

list 0 carried     6 Clé Appartement Kay'l, 171 Notice Sneak
list 1 second      176 Notice Multiplan, 163 Anneaux 5
list 2 memos       empty

variables         0 of 694 non-zero
scenes resident   none
prop states       217 at 1, 453 at 3, of 670
objects shown     628 of 1032
addresses enabled 289 of 791
zones set         3834 of 4558
```

Three things worth naming:

* **The area id resolves to `Introduction Kay'l`** — `AREAS.TAG` 118. That
  single lookup is what confirms `+1414`: no other field in the block resolves
  to a sensible starting area.
* **There is no player.** The record is `0xFF`-filled, so its id reads
  `0xFFFF`, and `State_Apply` tests exactly that value and skips the entire
  spawn path — no model load, no bank list, no `Player_SetActor`. A new game
  begins with no player actor; the opening casts one.
* **Every variable is zero.** The whole of the game's remembered progress is
  the 694 variables plus four bitmaps, and the variables start empty. The
  bitmaps do not: they are authored defaults saying which zones, addresses,
  props and characters the world begins with.

## 8. The save file

`IAM\GAMES` is created at run time and is **not shipped**, so this section was
read from the writer and the reader alone — until the engine wrote one under
the golden-trace rig. It now has exactly one file behind it, and everything
below is checked against that.

```
+0                3496 bytes   the slot directory (see below)
+3496 + 32808*n                slot n, for n in 0..255
      +0     char[32]          the profile name (byte_6A05C0, "OMK_SAVE")
      +32    u32               the day counter
      +36    u32               the time within the day
      +40    8192              the game DB, un-relocated
      +8232  24576             a frame capture for the slot's thumbnail
      =32808
total 3496 + 256 * 32808 = 8402344
```

The arithmetic is the reason to trust it: `sub_408EF0` writes the thumbnail at
file offset `32808*n + 11728`, which is `8232` into the slot, and
`32 + 4 + 4 + 8192` is `8232` exactly; `8232 + 24576` is `32808` exactly; and
the two literals `0x802800` and `0x8035A8` in the writers are `256 * 32808` and
that plus 3496. Nothing is left over.

~~**Not established:** the directory record.~~ **Settled 2026-08-29 by a file
the engine wrote.** Running the original under `tools/goldentrace.py` makes it
create `IAM\GAMES`, and that file is **8402344 bytes** — exactly
`3496 + 256*32808`. The 72-byte reading of `SaveDir_CountByName` /
`SaveDir_NameAt` needs 18432 bytes of header and gives 8417280, so the **3496
is right** and those two walk something else — **and what they walk is now
read** (2026-08-30). `SaveDir_Build` (0x00408A10) loads the file whole and
lifts four fields out of every slot into a 256 x 72 in-memory table; the
`memset` it opens with is `0x4800`, exactly 256 x 72, and the stride it steps
by is 8202 dwords = 32808, the slot size, with the first name landing at
file+3496:

| directory | | from the slot |
|---|---|---|
| +0 | `char[32]` the profile name | slot +0 |
| +32 | `u32` the day counter | slot +32 |
| +36 | `u32` the time within the day | slot +36 |
| +40 | 32 bytes | slot **+76** — 36 into the slot's DB |

The last field is **not a string** in the one real save
(`traces/save-appart.bin` has binary DB data there) and nothing read so far
consumes it, so what it is for stays open. `verify.py: sim: load panel`. The header is also plainly not a
slot directory: it opens with the profile name `OMK_SAVE`, then `0280 01e0` —
the display mode, 640x480 — and only 119 of its 3496 bytes are non-zero in a
fresh file. `gamestate.from_save(path, n)` reads a slot's DB, which is what
lets a golden-trace replay start from the state a script actually ran under.

**And the DB inside a real save parses**, which is the part no literal could
have supplied. `traces/save-appart.bin` is the engine's own file reduced to its
3496-byte header plus slot 0's first 8232 bytes — name, day, time and the DB,
without the thumbnail — from a save made in Kay'l's apartment. Read through
`from_save` it says:

* **area 237** `Anekbah Appart Kayl`, with **scene 57** (`1-02 Appart Kayl
  Rencontre`) over it — where the save was made;
* day **52**, time 2566060: a new game starts at day 52 / 2000000 (§6), so the
  same day, later;
* `première impasse` set and `Impasse Finie` still 0 — the Impasse has
  happened, the apartment scene has not resolved;
* **`Interface` (variable 19) is 1.** That is the single value
  `verify.py: sim: area load` has to *supply*, because `ui.open` asks the
  player a question the simulator cannot answer and the capture only showed
  which arm was taken. The save records the answer, so the seed stops being a
  choice between two known paths.

`verify.py: save file` pins all of it against the fixture.

### What a save does *not* carry

Only the four items above. In particular the block is written by `State_Save`,
which **un-relocates only the six array offsets** — `a1[2..7] -= base`. The two
bio pointers at `+60`/`+64` are left absolute (§4) and the object-list pointers
are globals recomputed on load, so neither is a problem; but it does mean a
save file is not a self-contained image of what `State_Apply` produced, and a
reader must undo nothing except those six.

## 9. What survives what

| | area change | reincarnation | save / load |
|---|---|---|---|
| the 8192-byte block | yes, untouched | yes | yes |
| the player's character sheet | yes | **replaced** (op 56) | yes |
| the clock | yes | yes | yes (two u32s) |
| position / area fields at +44, +1414 | stale until the next save | — | written, then read |
| the three stored object lists | yes | yes | yes |
| list 3, the shop stock | rebuilt | — | not stored |
| actor records, scene and area blocks, script contexts | freed and rebuilt | — | rebuilt from the block |

An area change never rewrites the block; it calls `Area_SetLoadedScene` on the
one int16 for the area it is leaving or entering, and everything else it needs
it reads back out of the bitmaps (`Scene_LoadProps` replays `object.show` from
array 3, `Zones_RegisterAll` from array 5).

## 10. The reader

```bash
python3 tools/gamestate.py            # the new game, in words
python3 tools/gamestate.py --check    # the invariants, one line each
python3 tools/gamestate.py --raw      # the segment walk
```

`GameState` addresses the block by name — `st.var(i)`, `st.zone_state(z)`,
`st.object_list(0)`, `st.player_id` — and carries `relocate()` / `unrelocate()`
mirroring `State_Apply` and `State_Save`. It is what phase 6's simulator will
hold its state in, and what `verify.py` runs the invariants of §2 through.
