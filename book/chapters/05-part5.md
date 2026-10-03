<h1 class="part" id="part-v">Part V — Rebuilding it</h1>

<div class="pagebreak"></div>

# 12. The shape of the replica

## What it is

`engine/` is the replica: C++20, roughly 60 000 lines in the engine proper and
33 000 in the backends that draw and play it, and **no required dependency**.
`make` on a machine with nothing installed builds every probe and passes the
test suite. SDL and a Vulkan loader are optional; they buy you a window.

![The port's shape.](figures/fig09-port.svg)
<p class="caption">Figure 9 — One engine; reference and live implementations behind one boundary.</p>

The engine's directories follow the original's subsystems:

| directory | what it holds |
|---|---|
| `formats/` | one reader per file format |
| `script/` | the **Session** (the resident places, the transitions, the frame), the VM's handlers, the zones, conversations, the game-state block, scene objects |
| `actor/` | the `.CTL` channel, the player's controller and cameras, posing, the walker, the slider, melee, shoot mode, the crowd and its vehicles |
| `o3de/` | the bucket keys, the 58-slot texture cache, the visible-set walk, particles, the depth tie, collision |
| `ui/`, `audio/`, `input/` | the interface, the sound decisions, the four control schemes |
| `platform/` | `DataFs` (every file access, case-insensitively), boot, films, threads |
| `app/` | what a frontend needs and SDL does not: the command line, the game's state (no drawing in it), shared helpers |

## The one design decision

Every output subsystem has **a reference implementation and a live one**
behind **one boundary**:

| | reference | live |
|---|---|---|
| picture | a software rasterizer into an RGB565 framebuffer | Vulkan (MoltenVK on macOS), GLES2 for the PS Vita, and fixed-function OpenGL 1.x for a PowerPC Mac |
| sound | PCM buffers | an audio device |
| input | a replayable event stream | a keyboard, or a gamepad as the engine's joystick |

The reference is what the checks measure. The live one is what makes the
replica playable. Neither may be required to build the other.

The renderer boundary is placed at the level of **decisions**, not API calls.
The engine does not say "bind this texture, draw these vertices". It says
"here is a draw: this bucket key, this range of this geometry, this blend,
this cutout". A backend turns decisions into API calls, and never makes a
decision itself.

```
begin(view)        the camera and its frustum
submit(draw)       { bucketKey, geometry, range, blend, cutout }
end()              the frame
```

Put the boundary at the API level instead, and the ported decisions, which
are the thing that was actually reverse-engineered, would leak into
Vulkan-specific code. A second backend would mean extracting them again. The
boundary is what made four backends possible.

> **From Unity:** it is the difference between writing game code against
> `Graphics.DrawMesh` and writing it against the Vulkan API. The engine speaks
> in draws and materials, and each backend is a render pipeline.

## Tables that are not in any file

Some of the game lives in the executable itself: the opcode table, the
interface's widget tree, the key bindings, the special moves, the camera
presets. A replica cannot read those out of the data directory, so they were
**lifted to JSON** (`tables/`), each with a self-check that re-derives it from
the executable and compares.

## Enhancements are off

A second rule sits beside the boundary: **anything the original did not do is
an enhancement, and every enhancement is off by default.** Multisampling,
mipmaps, real shadow maps, per-pixel light, supersampling, a 60 fps pacer,
bodies smoothed between animation keys, text scaled with the screen: all
exist, all behind a flag. (Bilinear texture filtering was on that list until
it was read that the original's 3D-card device set it; it is now the default
on every GPU backend.) A replica judged against the original must draw what
the original drew unless told otherwise.

The flip side is just as important. Before building an enhancement, check
whether the game already did the thing and the port had dropped it. The
dither and the shimmer (chapter 8) were found that way: they looked like
missing features and were missing *fidelity*.

<div class="pagebreak"></div>

# 13. Running the world

## The Session

The heart of the replica is the **Session**, in `script/area.cpp`. It holds
the two resident place slots, runs the transitions between them, owns the
script contexts, and advances one frame. It is the port's counterpart of the
original's frame function, and it keeps the original's order: scripts, then
bodies, then scene objects, then the camera, then the draw.

It is also where the evidence meets the code. The Session's record of which
scripts ran and what they announced is what is compared against the golden
traces. From a cold start, it reproduces the capture of the game's opening
**42 of 42 events, in order**, with nothing wired by hand. The start menu,
the name field and the first conversation all come out of AREA 118's own
startup script.

## The frame loop of the viewer

`omk-play` is the playable frontend: SDL, a window, the pad, the audio device.
It was once one `main` of over 21 000 lines. It is now a `PlayState` object
whose `main` is 57 lines: the setup in sections, one turn of the loop in six
phases (input, control, modes, world, screens, present), each phase in parts,
and the GPU window in one file per backend, so that no game code carries a
backend `#if`. The split was proven by a record of 28 scenes, each the same
frame before and after. Each frame it asks the Session to step, poses the
bodies, gathers the draws, and hands them to a backend.

It also carries its own instruments, and they are part of how the port is
developed: a frame's cost is marked section by section, a slow frame prints
what took the time, a `--dump` writes the framebuffer, and `--flicker` catches
a fault too short to screenshot by watching for frames much darker than their
neighbours. They sit behind one seam, so a build for players can leave them
out (`INSTRUMENTS=0`).

## Determinism

A run limited by `--frames` uses a fixed step of exactly one engine frame, so
the same command gives the same frames. That is what lets a check render a
street, change one line of code, render it again and compare the two dumps
byte for byte. Most of the speed work in the next two chapters was proven
exactly that way: **same output, less time**.

<div class="pagebreak"></div>

# 14. Four renderers, and two faces in the same place

## The software rasterizer

The reference renderer is a CPU rasterizer into a 640 × 480 RGB565 buffer,
following the original's conventions (the right-handed world with **Y pointing
down**, the bucket order, the strict depth test). It is what `verify.py`
measures, and against the original's own captured framebuffer it reaches tier
4 for one camera: the apartment through dialogue 402's camera, compared by
edge alignment and coverage against a chance floor, not pixel by pixel.

