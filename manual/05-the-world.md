# 5. The world

← [Contents](README.md) · prev: [The script VM](04-the-script-vm.md) · next: [Actors](06-actors.md)

---

## In short

The world is a set of **places**. A place is a chunk in an archive: its set,
its characters, its props, its scripts, its shop stock, and the invisible boxes
that make things happen when you walk into them.

Four ideas carry the whole chapter.

**A place runs a script the moment it loads.** Nothing has to name the script.
Arriving *is* the trigger, and that is how a new game starts talking to you.

**Everything else is a trigger zone.** A zone is a quad on the floor, an arc
saying which way you have to be facing, and up to three scripts: one for
walking in, one for pressing the action button inside it, and one for walking
out. Each zone has one bit in the saved game, which is how a one-shot event
stays used up. A zone has a height too, but the height is not stored in the
zone.

**Two places are loaded at once.** When you walk out of a street into a
building, the street is not thrown away. It stays resident in the other slot,
its animations still running, so walking back does not reload it.

**Hidden is not gone.** A place that is loaded but not shown is still solid.
Its walls still stop you, and a step onto its floor is undone. One building in
the game is designed around that rule.

Underneath all of it is one 8 192-byte block of memory that is both the live
game state and the save file, and a calendar of thirteen 41-day months.

## In detail

### Areas and scenes

