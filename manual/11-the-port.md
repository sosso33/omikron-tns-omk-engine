# 11. The port

← [Contents](README.md) · prev: [The interface](10-the-interface.md) · next: [Evidence](12-evidence.md)

---

## In short

`engine/` is the re-implementation: C++20, about 37 400 lines across eight
directories, and **no required dependencies at all**. `make` on a machine with
nothing installed builds every probe and passes the test suite. SDL and the
Vulkan loader are optional and buy you a window.

The shape of it comes from one decision. Every output subsystem — picture,
sound, input — has **two implementations behind one boundary**:

| | the reference | the live one |
|---|---|---|
| render | a software rasterizer into an RGB565 framebuffer | Vulkan, through MoltenVK on macOS |
| audio | PCM buffers | an audio device |
| input | a replayable event stream | a real keyboard |

The reference is what the checks measure. The live one is what makes the
replica playable. Neither may be required to build the other, and a bare
checkout with no Vulkan SDK must still build and still pass — that property is
what keeps the evidence honest on somebody else's machine.

## In detail

### The layout

| directory | what is in it |
|---|---|
| `formats/` | one reader per file format — iam, scx, sfx, ctl, anim, morph, mesh3do, tex3dt, fnt, adpcm, addresses, opt |
| `script/` | the world-script runtime: the Session with its two resident slots and its frame, the VM handlers, the zone registry, conversations, the game state, the scene objects |
| `actor/` | the `.CTL` channel, the player controller and follow camera, skinning, the walker, the shoot AI, the street crowd, the spatial index |
| `o3de/` | the renderer boundary and the software rasterizer, the draw buckets, geometry, the texture cache, world cameras, camera editings, particles, collision |
| `ui/` | the I2D layer, the widget walk, screen drawing, text, surfaces, the options tree |
| `audio/` | the voice bank and pool, the voice-over path, music |
| `input/` | the four control schemes |
| `platform/` | all data access, the boot path, the frontend interface, the movie decoder, JSON |

Plus `backends/sdl/` (the viewer), `backends/vulkan/`, and 145 small
command-line probes in `engine/tools/` — one per check, mostly.

The build compiles to `build/obj/**.o` with dependency tracking and links. A
clean build is about 11 seconds and a no-op 0.03; it used to rebuild every
source for every tool, which meant roughly 1 500 translation units a build, and
the test suite paid that cost once per engine check.

### The boundary is at the decision level

This is the load-bearing choice, and getting it wrong is expensive in a way
that is hard to undo. What is ported is not triangles but **decisions** — the
drawable mask, the bucket key, the blend modes, the texture cache, the
visible-set walk (chapter 8). The interface takes those:

```
begin(view) · submit(draw) · submit2d(list) · end() -> Frame
```

Put the boundary at the API level instead and those decisions leak into
API-specific code; the software backend then cannot be added without extracting
them again, and the frame oracle becomes unusable.

The software backend was written first, for three reasons, and only the third
is caution: it *is* the port for the 2D rasterizers, which have to be
transcribed anyway; it is the only path a captured frame can check exactly; and
it separates a wrong ported decision from a wrong API call at the moment the
code is newest.

### What "dependency-free" actually means

It is a property of the **verification path**, not of the program. Everything
the suite touches builds with nothing installed. The playable frontend needs
dependencies and should have them — hand-rolling Cocoa, Win32 and X11 to avoid
SDL, or writing an MPEG decoder, would be more code, no verification benefit,
and a port of nothing in the original.

Three rules keep that honest:

1. **`make` with nothing installed builds every probe and passes the suite.** A
   dependency that breaks this is rejected, not worked around.
2. **No ported source includes a dependency header.** They appear only in
   backend files, behind the boundary.
3. **No dependency may do work a reference implementation is supposed to be a
   port of.** Using SDL's blitter instead of the ported one, or a library's BMP
   loader instead of the ported one, means the check tests the library — and it
   passes exactly as green while establishing nothing.

The third is the one that is not hygiene, and it is the easiest to breach by
accident, because the result looks identical.

Vendored and system dependencies fail differently, so they are kept apart: the
MPEG decoder is checked in and always present, so the boot path may use it
directly; SDL and Vulkan are found by the build and their absence disables the
frontend and nothing else.

### Two rules that shaped the code more than any design

**Everything counts in frames.** The simulation is fed thirtieths of a second,
never seconds, however fast the frontend presents — chapter 2 has why. Two bugs
came from breaking it: an ambient period read as seconds ran a city 30× too
slow, and a viewer that pre-sampled one entry per whole frame went black on a
fractional index.

**All data access goes through one class.** `DataFs` resolves case-insensitively
because Win95 did, and it is also where the write guard lives: `safeOutputPath`
refuses any output path inside the shipped tree or carrying a shipped-data
extension. That guard exists because a tool whose second positional argument was
its output once truncated a file from the 1999 disc, and the check that noticed
ran afterwards — the wrong side of the event.

### The instruments

Three things exist to be looked at rather than measured:

* **`omk-play`** — the game, and also a free-look viewer for one set. It draws
  through the same boundary the checks measure, so a fault you can see in it is
  a fault in the thing they check. It is where several corrections in `docs/`
  came from.
* **four web viewers** that read the data directly rather than through the port
  — a conversation player, a cutscene player, the menus, and the world scripts
  as annotated listings. They are an independent implementation, which is what
  makes the differential tier possible.
* **`tools/sim`** — a Python model of the runtime, about 3 800 lines, that the
  C++ is compared against.

### Where the port stands

The coverage audit in `engine/README.md` is the authority, and it is written out
row by row rather than summarised, because it has been wrong twice — once
because it quietly dropped the rows it judged unportable, once with a figure
left stale by a day's work.

As it stands: **31 rows fully ported**, **7 partly**, **0 lifted as a table but
never consumed**, **0 unported**, and **3 that are not portable subjects at
all** — the simulator, the UI model and the trace rig, which are this project's
instruments rather than parts of the game.

What is left is essentially **device**: DirectDraw's back end, DirectSound's
mix, and the screen callbacks that are not in the disassembly.

## Where it lives

| | |
|---|---|
| the standard | `docs/PORTING.md` — Part A is this chapter, Part B is chapter 12 |
| the audit | `engine/README.md` §Coverage |
| the build | `engine/Makefile` |
| the enforcement | `verify.py: porting standard`, which asserts the audit's counts sum and that every item the standard calls unfinished is still called unfinished |

## What is not settled

* **The port has only ever been built and run on macOS on Apple Silicon.** It
  contains no operating-system conditional in its core — not one — so it is
  portable by construction, but that is a property of the source rather than a
  tested fact. Case-sensitive filesystems will break some of the *Python*
  tools, which let the host do the resolving; the C++ will not care.
* **The Vulkan backend has no reachable tier**, by construction: its
  correctness is inherited from the software backend it mirrors.
* **The trace rig is macOS-only** — it drives the original under CrossOver and
  captures with a platform screenshot tool.
