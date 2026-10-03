<h1 class="part" id="part-i">Part I — The game as a machine</h1>

<div class="pagebreak"></div>

# 1. A machine and its content

## Three games in one coat

*Omikron: The Nomad Soul* was made by Quantic Dream and published by Eidos in
1999 for Windows. It is an adventure game in which you walk through a city and
talk to people; a beat-'em-up in which you fight them hand to hand; and a
first-person shooter in which you shoot them. Now and then you swim. Its
central idea is that you are a soul that can move from one body to another, so
the game must be able to hand you a different character at any moment and
carry on.

A game with that much in it sounds like it should need a very large program.
It does not. The executable is about **985 KB**. Everything else, about
**1.7 GB in 2 812 files**, is data.

## The split that makes it readable

What is compiled into the executable is not the game. It is a **machine**:

* an interpreter for a small bytecode language, which runs the game's scripts;
* a runner for state graphs, which moves the characters' bodies;
* a renderer, a sound bank and a 2D interface layer;
* one reader for each file format.

What the game *does* is all in the data. Which conversation starts when you
walk through a doorway, which camera watches it, which animation a character
plays when it turns round, which gunman patrols which corridor: those are
scripts, state graphs, meshes and tables in the files beside the executable.

![The executable is the machine; the data is the game.](figures/fig01-split.svg)
<p class="caption">Figure 1 — The executable is the machine; the data is the game.</p>

This split is the single most important fact in the book, because it is what
made the whole reverse-engineering project possible. Read the machine once,
carefully, and the content reads itself: 5 785 scripts, 320-odd conversations
and hundreds of models become understandable, because they are all run by the
same few hundred functions.

> **From Unity:** think of the executable as the Unity player, the compiled
> runtime you never edit, and of the data as the scenes, prefabs, animator
> controllers and assets of a built game. The difference is that Omikron's
> "player" was written for one game only, so its data formats encode that
> game's ideas directly: a trigger zone, a conversation, a fight.

## What OMK is

**OMK** is the project this repository holds. It has two halves:

1. **The reading.** The findings about how the original engine works, each one
   tied to the function in the executable that establishes it. They live in
   `docs/`, and roughly five hundred automatic checks (`tools/verify.py`) keep
   every number in them honest.
2. **The replica.** `engine/` is a new implementation of the machine, written
   in C++20, which reads *your* copy of the game's data and runs it. It
   contains no game content and never will.

The replica is not an emulator. It does not execute the original machine code.
It re-implements what that code *decides* (which script runs, which state a
body enters, which triangles are drawn in which order) and uses today's
hardware to carry out those decisions.

## The words you will need

A handful of the game's own terms come back throughout:

| term | what it is |
|---|---|
| **area** | a place, such as a street, a flat or a corridor, stored as one chunk of an archive |
| **scene** | a second layer of content that can be loaded over an area: the cutscene material for a particular moment of the story |
| **set** | the 3D model of a place: its walls, floors, signs and lamps |
| **zone** | an invisible box on the floor that runs a script when you enter it, act inside it or leave it |
| **context** | one running script, with its own program counter and stack |
| **channel** | the state-graph runner attached to one body |
| **slider** | the flying car of the game's cities |
| **sneak** | Kay'l's handheld device: the inventory, the map, the notes |

You do not need to remember them now. Each gets its own explanation when the
story reaches it.

<div class="pagebreak"></div>

# 2. One frame

## The loop

Double-click the icon and the executable does five things in order. It starts
its subsystems. It plays three videos (Eidos, Quantic Dream, then a title
sequence). It loads a scene file called `aventure.scx`. It shows a splash
bitmap. Then it enters a loop that runs one frame at a time until you quit.

That loop is an ordinary Windows message pump. The game only runs a frame
when the pump has **nothing else to do**, and only while the window is in the
foreground. Switch to another application and the loop calls `WaitMessage()`,
which really blocks: the process stops using the processor. When you come
back, the first thing the next frame does is **throw away** the time that
passed. Without that, returning after a two-minute break would hand the
simulation a two-minute step.

![One frame of the original engine.](figures/fig02-frame.svg)
<p class="caption">Figure 2 — One frame: the clock, the input, the scripts, the bodies, the scene, the draw.</p>

![The start menu, as the original drew it.](../traces/frames/menu-22.png)
<p class="caption">The start menu in the game's invented alphabet: a frame of the original engine's own framebuffer, captured at 640 × 480 (<code>traces/frames/menu-22.png</code>).</p>

`aventure.scx` deserves a note, because its name suggests a menu. It is not
one. It is the game's global library of effect sprites and sounds. There is
no "menu state" in this engine. The start menu is a screen opened by the
first area's own startup script, over an ordinary scene: the same machinery
that later runs a street. Chapter 5 shows how.

## The clock counts frames

Every clock in the engine (animations, camera moves, the flicker of a neon
sign) is measured in **frames at 30 Hz**, not in seconds. That comes from one
line in the frame function, which sets the time step to

```
dt = 30 / fps
```

At exactly 30 frames per second, `dt` is 1.0. At 60 it is 0.5, and every
clock advances half a frame per rendered frame, so the game runs at the same
speed at any rate. This is why durations everywhere in this repository are
quoted in frames. A clip "136 frames long" is four and a half seconds.

It does not quite *look* the same, though. A body's pose is taken from its
clip by truncating the clock to a whole key, so at 60 frames per second each
key is simply shown twice. The port does the same by default; drawing the
in-between poses is one of its enhancements (chapter 12).

