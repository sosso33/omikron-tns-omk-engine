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

## What is NOT worth testing yet

* ~~the videophone's own picture inside the sneak~~ — **CONFIRMED IN PLAY
  2026-09-08**, nothing left to test here. For the record, what was ported: the panel's own viewport item now renders the world into
  its 500x280 rectangle through the live camera, so the caller's face should
  be back in the device during the restaurant call (`--call 386` is the
  headless repro). What to look for: the face fills the rectangle the way
  the original's capture shows, the frame of the device is drawn around it,
  and the street is NOT visible anywhere else on the sneak.