It is too slow to play the city. It does not need to be fast.

## Vulkan and GLES

The live renderers turn the same draws into GPU work. Vulkan, through MoltenVK
on macOS, is the desktop one. GLES2 was written for the PS Vita, whose GPU is
programmed through vitaGL, an OpenGL ES 2 layer. Both agree with the software
reference on coverage, to 99% or better (Vulkan 0.995 through dialogue 402's
camera; GLES 0.996 there and 0.992 on the street), which is the right measure
for a GPU:
a driver's rasterisation rules and rounding are its own.

They also filter. The original had two device set-ups: a 3D card got
**bilinear** filtering, a software device **point** sampling. The live
renderers stand where the card stood, so they filter by default; the software
reference stands where the software device stood, so it does not. That
agreement is measured with both on point sampling - the comparison is about
which triangles land where, not how a texel is blended.

The GLES backend goes further than the original ever did. It **poses and
lights the characters in its vertex shader**: the rest geometry stays on the
GPU, and each frame sends only one matrix per mesh and the lights that reach
the body. The original did all of that on the CPU. A handheld needs it,
because what a Pentium II did for this game's bodies is a large part of a
Cortex-A9's frame.

## A fourth, for a 1999 Mac

The fourth renderer answers a what-if: how would Omikron have looked ported
to a Macintosh of its own day? Such a machine has no shaders and, in 1999,
mostly no hardware transform either. So this backend uses **OpenGL 1.x fixed
function** and does what the original did with Direct3D: one draw per bucket,
a render state changed only when it differs, the texture chosen by the key's
low six bits, 16-bit textures, the colour key as an alpha test, and the
vertices transformed, clipped and culled **on the CPU**, with the fog
computed per vertex. Doing the CPU's share by hand was faithful, and it was
also forced: under emulation, the card's own transform let triangles that
cross the near plane through as garbage, and its fog darkened the near scene.

It runs, cross-compiled, on Mac OS X 10.4 on PowerPC, in an emulator: the
films, the menu and Anekbah's street at 6 to 10 frames per second, against
about one for the software rasterizer on the same emulated machine. It
agrees with the reference on coverage to 0.993. Getting there needed the
byte-order fix of chapter 3, a big-endian build of every tool, and one more
lesson about floating point: the PowerPC compiler fuses a multiply and an add
into one instruction that rounds once, where the Mac rounds twice, and a
300-frame traffic run drifted by a tenth of a unit until fusion was turned
off on both sides. The original, on an x87, had no fused operation at all.

Mac OS 9 itself has no SDL. So the viewer's game code now reaches the host
(the window, the keys, the sound, the clocks) through **one interface**, the
renderer boundary's counterpart for the host, and compiles without a single
SDL header. A Carbon frontend will replace SDL by implementing that one class.

