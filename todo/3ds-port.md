# The Nintendo 3DS port - the plan

Written 2026-10-05, while a sweep ran; nothing here is built or measured yet.
The reader asked *"How about a 3DS port?"* and then, after the week's RAM and
CPU work, for the plan. It follows the shape of the two ports that came before
it - [`vita-port.md`](vita-port.md) (an ARM handheld, a GPU backend under the
same boundary) and [`classic-mac-port-1999.md`](classic-mac-port-1999.md) (a
fixed-function GPU, a minimal toolchain, a 64 MB budget) - and takes from each
what already exists rather than redoing it.

A homebrew build that runs on the reader's own game data, like the Vita's.

## 1. The target

| | Old 3DS / 2DS | New 3DS / New 2DS |
|---|---|---|
| CPU | ARM11 MPCore, 268 MHz, VFPv2, **no NEON** | the same core at 804 MHz, 2 MB L2 |
| app memory (FCRAM) | ~64 MB by default (more in special exheader modes) | ~124 MB, ~178 MB in the extended mode |
| GPU | PICA200, 268 MHz: programmable VERTEX shaders, a FIXED fragment stage (6 texture-combiner stages, alpha test, blend, fog LUT, 24-bit depth + 8 stencil); 6 MB VRAM, textures and buffers may also live in the linear FCRAM heap | the same |
| screens | top 400x240 (800x240 wide mode, not with stereo), bottom 320x240 touch | the same |

**The New 3DS is THE target; the Old 3DS is a stretch** decided by step 8's
measurement, not assumed.

Little-endian, so `classic-mac-port-1999.md` step 3's byte-order work does not
apply; the `loadLE` discipline (PORTING A9) makes that free either way.

## 2. What already exists (measured 2026-10-04/05, the street)

* **Memory**: `ram-vs-original.md` took the street **72.9 -> 40.1 MB** live,
  frames identical - on a 64-bit host, so a 32-bit ARM build is smaller still
  (UNMEASURED: the last 32-bit figure, ~82 MB, is from before the cuts).
  Textures are kept as 8-bit indices + palette CPU-side (14.1 -> 4.8 MB).
  GPU memory was 23.8-30.9 MB on 2026-10-04, before the cuts, uploaded as
  keyed RGBA8.
* **CPU**: the per-set depth tie (optimization step 29), Kay'l and the sky
  posed / moved by the renderer (step 30), bodies posed once a vertex, the
  particles' depth gate (88% left out), the moving collision placed on demand
  (99% of placements gone) - `cpu-vs-original.md`. The Vita console runs
  Anekbah at ~22-25 ms sim+draw (`handoff-vita-port.md` 3b4c).
* **The boundaries**: game code reaches the host only through `omk::Frontend`
  (`src/platform/frontend.h`, 236 lines: open, pump, present, audio, clock,
  text input) and draws only through `omk::Renderer` (`src/o3de/renderer.h`:
  textures, begin/submit/end, the optional `posesBodies`, `bakeDepthTie`,
  `drawMirrorScene`). `carbonfront.cpp` (519 lines) is the precedent for a
  frontend written straight on a platform API with no SDL.
* **The GL1 backend** (`backends/gl1/gl1render.cpp`, 897 lines) is modelled on
  the original's D3D path - per-vertex colour, the two blend modes, the
  cutout, linear black fog, CPU transform - which is the PICA200's model too.
  It is the template for the citro3d backend.
* **The Vita's pieces**: the bench (`bench_main.cpp`, the device factor), the
  IME for the name field (`ime.cpp`), the hardware-film fallback pattern
  (`avmovie.*`, MPEG-1 when the film is absent), the CPU interface composite
  and its row hash (`playgpu_gles.cpp`), `readFileRange` for the ranged
  archive reads.

## 3. The decisions that are the reader's

Asked at the step that needs them, not before:

* **Hardware**: is there a 3DS with custom firmware (Luma3DS) to test on? The
  emulator (Azahar, Citra's successor) is enough to boot and draw; timing and
  memory are only real on hardware - the Vita's lesson (a uniform ~40x the M1,
  invisible in Vita3K).
* **The interface's place** (step 5): which of the 37 screens move to the
  bottom screen, and whether half-scale text is readable - a judgement by eye
  on a console.
* **The dither** (step 3): the original's 16-bit device dithered; the PICA
  renders RGBA8 and the display transfer writes the screen's format. Port the
  ordered dither as the other backends do, or render 16-bit.
* **SDL or not** (step 2): SDL has a 3DS port; a libctru frontend like
  `carbonfront.cpp` is smaller and has no dependency to chase. The plan
  assumes libctru.

## 4. The steps

Each ends in a commit and a report, and declares its evidence tier (PORTING B).

### Step 0 - the toolchain and a cross-build check

devkitPro's devkitARM + libctru + citro3d (`scripts/install-deps.sh` reports
them, never requires them - PORTING A1). `engine/backends/n3ds/` with a
Makefile or CMake in the Vita's style; `omk-core` (the engine library) for
`armv6k`, hard-float VFP. `verify.py: engine: 3ds build`, modelled on
`engine: vita build` - SKIPPED, never red, when devkitARM is absent. The
first cross build will find what the Vita and classic builds found (headers
the Mac supplied and the cross toolchain does not, `%zu`).

### Step 1 - boot headless: the intro trace and the device factor

