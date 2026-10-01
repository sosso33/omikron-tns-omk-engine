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

## 9. LEFT SHIFT runs, LEFT CTRL sidesteps and dives — the keyboard fixes

```
build/omk-play ~/Documents/omk/fr ../tables --save ../traces/save-appart.bin \
    --area 0 --stand 1804,0,-6890,336
```
Walk with the arrows and hold **LEFT** Shift: he should RUN, which he did not
before (only right Shift did). Left Ctrl should sidestep / half-turn, and in
water it dives. And ALT+TAB must NOT open the sneak. These are `Input_Poll`'s
own rules (`todo/vita-port.md` F3).

## 10. A GAMEPAD — any SDL-supported controller

The same command with a pad plugged in (the log prints `pad: <name>`). Left
stick or d-pad walks and turns, A/CROSS is Action and the UI's confirm, B/
CIRCLE cancels / jumps, the right shoulder RUNS, BACK/SELECT opens the sneak,
START is the pause screen. **The button layout and the stick's dead zone are
this port's choices** (`src/input/pad.h`) - say if they feel wrong.

## 11. THE GAME THROUGH THE VITA'S RENDERER, on this Mac

```
make play-gles
build/omk-play-gles ~/Documents/omk/fr ../tables --save ../traces/save-appart.bin \
    --area 0 --stand 1804,0,-6890,336 --res 640x480
```
The same street through the GLES2 backend the Vita draws with. It should look
like `build/omk-play --software`; the headless comparison says 0.99 coverage.

## 12. THE GAME ON A PS VITA — `engine/build/vita/omk_vita.vpk`

Copy the game data to `ux0:data/omk/gamedata/` (MESHES/, IAM/... inside it),
have `ur0:data/libshacccg.suprx`, install the VPK, launch. `ux0:data/omk/
args.txt` takes extra arguments one per line (`--nofmv` skips the ~143 s of
movies; the street start above works too). Send back `ux0:data/omk/
omk-play.log` and `.err` whatever happens - nothing of this has run on a
console yet. `omk_bench.vpk` and `omk_smoke.vpk` (same folder) are the two
measurements (`todo/vita-port.md` §0).

## 13. THE OPTIONS MENU, and FULLSCREEN (`todo/options-menu.md`, 2026-10-01)

```
build/omk-play ~/Documents/omk/fr ../tables --nofmv
build/omk-play ~/Documents/omk/fr ../tables --save ../traces/save-appart.bin \
    --area 0 --stand 1804,0,-6890,336
```
From the start menu: `Options`, then `Vidéo`. Arrows move and change a value
(ENTER steps it forward too), SPACE goes back. Change something and go back:
"Sauvegarder les options" asks, `Oui` writes the settings into
`omk-saves/GAMES`. Back on the root, SPACE returns to the menu (one more SPACE
to its four buttons - the game's own two steps). In the street, TAB opens the
sneak; its `Options` tab shows the same pages and RIGHT gives them the keys.
After the first pass (2026-10-01): a saved resolution is the next start's
size; `Retour` goes straight back to the menu's buttons; the three volumes act
(music at once, dialogue and effects from the next sound - 0 is 40 dB down,
the game's floor, not silence); the green button and Window > Enter Full
Screen work.
Worth looking at: the clip distance and the sky change the street at once;
the crowd changes at the next area; the RESOLUTION changes the window there
and then. F11 toggles fullscreen anywhere, and `--fullscreen` starts in it.

## 14. THE BACK-FACE CULL — dialogue cameras that were blocked (2026-10-01)

```
build/omk-play ~/Documents/omk/fr ../tables --save ../traces/games-resto.bin --slot 0
build/omk-play ~/Documents/omk/fr ../tables --save ../traces/games-resto.bin --slot 2
```
Slot 0 is before the apartment conversation with Telis (dialog 402), slot 2
before the restaurant lunch (dialog 387). The engine never draws the BACK of
a face (except water), and the port drew it. What should now match the
original: the restaurant's high crane shot looks down at the table instead of
at the back of the ceiling; the apartment's wide reply shot across the room
is no longer the back of a wall; the shot from inside the vivarium shows ONE
layer of yellow glass, not a wash; the lift's arrival in the security centre
is no longer black. **Not yet seen by anyone: the line cameras that started
INSIDE Kay'l's head** - the cull explains them (from inside a head every face
points away), but no run here put the lens in his head. Also worth a look
anywhere: a two-sided sign now shows each side's own advert, and water should
still be visible from above and below. All three renderers (software, Vulkan,
GLES) cull. `OMK_NO_CULL=1` draws both sides on the software renderer, to
compare.

## 15. OUT OF KAY'L'S LIFT INTO HIS FLAT (2026-10-01)

```
build/omk-play ~/Documents/omk/fr ../tables --save ../traces/games-resto.bin --slot 0 \
    --area 237 --address 677
```
Address 677 is where the lift from Hall 27 puts him. Walk forward once the
doors have opened: he should go through the doorway without jumping. The
fix is in the swept body (its head was 12 units too high, so the lift's
lintel caught it), so it can change anywhere with a low ceiling or a low
door frame - worth a look under stairs, through low doors and in the
sewers/tunnels, where he should now pass anything a 1.80 m man passes.

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