The engine underneath is already there. Built once as a Carbon program, the
headless boot runs on Mac OS 9 and on Tiger, and what it decides is
byte-identical to the Mac's. The last obstacle was the most instructive. A
file stream crashed on start-up, but only in a large program: a small test
passed, and the same test crashed as soon as about two megabytes of other
code were linked beside it, the engine's or anyone's. The cause was in the
toolchain. Its linker gave two of the C++ library's objects the same
address, so the library's own locale set-up overwrote one of them. The
engine now reads its files without C++ streams at all. The lesson is one
Part IV would recognise: a test too small to reach the fault proves nothing
about the program that does.

## Two faces in the same place

Chapter 8 said that when two faces occupy the same place, the first drawn
keeps every pixel, from every angle, with no flicker. A strict depth test on
a **quantised** depth buffer produces that: two faces at the same depth
quantise to the same value, the second is not strictly nearer, and it loses
every pixel. A later face wins only if it is nearer by a whole step of the
buffer.

A GPU does not behave that way. Two faces that split the same quad along
different diagonals have interpolated depths that differ in the last bits
(about two parts in ten million), and the depth test picks per pixel
wherever the noise falls. The result is **dots of the other face**, re-rolled
by every sub-pixel camera move. Asking for a 16-bit depth buffer with the
same strict test is not enough: a rounding boundary still falls inside the
noise.

The port met this first on Anekbah's shop signs, and drew the wrong
conclusion from them for three weeks. A sign's two sides are the same four
vertices with an advert each, and they flickered in the port. The tie was
blamed. The real cause was that the port drew the back of the sign at all:
the original culls it (chapter 8), so its two sides never compete. Once the
port culled too, Anekbah's competing faces fell from 248 to 87. But 87 is
not zero. The rest are faces that really do share a place and a side, and
they still need the original's answer.

