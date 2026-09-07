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

Three rules run through all of it, and each was learned by getting it wrong:

* **A shot is as long as its editing**, not as long as the animation inside it.
* **When an editing ends, the camera does not go anywhere.** It holds the last
  frame until the next beat takes it.
* **A spoken line changes a character's pose, never their position.** The line
  is played over wherever the scene already put them.

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
therefore stops well before a long line finishes.

A camera's subject field is not a boolean but a **resolver kind**, and three of
the four had gone unread:

| kind | where the point comes from |
|---|---|
| 0 | the actor record's own `+244`/`+248`/`+252` |
| 1 | the character's `Tete` node — the head |
| 2 | the body node's world origin |
| 3 | the head node's world origin |

**176 of the 253 relative cameras the file ships are one of the two head
kinds** (92 and 84) against 24 of kind 2 — the one kind that had been read was
the rarest, which is why a camera authored 40 units in front of a character's
face sat at chest height inside them.

And a camera *travel* is a lerp of two **solved world points**, carrying its fov
and its roll with it. Both ends are resolved every frame; flattening one of
them freezes the move's start, and a shot that widens 85° → 99.6° mid-travel
shows immediately if the fov is snapped instead of blended.

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

The **root translation** goes nowhere near the body's position, and this is the
rule the port got wrong twice. The store that would write it into the node sits
behind the same −2 index, so it matches no track; what the translation is
actually used for is an offset from an origin **latched once at the line's
start**. A line therefore moves a body *from where it already stands*, and the
scene's own placement stays under it.

Weighting that placement by the line/idle fade — which is what "the fade owns
the position" amounts to — makes a speaking character drift back to their bare
staged spot and lerp there and back. A reader watching Telis in the restaurant
described it exactly: *the interpolation between the two makes it look like the
character is flying*, and named the correct end, because her face left the
(correct) camera while she spoke.

It survived five days of green checks, and the reason is worth carrying: the
check on this machinery asserts the fade's arithmetic, its lengths and the
root-kept bow — **the pose** — and nothing asserted where the body stood.

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

Three things happen at the end of one, and the port had all three wrong until
2026-09-07:

**The shot lasts as long as the editing.** The object's own function that runs
the chain returns "still busy" while its editing plays, and the object is
stopped only when *both* the chain and the editing are done. A replica that
requires the program to be running drops the tail of a shot: one Impasse beat
ends its steps at frame 110 of a 185-frame editing, and the last 75 frames of
that shot went missing.

**The camera holds.** The engine has a fall-back that puts the camera back on
the player, and it is gated on an ini key — `autocameraplayer` — which is read
with a default of `"0"` and has no other writer in the whole image. So it never
fires in a shipped game. The mode stays as it was, the camera tick copies
nothing from an absent active camera, and **the view freezes on the editing's
last frame** until something else asks for a camera. The port cut back to
whatever world camera it last held — which, on the intro path, belongs to the
area you have just left — and every gap between beats drew **0 of 480 000
pixels lit**.

**And nobody takes the body.** Between two beats no program is driving the
characters, and nothing needs to: the engine has one actor record per
character, its node keeps whatever pose and place the last step left, and there
is no second owner to hand it to. A replica with a separate player controller
will hand it over for that one frame — the port did, and Kay'l snapped to his
placement record 3 400 units away, in his idle stance, for a frame at a time.

The same rule, one level down: **a finished animation does not re-pose.** On the
tick a run count is spent the engine returns without writing the node, so it
keeps the frame the previous tick wrote. Recomputing the clip frame there finds
no live animation and reads frame 0 — the clip's authored *start* — so the body
snapped back to where its animation began on the last frame of every beat, held
there through the hand-over, and returned. Out and back over two frames, which
is what a reader described as *max 5 frames*.

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

The invariant that settles it is a corpus one, and it is the kind this
repository prefers: a program's steps **chain**, so a correct reading re-places
the body where it already stands. Over one scene's 68 hand-overs the mean gap
is 0 units and the worst is 2; read as sticky, 28 and 628.

## Where it lives

| | |
|---|---|
| the findings | `docs/FILE_FORMATS.md` §2 and §5c, `docs/CUTSCENES.md`, `docs/ASSETS.md` |
| the port | `engine/src/script/dialogue.*`, `scenerunner.*`, `program.*`; `engine/src/o3de/camedit.*` |
| the viewers | `/dialog` and `/cutscene` in `tools/omkweb.py` |
| the checks | `engine: cam mode 13`, `engine: cam editings`, `engine: editing hold`, `engine: frame hold`, `engine: beat handover`, `engine: pose blend`, `dialog staging`, `impasse beats` |

## What is not settled

* **Where a body stands through a spoken line has no check.** The rule is
  established and ported; asserting it needs a running conversation the
  headless harness cannot yet open.
* **Three placement jumps at beat *starts*** in the Impasse are unread: a new
  object snaps a character to its own clip's root key 0, which is what the
  engine does, but whether those three particular placements are authored that
  way has not been established. The check pins the count so one of them cannot
  change unnoticed.
* **105 of the 321 conversations are launched by no script** — chapter 13.
* **The `/dialog` web viewer still stages from the clip root** for objects that
  use the relative function, and still cancels the line's root. It is a viewer
  convenience the engine does not have, and it is labelled as one.
