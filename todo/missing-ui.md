# The missing UI — next-tasks 13, "any remaining UI screens"

Opened 2026-09-17 on the reader's order, who named two: *"the lift in the
security center or the fight training machine"*. They are SCREEN 4 `LIFT` and
SCREEN 11 `FIGHT SIM` of the 37.

## 1. Where every screen is opened from, measured

`ui.open` (opcode 70) is the only way in, and the corpus has **236 sites** over
AREA, SCENE and GLOBAL. By screen:

| screen | sites | where |
|---|---|---|
| 0 VIDEOPHONE | 10 | AREA 41, SCENE 39.. |
| 2 MULTIPLAN | 82 | done 2026-09-15 |
| **4 LIFT** | **18** | **all in AREA 157, the Anekbah security centre** |
| 5 TERMINAL | 1 | AREA 179 |
| **11 FIGHT SIM** | **1** | **AREA 237 record 23 - the training machine** |
| 12 GANDHAR DOOR | 1 | AREA 81 |
| 13 DEN | 1 | SCENE 43 |
| 14 XACHEN | 1 | AREA 58 |
| 15/16/17 SURV | 1 each | SCENE 51, all three from one record |
| 18 ARCHIVES | 1 | AREA 180 |
| 19 MORGUE | 1 | AREA 35 |
| 20..28, 32 the shops | 3..40 | done 2026-09-15 |
| 30 SAVE | 37 | done |
| 36 HIGH-SCORE | 1 | AREA 59 |

So the tail is **ten screens with one or two sites each**, plus the LIFT's 18.
Nothing opens screens 1, 3, 6, 8, 10 (the ELIMINE ones), 7 (the slider, opened
by the sneak), 9 (the sneak, `UI_OpenScreen(9)` from the special move), 29/31
(the menus), 33/34/35 (the shoot HUDs and options, `UI_LoadScreen` direct).

## 2. The LIFT — it opened all along, and could not work. FIXED

The screen was never the problem: the port opens any screen the widget tree
has, and `screen 4 is asking` appeared at the first press. What did not happen
was the ARRIVAL.

**AREA 157's lift zones stack over one footprint**, one pair per level - ids
2510/2511 at y -18, 2512/2513 at y 207, and so on up the shaft (`zone_quads
<gamedata> 157 area`). The port scanned every live zone by its quad, which is
XZ only - and that is true of the engine too: `Zone_ContainsPoint` (0x0048C880)
takes the y and never reads it. **The filter is the ITERATOR**:
`Actor_ScanZones` seeds `sub_431D40` with the actor's (x, y, z) and it builds a
box of `+88` in all three axes (`a3 - v15 .. a3 + v15` for y), so only the
zones at his height are ever yielded.

Without it, one press of the action button activated FIVE lift scripts -
`[ctx]` trace, frame 83: contexts 1, 4, 5, 6 and 7 all reaching op 70. Each
parked on its own `ui.open 4`; the Session holds ONE pending screen, so the
answer went to the last of them and the lift the player stood in stayed parked
for ever. The screen reopening a few seconds later is what a player sees.

**Note what is NOT the fault**: the engine activates every armed slot too -
`Script_Pump` case 2 runs the whole 16-slot loop and queues an activate for
each. It is the ARMING that differs, not the pump.

`ZoneRegistry::scanZones` now takes the quad's own y extent plus one metre.
**RECONSTRUCTION, labelled**: the box's radius is a float the zone-space record
carries and this has not read it. The corpus says the choice is not delicate -
over all 4558 zones the quad's y spread is 0.0 at the median and 23.8 at the
99th, while zones sharing a footprint at different heights sit a median 389.7
apart (130 to 225 in this shaft), so anything from a few units to ~60 separates
the levels identically.

Measured after: one screen, `Etage` = 6, and case 6 walks him from y -12 to
y -453, 'Asc CS Lev1'. Floors 1 and 2 enter AREAS 178 and 179. `verify.py:
engine: lift`, shown to fail by dropping the band. The zone family and
`trace agreement` - the golden captures of real play - are green with it.

### 2b. The description box under the grid — FIXED

A reader, the same evening: *"when a level is hovered, the names of the
people's office and others section of the level should be displayed"*.

