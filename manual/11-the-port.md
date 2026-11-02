# 11. The port

← [Contents](README.md) · prev: [The interface](10-the-interface.md) · next: [Evidence](12-evidence.md)

---

## In short

`engine/` is the re-implementation. It is written in C++20, and it has **no
required dependencies**: `make` on a machine with nothing installed builds every
probe and passes the test suite. SDL, the Vulkan loader, an OpenGL context and
the console SDKs are all optional. Each one adds a window on some machine.

One decision shapes the whole thing. Every output subsystem (picture, sound,
input) has **a reference implementation and one or more live ones, behind one
boundary**:

| | the reference | the live ones |
|---|---|---|
| render | a software rasterizer into an RGB565 framebuffer | Vulkan (MoltenVK on macOS); OpenGL ES (the PS Vita and the Quest); citro3d (the Nintendo 3DS); fixed-function OpenGL 1.x (the PowerPC Macs) |
| audio | PCM buffers | an audio device |
| input | a replayable event stream | a keyboard, a pad, a touch screen, VR controllers |

The checks measure the reference. The live ones make the game playable.
Neither may be required to build the other.

Three more rules sit beside that one:

* **Anything the original did not do is an enhancement, and every enhancement
  is off by default.**
* **The remaster is a separate tier.** It overrides the authored art, so it
  lives in its own files and only some builds compile it at all.
* **A speed-up may change *how* an answer is computed, never the answer.** Every
  optimisation is proven by the same frame, byte for byte, or by an exhaustive
  comparison.

The port now runs on five kinds of machine besides the desktop. The Vita was
already there at the last edition, and the other four have arrived since. It
runs **on a real PS Vita** into the city, inside 30 frames a second in the one
console run measured that way. It runs **on a real New
3DS** into the city. It runs **in VR on a Quest 2**, as a prototype. And it
runs **on Macs from 1999–2005**: an iBook G3 and an iMac G3 under Mac OS 9
and early Mac OS X, at single-digit to high-teen frame rates. Each of these is
a separate backend with its own handoff file. None of them is part of the
coverage audit.

## In detail

### The layout

Counted on 2026-11-02 with `wc -l` over every `.cpp` and `.h` file:
`engine/src` holds **77 411** lines in ten directories, and `engine/backends`
holds **58 963** in eleven (`.inl`, `.mm` and `.c` files counted too). The
probes in `engine/tools/` add **32 411** lines over **229** `.cpp` files.
Vendored code lives in `engine/third_party/` and is not counted.

| `src/` directory | what is in it |
|---|---|
| `formats/` | one reader per file format; `le.h` and `bytes.h`, the byte-order readers every multi-byte read goes through (PORTING A9) |
| `script/` | the world-script runtime: the Session with its two resident slots, the VM handlers, zones, conversations, the game state and the save, the scene objects, the inventory |
| `actor/` | the `.CTL` channel, the player and his cameras, skinning, the walker, the slider, melee, shoot mode, the street crowd and its vehicles, the spatial index |
| `o3de/` | the renderer boundary and the software rasterizer, buckets, geometry, the texture cache, the depth tie, lights and shadows, the day/night cycle, the weather, world cameras, particles, collision; and the shared `drawcore` (one CPU pose and light law for every GPU backend) |
| `ui/` | the I2D layer, the widget walk, screen drawing, text, the options tree, the city map, the HUD, the overlay's change checks |
| `audio/` | the voice bank and pool, the voice-over path, music |
| `input/` | the four control schemes, `Input_Poll`'s rules, the pad |
| `platform/` | data access, the boot path, the `Frontend` interface, the movie decoder, settings, threads, the profiler, the environment switches |
| `app/` | the viewer's command line and helpers, lifted out of its old single file |
| `vr/` | the head transform composed on the authored camera (only under `OMK_VR`) |

The eleven backend directories are `sdl/` (the desktop game and viewer,
`omk-play`), `vulkan/`, `gles/`, `gl1/`, `n3ds/`, `vita/`, `classic/` (the
Carbon frontend for Mac OS 9 and Mac OS X), `android/`, `openxr/`, `vr/`, and
`remaster/`.

