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
points away), but no run here put the lens in his head. **Update 2026-10-02**: the head in the FIRST line's shot
(the back of his head, seen on Vulkan) was him SLIDING 2.8 units into the camera after walking into
Telis's zone - a clip's root motion the engine never applies in a conversation; fixed (`6c623fc`).
Walk INTO the zone (do not load straight into it) to see the case. Also worth a look
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

## 16. KAY'L'S CHEST, and anything a scene SWINGS open (2026-10-02)

```
build/omk-play ~/Documents/omk/fr ../tables --save ../traces/games-resto.bin --slot 0 \
    --area 237 --address 683
```
Press action at the chest: the lid should open on its back hinge as one
piece. The fix changes the sense of EVERY rotation a scene program gives a
set mesh (`Script_MoveObjectOnPath`'s path keys), so a door, shutter or lid
that SWINGS anywhere else is worth a look - it should swing the way the
original does. Sliding doors are unaffected.

## 17. THE TRAINING MACHINE in Kay'l's flat (2026-10-02)

```
build/omk-play ~/Documents/omk/fr ../tables --save ../traces/games-resto.bin --slot 0 \
    --area 237 --address 680
```
Press action at the console, then 1 and ENTER on the keypad: the virtual
partner should fight you where you see him - blows land on the body in front
of you, not on air. Any melee opponent no scene program moved before the
fight (most of them) took the same fault.
Knocked down, he should lie ON the floor, and after you win he should stay
where he fell (`2edc423`).

## 18. BILINEAR FILTERING, now the default on every GPU backend (2026-10-03)

```
build/omk-play ~/Documents/omk/fr ../tables --vulkan --save ../traces/save-appart.bin \
    --area 0 --stand 1804,0,-6890,336
```
The original's 3D-card device filtered bilinear (`docs/ASSETS.md` 4), so the
GPU backends now do by default - Vulkan, GLES (`omk-play-gles`, the Vita) and
GL1 (seen in Tiger, one still). Look at three things, with `--filter
nearest` to compare: CUTOUTS - grilles, railings, sign lettering - should keep
a clean edge with no dark outline (GL1 may darken them by up to half; the
other two should not); the Anekbah SHOP SIGNS, which sample rectangles of a
shared atlas and may show a thin line of their neighbour at the edge (a known
limit, never clamped); and anything that now looks blurry where the original
looked sharp.

## 19. THE TETRA COUNTDOWN - the clock and the script timer (2026-10-05)

```
build/omk-play ~/Documents/omk/fr ../tables --save ../traces/save-appart.bin \
    --area 77 --zone-enable 1532 --stand 11969,3899,9852,358
```
ENTER in the zone places the first bomb. **A countdown should appear centred
at the top, `14:59:..`**, the last pair HUNDREDTHS (the engine's format is
minutes, seconds, hundredths), losing a second each real second. Let it run
out - fifteen minutes - and the AREA's time-out should fade to black and send
you back to AREA 61. The `--stand` point is the zone's CENTRE and is on the
edge of the floor: step back before pressing, or he walks off it - that is
the harness, not the timer. Also worth a look: the sneak's date and time now
MOVE (an in-game day is one real hour), and a save made after playing a while
carries the later time. `todo/drift-audit.md` S1.

## 20. A REPLY THAT RETIRES ITS OWN ZONE - Namtar (2026-10-05)

```
build/omk-play ~/Documents/omk/fr ../tables --save ../traces/save-appart.bin \
    --area 47 --scene-chunk 19 --zone-enable 990 --stand -90,-10,-1040,0
```
ENTER to talk to Namtar (dialog 197), take the FIRST reply. When it ends,
pressing ENTER where you stand should NOT start the same conversation again:
the reply retired zone 990 and opened 991. Before this, the zone stayed live.
`todo/drift-audit.md` S2.

## 21. A HIDDEN CHARACTER COMES BACK WHERE HE WAS (2026-10-05)

No single command reaches it - it is every `character.hide` ... `character.show
X, 0` pair in the game (801 shows). What to watch for anywhere: a character
who disappears for a beat and comes back should come back WHERE HE WAS and in
the POSE he was in, not at some other spot, and never in a T-pose. The street
T-pose report (next-tasks 6) is the one to look at again. To see the mechanism
on Telis in the flat (needs `omk-saves/GAMES`), `--hide-show 53,260,300,0`
with `verify.py: engine: hide show`'s command. `todo/drift-audit.md` M1.

## 22. A RETRIED SHOOT PHASE GETS ITS AMMUNITION BACK (2026-10-05)

Die in a shoot phase that checkpoints (AREA 2 saves on entry; its "Mort
Joueur" handler restores): on the retry the carried items and the five guns'
ammunition should be what you entered with, not what the failed attempt left
- the carried list in REVERSE order, which is the engine's own. `todo/drift-
audit.md` S4.

## 23. A SOUL CAPTURED RESTARTS THE GAME (2026-10-05)

The three `game.restart` sites (Lahoreh / AREA 61, AREA 64, Ix Astaroth 2):
when Astaroth takes the player's soul the screen should fade in from WHITE on
the start menu, with nothing of the old area still moving or sounding behind
it. `omk-play ... --game-restart N` does the same from anywhere.
`todo/drift-audit.md` S3.

## 24. RIDE A LIFT - AREA 50, Jaunpur's library (2026-10-05)

```
build/omk-play ~/Documents/omk/fr ../tables --save ../traces/save-appart.bin \
    --area 50 --var 321=1 --zone-enable 1040 --stand 57,-20,1200,44
```
ENTER on the platform: the barrier closes and the lift should carry you up
to the upper floor and LEAVE YOU THERE - not drop you back to the bottom
while it goes on without you. ENTER again up there to ride back down. And
anywhere: a character standing still on any moving floor should stay on it.
`todo/drift-audit.md` M9.

## 25. A LADDER IN A SHOOT PHASE - the rooftops (2026-10-05)

```
build/omk-play ~/Documents/omk/fr ../tables --save ../traces/save-appart.bin \
    --area 249 --scene-chunk 62 --zone-enable 4288 --stand 25428,-364,1711,267 --shoot
```
ENTER at the foot of the ladder: the HUD should close, the camera fly, you
appear at the top, and after a moment the first-person view, the gun and the
HUD come back. While it plays NO gunman moves or fires. Before 2026-10-05 the
press did nothing at all in a shoot phase. And in the Tetra raids
(`shoot.freeze_all`), gunmen should stand still until a shot or a hit wakes
them. `todo/drift-audit.md` S7.

## 26. QUIT TO THE START MENU, THEN LOAD (2026-10-05)

Anywhere in play: ESC, `Quitter le jeu`, `Oui`. The game should fade in from
WHITE on the start menu - before this the viewer simply closed. Then
`Charger une partie` and a slot should load it like a fresh boot does. The
same from the sneak's quit tab. `todo/drift-audit.md` T1.

## 27. ASTAROTH'S SOULS (2026-10-05)

The end-game fight (AREA 175, after dialog 335): with the baton, shoot the six
glowing souls (`PAame01..06`) around the arena. Each should take THREE bolts
and then vanish, and its TUY creature with it. Bolts on Astaroth himself do
nothing while a soul remains. Once all six are down, bolts in his BACK
(from behind) hurt him - he jerks forward each time - and bolts from the front
do nothing, except that every tenth makes him flinch for a few seconds. 34 back
hits kill him and the ending cutscene runs. He now FIGHTS: while
a soul stands he stays put, aims at you (his torso bending to follow) and
shoots from his left hand about every 1.4 s; with the souls down he walks at
you, and close in he winds up, rises, and LEAPS onto where you stood - stay
under him and the landing kills. Then he fires a slow bolt from his chest and
walks again. His gauge is on the right edge. Things to watch: his walk and
turns look natural, the leap's height, whether his bolts look right (the port
draws them with the shot sprite rows `AstMain` / `AstBust`). Before this no
soul could be struck at all.

## 28. THE CAMERA SHAKE (2026-10-05)

`camera.shake` now moves the view: a short vertical judder that fades. Where
to see it: being hurt in a shoot phase or a fight when the scene has no hurt
handler of its own (GLOBAL's shakes 15/40 or 20/20), bumping into passers-by
in a city (a small 5/5 with their voice line), Astaroth's footsteps, and his
death (a long 100/100). Before this nothing shook. `todo/astaroth.md` 4.

## 29. GUNMEN REACT AND HEAR (2026-10-05)

In any gunfight: a gunman you hit (above his reaction threshold) should now
REACT, and gunmen should be ALERTED by shots and bolts nearby - before this
both were computed and thrown away. A gunman a script enters with no action
stands idle until something moves him. The Shooting gallery is the place to
see it: its third gunman (240) used to wait; now he turns and fires as soon as
the first shot is fired. `todo/drift-audit.md` S13.

## 30. CLOSE-UPS ON THE PLAYER'S HEAD (2026-10-05)

The game's medium shot and close-up of the player (`CAM GLOBAL PM J` 69,
`CAM GLOBAL GP J` 13, `CAM GLOBAL BUSTE J` 11) frame his FACE now, not his
chest, and the travel from one to the other stays on the face. Where to see
it: the Mayerem tombs (AREA 141), activating tomb 9 after the shoot phase -
headless, `--area 141 --stand 40955,1116,-3021,90 --var 471=1 --shoot --keys
28,28,28`, where frame 45 is the medium shot and frame 80 the close-up. Also
any street conversation (the pharmacist, AREA 39): her bust shot is
`sub_415320`'s rest point now, which on a standing speaker moves the frame by
about a unit - a SEATED speaker is where the two readings part. Rendered
headlessly and looked at: both shots read as their names say.
`todo/drift-audit.md` S14b.

## 31. GANDHAR FIGHTS AS HIMSELF (2026-10-05, step 1 of `todo/gandhar.md`)

In the lava cave (AREA 2), after the bridge meeting (zone 317), Gandhar no
longer behaves as an ordinary gunman: he plays his own routine - his attack
animations, then sinking into the lava, waiting, rising again - turning to
face you, with the boss health bar on the right. Killing him ends the shoot
and runs the collapse. Since step 2 he WALKS toward you at 6 units a frame
during his walking actions and stops at his floor's edge (sliding along it).
Since step 3 he FIRES: a slow bolt from his tail once in each of his tail-attack animations (damage 20) and a rapid stream from his head (damage 10). Since 3b: he is drawn LOWER - his pelvis on his node (he had been drawn ~280 too high, his head above the cave ceiling, so nothing could hit him) - and he can be hurt only with the BATON (the cave gives it: zone 313, `Départ Shoot Grotte`), only through his HEAD, and only while he walks; a Waver bolt does nothing. To check by eye: does his height over the lava now look right, and do head hits flash (effect 19)? Since step 4 he STRIKES (11 damage, a shove) and GRABS when he reaches you - only from the edge of the walkway nearest his floor - and a grab plays one of the cave's four kill scenes (a ring spent, back to the entrance). To check by eye: the grab's kill scene, and whether his reach looks right. Headless: `--area 2 --stand 1811,-9,1216,0 --player-at 60:125,-9,401,0` (the cave's own baton start). Since step 5 the fight plays to its end headlessly: his death sinks him into the lava, the cave collapses, and the antechamber (AREA 43) follows. To check by eye: is he drawn at the right HEIGHT over the
lava, and do the sink/rise look right?

## 32. BODIES ANIMATED BY NAME, AND THE UNDRIVEN AT REST (2026-10-06, `todo/drift-audit.md` M2b / M2)

A scene animation now finds the body it moves by its node NAME, as the
original does, so a plain `scx.play` animates its character at last. To check
by eye: in the lava cave, after the bridge meeting, **the snake RISES out of
the lava by the bridge** (`4_D+Pont2`, about 200 frames) before the shoot
starts - before, he stood frozen there; in the **Morgue** (AREA 35) the two
bodies on the slabs (`M2_FN`, `MOOBJ_FN`) are posed by the `Cadavre`
animation from the start. Note the snake's entry: drawn ~43 units lower one
frame after the rise ends (his shoot entry snaps his node to y -147) - say
whether the original pops there too. **The reader (2026-10-06): in the original, after the cutscene, his head is WAY ABOVE the player** - which the port's frames 945 and 990 show too (his head at the top of the view, the player's at the bottom); the 43-unit drop at the entry has not been compared by eye. **And the reader, with a frame of the original (2026-10-06): the position is correct, the LIGHTING is not** - in the original his white spikes and body are tinted the lava's RED all over; the port draws them white/grey, unlit. And an NPC nothing drives now holds his
REST pose (or the last pose he was given) instead of his bank's first frame -
nothing visible was found that changes (the AREA 71 mecaguards are out of
sight before zone 1414 wakes them).

## 33. THE BLACK-AND-WHITE CUTSCENES (2026-10-06, `todo/drift-audit.md` S11, ops 150/151)

Fourteen scripted camera sequences now draw in GREYSCALE, as the original
does: the 3D picture only (set, bodies, props, effects, fog and the clear
colour); the interface and subtitles keep their colour. Where to see them in
play: the FIRST entry into the Morgue (SCENE 9's zone 851, while variable 286
`Première Morgue` is 0 - the two `cam Flash Plan` shots); Telis's flashback in
Kay'l's flat (SCENE 57's zone 4128, `ZVO M012 Telis Flash Appart 1`); Anissa
Aka's bar (SCENE 42); the Bowie concert (SCENE 60); and four AREA scripts
(145, 156, 158, 177). Each bracket ends in colour. Look for: a shade that
looks right against the original (the port greys through the palette - the
original's 16-bit path differs by under a level); nothing left coloured in
the 3D picture; the colour coming back at the bracket's end. Without a route,
`--op-at 30:150,60:151` shows the bank alone (Kay'l's flat: `--save
traces/save-appart.bin --area 237 --scene-chunk 57`). **Not seen on the 3DS:**
its two shaders carry the grey, but nothing has been built or run there.

## 34. THE RING A BEATEN CHARACTER DROPS (2026-10-06, `todo/drift-audit.md` M5, op 98)

After the six scripted melee wins, the script shows `Anneaux 5` and places it
UNDER THE CHARACTER JUST BEATEN - in the port it stayed at its authored spot
(the supermarket's ring 385 units away). To check by eye: win the supermarket
fight (`--fight-supermarket`); the ring should lie on the floor where the
Gun Waver went down, and be takeable there. Note its ROTATION: the original
leaves it to stack residue, the port draws it unrotated - say whether the
original's ring stands, lies or tilts there.

## HEADLESS PASS over 19-29 (2026-10-05)

Each item driven through the viewer with its check's own command, frames
rendered at the moments it names and read by eye (not the same as playing):

| item | verdict |
|---|---|
| 19 Tetra countdown | readout centred at the top, counting down ~1 s per 30 frames; the scene behind could not be judged - the `--stand` point drops him off the floor's edge, as the item warns |
| 20 Namtar | PASSES: after the conversation ENTER gives "Je n'ai rien de nouveau a dire a Namtar" (zone 992), never the conversation again |
| 21 hide / show | PASSES: Telis by the wall at 250, gone at 280, back in the same spot and pose at 310 (seen through `--eye`/`--at`) |
| 22 checkpoint | not visual; `engine: inventory checkpoint` only |
| 23 game restart | PASSES: the street at 58, the start menu fading in from white at 66, whole at 120 |
| 24 lift | PASSES: up 158 units, still there at 230/260, down again by 480; the frame at 230 shows him on the upper floor |
| 25 ladder | the suspend / put-down / resume sequence is right (41 / 83 / 104). The climb's cameras were framed on the PLAYER and looked away from the ladder - FIXED the same day (`todo/drift-audit.md` S14); the move now looks at the wall from the foot and up past the roof at the top, as the data frames it, and Kay'l is hidden through it by the script itself. Worth a look in play: whether the original's climb looks the same |
| 26 quit to menu | the screen stayed WHITE after the quit - FIXED the same day: a fault of headless (`--frames`) runs only, the pause's frame delta of 0 never put back; the menu now draws (`todo/drift-audit.md` S15) |
| 27 Astaroth | PASSES as far as rendered: soul 5 glowing at 130, gone in a green burst at 140; his walk and leap were looked at earlier the same day |
| 28 camera shake | not visual at 640x480 (a few units of Y); `engine: camera shake` reads it from the view |
| 29 gunmen react | the log shows 240 alerted at 43 and firing; the gunmen are behind the player's view in the gallery run |

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
