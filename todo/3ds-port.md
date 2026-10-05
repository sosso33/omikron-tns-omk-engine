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
| CPU | ARM11 MPCore, 268 MHz, VFPv2, **no NEON** | the same core at 804 MHz, 2 MB L2 (`osSetSpeedupEnable`) |
| app memory (FCRAM) | ~64 MB by default (more in special exheader modes) | ~124 MB, ~178 MB in the extended mode |
| GPU | PICA200, 268 MHz: programmable VERTEX shaders, a FIXED fragment stage (6 texture-combiner stages, alpha test, blend, fog LUT, 24-bit depth + 8 stencil); 6 MB VRAM, textures and buffers may also live in the linear FCRAM heap | the same |
| screens | top 400x240 (800x240 wide mode, not with stereo), bottom 320x240 touch | the same |

**The New 3DS is THE target** - the reader has one with custom firmware
(2026-10-05). The Old 3DS is a stretch decided by step 7's measurement, not
assumed.

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
* **The profiler** (`todo/debug-tools.md`): the game WRITES a capture file and
  takes `pause` / `step N` / `resume` / `snapshot` from `omk.ctl`, read once a
  frame - a file being the one transport every target has. Memory by owner
  (the counting allocator, `--sites`) and GPU memory by category
  (`OMK_GPU_ALLOC`). All of it compiled out by `OMK_PROFILE=0`.
* **The Vita's pieces**: the bench (`bench_main.cpp`, the device factor), the
  IME for the name field (`ime.cpp`), the hardware-film fallback pattern
  (`avmovie.*`, MPEG-1 when the film is absent), the CPU interface composite
  and its row hash (`playgpu_gles.cpp`), `readFileRange` for the ranged
  archive reads.

## 3. Decisions

**Taken by the reader, 2026-10-05:**

* **Hardware**: a New 3DS with custom firmware. Build a **`.cia`** (installed
  through the CFW, it sets its own memory mode in the exheader and starts as
  an ordinary title) and keep a **`.3dsx`** for quick iteration. Neither
  runs FASTER: the CPU, the 804 MHz mode and the GPU are the same however the
  code is started. What differs is MEMORY - a `.3dsx` started from the
  Homebrew Launcher as an applet gets far less than an application, and only
  title takeover gives it an application's share - and the CFW's extras:
  Luma3DS's Rosalina carries a **GDB stub** (step 1's debugging) and its own
  screenshots. So: the `.cia` for every measurement.
* **The interface stays on the TOP screen** for now. What the game does when
  a screen is up needs PER-SCREEN work (step 10), and the reader called that
  the most important thing to check. Until then every screen is composited
  at 640x480 as on every target, box-filtered 2:1 and pillarboxed into
  320x240 in the middle of the top screen (4:3 onto the 4:3 part of a 5:3
  screen); text drawn over the world (subtitles, replies, fades, the shoot
  HUD) goes the same way.
* **The bottom screen is the INSTRUMENT PANEL** (step 2b): stats, debugging
  buttons and the enhancement toggles the 3DS can draw.
* **libctru, not SDL** - the reader's rule was "libctru if it is faster or
  needed for stereoscopic 3D, SDL otherwise", and stereo NEEDS it: the
  slider, the second eye's framebuffer and `gfxSet3D` are libctru calls with
  no SDL equivalent. SDL would also bring nothing for the GPU: its 3DS
  renderer is 2D, so the 3D goes through citro3d either way. For input and
  audio SDL is a thin layer over the same services (HID, `ndsp`), so it saves
  little code and adds a dependency to chase. The frontend is libctru,
  `carbonfront.cpp`'s way.

**Open, with what each would buy:**

* **16-bit rendering or the dither** (step 3). The original rendered into a
  16-bit RGB565 surface with `DITHERENABLE` on, and the port's GPU backends
  render deeper and reconstruct the ordered dither. On the PICA a **RGB565
  colour buffer** would buy:
  * half the colour buffer's VRAM (400x240: 188 KB against 375 KB; at 2x2
    supersampling 750 KB against 1.5 MB) and half its write bandwidth - the
    PICA's fill rate is the likely limit, so this is real frame time;
  * a display transfer with no format conversion;
  * the original's own depth - every colour the original could show, and no
    others.
  
  What it costs depends on a fact not yet known here: **whether the PICA
  dithers when it writes a 16-bit buffer.** If it does, 16-bit is the
  original's arrangement exactly (a 16-bit device, dithered by the hardware)
  at the lower cost - the best of both. If it does not, 16-bit BANDS (the
  fog's ramp, the shading), and the choice is between RGBA8 output (smooth,
  smoother than the original) and the port's ordered dither, which on a
  fixed fragment stage is awkward (a screen-space dither texture through
  projective coordinates, or a CPU pass after a readback). Measured first
  thing in step 3: a gradient rendered into an RGB565 target, looked at.

