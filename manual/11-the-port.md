# 11. The port

← [Contents](README.md) · prev: [The interface](10-the-interface.md) · next: [Evidence](12-evidence.md)

---

## In short

`engine/` is the re-implementation: C++20, about 55 700 lines across eight
directories, and **no required dependencies at all**. `make` on a machine with
nothing installed builds every probe and passes the test suite. SDL and the
Vulkan loader are optional and buy you a window.

The shape of it comes from one decision. Every output subsystem — picture,
sound, input — has **a reference implementation and a live one behind one
boundary**:

| | the reference | the live one |
|---|---|---|
| render | a software rasterizer into an RGB565 framebuffer | Vulkan, through MoltenVK on macOS — and GLES2, begun for the PS Vita |
| audio | PCM buffers | an audio device |
| input | a replayable event stream | a real keyboard, or a gamepad as the engine's own joystick |

The reference is what the checks measure. The live one is what makes the
replica playable. Neither may be required to build the other, and a bare
checkout with no Vulkan SDK must still build and still pass — that property is
what keeps the evidence honest on somebody else's machine.

And a second rule sits beside it, about what the port may add: **anything the
original did not do is an enhancement, and every enhancement is off by
default.** A replica judged against the original must draw what it drew
unless told otherwise.

## In detail

### The layout

| directory | what is in it |
|---|---|
| `formats/` | one reader per file format — iam, scx, sfx, ctl, anim, morph, mesh3do, tex3dt, fnt, adpcm, addresses, opt, map2d, the `.3DO` light table |
| `script/` | the world-script runtime: the Session with its two resident slots and its frame, the VM handlers, the zone registry, conversations, the game state and the save file, the scene objects, the inventory |
| `actor/` | the `.CTL` channel, the player controller and its cameras, skinning, the walker, the slider, melee, shoot mode's brains, weapons and projectiles, the street crowd and its vehicles, the spatial index |
| `o3de/` | the renderer boundary and the software rasterizer, the draw buckets, geometry, the texture cache, lights, shadows, the shimmer, world cameras, camera editings, particles, collision |
| `ui/` | the I2D layer, the widget walk, screen drawing, text and its layout, surfaces, the options tree, the city map, the HUD bars |
| `audio/` | the voice bank and pool, the voice-over path, music |
| `input/` | the four control schemes, `Input_Poll`'s rules, the pad |
| `platform/` | all data access, the boot path, the frontend interface, the movie decoder, the settings and the ini, JSON |

Plus four backends — `sdl/` (the game and viewer), `vulkan/`, and `gles/` and
`vita/` for the handheld port in progress — and 196 small command-line probes in
`engine/tools/`, one per check, mostly. About 107 900 lines all told.

The build compiles to `build/obj/**.o` with dependency tracking and links. A
clean build is about 11 seconds and a no-op 0.03.

### The boundary is at the decision level

This is the load-bearing choice, and getting it wrong is expensive in a way
that is hard to undo. What is ported is not triangles but **decisions** — the
drawable mask, the bucket key, the blend modes, the texture cache, the
visible-set walk (chapter 8). The interface takes those:

```
begin(view) · submit(draw) · submit2d(list) · end() -> Frame
```

Put the boundary at the API level instead and those decisions leak into
API-specific code; a second backend then cannot be added without extracting
them again, and the frame oracle becomes unusable. The boundary has since
carried a Vulkan backend and a GLES2 one, and grown for the enhancements by two
fields and one virtual whose default is a no-op, so the software reference
still draws exactly what the original drew.

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

### Settings, and the enhancements

The game's settings resolve from three sources, in order: the engine's own
defaults, the `[Preferences]` section of the game's ini (the 65 keys the binary
spells), and the **save file's header**, which carries all 74 option rows. The
port records which source supplied each field, and prints it at start-up.

Everything else lives under `[Enhancements]`, a section the original never had:
anti-aliasing, texture filtering and anisotropy, fitted and mapped shadows,
per-pixel lighting, supersampling, an unlimited clip distance, interface
scaling — each off by default, each with a command-line flag, and one switch
(`all = max`) that turns every one to its top. The two fidelity fixes that came
out of the same reading — the dither and the shimmer — are **not** in that
list: the original does them, so they are on.

### Speed, and the handheld

A frame's cost was measured with a **PS Vita** as the yardstick, and cut in
measured steps with frames kept byte-identical: grids for the ground probe and
the body and camera sweeps, the depth-tie table hashed and stored flat, texture
pixels shared between their copies, music held at its own rate, the moving set
meshes patched through an index. On an M1, capped at 30 fps on the Anekbah
street, the main thread now does about **6.0 ms** of work a frame, and the
frame is paced to a deadline so the cap really holds 30.

The decision recorded from those numbers is that a 30 fps Vita build doing this
CPU work on one core is **not in reach**, by a factor of several. The port to
the Vita began anyway, as its own line of work: a GLES2 backend, the pad as the
engine's joystick device, a VitaSDK build, and a first boot to adventure mode
in the Vita3K emulator. It has not run on a console.

### Two rules that shaped the code more than any design

**Everything counts in frames.** The simulation is fed thirtieths of a second,
never seconds, however fast the frontend presents — chapter 2 has why.

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
  a fault in the thing they check. Its harness flags — a street start at an
  address, a scene loaded over an area, a zone enabled or disabled, keys held
  or pressed on a schedule — reach a flow without the playthrough that would.
  Most corrections in `docs/` of the last weeks came from a person playing it.
* **four web viewers** that read the data directly rather than through the port
  — a conversation player, a cutscene player, the menus, and the world scripts
  as annotated listings.
* **`tools/sim`** — a Python model of the runtime, about 3 800 lines, that the
  C++ is compared against.

### Where the port stands

The coverage audit in `engine/README.md` is the authority, and it is written out
row by row rather than summarised, because it has been wrong twice.

As it stands: **31 rows fully ported**, **7 partly**, **0 lifted as a table but
never consumed**, **0 unported**, and **3 that are not portable subjects at
all** — the simulator, the UI model and the trace rig, which are this project's
instruments rather than parts of the game. The 41 rows predate most of what
chapter 6 describes; melee, shoot mode's runtime, the slider ride, the water,
the shadows and the lights are recorded in dated paragraphs above the table
rather than as new rows.

What is left on the table's own terms is essentially **device** and **native
code that is not in the decompilation**: DirectSound's mix, and the screen
callbacks that were never recognised as functions.

## Where it lives

| | |
|---|---|
| the standard | `docs/PORTING.md` — Part A is this chapter, Part B is chapter 12 |
| the audit | `engine/README.md` §Coverage |
| the enhancements and the speed record | `todo/enhancements.md`, `todo/optimization.md`, `todo/handoff-vita.md` |
| the build | `engine/Makefile` |
| the enforcement | `verify.py: porting standard`, which asserts the audit's counts sum and that every item the standard calls unfinished is still called unfinished |

## What is not settled

* **The port has been built and run on macOS on Apple Silicon**, and the Vita
  build in an emulator. Its core has no operating-system conditional, so it is
  portable by construction, but that is a property of the source rather than a
  tested fact. Case-sensitive filesystems will break some of the *Python*
  tools, which let the host do the resolving; the C++ will not care.
* **The Vulkan backend has no reachable tier**, by construction: its
  correctness is inherited from the software backend it mirrors. The GLES2 one
  is measured only by coverage against the software reference.
* **The trace rig is macOS-only** — it drives the original under CrossOver and
  captures with a platform screenshot tool.
