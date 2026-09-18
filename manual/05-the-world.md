# 5. The world

← [Contents](README.md) · prev: [The script VM](04-the-script-vm.md) · next: [Actors](06-actors.md)

---

## In short

The world is a set of **places**, and a place is a chunk in an archive: its
set, its characters, its props, its scripts, its shop stock, and the invisible
boxes that make things happen when you walk into them.

Four ideas carry the whole chapter.

**A place runs a script the moment it loads.** Nothing has to name it — arriving
*is* the trigger. That is how a new game starts talking to you.

**Everything else is a trigger zone.** A quad on the floor, plus an arc saying
which way you have to be facing, plus up to three scripts: one for entering,
one for pressing the action button inside it, one for leaving. There are 4 558
of them, and each has one bit in the saved game, which is how a one-shot stays
shot. A zone has a height too, though not where you would look for it.

**Two places are loaded at once.** When you walk out of a street into a
building, the street is not thrown away — it stays resident in the other slot,
its animations still running, and walking back is not a reload.

**Hidden is not gone.** A place that is loaded but not shown is still solid:
its walls still stop you, and a step onto its floor is undone. A whole
building in the game is laid out around that.

## In detail

### Areas and scenes

`IAM\AREA` and `IAM\SCENE` are archives of chunks. An AREA chunk is a location:
it names its set (`+97`), its 2D map (`+106`), its animation library, its
traffic circuit, its characters and props, its shop stock (`+8`, sixteen object
ids ending at `0xFFFF`), its sky (`+133`) and its trigger zones. A SCENE chunk
is a *layer* loaded over an area — a cutscene's cast and its own zones — and
`scene.load` brings one in without disturbing the area under it.

Both carry a **startup script at `+4`**. `Area_TickLoad` hands it to
`Script_NewContext` and queues it the moment the chunk loads, so entering the
place is what runs it. **173 of the 330 chunks carry one and all 173 decode.**

That field is worth a paragraph because of how long it stayed hidden. The
question "what starts a cutscene's beats?" was open for weeks while every
route that *was* checked came back empty — and the enumeration was the problem,
not the reasoning: the 5 785 script slots come from the zone records and the
message subscriptions, and nothing in that walk reaches `+4`. So "no shipped
script starts them" really meant "no script I enumerate". The golden trace had
been announcing the answer the whole time.

### The trigger zones

68 bytes, and the whole surface through which the world reacts to you:

```
+0/+4/+8  three script slots — enter, activate, leave
+12       four corners × {int32 x, y, z} — the quad on the floor
+60       facing-arc centre  } 4096ths of a turn on disk; the loader
+62       facing-arc width   } converts both to degrees as it relocates
+64       int16 id — one save-game bit each
+66       int16 world camera, -1 = none
```

`Zones_RegisterAll` puts each into a sweep-and-prune index, and only zones
whose save bit is set are registered — which is how `zone.disable` retires a
one-shot permanently. Every frame, the actor scan tests containment, raises
event 8 on touch, and raises event 7 — the 16-slot "what can I press the button
on" table — when the facing matches too.

**The containment test has no height, and the scan does.** `Zone_ContainsPoint`
(0x0048C880) takes a y and never reads it; the filter is the iterator, which
builds its search box around the actor in all three axes, so only zones at his
height are ever yielded. The security centre's lift shows why it matters: one
pair of lift zones per level, stacked over a single footprint up the shaft.
Scanning by the quad alone armed every level's lift at once, five scripts
parked on five copies of the lift screen, and the one answer went to the wrong
one. The port bands by the quad's own height plus a metre — **a
reconstruction**, since the record's radius field is unread; over all 4 558
zones the choice does not decide anything shipped.

54 of the zones carry a world camera, and walking into one forces that camera:
the game's walk-into-a-room auto-cut.

**Verified**: 4 558 zones, 0 invalid script offsets, 0 arcs past 4096, 0
duplicate ids.

### Messages

Scripts also subscribe to **events** — 154 subscriptions across ids 0..32 —
which is how one script tells another that something happened without either
knowing about the other. The same dispatcher is what resumes every parked
script in chapter 4's table. Several new senders turned up with the port's
later work: examining an object posts message 4, a hard landing 10 or 11, a
vehicle hitting the player 17, surfacing from the water 21 and running out of
breath 12 — each answered by some area's or `IAM\GLOBAL`'s handler.

### Two resident slots, one pool each

The engine keeps **two** areas loaded, in a two-row table, with one row active.
Everything that walks the world walks both rows: the zone registry, the camera
search, the message handlers. The outgoing area stays live — zones armed,
scripts running — for as long as it is resident.

The object pool belongs to the **slot**, not to the game. `Area_LoadScx` walks
the decor slots for the one holding the area, fills *that slot's* container,
and binds *that slot's* sound file, and `Game_Frame` plays **both** pools every
frame, which is why the place you are not standing in goes on animating. A
script plays objects in its own slot's pool, too (chapter 4).

**So walking back out of a building reloads nothing.** `Area_LoadIntoSlot`
opens by testing whether the slot already holds that area, and if it does it
refreshes the fog block and returns — no `.SCX` reload and no startup script.
The street's container, its sound binding and its running programs are exactly
as you left them. The port once rebuilt the street from the file on the way
back, and a city that had 32 programs running and 153 ambient emitters bound
came back with 0 and 0; coming back is a *swap*, and now is one.

