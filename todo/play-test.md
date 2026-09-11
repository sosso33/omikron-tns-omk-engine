# What to test in play — the pass at `c63a110`

Everything below is committed on `main` and passes its own checks, and **none
of it has been confirmed by a person** except where it says so. Written
2026-09-07, after four tasks: the sneak's two bugs, `Text_LayOutBlock`, and
the slider.

**Section 7 was added 2026-09-09** and is a second, later batch: the whole of
that day's render work — the dither, the shimmer, three shadow qualities,
per-pixel lighting and supersampling — none of it judged by a person. If you
have five minutes and not thirty, do 7 and leave 1 to 6.

The order is deliberate — **4 is the one to try first**, because it is the
whole slider feature end to end and it is new. 1 to 3 are quick and
independent, 5 needs a save with several objects, and 6 is the harness route
into the same flight model.

```
cd engine && make play
build/omk-play ../gamedata ../tables --save ../traces/save-appart.bin \
    --area 0 --stand 1804,0,-6890,336
```

is the shortest way into Anekbah with the device usable. `--slot 2` on
`../traces/games-resto.bin` puts you in the restaurant instead.

**Getting around the sneak**, because three of these need it: `TAB` opens it on
the inventory page with the rows selected. `RIGHT` moves to the tab column and
`UP`/`DOWN` walk it, `ENTER` opens a page. On the **slider** page the cursor
starts on the *"Appel du slider"* header, so press `DOWN` to reach the
destinations. On the **inventory** page `ENTER` on a row drops into the verb
bar, where `LEFT`/`RIGHT` move between *Utiliser*, *Utiliser sur* and
*Examiner*.

---

## 1. The pause screen still suspends the music — 30 seconds

`ESC` in the street. The music should stop **at once**, not fade out over a
second, and come back when you leave the screen. The world behind it should
still be drawn — the pause menu is one of only three screens that keep it.

Fixed 2026-09-07 (`032a2cb`); you reported it and I never heard the verdict, so
it is here to close rather than because I doubt it.

## 2. The examine page's long text scrolls — 1 minute

`TAB`, `ENTER` on a carried object, `RIGHT` `RIGHT` to *Examiner*, `ENTER`.

* The page should come up **on the text**, not with the highlight on the tab
  column at the left.
* `DOWN` should scroll the description 8 pixels a press; `UP` back.
* At the bottom you should reach the signature line — on the *Notice MK400*
  that is *"Khonsu, la technologie de demain."* in its own gold face, which
  **flashes red** about twice a second. Both are correct.
* The line straddling the top edge should be **cut mid-glyph**, not vanish and
  reappear.

Wrong looks like: nothing moves at all (the page is standing in the wrong
list), or the text spills above the box over the device's frame art.

## 3. The wrap and the line spacing, everywhere — 2 minutes

`Text_LayOutBlock` is ported, so this changed the layout of **every** piece of
interface text at once, not only the examine page. It reproduced the old
output pixel for pixel wherever nothing wraps, so what to look for is the
places that *do*:

* a menu row or a shop line whose text is longer than its box — it should now
  **wrap inside the box** where it used to run off the end in one line;
* the subtitles under a conversation: the line pitch changed from 19 px to the
  engine's 20, and a long line breaks in a slightly different place. Look for
  lines that overlap, or a reply stack that has drifted off its box.

**One thing I could not settle and would like your eye on.** `docs/UI.md`
recorded from a capture of the original that it fits *about four more lines in
the same box* than the port did. The engine's own pitch is now read, and it is
**looser** than the port's old guess (20 against 19, and 20 against 12 for a
blank line) — so the gap widened rather than closed. If your captures still
show more lines than this build, the difference is something other than the
line pitch and I would rather chase it than leave it recorded as a mystery.

## 4. CALL a slider and be TAKEN — the one to try first

**CONFIRMED IN PLAY, 2026-09-08** — three rounds of reports, each fixed and re-tested by the reader; the last: *"ok, it works"*. What follows is kept as the recipe.

