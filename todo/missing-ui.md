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

## 3. The FIGHT SIM — it opens, and answering it is the work left

AREA 237 record 23, reachable in one command:

    build/omk-play ../gamedata ../tables --save ../traces/save-appart.bin \
        --area 237 --address 680          # 'Console', facing the machine

Press the action button and `screen 11 is asking` appears, with its own sound
(slot 4, `FS001`). The script around it is read: it teleports him to ADDRESSES
680, disables zone 4050, sets camera 4449 over 30 frames, plays `player.move.wait
58`, opens the screen into VARIABLE 19 `Interface`, snapshots `Vie` three ways
and then - **on `Interface == 1`** - disables five zones, fades to black, shows
CHARACTER 145 and plays object 0x016f, which is the training fight.

What is missing is the panel's own hooks. `tables/ui_widgets.json` screen 11 is
a **keypad**: list 0 is ten 25x25 cells in a 3x3 grid plus one below (callback
0x4AF3D0 on each, three of them BOUND to strings 0..2) and a 96x64 button at
(528, 398) with callback 0x42A1D0; list 1 is the display - a 430x320 text item
in font 74 with `textFn` 0x4AF4D0 over a `drawFn` 0x477ED0 backdrop; list 2 is
two more buttons at the bottom. So it is a code to be typed and confirmed, not
a menu to be walked, and until those callbacks are read the walk has nothing to
answer with.

**Next step**, and it is the same shape as the shops and the multiplan: read
0x4AF3D0 (the cell), 0x42A1D0 (the button) and 0x4AF4D0 (the display's text)
out of the raw image - they have no `proc` label - and give `UiWalk` the
answer site. Then the training fight can be played through `fight.begin`,
which already runs.

## 4. The steps

| step | what | state |
|---|---|---|
| 0 | where every screen is opened from | **done 2026-09-17** - §1 |
| 1 | the LIFT: why it never arrived | **done 2026-09-17** - §2, `verify.py: engine: lift` |
| 2 | the FIGHT SIM's keypad hooks | §3 has the three addresses |
| 3 | the tail: 5 TERMINAL, 12 GANDHAR DOOR, 13 DEN, 14 XACHEN, 15/16/17 SURV, 18 ARCHIVES, 19 MORGUE, 36 HIGH-SCORE - one site each | |
| 4 | play | |