The viewer used to be one file. `todo/play-split.md` records how
`backends/sdl/play.cpp` went from **23 069** lines to **57** on 2026-10-02:
`main` became a `PlayState` with the phases as separate functions, checked at
each step against a golden record of 28 scenes (frame, stdout and saves). On
2026-11-02 `play.cpp` is 101 lines and `backends/sdl/` holds 49 files.

### The boundary is at the decision level

This is the load-bearing choice (`docs/PORTING.md` §A2). What is ported is not
triangles but **decisions**: the drawable mask `flags & 0x800043`, the 14-bit
bucket key, the texture slot as its low six bits, the blend modes, the 58-slot
texture cache, and the visible-set walk. The interface takes those:

```
begin(view) · submit(draw) · submit2d(list) · end() -> Frame
```

A backend receives decisions and turns them into API calls. It never makes one.
That rule is why the boundary now carries **five** renderers (software,
Vulkan, GLES, GL1, citro3d) without any of them owning a rule of the game.

The boundary grew in one direction, for speed. `Renderer::posesBodies()` asks
whether a backend can pose a body itself. Since October every GPU backend says
yes (Vulkan, GLES, GL1 and citro3d). The software reference still poses on the
CPU and draws exactly as before. The pose and light law they share is one
copy in `o3de/drawcore`, held to `applyPose`/`applyLights` by
`verify.py: engine: draw core`.

**The host has a boundary too: `omk::Frontend`** (`src/platform/frontend.h`,
PORTING A1, 2026-10-03). The window, keys, pad, audio device, clocks and
display modes all cross it. The viewer's game code compiles with no SDL header
(`verify.py: engine: frontend gateway`). That is what let a Carbon frontend
stand in for SDL on Mac OS 9 by implementing one class.

### What "dependency-free" means

It is a property of the **verification path**, not of the program
(`docs/PORTING.md` §A8). Everything the suite touches builds with nothing
installed. The playable frontends need dependencies and should have them.
Three rules keep that honest:

1. **`make` with nothing installed builds every probe and passes the suite.**
2. **No ported source includes a system dependency's header.** A *vendored*
   one, such as the MPEG-1 decoder, is always present and may.
3. **No dependency may do work a reference implementation is supposed to be a
   port of.** If it did, the check would test the library and still pass.
   PORTING calls this the one rule that is not hygiene.

SDL3 is preferred and SDL2 still works. SDL3 had silently stopped building
until 2026-10-26. `verify.py: engine: sdl3` now builds and runs both where both
exist (PORTING A8).

### Rules the other machines added

Three of PORTING's sections exist because of the old Macs:

* **A9, byte order.** The data is little-endian and the host need not be. On
  PowerPC, raw reads gave "0 actor records for 1032" with no error. Every
  multi-byte read now goes through `loadLE`, and every tool call the checks
  make was replayed on emulated PowerPC Tiger: **1927 of 1928 identical**, the
  last differing only in directory order. That replay is **not** in
  `verify.py`. It also missed three reads in the viewer, which a walker drawn
  as a shard found later.
* **A10, no C++ streams and no weak objects with constructors.** Retro68's
  linker breaks both on classic Mac OS, in large programs only.
  `verify.py: engine: classic build` holds it.
* **Fused multiply-add is off** (`-ffp-contract=off`) wherever two builds must
  agree to the byte. GCC fuses `a*b+c` on PowerPC, and a 300-frame traffic run
  drifted by 0.1 unit (A9).

### Settings, the enhancements, and the remaster

The game's settings resolve from three places in order: the engine's own
defaults, the game ini's `[Preferences]`, and the save file's header. Anything
else lives under sections the original never had.

