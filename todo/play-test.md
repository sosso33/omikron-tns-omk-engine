# What to test in play — the pass at `c63a110`

Everything below is committed on `main` and passes its own checks, and **none
of it has been confirmed by a person** except where it says so. Written
2026-09-07, after four tasks: the sneak's two bugs, `Text_LayOutBlock`, and
the slider.

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

## 4. CALL a slider, and ride it away — the one to try first

`TAB`, `RIGHT`, `UP`, `ENTER` (the slider tab), `DOWN` (onto the
destinations), `ENTER` **on a row**. From the sneak, choosing a destination
does not take you there — it **calls a slider to you** and remembers where you
wanted to go. That is the engine's own rule: screen 9's `param` is 0, and only
screen 7 (the same page, opened from inside the vehicle) travels.

**You should not move.** The camera should cut to the slider — behind and
above it, its subject the vehicle — as it spawns at the top of the nearest
road and **drives down it to you**, twenty-one segments of lane in Anekbah,
stopping within 117 units (about 3 m) of the kerb nearest you. It is a real
vehicle out of the same pool the traffic uses.

If the camera does NOT cut to the slider, or you are moved anywhere, that is
wrong.

When it has stopped, **walk to it and press the action button** (`ENTER`). You
should be aboard: arrows steer, up/down thrust, **SPACE** stops it once you are
slow. That is `MDSLIDIN`'s own gate — an active slider, standing open, in
reach.

What to judge: whether it takes a sensible route, whether it stops somewhere
you can actually reach, and whether boarding it feels like boarding rather than
teleporting onto it.

**Knowingly missing.** Once aboard, the slider page should open again (screen
7) so you can confirm the destination and be DRIVEN there — it does not yet, so
you can fly it but not be taken anywhere. The ACTOR_STATE the move writes (7)
is not set (the engine reaches it from state 6 and nothing puts him there), so
he keeps his walking pose aboard; the door animation (`A_SliderIn`) is not
played; the correct side to board from is not enforced; and the optional
cutscene of the slider on its road — the longer one, not the camera cut — is
not found. See `todo/handoff.md` §3.

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

## What is NOT worth testing yet

* the videophone's own picture inside the sneak — the world no longer shows
  through the device's hole, which is correct, but the interface's own 3D view
  that belongs there is not drawn (`omk-play` 83).
