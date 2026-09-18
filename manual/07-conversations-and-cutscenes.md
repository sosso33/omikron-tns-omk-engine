# 7. Conversations and cutscenes

← [Contents](README.md) · prev: [Actors](06-actors.md) · next: [Rendering](08-rendering.md)

---

## In short

A conversation is a little graph. Each node is a line somebody says, with up to
four conditions deciding whether it is available and up to four actions that
run when you pick it. The text, the branching, the voice recording and the
facial animation all ship together; so do the cameras, because a conversation
carries **its own**.

A cutscene is not a different system. It is the same scene machinery the rest
of the game runs: a scene chunk's startup script starts a sequence of
**objects**, each object is a little program that animates a character along a
path, and some of those objects have a **camera editing** attached — a
recorded camera move that takes over the view while the object plays.

Four rules run through all of it, and each was learned by getting it wrong:

* **A shot is as long as its editing**, not as long as the animation inside it.
* **When an editing ends, the camera does not go anywhere.** It holds the last
  frame until the next beat takes it.
* **A spoken line changes a character's pose, never their position.** The line
  is played over wherever the scene already put them.
* **Not everything that waits is a cutscene.** A door sliding open and a gunman
  making his entrance park a script exactly as a cutscene beat does, and
  neither takes the player's control. What does is the engine's own hold — and
  the black bars at the top and bottom of the screen mean exactly that: *you
  are not in control right now*.

## In detail

### The conversation format

`IAM\DIALOG` holds 321 conversations. A node is 64 bytes and carries four
pointer slots twice over, and what separates them was settled by tracing rather
than guessed: the event raised while the reply menu is being built
**evaluates** `ptr[0..3]` for a value — those are the conditions — and the event
raised when a reply is chosen **executes** `ptr[4..7]` in a throwaway context —
those are the actions.

A conversation is launched by exactly one opcode, `dialog.start`, at 1 246
sites. Which character speaks it is resolved through the actor record: `+144`
names the model, `+72` names the `.CTL` bank.

### One line, two cameras

A conversation carries its own 44-byte camera records, and each node names two
pairs — one for the line, one for the reply menu. The line **snaps** to the
first and **travels** to the second over 160 frames, which is 5.3 seconds and
therefore stops well before a long line finishes. A node with no reply pair
keeps the line's pair in force, so opening its menu must not restart the move.

A camera's subject field is not a boolean but a **resolver kind**:

| kind | where the point comes from |
|---|---|
| 0 | the actor record's own `+244`/`+248`/`+252` |
| 1 | the character's `Tete` node — the head |
| 2 | the body node's world origin |
| 3 | the head node's world origin |

**176 of the 253 relative cameras the file ships are one of the two head
kinds** (92 and 84) against 24 of kind 2 — the one kind that had first been
read was the rarest, which is why a camera authored 40 units in front of a
character's face sat at chest height inside them.

A camera *travel* is a lerp of two **solved world points**, carrying its fov
and its roll with it. Both ends are resolved every frame; flattening one of
them freezes the move's start, and a shot that widens 85° → 99.6° mid-travel
shows immediately if the fov is snapped instead of blended.

**The dialogue cameras take no obstruction pass.** A conversation's cameras
are absolute — in dialog 387 all 44 of them — and the engine rebuilds its
camera flags on every change, giving the collision pass only to the follow
camera; the line's issuer also clears the pass's flag explicitly. So when the
restaurant lunch's high crane shot puts part of the ceiling between the lens
and the table, the pass is not what clears it in the original, and three
attempts to port it that way made other shots worse and were reverted. Measured
over the whole conversation, switching the pass off moves 0 frames of its 22
shots. What the original does there is open.

### What a line does to a body — and what it does not

The facial animation and the voice ship in one `.3DM` file, and the pose it
drives blends into and out of whatever the character was already doing over
`min(30, frames / 4)` frames.

The **root rotation** of that line is applied rather than cancelled, and the
reason is an accident in the original: the code that means to replace the root
track with identity indexes it with a variable that is **−2** everywhere in the
image, so the replacement never fires. All 19 recorded rotations reach the
skeleton, which is why the game bows a speaker's whole body toward the camera —
47° of pelvis-to-head pitch where cancelling it gives 3°.

The **heading** is taken once. `Morph_Play` reads the node's world yaw, less the
actor's Euler, at the line's first frame and **holds it for the whole line**,
whatever the scene program does after. The viewer got this wrong three ways in
a day, each caught by a transition and never by a still frame: with no yaw at
all a speaker snapped 80° when her idle came back; without the clip's root yaw
she stood about 90° off the engine's own frame of the greeting; and re-reading
it every tick snapped her by about a hundred degrees when her scene program
ended mid-line.

The **root translation** goes nowhere near the body's position, and this is the
rule the port got wrong twice. The store that would write it into the node sits
behind the same −2 index, so it matches no track; what the translation is
actually used for is an offset from an origin **latched once at the line's
start**. A line therefore moves a body *from where it already stands*, and the
scene's own placement stays under it.

Weighting that placement by the line/idle fade — which is what "the fade owns
the position" amounts to — made a speaking character drift back to her bare
staged spot and lerp there and back. A reader watching Telis in the restaurant
described it exactly: *the interpolation between the two makes it look like the
character is flying*, and named the correct end, because her face left the
(correct) camera while she spoke. It survived five days of green checks,
because every check on this machinery asserted **the pose**, and nothing
asserted where the body stood.

### Cutscenes: beats and editings