**`[Enhancements]`** (`todo/enhancements.md`, rows 0–13): anti-aliasing,
trilinear and anisotropic filtering, filtered interface scaling, an unlimited
draw distance, fitted shadows, mapped shadows, per-pixel lighting, the sets
receiving the lights (held back and not recommended), supersampling, shoot
mode's radar in every arena, 60 fps, bodies smoothed between keys, and text
scaling. Each is off by default, and `--enhance-all` turns them all on.

Three things look like enhancements and are **fidelity**, so they are **on**:
the dither, the shimmer, and **bilinear filtering on every GPU backend**. That
last one was row 1 until 2026-10-03, when the original's own render states were
read: on a 3D card it filters bilinear, and only its software devices sample
nearest (`docs/ASSETS.md` §4).

**`[Remaster]`** (`todo/remaster.md`, planned 2026-10-20 and largely built
since) is a tier apart, because it **overrides authored art**. It is held to the
same contract (off by default, byte-identical with it off, a check per feature
shown to fail) but not to "the data backs it". Its pieces, from
`engine/README.md`'s remaster paragraph:

* texture, interface, font and film packs, which **each player generates from
  their own copy** and which never enter the public repository;
* bloom from the packs' light maps, and a colour grade per city;
* reflecting water (with a Fresnel term) and reflecting floors;
* bodies lit by their surroundings, emitters turned into point lights, tiled
  lighting on Vulkan;
* generated normal maps, ambient occlusion, mist, rounded bodies, FSR 1
  upscaling;
* the game's own rain and snow drawn as 3D drops and flakes, wet floors, walls
  and bodies, and snow lying.

`--remaster DIR` is a preset over the separate options. **F10** switches
between original and remaster in play, and OMK's own options pages write all
of it back to the ini (`todo/graphics-menu.md`). It compiles only under
`OMK_REMASTER`: the desktop and Android (Quest) builds define it, and the Vita,
3DS, classic Mac and PowerPC builds never do (PORTING A1,
`verify.py: engine: remaster switch`). Measured before the switch existed, the
remaster code cost the original mode no frame time and about +17% binary size
on the Vulkan build (`todo/remaster.md` "The remaster switch").

Beside it sits **`[Restored] weather`** (`todo/restore-weather.md`,
`docs/ASSETS.md` 4e): the showers that Qalisar and Lahoreh ship tracks for but
never trigger, and the sand that type-3 areas have a complete code path for.
The override is OMK's, so it is off by default.

### Speed: the rule, and what it bought

`todo/optimization.md` is the record. Its principle is the one above: a step
that moves a pixel or a position is a bug in the step. Every row ends in an
equivalence (frames byte-identical, a hash unchanged, or an exhaustive
comparison), and most rows name a check.

**Every millisecond carries its machine, and the machines do not compare.**
The desktop work alternates between an **M1** and an **M3**. Counts
(allocations, state calls, uploads) compare across machines. Milliseconds do
not.

The phases, in order:

* **2026-09-12 to 09-14 (M1)**: grids for the ground probe, the walker and the
  sweeps; the depth tie stored flat; the interface's conversions by table; the
  moving set meshes patched through an index.
* **2026-09-29 to 10-01 (M3)**: an audit for the console (§15); the music
  through a table (exact over all 93 323 264 states, §16); the GLES state
  cache (1809 → 92 state calls a street frame, §17); the frame's heap churn
  cut (§18); and then **step 28, the port measured against the original on the
  console**. That step ranked twelve places where the port did more than the
  1999 engine, and steps 29–38 took them in order. The depth tie is now
  decided once per set and drawn a step back (§29). Kay'l and the sky are moved
  by the renderer as the original moves them (§30). Sets are prepared while
  the game streams them (§31), and moving meshes are drawn and collided as
  whole meshes (§37–38).
* **2026-10-04/05 (M1): RAM and CPU against the original.**
  `todo/ram-vs-original.md` went through the street's memory owner by owner
  and took it from **72.7 to 40.1 MB**, with frames byte-identical at each cut
  (`engine: street memory`). `todo/cpu-vs-original.md` did the same for time.
  Its tier A made the software reference **7×** faster: the frame hash had been
  computed every frame, and taking it out let the reference hold 30 fps capped
  (`engine: raster cost`).
