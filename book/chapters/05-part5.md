<h1 class="part" id="part-v">Part V — Rebuilding it</h1>

<div class="pagebreak"></div>

# 12. The shape of the replica

## What it is

`engine/` is the replica: C++20, roughly 57 000 lines in the engine proper and
28 000 in the backends that draw and play it, and **no required dependency**.
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

## The one design decision

Every output subsystem has **a reference implementation and a live one**
behind **one boundary**:

| | reference | live |
|---|---|---|
| picture | a software rasterizer into an RGB565 framebuffer | Vulkan (MoltenVK on macOS), and GLES2 for the PS Vita |
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
boundary is what made three backends possible.

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
mipmaps, real shadow maps, per-pixel light, supersampling: all exist, all
behind a flag. (Bilinear texture filtering was on that list until it was
read that the original's 3D-card device set it; it is now the default on
every GPU backend.) A replica judged against the original must draw what
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
Its main loop is long, over 21 000 lines in one file, and a split into
smaller files has begun. Each frame it asks the Session to step, poses the
bodies, gathers the draws, and hands them to a backend.

It also carries its own instruments, and they are part of how the port is
developed: a frame's cost is marked section by section, a slow frame prints
what took the time, a `--dump` writes the framebuffer, and `--flicker` catches
a fault too short to screenshot by watching for frames much darker than their
neighbours.

## Determinism

A run limited by `--frames` uses a fixed step of exactly one engine frame, so
the same command gives the same frames. That is what lets a check render a
street, change one line of code, render it again and compare the two dumps
byte for byte. Most of the speed work in the next two chapters was proven
exactly that way: **same output, less time**.

<div class="pagebreak"></div>

# 14. Three renderers, and two faces in the same place

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

## Two faces in the same place

Chapter 8 described the shop signs: two faces, the same four vertices, wound in
opposite directions, each with its own advert. The original shows the first
one drawn, from every angle, and never flickers. A strict depth test on a
**quantised** depth buffer produces that: two faces at the same depth quantise
to the same value, the second is not strictly nearer, and it loses every pixel.

A GPU does not behave that way. The two windings split the quad along
different diagonals, their interpolated depths differ in the last bits (about
two parts in ten million), and the depth test picks per pixel wherever the
noise falls. The result is **dots of the other advert**, re-rolled by every
sub-pixel camera move. Asking for a 16-bit depth buffer with the same strict
test is not enough. The GLES backend has exactly that, and without help the
GPU gave the whole sign to the second face.

![The original's answer, a GPU's, and the port's.](figures/fig08-tie.svg)
<p class="caption">Figure 10 — Two coincident faces: the original, a GPU float compare, and the port's depth tie.</p>

The port's answer is the **depth tie**. Before a draw, it finds faces whose
positions an earlier depth-writing face already claimed, and **degenerates**
them in the vertex buffer: their three corners collapse to one point, so they
draw nothing. The result is the original's picture. The cost is a walk over
the faces, and for geometry that moves (a posed body, cargo swinging on a
crane) the walk has to be kept up to date.

A great deal of work went into making that walk cheap: remembering its answer,
replaying it for a rigid body, writing only what changed. It is still one of
the largest costs of a street frame. And there is a better idea, prompted by a
reader's question: *the original has no flicker, so its answer must be cheap
to reproduce.* For the shop signs it is: both faces of each pair sit in **one
mesh**, so they move together and stay coincident, and the texture slots that
order them are fixed when the set **loads**. Their answer never changes while
the set is resident, and could be computed **once, at load**.

Before building that, the project counted every coincident group in every
model, keyed exactly as the tie keys them. The census is the kind of check
Part IV argues for, and it changed the plan. In the sets, 3 821 of 5 361 groups
lie within one mesh, but **1 540 span two meshes**, and the first examples are
**the two leaves of a door**. Doors move. Open, the faces separate and the
losing one must draw again; closed, they coincide again. A load-time answer
would leave that face missing while the door stands open.

So the design that reproduces the original exactly is a hybrid: the groups
within one mesh resolved once at load, and the few across meshes (four door
pairs in Anekbah, out of thirty thousand faces) still watched at run time
(`todo/optimization.md` step 27). It is argued and not yet built: the proof
would be the per-frame tie's answer and the hybrid's compared frame by frame
while cargo moves and doors open.

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
  in the vertex shader (chapter 14). The player is next.
* **NEON.** Two hot loops got ARM vector versions beside the portable ones,
  each proven bit-identical.

## Fewer calls, fewer allocations

The most recent round looked at what the GPU backend and the heap were paying
for.

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

None of these has been measured on the console yet. They are measured in
counts on a Mac, and what they are worth on a Vita is for the next console log
to say.

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

## Compared with the original

Put side by side with the 1999 engine, the pattern is clear.

| | the original | the port |
|---|---|---|
| draw state | the bucket order groups draws by blend and texture | a state cache at the API gives the same effect |
| per-frame memory | fixed pools: the display list's node cap, shadows in the frame's own pools, a 160-buffer sound bank | down from ~720 to ~160 heap allocations a frame, heading toward pools |
| scene programs | pointers followed in place | a small inline buffer |
| characters' geometry | posed and transformed on the CPU | posed on the GPU, beyond the original |
| music | decoded by branches | a table, cheaper than the original |
| coincident faces | free: quantised depth, strict test | the depth tie; a hybrid is argued (pairs in one mesh answered once at load, door leaves watched) |

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
  handoff, the task files.
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

