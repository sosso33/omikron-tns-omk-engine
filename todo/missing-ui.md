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
| 14 `XACHEN` | 0x4E4620 | 0x0042A930 (`kMoveSelectionLR`, ported) + a 4-item list | 4 | 0x004AF9D0 **PORTED** | done - see 5d; Dakobah's four cartridges |
| 0 `VIDEOPHONE` | 0x4DF128 | none | 8 | 0x0049DBF0, `textFn` 0x0049E090 | the SNEAK family's - 0x49E090 is already supplied by the viewer |
| 36 `HIGH-SCORE` | 0x4E22F0 | 0x004ADA80 **PORTED** | 1 | 0x0042A990 (the family's generic button) | done - see 5e; the one item's DRAW hook 0x004ADAD0 is the screen |

Four of the six are done (12, 13, 14 and 36, below). What is left is the
VIDEOPHONE (0x0049DBF0, the SNEAK family's).

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

### 5d. XACHEN — Dakobah's CARTRIDGES, done 2026-09-18

`sub_4AF9D0` read from the raw image (no `proc` label). Screen 14 is the panel
in front of Xendar's door: four BUTTONS on the ordinary left/right mover
(`0x0042A930`, already ported) under four SYMBOL widgets in a list flagged
`0x20000004`, so nothing can select those.

* A button's `+3C` is a **pointer to its symbol** - `dword_4E43BC =
  0x4E44D0` and its three neighbours, written by the screen's own OPEN
  callback and not by the record - and the symbol's own `+3C` is its value.
* A press steps that value along `dword_4E42E8`, a fourteen-entry **ring that
  is not in numerical order**: `7, 11, 1, 8, 3, 5, 12, 2, 4, 10, 14, 13, 6, 9`.
  A value the ring does not hold gives index -1, so the press lands on entry 0.
* It then writes that symbol's 51x23 cell of `Xanoir1.bmp` into the widget's
  **LIT** source (`+0x0C`/`+0x0E`), and the widget carries bank B `0x8`, always
  lit, so that is what draws. The cells are `word_4E42A8[value * 4]`, three
  columns at x = 488 / 539 / 590 and five rows 23 apart; values 0 and 15 are
  (0, 0), which is why the hook refuses anything outside 1..14.
* The open callback sets the four symbols to **1, 2, 3, 4** whatever the
  record says (it ships 7, 8, 11, 3), clears `0x40000008` on the four buttons
  and puts the selection on the first.
* `unk_4E4320` is the code: **10, 14, 7, 9**. On it the hook gives every button
  lit source (0, 23) with `0x40000008` set - the lamps come on - plays
  interface sound 0x2B, writes the ANSWER **1** and starts a 4000 ms
  oscillator 5 on the screen.

From the opening 1, 2, 3, 4 the ring makes the code **7, 3, 10 and 5 presses**.

**The route**: AREA 58's zone 1130 'Cartouches', enabled by the chunk's own
startup script on `2-BE Rencontre Dakobah == 1 && 2-BE Porte Xendar Ouv == 0`:

    build/omk-play ../gamedata ../tables --save ../traces/save-appart.bin \
        --area 58 --var 28=1,57=0 --stand -1471,150,133,290

On the answer the script teleports the player, enables ZONES 1129 'Porte
Xendar' and plays DIALOGS 227 'Dakobah/Xendar'.

**The composer gained `setItemLitSource`** for this, beside the `setItemSource`
Den's locker needed. The two screens write different halves of one record -
Den moves `+0x12`, the unlit y, on a wheel that draws UNLIT; XACHEN moves
`+0x0C`/`+0x0E` on a symbol that draws LIT - and one map cannot serve both
without guessing which field a caller meant.

NOT ported, labelled: the sound and the 4000 ms timer.

`verify.py: engine: xachen`, shown to fail by putting the ring in numerical
order: the same twenty-five presses walk to 8, 5, 13, 9 and nothing answers.

### 5e. The HIGH-SCORE table — done 2026-09-18, and it found where the scores live

Screen 36 is the shooting gallery's board: ONE 400x480 item at (120, 0) and two
native hooks, both read from the raw image.

* **`sub_4ADA80`, the panel hook**, moves no selection at all. LEFT and RIGHT
  step the SCREEN'S OWN `+4` parameter through 0..3, wrapping, and both arms
  return 1 - so the screen has four pages and nothing else to walk.
* **`sub_4ADAD0`, the item's draw hook**, is the whole screen. The pen starts
  at the item's scaled place; every box runs to `pen + I2D_ScaleX(w)` and
  `+ I2D_ScaleX(h)` (X for the height too, which is what 0x0049C2B0 does as
  well). Then: `y += ScaleY(50)` twice and the title, `IAM\HScore` string 3
  ("MEILLEURS SCORES"); `y += ScaleY(30)` and the page heading, string 6 on
  page 0 ("Tous les niveaux terminés !") and otherwise
  `sprintf("%s %d", string 5, page)` ("Niveau 1"); `y += ScaleY(50)` and five
  rows `ScaleY(30)` apart, `"%d.- %s"` LEFT in the left half of the box and
  `"%d'%02d\"%02d"` RIGHT in the right half.

**And the rows are in the SAVE HEADER.** The hook bases them at
`ds:90E454h + param * 180`, stride 36, and `0x90E454` is `byte_90E180 + 724` -
the 3496-byte settings block. Four pages of five, a 32-byte name and the time
in MILLISECONDS at `+0x20`, `4 x 5 x 36 = 720` landing exactly on +1444, the
next field `savefile.h` already carried. `sub_42B8E0` splits the milliseconds
into minutes / seconds / hundredths, the printf's order.

So **the range's scores travel with the OPTIONS**, one copy for all 256 slots,
not with a game. `docs/GAME_STATE.md` §8a had those 720 bytes down as "never
written and never non-zero", which was a fact about the two shipped captures
and not about the field - the §1 shape exactly.

**The route:**

    build/omk-play ../gamedata ../tables --save ../traces/save-appart.bin \
        --area 59 --stand 5412,15199,-3546,1

AREA 59's zone 1183, in front of the board. `ui.open 36, -1, -1` - the screen
answers nothing, it is a display.

The world drawn behind it is CORRECT and already explained: the screen record
says hide, but `Ui_DrawPanelDim` turns it back on for a panel carrying bank B
`0x800`, and this panel is one of the five that do.

NOT ported, labelled: the engine's `test ecx, ecx` before each row can never
fail (`ecx` is the record's ADDRESS, a fixed global plus an offset), so all
five rows always draw and an empty table is five numbered blanks - transcribed
as written rather than "fixed"; and the last line, behind the
`sub_42B5E0(0)` / `sub_42B5F0` pair this project has not read (the same pair
that gates two arms of the terminal's body), whose text is strings 0 and 1,
*"Félicitations ! Votre score a été homologué."* and its refusal.

`verify.py: engine: high score` WRITES a copy of `traces/save-appart.bin` with
five names and times on page 1 and reads them back off the drawn screen - so
the base, the stride and the `+0x20` are each testable rather than asserted.
Shown to fail by moving the base one record (724 -> 760): the five names shift
by one row and the fifth goes blank.

### 5f. `Lire plan` — the SNEAK'S CITY MAP, done 2026-09-18

Not on the survey table above, because it is not a SCREEN: it is a PANEL of
screen 9 that nothing in `tables/ui_widgets.json` reached. The full read is
`docs/UI.md` §3g-bis; the short form:

* item `0x004DE3C8`, the third 50x50 tile, callback `0x0049BC40` =
  `sub_42A370(screen, off_4DF190)`. `off_4DF190` has **two** references in the
  54 MB listing - that push and its own definition - so no `+44` names it;
  `exetables.py`'s `CODE_NAMED` lifts it now, as the Inventaire page's second
  code-installed child. Its neighbour `0x0049BC30` on the ANNEAUX tile is
  `mov eax, 1; retn` and is INERT in the original; the port transcribes it as
  inert rather than "implementing" it.
* the panel's `+4` `sub_49D9E0` builds `Images\<resident set>.bmp` from the
  decor node's own path and **bounces back to the Inventaire page when the
  `fopen` fails**, which is every location but the four cities.
* two compiled tables lifted to `tables/city_maps.json`: four map rectangles
  (`0x004DF1F8`, stride 52) and fifteen per-place position overrides
  (`0x004DF2C8`, stride 44 - three places in five languages).
* three draw hooks, of which the port already had one: `0x00477CA0` blits the
  bitmap, `0x0049E6F0` draws the player's blue heading ARROW and a red triangle
  per enabled destination of that city, each labelled, and `0x00477ED0` is the
  interference box.

Ported as `engine/src/ui/citymap.*` plus two arms in the composer and one in
the walk; `verify.py: engine: sneak map`. **Deliberately not reproduced and
labelled in the port**: `sub_40E630`, which the marker loop falls back on, is
the TRANSPORT and `Area_Load`s a non-resident area - the port resolves a marker
only against the resident chunk's address table and reports any it drops (0 in
the shipped data, because every destination of a city carries that city's area
id). Watched: Anekbah's map draws with its street plan, the scanlines, the two
enabled markers on their streets and the arrow where the player stands.

### 3b. The terminal's HEADER BAR — done 2026-09-18

The last of the terminal family's gaps, and eleven bytes of code. `unk_4E3FE0`
is the **keypad's own list** - `db 0Bh` at +0 is its eleven items,
`word_4E3FE2` at +2 its selection, `sub_4AF300` at +4 its hook and
`off_4E3FEC` at +0x0C its item array - so `textFn` 0x004AF5A0 is

    movsx ecx, word_4E3FE2 ; mov edx, off_4E3FEC
    ... sub_476860(screen, [edx+ecx*4], out)

which hands the GENERIC string callback the keypad item **the cursor is on**,
in place of the header's own. The 430x17 bar at (40, 28) is therefore the
label of the highlighted cell, and it is the same shape as the lift's
description box: a widget whose text belongs to another widget.

The labels are the ones each screen's open callback binds (`item+28`, lifted
as `bind.string`): TERMINAL 5..9 on the first five cells and 10 on the big
button, FIGHT SIM 0..2, ARCHIVES 0..3, MORGUE 0..4 - and the three SURV
screens bind none at all, so on those the bar is rightly empty. Kay'l's
terminal now reads *"Consulter le dossier n°1"* under the cursor.

Asserted inside `verify.py: engine: terminal family` (two cells, so a bar that
never changed would satisfy neither), shown to fail by cutting the header off
the composer.

## 4. The steps

| step | what | state |
|---|---|---|
| 0 | where every screen is opened from | **done 2026-09-17** - §1 |
| 1 | the LIFT: why it never arrived | **done 2026-09-17** - §2, `verify.py: engine: lift` |
| 2 | the terminal FAMILY: the display, the keypad and the answers - 7 screens | **done 2026-09-17** - §3, `verify.py: engine: terminal family` |
| 3 | the SPECIAL screens: **12 GANDHAR DOOR** (§5b), **13 DEN'S LOCKER** (§5c), **14 XACHEN** (§5d), **36 HIGH-SCORE** (§5e) and the terminal's **header bar** (§3b) all done; 0 VIDEOPHONE left | §5 has each one's hooks |
| 4 | play | |

## 6. The lift arrives and the level is not drawn — the SLOT reading REFUTED, and the real occluder

Reported in play, 2026-09-17: *"there is an issue with the camera inside it,
which is not placed correctly so a part of the environment is just in front of
the camera (it is not the only place where this issue occurs)"*, then *"this is
a static camera, not the following one"*, and later that **the lift door does
not open at arrival**.

**The reader's own first sentence was right and the diagnosis written here on
2026-09-17 was wrong.** It is the camera, and a single mesh in front of it. The
slot bookkeeping — `area.arrive -1` hiding the row that has just been loaded —
is real, is still there, and has **nothing to do with the black frame**. What
follows replaces the whole of the previous section; the old reading is kept
only where it is refuted, because both of its premises looked solid.

### 6a. What was measured, 2026-09-18

Every number below is from the repro at the end of this section, `omk-play`
headless under `SDL_VIDEODRIVER=dummy`, "dark" being pixels whose R+G+B ≤ 24
out of 640×480.

| what was changed at `area.arrive -1` | arrival frame | player walks, 300 frames of forward |
|---|---|---|
| nothing — the port as it stands (hides AREA 179) | **96.7% dark** | **377.7 units**, (5752.0, 342.5, −11485.0) → (6041.8, 346.5, −11242.9) |
| hide NOTHING — both sets drawn all the way through | **96.7% dark** | 377.7 units, same end point |
| hide `Transition::outArea` — the REVERTED patch | 82.3% dark | **0.0 units** |
| the port, with the ONE mesh `CSPont04` displaced | **16.9% dark** | — |

So: **the hide is not the cause.** Drawing both sets changes the arrival frame
by nothing at all — the same 96.7%, the same picture. And the reverted patch
never really "made the arrival draw" either: 82.3% is still a black frame, and
it strands the player, which is why a reader caught it in one sentence.

### 6b. The occluder is `CSPont04` — the lift car

`ACSpuits.3DO` mesh 36, flags `00000000` (drawable: `flags & 0x800043` is 0),
492 corners, bounding box **x 5670..5798, y 228..346, z −11570..−11442** — a
closed box 128 units square whose floor is at y 346 and whose ceiling is at
228, i.e. the LIFT CAR at level −2. Camera 2986's eye (5708, 259, −11484) and
the player (5752, 342.5, −11485) are both **inside it**, so the whole lens is
the inside of the car.

Displacing that one mesh takes the arrival from 96.7% to **16.9% dark**.
Displacing `CSPorte79h` (the level −2 lift door), `CSPorte80h` (its twin) or
`CSNivo-2a` (the landing) instead leaves it at 96.7% each — so the door is not
what blocks the view, and neither is the landing.

**And it is not only the arrival.** The `CSPont` meshes are a stack of eight,
one per level (`CSPont02` y 547.2 … `CSPont06b` y −303.1), pairing off against
the `CSNivo-N` landings. At level 0 camera 2978 (eye 5706, −92, −11488) sits
inside `CSPont06a` in exactly the same way, and the frames before the ride are
37.5% / 38.2% dark showing the **same** repeated chevron wall as the arrival.
One phenomenon at every level of the shaft; the arrival is just where it is
total. That is the reader's *"it is not the only place where this issue
occurs"*.

**What the engine does about it is NOT settled** and is the next thing to read.
Three candidates, none tested: it culls the box's faces from inside (but
`ASSETS` 4b records `D3DCULL_NONE` on at least one path); it hides the car that
is not in use; or `sub_417070` pulls the camera eye in to the first hit, which
is gated on camera flag `+356 & 8` (`04_sys.c` 3800, the only call site) and
has never been read for a scripted world camera. Do not guess between them.

### 6c. The ordering fix cannot work — the premise is false

The previous section said *"He IS standing on the destination's floor when it
happens: feet at y 342.5 over ACSLEV-2's 345 … one probe between the show and
the hide would flip the row"*. **Measured at exactly that frame, with both
decors resident, `decorUnder` answers 157.** ACSLEV-2 has **no walkable floor
under him at all**: over a 13×13 grid at 120-unit spacing (±720 units) around
the arrival point, its 986-triangle walkable soup gives no floor in his own
column at any height.

The reason is structural and settles the question for good: **`ACSlev-2.3DO`
is 71 meshes of FURNITURE AND DOORS ONLY** — `CSOTable*`, `CSOseat*`,
`CSOffic*`, `CSOmulti*`, `CSPorte*` — with no floor and no walls anywhere in
it. The structure of the whole security centre (the landings `CSNivo-N*`, the
lift cars `CSPont*`, the shaft doors `CSPorte??h`, 99 meshes) is in
`ACSpuits.3DO`, which is **AREA 157's** set. So the player stands on AREA
157's decor on every level of the building, and no probe, at any moment and in
any order, can ever raise event 9 for AREA 179. Running the feet probe before
the pump, or again after a `showSet`, would change nothing.

The engine's own line was re-read in the raw assembly rather than the
decompiler (`0x004087B5`): `cmp dword_69BC60, ebx / jnz loc_4087C4 / push
dword_69BC58 … loc_4087C4: push dword_69BC48`. It hides the NON-active slot,
which is always the slot `area.goto` loaded into (`Area_LoadIntoSlot(1 - a2,
a5)` with `a2 = dword_69BC60`), so only event 9 can ever make it name the
outgoing area. The port's reading is right. And the ORDER is right too:
`Game_Tick` runs `Script_PlayAllScripts` per decor slot and `Actors_TickAll`
after it, so the engine's probe sits between the show and the arrive exactly
as the port's does — one frame, in both.

**What is left open here**: with event 9 unable to name 179, the port never
draws level −2's furniture at all, at any time — `area.arrive -1` hides it one
frame after it is shown and nothing shows it again. That is a second, separate
symptom of the same fact. Settling it means re-reading the engine's event-9
raiser at the SCENE-ROOT level (`21_d3d.c` ≈3700 and `19_dsound.c` ≈1810: the
test is the scene root of the surface the ground probe hit, against the actor
node's own parent, and the decor slot must be in state 2), not the feet.

### 6d. The lift DOOR does open — and two port faults beside it

AREA 157 record 56 slot +0 is the level −2 arrival script and it runs in full:
`camera.set 2986, 0, 2` / `zone.enable 2526` / `zone.enable 2559` /
`scx.play.wait obj 0x3e`. Object 62 drives mesh **`CSPorte79h` 87 units up,
y 299.2 → 212.1, over frames 177–215**, resolved through
`Program::NodeMotion::placeOn` (the sample is a displacement off the mesh's
authored position — reading `mo.pos` raw reports the sample, not the place).

So "the door does not open" is not literally true of the port's model. Two
real faults were found beside it, and **neither reaches the picture** — the
arrival frame is 96.7% dark at 205, 216, 225 and 260 frames alike, i.e. with
the door at y ≈218, ≈212, ≈212 and back at 299:

* the door is driven from the **OUTGOING** pool from frame 178 on, because
  `showSet(179)` moves `curSlot_` and `finishScene()` makes `lev-2.SCX` the
  resident scene while `ACSPUITS` is still the shown set. It works only
  because `play.cpp` ticks both pools.
* ~~when the program ends at frame 215 the motion patch is **dropped** and the
  mesh **snaps back** to its authored, closed position~~ — **the engine half is
  right and the port half is REFUTED, measured 2026-09-18.** The engine's
  `Script_MoveObjectOnPath` does end in `o3de_SetNodePos` and does leave the
  node where it put it (confirmed in `readable/src/23_script.c` and in the raw
  listing at `0x0046F400`: the tail is `call sub_4370A0`, `call sub_437160`,
  the loop counter, `retn`, with no restore). But the port does **not** snap
  back on screen. The frontend's patch writes `baseCorners -> geo.corners` only
  for the meshes it patches and nothing rewrites the rest, so a mesh whose
  program has ended simply keeps the last corners written. Probed through
  `OMK_MESH_AT=CSPorte79h`, which reads the centroid of the vertex buffer the
  frame hands the renderer: the door goes y 313.6 -> 226.6 over frames 178-214
  and is **still at 226.6 at frame 258**, 43 frames after its program stopped.
  Hall 40's `HA40DoorL` behaves the same over 260 frames. The snap-back was
  inferred from `SceneRunner::motions()` being refilled every tick — which is
  true — and never measured at the geometry.

  **What was really wrong is narrower and is now fixed.** The port had no
  representation of the node's position at all: the drawn set was right by
  OMISSION. Where the omission does not save it is a mesh that is also
  SCALED — `nodeScales()` persists, so such a mesh kept a patch entry with no
  motion in it, and `at = mp` re-placed it at its **authored** origin every
  frame from then on. **35 shipped mesh names are both scaled and moved by
  their scene.** `SceneRunner::placements()` is now the node's position as
  state, the frontend's patch is built from it, and
  `verify.py: engine: node rest` pins it. The corpus: of 2461 objects that
  move a node, 2315 end, **1277 of those leave a node displaced** — and
  **1239 of the 1277 have a LINKED partner state** (`CSPorte79hopen` /
  `CSPorte79hclosed`), which is the data's own argument that a node stays put,
  since a partner that runs the path back would otherwise be redundant.

### 6e. Repro, and what it costs

    build/omk-play ../gamedata ../tables --save ../traces/save-appart.bin \
        --area 157 --address 446 --frames 260 --nofmv --nodelay --no-crowd \
        --hold 'k*40,k28*2,k*60,k208*2,k*30,k28*2,k*200' --dump out.bin

For the walk test, replace the trailing `k*200` with `k*40,k200*300` and pass
`--frames 500`; the end-of-run `walked N` line is the number.

`OMK_NO_ZONE_BAND=1` turns the height band off for the same run, which is how
the band was ruled out (the arrival then picks camera 2999, level −5's, and is
66% dark). The band stays.

**Nothing was changed.** `verify.py --only "engine: lift"` runs `engine: lift`
and `engine: lift doors`, both green, on the tree as it stands.

### 6f. Captain Lea's lift: the door stays shut because a script played it in the WRONG SCENE — FIXED 2026-09-18

Reported in play: *"the lift door stays closed when I go to the captain Lea
office (after the call on the sneak)"*. Not the door, not the snap-back of 6d,
and not a race: **a scene-local object id resolved in another scene's file.**

**What the engine does.** `scx.play.wait` (0x004031E0) resolves its object
through `dword_69BC48[ctx+1F * 16]` - the object container of the slot the
RUNNING CONTEXT belongs to (`+1F` is the context's slot byte, `Ctx::slot` at
+31 here). The port handed every `scx.play*` to `scene_`, the pool loaded
LAST. Across a transition that is the destination's.

**Why only after Lea's call.** SCENE 45 is the call, loaded over the shaft
(AREA 157), and it takes the lift over:

* its startup DISABLES level -4's own car zone 2552 (and 2551) - the zone whose
  enter script (AREA 157 record 60) plays `obj 0x0012` and opens the door on
  every other ride;
* its zone 2636 does `ui.open 4`, then on `Etage == 4`: close the -2 door,
  `actor.goto_address 453`, `area.goto 181`, `area.arrive -1`, and **only then**
  `scx.play.wait obj 0x0012` - the level -4 door.

By that line `lev-4.SCX` is the newest pool, so `0x12` named some object of
AREA 181's scene and the shaft's door never moved. Deterministic. On every
OTHER ride the door worked by ORDER: record 60's enter script started it one
frame before the swap, so it bound to `puits.SCX` and ran on in the outgoing
pool. That is also why 6d's harness (a ride from level 0, no call) could never
see this.

**The fix** (`Session::poolForSlot`): a context plays objects in its own slot's
pool - `sceneOut_` when its slot's area is the outgoing one, else `scene_` -
and a context parked on a program remembers WHICH pool it is parked in
(`Ctx::waitingInOut`), so its release and the two "parked on a program"
queries read the same pool. Five edits in `area.{h,cpp}`.

**Measured on the post-call route** (`--scene-chunk 45`, zone 2548 off and
2636 on as the call leaves them, -2 -> -4, door mesh `CSPorte08h` read through
`OMK_MESH_AT` - the vertex buffer the frame draws):

| | `CSPorte08h` at frame 800 |
|---|---|
| fix OFF | `moved 0.0 0.0 0.0` - never opens: the reported bug |
| fix ON  | `moved -0.0 -87.1 0.1` - opens 87 units, the same travel as level -2's door, and stays open |

With the fix the log says the door was started by the OUTGOING pool
(`puits.SCX`) AFTER `lev-4.SCX` became resident - exactly the case the rule
exists for. `verify.py: engine: slot pool` pins it.

Two rides that already worked keep working, measured both ways: level 0 -> -4
and -2 -> -4 WITHOUT the call, in fast-forward and at real-time pacing - the
door opens 87 units in all four.

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