A cutscene is a scene chunk whose `+4` startup script plays a series of scene
objects. For the Impasse — the game's first — that script fires all **sixteen**
beats in the authored order and then hands off to the next scene with a
`scene.load`.

Some of those objects have a **camera editing** linked to them: a recorded move
in the scene file's chunk 10. 29 scenes carry one, 125 shots in all, and the
port samples them against an independent reader at **24 112 of 24 112 frames**.
While an editing drives, it is sampled at *the object's own program clock*, so
the shot and the animation cannot drift apart — they are the same clock.

Three things happen at the end of one:

**The shot lasts as long as the editing.** The object is stopped only when both
its step chain and its editing are done. A replica that requires the program to
be running drops the tail of a shot: one Impasse beat ends its steps at frame
110 of a 185-frame editing.

**The camera holds.** The engine's fall-back that puts the camera back on the
player is gated on an ini key, `autocameraplayer`, read with a default of `"0"`
and with no other writer in the whole image. So in a shipped game **the view
freezes on the editing's last frame** until something else asks for a camera.
Cutting back to the last world camera instead drew **0 of 480 000 pixels lit**
in every gap between beats.

**And nobody takes the body.** Between two beats no program is driving the
characters, and nothing needs to: the node keeps whatever pose and place the
last step left. **A finished animation does not re-pose** either — on the tick
a run count is spent the engine returns without writing the node — and
recomputing the frame there snapped bodies back to their clip's start for a
frame at the end of every beat.

**An actor is driven by one scene program at a time.** Starting a second
program on the same actor stops the first; appending instead left a looping
idle fighting the new beat for the same body.

### Staging a character

Where a body stands when a scene program places it depends on which of two
functions the object uses, and they do not agree:

* `Script_SelectBodyAnimation` snaps the node to the clip's **root key 0** —
  the authored placement — and adds the per-frame deltas from there.
* `Script_SelectRelativeBodyAnimation` never reads the clip root at all: it
  places the character on an authored **`.3DP` path**, named by two of its own
  parameters, minus an offset in three more.

Both were once read as the first, and for one conversation that put the speaker
365 units from where the path puts her. And every body-animation step writes
the actor's Euler, which turns both the root motion and the head aim — read as
sticky instead, a waiter's walk cycle sent him through the walls.

A path can be **turned into the set**, too. One lift or one door is authored
once and thirty-odd halls install it at their own angle: `Script_MoveObjectOnPath`
carries three degree parameters and rotates the whole path about its starting
key before either placement arm. 854 of the corpus's 4 841 calls carry one;
851 turn about Y alone, and the other 3 use Euler orders this tree has not
established, so they are counted and left alone rather than guessed.

The invariant that settles staging is a corpus one: a program's steps
**chain**, so a correct reading re-places the body where it already stands.
Over one scene's 68 hand-overs the mean gap is 0 units and the worst is 2; read
as sticky, 28 and 628.

### The letterbox, and what "a cutscene" means to the player

The engine's 1.818:1 **letterbox** is the same two 64-row bands its screen fade
draws, and the captures decide when it shows: every letterboxed frame in
`traces/frames` is one the player does **not** control — conversations, and a
cutscene on a scripted world camera — while no interface frame has it. The
rule that holds up in play is *no control, and not held, and the fade actually
darkening the bands this frame*. It took three rounds, each refuted by playing:
keyed on the follow camera, the bars never came off at a door that left an
absolute camera up; keyed on control alone, they came off before a scene's
closing fade and snapped instead of fading; keyed on a fade being armed, they
never came off after a boot, because the first area's startup script arms one
and never clears it.

Two things that look like cutscenes are not, and a reader flagged both:

* **A gunman's entrance is gameplay.** In the engine only `player.anim.hold`
  blocks the player's input; `camera.set` and `scx.play*` do not. An enemy
  appearing with a special animation must not freeze you or take the camera.
* **A sliding door is not a beat.** A waiting `scx.play` on a door parks its
  caller exactly as a beat does, and treating "a script is parked on a program"
  as a cutscene brought the bars down and hid the player for the length of a
  door.

## Where it lives

| | |
|---|---|
| the findings | `docs/FILE_FORMATS.md` §2 and §5c, `docs/CUTSCENES.md`, `docs/ASSETS.md` §8, `docs/UI.md` §3j (the letterbox) |
| the port | `engine/src/script/dialogue.*`, `scenerunner.*`, `program.*`; `engine/src/o3de/camedit.*` |
| the viewers | `/dialog` and `/cutscene` in `tools/omkweb.py` |
| the checks | `engine: cam mode 13`, `engine: cam editings`, `engine: editing hold`, `engine: frame hold`, `engine: beat handover`, `engine: pose blend`, `line facing`, `letterbox`, `dialog staging`, `impasse beats`, `engine: camera obstruction` |

## What is not settled

* **Where a body stands through a spoken line has no check.** The rule is
  established and ported, and a conversation can now be reached headlessly from
  a save; nothing yet asserts the position across a line.
* **What clears the lens** when an absolute dialogue camera has scenery in
  front of it — the restaurant crane — is open; it is not the obstruction pass.
* **Three placement jumps at beat *starts*** in the Impasse are unread: a new
  object snaps a character to its own clip's root key 0, which is what the
  engine does, but whether those three placements are authored that way has not
  been established. The check pins the count.
* **Two Euler orders** of `Script_MoveObjectOnPath`'s turned paths (3 calls)
  are counted, not read.
* **105 of the 321 conversations are launched by no script** — chapter 13.
* **The `/dialog` web viewer still stages from the clip root** for objects that
  use the relative function. It is a viewer convenience the engine does not
  have, and it is labelled as one.
