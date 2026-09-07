# 5. The world

← [Contents](README.md) · prev: [The script VM](04-the-script-vm.md) · next: [Actors](06-actors.md)

---

## In short

The world is a set of **places**, and a place is a chunk in an archive: its
set, its characters, its props, its scripts, and the invisible boxes that make
things happen when you walk into them.

Three ideas carry the whole chapter.

**A place runs a script the moment it loads.** Nothing has to name it — arriving
*is* the trigger. That is how a new game starts talking to you.

**Everything else is a trigger zone.** A quad on the floor, plus an arc saying
which way you have to be facing, plus up to three scripts: one for entering,
one for pressing the action button inside it, one for leaving. There are 4 558
of them, and each has one bit in the saved game, which is how a one-shot stays
shot.

**Two places are loaded at once.** When you walk out of a street into a
building, the street is not thrown away — it stays resident in the other slot,
its animations still running, and walking back is not a reload. This is the
detail a replica is most likely to get wrong, and this one did: it rebuilt the
street from the file, and a city that had 32 animations running came back with
none of them, its fires and its neon gone with them.

## In detail

### Areas and scenes

`IAM\AREA` and `IAM\SCENE` are archives of chunks. An AREA chunk is a location:
it names its set (`+97`), its 2D map, its animation library, its traffic
circuit, its characters and props, and its trigger zones. A SCENE chunk is a
*layer* loaded over an area — a cutscene's cast and its own zones — and
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

54 of the zones carry a world camera, and walking into one forces that camera:
the game's walk-into-a-room auto-cut.

**Verified**: 4 558 zones, 0 invalid script offsets, 0 arcs past 4096, 0
duplicate ids.

### Messages

Scripts also subscribe to **events** — 154 subscriptions across ids 0..32 —
which is how one script tells another that something happened without either
knowing about the other. The same dispatcher is what resumes every parked
script in chapter 4's table.

### Two resident slots, and one pool each

The engine keeps **two** areas loaded, in a two-row table, with one row active.
Everything that walks the world walks both rows: the zone registry, the camera
search, the message handlers. The outgoing area stays live — zones armed,
scripts running — for as long as it is resident.

And the object pool belongs to the **slot**, not to the game. `Area_LoadScx`
walks the decor slots for the one holding the area, fills *that slot's*
container, and binds *that slot's* sound file:

```c
sub_44B140(slot + 8);                       /* clear the container      */
Scene_LoadSCX(Buffer, slot + 8);            /* ...and fill it           */
if ((v6 = File_LoadWhole(Buffer, ...))) {
    Sfx_LoadFile(v6, slot);
    Sfx_BindAmbientEffects(slot);
}
```

`Game_Frame` then plays **both** pools every frame, which is why the place you
are not standing in goes on animating.

**So walking back out of a building reloads nothing.** `Area_LoadIntoSlot`
opens by testing whether the slot already holds that area, and if it does it
refreshes the fog block and returns — no `Area_Load`, so no `.SCX` reload and no
startup script. The street's container, its sound binding and its running
programs are exactly as you left them.

That is the rule this port broke and has now fixed. It kept the outgoing pool
and ticked it, correctly, but had no way home: the return built a fresh runner
from the file. Measured over the game's own door pair — Anekbah into Hall 43
and back — the city went from **32 programs running and 153 ambient emitters
bound** to **0 and 0**, and stayed there. Its animations and its neon were dead
for the rest of the session. Coming back is a *swap*, and now is one.

### The transition

`area.goto` names a destination and up to two **door objects**: one played on
the outgoing scene, one on the arriving one. The set streams in at 0x20000
bytes a frame while the game keeps running; the caller parks at status 10 and
is handed back through the pump's tail. A second `area.goto` supersedes the
first and leaves its caller parked for ever (chapter 4's status 5).

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

Saves are not free and not anywhere: you save at a **save point**, by
interacting with it, and each save spends one *anneau* — a ring — charged when
the slot is confirmed. The port does that too, through the game's own panels.

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
| the findings | `docs/GAME_STATE.md`, `docs/FILE_FORMATS.md` §5b2b–5b3, `docs/SCRIPT_VM.md` "The area transition" |
| the port | `engine/src/script/area.*` (the Session: slots, transitions, the frame), `zones.*`, `gamestate.*`, `savefile.*` |
| the checks | `zone records`, `startup scripts`, `engine live zones`, `engine: area transition`, `engine: airlock walk`, `engine: city return` |

## What is not settled

* **One field of the 72-byte save-directory record** is still unexplained.
* **Status 5** — a superseded transition caller — is parked with no resumer
  anywhere in the image. Recorded as the engine's shape rather than as a gap.
* The **fog block** refreshed by a resident return is read as a refresh and its
  contents are not traced.