There is one more rule, and it matters for a slow machine: **`dt` is capped at
3.0.** Below 10 frames per second the game does not take larger steps. It
slows down. A replica that integrated real elapsed time on a slow console
would not be more accurate; it would be doing something the original never
did. The port learned this the hard way. On a PS Vita running at 200–300 ms a
frame, an earlier clamp of its own made the camera visibly "shake"; replacing
it with the engine's own cap of three frames fixed it.

> **From Unity:** `dt` is the engine's `Time.deltaTime`, but expressed in
> thirtieths of a second and clamped at 0.1 s, the equivalent of setting
> `Time.maximumDeltaTime`. There is no separate `FixedUpdate`: the whole game
> steps once per rendered frame, on this one variable.

## Two details at the edge of the loop

**Escape is not a key the game binds.** The loop reads it directly, just
before the frame, and opens the pause screen itself. The pause does more than
set a flag: the pause screen's open callback suspends every sound buffer and
music stream, and its close resumes them from where they were.

**The intro films have two skips.** Any key ends the film that is playing;
**Alt ends all three**. One reads a key held down, the other sets a latch that
nothing ever clears. It is a small thing, but it shows the style of the whole
engine: behaviour lives in a few lines of plain code, and every one of those
lines matters.

## What this means for the rest of the book

Everything that follows happens inside one pass around Figure 2. The scripts
run first, then the bodies move, then the scene objects play, then the frame
is drawn, all on one thread. (There is one exception: while a line of
dialogue plays, its voice and facial animation are streamed out of the file
by a multimedia-timer callback, which Windows runs beside the frame. The
game's logic never runs there.) A script that needs to wait for something
cannot block, because blocking would stop the entire game. How the engine
waits without blocking is the subject of chapter 4, and it is one of the most
elegant ideas in it.

<div class="pagebreak"></div>

# 3. Where the game lives

## The shipped tree

The data directory holds a dozen folders. Each belongs to one or two
subsystems:

![The data folders, what they hold, and who reads them.](figures/fig03-data.svg)
<p class="caption">Figure 3 — The data folders, what they hold, and which part of the machine reads them.</p>

The most important is `IAM/`, 67 files. Most of them are **archives**: one
file holding hundreds of numbered chunks behind a directory at its start.
`IAM\AREA` holds the places. `IAM\SCENE` holds the scene layers. `IAM\DIALOG`
holds the conversations. `IAM\GLOBAL` holds the scripts that apply everywhere.
The interface text and the saved games are in `IAM\` too.

## Plain formats

The whole project was possible largely because the formats are **plain**.
Nothing is encrypted, there is no versioning beyond one field, and the little
packing there is (textures are LZ-packed unless an image is exactly 64 KB; the
audio is ADPCM) is simple and decodes exactly. A record is a C struct written
to disk, an array is an array, and its count is usually right there beside it.
A texture is a palette followed by indices. A mesh is a list of records of fixed size, 140,
32, 28, 32 and 52 bytes for its five kinds of record.

This was normal in 1999. Loading was mostly a matter of reading a file into
memory and fixing up the pointers inside it. It explains a style you will see
again and again in the engine: a structure read from disk *is* the structure
the engine uses at run time. Some fields in a file are placeholders that the
loader overwrites the moment it reads them (a texture's slot number, for
instance, ships as −1 in every one of the 2 534 materials), because on disk
they have no meaning yet.

> **From Unity:** there is no asset database and no import step. What Unity
> does once in the editor (convert, compress, build atlases) Omikron's tools
> did offline, and what shipped is already the runtime layout, closer to a
> memory dump than to a `.prefab`.

## A file is often more than its name

Two examples, both found only by reading the code that uses the data:

* A `MAP2D` file is the map the player sees in the sneak, and **also** the
  navigation grid the gunmen walk by in shoot mode.
* A mesh flag that makes a distant skyline **shimmer** (its vertex colours
  oscillate on the frame clock) also marks the bed of a canal you can swim in.

Nothing in the data announces a double use like that. This is the first hint
of a principle Part IV makes explicit: the data can tell you where to look,
but only the code can tell you what a field *is*.

## Case, and a lesson from a console

One more property of the tree turned out to matter the moment the replica ran
anywhere but Windows. **Windows 95 and 98 did not care about letter case in
file names, and the game relied on that.** The executable asks for
`FLIS\EIDOS.mpg` while the disc ships `EIDOS.MPG`. On macOS or Linux that is a
missing file. The replica therefore opens every file through one class,
`DataFs`, which resolves names without regard to case. It resolves all 2 367
files it is asked for under four different manglings of their paths.

The PS Vita port added a lesson of its own. A copy of the `IAM` archives made
with an FTP client in *text* mode had its line endings "corrected", which
silently damaged binary chunks. The symptom was an empty area where the
game's first scene should have been. Binary data must be copied as binary,
and a symptom that looks like an engine bug may be the copy.

## Byte order, and a lesson from a PowerPC

The game ran on x86, so every number in every file is stored
**little-endian**, low byte first. Most of the replica's readers build their
values byte by byte and never noticed. A handful copied four bytes straight
into an integer, which is correct on x86 and on ARM and wrong on a
big-endian processor. When the replica was built for a PowerPC Mac (chapter
14), those reads found **no actors** where 1 032 should have been, 16 map
lines of 79, and a traffic circuit that would not load. Nothing failed to
parse: the numbers were simply other numbers. Every such read now goes
through one helper that assembles the bytes in order, and every tool the
checks run gives the same output on the PowerPC as on the Mac.