`omk_3ds_boot` (the `OMKBoot` equivalent): the engine on `sdmc:/omk/`, DataFs
resolving case-insensitively there, `build/omk`'s boot to `traces/intro.log`
**42 of 42 in order** - the same proof every port gave. Beside it the Vita's
bench on the device: the frame's CPU against the M1's, the **32-bit memory**
of a standing street (the profiler's counting allocator, `--sites`), and the
SD read time of one area load. **This step decides Old 3DS vs New 3DS** for
everything after.

### Step 2 - the frontend, first light on the software renderer

`n3dsfront.cpp`: HID (buttons, circle pad, touch), `ndsp` audio (the mixer's
float PCM converted to 16-bit, 22050 stereo; the music ring as is),
`svcGetSystemTick` for the clock, `swkbd` for the name field (the Vita's IME
pattern). `present()` blits the frame to the top screen. **First light uses
the software reference** at 400x240 - slow, but it proves data, script, audio
and input end to end with no GPU code - and the interface composited at
640x480 and box-filtered 2:1 into a pillarboxed 320x240 on the top screen.
Played: the start menu answers, the flat draws, Kay'l walks.

### Step 3 - the citro3d backend

`backends/citro3d/` modelled on `gl1render.cpp`, `playgpu_citro3d.cpp` its
glue. Each item is a reading of the boundary, not a new design:

* **textures** from the kept indices + palette into **RGBA5551** - a native
  format whose 1-bit alpha IS the colour key, 2 bytes a texel against
  today's RGBA8 4 - Morton-tiled at upload; the PICA has no palette format.
  The .3DT sizes must be powers of two, 8..1024 a side: measured over all
  2534 before the code is written, and whatever does not fit says so;
* the **two blend modes** (additive `0x1000|0x2000`, multiply
  `0x1000|0x4000`), the **cutout** (alpha test), vertex colour x texture in
  combiner 0, the **linear black fog** through the fog LUT, the software
  back-face cull and its `0x20000000` exemption (CPU, as everywhere);
* the **depth tie** baked once a set (`bakeDepthTie`, GLES step 29), the
  **mirror** with the stencil (`drawMirrorScene`), the **shimmer**;
* the viewport and fov for the 5:3 top screen (each camera mode's fov is a
  decision written down, as the letterbox's was).

Checked as GL1 and GLES were: one set through it against the reference
(coverage agreement), and every frame decided by the same draw list.

### Step 4 - the memory fit

The app memory mode in the exheader (a `.cia`, or the Homebrew Launcher's
title takeover for a `.3dsx`); textures in the linear heap at 2 bytes a
texel; render targets in VRAM. Measured on the console standing, walking and
across an area change. If the Old 3DS is still in play, what is left over
64 MB is a list of cuts, `ram-vs-original.md`'s deferred indexed geometry
(-3 MB) the first of them - **the budget is a goal for the code, never raised
to the measurement** (the classic Mac's rule).

### Step 5 - the two screens

The world on top; the interface on the bottom where it is a SCREEN (the 37
of `UI.md` - menus, the sneak, the inventory, the shops, the save panel) at
exactly half its 640x480, 4:3 onto 4:3. What is drawn OVER the world stays on
top - the subtitles, the fades, the shoot HUD, the dialogue replies are to be
classified one by one from the I2D layers, not guessed. The reader judges the
half-scale fonts on hardware; if the small ones fail, a 2:1 filter tuned for
the coverage ramp is the first remedy, not a re-layout. Touch to select a row
is an ENHANCEMENT and waits for step 9.

### Step 6 - controls, saves and the films

* the four control schemes' joystick column on the buttons and circle pad,
  rebindable as on the PC (`input/bindings.*`);
* saves in `sdmc:/omk/` (the settings header read at every boot, as now);
* the **films**: MPEG-1 through `pl_mpeg` on the New 3DS's CPU first; the
  New 3DS's hardware decoder (H.264) as the optional path, exactly the
  Vita's `avmovie` pattern - a film absent in that form falls back.

### Step 7 - performance

Measured on the console with the 30 fps cap on. In the order the
measurement points to, but expected:

* **bodies posed by the vertex shader** (`posesBodies`, as GLES does); the
  PICA's 96 vec4 uniforms hold about 30 3x4 bone matrices, so the bone counts
  of the 181 character models are measured first and a model over the limit
  splits its draw;
* the **second core** (`OMK_THREADS`): on the New 3DS a whole core is the
  app's; on the Old one the system core's share is capped;
* the crowd's density default for the device (options row 6, the original's
  own knob).

### Step 8 - the Old 3DS, decided

Steps 1, 4 and 7 measured on a 268 MHz console. Either a supported target
with its settings written down, or a recorded "no" with the numbers.

### Step 9 - enhancements, OFF by default

* **stereoscopic 3D**: `gfxSet3D`, the slider (`osGet3DSliderState`), two
  passes with off-axis frusta, per-eye visible set and cull, billboards and
  2D at the screen plane, one pass at slider 0. The convergence distance per
  camera mode is a decision made by watching;
* touch selection on the bottom screen.

`[Enhancements]` in the config, as every other.

### Step 10 - packaging and the handoff

`.3dsx` and `.cia`, the layout on the card, a `handoff-3ds-port.md` in the
Vita handoff's form, CLAUDE.md's map row updated.

## 5. Traps already known

* **A run in the emulator is not a measurement** - Vita3K played what the
  console could not (the films, the frame time). Timing and memory come from
  hardware or are labelled as the emulator's.
* **A file copied in ASCII mode** is a black screen - the Vita's IAM over
  FileZilla. Copy the data in binary mode, and hash one archive on the card.
* **A positional initialiser shifts** when a boundary struct gains a field
  (CLAUDE.md 1) - a new backend is where that bites first.
* A cross toolchain's headers are not the Mac's: include what is used.
