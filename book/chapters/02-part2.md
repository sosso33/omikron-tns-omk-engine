<h1 class="part" id="part-ii">Part II — How the world behaves</h1>

<div class="pagebreak"></div>

# 4. A tiny computer inside the game

## Bytecode

When you walk into a doorway in Omikron, a script runs. It is **bytecode**:
one byte names the instruction, and its operands follow. A table compiled
into the executable has 153 entries, each a pair: the address of the handler
function, and how many operand bytes the instruction takes. The interpreter
reads a byte, looks it up, calls the handler, and advances past the operands.

There are **5 785** such scripts in the shipped game, and between them they
say almost everything the game does: *this conversation starts here*, *this
door opens*, *this camera watches*, *this variable is now 1*, *this gunman
patrols that corridor*.

The instruction set is small and specific. Here are a few of its opcodes, with
the names this project gave them (the original's names were not shipped):

```
dialog.start   272        start conversation 272
ui.open        29, -1, -> variable 19   open screen 29, store its answer
camera.set     2148       cut to camera 2148
scene.load     237, 57    load scene 57 over area 237
music.play     15         play TRACKS\15.ADP
render.grey.on            switch the scene renderer to greyscale
```

The last one is a nice surprise. Opcodes 150 and 151 swap the whole scene
renderer to a second, greyscale version and back, and every use of them
brackets a run of camera instructions. So the game has **black-and-white
cutscenes**, switched on by the script that plays them.

> **From Unity:** a script here is closer to a coroutine than to a
> MonoBehaviour. It has no `Update()`. It runs from its first instruction
> until it has to wait for something, and it resumes exactly where it
> stopped. How it "waits" without `yield return` is the next section.

## How a script waits

This is the idea to take away from the chapter.

The whole game is one loop with no threads (chapter 2). So a script that opens
a menu, or starts a fight, or waits for a character to finish walking, **must
not block**: blocking would freeze everything, including the menu it is
waiting for.

Instead, each running script, called a **context**, has a **status word**. The
interpreter runs a context only while its status is 1:

```c
while (ctx->status == 1)
    execute_one_instruction(ctx);
```

An instruction that has to wait simply writes a different number into its
own context's status word and returns. The interpreter loop sees the status
is no longer 1, and stops running that script. Its program counter and its
stack are untouched. The script is **parked**.

![A script parks, and something else wakes it.](figures/fig04-park.svg)
<p class="caption">Figure 4 — A script parks by writing its own status; the event that answers it writes 1 back.</p>

Later, whatever it was waiting for happens (the screen closes with an answer,
the fight ends, the camera move finishes) and the code that handles that
event writes 1 back into the status word. On the next pass the interpreter
runs the script again, from the next instruction. **Nothing polls.** Every
resume is an event.

The status numbers name what the script is waiting for: 3 for a fight, 4 for
a scene object or a player move, 6 for a screen, 7 for a camera move, and
8 to 11 for the stages of an area transition. One value, 5, is used for a
transition that a second transition has overtaken, and nothing in the whole
executable ever resumes it. That is not a gap in the reading. It is the
original's own shape: such a script is parked for good.

This design is why a conversation can interrupt a cutscene, why an area
transition can take three seconds of streaming without the game stopping,
and why the engine's logic never needed a thread.

## A script belongs to a place

Scripts are stored inside the places they belong to, and they name things
(scene objects, cameras) by small numbers that are **local to that place**.
Object 12 in one scene is not object 12 in another. Because two places are
loaded at once (chapter 5), a script must always be read against the place it
came from. An early search for "which scripts start the Impasse's cutscene"
found eight candidates across the corpus. Every one of them was another scene
addressing its own object 12, and the real answer was zero: that cutscene is
started by something else entirely, the scene's *startup script*, which the
next chapter introduces.

## How the game starts talking to you

You can now follow the first minute of a new game with the real mechanism.

1. The frame loop loads the starting area, number 118, "Introduction Kay'l".
2. Loading a place **starts its startup script**. Nothing needs to name it;
   arriving *is* the trigger.
3. That script reaches `ui.open(29, ...)`, which opens the start menu and
   parks the script at status 6.
4. You type a name and confirm. The screen's answer is written into variable
   19 and the context's status goes back to 1.
5. The script carries on and reaches `dialog.start 272`: Kay'l's first
   conversation.

The replica does exactly this, with nothing wired by hand, and its record of
what the scripts did matches a capture of the original game, event for event:
42 of 42 in order. Part IV explains how a capture of the 1999 executable was
possible at all.

<div class="pagebreak"></div>

# 5. Places

## A place is a chunk

The world is a set of **places**, and a place is a chunk in an archive. An
area chunk carries its set, its characters, its props, its scripts, its shop
stock and its trigger zones. A scene chunk, loaded over an area, carries the
material for a moment of the story: which objects appear, what they do, which
cameras film them.

Four ideas carry the whole subject.

## 1. A place runs a script the moment it loads

A chunk's offset `+4` holds its startup script. The loader hands it to a new
context and queues it as soon as the chunk arrives. 173 of the 330 area and
scene chunks carry one, and all 173 disassemble cleanly. This is what starts a
new game (chapter 4), and it is also what orders a cutscene's beats: the
Impasse cutscene's sixteen beats are fired, in order, by its scene's startup
script, which then hands over with `scene.load(237, 57)`.

That fact stayed hidden for a while, and the reason is instructive. Every
search for "who starts these beats" enumerated the scripts found in the trigger
zones and the message subscriptions, 5 785 of them, and none of them did. The
search was correct. The *inventory* was incomplete: startup scripts were not in
it. A negative result is only as strong as the list it searched.

## 2. Everything else is a trigger zone

A **zone** is a quad on the floor, plus an arc saying which way you have to be
facing, plus up to three scripts: one for entering, one for pressing the action
button inside it, one for leaving. There are **4 558** of them. Each has one
bit in the saved game, which is how a one-shot event stays shot.

> **From Unity:** a zone is a trigger collider with `OnTriggerEnter`,
> `OnTriggerExit` and an "interact" event, except that the three handlers are
> bytecode stored in the level, and the "has this fired?" flag is part of the
> save file rather than of the object.

## 3. Two places are loaded at once

When you walk out of a street into a building, the street is not thrown away.
It stays resident in the second of two **slots**, its animations still running.
Walking back is not a reload.

![Two places are resident at once.](figures/fig05-slots.svg)
<p class="caption">Figure 5 — The active slot is drawn; the hidden one is not, but it is still loaded and still solid.</p>

## 4. Hidden is not gone

A place that is loaded but not shown is **still solid**. Its walls still stop
you, and a step onto its floor is undone. One building in the game is laid out
around that property.

It has a subtler consequence too, in the texture cache. The engine hands out
58 texture slots from one global pool and matches textures by **file name
alone**. When a new set loads while the old one is still resident, a texture
with the same name is taken from what is already in memory, even if the new
set's copy of that file has different pixels. 182 texture names ship with
different pixels in different sets. So some Anekbah signs genuinely draw a
little differently depending on which neighbouring place you walked in from.
The port reproduces that, because the original did it. It is one of several
places in this book where being faithful means reproducing a quirk.

## State that survives

Everything the story remembers lives in one block of **8 192 bytes**: the
variables scripts set, the zone bits, the inventory, the player's record. A
new game starts from a template, `IAM\START`, which is itself just a saved
game with no scripts in it. The save file stores the same block, plus the
settings: saving a game also saves your options. A save costs one ring,
*anneau*. (What happens when you have none is one of the places where the
records still disagree with each other, and the manual lists it as open.)

<div class="pagebreak"></div>

# 6. Bodies

## A character is a tree of meshes

A character model (`.3DO`) is a hierarchy of meshes: a pelvis, a chest, the
upper and lower arms and legs, a head, each parented to another by an ID.
The **pelvis is the root** in all 181 character models. An animation clip
(`.ani`, or `.3DA` for a scene's own clips) stores one rotation per mesh per
frame as a quaternion, plus a root track for movement. Posing a body means
walking the tree, composing each mesh's rotation with its parent's, and
transforming its vertices.

One detail catches everyone: **key 0 of a track is a rest pose, not frame 0.**
A clip of *N* frames holds *N + 1* keys. Read key 0 as the first frame and
every loop shows a one-frame T-pose.

> **From Unity:** this is a skinned mesh, except that each vertex belongs to
> exactly one bone. It is rigid skinning, with no weights. A body is a
> hierarchy of GameObjects each carrying its own piece of mesh, and a clip is
> an AnimationClip of local rotations. There is no blending tree beyond a
> short cross-fade.

## A body's behaviour is a state graph

Every character, the one you steer and the ones you do not, is driven by the
same machine: a **state graph that shipped on the disc**, a `.CTL` file. Each
state names an animation. Each transition names the **input bits** that take
it. Press forward, and the graph moves from standing to walking, because an
author drew that edge. Press the action button next to an object and it walks
a chain of states: an adjusting step into position, a reach, a stand-up with
the object in hand, a wait, and then, on your next press, into the bag or
cancelled.

![A body is a state graph.](figures/fig06-ctl.svg)
<p class="caption">Figure 6 — A state graph (illustrative: <code>H_STAND</code> and the take chain, from <code>todo/take-animation.md</code>, are real; the walk and run states are drawn for the idea).</p>

The input is **fourteen bits**, one word shared with the interface. Which key
sets which bit depends on the context. There are four control schemes
(adventure, swimming, shooting, fighting), each binding 14 actions on three
devices. Entering a fight installs the fight scheme, entering shoot mode the
shoot scheme.

This makes something that looks complicated turn out simple. "The player draws
a gun", "the player dives" and "the player takes a step" are the same kind of
event. The fight and shoot modes are not separate engines. They are different
input schemes feeding the same graph, and different things pressing the
buttons.

> **From Unity:** a `.CTL` is an Animator Controller in which every transition
> condition is a bit mask over the input word, and in which the states carry
> gameplay data too: a hit's damage, a window in which it can be cancelled, a
> sound to play on a given frame, a "special move" to call.

## Who presses the buttons

In a **fight**, the opponent's AI presses combinations of bits into *his own
input queue*, exactly as your keyboard presses into yours. Its behaviour comes
from a profile in the `.CTL`: lists of button combinations, each with a
percentage it rolls against and a waiting time. Harder levels wait less. The
fight's difficulty is adaptive: the scripts raise the AI level when the last
fight cost you under 30 life, and lower it when it cost 70 or more.

In **shoot mode**, each gunman has a small brain picked by character type, and
it walks the level's navigation grid, the same `MAP2D` file the player sees as
a map.

## The walker

Around the graph sits a **walker** that decides whether a step is possible: a
slope steeper than 30° is a wall, a ledge up to 30 cm can be climbed, a fall is
sorted into tiers by height. It knows when the floor is water, which puts the
body into the swimming state. A jump is a clip whose forward motion (2.5 m) and
flight time come from the animation itself.

The state numbers the engine keeps for a body, `ACTOR_STATE` 0 to 17, name
these conditions: 14 is water; 7 and 8 are mounting and riding the slider, the
game's flying car.

## The crowd

A city's crowd is not a set of placed characters. It is a **traffic circuit**,
a `.OPT` file of lanes, routes and junctions. When an area loads, the engine
walks along every pedestrian lane and places a walker at regular intervals,
with the spacing set by the street-activity option (0 to 4) and a value from
the circuit's header:

```
one walker every 39 × (5 − density) × h[3] units of lane
```

At the highest setting walkers are `39 × h[3]` apart; at the lowest, five
times sparser. The walkers follow their lanes, overtake,
wait for one another at shared junctions, and push the player aside with a
simple sphere test. Vehicles, sliders and motorbikes, use the same circuit's
vehicle lanes.

![Anekbah's main street, drawn by the port.](../manual/images/anekbah-street.png)
<p class="caption">The running example: Kay'l in Anekbah's main street with the crowd, the set's lights, the blob shadows and the dither, drawn by the port (<code>manual/images/anekbah-street.png</code>, with the command that made it in that folder's README).</p>

This matters later, in Part V. The port's crowd is **exactly the original's
crowd**: the same rule, the same circuit, the same models. When the port was
slow on a handheld, it was not because it drew more people than the game did
in 1999.

<div class="pagebreak"></div>

# 7. Stories: conversations and cutscenes

## A conversation is a little graph

Each node of a conversation is a line somebody says. It has up to four
**conditions**, evaluated to decide whether a reply is offered, and up to four
**actions**, executed when you pick it. The two sets were told apart only by
following the code: one event handler *evaluates* the first four pointers for
a value while the reply menu is built; another *executes* the second four
when you choose.

A line's text, its voice recording and its facial animation ship together:
the `.3DM` file holds the face's animation **and** the ADPCM audio of the
voice. A conversation also carries **its own cameras**.

There is one open mystery here worth knowing about. 105 of the 321
conversations have no way to be started: no script anywhere starts them. The
likeliest reading is that they were **cut late**. Their text is as complete as
the rest, but only 14% of them have the expensive facial animation, against
68% of the ones that are used.

## A cutscene is not a separate system

A cutscene is the ordinary scene machinery. A scene chunk's startup script
starts a sequence of **objects**. Each object is a little program of steps:
play this clip, move along this path, play this sound. Some objects have a
**camera editing** attached, a recorded camera move that takes over the view
while the object plays.

A program's steps can be linked into **sync chains**, so that a sound starts
exactly when a clip reaches a given frame, or two steps run together. And a
body placed by a scene is placed along an authored `.3DP` path, not at the
position stored in its clip, which is a detail that took several wrong
attempts to settle.

## Rules learned the hard way

* **A shot is as long as its editing**, not as long as the animation inside it.
* **When an editing ends, the camera does not go anywhere.** It holds its last
  frame until the next beat takes it.
* **A spoken line changes a character's pose, never their position.** Nor
  does anything else during a conversation: a body in the dialogue states
  plays its clip but is not moved by it, even one caught mid-stride.
* **A conversation's camera eases; a script's does not.** A dialogue camera's
  move speeds up and slows down along a curve. Every one of the 1 019 camera
  moves the scripts wait on asks for the linear curve, and all of them
  advance on `dt`, so a move lasts the same time whatever the frame rate: the
  title sequence's 23 moves stay on its music.
* **Not everything that waits is a cutscene.** A door sliding open parks a
  script exactly like a cutscene beat, and does not take away your control.
  What takes it away is the engine's own *hold*, and the black bars at the
  top and bottom of the screen mean exactly that: *you are not in control
  right now*.

> **From Unity:** a camera editing is a Timeline track driving a Cinemachine
> virtual camera; an object's program is a small Timeline of its own; and the
> startup script is the PlayableDirector that starts them. The difference is
> that everything, including when the camera stops, follows from a few
> engine rules rather than from what the author dragged onto a track.