Screen 4's list 1 is ONE text item, 475x105 at (15, 360) in font 67, whose text
comes from a native callback - `textFn` **0x004B01C0**. The composer draws an
item's own string or nothing (`screendraw.cpp`: `run_` from the caller's rows,
else `kTextFnString`), so the box was empty.

What belongs in it is `IAM\Lift`, and the file is exactly the seven slots in
order, matching the grid's string ids 0..6:

| slot | answer | the box |
|---|---|---|
| 0 | 6 | `Niveau 1 : Bureau du commandant Gandhar` |
| 1 | 0 | `Niveau 0 : Entrée principale` |
| 2 | 1 | `Niveau -1 : Bureaux des agents-enquêteurs` + Tarek 511, Boog 710, Shamet 337, Vode'm 457.. |
| 3 | 2 | `Niveau -2 :` + **Kay'l 669**, Den 415, Maar 516, Sork 121.. |
| 4 | 3 | `Niveau -3 : Cellules de détention / Salles des archives` |
| 5 | 4 | `Niveau -4 : Salle de surveillance / Bureau du capitaine Léa` |
| 6 | 5 | `Niveau -5 : Atelier de maintenance / Salle des aérateurs` |

Each carries the game's own markup - `{fC}` for the face and `{I045175045}`
per agent, which the port's text layer already draws. The viewer supplies the
selected slot's string for that item and the box follows the grid.

**LABELLED**: 0x004B01C0's body is not transcribed. It builds its string
through the inventory channel (`sub_4083F0` with event 0x24 on a {slot, 7}
block, then `sub_4767E0` into a buffer); this takes the file's string directly,
which is what that produces for all seven shipped slots.

## 3. The TERMINAL FAMILY — seven screens, one keypad. WORKING

