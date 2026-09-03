# The cutscenes — how the game stages a scene with nobody driving

Start at [`../CLAUDE.md`](../CLAUDE.md) for the working practice these findings
depend on.

Omikron has two quite different non-interactive things, and they are worth
separating before anything else:

| | where | what it is |
|---|---|---|
| **pre-rendered video** | `gamedata/FLIS/*.MPG` — three files | `EIDOS.MPG`, `QUANTIC.MPG`, `GAME.MPG`. Publisher logos and the intro film. Nothing to decode |
| **in-engine cutscenes** | `SCPTDATA/*.SCX` + a world script | real characters, real sets, a keyframed camera. **This document** |
| **world-camera cutscenes** | a world script alone | no `.SCX` editing at all: the script cuts and travels between world cameras. 106 of them, including the title sequence — §4b |

There is no third thing. There is also no *cutscene format*: an in-engine
cutscene is the ordinary scene machinery — the same `.SCX` file that stages a
conversation — driven by a world script that blocks on it. Everything below is
already documented piecewise in
[FILE_FORMATS 5c](FILE_FORMATS.md) and [SCRIPT_VM](SCRIPT_VM.md); what was
missing was the sentence that joins them.

> Ruled out while looking: `gamedata/TRAJECTOIRES/*.opt` (six files named for
> cities) are **not** camera paths — they are the slider circuits, and §1
> closes the `AREA` field that names them.

---

## 1. The five pieces

```
   AREA record +97  ──► SCPTDATA/<stem>.SCX  ──►  chunk 0   .3dp paths
                    └─► SCPTDATA/<stem>.sfx        chunk 1   .3DA clips
   AREA record +88  ──► MESHES/Decors/<stem>.3DO   chunk 2   the programs
                                                   chunk 10  the camera editings

   IAM\AREA / IAM\SCENE bytecode  ──►  scx.play*.wait  ──►  one program per beat
```

| piece | who consumes it |
|---|---|
| **chunk 2**, the 100-byte script objects | `Script_PlayScript` — one object is one beat: a list of functions, run one chain per frame |
| **chunk 1**, the `.3DA` clips | `Script_SelectBodyAnimation` (545 calls) and `Script_SelectRelativeBodyAnimation` (2398), which also move the actor by the clip's sampled root motion |
| **chunk 0**, the `.3dp` paths | `Script_MoveObjectOnPath` — **4841 calls**, the most-used function in the whole set: doors, lifts, vehicles, props |
| **chunk 10**, the *camera editings* | `Cam_PlayEditing` — this is the shot list |
| the sibling `<stem>.sfx` | `Sfx_LoadFile`; the engine's own name for the animation-bound rows is **`"sfx for cin"`** |

### Where a place's scene file is named