## 4. The steps

Each ends in a commit and a report, and declares its evidence tier (PORTING B).

### Step 0 - the toolchain and a cross-build check

devkitARM + libctru + citro3d, with the host tools (`3dsxtool`, `smdhtool`,
`bin2s`, `picasso`). Two ways in, and `engine/backends/n3ds/Makefile` takes
either through `$DEVKITPRO` / `$DEVKITARM`:

* devkitPro's pacman (`sudo dkp-pacman -S 3ds-dev`, into /opt/devkitpro) -
  what devkitPro asks for where it can be used;
* **`scripts/3ds-toolchain.sh`** (2026-10-06, the reader's ask: "not
  possible without pacman?") - no pacman, no sudo, into `~/devkitpro`:
  devkitARM by devkitPro's OWN buildscripts at a pinned tag, the tools and
  libraries from their GitHub tags, every version pinned in the script,
  resumable by stamps, `--check` to report. A toolchain built so is "for
  personal use only" by devkitPro's terms, so nothing it builds is ever
  committed or shipped.

**devkitPro's download hosts refuse this machine** (2026-10-06: Cloudflare
answers 403 from `pkg.devkitpro.org` and from `downloads.devkitpro.org`,
which redirects there - curl, wget, the buildscripts' own user agent, IPv4
and IPv6). The script does not try to get past that: the buildscripts skip
any archive already in their source directory, so it puts the same archives
there first from where they are published - binutils and GCC from GNU,
newlib from sourceware, devkitPro's rules and crt0s from its GitHub -
reading the versions out of the pinned tag's own scripts. It also means a
pacman install may meet the same wall here.

A plain Makefile, not CMake: devkitPro's CMake toolchain files are not
published as source, so the from-source toolchain has none. Then
`verify.py: engine: 3ds build`, modelled on `engine: vita build` - SKIPPED,
never red, when devkitARM is absent. The first cross build will find what
the Vita and classic builds found (headers the Mac supplied and the cross
toolchain does not, `%zu`).

### Step 1 - boot headless: the intro trace and the device factor

`omk_3ds_boot` (the `OMKBoot` equivalent): the engine on `sdmc:/omk/`, DataFs
resolving case-insensitively there, `build/omk`'s boot to `traces/intro.log`
**42 of 42 in order** - the same proof every port gave. Beside it the Vita's
bench on the device: the frame's CPU against the M1's, the **32-bit memory**
of a standing street (the counting allocator, `--sites`), and the SD read
time of one area load. Rosalina's GDB stub for whatever crashes. **This step
says how far the New 3DS is from 30 fps** and whether the Old 3DS is worth
step 7.

### Step 2 - the frontend, first light on the software renderer

`n3dsfront.cpp`: HID (buttons, circle pad, touch), `ndsp` audio (the mixer's
float PCM converted to 16-bit, 22050 stereo; the music ring as is),
`svcGetSystemTick` for the clock, `swkbd` for the name field (the Vita's IME
pattern), `osSetSpeedupEnable(true)`. `present()` writes the top screen.
**First light uses the software reference** at 400x240 - slow, but it proves
data, script, audio and input end to end with no GPU code - with the
interface pillarboxed as section 3 decided. Played: the start menu answers,
the flat draws, Kay'l walks.

**WRITTEN 2026-10-06, NOT YET BUILT** - devkitPro is not installed on the
M1 this was written on, so none of it has met a compiler. In
`engine/backends/n3ds/`:

* `Makefile` (replacing the first `CMakeLists.txt`, step 0 says why) - the
  engine library (`OMK_THREADS 0`, exceptions and RTTI on), `omk_boot`
  (`tools/omk.cpp`) and `omk_play` (the viewer's game code: the classic
  build's file list, `playgpu_none.cpp` - the software reference - and
  `playharness_off.cpp`), each a `.3dsx` with the tables in its ROMFS, into
  `engine/build/n3ds/`;