`IAM\AREA` and `IAM\SCENE` are archives of chunks. An AREA chunk is a location.
Its header names the set (`+88`), the scene scripts (`+97`), the 2D map
(`+106`), the traffic circuit (`+115`), an animation library (`+124`), a sky
model (`+133`) and a music track (`+142`) (`docs/FILE_FORMATS.md`, "The AREA
header's asset manifest"). Its `+8` is the shop stock: sixteen object ids,
ending at `0xFFFF` (`docs/UI.md`, "…its rows are the AREA's STOCK"). A SCENE
chunk is a *layer* loaded over an area, typically a cutscene's cast and its own
zones, and `scene.load` brings one in without disturbing the area underneath.

Both kinds carry the same nine tables: objects, props, zones, prop assets,
characters, addresses, world cameras and message subscriptions. **AREA's offsets
are SCENE's plus 32 throughout**, except for the shared `+4`. With the strides
taken from the loaders, the tables abut with 0 overlaps in 330 chunks. Every
byte of every chunk is accounted for except **two** unreferenced runs of
bytecode, **65 bytes** in all (`docs/FILE_FORMATS.md`, "Every byte of an AREA /
SCENE chunk, accounted for"; `verify.py: chunk accounting`). There were three
runs and 97 bytes until 2026-10-11. The third run turned out to be the tail of
a high-score script that had been cut in half by reading opcode 142 at four
operand bytes instead of six (chapter 4; `docs/SCRIPT_VM.md`, op 142).

Both kinds carry a **startup script at `+4`**. `Area_TickLoad` hands it to
`Script_NewContext` and queues it the moment the chunk loads, so entering the
place is what runs it. **173 of the 330 chunks carry one** (117 of 259 AREA, 56
of 71 SCENE), and all 173 disassemble cleanly (`docs/FILE_FORMATS.md`, "`AREA +4`
/ `SCENE +4`"; `verify.py: startup scripts`).

This field gets a paragraph because of how long it stayed hidden. The question
"what starts a cutscene's beats?" stayed open while every route that *was*
checked came back empty. The problem was the enumeration, not the reasoning:
the 5 785 script slots come from the zone records and the message
subscriptions, and nothing in that walk reaches `+4`. So "no shipped script
starts them" really meant "no script I enumerate starts them". All along, the
golden trace was announcing scenes that no enumerated slot could emit
(`docs/PRACTICE.md` §6; chapter 7).

### The trigger zones

A zone is 68 bytes, and zones are the whole surface through which the world
reacts to you (`docs/FILE_FORMATS.md` §5b2b, *solved, both ways*):

```
+0/+4/+8  three script slots — enter, activate, leave
+12       four corners × {int32 x, y, z} — the quad on the floor
+60       facing-arc centre  } 4096ths of a turn on disk; the loader
+62       facing-arc width   } converts both to degrees as it relocates
+64       int16 id — one save-game bit each; bit 15 is a flag
+66       int16 world camera, -1 = none
```

`Zones_RegisterAll` puts each zone into a sweep-and-prune index, and it
registers only zones whose save bit is set. That is how `zone.disable` retires a
one-shot permanently. Every frame the actor scan tests containment and raises
event 8 on touch. If the facing also matches, it raises event 7, which feeds the
16-slot table of things you can press the button on. An arc width of zero
accepts any facing.

The three scripts run through one shared context as a small action queue. The
enter script runs when the zone arms. The activate script runs on the button
press, and conversation launches live in this slot. The leave script runs on
leaving, followed by a free (§5b2c). A press that nothing handles posts
**message 26**, the game's "nothing here" (`docs/SCRIPT_VM.md`, "The
unconsumed press"). A second press is refused only while the first activate
script is still running. The **one-shot bit**, which 37 zones carry, keeps the
slot out of the press cycle until the player leaves, rather than freeing it
early (`docs/FILE_FORMATS.md` §5b2b; `docs/SCRIPT_VM.md`, "The 16 prompt
slots"; `verify.py: engine: zone pump`).

**The camera is requested on touch, before the facing test.** 54 zones carry a
camera, over 40 distinct camera ids, and walking into one forces it. This is
the game's walk-into-a-room auto-cut (§5b2b).

**Zones are data, not code, even where they look like behaviour.** Take a
Mecaguard in Jaunpur who raises his weapon as you walk up to him. No engine
code does that. A zone in front of each guard does it, armed only when you face
into its arc: its enter script plays the guard's *up* clip, its leave script
the *down* clip, and its activate slot holds a voice line (`docs/STREET_LIFE.md`,
"The Mecaguards' raised weapon"; `verify.py: engine: meca react`).

**The containment test has no height. The search does, and it is now read.**
`Zone_ContainsPoint` (0x0048C880) is given a y and never reads it. Height
filtering happens in the index the scan queries. `Zone_Add` enters each zone as
an axis-aligned box: the quad's bounds plus **19.685 units (50 cm) above it**.
`Actor_ScanZones` asks for every box that overlaps the actor's own box, which
is his pelvis plus and minus his root mesh's bounding radius (42.5 for Kay'l),
on all three axes. For Kay'l, a zone is therefore found from about 20 units
below his feet to about 84 above them. Stacked levels 130 to 225 units apart
still separate (`docs/FILE_FORMATS.md` §5b2b, "WHICH ZONES ARE TESTED AT ALL";
`docs/UI.md` §3f, "…why it never arrived"; `verify.py: engine: zone box`,
`engine: lift`).

The security centre's lift is why this matters. It has one pair of lift zones
per level, stacked over a single footprint up the shaft. Scanning by the quad
alone armed every level's lift zone at once: five scripts parked on five copies
of the lift screen, and the one answer went to the wrong one. Until 2026-10-05
the port filtered with a band of its own, the quad's height plus a metre, which
was labelled as a reconstruction. The drift audit read the engine's box
(`todo/drift-audit.md` S10). The band had let in zones 20 to 39 units below the
engine's limit and turned away zones 39 to 84 units above it.

**Verified**: 4 558 zones, with 0 invalid script offsets, 0 arcs past 4096 and 0
duplicate ids (`verify.py: zone records`).

### Messages

Scripts also subscribe to **messages**. That is how one script tells another
that something happened without either knowing about the other. The shipped
data carries **154 subscriptions, 138 of them with a script**, over message ids
0..32 (`docs/FILE_FORMATS.md` §5b3; `verify.py: message tables`). A posted
message is looked up in the **scene, then the area, then `IAM\GLOBAL`**, and
the first match wins. The handler runs in a fresh context whose one readable
parameter is the sender (chapter 4). Message 25 runs inline and all the others
are queued (`docs/SCRIPT_VM.md`, "Messages, and the handler's parameter
block").

The engine is usually the sender:

* examining an object posts message 4 (`docs/UI.md`, "MULTIPLAN");
* a hard landing posts 10 or 11, a vehicle hitting the player 17, running out of
  breath underwater 12, and surfacing 21 (`docs/ASSETS.md` §7, "The fall
  reaction" and "The water");
* the script timer's expiry posts **18**. In the Tetra raid it is the bomb
  countdown running out: fifteen minutes, then a fade to black and a return to
  the first Tetra area (`docs/GAME_STATE.md`, "The script timer");
* **2** is a gunman hit who *survives*. **5-8** are Gandhar's grab: the side of
  the gunman's floor box nearest the player picks the number. **27-32** are
  Astaroth's six weak points (`docs/FILE_FORMATS.md` §5b3, "Who posts four of
  the rarer ones"; chapter 6).

Each of these is answered by a handler in some area or in `IAM\GLOBAL`.

### Two resident slots, one pool each

The engine keeps **two** areas loaded, in a two-row table with one row active.
Everything that walks the world walks both rows: the zone registry, the camera
search and the message handlers. The outgoing area stays live, with its zones
armed and its scripts running, for as long as it is resident (`docs/SCRIPT_VM.md`,
"Two resident slots").

The object pool belongs to the **slot**, not to the game. `Area_LoadScx` finds
the decor slot that holds the area, fills *that slot's* container and binds
*that slot's* sound file. `Game_Frame` then plays **both** pools every frame,
which is why the place you are not standing in keeps animating. A script plays
objects in its own slot's pool too (chapter 4).

**Walking back out of a building reloads nothing.** `Area_LoadIntoSlot` begins
by testing whether the slot already holds that area. If it does, the function
refreshes the fog block and returns: no `.SCX` reload and no startup script.
The street's container, its sound binding and its running programs are exactly
as you left them. The port once rebuilt the street from the file on the way
back, and a city that had 32 programs running and 153 ambient emitters bound
came back with 0 and 0 (`docs/SCRIPT_VM.md`, "Returning to an area that is
still resident runs **no** startup script"; `verify.py: engine: city return`).

**The player's feet decide which row is active.** The transition only shows and
hides sets. What switches the active row is event 9, which the ground probe
raises when the floor under the actor belongs to a set in the other slot that
is **shown**. The transition's completion then hides "the non-active row's"
set, and that is the one you left, because your feet have already moved the
row. When a script has no `area.arrive`, both sets stay shown and the player
walks from one onto the other with both drawn, as in the Impasse's airlock
(`docs/SCRIPT_VM.md`, "Which row is ACTIVE"; `verify.py: engine: airlock walk`).

The sky is the exception to one-per-slot. The engine keeps **one** sky: an area
that names a sky replaces the current one, and an area that names none leaves it
standing. 17 of the 259 areas name one (`docs/RECONSTRUCTION.md`, 2026-09-08;
`verify.py: the sky`).

**The two slots meet at doors, sometimes literally.** A city's set and the
interior behind a door can each carry the door's own leaves, at the same place,
moved by the same path while both slots are shown. Over Anekbah's interiors
that is 86 meshes and 931 faces, 73 of the meshes lit differently in the two
copies. The engine's depth test keeps whichever copy is drawn first. A 32-bit
depth buffer fought over them, and a reader saw the doors flicker as they
opened. Since 2026-11-01 the port leaves the later copy of each such face out of
the draw list. That is a reconstruction in one respect: the engine draws both
and lets its own buffer decide (`docs/ASSETS.md` §4b, "A door exists twice";
`verify.py: engine: door twin`; chapter 8).

### Hidden is not unloaded

Show and hide (`sub_419AF0` / `sub_419A90`) link a set into and out of the
**render list**, and that is all they do. The **collision array** is a separate
list. A set joins it when it finishes loading and leaves it only when it is
unloaded or evicted, so a hidden set keeps its walls.

A hidden set's floor also refuses you. The tail of `Walk_ProbeGround`
(0x00467030) has been read in full. When the floor under the actor belongs to
another scene, a slot in state 2 relinks him and raises event 9. A slot in
**state 1** (loaded and hidden) gets this frame's move subtracted again, so the
step is undone (`docs/SCRIPT_VM.md`, "A HIDDEN set is still SOLID").

The security centre is built on that rule. Each level's corridor is part of the
**shaft's** set: the landings, the lift cars and the shaft doors, 99 meshes in
one model. Each level's own set holds only its offices' furniture and doors and
no floor at all. That set is loaded and hidden until a door zone's
`area.goto` brings it in. Its doors and furniture stay solid while it is
hidden, and the corridor under the player is always the shaft's. The port once
dropped a set from collision when it hid it, and a player walked through the
barriers. It now keeps every loaded set solid and draws only the shown ones
(`verify.py: engine: security rail`; chapter 6 for the rails).

**A lift is just a moving floor.** `Actor_ApplyMotion` runs the ground probe
and its response on *every* frame, standing still included, so the player is
snapped onto the platform as it rises. The port used to answer the floor only on
frames with a root delta. The idle clip has two such frames in each loop, so
AREA 50's lift rose past the player on those frames and left him on the shaft
floor. It now carries him up and back down (`docs/ASSETS.md` §7, the walker;
`todo/drift-audit.md` M9; `verify.py: engine: lift ride`). Lift and platform
rides are also bracketed by `walk.ledges.ignore` / `.obey`, which waive the
30 cm step limit in both directions (`todo/drift-audit.md` S5).

### The transition

`area.goto` names a destination and up to two **door objects** in the outgoing
scene: a departure animation played before the switch and an arrival animation
after it. 448 of its 756 sites carry both. Resolved against the scene being
left, 416 of those 448 land; resolved against the destination, 84 do
(`docs/SCRIPT_VM.md`, "Opcode 47 `area.goto`"; `verify.py: area.goto
objects`). 441 of the 448 are zone enter scripts, and the objects are linked
open/close pairs of a door set piece (`docs/SCRIPT_VM.md`, "The objects are
doors").

The destination loads into the other slot while the game keeps running. Only
the set streams, in 128 KiB slices, one per frame, and everything after it loads
in a single pump tail. So a transition takes as many frames as the set has
slices: 17 for Anekbah's 2 099 056-byte set and 1 for the Impasse's
(`docs/SCRIPT_VM.md`, "The load takes frames"; `verify.py: engine: area
transition`). The caller parks at status 10 and is released through the pump's
tail. A 60-second watchdog unwedges a load that never reports. Those are 60
**wall-clock** seconds, because the timer is `Sys_GetTimeMs`. The port counted
frames until 2026-10-06, so on a 15 fps machine its watchdog fired only after
120 seconds (`docs/SCRIPT_VM.md`, "The watchdog"; `todo/drift-audit.md` S12).

A door a program opens **stays open**. `Script_MoveObjectOnPath` ends by setting
the node's position and restores nothing. Over the corpus, 1 277 of the 2 315
objects that move a node leave it displaced. 1 239 of those have a linked
partner state, an *open* and a *closed*, which is the data's own argument that
nothing puts the node back (`docs/SCRIPT_VM.md`, "…and a script plays objects in
ITS OWN slot's pool"; `verify.py: engine: node rest`).

### The saved game is one block

There is one 8 192-byte structure, and it is both the live game state and the
save file's payload (`docs/GAME_STATE.md` §1):

```
IAM\START ────► the 8192-byte DB ────► State_Apply ────► playing
   5686 bytes                                ▲
IAM\GAMES slot ─────────────────────────────┘
```

`IAM\START` **is the new-game save**. It is not a script archive, which is what
it was first read as. The file is 5 686 bytes and the block 8 192. Everything
past the end of the file is zero and stays zero, and the save writes all 8 192
bytes back. The walk over it lands exactly on the file size. Each of its six
arrays is counted against an independent source: 694 variables, 259 per-area
resident-scene ids, 670 two-bit prop states, 1 032 shown bits for characters and
objects, 791 address bits and 4 558 zone bits (`docs/GAME_STATE.md` §2;
`verify.py: game state`). A new game begins in AREA 118, *Introduction Kay'l*,
with no player body, every variable zero, and 3 834 of the 4 558 zones set
(`docs/GAME_STATE.md` §7).

`IAM\GAMES` is a 3 496-byte header followed by 256 slots of 32 808 bytes
(`docs/GAME_STATE.md` §8; `verify.py: save file`). The header is the settings
block: all 74 option rows, the three key-binding tables verbatim, and the
shooting range's high-score table at `+724`. Saving a game therefore also saves
your options and your best times, once for all 256 slots (§8a; `verify.py:
settings block`). The directory record's fourth field is slot `+108`, the
character's name, which the load panel uses to label a row (§8).

Saves are not free and not available anywhere. Screen 30 is opened only by the
world, at 37 zones, all in the activate slot (`docs/GAME_STATE.md` §8c;
`verify.py: save points`). 36 of them run the same program: check the player's
rings (his *anneaux*) and refuse with a voice line when he has none. The price
is charged by the panel, not by the script. All four call sites of
`Game_WriteSave` are preceded by the same instruction run, which takes exactly
one ring, so an overwrite costs the same as a new slot (§8c; `verify.py: save
price`). The screen's *Sauvegarde* item also opens a "not enough rings" page
when the count reads zero (`docs/UI.md` §3g). One save point, in *Ix Astaroth*,
has no gate at all. A slot carries a 128 × 96 thumbnail. Loading is a
*request*, served at the top of the next script pump, and the screen fades in
from white (§8).

**There is no load in the middle of a game.** Screen 30 hides *Charger*, the
pause menu offers only *Reprendre* and *Quitter*, and *Quitter le jeu* sets the
same restart request as op 152 `game.restart`. A mid-session load therefore goes
quit, restart, start menu, *Charger*. The port ended the run on that quit until
2026-10-05; it now restarts (`todo/drift-audit.md` T1, S3; `docs/SCRIPT_VM.md`,
op 152; `verify.py: engine: game restart`).

A dossier read on a terminal can **open a place**. `address.enable` sets a bit
in the 791-bit address map, and one dossier, for example, enables
'Anekbah - Bar Zone 52'. Those bits have two traced readers. The sneak's slider
page lists only the destinations whose bit is set, and the city map draws a red
marker for each of them (`docs/UI.md` §3g, "`IAM\GLOBAL +16` — the slider
destinations", and §3g-bis; `verify.py: slider destinations`).

### The calendar

Time is two globals saved beside the block, and every constant is a `dd` in the
data segment: **41 days per month, 13 months**, year zero 7216, and 3 600 000
milliseconds per day divided into 21 hours of 15 minutes of 33 seconds. The
months are named Aqed, Nadim, Andar, Xenep, Nevod, Ganevat, Osmydep, Qomivo,
Taznevet, Ustanevat, Nivat, Mozkanep, Primevat. The clock advances 166 units
per tick, about 996 a real second, so a game day is an hour of real time
(`docs/GAME_STATE.md` §6).

A new game begins on **12 Nadim 7216 at 11:10:00**. The shipped content agrees
with the code without either knowing about the other: all eight dated
newspapers in `IAM\OBJECT` carry legal dates in this calendar, and the first is
dated the day before the game starts (§6; `verify.py: game clock`).

**The clock actually runs now.** Until 2026-10-05 nothing in the port called it.
The clock stood where the save left it and no countdown ever ran out. The
original calls it at the tail of `Game_Tick` every frame, with no gate: under
a conversation, under a screen, and during the pause (whose delta is 0)
(`docs/GAME_STATE.md`, "The script timer"; `todo/drift-audit.md` S1;
`verify.py: engine: script timer`). In 19 areas the same clock also drives the
fog colour and the light floor, the day/night cycle of chapter 8
(`docs/FILE_FORMATS.md` §5b, the lights; `verify.py: engine: day night`).

## Where it lives

| | |
|---|---|
| the findings | `docs/FILE_FORMATS.md` §5b2b–5b3, §5c; `docs/SCRIPT_VM.md` "The area transition" and "The 16 prompt slots"; `docs/GAME_STATE.md`; `docs/UI.md` §3f "THE LIFT" |
| the records | `todo/drift-audit.md` (S1, S3, S5, S10, S12, M4, M9, T1) |
| the port | `engine/src/script/area.*` (the Session: slots, transitions, the frame), `zones.*`, `gamestate.*`, `savefile.*`; `engine/src/o3de/collision.*`, `o3de/settwin.h`; `engine/src/actor/walk.*` |
| the checks | `zone records`, `startup scripts`, `chunk accounting`, `message tables`, `engine: live zones`, `engine: zone pump`, `engine: zone box`, `engine: area transition`, `engine: airlock walk`, `engine: city return`, `engine: lift`, `engine: lift ride`, `engine: slot pool`, `engine: node rest`, `engine: security rail`, `engine: door twin`, `engine: game restart`, `engine: script timer`, `game state`, `save file`, `save points`, `save price`, `settings block`, `slider destinations`, `game clock` |

## What is not settled

* **Status 5**, a transition caller that a second transition superseded, is
  parked with no resumer found anywhere in the reading. The shipped scripts may
  never reach it, and settling it needs a trace this repository cannot take
  (`docs/SCRIPT_VM.md`, "The context status word").
* **How many sites post a message has not been counted.** `docs/SCRIPT_VM.md`
  says 28 and `docs/FILE_FORMATS.md` §5b3 says 27. The 27 is a floor over the
  decompilation, which misses every function that nothing calls. Neither
  number is asserted by a check.
* **The fog block** that a resident return refreshes is read as a refresh, and
  its contents are not traced here.
* **The save panel's two ring gates have not been tied together.**
  `docs/GAME_STATE.md` §8c reads the confirm, where zero rings skips the
  decrement and saves anyway. `docs/UI.md` §3g reads the *Sauvegarde* item,
  which opens a "not enough rings" page when the count is zero. These are
  different functions and the two readings agree: the item refuses at the
  panel's entry, and the confirm's skip only matters if something reaches it
  with zero rings. Whether anything does is not established, and no check ties
  the two (§8c's cross-reference, 2026-11-02).
* **One actor id in both resident slots.** 44 transitions pair two areas whose
  actor tables share an id. The original keeps one actor per placement *record*,
  so both bodies exist. The port keys bodies by actor id. This has been
  measured and not changed (`todo/drift-audit.md` M4).
* **The lift's dark arrival** at a security-centre level is mostly explained by
  the back-face cull: with it, the frame goes from 90.3% to 56.9% dark
  (`docs/ASSETS.md` §7, "The obstruction pass"; chapter 8). Whether the rest of
  that darkness is also the original's has not been compared against a capture.
* **The door-twin rule** decides between the two copies by draw order, and that
  is a reconstruction. Which depth precision the engine's buffer ran at has not
  been read from the binary (`docs/ASSETS.md` §4b).
* **Lift rides other than AREA 50's** have not been measured. Two of them carry
  the player with `scx.play.player`, which the walker does not own
  (`todo/drift-audit.md` M9).