![The original's answer, a GPU's, and the port's.](figures/fig08-tie.svg)
<p class="caption">Figure 10 — Two coincident faces: the original, a GPU float compare, and the port's depth tie.</p>

The port's first answer was the **depth tie**. Before a draw, it found faces
whose positions an earlier depth-writing face already claimed, and
**degenerated** them in the vertex buffer: their three corners collapsed to
one point, so they drew nothing. The picture was the original's. The cost was
a walk over the faces every frame, kept up to date for geometry that moves.

A great deal of work went into making that walk cheap, and a reader's
question pointed past it: *the original has no flicker, so its answer must be
cheap to reproduce.* The census that followed (Part IV's kind of check)
seemed to block the easy version. 1 540 of the sets' coincident groups span
two meshes, and the first examples are **the two leaves of a door**. Doors
move. Open, the faces separate and the losing one must draw again. A loser
removed once, at load, would stay missing while the door stood open, so a
hybrid was argued: groups within one mesh answered at load, the rest watched
every frame.

It was never built, because a closer look at what the original actually does
made it unnecessary. The original does not *remove* the losing face. Its
strict test on a quantised buffer only demands that a later face be nearer
by a whole step. So the port now decides the losers **once per set**, from
the set's whole draw order, and draws them **two 16-bit steps back**
instead of degenerating them. A closed door's losing leaf stays hidden. An
open door's leaf is far from its twin, the small push changes nothing, and it
draws. No per-frame walk, no hybrid. The frames are byte-identical to the
per-frame tie's over a walk past the signs, and the tie's share of a street
frame (up to 4.4 ms on an M3, about 50 ms on the console) is gone for the
set. Only posed bodies keep the per-frame tie.

<div class="pagebreak"></div>

# 15. Making it fast

## The yardstick

The original ran on a Pentium II. The port wants to run on a **PS Vita**:
four ARM Cortex-A9 cores at 444 MHz, and a homebrew heap measured in hundreds
of megabytes. The rule for the work is strict. **Nothing may change what is
drawn or decided**: every step replaces *how* an answer is computed, never the
answer, and its check is "same output, less time". A step that moves a pixel
or a position is a bug in the step.

Timings in this chapter come from two different Macs (an M1 and, later, an
M3). A millisecond from one is not comparable with a millisecond from the
other, and the Vita is slower than both by a factor that was, for a long
time, only estimated.

## The first profile: the port's own overheads

The first measurement, on the M1, found the largest cost in an unexpected
place: the **shadows' ground probe**. The original probes straight down from
each shadow bone, and the probe itself was faithful. What was not faithful
was *how* the port answered it: by scanning every walkable triangle of the
city, some 15 000, for each bone of each body, every frame. The original
culls whole meshes first. A **spatial grid** over the walkable geometry took
the street from 24 to 45 frames per second, with byte-identical frames.

That set the pattern for the whole effort. Almost every large cost was the
**port's**, not the game's:

* linear scans where the original culled (the probe grids, the walker's
  sweeps);
* whole-set copies every frame when one crane moved (a per-mesh patch, then a
  list of exactly which corners changed);
* the depth tie rebuilding its tables from scratch;
* a round trip through the CPU for every frame's picture (the frame now stays
  on the GPU when nothing is drawn over it);
* memory held needlessly: the music decoded whole, textures held several
  times, sprite tables mostly empty.

## The handheld

Then the port ran on a real console, and the console had its own lessons.
vitaGL copies a **whole buffer** when a small part of it is written while the
GPU may still read it. The depth tie's three-vertex patches into a 5 MB set
buffer turned into whole copies, and a city frame took 8.4 seconds. Writing
through a mapped pointer fixed it. The log, written synchronously to the memory
card one line at a time, fed the very lag it reported. The music's 16 MB was
refused by the heap as a city loaded.

And the three levers the earlier analysis had named were pulled one by one:

* **Posing on several cores.** The crowd's bodies are independent, so they are
  posed on a thread pool, with the frame bit-identical either way. On the
  console, 2.7× on three workers.
* **Posing on the GPU.** The walkers and the staged bodies are skinned and lit
  in the vertex shader (chapter 14), and since 2026-09-30 the player too. On
  the console a GPU-posing self-test had quietly been switching it off: the
  Vita's shader compiler *rounds* when it converts to an integer, where the
  Mac's truncates, so every odd mesh took its even neighbour's matrix.
  Fixed, the city frame went from 110-124 ms to 59-70.
* **NEON.** Two hot loops got ARM vector versions beside the portable ones,
  each proven bit-identical.

## Fewer calls, fewer allocations

The next round looked at what the GPU backend and the heap were paying for.

![Two measurements from 2026-09-29.](figures/fig11-optim.svg)
<p class="caption">Figure 11 — The GL state calls and the heap allocations of a street frame, before and after (M3; counts, not milliseconds).</p>

* The GLES backend set every piece of state on every draw. It now remembers
  what the last draw left: **1 809 state calls a frame became 92**, same
  frames. This is the modern counterpart of what the original's bucket order
  gave it for free (chapter 8).
* A street frame made about **720 heap allocations** outside the software
  rasterizer: scene programs building a list and a tree node on every tick,
  the light ramp rebuilt per light per body, per-frame maps of particles and
  moving meshes. It now makes about **160**, same frames.
* The music decoder became a table (chapter 9).

They were measured in counts on a Mac. The console log that followed is in
the next section.

## A wrong turn worth telling

In the same round, a comparison showed a street frame 537 pixels different
between two ways of uploading moving geometry. It looked like a bug in the
faster path. It was not. The difference came from an experimental merge that
assumed a list was sorted, and it was not. On the way there, a fix for a
suspected clock problem was committed on the strength of four captures that
agreed with each other. The check written for it could not be made to fail,
and that is what gave it away: the four runs had stalled at frame 0, because
the display was asleep, and each had dumped the same first frame. The fix was
reverted, and the lesson went into the project's list of traps: **a dump is
evidence only with the frame count it came from.** Part IV's rule, that a
check must be shown to fail, caught it.

## Doing it the original's way

The round after that took a different method. Instead of profiling the port
and asking what was slow, it read, section by section, **how the original did
the same job**, and asked why the port did more. The answers were nearly
always that the original did less:

* **The view cull had four side planes** the port had left out. With them,
  half of a street's set runs and most of its walkers are not drawn at all.
* **The crowd has levels of detail.** The original picks a simpler skeleton by
  view depth, at 10, 20, 30 and 40 m. The port now does too.
* **A moving mesh moves by one matrix.** The original writes three floats on
  the mesh's node and the vertices go through it on the way to the card. The
  port had rewritten every moving corner into the set's buffer each frame. Its
  collision layer, likewise, tests a moving mesh whole by its sphere first.
* **A set streams in slices.** The original queues one read of a new set and
  serves it a slice a frame while it draws on. The port read and built the
  whole set in one frame. It now prepares the set on a thread while the
  Session counts the original's frames.
* **A line's voice is decoded while it plays**, on the original's timer
  callback, never all at once when the line starts. The port now decodes the
  next lines ahead, on a thread.
* **The world's audio runs at 22 050 Hz.** The port's device was running at
  the films' 44 100, so every world sound was being resampled.
* **The tie is decided once** (chapter 14).

The console's log of most of that round: the city at **40 to 50 ms a frame**,
the player and the sky moved by the renderer, almost nothing re-uploaded. And
one new lesson from the arrival in Anekbah: the set took 1.3 s on its thread
against the half second of slices the original would allow, so the frame
waited. The original simply waits on its reader, however long the disc
takes, and so does the port now.

## Compared with the original

Put side by side with the 1999 engine, the pattern is clear.

| | the original | the port |
|---|---|---|
| draw state | the bucket order groups draws by blend and texture | a state cache at the API gives the same effect |
| per-frame memory | fixed pools: the display list's node cap, shadows in the frame's own pools, a 160-buffer sound bank | down from ~720 to ~160 heap allocations a frame, heading toward pools |
| scene programs | pointers followed in place | a small inline buffer |
| characters' geometry | posed and transformed on the CPU | posed on the GPU, the player included, beyond the original |
| moving meshes | one node matrix each | one matrix each, the same |
| loading a set | one read served a slice a frame | a thread, released on the original's frame count |
| music | decoded by branches | a table, cheaper than the original |
| coincident faces | free: quantised depth, strict test | losers decided once per set and drawn a step back; per frame only for bodies |

The original's speed came from **structure**: an integer draw order, fixed
pools, a quantised depth buffer, pointers walked in place. Most of the port's
optimisation removed costs the port had *added* by using general-purpose
containers and a GPU's float depth compare. Where the port does more than the
original, posing on the GPU, it is because a Vita's CPU is spent elsewhere.
It does not draw more characters. The crowd is the original's crowd, by the
original's rule.

<div class="pagebreak"></div>

# 16. Where to go from here

You now have the whole machine in outline: a small executable running a
large body of data, one loop, scripts that park instead of blocking, places
that stay resident and solid, bodies driven by state graphs, a draw order that
is a number, sound that is decided rather than mixed. And the replica: one
engine with its decisions behind a boundary, reference and live backends, and
a standard of evidence that asks every check to be able to fail.

For the next step, go by question:

* **"How exactly does X work?"** The manual, `manual/`, chapter by chapter,
  with the addresses and the checks.
* **"What is the evidence for that number?"** `docs/`: `FILE_FORMATS.md`,
  `ASSETS.md`, `SCRIPT_VM.md`, `UI.md`, `CUTSCENES.md`, `GAME_STATE.md`,
  `STREET_LIFE.md`. And `python3 tools/verify.py --list` names the check
  behind each figure.
* **"What is the standard for the port?"** `docs/PORTING.md`, the tiers and
  the boundary.
* **"What is being worked on?"** `todo/`: the optimisation record, the Vita
  handoff, the classic-Mac handoff, the task files.
* **"Why is it done this way, and what went wrong on the way?"** `CLAUDE.md`
  §1, the ground rules and the traps, each with the mistake that produced it.

## Glossary: Omikron ↔ Unity

| Omikron / OMK | the nearest Unity or OpenGL idea | where it differs |
|---|---|---|
| script context | a coroutine | parks by writing its own status word; resumed by events, never polled |
| trigger zone | a trigger collider with enter / exit / interact | its handlers are bytecode, its "fired" flag is in the save |
| area / scene | a scene / an additive scene | two places resident at once, the hidden one still solid |
| `.CTL` state graph | an Animator Controller | transitions are input bit masks; states carry gameplay data |
| `.3DO` character | a rigidly skinned hierarchy | one bone per vertex, no weights |
| bucket key | render queue + sort key | a fixed integer order, no distance sort |
| camera editing | a Timeline camera track | holds its last frame when it ends |
| texture slot cache | a texture atlas / cache | 58 slots, matched by file name alone |
| renderer boundary | a render pipeline | the engine submits decisions, not API calls |
| `dt = 30 / fps` | `Time.deltaTime` in 1/30 s | capped at 3.0: below 10 fps the game slows down |