Screens 5 `TERMINAL`, 11 `FIGHT SIM`, 19 `MORGUE`, 18 `ARCHIVES` and 15/16/17
`SURV` share panel `0x004E4108`: a 3x3 keypad with a `0` cell and a big button
(eleven items, list hook `sub_4AF300`), a 430x320 display, a one-line header and
two buttons. `docs/UI.md` had already read its ANSWER table ("a second
`Ui_OpenShop`"); what was missing was everything that makes it usable.

**Two faults, and neither was the screen.** Both were found by opening the
terminal in Kay'l's office (AREA 179, zone 3030) and photographing it: the
artwork drew - keypad, screen, the `<<<` / `>>>` buttons - with nothing on the
display.

1. **The display was blank.** Its text comes from a native `textFn`,
   `0x004AF5D0`, and the composer draws an item's own string or nothing. Read
   from the image, that function is two state queries and then a SEVEN-CASE
   jump table on the screen's fixed parameter, each arm naming one string of
   the screen's own text file:

   | case | screen | file | string |
   |---|---|---|---|
   | 0 | TERMINAL | `IAM\Term` | 12 - "Dossiers agents Kay'l 669 / Den 415" and the five crimes |
   | 1 | FIGHT SIM | `IAM\Fsim` | 5 - "Simulateur de combat WX2600, Modèle ELITE" |
   | 2 | MORGUE | `IAM\Morg` | 6 |
   | 3 | ARCHIVES | `IAM\Arch` | 5 |
   | 4 | SURV ERROR | `IAM\Surv` | 1 |
   | 5 | SURV NO KIT | `IAM\Surv` | 3 |
   | 6 | SURV KIT | `IAM\Surv` | 2 |

   **Not ported, labelled**: the two query branches (string 11 for the terminal
   and 4 for the simulator when `sub_42B5E0(6)`/`sub_42B5F0` is true, 6 for the
   simulator on the same pair over 5) - an object lookup this has not read. The
   default arm is what a first visit takes.

2. **The keypad refused every press**, because an unmodelled list hook makes
   the walk refuse input - so the screen opened and nothing could be chosen.
   `sub_4AF300` transcribed from the image (no `proc` label):

       UP     10 -> 9; 9 -> 7; nothing on the top row (n/3 == 0); else -3
       DOWN   n/3 == 2 -> 9; 9 -> 10; 10 -> nothing; else +3
       LEFT   n >= 9 nothing; else row*3 + (n + 2) % 3   (wraps in the row)
       RIGHT  n >= 9 nothing; else row*3 + (n + 1) % 3

   It returns 1 only when the selection MOVED, so a confirm falls through to
   `Ui_ConfirmSelection` and the item's callback - `0x004AF410`, whose table is
   `docs/UI.md`'s: FIGHT SIM `row + 1` (rows 0..2), MORGUE `row + 1` (0..4),
   ARCHIVES 1 from row 2, SURV KIT 1 from row 0, SURV ERROR falling through
   into it, SURV NO KIT nothing.

**The TERMINAL answers on the way OUT**, alone of the seven. `sub_4AF0E0`, read
from the image:

    if (dword_68A600) answer = 2 + (dword_68A5FC != 0);
    else              answer =     (dword_68A5FC != 0);

so it reports which protected dossier was opened - 0 neither, 1 the fifth, 2
the fourth, 3 both - and AREA 179's script wants 1 for `1-A-CS SecretFile` (the
voice-over, `DATA MEMORIZED` and memo 003) with a second branch on 2. Which row
sets which global rests on `docs/UI.md`'s reading of case 0, not on a fresh
transcription: `asmfn.py` snaps to the neighbouring function at that address
(CLAUDE.md 1's trap), and the raw-byte read was spent on `0x004AF0E0` and
`0x004AF5D0` instead. Labelled as such in the source.

**Measured**: the terminal lists Kay'l's dossiers, the keypad walks, closing
after dossier 4 answers 2 and the script plays *ZVO P315 DATA MEMORIZED* with
its subtitle. The simulator names the machine, its first cell answers 1, and
`fight.begin` runs - CHARACTERS 331, banks H1CMBT, the fight HUD and the fight
camera. `verify.py: engine: terminal family`, shown to fail by putting the
keypad's hook back among the unmodelled ones.

**What a terminal entry UNLOCKS**, which is the reason to read one at all (a
reader: *"some scenes can be triggered only if an entry on a terminal has been
read"*). AREA 179's script, after the answer:

| answer | dossier | what it does |
|---|---|---|
| 1 | the fifth | `1-A-CS SecretFile`; if `1-A-CS Archives` is still 0, the voice-over *ZVO P251 Terminal Kay'l*, *DATA MEMORIZED* and **memo 003 "Trouver Dossier Kay'l"** |
| 2 | the fourth | if `Dossier Bar` is still 0: sets it and `Mission Bar 56`, *DATA MEMORIZED*, **memo 056** and `address.enable 33` - **'Anekbah - Bar Zone 52'** |
| 3 | both | both arms |

So reading a dossier OPENS A PLACE. Ops 87/88 write a 791-bit map that nothing
announced, so the viewer now watches it and says each change - `ADDRESS 33
ENABLED` is in the check, and any play log will show what a screen unlocked.

**Still open here**: the header (`textFn` 0x004AF5A0) is
`table_at_0x004E3FEC[word_4E3FE2]` - a runtime pointer array, so which label it
shows per selection is unread; and the body does not yet switch to the chosen
dossier (`IAM\Term` 0..4), which is the same index. The five "Consulter le
dossier n°N" strings (5..9) and "Quitter le terminal" (10) belong to one of
those two.

## 5. The special screens — surveyed, not yet built

Asked for by name (*"did you look at the special UI too, like Den's locker,
Gandhar door"*). Every one is blocked the same two ways the terminal was: a
list hook the walk does not model (which makes it refuse EVERY press) and/or an
item callback that writes the answer. None has been opened yet; this is the
survey, from `tables/ui_widgets.json`:

| screen | panel | list hook | items | item callback | state |
|---|---|---|---|---|---|
| 12 `GANDHAR DOOR` | 0x4E4CB8 | 0x004AFE90 **PORTED** | 5 | 0x004AFF90 **PORTED** | done - see below |
| 13 `DEN` | 0x4E4990 | 0x004AFBE0 **PORTED** | 6 | none | done - see 5c; four digit wheels and two lamps, the hook itself answers |
| 14 `XACHEN` | 0x4E4620 | 0x0042A930 (`kMoveSelectionLR`, ported) + a 4-item list | 4 | **0x004AF9D0** | the mover is already modelled; only the callback is missing |
| 0 `VIDEOPHONE` | 0x4DF128 | none | 8 | 0x0049DBF0, `textFn` 0x0049E090 | the SNEAK family's - 0x49E090 is already supplied by the viewer |
| 36 `HIGH-SCORE` | 0x4E22F0 | none | 1 | 0x0042A990 | one item, the family's generic button |

Two of the six are done (12 and 13, below). What is left is three callbacks -
0x004AF9D0 (XACHEN), 0x0049DBF0 (VIDEOPHONE) and 0x0042A990 (HIGH-SCORE) - all
in the same page of the image the terminal's came from, and none of them needs
a new list hook.

### 5b. GANDHAR'S DOOR — done 2026-09-18

Both of its functions read from the raw image (neither has a `proc` label, and
`asmfn.py` was checked against the bytes first):

* **`sub_4AFE90`, the list hook.** The screen is a **6x6 grid** and the FIVE
  items are one selectable cursor plus four markers. UP/DOWN/LEFT/RIGHT step
  the cell inside 0..5; the cursor's x/y are then rewritten `col * 63 + 135`
  and `row * 63 + 61` - the item's own authored (135, 61) IS the grid's origin -
  and its `+3C` becomes `(row << 16) | col`. It returns 1 only when the cell
  moved, so a confirm falls through to the item's callback.
* **`sub_4AFF90`, the press.** It counts the press (`byte_68A608`), takes the
  next marker widget from `off_4E4C80[count]` and places it at the cell, and ORs
  one bit of `byte_68A60C` for four cells:

  | `+3C` | cell | bit |
  |---|---|---|
  | 5 | row 0, col 5 | 1 |
  | 0x10003 | row 1, col 3 | 2 |
  | 0x40002 | row 4, col 2 | 4 |
  | 0x50004 | row 5, col 4 | 8 |

  Four presses play interface sound 0x26; a mask of **0x0F** writes the ANSWER
  **1** and plays 0x27. The bits are ORed, so the symbols may be pressed in any
  order and the same one twice does not count twice.

Reached in one command - AREA 81's `Interface` address, zone 1659:

    build/omk-play ../gamedata ../tables --save ../traces/save-appart.bin \
        --area 81 --address 263

`verify.py: engine: gandhar door` walks the four cells and asserts the answer,
shown to fail by dropping one symbol from the code (the mask stops at three and
nothing answers).

**The composer gained `setItemMove`** for this: a widget tree carries an item's
AUTHORED place, and this hook moves its cursor every step, so the mover has to
say where it went. The four MARKER widgets are ported with it - the press
records the cell, the viewer places the marker there, and `UiWalk::itemShown`
is what clears the record's not-drawn bit the way `sub_428FF0(marker,
0x40000001, 0)` does. NOT ported, labelled: the two interface sounds.

### 5c. DEN'S LOCKER — done 2026-09-18

One hook, `sub_4AFBE0`, read from the raw image (no `proc` label). It both
moves and answers, because screen 13 has no item callback anywhere:

* **UP/DOWN** spin the wheel under the hand, wrapping (`0 -> 9` up, `9 -> 0`
  down). **LEFT/RIGHT** move the hand within 0..3 - and they move it by
  `dec`/`inc word ptr [esi+2]`, which is **the LIST'S OWN SELECTION**, not a
  field of the hook's.
* A wheel SHOWS its digit through its **unlit source**: the tail writes
  `digit * 46` into `+0x12` (`lea edx,[edi+edi*2]; shl edx,3; sub edx,edi;
  shl edx,1`) and the digit itself into `+0x3C`. The strip of ten figures is
  at **x = 0** in `DEN00.BMP`, 46 apart; the four wheels' authored unlit
  source is (0, 0), digit zero.
* Beside the move it sets `sub_428FF0(item, 0x40000084, 0)` on the wheel the
  hand leaves and `(.., 1)` on the one it lands on. Bank B `0x4` is
  `Ui_Oscillator(1)`, a 500 ms square wave, so **the wheel under the hand
  blinks**.
* **The combination is compiled in**: `dword_4E47E4 == 7 && dword_4E482C == 2
  && dword_4E4874 == 1 && dword_4E48BC == 3`, which are the `+3C` of the four
  wheel items (0x4E47A8, 0x4E47F0, 0x4E4838, 0x4E4880, 0x48 apart). So it is
  **7 2 1 3**, it opens the moment the last wheel lands, and there is no
  confirm. The success arm sets `0x40000004` and clears `0x40000080` on all
  four, paints them `+8 = 0`, `+9 = 255`, plays interface sound 0x22 and
  writes the ANSWER **1**.

**The route**, and it took finding: SCENE 43 over AREA 146 ('Anekbah Appart
Den'), record 4 = zone **2417 'Cache'**. The chunk's OWN startup script
disables 2417 unless `VARIABLES[482] 'Cache Trouvee'` is 1 - so `--zone-enable`
alone does nothing, and the first three attempts armed the neighbouring zone
2419 'Tableau Coffre', which shares the footprint and is what the story
disables on the way in:

    build/omk-play ../gamedata ../tables --save ../traces/save-appart.bin \
        --area 146 --scene-chunk 43 --var 482=1 \
        --stand 522,-10,343,178 --zone-disable 2419

On the answer the zone's script shows OBJECTS 13 `Cassette Den`, 104
`Pass Ventilos` and 103 `Plan Ventilos`, sets `Cache Ouverte` and `Blocage K7`
and swaps the lift zones - which is the whole point of the locker, and is what
the check asserts beside the answer.

**A fault the counts could not see, found by rendering a frame.** The first
version kept the hand in a private field, so the list's selection stayed at 0,
`Ui_DrawItemSprite`'s last rung ("lit = SELECTED") drew the FIRST wheel from
its own empty place in the artwork for ever, and the display showed three
figures and a gap while the log said `7 2 1 2`. The log line was derived from
what the viewer HANDED the composer, which is exactly the trap CLAUDE.md 1
names. `ScreenFrame::spriteSrc` now reports the source rect each asked-about
sprite was actually blitted from, and the line is printed from that.

NOT ported, labelled: the two interface sounds and the 2000 ms oscillator 5 the
success arm starts on the screen - in the engine the four wheels blink green
for two seconds before it closes, and this port closes at once.

`verify.py: engine: den locker`, shown to fail twice: with the combination
changed to 7 2 1 4 nothing answers and the cache stays shut, and with the hand
no longer written to the list's selection six frames draw a blank wheel that is
not the one under the hand.

## 4. The steps

| step | what | state |
|---|---|---|
| 0 | where every screen is opened from | **done 2026-09-17** - §1 |
| 1 | the LIFT: why it never arrived | **done 2026-09-17** - §2, `verify.py: engine: lift` |
| 2 | the terminal FAMILY: the display, the keypad and the answers - 7 screens | **done 2026-09-17** - §3, `verify.py: engine: terminal family` |
| 3 | the SPECIAL screens: **12 GANDHAR DOOR done** (§5b), **13 DEN'S LOCKER done** (§5c); 14 XACHEN, 0 VIDEOPHONE, 36 HIGH-SCORE left, and the terminal's own dossier pages | §5 has each one's hooks |
| 4 | play | |

## 6. The lift arrives and the level is not drawn — the mechanism, and a REVERTED patch

Reported in play, 2026-09-17: *"there is an issue with the camera inside it,
which is not placed correctly so a part of the environment is just in front of
the camera (it is not the only place where this issue occurs)"*, then *"this is
a static camera, not the following one"*.

**It is not the camera, and the camera the port picks is right.** Riding to
level -2 and photographing the arrival gives a frame 98% black. The camera up is
2986 - AREA 179's own, at level -2's height - and with the zone height band
REMOVED the port picks 2999 instead, which is level -5's, three storeys away. So
the band (section 2) is doing its job here.

**The mechanism, measured.** The log:

    [slot] frame 179  SHOW area 179 in slot 1   (active slot 0 = area 157 ...)
    [slot] frame 180  HIDE area 179 in slot 1

`area.arrive -1` hides the row that is NOT active, which is the engine's own
line (`if (dword_69BC60) hide(slot0) else hide(slot1)`). The ACTIVE row only
flips on event 9 - the player's feet on a new decor - and the port raises that
from a per-frame probe that runs AFTER the script pump. So the destination is
shown and hidden with no probe in between, the active row never moves, and the
row that goes is the one just loaded.

He IS standing on the destination's floor when it happens: feet at y 342.5 over
ACSLEV-2's 345. One probe between the show and the hide would flip the row and
both readings of the engine's line would name AREA 157, the shaft, which is what
should go.

**A patch that hid `Transition::outArea` instead was WRONG and is reverted.** It
made the arrival draw - but it takes away the shaft the player is standing in,
floor included, so he arrives somewhere he cannot walk: 400 frames of forward
input moved him 0.1 units. The reader caught it in one sentence - *"the issue
doesn't happen when I tested before ... this is a very recent regression"* - and
the code says so at the line.

**The fix is the ORDERING**, and it is not attempted here because it moves a
rule every transition check depends on: the feet probe has to see a set that was
shown this frame, either by running before the pump or by probing again after a
`showSet`. The engine's own transition takes frames to reach state 8 (the staged
load), which is what gives its probe the chance.

**Repro**, one command and 260 frames - the frame is 98% dark:

    build/omk-play ../gamedata ../tables --save ../traces/save-appart.bin \
        --area 157 --address 446 --frames 260 --nofmv --nodelay --no-crowd \
        --hold 'k*40,k28*2,k*60,k208*2,k*30,k28*2,k*200' --dump out.bin

`OMK_NO_ZONE_BAND=1` turns the height band off for the same run, which is how
the band was ruled out (the arrival then picks the wrong camera and is 66%
dark).

## 7. The dialogue camera in the ceiling — READ, and three attempts REVERTED

A reader, at the Telis lunch (2026-09-17/18): *"a similar issue occurs in the
telis dialog in the restaurant"*, then the two facts that decide it - *"the
camera is high at that moment in the original too, but in the original game,
there is nothing hiding the scene to the camera"*.

**What is established.**

* The camera is authored high. Dialog 387's reply pair **4194 -> 4195** is a
  CRANE: eye 226 units up (5.7 m) descending to 152, targets 110 down to 37.
  Both are ABSOLUTE (subject -1), so nothing is being mis-resolved - `dlgcam`
  now prints eye, target and subjects, which is how this was settled.
* The port places it exactly there and moves it exactly as the engine does:
  `Dialog_ApplyLineCameras` cuts to the first id (duration -1) and travels to
  the second over **160 frames**, which `docs/` already carried.
* So the difference is that the engine CLEARS the lens and the port does not.
  `sub_414520` case 12 -> `sub_4141F0` sets camera flags **0xC**, and bit 8 is
  the obstruction pass `sub_417070` (`player.h` has always recorded it as
  unported). Its rule, read from `04_sys.c` 3370ff:

      d    = eye - target;  len = |d|
      ray  = target .. target + d * (+300)          // +300 = 1.2, the swim 1.5
      hit? -> dist = |hit - target| / +300
              if (dist > +328 && !(flags & 1))      // eases OUT over +320 = 8
                  dist = (dist - +328) * dt / +320 + +328
              +328 = dist;  eye = target + normalise(d) * dist

  with a hit on a 0x20000000 mesh ignored when the camera carries 0x1000 (the
  "camera sees through" pair; no mesh of `ARESTO14` carries it).

**Three attempts to port it, all reverted the same evening.** Each was measured
and each was worse in play:

1. the rule on every camera: cleared the ceiling (the reader confirmed *"the
   issue is not there anymore"*) but *"some other cameras in the dialog are
   broken now (wrong position or translation, very sudden moves)"*;
2. `+328` held in ONE variable across cameras - so a CUT to a new pair inherited
   the previous pull and slid out of it, which is those sudden moves. Keyed per
   camera afterwards;
3. scoped to the cameras whose setup sets bit 8 (follow and dialogue): *"not
   resolved (the camera now is completely wrong)"*.

**So the reading stands and the port does not.** What is missing is not the ray
- that part is easy and is what the swim camera already uses - but the rest of
`sub_417070`: it is 271 lines, and the parts NOT read are the ones that decide
where the lens goes when the pull alone will not clear it (`+312/+316`, the
0.7 x height push, and the second ray from the PREVIOUS camera position that
the `+208` state machine runs). A third of that function is what makes the other
two thirds safe, and porting the ray alone moves every shot it touches.

Next time: read `sub_417070` whole - all 271 lines, `+208`'s three states
included - before writing any of it, and drive it from a scripted replay of one
conversation so every shot's eye can be diffed frame by frame against the same
run without the rule.
