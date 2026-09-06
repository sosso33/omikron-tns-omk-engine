# Save support — writing a game, and the two menus

`todo/next-tasks.md` item 8. The format is solved end to end
([`docs/GAME_STATE.md`](../docs/GAME_STATE.md) §5, §8, §8a) and the port already
READS a slot; what is missing is the **writing** and the **two menus**. So this
is mostly plumbing, with exactly one patch of fresh reverse-engineering (the
save panel's own callbacks, §Step 4).

Six steps, each ending in a commit and a report.

---

## What the tree already has

| | where |
|---|---|
| the slot geometry, the settings header field by field | `engine/src/script/savefile.h` |
| reading a slot (name, day, time, DB) | `readSaveSlot`, slot 0 only, used by `omk-play --save` |
| the 8192-byte DB, its six arrays and three object lists | `engine/src/script/gamestate.*` |
| the save DIRECTORY (`SaveDir_Build`'s 256 x 72) | `saveDirectory` / `saveProfiles`, `engine/src/ui/widgets.cpp` |
| the load panel's BRANCH on the profile count | `loadPanelFor`, `verify.py: sim: load panel`, `engine: load panel` |
| the calendar and both formatters | `formatDate` / `formatTime` |
| two real saves to check against | `traces/save-appart.bin` (header + slot 0), `traces/games-resto.bin` (3 slots) |

## What the engine does, read for this task

`Game_WriteSave(slot)` (0x00408EF0) is eleven lines and they fix everything:

```
v1 = File_LoadWhole("IAM\GAMES")          load the WHOLE file
memcpy(v1, byte_90E180, 0xDA8)            the 3496-byte settings over its head
v2 = v1 + 32808*slot
memcpy(v2 + 3496, g_SaveProfileName, 32)  the slot's name
u32(v2, 882) = Clock_GetDay()             = v2 + 3528 = 3496 + 32
u32(v2, 883) = Clock_GetTimeOfDay()       = 3496 + 36
State_Save(v2 + 3536)                     = 3496 + 40, the 8192-byte DB
memcpy(v2 + 11728, sub_433090(), 0x6000)  = 3496 + 8232, the thumbnail
write(v1, 0x8035A8)                       8402344 - the whole file back
```

* **`State_Save` (0x0040D950) takes the snapshot first.** The area and scene
  into +1414/+1416 out of the live area slot, and the player's position and
  facing into +44..+56 with the rounding GAME_STATE §5 quotes exactly. Then it
  copies 0x2000 and un-relocates only `a1[2..7]`.
* **The thumbnail is 128 x 96 x 2.** `sub_4331B0` blits the back buffer into a
  rect `(0,0)-(127,95)`, copies 0x6000 bytes and repacks every one of its
  **12288** pixels into a fixed 5:5:5 (`& 0x1F`, `<< into 0x3E0`, `<< into
  0x7F00`). 128*96 = 12288 and 128*96*2 = 24576 = the slot's tail exactly.
* **`SaveDir_ClearSlot` (0x004090A0)** writes ONE zero, at `32808*slot + 3496`
  — the first byte of the name. An empty slot is an empty name, nothing else.
* **`SaveDir_Delete` (0x00409100)** does that for every slot whose name matches
  a profile.
* **`sub_4092A0`** is the settings-only save, and it is also the CREATOR: when
  `IAM\GAMES` does not open it writes the 3496 bytes and then extends the file
  by 0x802800 = 256*32808.
* **`Game_LoadSave` (0x00408FC0)** reads the header (and discards it — the
  settings come back through `SaveDir_Load`, gated on the magic and the version
  at +8), then the slot's name, day, time and DB, and hands the DB to
  `State_Apply`.

## The one deliberate deviation: WHERE a save lives

The engine writes `IAM\GAMES` inside the game directory. This port must not:
`omk::safeOutputPath` refuses any path under the shipped tree's own
subdirectory names, `/IAM/` among them, and that guard exists because a shipped
file was destroyed once already (CLAUDE.md §1).

So the port keeps a **saves root** outside the data: `--saves <file>`,
defaulting to `omk-saves/GAMES` under the working directory, created on first
write. Reading falls back to the data tree's `IAM/GAMES` when the writable file
does not exist, so the shipped (empty) directory is still what a fresh run
sees. Labelled in the code as the deviation it is — everything about the FILE
is the engine's, only its location is not.

---

## The steps

### Step 1 — the writer  ☑ (2026-09-06)

`savefile.*` has the write half — `settingsBytes`, `blankSaveFile`,
`putSettings`, `writeSaveSlot`, `clearSaveSlot`, `deleteProfile`,
`thumbFromRgb565`, `readSaveFile`/`writeSaveFile` — and `GameState` has
`State_Save`'s snapshot half: `placement()`, `placementWorld()`,
`setPlacement()`, `setCurrentArea/Scene`. `engine/tools/write_save.cpp` and
`verify.py: engine: save write` prove it, and the mutation that shows the
check works is `lround` in place of the two rounding arms.

**Two things came out of it that were not in the plan.**

*The settings serialiser puts the fixture's 3496 bytes back, 0 of 3496
differing.* That is GAME_STATE §8a's "every field is named" as a number a byte
could break — an unnamed field is invisible to every reader in this tree and
shows up only when something writes the block back.

*And GAME_STATE §5's save conversion was wrong.* It read `nearest_int(world *
0.0254 * 256)`; the assembly is `_ftol` (truncation **towards zero**) plus
`lea edx, [ecx+1]`, which tries `v+1` and never `v-1`. For a negative
coordinate both candidates lie on the zero side, so it can only round towards
zero — **15113 of 30000 negative raws fail to round-trip, 0 of the positives
do, and all 15113 land one unit towards zero**. The docs are corrected and the
port reproduces the arms rather than the paraphrase; the twelve real placement
fields in `traces/games-resto.bin` cannot see the difference, which is why the
check sweeps 60001 raws instead.

### Step 2 — loading any slot, with its clock and its placement  ☑ (2026-09-06)

Three flags, and the placement finally read.

* **`--saves FILE`** — where this port reads and writes saves, default
  `omk-saves/GAMES`. Reading falls back to the shipped `IAM/GAMES`, so a
  checkout never saved into still sees the directory the game would; the error
  messages name whichever file was actually opened.
* **`--slot N`** — load slot N and **resume**: adventure mode in the save's own
  area, standing where it was saved, with its clock. Takes the slot from
  `--save FILE` when one is named, otherwise from the saves file.
* **`--save FILE` keeps its old meaning** — the DB as a starting state, the
  intro still playing — because every street-start recipe in the tree pairs it
  with `--area` and drops the save's player record into a city he was never in.

`+44..+56` and `+1414/+1416` had **no reader in this tree at all**: the engine
has exactly one (`State_Apply`) and the port was not it, so a loaded save came
up wherever the harness put the player. The precedence is now `--stand` >
`--address` > the save's own placement (only when the save's area is the one
being loaded) > the area's first ADDRESSES record.

An **empty slot is refused** rather than loaded as zeroes — `SaveDir_Build`
skips it and the load panel never offers it — but a slot whose name was
cleared while its DB is still on disk loads with a note, since that is the
distinction `SaveDir_ClearSlot`'s one byte actually makes.

`verify.py: engine: save load` runs all three slots of
`traces/games-resto.bin` — three places on one day, 14:14, 16:08, 17:14 — and
asserts the area, the scene, the clock, and the x/z against a conversion
`tools/gamestate.py` does independently.

**The first version of that check passed its own mutation**, and the reason is
worth keeping: it read the position off the `save:` line, which the loader
prints whether or not anything consumes it. Switching the placement off
entirely left that line intact while the player walked to the area's first
address, several hundred units away. It now reads the hand-over line — where
he is actually standing.

### Step 2a — the five fields re-read end to end  ☑ (2026-09-06)

Step 2 made the port consume `+44..+56` and `+1414/+1416` on the strength of
reading only as much of `State_Apply` as the feature needed. Read whole
(`docs/GAME_STATE.md` §5a) it had three more things to say, and one was a real
gap:

1. **`+1416` is used TWICE.** Before `Area_Load`, `State_Apply` copies it into
   the scene-per-area table for `+1414`'s area — and `Area_Load` opens by
   reading exactly that entry and ends by `Scene_Load`ing it. So the **header**
   decides which scene goes over a resumed area; the table is only how it gets
   there. The port did not do the copy. It happens to agree in all 4 real
   slots, which is why nothing had broken, and the two have different writers
   (opcode 71 writes the table, `State_Save` the header) so they can diverge.
2. **The placement is gated on the player record's `+272`.** No actor named,
   no placement applied — `IAM\START` is exactly that block.
3. **The conversion is a wrapping 32-bit multiply then a signed `fild`**, not
   the mixed-signedness arithmetic the decompiler shows. Reproduced, so a
   corrupt or hand-edited slot cannot decode two different ways.

All three are ported, and (1)'s agreement is asserted at 4 of 4 by
`verify.py: engine: save write` so it stops being an assumption.

### Step 3 — saving from the running game  ☑ (2026-09-06)

The snapshot taken from the live Session — area, scene, player node, facing,
the clock — plus the 128x96 thumbnail out of the framebuffer. A `--save-to`
smoke path in `omk-play` so a save can be written and re-loaded without a menu.

`--save-slot N` writes one when the run ends and `--save-name S` names it.
The snapshot is `State_Save`'s, in the engine's order: the live area and the
scene over it, the player's position and facing, the clock, then the four
copies into the slot and the whole 8402344 bytes back — with the settings over
the head, because `Game_WriteSave` does that on every slot save.

The scene comes from the **live resident slot**, the way `State_Save` takes it
(`dword_69BC4C[4 * dword_69BC60]`), and not from the scene-per-area table:
§2a showed the header is what a load believes, so taking it from the table
would leave the port unable to express a divergence the engine can. One of the
two mutations proves the check sees it.

The thumbnail is the last frame through `thumbFromRgb565` — a real picture,
looked at rather than assumed, and Kay'l is standing in it.

`verify.py: engine: save round trip` runs `omk-play` twice, writes a save of a
game it was playing and loads it back: the file is 8402344, the slot reads
back with its name, date, area and scene, the position moves by exactly
(−1, −1, −1) — the engine's own asymmetry, reproduced — and the picture holds
hundreds of colours with the X bit set in none of its 12288 pixels.

**Still a harness path, deliberately.** No save point, no panel, no ring
spent: those are steps 4 and 5.

### Step 4 — the load menu  ☐

The load panel is `0x004CF2E8`, the child of the start menu's item 1 AND of
`SAVE GAME`'s item 0 — one panel, told apart by `word_4CEA9A`. Its parts, none
yet modelled:

| | |
|---|---|
| `0x004CEC08` | the slot list, hook **`0x0047AEC0`** |
| `0x004CE968` | *Charger une partie*, callback **`0x0047AC90`** |
| `0x004CE9B0` | *Nouvelle partie*, callback **`0x0047ADB0`** |
| `0x004CE9F8` | *Détruire*, callback **`0x0047AE90`** |
| `0x004CEA40` | *Annuler* — child back to the start menu, no callback |

Read those four, model them in `widgets.*`/`UiWalk` the way the LIFT grid and
the name field are, then make the walk's answer actually load.

### Step 5 — the save menu, and the SAVE POINTS  ☐

**The route is not a pause menu.** A reader supplied this and the data
confirms it (`docs/GAME_STATE.md` §8c, `verify.py: save points`): screen 30 is
opened by **37 `ui.open 30` sites in the world scripts**, every one of them in
a trigger zone's *activate* slot — the action button — and 25 of those zones
are named `Sauvegarde…` while **2 are named `Anneaux`**, the rings object
itself. All four real saves in this tree were written with the player standing
inside one of those quads. So the work is:

* make a zone's activate script that reaches `ui.open 30` open the screen (the
  port already parks a script at `ui.open` and walks the screen for an answer
  — this is the same path screen 29 takes at boot);
* the panel itself, below.

* **charge the ring — READ, 2026-09-06, so this no longer needs guessing.**
  All four of `Game_WriteSave`'s call sites are preceded by the same run:
  read property 5 (the anneaux) through event 44, `dec` by exactly one, write
  it back through event 45, then write the save — the whole run skipped when
  the screen's `+78` is not −1. An overwrite is charged like a new slot (both
  row arms merge before it), and **zero rings does not stop the save**: the
  `jz` skips the decrement only, so the panel would write for free if you
  reached it. What stops you is the save point's script.
  `verify.py: save price`, `GAME_STATE` §8c.
* **write the thumbnail.** The load panel draws the picture of the player at
  the save location — that is the slot's 24576-byte tail, and `writeSaveSlot`
  already takes one; step 3 has to actually capture it. The format is settled
  and now checked against a frame of the original (`GAME_STATE` §8b).
* **the ring is spent on CONFIRM**, for an overwrite exactly as for a new
  slot, and deleting a save does not refund it (the reader, 2026-09-06). The
  panel shows no ring count anywhere, so nothing on screen has to display it.
  Rings are found in the world, not bought.

**Owed to the UI side, once `engine/src/ui/` is free** (another session holds
it): `saveDirectory` in `widgets.cpp` reads only three of the directory's four
fields — it needs the character name at **slot + 108** — and `SaveEntry` needs
somewhere to put it, because the load row is `<name> - <date> - <time>` and
without the field the port cannot draw a row at all. `tools/sim/ui.py`'s
`save_directory` has the same gap. Both are one field each; they are listed
here rather than done so as not to collide.

Nothing needs a new mechanism, and binding it to a key would be inventing one.


Screen **30 `SAVE GAME`**, panel `0x004E2ED8`: item 0 (callback `0x004AE060`)
opens the load panel in save mode, item 1 opens `0x004E3018` — the save-slot
page, five rows of callback `0x004AE220` whose text comes from `0x0042AA00`,
plus a *new save* row `0x004AE260` and the list hook `0x0042AFF0`. `sub_47A6D0`
is the screen's builder (UI.md §3b: it counts the directory and at **256**
marks the new-save row unselectable). Its flag `0x20000400` also opens screen
31, and screen 31 is `todo/next-tasks.md` item 3 — so the in-game ROUTE to this
screen is that item's work, and until it exists the screen is reached with
`omk-play … 30`.

### Step 6 — the docs and the sweep  ☐

`docs/GAME_STATE.md` §8 gains the writer, `engine/README.md` its coverage rows,
and one full `verify.py --slow` before this is called done.
