# What is committed and NOT yet confirmed by a person

Rewritten 2026-09-18, replacing the previous pass. **Everything below has a
check behind it and none of it has been played.** Every frame quoted was read
by eye from a render, which is not the same thing.

The data root on this machine is `~/Documents/omk/fr`. Each command below opens
a real window; drive it yourself.

---

## 1. Den's locker — screen 13

```
build/omk-play ~/Documents/omk/fr ../tables --save ../traces/save-appart.bin \
    --area 146 --scene-chunk 43 --var 482=1 \
    --stand 522,-10,343,178 --zone-disable 2419
```
ENTER on the cache, then arrows. **The combination is 7 2 1 3** and there is no
confirm — it opens the moment the fourth wheel lands. Expect the wheel under
your hand to BLINK; that is the engine's own oscillator and not a fault. On
opening it you should get `Cassette Den`, `Pass Ventilos` and `Plan Ventilos`.

## 2. XACHEN — screen 14, Dakobah's cartridges

```
build/omk-play ~/Documents/omk/fr ../tables --save ../traces/save-appart.bin \
    --area 58 --var 28=1,57=0 --stand -1471,150,133,290
```
ENTER, then the four buttons with LEFT/RIGHT and ENTER to step a symbol. They
start at 1 2 3 4 and the code is 10 14 7 9 — **7, 3, 10 and 5 presses**, because
the ring is not in numerical order. The Xendar door should open and
Dakobah/Xendar should play.

## 3. The terminal's header bar — screens 5, 11, 15..19

```
build/omk-play ~/Documents/omk/fr ../tables --save ../traces/save-appart.bin \
    --area 179 --stand 5468,336,-12118,120
```
The bar at the top should name the keypad cell the cursor is on
(*Consulter le dossier n°1*) and follow it as you move. **On the three SURV
screens it is rightly empty** — those screens bind no labels.

## 4. The HIGH-SCORE board — screen 36

```
build/omk-play ~/Documents/omk/fr ../tables --save ../traces/save-appart.bin \
    --area 59 --stand 5412,15199,-3546,1
```
LEFT/RIGHT step four pages. The rows will be BLANK unless a save has scores in
it — that is correct, the shipped ones are empty. **Known and not a fault of
this screen**: the world behind it shows a stretched mesh close to the camera.

## 5. The sneak's ECHO BAR — the one to look at hardest

```
build/omk-play ~/Documents/omk/fr ../tables --save ../traces/save-appart.bin \
    --area 0 --stand 1804,0,-6890,336 --sneak
```
The wide bar at the bottom was **blank before today**, and it is the only place
the game shows your seteks and anneaux. Walking the page it should read in
turn: `Inventaire  (n / 18)`, `Seteks en votre possession : N`,
`Anneaux en votre possession : N`, `Lire plan` (no number — the imager counts
nothing), and a verb's name. Also look for **the three small 3D objects** down
the left: they had stopped drawing entirely and are back.

## 6. The sneak's CITY MAP — `Lire plan`

From the same run, the third 50x50 tile. Expect Anekbah's street plan, red
triangles on its destinations, and a **blue arrow labelled with your own name**
where you stand. Outside the four cities with maps the button should silently
bounce back to the Inventaire tab — that is the engine's own behaviour.

## 7. The HINT SHOP — `Indices` on the save screen

Reach a save point and pick the middle button. It was an **empty page** before
today. Expect the memo rows, the memo's text in the body, and
*Anneaux en votre possession : N* at the foot; buying costs **three anneaux**
and reveals the memo's second bracketed section. With no memos it should say
*Aucun indice disponible*; with fewer than three rings,
*Je n'ai pas assez d'Anneaux pour faire ça !*

**Known and deliberate**: the row selection has no effect — the page sells the
FIRST memo's clue whatever is highlighted. That is read from the code
(`dword_4E2B4C` is only ever written by the two builders), not a shortcut.

---

## 8. The security centre's HANDRAILS

The report: running into the barriers at Kay'l's office level, the player went
through and fell down the shaft. The body sweep met a triangle's FACE only, so a
thin rail's EDGE let a sphere straight through. It now meets edges and corners
too (`sweepOne`, labelled a reconstruction). To test it, run along and INTO the
rails on level -2 and -4, including where two rails meet in a point, and try to
fall. Also worth a look: nothing that used to be walkable should now snag, e.g.
doorframes, stair edges, or table and stool corners in the restaurant.
`verify.py: engine: security rail`.

## What is NOT fixed, so do not report it as new

* **The lift arrival is still black** and the camera is inside the lift-car
  mesh `CSPont04`. Identified, measured, not fixed — `todo/missing-ui.md` §6b,
  which also records the three candidate mechanisms and why guessing between
  them was refused.
* **The lift door opens and then shuts itself.** The program moves it, then the
  port drops the motion patch. Being worked on separately; §6d.
* **Tab strips switch pages on the confirm, not on the move.**
  `todo/ui-child-on-move.md` — nine lines, held back because they change the
  shared walk.
* **The overwrite dialog does not name the file** and the start menu's
  `Quitter` is not the real exit — `todo/ui-save-confirm-and-quit.md`, both
  decoded and ready to transcribe.
* `engine: UI`, `ui item bindings` and `licence headers` are red for census
  drift that predates all of this; `todo/sweep-log.md` has the attribution.