Two ways in, and they end differently, exactly as the original does:

**A. Choose a destination.** `TAB`, `RIGHT`, `UP`, `ENTER` (the slider tab),
`DOWN`, `ENTER` on a row. **You should not move.** The camera should cut to a
slider spawning at the top of the nearest road and driving down it toward
you, behind and above it. It stops on the road within about 3 m of the kerb
nearest you. Now walk to **the door side** and press `ENTER`.

The door side is real, and it is the engine's own test (`MDACTION`
0x0046AEC0): you must be within **4.00 m** of the seat *and* on the slider's
own **−X** side, or nothing happens. If nothing happens the viewer says which
of the two failed — *"he is on the WRONG SIDE"* or *"too far away"*, with the
distance — so walk around it and try again. **Whether that is the side a
person would call the door is the thing to judge here**, because the reading
was settled by three numbers agreeing and not by a picture.

Press `ENTER` on the right side and he is snapped to the door, `H_SLDIN`
plays (72 frames — the door, the step in, the door shut) with the camera over
his shoulder from 4 m, and at the end of the clip the slider page opens by
itself as screen 7 — and because you chose a destination, **it closes again at
once and the slider drives you there**, you on it, the camera on the vehicle.
At the destination you are put out and the slider leaves.

**B. "Appel du slider".** Same page, `ENTER` on the header instead of a row.
A slider comes the same way. Board it and the page opens and **stays**: the
header now reads *Automatique* / *Manuelle*. `Manuelle` hands you the controls
— arrows steer, up/down thrust, SPACE stops. `Automatique` moves the cursor to
the destinations; confirm one and it drives you there.

What to judge: whether it takes a sensible route, whether it stops somewhere
you can reach, whether boarding feels like boarding, and whether the journey's
route and stop look right.

**What to judge now** (2026-09-08, after a first pass found six faults):
the door swings up as he steps in (`SLF_112.3DA`, 71.4 degrees) and down as
he gets out; he ends the entry IN the seat, 48 cm off the centreline on the
door side (the two clips agree on that point); he gets out where the slider
STOPPED, not at the destination's address; and once you walk 300 units away
and are in front of it the terminal prints `slider: RELEASED` and it drives
off as ordinary traffic. **Say if he sits 50 cm too far forward or back**:
the slider is drawn about `SlBasB`'s origin and the clips are authored about
`SlBassin`'s, 19.7 apart along the length, and that residual is not settled.

**The reader's standing fact, 2026-09-08, from the original replayed on
video**: Kay'l REALLY enters the slider - through the open door into a body
that is not a flat shell at that moment - and never passes through a face.
Any render or run where he crosses the hull is wrong, whatever the log says.

