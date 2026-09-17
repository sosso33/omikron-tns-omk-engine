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

**Still open here**: the header (`textFn` 0x004AF5A0) is
`table_at_0x004E3FEC[word_4E3FE2]` - a runtime pointer array, so which label it
shows per selection is unread; and the body does not yet switch to the chosen
dossier (`IAM\Term` 0..4), which is the same index. The five "Consulter le
dossier n°N" strings (5..9) and "Quitter le terminal" (10) belong to one of
those two.

## 4. The steps

| step | what | state |
|---|---|---|
| 0 | where every screen is opened from | **done 2026-09-17** - §1 |
| 1 | the LIFT: why it never arrived | **done 2026-09-17** - §2, `verify.py: engine: lift` |
| 2 | the terminal FAMILY: the display, the keypad and the answers - 7 screens | **done 2026-09-17** - §3, `verify.py: engine: terminal family` |
| 3 | the tail: 12 GANDHAR DOOR, 13 DEN, 14 XACHEN, 36 HIGH-SCORE, 0 VIDEOPHONE - and the terminal's own dossier pages | |
| 4 | play | |
