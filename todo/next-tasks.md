# The next tasks — a reader's list, with a first pass at ordering

Collected 2026-09-06 from a reader, features and bugs mixed and in no
particular order. This file adds a **quick** assessment per item — what the
repo already knows, what code exists, how hard it looks, and where I would put
it — so the list can be ordered. It is a triage, not a design: every "S/M/L"
here is a guess that the first hour of the actual work is allowed to overturn.

**Sizes**: S = a sitting, M = a slice with a plan file, L = several slices.
**Evidence** says how much of the answer is already in the tree, which is
usually a better predictor of cost than the size of the feature.

---

## Suggested order

The principle: **bugs with a known mechanism first** (they are cheap and each
one removes a wrong impression of the port), then **UI that the lifted tables
already describe**, then **systems that need fresh reverse-engineering**. Two
items are research and can be done any time they are wanted.

| # | task | size | evidence | why here |
|---|---|---|---|---|
| 1 | Enter held ≈ 0.5 s counts as several presses | **S** | strong | mechanism already documented; affects every screen and every conversation |
| 2 | black stripes entering/leaving a building | **DONE, WATCHED** | strong | 2026-09-07: the strip is CAMERA MODE, and camera mode is "he has no control" - not "the camera is not the follow camera". Areas that roam under a FIXED camera had the bars on for ever |
| 3 | ESC quits instead of opening the pause menu | **DONE, WATCHED** | strong | 2026-09-07: ESC is `Game_RunLoop`'s own `GetAsyncKeyState(27)`, not a binding, and the four item callbacks are four instructions each. `Quitter le jeu` is `Game_NewGame`, not an exit |
| 4 | tuto zone fires repeatedly, player not stopped | **M** | strong | zone lifecycle is read and there is already a check nearby |
| 5 | black frames in the Impasse cutscene | **FIXED, WATCHED** | strong | the camera should HOLD at the end of an editing, and a shot is as long as its editing |
| 20 | stuck on the last step of the bank's stairs | **FIXED** | measured | the capsule swept from the feet, so a 10.8-unit riser blocked him a sphere-radius short of the step; the sweep now starts a step-height up |
| 21 | a shop conversation's first camera is outside the shop | **M** | good | same family as item 5 and item 7 - what is resident when a script runs on ENTERING a building |
| 6 | street NPCs stop and T-pose | **M** | good | same family as the scene-facing work of 2026-09-05 |
| 7 | missing animations (lift doors, Kay'l's drawer) | **FIXED** | strong | the path is a DISPLACEMENT, not a position: `node = sample(t) - sample(t0) + anchor`. The flat's doors now slide 87.2 down, close behind you, and are AUDIBLE (gain 0.03 -> 1.00, the same fault reaching the 3D sound). The lift's were right by accident. Door COLLISION still to check |
| 8 | save support (save, save menu, load menu) | **M** | very strong | the format is solved end to end; this is plumbing, not research |
| 9 | main menu completed (new game correct, the rest) | **M** | very strong | the widget tree and the answer sites are lifted |
| 10 | sneak: character / info / config pages | **M** | very strong | the panels are already named constants in the port |
| 11 | shop UI | **M** | strong | 7 screens and their jump table are read |
| 12 | multiplan UI | **M** | fair | the tile background is read; the panel is not |
| 13 | any remaining UI screens | **M** | fair | 37 screens are enumerated; what is left is the tail |
| 14 | health: fall damage and vehicle hits | **M/L** | **weak** | nothing in the DB doc or the port; needs reading before estimating |
| 15 | jump / fall animation and physics | **L** | fair | the walker's slope and step rules are ported; the fall tiers are not |
| 16 | slider: call, ride, drive | **L** | good | **READ AND MOSTLY PORTED** 2026-09-07 - see `todo/slider.md`: the sneak's transport, the flight model, the ride machine and where a called one comes to. What is left is plumbing, not reading |
| 17 | fight mode | **L** | good | the AI profiles and combat block are read; nothing is wired |
| 18 | shoot mode | **L** | good | read, and deliberately unwired - a DECISION to revisit, not a gap |
| 19 | does the original filter (anti-aliasing, …)? | **research** | fair | cheap to answer, and the answer may be "no reachable tier" |
| 22 | the Telis cutscene in Kay'l's flat — and the conversation machinery behind it | **M** | **DONE** | reported 2026-09-06/07 over four rounds of play, twelve faults closed. The last four are the ones a teleport-in repro could never show: a scene program per actor, the camera move's fov and roll, the subject code as a RESOLVER kind (two of the four anchor on the head), and the player's channel ticking through a conversation. The transcan: its advert's only camera hangs off the speaker (`[2,2]`) and the port drew only absolute dialogue cameras; and `speakerReady` was gated on a camera solve that conversation could not make, so the line's morph never loaded. The waver in Telis's hand: **FIXED** (a set mesh on an ABSOLUTE path; parameter 5 picks the arm). Kay'l invisible: **FIXED** (a `scx.play.player` program poses the PLAYER'S ACTOR, and the viewer drew him only in adventure mode). The transcan camera: **FIXED** (the travel dropped the subjects). Open: the Gun Waver in her hands is not drawn, she faces the wrong way before the idle, and the bedroom mirror renders transparent instead of reflecting |

The **#** column is the item's id, not its position: 20 and 21 were added on
2026-09-06 and sit in the table where they belong rather than at the end.

---

## The items

### 1. Enter held for ~0.5 s counts as several presses — **FIXED 2026-09-06**

Was: one action per frame held (15 frames = 15 presses). Now one per
interaction, however long the button is down.

**It was never an input bug.** The port implements the engine's repeat mask
faithfully, the dialogue confirm already took edges, and the mask in the world
is correctly 0 - all three checked and all three innocent. The engine reads
DirectInput STATE and `sub_4A7A20` is a pure bit remap, so the ORIGINAL also
queues `MDACTION` every frame while Enter is held.

**The guard is `sub_465D30`'s tail**, `tab_special_move[3]` = 0x0046AEC0:

    SetPersoBankGroup(channel, Cef_FindGroupById(bank, dy <= 27.472441 ? 143 : 41))

It switches the actor into the take bank, where the same held bit means TAKE
rather than REACH — so the machine walks the interaction instead of repeating
the first step:

    MDACTION -> MDGETOBJ -> [H_WAITOB] -> MDPUTSNK -> MDSTAND -> (a new cycle)

Measured at area 237's zone 4051 with Enter held: 40 frames reaches and waits,
55 completes the take, 70+ completes it and begins a new cycle - which is what
holding SHOULD do. Press-release-press gives two activations. A press with
nothing in reach still retries every frame, which is also the engine (it never
reaches `sub_465D30`, so nothing switches).

Also ported: the handler's ACTOR_STATE switch, `byte_46B2BC[state-4] =
{0,4,4,4,4,4,4,1,4,2,3}` — states 4/11, 13 and 14 take other arms and are
refused with a log line — and the arm's own `ACTOR_STATE == 3` refusal.

**Still unported and labelled in the code**: the LOW arm. `sub_465D30` picks
group 143 when the object is within 27.472441 units (0.6978 m) of the actor's
height, and that needs the object's position which the press does not carry, so
a low object plays the standing take. And `sub_465D30`'s ADJUST - the walk to
0.600 m / 0.400 m with its 0.25 m settle and 1.25 m give-up - is not ported
either; the port acts where the player stands.

**Two false conclusions were published on the way, both from a broken
measurement**, and the tooling lesson is worth more than the fix. `--hold`
takes COMMA-separated runs and uses `+` only to join simultaneous keys within
one run: `k28*5+0*10+k28*60` parses as Enter for **five** frames. Every
"second press" test silently pressed nothing, which produced (a) "the bank
switch parks the player in H_WAITOB for ever" and (b) "the actor leaves
H_WAITOB only when the object is delivered". Both were wrong; the take
completes on the second press exactly as the `.CTL` says.

**The shop seller's dialogue, and the correction it forced (2026-09-06).**
The repeat itself was gone there, but a reader finishing the conversation
watched the whole TAKE GRAPH run on nothing: reach, wait, put back. Three
faults, all in this item:

1. **The bank switch was made on the wrong condition.** `sub_465D30` reaches
   `SetPersoBankGroup` only when MDACTION's object scan FOUND something - a
   press that merely activates a zone never gets there. The port switched into
   the take bank (group 41) on ANY successful press, so activating a talk zone
   installed `H_TAKL12` and played a take of nothing. The object case was
   already ported 300 lines above and installs 41/143/600 itself; the extra
   switch is removed, and one activation per press is now carried by the
   frontend gate (labelled as the reconstruction it is - the engine gets it
   from `Script_Pump`'s slot machine, which is not modelled to that depth).
2. **The viewer never entered or left DIALOGUE MODE.** `ActorRuntime` has
   carried `Actor_EnterDialogueMode` / `Actor_LeaveDialogueMode` since the
   ACTOR_STATE machine was ported and NOTHING called them, so the player's
   channel was never switched to group 400 and never switched back to 100.
   Wired into `omk-play` on the frame the Session's conversation opens and
   closes - the leave has to land on the SAME frame the dialogue ends, because
   the MDACTION gate runs earlier in the next one.
3. **`Perso_SetInputEnabled` is misnamed, and reading it as an enable killed
   the action button.** Flag 0x80 BLOCKS the device pass (`Cef_TickChannel`
   searches only under `!(flags & 0x81)`); the handler's arms are `or cl, 80h`
   for argument 1 and `and al, 7Fh` plus a reset for argument 0, and
   `Perso_GetInputEnabled` returns `flags & 0x80` precisely so the LEAVE can
   restore what the enter saved. `leaveDialogue` asserted it instead, and the
   player could never act again after any conversation. Renamed to
   `setInputBlocked` in the port so the sense cannot be misread twice.

And one transcription fix found on the way, which is real but did NOT cause
the symptom: **neither of the engine's two channel resets clears the 20-slot
latch.** `SetPersoBankGroup`'s `rep stosd` is 16 dwords from +28, ending at
+87; the latch is at +92. The port cleared it in `setBankGroup`,
`resetInputQueue` and `setInputBlocked`. Corrected; on H1AVNT it changes
nothing observable, because the action entry is reached by the per-tick chain
loop and a GoTo redirect and neither writes a latch id.

`verify.py: dialogue mode` pins the group table and the block flag either side
of a conversation; the mutation that asserts the flag instead of restoring it
takes `MDACTION after leaving` from 8 to 0.

### 2. Black stripes entering/leaving a building — **DONE 2026-09-07, CONFIRMED IN PLAY**

It was the letterbox, and the triage above was half right: the strip does
belong to camera mode. What was wrong is what the port took camera mode to BE.

**Reproduced.** Leaving Kay'l's flat is zone 24's activate script —
`player.anim.hold` / `fade.to_black` / `camera.set 4418, 0, 2` /
`scx.play.wait obj 0x8a` / `area.goto 229` — handing over to Hall 27, whose
own script leaves **absolute camera 4353** installed. `omk-play` letterboxed
whenever the camera was not the area's follow camera, so over that walk the
bars went on at frame 3 and never came off: by frame 400 `adventure` is 1 and
`animHeld` is 0 — the player has control — while `followCam` is still 0,
because a great many areas roam under a fixed camera.

**The captures decide the rule, and they refute the narrower reading too.**
Every letterboxed frame in `traces/frames` is one the player does not control,
and that includes a cutscene shot with a scripted world camera:

| capture | bands | what it is |
|---|---|---|
| `dlg402-32/35/38/41` | 64 / 64 | the conversation |
| `dlg402-44/47` | 64 / **32** | the same, with the line's SUBTITLE lighting the bottom band |
| `intro-75` | 64 / 65 | the intro CUTSCENE, on a scripted world camera |
| `menu-*`, `loadpanel-*`, `intro-42/48/60` | none | 2D interface |

So the strip is not "a conversation", and it is not "not the follow camera".
It is **he has no control**, and the fix is one term: `adventure` alone.

**And 64 rows is the engine's own band height.** `Screen_Fade`'s ticker draws
its two vignette quads `v3 = (HIWORD(g_ScreenSize) << 6) / 480` tall — 64 at
480, leaving 352, which is exactly what the captures measure. The letterbox
and the fade vignette are the same two bands, which is why the exit script's
`fade.to_black` darkens precisely them.

`verify.py: letterbox` asserts the five captures and the port's own walk out
of the flat; shown to fail by putting `followCam` back, which returns the
walked frame to 64/64 — the reader's stripes, to the row.

### 3. ESC quits instead of opening the pause menu — **DONE 2026-09-07, CONFIRMED IN PLAY**

Both parts. `docs/UI.md` §3h is the reading, `todo/omk-play.md` 82 the port
entry, `verify.py: engine: pause` the check (shown to fail three ways).

**ESC is not a binding.** `Game_RunLoop` (0x00439310) polls VK_ESCAPE with
`GetAsyncKeyState` one instruction before `Game_Frame`, level-triggered, and
the only debounce is `dword_4E9728` - the pause flag, whose two writes in the
image are screen 31's open and close callbacks. It calls `UI_LoadScreen`, not
`UI_OpenScreen`, so the screen answers nobody; and `UI_LoadScreen` refuses it
outright while a slot holds a screen carrying **0x20000400**, which is `OMK
START MENU` and `SAVE GAME` - the guard that keeps ESC from stacking a pause
menu on the boot's own screen.

**The items are four instructions each** and none of the four has a `proc`
label: `Reprendre le jeu` writes the screen's state word to 3 (closing),
`Quitter le jeu` installs the confirm panel 0x004E2730, whose `Oui` sets
`dword_4E6C9C` and whose `Non` installs the pause page back (it must, because
the confirm's parent is 0 and the back bit would close the screen). The
confirm's builder is `mov word_4E263A, 2; retn` - list 0x004E2638's selected
row - so it comes up on `Non`.

**`Quitter le jeu` does not quit the program**, which was the surprise:
`dword_4E6C9C` is a request served at the top of the next `Script_Pump(1)` as
`Script_Pump(3)` / `Game_NewGame` / `Screen_FadeFromColor(0xFFFFFF, 15, 0)`.
It ends the GAME and boots a new one, back out to the start menu. The port
ends the run instead and says so - `omk-play`'s boot is `main`'s body rather
than a function, so there is nothing to restart into.

**Three latent faults came out with it**, all the same shape - a rule written
as `adventure`, a per-frame MODE that any open screen takes false, where the
thing meant was "the world is live". The menu's animated CLOUD was painted
over every world-side screen (the reader's own screenshots of the original
show the save screen drawing over the live 3D scene, so this would have shown
there too); the pause was expressed as `adventure = false`, which stops the
player and leaves the crowd walking; and the PLAYER was not drawn at all while
a screen was up, which is the third time that test has been too narrow. The
pause flag's real mechanism is `frameSec = 0` - `Game_Tick` has no test for an
open screen anywhere in it.

**Two corrections to `docs/UI.md`.** §2 read flag 0x20000400 backwards
("opening either fires screen 31's open callback"); the branch says the
opposite, it is what makes 31 decline. And the widget lift now takes a panel
BUILDER's straight-line stores as well as the open callback's - which is what
gives the quit confirm its `Non` default - but only up to the first branch,
because nine of the eleven builders that write one of those fields write it
inside a conditional arm, and taking them all put the start menu's confirm
dialog on `Annuler`.

### 4. Tuto zone fires repeatedly and does not stop the player — M, strong

The zone lifecycle (enter / activate / leave, the 68-byte record, the save
bit) is read and there is a `tuto camera` check already. Two symptoms in one
report: the trigger re-arms, and the player is not halted. **The save bit is
the first thing to look at** — a zone that should fire once is normally made
one-shot by state, and firing several sounds and fades is what an un-cleared
bit looks like.

### 5. Black frames in the Impasse cutscene — **FIXED 2026-09-07, CONFIRMED IN PLAY**

The entry point was right and so was the guess: the black frame is the gap
between two editings. Measured on the intro path, it is **eight** gaps, every
one **0 of 480000 pixels lit**, seven of one frame and one of **73**.

**What the engine does at the end of an editing that the port did not: nothing
at all.** `Game_Frame`'s fall-back to the player camera is gated on
`byte_910322`, which is the `[Preferences]` key **`autocameraplayer`**, default
`"0"` and with no other writer in the image — so it never runs in a shipped
game. The mode stays 13, and the camera tick's mode-13 arm copies nothing when
the active camera is null, so **the view holds the editing's last frame**. The
port cut back to the Session's world camera, which through the whole cutscene
is still **2158** — AREA 118's, from the area the player just left.

**And a second fault underneath the long one:** a shot is as long as its
EDITING, not as long as its program. `Script_PlayScript` returns
`ediPlaying + busy`, so an object whose steps have run out keeps running until
the editing's duration expires; the port required the program to be running,
and `C_1_BoxMoves` lost 75 of its 185 frames.

Both fixed and both checked (`engine: editing hold`, `engine: frame hold`,
shown to fail); the readings are in `docs/CUTSCENES.md` §2 and the entry is
`todo/omk-play.md` 77. **A reader played it the same day and the black screen
is gone** — which is the confirmation this needed, since the post-fix headless
evidence is the repro and not the path the report came from.

### 6. Street NPCs stop walking and T-pose — M, good evidence

The reader's guess (end of their script) matches what 2026-09-05 found in the
same area: a scene actor is not placed before his first animation step runs,
and every step writes the Euler. A T-pose is the classic "no clip selected"
state, and `verify.py: played actors` already counts the cast.

Worth checking whether these are procedural WALKERS (`.OPT` sliders) or
authored extras (`scx.play.actor`) — the two have different owners and the
answer changes the fix entirely.

### 7. Missing animations — **REOPENED 2026-09-06: the apartment doors DO NOT animate**

Lift doors, the drawer in Kay'l's apartment.

> **The confirmation below was wrong for the apartment, and the way it was
> wrong is the useful part.** It rested on two things that cannot tell an
> animation from a mesh disappearing: `omk-play`'s `motion:` line, which is
> printed once when a pool first claims a mesh and says nothing about whether
> it then moves, and two stills taken from **two different camera positions**
> — 3900 and 3784 — whose difference I read as "the door opened". A reader
> watching the apartment reported no animation at all, and no door closing
> behind them either.
>
> **What actually happens.** `door_probe` samples the mesh every tick, and in
> the SESSION the door is animated correctly both ways: `Ap01Porte1` slides
> 632/−43.2 → 632/−130.4 over 27 frames with an ease-out, and on stepping out
> of the zone the leave script reverses it. The scripts, the zone lifecycle
> and the program player are all right.
>
> **The samples are in the wrong SPACE for this set**, and that is the bug:
>
> | set | mesh authored at | first motion sample |
> |---|---|---|
> | `AHALL40` (the lift) | 3947.6, −58.6, −1206.0 | 3949, −52, −1207 |
> | `AAPKAYL` (the flat) | 3759.9, 1037.3, −815.8 | **632, −43.2, 33.8** |
>
> Hall 40's samples ARE the set's world space, which is why its lift doors
> visibly slide and why item 7's lift half stands. The apartment's are not:
> the port offsets a mesh's corners by `sample − authored`, so this door is
> displaced about 3400 units instead of sliding a few. It vanishes on the
> first trigger and never returns — which is exactly "no animation" AND "the
> doors don't close any more", from one cause.
>
> **Open: why the space differs.** The likely shape is a path authored
> relative to the object's own node where the port takes it as absolute, but
> that is a guess and needs the `.3DP`/program read before anyone acts on it.
> A reader also reports **no collision against a closed door** — consistent
> with a door that is not where it is drawn, and worth re-checking once the
> space is right.

The rest of this entry stands as written: the drawer's script runs end to end
and the lift doors were watched opening in Anekbah Hall 40.

* **The doors animate.** They are `Open`/`Closed` scene-object pairs on a
  zone's *enter* and *leave* scripts — `Aapkayl.SCX` carries eleven such pairs
  (`PorteEnt1`, `PorteCui`, `PorteCha`, `PorteWC`, `Coffre`, `Placard`,
  `Console`, three `Tiroir`s). Standing in zone 4028 fires object 142
  `PorteEnt1Open`, and the audio path reports its motion.
* **The drawer animates.** Zone 4033's *activate* script is
  `actor.goto_address 685` → two `object.show` → `player.move.wait` →
  `scx.play.wait 130` (`TiroirCui1Open`), and the port runs it end to end: the
  player is put down at frame 18 and object 130 moves the kitchen mesh.

**The lift is CONFIRMED too, 2026-09-06 — and the paragraph that used to
stand here generalised from the hardest case in the game.** It said: *"The
lift doors that exist are gated: `LBibli.SCX`'s `PorteAscOpen` sits behind
zone 1700 Garde Ascenseur, which tests two variables, runs a `dialog.start`
with a guard and only then opens the door. Not reachable from a cold start."*
That is true of AREA 86's lift and of nothing much else.

**Counted instead of assumed**: across `IAM\AREA` and `IAM\SCENE`, **167**
scripts on zones named *ascenseur* play a scene object, over 43 chunks. **53**
sit behind a variable or a dialogue — and only **one** of those 53 is a
dialogue; the rest are variable tests. The other **114 are a bare `scx.play`
and `end`**, two instructions, no gate whatsoever. Anekbah Hall 40 (AREA 13)
has three of them side by side, one per lift, playing objects 11, 15 and 19.

**Watched in the port.** Standing in the centre lift's entry zone with
`--save traces/save-appart.bin --area 13 --stand 3923,-19,-1200,42`: the enter
script queues at `@1204`, `scx.play.wait` starts one program and misses none,
and both `HA40DoorL` and `HA40DoorR` are moved by the active pool. At frame 4
the doorway is a closed lit panel; by frame 120 it is an open shaft.

**Two traps cost the first attempt, and both are about the RECORDS.** An Entry
zone and its Exit zone are one doorway from two sides, and what separates them
is the **facing arc**, not the quad: of the 31 Entry/Exit pairs, 15 share a
quad to within 2 units, and in 30 of 31 the exit's `arcMid` is the entry's
reversed — a half turn, 2048, within 64 (within 128 for all 31). Standing
facing 180° armed zone 513, the EXIT, whose only script is a leave slot with
nothing to queue, and that reads exactly like a dead lift. The second trap is
underneath it: **`--stand`'s heading is in DEGREES and the arc is in 4096ths**,
so 42° is what `arcMid` 477 means.

`verify.py: engine: lift doors` pins the census, the three Hall 40 objects, the
Entry/Exit arc invariant and the port's own run.

**The third symptom was NOT a collider, 2026-09-06.** "There are no colliders,
I walk through the closed door and finish in the void" turned out to be a
CAMERA fault: the player is blocked at 3023.7 throughout (his run and four
teleport runs agree, at floor height, and the door puts six faces into the
narrow phase), and what fails is the picture. `Session::tickCamera` did not
copy `eyeSubject`/`atSubject` across a travel, so the flat's lift-door camera
arrived flagged relative and was resolved as an offset from the player — 3000
units outside the building, drawing black. A black screen in front of a door
you cannot pass reads exactly like walking into nothing. Fixed with the
travel's start also resolved to world; `verify.py: camera travel`.

**What this does NOT settle**: whether the lift then TRAVELS. The door opening
is a scene program on a trigger; riding one is item 16's territory
(`Slider_TickRide` and friends), and nothing here touches it.

**Two false diagnoses on the way, and both were tooling rather than the port.**
The first press landed at frame 2 while the zone armed at frame 4, so nothing
was ever activated — the `--hold` trap item 1 already documents, fixed with a
leading `0*15,`. Then the `ACTIVATE` log line turned out to be gated behind
`OMK_CAMLOG=1` while `ARM` printed unconditionally, so the default output
showed a zone arming and never activating, which reads exactly like a broken
activate script. That asymmetry is fixed: all four lifecycle events print
together or not at all.

### 8. Save support — M, very strong evidence

The one that is nearly free relative to its value. The format is solved end to
end (`GAME_STATE` §8, §8a): the slot geometry, the DB, the clock fields, the
3496-byte settings header, and `savefile.*` already reads slots and settings.
`tools/sim/ui.py` models the LOAD panel and the port walks it.

What is missing is the writing and the two menus, not the knowledge. Ranked
high because a port you cannot save is hard to test anything else in.

### 9. Main menu completed — M, very strong evidence

The start menu already ANSWERS FOR ITSELF in both the simulator and the port
(type a name, walk to Confirmer, the empty-name gate refuses). `ui_widgets`
carries the panels, items and answer sites. New game works but is "not
correct" per the reader — worth pinning what differs before building the rest.

### 10. Sneak: character / info / config pages — M, very strong evidence

`engine/src/ui/widgets.h` already names `kPanelSneakIdentity`,
`kPanelSneakMemory`, `kPanelSneakOptions`, the tab column, the shared row and
status lists and the page builders. The inventory page works. These three are
the same shape with different builders, so the second one should be much
cheaper than the first.

### 11. Shop UI — M, strong evidence

`docs/UI.md`: seven screens through one jump table (`Ui_OpenShop`, with case 4
falling through into 6), the titles naming their own screens 8 of 10, and the
inventory channel's buy/sell at half price already read. The data channel is
ported; the screens are not.

### 12. Multiplan UI — M, fair evidence

The tile-map background is read (and `ui page` asserts that MULTIPLAN's
lit-copy strip is black in the composed background, which is a real detail).
The panel behaviour is not. Nothing in `engine/` references it yet.

### 13. Any remaining UI — M, fair evidence

37 screens are enumerated with their bitmaps and text files resolving; 28 are
walked. This is the tail after 9-12, and should be re-scoped once those are
done because they will have built most of the machinery.

### 14. Health: fall damage and vehicle hits — M/L, **weak evidence**

The honest one: **nothing in `docs/GAME_STATE.md` or `gamestate.h` mentions
health at all**, so this is the item on the list whose size I trust least. The
reader is right that it needs careful reading first — where the value lives
(the 8192-byte DB? the actor record?), who decrements it, and what the fall
tiers and the vehicle collision actually raise.

Do the reading as its own step and re-estimate. Do not start by inventing a
health variable.

### 15. Jump / fall animation and physics — L, fair evidence

The walker is ported with the 30° slope limit and the 30 cm step, and the
"fall tiers" are named in `CLAUDE.md` §4 but not ported. Jumping is a
`tab_special_move` row and an `ACTOR_STATE`. Related to 14 (a fall that hurts
is a fall that landed), so the two may want doing together — read for 14
first, then decide.

### 16. Slider: call one from the sneak, transport, drive — L, good evidence

Already measured, which is why it is trusted: `todo/standing-unknowns.md` §5
puts `Slider_TickRide` at ~600 lines of undecompiled machinery
(`sub_4573E0` 387 lines, plus two more) with its own globals. Three facts are
already pinned — the ride runs at HALF SPEED, camera mode 8's subject is the
slider not the player, and the mount binds the player's `node+156` to the
slider's matrix. The sneak's slider page exists in the widget tree.

### 17. Fight mode — L, good evidence

The `.CTL` combat block is fully decoded (damage, hit window, reaction by id,
knockback), the four AI profiles are read (button combos injected into the
player's own input queue), `tab_special_move`'s 66 rows are lifted, and
`Fight_Begin` installs control group 3. Nothing is wired to a frontend.

Note the oracle problem: `traces/fight.log` proved combat CANNOT be captured
by the trace rig, because `fight.begin` announces nothing. So this will be
data-constrained rather than trace-verified, like the `.CTL` channel.

### 18. Shoot mode — L, good evidence, and a DECISION to revisit

Read: four callbacks by character type, Gandhar's three compiled behaviour
scripts, 302/3/1/0 over 306 sites. `todo/standing-unknowns.md` §2 records that
the brains were **deliberately not wired**, because the generic arm's choice
depends on navigation, line of sight and weapon range — none of which this
tree has — so driving a body from it would draw a deterministic first-edge
walk *as if* it were the game's behaviour.

If shoot mode is wanted, that decision is the thing to revisit first: it may
need the navigation data before it needs any code.

### 19. Does the original filter (anti-aliasing, …)? — research, fair evidence

Cheap to answer and worth answering early because it bounds what any later
render comparison can claim. Two threads already exist: the 1999 spec sheet
(`todo/engine-spec-1999.md`) lists *"Z-mapping (mapping exact)"*, which decodes
to `D3DRENDERSTATE_TEXTUREPERSPECTIVE`, and the renderstate census in that
audit shows which states the engine ever sets — filtering would be among them
if it were set at all.

`docs/PORTING.md` already rules that no pixel's VALUE is checkable against the
captures, so the likely honest outcome is "the engine sets these states, and
their effect has no reachable tier" — which is a real result and should be
recorded as one rather than left as a question.

### 20. Stuck on the last step of the bank's stairs — **FIXED 2026-09-07**

> **CONFIRMED IN PLAY 2026-09-07.** The reader ran the fixed build from the
> foot of the flight — launched standing at (4870, 1, −2577) facing up the
> stairs — and reported it good.

Reported 2026-09-06, reproduced and fixed 2026-09-07 once the reader placed it:
*"when you are at the bottom of the bank's stairs, try to climb them and enter
the bank: I was blocked by the very last step."*

**It is the ENTRANCE stairs, outside in Anekbah** — ten steps up from the
street to the bank's door — and my first day of work missed them entirely
because I enumerated staircases by mesh NAME. These are part of the building
mesh `Bat29`, not a mesh called `escalier`, so a name search does not see them.
That is the lesson of the item: the thing you are looking for need not be named
after itself.

**The reproduction.** Standing at the foot (4870, 1, −2577) and walking −x, the
player climbs nine steps of 10.25 units and then stops dead at **x 4604.6,
y −91.5** — the second-to-last tread — and never moves again in 300 frames. The
door is 10 units away. The last riser is **10.8** units, against a step limit
of 11.811 (30 cm), so the step rule allows it.

**The cause is the capsule, not the step rule.** The bare walker climbs the
same step cleanly (rise 10.35, `engine/tools/step_probe`); with the sweep on it
is `BLOCKED` at x 4605.06 — the same spot. Kay'l's four collision spheres hang
off his feet and the lowest has its **bottom exactly there** (centre 30.90,
radius 10.91; the pelvis-to-feet distance is 41.8), so every riser in the game
is inside that sphere. The sweep then stops him a sphere-radius short of the
step he is trying to climb, where the ground probe can never reach the tread
above. Nine risers of 10.25 squeak through and the one of 10.8 does not — a
knife-edge, which is why only the last step blocks.

**The fix**: the sweep starts a step-height above the feet, so the two halves
agree. `Walk_ProbeGround` already casts from `feet − kStepUp − 1`, and anything
inside that window is something the actor CLIMBS — it cannot also be something
he collides with. A wall taller than the limit still blocks: `engine: narrow
phase` still stops him 13.0 in front of one, and `engine: airlock walk` is
unchanged. He now climbs the ten steps, the door zone fires at frame 148, and
he walks into the bank.

**Labelled a RECONSTRUCTION**, because what the engine does here is not read:
`Sweep_ActorMove` and the 930-line `Sweep_PolygonKernel` were deliberately not
transcribed, so the sweep's own start height is unknown. What is known is that
the game climbs its own stairs, that its limit is 30 cm, and that a sweep
anchored at the feet cannot do both. Reading `Actor_Move`'s order — whether the
step-up runs before the sweep — would settle it.

**What the original does, read on the way** (`docs/ASSETS.md`): there is no
stair rule at all. The step refusal has **three** arms, not the two this port
had — `rise > 11.811 || cos(30°) > −n || (mesh flags & 0x20000000)` — and a
drop under **20 cm** (7.8740158, the fourth cm→inch constant) is silent, with
anything more taking the fall path at 3 m and 5 m. `Sweep_MeshTest` never
sweeps a mesh flagged `0x20000000` or `0x41`; that exclusion is now ported, and
the step arm is not, because it sits in a push-back branch and dropping those
meshes from the walkable floor would take 3606 of Lahoreh's 17658 floor
triangles.

**Still open**: the third refusal above, which needs the flag carried per
triangle into the step test.

### 21. A shop conversation's first camera is placed outside the shop — M, good evidence

Reported 2026-09-06, and the shape of the report is the useful part: in many
shops (the drugstore is the reader's example) the **first** attempt to talk to
the seller frames the camera far outside the building, looking at the whole
store from the void. Break off the conversation and speak to them again and
the camera is correct.

**Right the second time is the diagnosis.** Something the camera needs is not
yet resolved when the conversation opens the first time and is by the second,
so this is an initialisation-order bug and not a camera-maths bug. Two
candidate mechanisms, and they are distinguishable:

* **The set is not resident yet.** A dialogue camera with `subject[0] ==
  0xFFFF` is an absolute point in the **set's** coordinates
  (`engine/src/script/dialogue.h`), so a camera resolved while the shop's
  scene is still loading — or against the area the player walked in from,
  which `Area_LoadSet` keeps resident in the other slot — lands in the
  outdoor set's space. "Far outside the store" is what that looks like.
* **The speaker is not staged yet.** A camera whose `+32` names one of the two
  speakers resolves against that actor's position; an actor not yet placed
  sits at the origin or at his authored pre-load spot, and the camera follows
  him there. This is the same family as the 2026-09-05 finding that a scene
  actor's Euler must be written before his first step, and as the T-pose of
  item 6.

Read which of the two it is off the conversation's own camera records before
changing anything: if the drugstore's cameras are all absolute, the second
mechanism is ruled out in one command.

**Test it in real conditions — the reader asks for this explicitly, and the
repo has already been bitten by not doing it.** Reproduce by starting
*outside* the shop, walking in through the door, and talking to the seller —
not by standing in the shop with `--stand`, and not from a save taken inside.
Several faults here have turned out to be about what the scripts do on
**entering a building from the outside** (item 7's apartment doors, the
texture-cache substitution that depends on which neighbour is resident), and a
test that skips the entry cannot see them. The first attempt must be a genuine
first attempt: once the conversation has been opened once the bug is gone, so
a rerun in the same session proves nothing.

---

### 22. The Telis cutscene in Kay'l's flat — four faults, two fixed

Reported by a reader on 2026-09-06, from the first slot of `omk-saves/GAMES`:
after the goodbye scene, *"she's looking at the wrong direction then goes back
to the correct one for the idle, and Kay'l and the waver Telis has in her
hands are not visible"*, plus *"the mirror in the chamber is displayed with
transparency instead of reflecting"*.

Reproduce headlessly with the gate already open:

```
build/omk-play ../gamedata ../tables --save ../omk-saves/GAMES --slot 0 \
    --var 652=1,657=1 --give 0:42,0:3,1:3 --stand 3054,1071,-753,154 \
    --frames 220 --res 640x480
```

**Kay'l invisible — FIXED.** Op 46 is
`ScriptObject_StartOnActor(Actor_Player(), …)`, and that function binds the
object to `&g_Actors + 1312 * a1`'s node: a player program animates the
player's **own actor**, exactly as 59/60 animate the actor they name. The
viewer built `staged` from `Session::shown()` — which the player is not in,
because no placement record puts him anywhere — and drew him through the
adventure controller, which the same program suspends. So the program ran, the
editing flew, and the body it was posing belonged to nobody. He now joins the
staged bodies for the sixty frames of `HOCINE07.3DA` and is handed back at the
place the clip left him (x 3099) rather than the 3054 the walker still held.
`verify.py: engine: player program`.

**The transcan camera — FIXED** on 2026-09-06 by carrying the subjects
through a camera travel (`camera travel`); confirmed by the reader.

**Still open.**

* ~~**The Gun Waver in her hands is not drawn.**~~ **FIXED** — and it was
  never a prop. `OBJECTS[42]` *is* shown by the beat's `object.show 42`, at
  its own authored spot on the floor, and that is the copy the player has to
  pick up before the lift will open. The one in her hand is a **set mesh**,
  `Gunbl` in `AAPKAYL.3DO`, parked out of sight at 3635.9/1317.2/−682.9 and
  carried to her hand by `Script_MoveObjectOnPath` — whose two arms are
  selected by **parameter 5**, which the port was ignoring. See FILE_FORMATS,
  "`Script_MoveObjectOnPath`".
* ~~**She faces the wrong way and then snaps to the idle.**~~ **FIXED**,
  by `Morph_Play`'s own rule and not by a tuned yaw: a line plays in the
  frame the node was in when it started — the scene clip's root yaw plus the
  Euler — and holds it. Three readings were needed (engine/README, "A spoken
  line plays in the frame…"); each of the first two looked right on one
  scene and was caught by the transition on the other.
* ~~**The bedroom mirror renders transparent.**~~ **FIXED**, and the flags
  were a red herring: `AP_mirror`'s `0x00103000` is the mirror bit plus the
  additive pair, and the blend is the compositing operator OVER a reflection,
  not a substitute for one. The real cause is that `drawWithMirror` had only a
  single-`Geometry` form and only the `--scene` free-fly viewer ever called
  it, so the game path drew the pane and nothing under it. The plane was
  already being read per set and used for nothing but a log line.