**Also to judge, added later the same day**: the vehicle under you should
be the COCKPIT body from the moment you press ENTER at the door - an open
red well along the flank, the door on it - and the plain shell again once
you are out (`sub_4521E0`'s swap; the shells have no door at all). And a
destination in ANOTHER city (`--area 1 --address 34` in Jaunpur, then the
Anekbah row): the load, then the slider is already at the destination's
kerb with you aboard and you get out there - the engine skips the drive
too. The camera should drop in behind you as you step out (camera 17).

**And a REINCARNATION**: the viewer now rebuilds the player's controller when
the player becomes another actor (`player.become`), and keeps it across an
area load. If you have a save past a reincarnation, ride the slider in the
other body: he must stay visible through the load and board as that body.

**Knowingly missing**: the optional cutscene of the slider on its road; and
a called slider you never board does not yet give up after its 600 frames.

## 4b. The slider takes you somewhere — 1 minute

`TAB`, `RIGHT`, `UP`, `ENTER` (the slider tab), `DOWN` (onto the
destinations), `ENTER` on a row.

The screen should close and you should be **standing at that place**, with the
follow camera back and a fade in. From `save-appart.bin` three destinations are
enabled; the first is *"Anekbah - Appartement de Kay'l"*.

What is deliberately missing: no slider appears and drives you there. The
engine only does that where the area has vehicle lanes; without a pool its own
code teleports, and that is the arm this build takes everywhere.

## 5. A sneak verb uses the row you are actually on — needs 10+ objects

**This one cannot be tested from `save-appart.bin`**, which carries one
object. It needs a save with **more than nine** carried items, because the bug
was in the scroll: the nine row widgets are a window onto a longer list, and
the verbs used the widget rather than the row.

With ten or more: scroll to the bottom of the inventory, pick the last row,
and *Examiner* it. The page should describe **that** object. Before the fix it
described the one three rows above.

Same for *Utiliser* — and for *Utiliser sur*, which should open the combine
with the object you chose.

## 6. Flying a slider — 2 minutes, and the one most likely to look wrong

```
build/omk-play ../gamedata ../tables --save ../traces/save-appart.bin \
    --area 0 --ride
```

`--ride` is still there and is still a harness — it skips the call entirely.
Item 4 is the real path now, and this is the quick way to the flight model.

* **left / right arrows** steer, **up / down** accelerate and brake,
  **SPACE** stops the ride once you are under about 10 units of speed
  (group 0's bit `0x20`, *Annuler / Sauter*).
* The camera is the engine's ride camera, 3 m up and 7 m back, and its subject
  is the **vehicle** rather than the player — so it does not swing round
  behind you the way the follow camera does.
* Held over, the bank should reach a hard **11 degrees** and no further.
* Parked, it should **idle up and down** by about two units; moving, it should
  hold its height. That pair is the one number I would most like a second
  opinion on, because it comes from a comparison the decompilation lost and I
  recovered from the raw listing.

**Under `--ride` Kay'l flies standing up on nothing, and that is expected** —
the harness has no vehicle. Come in through **item 4** instead and the slider
you called is drawn under him, because the ride moves the vehicle with it the
way `sub_457F50` moves the slider's node.

---

## 7. THE RENDER WORK OF 2026-09-09 — the whole of it needs eyes, 5 minutes

Six things landed in one day and **not one has been judged by a person.**
Every check behind them measures a number a metric can compute, and each says
in its own docstring what it cannot see. This section is that list.

```
cd engine && make play
build/omk-play ../gamedata ../tables --save ../traces/save-appart.bin \
    --area 0 --stand 1804,0,-6890,336 --vulkan
```

Anekbah's main street, with the crowd and the neon, on the backend that has
all of it. Add `--enhance-all` to see every enhancement at once, then take
them away one at a time.

**On by default, and the two that are FIDELITY rather than enhancement — look
at these first, because if either is wrong the port is now further from the
original than it was.**

* **The dither.** Look at a large smooth wall or the sky and ask whether it
  reads as a fine noise or as visible speckle. `--no-dither` for the contrast:
  the bands it replaces are what the 16-bit target does without it. What no
  check can see is whether the noise reads as *smoother* than the band — the
  arithmetic is proved, the judgement is not. Also worth one look: the frame
  must not appear brighter with the dither on than off. A centred dither is
  what makes that true and the measurement says it is centred to 0.249 of 255,
  but a systematic lift is exactly the kind of thing a number can pass and an
  eye can catch.
* **The shimmer.** The far skyline of any city — Lahoreh has 132 of the 233
  meshes. It should breathe slowly, not strobe. It is on the frame clock at
  2 a frame wrapping at 256, so the period is long; if it looks like a flicker
  the clock is being advanced somewhere else as well.

**Off by default — turn each on alone.**

* `--shadow-quality fitted` then `mapped`. Fitted should lay the blob on the
  step or the kerb the body stands on instead of on a flat plane; mapped
  should put a real shadow at the player's feet under a street lamp. Walk him
  between lamps: the direction should change and the shadow should not swim.
* `--lighting perpixel`. The gain is almost all on the CROWD, whose models
  ship white and whose shading IS the lights; on a lamp-lit thigh a falloff
  that used to bend across one big triangle should now bend across the leg.
  A body the engine never lights keeps its baked shading, so watch that Kay'l
  does not become a silhouette away from the lamps.
* `--ssaa 2` or `4`. The place to look is a GRILLE, a railing or a sign — the
  cutout edges, whose silhouette is a colour key inside a triangle and which
  MSAA never touches. `--aa 4` beside it should NOT fix those, which is the
  whole reason both exist.

Nothing here is a regression hunt; all six pass their checks. What is missing
is the one judgement none of the checks makes, which is whether the picture
is better.

---

## 8. THE JUMP — the one thing here a check CANNOT settle, 2 minutes

The same day, `todo/player-vertical.md` steps 1-3 landed: the walk no longer
floats, and the jump has an impulse for the first time. The walk half is
settled by measurement (the drawn foot is centred on the floor, and it was a
constant offset off it before). **The jump half is not, and cannot be.**

```
cd engine && make play
build/omk-play ../gamedata ../tables --save ../traces/save-appart.bin \
    --area 0 --stand 1804,0,-6890,336
```

Walk with UP, then press SPACE (*Annuler / Sauter*, the Aventure scheme's
bit 32).

What the port now does, every number of it read out of the engine and none of
it invented:

* **14 frames of hang** (0.47 s), from the `.CTL` entry's own `+12`;
* **22.9 cm of lift** — the apex, `-g * N/2` integrated;
* **2.5 m of forward reach**, `dword_910348` rotated by his facing.

**So it is a flat running LEAP, not a vertical hop, and that is the thing to
judge.** Every check behind it is Tier 5 — no capture from the trace rig can
reach a special-move handler, so the oracle is the shipped data's own
arithmetic and *not* the original's behaviour. If 23 cm looks too low or 2.5 m
too far, that is evidence about `N` (the reading this repo is least sure of,
because `entry+12`'s low half is a ROLE code in the combat banks) and it is
worth saying so — the alternative readings were tested and refuted, but a
person watching it is a better instrument than any of them.

Two more things a check here cannot see:

* **the landing.** A flat leap lands in band 2 and must play NO landing
  reaction. Jumping off something 1.5 m or higher should put him into
  `ACTOR_STATE 18` with bank group 2 — that arm is transcribed from
  `MDJUMP03` and **has never been executed**, by any check or by anyone. The
  band table itself is run at all four of its edges; what it *does* is not.
* **whether he goes through anything.** The leap is swept against the same
  walls a walk is, so a jump into a wall should stop dead rather than pass
  through.

---

## 9. WALK OUT OF A SHOP — **DONE, CONFIRMED 2026-09-09**

The fix for `todo/omk-play.md` 94. The reader walked it the day it landed and
reported it good, so this section is kept as the recipe rather than as an
outstanding test. From Anekbah's main street:

```
build/omk-play ../gamedata ../tables --save ../traces/save-appart.bin \
    --area 0 --stand 3703,0,-9010,180
```

puts you outside the drugstore by the security centre, facing its door.

* Walk in. The city's doors slide apart in front of you, as they always did.
* Turn round and walk back at the doorway. **The shop's own doors should now
  slide apart** — before the fix they stayed shut, you slid sideways along
  them, and the street was put away again after a few seconds.
* You should end up in the street, with the shop unloaded behind you.

Wrong looks like: you reach the door and stop dead, drifting sideways along an
invisible line while the world outside flickers in and out.

**Still unwatched**: the same fault was in ten other interiors — the bank, the
armoury, the bookshop — and the one worth a look for its own sake is
**Qalisar's temple**, where the same parameter drives `Qtrappe`, the trapdoor
of the reincarnation beat. The drugstore's confirmation covers the mechanism
for all of them; the trapdoor is a different question, because it is a story
beat that has presumably never opened in this port.

## 10. SHOOT MODE - FIRE, 2 minutes (committed 2026-09-10, `cc3d1f9` + `96fab56`)

**PLAYED 2026-09-10 - *"Ok"***. Missing, and planned in `todo/shoot-mode.md` §8: the FIRE SOUND and the shoot HUD. Still unanswered: which side of the view the bolt leaves from.

```
build/omk-play ../gamedata ../tables --save ../traces/save-appart.bin \
    --area 59 --stand 5000,0,-2900,180 --shoot
```

(the Shooting gallery; the supermarket phase is `--area 230 --scene-chunk 56`).
Fire with the LEFT MOUSE BUTTON or RIGHT SHIFT - the *Tirer* scheme's `Tir`.

* **A tap fires ONE green bolt, a moment after the press** - about six frames
  if you have not fired for a second, because the weapon has to come back up
  first. That delay is the engine's (the shoot record's `+176`), not lag.
* **Holding fires every 10 frames** with the Waver - three shots a second.
* The bolt **stretches into a streak** over its first eight frames and
  **stops at the first wall**: it should vanish where it meets the geometry.
* **Which side it leaves from.** The gun hangs on `Maing`, the LEFT hand, and
  a headless frame puts the bolt slightly RIGHT of centre. One still frame
  cannot settle a handedness; watching can. Say which side it is.

Wrong looks like: a bolt on the press frame itself (the gate bypassed), one
bolt per frame while held (no rate), a bolt from the eye rather than low in
the view, or a bolt that goes through walls.

**Now expected to work (ported after the play, 2026-09-10)**: hitting a
gunman. Three Waver bolts should kill one; he should play a death clip
and stay down, and later bolts should stop at his body. The hit test uses
boxes, so a bolt skimming just over a shoulder can still count - that is
the engine's own test (`todo/shoot-mode.md` §7j), not a fault.

**CONFIRMED IN PLAY 2026-09-10 (the supermarket, *"ok, good"*): the ARM
RAISE**, ported the same day after *"The animation of the arm when firing is
missing"*. At rest the gun hangs low at the
bottom right, mostly off screen. Press fire and the arm swings it up to the
middle of the view over about six frames, the bolt leaves from it THERE, and
a second after you let go it sinks back to the bottom right (0.1 a frame).
Held, it stays up. Looking up and down with the mouse should tilt the raised
arm with the view - it follows the look pitch at up to 30 degrees a frame,
but only while the trigger is held. Wrong looks like: the gun jumping between
the two places with nothing in between, a bolt leaving from the low gun, the
arm staying up after release, or the arm twisted (a key picked from the wrong
band - `todo/shoot-mode.md` §8.0).

**CONFIRMED IN PLAY 2026-09-10 (the supermarket, *"ok, the move is good"*):
MOVING IN FIRST PERSON**, ported the same day after *"don't forget the
integration of moving while in fps mode"*. The shoot scheme's
keys: UP / DOWN walk forward and back, LEFT / RIGHT arrows SIDE-STEP, NUMPAD 4
/ 6 turn (the mouse still turns too), RIGHT CTRL crouches. He should speed up
over about a second (27 frames to full speed) and stop within a fifth of one
when you let go; side-steps get going twice as fast; crouched he moves at half
speed. Walls and the gunmen's bodies should stop him. Wrong looks like: a
jerk to full speed on the first frame, a slide after release, moving in the
wrong direction for the way you face, or walking through a crate. There is no
head bob and no footstep sound yet - both are known.

**Now expected (ported 2026-09-10): THE GUNFIRE NOISE.** A shot, and every
bolt that lands on a wall or a body, now ALERTS the robbers who can hear it -
each within his own hearing range (the first robber in the supermarket hears
20 grid cells, about 20 m) and on the same floor - even ones who have not
seen you. So a robber out of sight should come to life when you fire nearby,
instead of waiting until he sees you. Their own shots are still not wired.
Wrong looks like: robbers across the shop reacting to every shot, or none
ever reacting until they see you. (Played 2026-09-10: *"Ok, good"*; 5
robbers alerted in the session. Fixed after: robbers standing off the grid
were never alerted - their floor was misread.)

**Reported in play 2026-09-11, *"Outside the appearance events, ennemies have
no animation and do not move"* - the ANIMATION half is fixed:** between his
entrance and his death a robber now plays his action's clip on a loop (the
aim stance, 19 frames in the supermarket's library) where he stood frozen on
its first frame. Wrong looks like: a robber still rigid, or one twitching back
to his first pose every second. He still does NOT WALK - that is the larger
half, planned in `todo/shoot-mode.md` §8 item 6B.
**And, after *"They shoot but without a shooting animation"*, THE AIM:** while
he fights, a robber's arms come UP and turn toward you - his gun on you - and
drop back when he stops. Wrong looks like: arms raised the wrong way (the gun
pointing to his other side), arms snapping up and down every frame, or a
robber twisted at the waist. They hit you a little less often in the gallery
now: their bolts aim at your hips with a small scatter and your body's hit
boxes are narrow. (Your screenshots of 2026-09-11 showed robbers facing away
and empty-handed: the robbers who come on through an entrance event were drawn
at the heading their entrance left while their brains aimed from another, and
their guns were never drawn. Both fixed: every robber now holds his gun on his
left hand, and his body faces where his brain aims. Not yet judged by eye.)
(Then, *"Ok, better"*, and *"when an ennemy die and its dying animation run,
they float in the air"*: fixed - a robber's death clip now carries him down to
the floor and slides him as it falls, the pelvis ending a few units above the
ground. **CONFIRMED IN PLAY 2026-09-11** (*"Yes, it is fixed, the bodies reach
the floor"*). Wrong looks like: a corpse still lying at waist height, one sinking
into the floor, or one sliding through a wall - the engine's wall stop is
ported now, so that last would be a fault.)

**NOT YET PLAYED (2026-09-11): THE ROBBERS WALK, and walls stop them.** Once a
robber's brain has him, his clip moves him: in the gallery the three gunmen
come toward you at a walking pace while they shoot, and when one meets a wall
he slides along it or turns. Wrong looks like: a robber gliding with his legs
still (the walk and the clip out of step), walking THROUGH a wall or a shelf,
sliding sideways at running speed, or sinking/rising as he goes. EXPECTED and
not a fault yet (the path-finder is not ported): a robber that walks into a
wall, turns, and walks into it again, over and over; robbers walking through
each other; and one walking right up to you. Say which of these you see.
(PLAYED 2026-09-11, *"Ok, good progress"*: a robber CLIMBED a little at every
loop until he hung from the ceiling - fixed, every clip start now puts his
height back as the engine does; and robbers walked INTO you, *"like I had no
collider"* - the original pushes YOU out of their bodies, and now so does
this. Look for: no robber rising off the floor however long he walks into a
wall; a robber that reaches you shoving you back instead of standing inside
you. EXPECTED, and the path-finder's to fix: two robbers can shove you a long
way across the room. Wrong looks like: being thrown far in one frame, or
pushed by an empty spot - a dead robber's body still pushes, since nothing
the engine was read doing removes it.)

**NOT YET PLAYED (2026-09-11): THE GUNMEN FIRE - and turn round to do it.**
In the supermarket the robber nearest the entrance (actor 77) starts with
you behind him: he should turn round on his own clip over about a second as
the phase begins, then fire at you every half-second. Wrong looks like: a
robber spinning on the spot in a stiff pose, turning more than once for no
reason, or firing with his back to you. (Your session of 2026-09-11, from its
log: 77 turned and fired, and every robber fired - but 519 and 521, placed by
their records, stayed stuck mid-turn: 519 restarted a turn every half-second
facing the same way. Fixed after: the placement was putting their facing back
every frame. Not yet judged by eye.) (Your second session the same day, from
its log: the phase run to its end, 18 robbers killed and reported; 93 bolts
on you, each through your Body Shield in the engine's order; 519 turned and
fired. You were KILLED twice - the gauge froze at 4 each time, as it should -
and each time a medikit on the floor BROUGHT YOU BACK, to 20 and then to 104.
That is the unported death, not the game: the kit adds to the stored health
property, which the killing hit never lowers. It goes when `sub_423FC0` is
ported. Not yet judged by eye.) The gallery harness shows two gunmen already
facing you:

```
build/omk-play ../gamedata ../tables --save ../traces/save-appart.bin \
    --area 59 --stand 5000,0,-2900,0 --shoot
```

Both open fire at once: one bolt every 10 frames from the DBWAVER on the
left, one every 4 from the HEXAGUN, each with its firing sound, flying at
you with a small scatter, **and now they hit you**: the gauge drops by 4 a hit
(5 through your Body Shield), and a kit you carry should be used by itself
once you are under 40. At 0 the game would kill you; that is not ported yet,
so you play on with the gauge frozen at its last value. Their arms do not come
up to aim (the aim pose is not ported for them). Wrong looks like: bolts flying off at a
wide angle from you, or leaving from somewhere other than the gun in the
hand.

**CONFIRMED IN PLAY 2026-09-10 (*"yes, it was correct"*): HEALTH ITEMS IN A
SHOOT PHASE**, fixed the same day after *"grabbing a health item does not
restore your health"* and your correction that shoot mode uses them at once. Walking onto a medikit in the
supermarket should now raise the gauge at once - +50 for a medium, +100 for a
large, +16 for a small (its subtitle says +15) - with the pickup line. The kits
you carry from adventure mode are used by themselves when a hit leaves you
under 40 health; nothing can hit you yet, so that part waits for the robbers'
shots. Wrong looks like: the pickup line with no change on the gauge. (The reader's
session of 2026-09-10 logs all three: the medium kit 10 -> 60, the small 60 ->
76, the large 76 -> 176, each through `sub_423A40` to the gauge. Not yet
judged by eye. The HUD's own log line then printed every frame at 176 -
its empty-part pixel probe sat in the gauge's scrolling fill - and no longer
compares the probes.)

**CONFIRMED IN PLAY 2026-09-10 (*"Good"*): THE MOUSE LOOK, the engine's own.** The
mouse turns and tilts the first-person view with the game's own settings -
your save's options 23 and 24, sensitivities 20 and 15 - so turning is a
touch faster than before (0.20 degrees a pixel against 0.18) and the tilt now
stops at 45 degrees up or down instead of 70. The tilt is 0.15 degrees a pixel
at 30 frames a second, and the engine scales it by the frame time, so at a
higher frame rate it tilts less per pixel. The DIRECTIONS are unchanged -
yours. The shoot scheme's *Regarder En-Haut / En-Bas*, if bound, now tilt in
steps. Wrong looks like: a direction reversed, or a tilt past 45 degrees.

**CONFIRMED IN PLAY 2026-09-10 (*"It looks good"*), then CORRECTED the same
day: THE RADAR.** The reader did not remember it from the original, and the
game agrees: in the supermarket and six other arenas a script turns it on only
when Kay'l carries object 980, "Radar activé", which nothing in the game gives
- so it is now HIDDEN there, as shipped. `radar = always` under
`[Enhancements]` (or `--radar always`) brings it back, in the human HUD's own
box (top right, 180x180) with the camera 9 m behind you. The Archives (AREA 63
and 67) turn it on by themselves. What to judge: that the supermarket shows no
radar by default. **`radar = always` CONFIRMED IN PLAY 2026-09-10** (*"ok,
good"*, the supermarket in the human HUD's own 180x180 box); the hidden
default has not been looked at by a person.

**CONFIRMED IN PLAY 2026-09-10 (the supermarket, *"ok, good"*): THE SHOOT HUD,
parts 1-3**, ported the same day after *"no UI"*.
In shoot mode the screen should carry: at the top left a box with your ring
count under it (the *anneaux* - 244 in your frames of the original, 2 with the
test save), at the bottom left a box with the weapon's name over it (`Waver`)
and, for a weapon with a magazine, the rounds left just above, and a small
white cross at the centre of the screen, and in the two boxes the RING and
the WEAPON turning slowly (one turn every five seconds, the sneak's own
turntable), and down the LEFT EDGE the health gauge - a black column with a
diamond at each end, the fill rising from the bottom (the save's player is
at 10 of 200, so only a sliver) with a coloured column scrolling through it
and small green sparks drifting up and out from its top. NOT there yet,
known: the radar at the top right (the supermarket is one of the nine places that have
one). Say whether the boxes' grey matches the original.

**CONFIRMED IN PLAY 2026-09-10 (*"ok, good"*; the log walks him out through
areas 60 and 245): THE RETURN**, fixed the same day after *"the return to
adventure mode (after the cutscene) is buggy: invisble character, impossible
to move, weird camera"*. After the supermarket's ending hands you back you
should see Kay'l again from the ordinary follow camera, behind and above him,
and walk with the adventure keys. The last hostage talking to you is part of
the script; control comes back when it ends.

**CONFIRMED IN PLAY 2026-09-10 (*"The event is triggered, and the ending
cutscene is triggered"*): THE PHASE ENDS**, fixed the same day after *"i
can't finish the supermarket shoot sequence"*. The RETURN to adventure after
the ending was reported broken, and is fixed and confirmed - see the entry
above and `todo/shoot-mode.md` 8.5e. Each robber's death is now reported to the
scene when his death animation finishes (the terminal says `death clip over:
message 3 ... handler scene +0x433f`). The supermarket's ending is keyed to ONE
of them - actor 84, the script's *Braqueur 15* - whose death plays a victory
track and opens the end zone at the back of the shop, around (13806, 1658),
where the hostages and the doctor are. Walk into it and the end cutscene
should run: the gunmen vanish, the camera takes two shots of the room, and
you are handed back. Wrong looks like: no music when 84 falls, or nothing
when you reach the back.

**CONFIRMED IN PLAY 2026-09-10 (*"Ok, this event issue is fixed"*): ENEMY
ENTRANCES ARE GAMEPLAY**, fixed the same day after *"some events (like some
ennemie appearing with a special animation) are considered as cutscenes,
stops move and change camera"*. When a gunman makes his
entrance - vaulting in, stepping out from a shelf - you should keep walking,
turning and shooting through it, and the first-person view should stay yours.
If Kay'l still stops or the view jumps, the terminal says why on a line
starting `adventure OFF in shoot mode`.

**Now expected (ported 2026-09-10, after *"no fire sound effect"*): THE SHOT'S
SOUNDS.** Every shot of the Gun Waver should play its fire sound (WAVER2.WAV)
the moment the bolt leaves, and every bolt that stops - on a wall, a crate or
a body - its impact sound (WIMP1.WAV), quieter the further away it lands.
Wrong looks like: a sound on the press rather than on the shot (the shot
comes a moment later while the gun comes up), a sound per frame while the
bolt flies, or the impact sound for a bolt that went out of range. The muzzle
flash and the impact sparks that go with them are not drawn yet.

**And look at where people STAND, here and elsewhere.** Every body placed
by its record and turned by its facing now turns about its pelvis instead
of its model origin. For most characters that moves nothing you would see;
for the gallery's gunmen it moved them up to 770 units, onto the spots
their own AI thought they were on. Anyone suddenly standing somewhere odd
- in a wall, off a ledge - after this change is the thing to report.

## What is NOT worth testing yet

* ~~the videophone's own picture inside the sneak~~ — **CONFIRMED IN PLAY
  2026-09-08**, nothing left to test here. For the record, what was ported: the panel's own viewport item now renders the world into
  its 500x280 rectangle through the live camera, so the caller's face should
  be back in the device during the restaurant call (`--call 386` is the
  headless repro). What to look for: the face fills the rectangle the way
  the original's capture shows, the frame of the device is drawn around it,
  and the street is NOT visible anywhere else on the sneak.