* **The 4K yardstick** (the reader's, 2026-10-01, M3): 30 fps or more at
  3840×2160. The lag seen there was **closed as not the engine**. macOS held
  a bare binary's GPU at 338 MHz, and `make app` builds `OMK.app` so the game
  runs in Game Mode (`f09b0e4`).

Step 28 also **withdrew a claim**. Step 7 had said the original did the work
"at a fraction of this detail". The crowd's density is the engine's own rule on
the same models, and the 1999 engine posed every body on the CPU too, so the
port draws what the original drew. What it paid extra was its own: driver
calls, uploads, the per-frame depth tie.

### The machines

The coverage audit covers none of these; `engine/README.md` §Coverage lists
them as separate backends, each with a handoff file. Each row of the audit's
"What is left" table gives them **no reachable tier**: what is asserted is
agreement with the software reference, never with the original.

**The PS Vita** (`todo/handoff-vita-port.md`; GLES2 through vitaGL; begun
2026-09-17).

* The 2026-09-21/22 console logs reached the city at ~200 ms a frame, first
  through three crashes and stalls (the music's 16 MB, a whole-buffer copy on
  every tie patch, the port's own delta clamp).
* **The 2026-10-05 console run** (a development build, `--enhance-all
  --no-tie`, 960×544): Anekbah's simulation and drawing took **22–25 ms** and
  the present 2–7, so **~27–31 ms a frame, inside 30 fps** (handoff §3b4c).
  That run had the per-frame depth tie turned off. The baked tie (step 29) was
  first seen on the console at 40–50 ms a frame on 2026-09-30 (§3b5), and
  whether the signs hold on the Vita's own depth buffer with the bake is a
  question for the eye.
* Bodies are posed by the GPU, behind a start-up self-test that falls back to
  the CPU. Films go through the console's hardware decoder. Shaders are
  precompiled. The crowd is posed over several cores (2.71× on three runners,
  exact). NEON kernels sit beside generic ones and are measured against them.
* **`--profile` does not work on the console**, which is unexplained: no
  `profile:` line is printed at all (§3b4c). Until it works, console figures
  are the log's own section lines.

**The Nintendo 3DS** (`todo/handoff-3ds-port.md`; citro3d; begun 2026-10-05).

* It runs on the reader's New 3DS: the films, the menu, a loaded save, the
  restaurant, and Anekbah with its crowd. Sound and the touch screen work.
* **In the Azahar emulator** the headless boot reproduces `traces/intro.log`
  **42 of 42**. The menu frame is byte-identical to the desktop's. The street
  agrees with the software reference at **99.8–99.9%** of pixels within 24
  levels.
* On the console the game side is **~30–48 ms** a frame. The dense street is
  over budget on **both** CPU and GPU (the CPU waits 13–16 ms for the GPU;
  citro3d's drawing reads 52 ms). About half of the GPU side is fill.
* The present runs one frame behind, by the reader's decision. Textures follow
  the original's own size rule for video memory (a New 3DS gets the 4 MB card's
  answer, 128×128). Stereoscopic 3D is built, off unless asked, and
  Azahar-proved only.

**The Meta Quest 2** (`todo/handoff-quest-port.md`; OpenXR on Android, all
under `OMK_VR`; a prototype).

* Immersive at **72/72 fps**: each eye at the runtime's resolution, first
  person on the Touch controllers, the interface as a transparent layer, and
  the screens as windows fixed in the world over a shaded world.
* The head is a **render** transform composed on the authored camera, so game
  logic is unchanged. With no head pose every frame is byte-identical.
  `verify.py`'s `engine: vr camera rule`, `vr frame`, `vr modes` and `vr aim`
  check it through a simulated headset on the desktop.
* The videophone's monitor and the system keyboard were built but not yet
  played when the handoff was last updated (2026-10-07). The first play pass
  (step 7) had not started.
* It is the one console-class build that carries the remaster.

**The classic Mac** (`todo/handoff-classic-mac.md`; Retro68 and Carbon, the
GL1 backend; begun 2026-10-02).

* **Emulated.** The headless boot and 30 frames of the street start are
  **byte-identical to the host** on Mac OS 9.2 and on Tiger, in QEMU (§1).
* **A real iBook G3** (500 MHz, an 8 MB Rage 128) under Mac OS X 10.2/10.3,
  through the Carbon build. The street ran at 85–180 ms a frame in one run
  and 146 ms in the next (§3f), and the iBook's "white screens" were traced to
  OpenGL's colour sum being left on after the world.
* **A real iMac G3** (400 MHz, Rage 128 VR, OpenGL 1.1) under **Mac OS 9**
  (§3i). Every allocation had been a Memory Manager `NewPtr`. A slab allocator
  under `malloc` took Anekbah **from ~3 to ~20 frames a second**. In a later
  run where the reader changed the view distance, the street ran at
  **16.5–18.1 frames/s at a 50 m clip** and **8.7–12.3 at 100 m**. The GL draw
  there follows the triangle count, so the time is in the geometry path, not
  in fill.
* **The latest GL1 work** follows the original's Direct3D path on these cards:
  one draw per bucket, indexed draws, the vertex arrays locked, and since
  `7853dda` (2026-11-02) **the fog drawn by the card** as OpenGL's linear fog,
  the equivalent of the original's table fog. On the emulated OS 9 that fog
  frame is within 310 pixels of the host's. It had not yet been run on the
  iMac when the handoff was written.
* A **launch guard** records the step a frozen OS 9 launch stopped in and
  skips it next time. It exists because an earlier iBook freeze left no log
  to say where.

**PowerPC Mac OS X 10.4** builds through `ppc-darwin.mk`, as the SDL viewer
with the GL1 backend. On a real iBook G4 it ran at 6–8 fps, because it read
every frame back from the card. The fix draws the world straight to the
window, and it was measured only on the emulated Tiger, where the street's
frame median fell to 32.2 ms, which is the 30 fps cap. The G4's own run with
it is still the next step (§3g).

### Two rules that shaped the code more than any design

**Everything counts in frames.** The simulation is fed thirtieths of a second,
never seconds, however fast the frontend presents (`docs/PORTING.md` §A7). The
Quest steps the simulation on the measured delta at 72 Hz, and that delta is
still expressed in frames.

**All data access goes through one class.** `DataFs` resolves names
case-insensitively, because Win95 did. On each port it is the one place a file
API is chosen: the Vita's, the 3DS's, and the classic Mac's File Manager with
HFS paths through `omk::hostPath()` (PORTING A10). It is also where the write
guard lives. `safeOutputPath` refuses any output path inside the shipped tree
or with a shipped-data extension, because a tool once truncated a file from the
1999 disc.

### The instruments

* **`omk-play`**, which is the game and also a free-look viewer for one set,
  on any desktop renderer. It draws through the boundary the checks measure.
* **The profiler** (`todo/debug-tools.md`): `--profile run.prof`, read by
  `tools/omkprof.py` (a summary, per frame, or a served page with a call tree,
  memory by owner and GPU memory). It compiles out of release builds
  (`make release`, `classic-release`, `vita-release`). Its one obligation is
  to change nothing the game does, and `engine: profiler` holds it to that.
* **Events**: `omk::event(...)` writes `@evt` lines that checks parse, so
  rewording a log line cannot turn a check red (`engine: events`).
* **Five web viewers** that read the data directly rather than through the
  port: `/dialog`, `/cutscene`, `/ui`, `/model` (every `.3DO`) and `/world`.
* **`tools/sim`**, a Python model of the runtime that the C++ is compared
  against.

### Where the port stands

The coverage audit in `engine/README.md` §Coverage is the authority, and it is
written out row by row because it has been wrong twice. On 2026-11-02 it still
reads: **31 rows fully ported**, **7 partly**, **0 lifted as a table but never
consumed**, **0 not ported**, and **3 that are not portable subjects** (the
simulator, the UI model and the golden traces). `verify.py: porting standard`
checks that those buckets sum, and it was green on 2026-11-02.

The 41 rows date from 2026-08-31. Most of what has been ported since (street
life, the weather, the day/night cycle, the `.3DO` light table, the slider
audit, melee, shoot mode and both bosses) is recorded in dated paragraphs
around the table, not as new rows. The partly-ported rows are still missing
the same kind of thing: per-screen native callbacks never recognised as
functions, the joystick axes, the player's own firing state, DirectSound's mix,
and the Vulkan back end of I2D.

What a person has played and what is only committed are kept apart.
`todo/play-test.md` lists **41** items that have checks behind them and are
still waiting for someone to play them.

## Where it lives

| | |
|---|---|
| the standard | `docs/PORTING.md`. Part A is this chapter and Part B is chapter 12 |
| the audit | `engine/README.md` §Coverage and its "What is left" table |
| the enhancements | `todo/enhancements.md` |
| the remaster | `todo/remaster.md`, `todo/graphics-menu.md`, `todo/restore-weather.md` |
| the speed and memory record | `todo/optimization.md` (steps 1–38, the 4K benchmark), `todo/cpu-vs-original.md`, `todo/ram-vs-original.md`, `todo/debug-tools.md` |
| the PS Vita | `todo/handoff-vita-port.md`, `todo/vita-port.md`, `todo/gpu-skinning.md` |
| the Nintendo 3DS | `todo/handoff-3ds-port.md`, `todo/3ds-port.md` |
| the Quest | `todo/handoff-quest-port.md`, `todo/quest-port.md` |
| the classic Mac and PowerPC | `todo/handoff-classic-mac.md`, `todo/classic-mac-port-1999.md`; `engine/ppc-darwin.mk` |
| the viewer's split | `todo/play-split.md` |
| the enforcement | `verify.py: porting standard`, `engine: frontend gateway`, `engine: remaster switch`, `engine: classic build`, `engine: draw core` |

## What is not settled

* **No backend but the software reference has a reachable tier.** Vulkan,
  GLES, GL1 and citro3d are measured only against that reference, never
  against the original (`docs/PORTING.md` §B6; `engine/README.md` "What is
  left"). The GL1 backend agrees with the reference at coverage 0.9932 on the
  street (`engine: gl1 backend`).
* **The console figures are single runs and are not repeatable in
  `verify.py`.** The Vita's ~27–31 ms is one run without the depth tie. The
  3DS's numbers come from card sessions, and several of its fixes (the
  one-frame-behind present's tiling, the 400×224 present, stereoscopic 3D) are
  Azahar-proved only. The iMac's rates depend on the view distance, and the
  card fog of 2026-11-02 had not yet run on it.
* **The 3DS has an intermittent `bad_alloc` at the first world frame.** It was
  seen twice in Azahar and never on the console. It depends on memory layout,
  and the refusal now prints its own call site for the next time
  (handoff §5).
* **`--profile` does nothing on the Vita**, and the cause is unknown
  (handoff §3b4c).
* **The iBook's earlier OS 9 freeze has no cause.** The emulator does not
  reproduce it. The launch guard works around it without explaining it
  (handoff §3i).
* **The Quest's last two built features had not been played** as of its
  handoff, and its first play pass had not started.
* **Windows and Linux desktops have never been tried** (`README.md`
  "Platforms"). The core has no operating-system conditional outside
  `src/platform/`, but that is a property of the source, not a tested fact.
  The Python readers let the host resolve case and will break on a
  case-sensitive volume. The golden-trace rig is macOS-only.
* **The big-endian replay is not a check.** It needs the emulator, so a new
  raw read is caught only when someone runs it again (PORTING A9).
* **A question noticed and not changed**: `shadowBonesFor(-1)` returns no
  bones, while its own comment reads the engine's switch as giving an NPC at
  detail 0 the chest's shadow. The engine's switch decides it, and it has not
  been re-read (`todo/optimization.md` §18).