The `AREA` header's asset manifest already has this — see
[FILE_FORMATS](FILE_FORMATS.md#the-area-headers-asset-manifest): `Area_TickLoad`
(0x0040C7E0) walks a run of 9-byte stems and appends a compiled-in extension to
each, `+88` `.3DO` for the set and `+97` `.SCX` for the scene script (then the
same name with `c`→`f` for `<stem>.sfx`). The switch cases are three lines apart
in the asm — `lea edi, [ebp+58h]` and `lea edi, [ebp+61h]`.

What this document adds is that they **resolve**, which is worth a check because
a field that was really something else would miss at random. Across the 258
shipped areas `+88` names a shipped set **232 / 258** and `+97` a shipped `.SCX`
**227 / 252 non-empty**; the **28** areas missing one are not scattered — 24 of
them are two coherent blocks of cut content, the twelve `Lahoreh Musée` rooms
and the twelve `Mayerem Hamest Tombo` tombs. `verify.py: area assets`.

`Jaunpur Prison Pamoka` → `SPRISON` → `SPrison.SCX`, the file with the twenty
prison editings below.

> While checking `TRAJECTOIRES`, the manifest's one unexplained row closed
> with it: **`AREA +115` is the area's slider circuit**, `.OPT` appended
> (`dword_4C0D50`) and handed to `Area_LoadSliderTrack` → `Slider_Init`. Five
> areas name one — Anekbah, Jaunpur (`SOUK`), Lahoreh, Qalisar (`QCHAUD`),
> Anekbah CS Puits — and **all five ship**; `BIBLIO.OPT` ships unnamed by any
> area. That is the whole of `gamedata/TRAJECTOIRES`, and none of it is cutscene
> data. `verify.py: area assets`.

## 2. The camera — chunk 10, the "editings"

An **editing** is a shot: an ordered list of tracks, each a run of
`(frame, camera)` keys interpolated by `Cam_PlayEditing` (0x0049ECE0, the
engine's own name), plus a `duration` in frames and the id of the script object
it belongs to. The format is in
[FILE_FORMATS 5c](FILE_FORMATS.md#chunk-10--the-camera-editings); what matters
here is that the names are a shot list and they read like one.

`Impasse.SCX` — the alley Kay'l walks into in the first minute of the game:

```
mecaspeak  558 frames (18.6 s)  5 tracks  -> C_2_MecaSpeaks
demonout   350 frames (11.7 s)  5 tracks  -> KaylDemonAme
intro      330 frames (11.0 s)  2 tracks  -> A_1_KaylArrives
kaylup     255 frames ( 8.5 s)  3 tracks  -> C_3_KaylsUp
boxblow    185 frames ( 6.2 s)  1 track   -> C_1_BoxMoves
sautdemon  132 frames ( 4.4 s)  3 tracks  -> A_2_DemonLook
combkayl    72 frames ( 2.4 s)  1 track   -> 2_Combat_Kayl
```

`Aapkayl.SCX` (Kay'l's apartment) has `a2kaykiss`, `amour`, `sdb`, `trainroom`;
`PAstarot.SCX` has one 17.7-second `die` on `1_Kush_End`. The object names carry
their own ordering — `A_1`, `A_2`, `C_1`, `C_2`, `E_4`, `F_5`, `I_7` — a beat
letter and a step within it.

**29 of the 220 scenes carry chunk 10, holding 125 editings and 24112 frames —
13.4 minutes of authored camera work.**

### How the editing takes the camera

`Script_PlayScript` (CLEAN, `readable/src/17_script.c`) does it at the top of
the object's tick, before the program runs:

```c
editings = u32(obj->scene, 92);              /* the chunk-10 container */
if (editings && obj->editings[slot]) {
    edi = Scene_FindCamEditingById(editings, obj->editings[slot]);
    if (obj->clock < duration(edi)) {
        Cam_PlayEditing(&scratch, edi, obj->clock);   /* the program clock */
        Scene_SetActiveCamera(scene, &scratch);
        ediPlaying = 1;                                /* ... and the two   */
    }                                                  /* camera functions  */
}                                                      /* are skipped below */
```

Four consequences worth stating plainly:

* the shot is sampled at **the object's own program clock**, so the camera and
  the animation cannot drift apart — they are the same clock;
* while an editing is driving, the object's own `Script_SelectCamera` /
  `Script_InterpolateCameras` calls are **skipped**, so an author can leave both
  in the program;
* past `duration` the slot advances and the editing simply stops driving. It
  does not have to match the program's length;
* and the frame on screen is sampled at the clock **before** this tick's
  delta — `Script_PlayScript` samples `obj->clock`, then advances it — so the
  object's first drawn frame is key frame **0**, not `dt`. A port that reads
  the clock after the tick is one frame late everywhere, which is not a
  rounding difference: the port's first build did exactly that and disagreed
  with `tools/cutscene.py` at 6940.40 against 6938.87 on the very first sample
  (`SceneRunner::editingClock`, `verify.py: engine: cam mode 13`).

### The link, and a correction

The object record's four bytes at `+94..97` are its **camera-editing slots**.
They are `0` on disk in **all 4511 shipped objects**; `Script_LinkCamEditing`
writes them at load from the target id each editing carries, at most four per
object ("You cannot link more editings to this script").

> **This corrects a reading in SCRIPT_VM.md.** Those bytes were described there
> as opaque runtime state, and the function that reads them —
> `ScriptObject_HasCamEditing` (0x0041D9C0) — was named
> `ScriptObject_IsRunning`. It does not test running: the running flag is the
> int16 at `+28`, which is what `Script_PlayScript` returns 2 off. What the
> function is for is visible at its only call site, in the `scx.play` handlers:
>
> ```asm
> call    sub_41D9C0          ; does this object own a camera editing?
> test    eax, eax
> jz      short loc_402D08    ; no  -> no camera request at all
> ...
> push    0Dh                 ; yes -> Camera_Request(13, ...), the travel cam
> call    sub_414BF0
> loc_402D08:
> mov     word ptr [esi+16h], 4   ; and either way, suspend the script
> ```
>
> So the guard means *"is starting this object about to take the camera
> over"* — which is exactly the question worth asking before requesting a
> camera move. Two shipped facts make the correction safe rather than another
> guess: the bytes are zero on disk everywhere, so nothing could read them as
> loaded state; and only **95 of 4511** objects ever receive a value, so the
> old reading would have called 4416 objects that demonstrably run "not
> running".

Of the 125 editings, **95 name an object and 30 ship unlinked** (authored and
never wired). No shipped object is the target of more than **one** — the
four-slot machinery is never exercised by the game's own data.
`verify.py: cutscene links`.

## 3. Why it plays without waiting for the player

The world script is the director, and the mechanism is one status word.

1. **`scx.play*.wait` suspends the script.** The `.wait` halves (ops 58, 60, 46)
   write **4** into the script context's `status` at `+22`, and `Script_Execute`
   loops only while that is 1. They also register the object in the 32-slot
   table at `dword_910500/4/8` against the script's own slot.
2. **A per-frame scan resumes it.** In the main tick:

   ```c
   for (slot = 0; slot < 32; slot++)
       if (pending[slot] && !ScriptObject_IsBusy(pending[slot])) {
           pending[slot] = 0;
           Game_RaiseEvent(3, slot);
       }
   ```
3. **Event 3 puts the status back.** `Game_HandleEvent` case 3 takes the context
   in that slot and, if its status is 4, writes 1 — the script resumes at the
   instruction after the `scx.play`.

Nothing in that loop consults input. The clock that ends a beat is the
animation program's, so the sequence advances on its own; a conversation in the
middle of one blocks on the player instead, because opcode 61 is a different
mechanism (`Script_Execute` returns outright after it).

The rest of the "take control away" work is done by ordinary opcodes:
`player.anim.hold` (647 sites) hands the player's body to the scene clips and
`player.anim.release` (618) gives it back; `zone.disable` closes the triggers;
`fade.to_black` covers the cut.

**How much of the game is blocking:** the `scx.play` family runs at **3025
sites, 2043 of them `.wait`** — 68%. **96 world scripts** carry five or more
`scx.play` calls, in 35 area or scene chunks; those are the directors.

## 4. A whole cutscene, start to finish

`python3 tools/script_dump.py SCENE 7 --rec 0` — the arrival in Pamoka prison,
43 instructions:

```
scene.unload         1               ; AREAS[1] 'Jaunpur' - drop the city
set.var.i8           270, case 255
inventory.transfer   0, 1, 270       ; confiscate what the player carries
zone.disable         701             ; ZONES[701] 'Arrivée Kayl'
player.anim.hold                     ; the scene clips own the body now
fade.to_black
fade.from_color      0, 0, 35, 0
scx.play.player.wait 151, 0          ; each beat blocks the SCRIPT, not the player
scx.play.player.wait 275, 0
character.show       243, 1
scx.play.actor.wait  243, 150, 0     ; CHARACTERS[243] - Mashroud
scx.play.wait        13, 0, 0
scx.play.actor.wait  243, 168, 0
scx.play.actor       243, 277, 0     ; left looping - need not finish
scx.play.wait        15, 0, 0
actor.goto_address   114             ; ADDRESSES[114] 'dialogue mash 1'
character.look_at_player 243
dialog.start         169             ; DIALOGS[169] 'Mashroud/Pamoka'
character.look_away  243
scx.play.actor.wait  243, 165, 0
...
dialog.start         170             ; DIALOGS[170] 'Jenna/Pamoka/Prisonniers'
inventory.add        2, 572          ; OBJECTS[572] 'Memo 041 Jenna Pamoka'
...
fade.from_black
player.anim.release                  ; give the body back
zone.enable          702             ; ZONES[702] 'Action Garde'
zone.enable          712             ; ZONES[712] 'cam gp ecuelle'
end
```

Read top to bottom that is a shooting script: strike the set, take the props,
take the body, fade, play the beats in order, cut to dialogue, fade, give
everything back. The camera never appears in it — `SPrison.SCX`'s twenty
editings are attached to the objects being started, so it follows by itself.

## 4b. The other family — world-camera cutscenes

**Not every cutscene is an `.SCX` camera editing**, and the ones that are not
were invisible to everything above, because everything above starts from
chunk 10. A world script can direct a sequence itself:

```
camera.set       151, 0, 3    ; cut to 'cam Bowie Flamme 1'
camera.set.wait  152, 110, 3  ; travel to 'cam Bowie Flamme 2' over 110 frames
music.play       3, 0, 1
media.play       741          ; OBJECTS[741] = 'ZVO G010 EIDOS'
```

The move model is the dialogue one — `camera.set X, 0` **cuts**, a following
`camera.set X, t` **travels** to it over `t` frames — so a script of these is a
shot list and the frames it names are its own clock. `.wait` is what holds the
script while the move runs, which is what makes the timeline add up.

The cameras come from the **world camera table** `Camera_FindWorld` scans:
44-byte records at `AREA +64` (count `+84`) / `SCENE +32` (`+52`) /
`GLOBAL +20` (`+30`), holding `eye[3] target[3]` as int32, then the id at
`+24`, mode at `+26`, roll and fov at `+28`/`+30` in 4096ths of a turn, and the
two **subject** fields at `+32`/`+34` — the same shape as a `DialogCamera`.
Those last two are why a quarter of the table is not a pair of world
coordinates at all: `-1` means the point is absolute, anything else makes it an
OFFSET from that actor, decided per point by `Camera_LoadParams`. 1440 of 5381
set at least one, and [FILE_FORMATS](FILE_FORMATS.md) carries the decode.
**And they are not a corner of the table.** Over every `camera.set` /
`camera.set.wait` site whose id resolves, the camera is actor-relative in
**1707 of 2852 cuts** and **715 of 1674 travels** — so the majority of the
game's scripted camera work is framed on an actor rather than on the world, and
a replica cannot point most of these cameras until it knows where its actors
are. The shot lists in this document are the absolute ones. Their coordinates are **raw**, in the units
`Global_Load` converts, and the data settles that: scaled, AREA 0's eyes span
x −276..16272, z −12139..2732 inside an `ANEKBAH` set of x −3399..16397,
z −12909..5147; unscaled they are 6.5× too big for the room. All **154** of the
cameras AREA 0 can see land inside the set they film.

**The roll field wraps, and that matters only when it moves.** Roll and fov are
4096-per-turn integers, and `Global_Load` converts them by 360/4096 *without*
wrapping — so a small negative roll is stored near 4096 and comes out as
~+359°. Standing still that is the same rotation and renders identically,
which is why it hides. Interpolate two of them and it stops being the same:
lerping +359° to 0° sweeps the long way, and the title sequence visibly span
its camera through complete turns at frames 3580 and 3840 where the game turns
a few degrees. **28 of AREA 0's 154 cameras** carry one of these. Wrapped to
(−180, 180] and interpolated along the short arc, the largest roll any camera
move in the game sweeps is **73°**, and 0 of 638 moves exceed 90°
(`verify.py: camera scripts`).

**And the SIGN flips when you leave the game's space.** The viewers map the
world with `W(v) = [x, -y, z]`, because Y points down in the data. Negating one
axis is a **reflection**, and a reflection reverses the sense of every rotation
about an axis — so a point survives the flip but an angle does not. Applied
unnegated in the flipped basis, the roll turns the camera the *opposite* way,
and every rolled shot came out mirrored. `/cutscene` negates it
(2026-08-29); `camshot.py --roll` renders in the game's own space, where the
record's sign is already right, and that is what settled it against a
screenshot.

It showed up in the title sequence and nowhere else for a reason worth
recording: **2725 of its 4370 frames carry a roll over 1°, up to 42.5°**, and
43 of AREA 0's 154 world cameras are rolled — where the dialogue cameras the
other viewer uses are unrolled on 1688 of 1923. `verify.py: camera roll`.

> The `.SCX` editings are not affected and their sampler is left alone: there
> roll is a float in the file, and **0** of the shipped key pairs jump more
> than 180°, so `Cam_PlayEditing`'s plain linear interpolation is right as
> transcribed.

**106 world scripts** direct one of these with six or more moves and at least
ten seconds of travel. The longest are city fly-throughs — `Jaunpur Zone 24`
at 338 s over 113 moves, `Lahoreh Konshu` at 288 s over 141 — and the one that
matters most is the game's own:

| | |
|---|---|
| **the title sequence** | `AREA 0` (Anekbah) record 78 |
| length | 4370 frames = **145.7 s**, 46 camera moves |
| music | `music.play 3` — and track 3 is **143.8 s**, the same length |
| voice | 20 cues: `EIDOS`, `QUANTIC`, `David Cage`, `TITRE`, `Loic`, `Chefs Projet`, `Codeurs`, `Music`, `Scripting IAM`, `Designs`, `Anims`, `Production`… |
| gated on | `VARIABLES[130] 'PSX'` and `ZONES[78] 'BOWIE OMIKRON THEME'` |

The camera names say what it is on their own: `cam Bowie Flamme`,
`cam Bowie Mirador`, `cam Bowie Lampadaire`, `cam Bowie Allée`,
`cam Bowie Passants`. It is the credits, flown through Anekbah over the Bowie
theme. `verify.py: camera scripts`.

## 5. Playing one back

```bash
python3 tools/cutscene.py                     # every shot in the game
python3 tools/cutscene.py Impasse.SCX intro   # one shot, resolved
python3 tools/cutscene.py --selftest

python3 tools/omkweb.py                       # then http://localhost:8752/cutscene
```

`tools/cutscene.py` joins the three halves — the editing, the object's program
and the set — and hands back everything a viewer needs: the camera at every
frame, the shot's tracks and keys, the program with its parameters resolved,
one entry per actor, and the sound cues with the frame each fires on.
`/cutscene` in `tools/omkweb.py` plays that in WebGL, with a transport, audio,
a shot timeline carrying the cues, and a free-look camera to step outside the
shot and see how it was staged.

Two things the page has to get right about **fractional frames**, because the
engine's clock is a float (`obj+88 += dt`) and the pre-sampled camera is not:

* it interpolates between the two neighbouring samples, which reproduces
  `Cam_PlayEditing` exactly inside a key segment since the function is
  piecewise linear in the frame;
* but never **across a track boundary**. Tracks are laid end to end and the
  next opens on a different camera, so that change is a *cut*: blending over
  it slides the camera through the cut for a frame. 92 boundaries, one of them
  fractional (`shoot.SCX`'s `e1` cuts at 157.5), all held hard.

**The transport keeps the audio with the picture.** Scrubbing, the arrow keys
and the loop wrapping all go through one `seeked()`: it silences what is
playing, rearms the cues so none of the ones you skipped retro-fires, then
restarts anything that would *still* be sounding at the new frame **from the
right offset into it** — seeking to frame 175 of `intro` resumes
`MVTTISS01.WAV` 0.17 s in and correctly leaves the 0.05 s footstep at frame
170 alone, because it has finished. The music is seeked to the same point:
locked to the shot's own `music.play` cue where there is one (the title
sequence is 145.7 s over a 143.8 s track and is meant to line up), and to
frame 0 otherwise, since an `.SCX` shot's bed belongs to the area rather than
the shot. A once-a-second nudge with a 0.12 s deadband holds the `<audio>`
element against the frame counter, which are different clocks and drift apart
over a two-minute sequence.

**The camera sampler is `Cam_PlayEditing` transcribed**, so the path drawn is
the path driven, and it self-checks: sampling every frame of every shipped shot
lands inside a key pair **24112 / 24112 times, all 125 shots**. That is a test
the data could fail — any other track layout (all starting at 0, or the
editing's duration as the span) opens gaps at once. `verify.py: cutscene
camera`.

**The port carries a second, independent transcription** —
`engine/src/o3de/camedit.cpp`'s `sampleCamEditing` — and the two agree on
**every one of the 24112 frames to 0.002** (float32 against float64), with 0
rows over 0.02; shifting the comparison by one frame fails 20351 of them, so
the agreement is a test and not a tautology (`verify.py: engine: cam mode 13`).
`omk-play` draws through it: walking the Impasse, the `intro` editing drives
329 of its 330 frames, then `sautdemon`, `demsuite`, `combkayl`, `demonout`,
`mecaspeak` and `kaylup` each take the camera in turn, each falling back to
world camera 0 when it ends — which is the engine's own cut back (SCRIPT_VM,
camera mode 13). Frame 90 of `intro`, **looked at**, is the alley between the
crates through an 88° lens, where every frame before this landed drew the
relative camera 0.

**Two limits, labelled rather than buried.** Roll is sampled and *not
applied*, because `RCamera` has none — the same gap the dialogue cameras have,
and it means `sautdemon` (opening at −28°) and `boxblow` (−18.5°) are drawn
upright. And the travel blend between the last drawn camera and the editing is
a **reconstruction** of `sub_414A90` plus the mode-13 tick, not a
transcription of the interpolation law; the shipped startup scripts exercise it
for at most one frame (travel 0 at 775 of 786 sites, 1 at the other 11), so
there is nothing in the data that could confirm or refute it.

### The sound

The scene's sounds are **embedded in the `.SCX` streamed section** — `gamedata/SOUNDS`
ships two files, and neither is one of them. They are plain RIFF/WAVE, 16-bit
PCM mono at 22050 Hz, exactly what `Sound_Play3D` hands to DirectSound, which
is why the viewer can play them without a decoder: **563 of them across the 29
cutscene scenes, all 563 starting with `RIFF`**. The stream walk that finds
them is the one `anim_3da.py` already lands exactly on the file size in 220/220
scenes, so the count is a byproduct of a checked walk.

**When each one fires is `Script_PlaySyncSound`'s param 1.** Its handler
(0x004A14D0) returns busy while `param1 > obj+88` — and `obj+88` is the
object's **program clock**, the same clock `Cam_PlayEditing` is sampled on. So
a cue's number is a frame on this page's own timeline, and a pending cue also
holds the function chain, which is how a beat waits for its sound.

The corpus agrees: of **475** sync cues, **467** fire inside their editing's
duration, and 5 of the remaining 8 are inside the actor's own clip — the
program simply outlives the camera (Yob keeps walking for 106 frames after a
31-frame shot). A field that was a volume or a radius could not do that.

> `Script_PlaySound` (0x004A12D0) does **not** share the layout: there param 1
> is a loop flag, param 2 the latch and param 3 the node, and the sound fires
> when the program counter reaches it. Reading the two alike invents cue times,
> which is what `verify.py: cutscene sound` guards.

The `intro` shot's cue list reads like the scene it is:

```
SCAEI_K1.WAV  @0      005.WAV @0
MVTTISS01.WAV @170    STPR.WAV @170     cloth, then the right foot
STPL.WAV      @200    STPL.WAV @210     the left, twice
STPR.WAV      @280
```

### Music, and the missing voices

Neither is in the cutscene data — a scene's object programs never touch either.

**Music** is the *area's*, and resolves: `AREA +142`, which `Area_Load`'s case
9 hands to `Music_PlayTrack` on arrival, covers **26 of the 29** scenes, and
`music.play` in the area's own scripts covers 2 more. All 28 name a shipped
`TRACKS\<n>.ADP`, which the same ADPCM decoder the dialogue voices use turns
into a WAV. The viewer runs it as a loop under the shot.

~~**The voices cannot be played from this tree, and that is a fact about the
data.**~~ **[WRONG - corrected 2026-09-02: the two numbers were right and the
conclusion was not; the handler substitutes a JINGLE for 520 of them. The
corrected reading follows.]**

**The voices are mostly PLACEHOLDERS, and that is a decision in the engine
rather than a gap in the tree.** `media.play` (op 92, handler `0x00404590`)
builds `<rec+14>.ADP` from the `IAM\OBJECT` record its `OBJECTS` operand
names, and then does one more thing before playing it: **if the name's first
four bytes are `ZVOT` or `ZVOP` it overwrites the first thirteen with the
constant at `0x004C0868`, which is `"JINGOFF3.ADP"`.** The compare is a raw
dword, so it is case-sensitive. That is **520 of the 561** ZVO objects, and
`gamedata/VOICEOFF/JINGOFF3.ADP` ships — 45898 mono samples, 2.08 s, peak 24889,
real audio and not silence. So the partition is **10 objects with their own
shipped file, 520 that play the jingle, and 31 that are genuinely silent**;
the earlier reading of this section had the first two numbers right and the
conclusion wrong. The Impasse cutscene's four voices all sound: 142
(`ZVOD001`, 3.36 s), 141 (`ZVOM010`, 17.52 s), and 404 / 410 (`ZVOT001` /
`ZVOT002`) as the jingle.

The play itself is not the sound bank. `sub_41B200` calls `Morph_Stop()`,
`Morph_SetAudioFormat(0x5640, 1, 30)` — 22080 Hz, **mono**, 30 fps — and
`Morph_Start("VOICEOFF\\<name>")`, so a voice-over runs through the same
streamer a `.3DM` dialogue line does, with no morph tracks. The leading
`Morph_Stop` means **one media voice at a time**: a second `media.play` cuts
the first.

Two arms of the same handler are not audio at all. A record of **kind 16** is
a DOCUMENT: the handler builds `IMAGES\<stem>.BMP`, loads it as a full-screen
bitmap and puts the player in `ACTOR_STATE` **10** (`ImageScreen`). And every
call shows a SUBTITLE from the record's `+280` description, prefixed `{C}`
when the player is in state 3 or 15. `traces/impasse-walk.log` exercises both
paths: object **715** ("ZVO G001 TITRE") is kind 16 and
`gamedata/IMAGES/zvog001.bmp` is on the disc.

`gamedata/VOICEOFF` holds 17 files. Ten are named by an object stem, `JINGOFF2` (the
document cue, `Game_HandleEvent` case 47) and `JINGOFF3` by the executable
itself, and **five are orphans** — `JINGOFF1.ADP`, `133205.ADP`,
`ZVOPG001.ADP`, `ZVOU001.ADP`, `ZVOp201.adp` — that no path the engine can
build reaches, the `pluie.wav` shape of §3b in `docs/ASSETS.md`.

`engine/src/audio/voiceover.{h,cpp}` ports all of this and
`verify.py: engine voice over` asserts it, including against the golden
traces' own `media.play` announcements. The dialogue voice remains a
different thing and is present: per-frame ADPCM inside each line's `.3DM`.

### What orders the beats — CLOSED (2026-08-29)

**A scene's beats are started by the SCENE chunk's own startup script, at
chunk offset `+4`** — a per-chunk script that the 5785-slot inventory never
enumerated, so every scan built on that inventory was blind to it.

Read from the code. `Scene_Load` (0x0040C120) relocates `+4` exactly like every
other pointer field in the header, and `Area_TickLoad` (0x0040C7E0) runs it —
once for the AREA block, once for the SCENE block:

```asm
mov  esi, dword_69BC40      ; the AREA block (then 69BC44, the SCENE)
mov  ecx, [esi+4]           ; <-- the startup script
push 0 / push 0 / push ecx / push ebp
call sub_406290             ; Script_NewContext(slot, script, 0, 0)
mov  [esi], eax             ; the context is stored at block +0
push 1 / push eax
call sub_4063D0             ; Script_QueueAction(ctx, 1)
```

which incidentally explains the header's `+0`: it is 0 on disk in every chunk
because it is where the running context goes.

**173 chunks carry one** (117 AREA, 56 SCENE; 157 have none), and the field is
self-checking in the §1 sense — a pointer that was really something else would
land mid-instruction — **173 of 173 disassemble clean, 0 failures**, 1968
instructions. `verify.py: startup scripts`.

For Impasse: the engine loads `SCENE 55` ("1-01 Impasse") over `AREA 222`
("Anekbah Impasse"), and SCENE 55's `+4` script, at offset **1212**, fires all
sixteen `scx.play*` in the order the object names always implied —

```
A_1_KaylArrives → A_2_KaylMoves → A_2_DemonLook → A_2_KaylSuite
→ A_2_DemonSuite → 2_Combat_Demon → 2_Combat_Kayl
→ KaylDemonAme(2) → KaylDemonAme → C_1_BoxMoves → C_1_MecaComes
→ C_1_KaylStand → C_2_KaylStand → C_2_MecaSpeaks
→ C_3_Meca Leaves → C_3_KaylsUp
```

— then sets `première impasse` / `Impasse Finie` and hands off with
`scene.load(237, 57)`, area "Anekbah Appart Kayl", scene "1-02 Appart Kayl
Rencontre". The whole chain in from a new game is **AREA 118**
("Introduction Kay'l") `+4`: the world-camera intro, conversation 272, then
`area.goto 222` and `scene.load(222, 55)`.

**Checked against the engine, not only read.** 19 of the 21 events SCENE 55's
script would announce appear in `traces/intro.log` **in order**, as trace
events 19 → 42. The two that do not are both explained: one is the untaken arm
of the script's own branch, and one is `loggable` naming op71's field 0 (the
area) where the handler logs field 1 (the scene) — the capture is the
authority there, and it shows the scene. `verify.py: impasse beats`.

This also subsumes the `intro script` finding: AREA 118's intro was located at
`+68` with a note that the empty subscription table's base "coincides with the
start of the code after it". It does — and `+4` is the field that actually
names it. For chunk 118 the two are the same number, 1040.

The name convention was never the mechanism; it agreed with it. And the "96
world scripts with five or more `scx.play` calls" were not the exception after
all — they are the same pattern, just in chunks whose driver happened to sit in
a slot the walk did enumerate.

### What orders the beats — the record of how it stayed open

A scene's shots are **one cutscene**, and the object names say so: `Impasse.SCX`
runs `A_1_KaylArrives` → `A_2_DemonLook` → `A_2_DemonSuite` → `C_1_BoxMoves` →
`C_2_MecaSpeaks` → `C_3_KaylsUp` → `2_Combat_Kayl` → `KaylDemonAme`, 2206
frames of camera in all, 73.5 seconds. The viewer sorts by that key and will
chain the shots back to back.

**But the naming is a convention, not a mechanism, and the mechanism is not in
the data.** What was ruled out:

* **the editing ids are not the order** — Impasse's `intro`, plainly first, is
  id 15 of 15;
* ~~**nothing but `scx.play*` can start a scene object.**~~ **This was wrong,
  and it was the load-bearing one** (corrected 2026-08-29, `verify.py:
  start-script graph`). `Script_StartScript` (0x0044A7E0) has **nine** call
  sites in nine distinct functions, not four, and `ScriptObject_Start` is
  reached from three places that are not the VM's action dispatcher:

  | route | what it is |
  |---|---|
  | `Area_Transition` (0x00408530) | a staged state machine — **3** sites, starting scene objects as it advances |
  | `Script_ProcessActions` | its 60-second transition watchdog |
  | `Shoot_StartTargetScripts` (was `sub_47BEF0`) | the shooting minigame, from `Shoot_TickPlayer` |

  and `Actor_StartPendingScx` is ticked by `Actors_TickAll`, starting whatever
  is parked at actor `+176` once `Morph_IsDone()` — a **deferred** start, which
  is precisely the shape a staggered cutscene needs. The `+176` slot was
  described here as "the object an `scx.play` parked", but that is an
  assumption about who writes it, not a traced fact.

  How the miscount happened is worth keeping: IDA emits a `proc` only where it
  found a prologue, so the VM's table-dispatched handlers fold into whichever
  proc precedes them — `sub_402B70` alone spans handler addresses
  0x402c30..0x406090. Counting callers through the decompiler's function list
  therefore collapses many handlers into one name *and* hides the ordinary
  functions among them. The same trap is already recorded in CLAUDE.md §1 for
  opcode 120's handler block; it bit again here, one level up, in a **caller
  count** rather than an operand length.

  So the conclusion this bullet supported — "either the mechanism is outside
  the data or the content is cut" — no longer follows.

  `Area_Transition` was read first and is now **decoded and excluded**: it is
  driven by opcode 47 `area.goto`, whose two extra fields are scene-object ids
  it starts as it advances (SCRIPT_VM, "Opcode 47"). 448 of 756 sites carry a
  pair, and they resolve in the **source** scene 416 to 84 — the exit is
  animated where the player still stands. But exactly one `area.goto` targets
  area 222 and it carries `-1, -1`, so this is not what fires Impasse.

  ~~**Still unread, and now the leading candidate:** `Actor_StartPendingScx`~~
  — **traced and excluded (2026-08-29).** Who writes actor `+176` is
  **`Morph_Play`** (0x0041AFC0), and nothing else. No `scx.play*` handler
  touches the slot.

  Counting every store to `+0B0h` in the binary and asking which is an actor:
  `Morph_Play` 2 (the two arms of one if/else), `Actor_LoadModel` 1 (zeroed at
  init beside `+172` and `+404`), `Actor_StartPendingScx` 1 (zeroed on
  consumption), and 16 in other structs entirely. The actor is not a typing
  assumption — `Morph_Play` builds the pointer as
  `lea ecx,[eax+eax*4]; lea esi,[eax+ecx*8]; shl esi,5`, which is
  `eax*41*32 = eax*1312` = `ACTOR_STRIDE`, added to `g_Actors`. The arithmetic
  names the struct.

  And what it does there is a **suspend**, not a launch. Starting a
  talking-head morph on an actor an SCX object is already driving
  (`ACTOR_STATE` 4):

  ```c
  if (Script_ObjectRunsForever(rec[43]))  rec[44] = rec[43];  /* park */
  else                                    rec[44] = 0;        /* drop */
  Scene_ResetObjectState(rec[43]);  rec[43] = 0;  state = 0;
  ...                       /* last statement of the function: */
  state = 5;
  ```

  `Actors_TickAll` dispatches state 5 straight to `Actor_StartPendingScx`,
  which waits for `Morph_IsDone()`, restarts what is parked, returns it to
  `[43]` and the actor to state 4. So `+176` is the save slot of a
  suspend/resume pair wrapped around a **spoken line** — `Morph_Play`'s caller
  is the dialogue UI, building `<line>.3dm` for `g_DialogSubjectActor`.

  That kills it as a beat-orderer, and for a structural reason rather than a
  corpus one: **nothing can be parked at `+176` that was not already
  running**, so the deferred start can only ever resume an interrupted actor,
  never start a beat that was not going. Exactly **one** instruction in the
  binary writes `ACTOR_STATE = 5`, so there is no second way into that state
  left to look for either. `verify.py: actor pending scx` pins the store sites
  out of the assembly.

  The gate on parking is the newly named `Script_ObjectRunsForever`
  (0x0044B460): true when the object's loop count `+52` is -1, or any
  sync-reachable function's repeat count `+16` is -1. A one-shot program that
  would have ended is dropped rather than resumed — which is why the else arm
  exists at all;
* **no world script fires Impasse's beats.** Restricting the scan to the
  chunks that could actually resolve against `Impasse.SCX` — area **222**,
  which names it at `+97`, plus any scene loaded over it — gives **0** sites
  against its 19 beat objects. (A naive scan over every chunk returns 8, all
  of them other scenes addressing their own objects: ids are small and
  collide, which is why the attribution matters. The mapping itself is sound:
  `handle >> 16` is the u16 at `+26` in 36 of 36 objects.)
* **the staggered-`Script_Wait` idea is dead** — exactly one of the 36 objects
  has a Wait at all.

~~So Impasse's opening plays in the game and nothing in `IAM\AREA`,
`IAM\SCENE` or `IAM\GLOBAL` starts it.~~ **Wrong, and wrong in an instructive
way: the conclusion was drawn from an inventory, and the inventory was
incomplete.** Every bullet above is individually correct — the ids are not the
order, no *slot* fires the beats, the Wait idea is dead, `area.goto` carries
`-1, -1`. What none of them could see is that the 5785 slots are not all the
code: the driver is the SCENE chunk's startup script at `+4`, above.

The lesson is worth keeping next to §1's. A negative result over a corpus is
only as strong as the enumeration behind it, and "no script does X" was really
"no script *I enumerate* does X". The tell was there in the golden trace all
along — it announced `SCENES 55` and `SCENES 57`, and **no op-71 site in the
5785 could emit either**. That unexplained residue was the evidence, and the
attribution notes even said so: "an event that NO slot can emit came from
outside the corpus".

Where a driver **does** exist, it is authoritative and already documented: the
prison arrival (§4) chains its beats with `scx.play*.wait`, and **96 world
scripts** carry five or more `scx.play` calls. Those are the real cutscene
directors; the name convention is what is left where one is missing.

### The actors, and how far they can be trusted

The characters come from the program's `Script_SelectBodyAnimation` calls,
which name a scene node and a clip. The rig follows from the node: every
character mesh is `<prefix><bone>`, so `UBassin` is a pelvis on the `U` rig,
which only `HO1_FN` and `HO1_FNM` carry.

Placement is two rules, both from the engine:

* **root key 0 is the authored placement** — the rest-sentinel slot, world
  scale in 740 of the 874 scene clips (`verify.py: scene clip roots`);
* **keys 1.. are per-frame deltas**, which is what `Anim_RootDelta`
  (0x004711D0) does with them: given a frame interval it *sums* the float[3]
  array over it and hands the total to `Actor_MoveBy`.

The corpus agrees with the code, and says so in a shape worth keeping.
`1-01KAY.3DA` is Kay'l walking into the alley, the longest travel in the
game's cutscenes. Read as deltas, his x climbs from 0 to ~117 over the first
forty frames and then **sits flat at 110–118 for the remaining 230** — he walks
in and stops. Read as absolute offsets the same numbers never leave ±2.3, and
he would never move at all. The plateau is the tell, and it is the opposite of
the `.3DM float[3]` trap in `CLAUDE.md` §1, where integrating a near-constant
vector produced runaway drift: here the integral *converges*.

Dropped onto the set floor — lowest vertex against the floor under the
centroid — the 64 distinct cutscene actors land at a **median 5.5 units**, 52
of them within 8. Summing the deltas is what earns that: without the
accumulation the median is 8.2 and only 32 are within 8.

**Settled by looking (2026-08-28).** Three measurements leaned toward the
conjugate without deciding it — the floor test at 5.5 units against 6.0, the
model's −Z axis toward its camera at 45/76, and the posed face mesh at 31/51 —
and per shot each convention appeared to win somewhere, which suggested the
answer might not be a single convention at all.

It is. Both `lev-4.SCX / leaboit` (where the metric favoured the conjugate,
+0.89 against −0.61) and `SPrison.SCX / gard1look` (where it favoured the
plain quaternion, −0.68 against +0.70) **look correct in the viewer**, which
uses the conjugate. So the apparent split was the *metric's* fault, not the
data's: "faces the camera filming them" is simply a bad prior — a guard in a
prison corridor is not looking at the lens, and no amount of averaging fixes
a prior that is wrong for a third of the corpus.

**The convention is the conjugate**, the one every other bone already reaches
the pose stream in. What the three statistics were really measuring was how
often a character happens to face their camera.

applied conjugated, because that is the convention every other bone reaches
the pose stream in (`ani_pose_stream` conjugates each track before composing,
and strips the root's precisely so a caller can apply it as a world
orientation). The floor test prefers it — median 5.5 against 6.0 for the plain
quaternion and 6.1 for no rotation, and it is the only one of the three that
finds floor under every actor — but the margin is modest and the test is weak
(actors sit, kneel and stand on stairs). Treat the staging as a reading with
support, not a result. The viewer says so on screen.

## 6. What is not established

* **`SCENE` chunks carry no scene-file stem.** `AREA +88`/`+97` do; the
  equivalent bytes in a `SCENE` chunk are not a name. A scene is loaded over a
  resident area, so it plausibly reuses the area's `.SCX` — plausibly is not
  established, and nothing here depends on it.
* **The two duration-0 editings** (of 125) never drive anything, since
  `Script_PlayScript` requires `clock < duration`. Authoring leftovers, most
  likely; not checked further.
* **Three of the 17 script function ids have no name** in the binary —
  `0x0400000C` (58 uses), `0x04000029` (59) and `0x0400001F` (3). All three are
  in the sprite range, so they are presentation, not staging.
* **The root quaternion's convention** — §5. The conjugate leads on the floor
  test but not decisively, and nothing else in the shipped data discriminates.
  A golden trace from the original (RECONSTRUCTION phase 6) would settle it in
  one run.
* **What `Anim_RootDelta`'s optional 3x3 is for.** It rotates the summed delta
  when its caller passes a matrix. The scene clips need it left alone — for
  `1-01KAY.3DA`, rotating by the root's own quaternion sinks the pelvis 70
  units over the walk and cuts the travel from 119 to 69 — so the matrix
  belongs to some other caller, presumably `.CTL` locomotion where the actor's
  own facing steers the step. Not traced.