**Which row is active is decided by the player's feet.** The transition only
shows and hides sets; what switches the active row is event 9, raised by the
ground probe when the floor under the actor belongs to a set in the other slot
that is **shown**. The transition's completion then hides "the non-active
row's" set — which is the one you left, because your feet already moved the
row.

The sky is the exception to one-per-slot: the engine keeps **one** sky, and
whichever area loads last and names one replaces it.

### Hidden is not unloaded

Show and hide (`sub_419AF0` / `sub_419A90`) link a set into and out of the
**render list**, and that is all they do. The **collision array** is a separate
list: a set joins it when it finishes loading and leaves it only when it is
unloaded or evicted. So a hidden set keeps its walls.

And its floor refuses you. The tail of `Walk_ProbeGround` (0x00467030), read
whole: when the floor under the actor belongs to another scene, a slot in
state 2 relinks him and raises event 9; a slot in **state 1** — loaded and
hidden — gets `o3de_MoveNodeBy(node, -(this frame's move))`. The step is
undone.

The security centre is built on that. Each level's corridor is part of the
**shaft's** set — the landings, the lift cars, the shaft doors, 99 meshes in
one model — and each level's own set holds only its offices' furniture and
doors — no floor at all — loaded and hidden until a door zone's `area.goto`
brings it in. Its doors and furniture stay solid while it is hidden, and the
corridor you stand on is always the shaft's. The port had dropped a set from
collision when it hid it, and
a player walked through barriers and fell down the shaft; it now keeps every
loaded set solid and draws only the shown ones. The rails also needed the
body's sweep to meet a triangle's edges (chapter 6).

### The transition

`area.goto` names a destination and up to two **door objects**: one played on
the outgoing scene, one on the arriving one. The set streams in at 0x20000
bytes a frame while the game keeps running; the caller parks at status 10 and
is handed back through the pump's tail. A second `area.goto` supersedes the
first and leaves its caller parked for ever (chapter 4's status 5).

A door a program opens **stays open**: `Script_MoveObjectOnPath` ends by
setting the node's position and restores nothing. Over the corpus, 1 277 of
the objects that move a node leave it displaced, and 1 239 of those have a
linked partner state — an *open* and a *closed* — which is the data's own
argument that nothing puts the node back.

### The saved game is one block

There is one 8 192-byte structure, and it is both the live game state and the
save file's payload:

```
IAM\START ────► the 8192-byte DB ────► State_Apply ────► playing
   5686 bytes                                ▲
IAM\GAMES slot ─────────────────────────────┘
```

`IAM\START` **is the new-game save** — not a script archive, which is what it
was first read as. The file is 5 686 bytes and the block 8 192; everything past
the file is zero and stays zero, and the save writes all 8 192 back. The walk
over it lands exactly on the file size, and six independent counts agree with
six independent sources.

`IAM\GAMES` is a 3 496-byte header and 256 slots of 32 808 bytes. The header is
the settings block — all 74 option rows, the three binding tables verbatim, and
the shooting range's high-score table at `+724` (four pages of five, a name and
a time in milliseconds) — so saving a game saves your options and your best
times, once for all 256 slots. The directory record's fourth field, once
recorded as "not a string", is slot `+108`: the character's name, which is
what the load panel labels a row with. The wrong offset had landed in the
state's array counts.

Saves are not free and not anywhere: you save at a **save point**, by
interacting with it, and each save spends one *anneau* — a ring — refused both
by the save point's script and by the panel when you have none. A save carries
a 128 × 96 thumbnail. Loading is a *request*, served between two script pumps.

A dossier read on a terminal can **open a place**: the scripts enable entries
of a 791-bit address map with `address.enable` — one dossier enables
'Anekbah - Bar Zone 52' — and the sneak's city map draws a marker for each
enabled destination.

### The calendar

Time is two globals saved beside the block, and every constant is a `dd` in the
data segment: **41 days per month, 13 months**, year zero 7216, 3 600 000 units
per day divided into 21 hours of 15 minutes of 33 seconds, with the months
named Aqed, Nadim, Andar, Xenep, Nevod, Ganevat, Osmydep, Qomivo, Taznevet,
Ustanevat, Nivat, Mozkanep, Primevat.

A new game begins on **12 Nadim 7216 at 11:10:00**.

## Where it lives

| | |
|---|---|
| the findings | `docs/GAME_STATE.md`, `docs/FILE_FORMATS.md` §5b2b–5b3, `docs/SCRIPT_VM.md` "The area transition" and "A HIDDEN set is still SOLID" |
| the port | `engine/src/script/area.*` (the Session: slots, transitions, the frame), `zones.*`, `gamestate.*`, `savefile.*`; `engine/src/o3de/collision.*` |
| the checks | `zone records`, `startup scripts`, `engine: live zones`, `engine: area transition`, `engine: airlock walk`, `engine: city return`, `engine: lift`, `engine: slot pool`, `engine: node rest`, `engine: security rail` |

## What is not settled

* **The zone scan's height** is a labelled reconstruction: the search box's
  radius is a field of the zone-space record that has not been read.
* **Status 5** — a superseded transition caller — is parked with no resumer
  anywhere in the image. Recorded as the engine's shape rather than as a gap.
* The **fog block** refreshed by a resident return is read as a refresh and its
  contents are not traced.
* **What the engine does about the lift car around the camera** at a level's
  arrival is open: the frame is dark because a camera sits inside the car's
  mesh, and neither the obstruction pass nor the slot bookkeeping is the
  answer (chapter 8).