* `n3ds_main.cpp` - the real `main` for both: the card layout
  (`sdmc:/omk/gamedata`, `saves/GAMES`, `omk.ini`, `args.txt`), a dated
  `.log` / `.err` pair a run, stdout and stderr TEED to the file and to
  libctru's console on the bottom screen, the console's model, clock and
  memory as the first lines, `bad_alloc` said rather than an abort, a
  1 MB main stack (`__stacksize__`; `PlayState` is on the heap), and START
  to leave once the program returns;
* `n3dsfront.{h,cpp}` - `omk::Frontend` on libctru: the 640x480 frame
  halved by an exact 2x2 box filter into a pillarboxed 320x240 on the top
  screen (RGB565 straight into the turned framebuffer), the buttons as the
  engine's joystick (A confirm and B back BY LABEL, a choice to play-test),
  the circle pad and the New 3DS's C-stick as the sticks, START as ESCAPE,
  START + SELECT to quit, the HOME menu's close obeyed; `ndsp` with four
  1/20 s buffers refilled from `HostMixer` on the main thread; the clocks
  from the system tick;
* `playscene_off.cpp` - the `--scene` viewer refused, as on the classic Mac.

**OWED IN SHARED CODE, left alone while other sessions work there** (the
reader, 2026-10-06: "focus on new 3ds specific code for now"):

* the NAME FIELD's keyboard - `playframe_modes_parts.cpp` opens the Vita's
  IME under `#if defined(__vita__)`; the 3DS's `swkbd` wants the same hook,
  better as a `Frontend` call (`editText`) both consoles implement than as a
  second `#if`. Until then a new game cannot be named on the 3DS - a SAVE
  (`--save` in `args.txt`) or `--area` starts play;
* the films' hardware path (`playsetup_play.cpp`, `__vita__`), step 5's;
* whatever the first build finds in `src/` (the Vita and classic builds each
  found headers the Mac supplied, and `platform/profile.cpp` uses
  `std::thread` / `std::mutex`, which devkitARM's libstdc++ may or may not
  provide with `OMK_THREADS 0`).

### Step 2b - the bottom screen: the instrument panel

A 320x240 RGB565 surface the CPU composites with the engine's own text
renderer and fonts (`ui/text.*`), redrawn a few times a second (stats move
slowly, so its cost stays off most frames) and written straight to the bottom
framebuffer - no GPU work. Touch picks a button. An INSTRUMENT, not the
game: built like `playharness.cpp`, and `INSTRUMENTS=0` / the release build
leaves the screen with the frame rate alone.

* **Stats**: fps and frame ms (mean and worst of the last second); the
  frame split - sim, draw, present - from the profiler's zones; CPU use as
  busy time over the 33.3 ms period; GPU time (`C3D_GetProcessingTime` /
  `C3D_GetDrawingTime`); memory - the counting allocator's live bytes and
  its top three owners, the linear heap and VRAM free (`linearSpaceFree`,
  `vramSpaceFree`), the application region free (`osGetMemRegionFree`); the
  area, the camera mode, bodies staged, crowd slots live.
* **Debugging**: CAPTURE (a profiler `snapshot` and both the frame and the
  640x480 interface layer dumped to `sdmc:/omk/captures/`, read on the Mac by
  `tools/omkprof.py` and the dump tools); PAUSE / STEP / RESUME (the
  profiler's own commands, issued in-process instead of through `omk.ctl`);
  record a capture on / off; the log's last lines.
* **Enhancements**, live, each OFF by default and saved under
  `[Enhancements]` as on every target - only the rows the PICA can draw
  (`todo/enhancements.md`'s numbers):
  * 2 trilinear (the PICA has mipmaps; anisotropy it does not have);
  * 9 supersampling 2x1 / 2x2 - rendered larger and averaged down by the
    display transfer, the 3DS's own anti-aliasing (row 0's MSAA does not
    exist on the PICA, so 9 stands in for it);
  * 3 the interface's filtered downscale - which matters more here than
    anywhere, everything being drawn at half;
  * 4 unlimited draw distance, 5 fitted shadows, 10 the shoot radar, 11 60
    fps, 12 bodies smoothed between keys - CPU-side or cheap, available as
    they are, worth what the frame budget allows;
  * stereoscopic 3D (step 8), the 3DS's own row;
  * 6 mapped shadows and 7 per-pixel lighting are NOT offered at first: the
    PICA has a shadow-texture mode and fragment lighting through lookup
    tables, both fixed-function, and whether the engine's law fits them is a
    reading for later.
  
  The dither toggle sits with them as on every target - a comparison tool,
  not an enhancement.

### Step 3 - the citro3d backend

`backends/citro3d/` modelled on `gl1render.cpp`, `playgpu_citro3d.cpp` its
glue. First the 16-bit question of section 3, then each item as a reading of
the boundary, not a new design:

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

The `.cia`'s memory mode in the exheader; textures in the linear heap at 2
bytes a texel; render targets in VRAM. Measured on the console standing,
walking and across an area change - the panel shows it live. If the Old 3DS
is still in play, what is left over 64 MB is a list of cuts,
`ram-vs-original.md`'s deferred indexed geometry (-3 MB) the first of them -
**the budget is a goal for the code, never raised to the measurement** (the
classic Mac's rule).

### Step 5 - controls, saves and the films

* the four control schemes' joystick column on the buttons and circle pad,
  rebindable as on the PC (`input/bindings.*`);
* saves in `sdmc:/omk/` (the settings header read at every boot, as now);
* the **films**: MPEG-1 through `pl_mpeg` on the New 3DS's CPU first; the
  New 3DS's hardware decoder (H.264) as the optional path, exactly the
  Vita's `avmovie` pattern - a film absent in that form falls back.

### Step 6 - performance

Measured on the console with the 30 fps cap on, the panel's numbers and the
profiler's captures. In the order the measurement points to, but expected:

* **bodies posed by the vertex shader** (`posesBodies`, as GLES does); the
  PICA's 96 vec4 uniforms hold about 30 3x4 bone matrices, so the bone counts
  of the 181 character models are measured first and a model over the limit
  splits its draw;
* the **second core** (`OMK_THREADS`): on the New 3DS a whole core is the
  app's; on the Old one the system core's share is capped;
* the crowd's density default for the device (options row 6, the original's
  own knob).

### Step 7 - the Old 3DS, decided

Steps 1, 4 and 6 measured on a 268 MHz console. Either a supported target
with its settings written down, or a recorded "no" with the numbers.

### Step 8 - stereoscopic 3D, OFF by default

`gfxSet3D`, the slider (`osGet3DSliderState`), two passes with off-axis
frusta, per-eye visible set and cull, billboards facing the centre camera,
the 2D layers at the screen plane, one pass at slider 0. The convergence
distance per camera mode is a decision made by watching. A toggle on the
panel and a key under `[Enhancements]`.

### Step 9 - packaging and the handoff

`.cia` and `.3dsx`, the layout on the card, a `handoff-3ds-port.md` in the
Vita handoff's form, CLAUDE.md's map row updated.

### Step 10 - EXTRA: what happens when a screen is up, the per-screen survey

**An extra step, taken when everything above works, 3D included** (the reader,
2026-10-06). Until then the interface stays on the top screen as section 3
says.

The reader's priority. For each of the 37 screens of `UI.md` and each thing
drawn over the world, recorded in a table before anything moves:

* whether the WORLD keeps drawing behind it, paused or live (the sneak, the
  shops, the save panel, the start menu each differ), and what of it shows;
* which I2D layers it uses and what text sizes - the half-scale legibility
  judged by the reader on the console, screen by screen;
* what input it takes (the 14-bit word, the name field's characters, a
  pointer anywhere?);
* the candidate placement - stay on top, move to the bottom screen, or split
  - with the reason.

Then the placements the reader picks, one screen family at a time, each
played. The instrument panel moves aside (a button, or the release build)
when a screen takes the bottom. Touch to select a row is an ENHANCEMENT and
comes with that work, not before.

## 5. Traps already known

* **No sound without the DSP firmware**: `ndspInit` fails unless the
  console's DSP firmware has been dumped to `sdmc:/3ds/dspfirm.cdc` (the
  DSP1 homebrew, once). The frontend says so in the log and runs silent.

* **A run in the emulator is not a measurement** - Vita3K played what the
  console could not (the films, the frame time). Timing and memory come from
  the console or are labelled as the emulator's (Azahar, Citra's successor).
* **`--profile` did not work on the Vita console** (`handoff-vita-port.md`
  3b4c, unexplained on 2026-10-05). Prove the capture on the 3DS in step 2b
  before relying on it, and read the log's own numbers until then.
* **A file copied in ASCII mode** is a black screen - the Vita's IAM over
  FileZilla. Copy the data in binary mode, and hash one archive on the card.
* **A positional initialiser shifts** when a boundary struct gains a field
  (CLAUDE.md 1) - a new backend is where that bites first.
* A cross toolchain's headers are not the Mac's: include what is used.
