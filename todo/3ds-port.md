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

**RUN IN AZAHAR, 2026-10-06 - 42 OF 42.** `omk_boot.3dsx` in the Azahar
emulator (2126.1.2, macOS arm64, installed by the reader's go) on its
emulated NEW 3DS - 804 MHz mode, 96 MB of application memory, a 92 MB heap
and a 32 MB linear heap as libctru's start-up split them - with the
emulated card's `omk/gamedata` a symlink to the data tree. The boot's
`--dump` (through `sdmc:/omk/args.txt`), parsed by `engine: boot`'s own
code: the header **(3, 0, 1, 20, 53, 1, 29, 19, 118, 1, 1, 1)**, exactly
what that check asserts on the host, and **42 decisions announced, 42 of
them matching `traces/intro.log` in order** - the original engine's own
capture. The log is the host's line for line but for its load times. Two
faults on the way, both this port's: the tables root `romfs:/` made
`romfs://vm_opcodes.json`, which libctru refuses ("no VM opcode table") -
now `romfs:`; and `osGetMemRegionFree` read 4 GB (libctru takes the whole
region for its heaps) - the log now reports the heaps' sizes and
`mallinfo`. **An emulator's run, not the console's** (section 5's first
trap): it proves the code, not the timing or the memory on hardware.

### Step 2 - the frontend, first light on the software renderer

`n3dsfront.cpp`: HID (buttons, circle pad, touch), `ndsp` audio (the mixer's
float PCM converted to 16-bit, 22050 stereo; the music ring as is),
`svcGetSystemTick` for the clock, `swkbd` for the name field (the Vita's IME
pattern), `osSetSpeedupEnable(true)`. `present()` writes the top screen.
**First light uses the software reference** at 400x240 - slow, but it proves
data, script, audio and input end to end with no GPU code - with the
interface pillarboxed as section 3 decided. Played: the start menu answers,
the flat draws, Kay'l walks.

**BUILT 2026-10-06, NOT YET RUN** - on the M1, with the toolchain
`scripts/3ds-toolchain.sh` built (298 MB in `~/devkitpro`, six fixes to get
devkitPro's buildscripts through on macOS, each commented in the script).
**Every engine source compiled for the ARM11 unchanged, with 0 warnings**;
the one fault was this Makefile's. `omk_boot.3dsx` 1.4 MB and
`omk_play.3dsx` 3.0 MB, the tables in each ROMFS. `verify.py: engine: 3ds
build` holds it (shown to fail). Neither program has run - on a console or
in an emulator - so everything below about BEHAVIOUR is still a reading of
the code. In `engine/backends/n3ds/`:

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
* ~~whatever the first build finds in `src/`~~ - nothing: devkitARM's
  libstdc++ has `std::thread` / `std::mutex` / `std::filesystem` (devkitPro
  builds GCC with `--enable-threads` over libctru), so `platform/profile.cpp`
  and `datafs.cpp` compiled as they are.

**FIRST LIGHT IN AZAHAR, 2026-10-06 - FRAME 300 BYTE-IDENTICAL.**
`omk_play.3dsx` boots on the emulated New 3DS: the films (software MPEG-1,
three quarters of the frames dropped to keep pace - the EMULATOR's speed, not
the console's), area 118, GRID, the start menu. With `--nofmv --frames 300
--dump sdmc:/omk/frame.bin` in `args.txt`, the framebuffer of frame 300 is
**byte for byte** the desktop `omk-play`'s with the same arguments (the
start menu: title, four items, the tile map) - the whole software path, the
interface's compositing included, deciding the same pixels on the ARM11 as
on the M1. A first run differed in 102119 pixels because the READER was
pressing keys in the emulator's window (it had opened the load panel, which
the engine did correctly); the frontend now logs every change of the pad
(`pad: pump N buttons ...`), and the run with no `pad:` line is the
identical one. ndsp fails in Azahar for want of a DSP firmware file on the
emulated card - the console's case before DSP1 - and is now tried once
rather than once a film.

### Step 2b - the bottom screen: the instrument panel

**DONE 2026-10-06, RUN IN AZAHAR** (`n3dspanel.{h,cpp}`, `n3dshost.{h,cpp}`
- the log ring the tee feeds, the card paths, the capture path). The panel
draws in the game's SMALL face: the console line; fps, frame time (mean and
worst over half a second) and the BUSY share (the interval less what the
pacer's `delayMs` slept); the heap in use of libctru's, linear and VRAM
free; the counting allocator's live and peak; the profiler's path and the
state the game wrote; the log's last lines; four touch buttons. Looked at
through `sdmc:/omk/panel-dump` (each redraw also written to
`sdmc:/omk/panel.bin` - an instrument for a screen that cannot be
photographed). **THE PROFILER WORKS ON THE 3DS** - which it never has on the
Vita (`handoff-vita-port.md` 3b4c): `--profile sdmc:/omk/run.prof` writes the
capture a chunk a frame and the `.state`; a `pause` written to `.ctl` the way
the PAUSE button writes it paused the game at frame 434 and wrote the
snapshot; `resume` ran it on; `tools/omkprof.py` reads the capture (442
frames, the zones by name). The buttons' TOUCH is untested by the run - the
reader can click the bottom screen in Azahar. Two things for the console:
VRAM reads 0.0 MB free in Azahar, unexplained; and the game logs a SLOW
FRAME line every frame while it runs slow, each flushed to the card by the
tee - a real cost on an SD card, and the first thing to measure there.

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

**FIRST LIGHT 2026-10-06, IN AZAHAR** - `backends/n3ds/c3drender.{h,cpp}`
(not `backends/citro3d/`: it is the 3DS build's alone), `scene.v.pica` (a
pass-through vertex shader: the vertices arrive in clip space), the glue
`playgpu_citro3d.cpp` in the world slot, drawn OFFSCREEN at 640x480 and read
back to the RGB565 frame - the SDL GL1 path's shape. **The textures were
measured first: 2534 of 2534 are 256x256 (2324) or 64x64 (210), all inside
the PICA's power-of-two 8..1024.** Compared with the desktop software
reference on the same arguments: **Anekbah's street, 99.9% of pixels within
24 levels per channel, mean difference 3.6** (the same picture - the set,
the crowd, Kay'l and his shadow, the fire's additive blend, the sky); **the
Impasse's opening (`--area 222 --scene-chunk 55`), the letterbox's 128 black
rows exactly the reference's and 97.1% within 24 levels**. Two conventions
settled by those runs: the display transfer hands the rows TOP-DOWN (read
bottom-up, the first frame was upside down and otherwise right), and the
viewport's origin is the bottom-left (the letterbox lands where the
reference puts it). The first street frame: 31879 triangles, 15609 drawn,
47400 vertices; 31 textures, 3968 KB as RGBA5551; 3744 KB of VRAM left with
the target and its depth. (The panel's "VRAM 0.0 free" before this was the
pool not yet made: libctru makes it on the first `vramAlloc`.) Not yet:
presenting straight to the top screen, the depth tie baked, bodies posed by
the vertex shader, the 16-bit question (needs the console's own dither), and
any timing - the emulator's says nothing about the console's.

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

### THE FIRST CONSOLE RUN - the reader, 2026-10-06 (a New 3DS, CFW, Homebrew Launcher)

Logs and three panel captures off the card (`EMUNAND9SD/omk/`; the console's
clock says 2017-06-14). What it established, ON HARDWARE:

* **`omk_boot`**: boots headless, 61 decisions as everywhere; the console has
  **124 MB** of application memory under the Homebrew Launcher, an 89 MB heap.
* **`omk_play`**: the films, the start menu, a save loaded (the restaurant,
  AREA 217), out into **Anekbah** with its crowd - the captures are the
  street drawn by citro3d as on the desktop. **Sound works** (the DSP
  firmware was on the card): ndsp at 44100 for the films, 22050 for the world.
  **The touch buttons work**: two CAPTUREs written, STEP and PAUSE refused
  without `--profile`, as they should.
* **The films nearly keep up in software**: EIDOS dropped 147 of 386 frames,
  QUANTIC 8 of the 77 it played before the reader skipped it - the MPEG-1
  path stays (section 3's video reasons). GAME.MPG was missing from the card
  (an incomplete copy; "not decodable, skipped").
* **THE FRAME IS CPU-BOUND** - the reader: *"I try modifying graphic options
  in-game and the framerate didn't change so the bottleneck seems to be the
  cpu"*, and the sections agree. In the street, the mean of 60 frames: world
  submit **44-48 ms** (this backend's CPU transform, clip and cull of every
  vertex - GL1's way), screens/hud **~29** (the CPU composite of the frame),
  present **12-14** (the readback and the 2:1 halving), staged bodies
  **12-13**, pedestrians/traffic **10-13**, scripted motion 4.7. Slow frames:
  median **99 ms**, 90% 138, about 10 fps.
* **The textures were re-uploaded ~20 times** (`c3d: 54 textures ...` at
  every pool change, each re-tiling ~7 MB on the CPU) - hitches walking.
* Loads: Anekbah's set 712 ms, its `.SCX` 841 ms, its **traffic circuit
  1897 ms**.

### Step 3, the CPU work (the reader's go, 2026-10-06) - 3a to 3d

* **3a, DONE 2026-10-06: the texture uploads cached** - as GLES: a texture
  already on the GPU from an earlier pool (the same pixel storage, the same
  size; the entry holds the storage) is kept, the rest uploaded, what the new
  pool does not use dropped. In Azahar on the street's two hand-overs: 31
  uploaded, then **31 kept and 8 uploaded, 43 ms instead of 167**; the frame
  still 99.9% within 24 levels of the reference. The console's ~20 re-uploads
  walking out of the restaurant become uploads of the new textures only.
* **3b, DONE 2026-10-06: the geometry resident, transformed by the vertex
  shader** - the GLES design. Each geometry in a linear-memory buffer keyed
  by pointer, refilled when its revision moves (only the dirty corners when
  it lists them), freed by the residency listener when it is destroyed (or at
  the next pass, if this pass drew it); one changed after this pass drew it
  goes through a per-pass ring. `scene.v.pica` does the view-projection (its
  depth row puts the PICA's hardware near clip at `kNearCut`), the shimmer
  (the 32-entry wave by relative addressing, indexed as `shimmerOffset`), the
  linear fog toward black with the key's exclusions, texels to 0..1. The cull
  is the PICA's, one draw per run of `cornerCull`, `GPU_CULL_BACK_CCW` for the
  reference's back face (measured: the frame is right with it). In Azahar the
  same scores as the CPU path - **the street 99.9% within 24 levels, the
  Impasse letterbox exact and 97.1%** - so nothing of the picture moved; the
  console says what it saved.
* **3b ON THE CONSOLE (the reader's second run, 2026-10-06): world submit
  44-48 -> 9-13 ms**; the texture pool kept what it had (54 kept, 1 uploaded,
  2.8 ms); slow frames median 99 -> **86 ms**, 90% 138 -> 117. What is left
  of the frame: "screens, hud" ~28 - mostly the READBACK, which runs after
  the "world end, submit" mark and so is booked to the next section: the wait
  for the GPU's drawing, then 640x480 converted on the CPU - present 11-12,
  staged bodies 9-15, pedestrians 10-14.
* **16:9 BY DEFAULT** (the reader, 2026-10-06: "use 16:9 resolution by
  default on 3ds since it is already supported"): `--res 800x448`, added by
  `n3ds_main.cpp` only when args.txt names no `--res` - the viewer takes the
  FIRST `--res` it is given, so a default put first silently won over the
  reader's (found in Azahar). 800x448 and not 800x450: 450 is not a multiple
  of the PICA's 8 (the target is rounded up for such a frame), and **800x450
  asked a 135 MB allocation of the 32-bit build at the first world frame -
  bad_alloc, in Azahar; the desktop draws it. UNEXPLAINED, open** - a 32-bit
  sizing somewhere the 64-bit build does not show. 800x448 in Azahar: 99.8%
  within 24 levels of the desktop at the same size; halved, 400x224 on the
  top screen (93%, where 4:3 used 80%).
* **3c, DONE 2026-10-06 (Azahar): the straight present.** The 3DS glue now
  answers `gpuWindowBuild()`, so the build decides frame by frame - the GLES
  window's gates in `playframe_world_draw.cpp` - whether anything is drawn
  over the world; a frame with nothing over it skips the readback and the CPU
  composite: inside the frame the display transfer halves the target with its
  own 2x2 average (`GX_TRANSFER_SCALE_XY`) into a linear buffer, and the CPU
  writes that, dithered, into a 400x240 screen image the frontend's `present`
  copies 1:1 (its stats and CAPTURE as ever). One convention found: with
  SCALE_XY the transfer halves the OUTPUT size it is given, so it is given
  the input's (told the halved size it wrote a quarter-size picture). A
  capture of such a frame (`sdmc:/omk/capture-at`, a new instrument: CAPTURE
  on a given present) against the desktop's 800x448 frame halved: **97.0%
  within 24 levels** (frame 100 against 120 - the crowd moved). Frames with
  the interface over the world keep the CPU path; the GLES overlay's way of
  blending them on the GPU is not ported.
* **3d, DONE 2026-10-06 (Azahar): bodies posed and lit by the GPU** -
  GLES's `kPosedVert` as `posed.v.pica`, fitted to the PICA's 96 uniform
  registers: its first three uniforms declared in the scene shader's order
  (so they carry across a program switch - checked at start-up, posing
  refused if they do not line up), then `misc` (the lit-from-black flag and
  the scene's grey, `Draw::lightBase`), eight lights of two rows, and **23
  pose slots** of three rows; no shimmer wave (its 32 registers are the
  slots'), so the scene's wave is set again on the way back from a posed
  draw. The rest geometry is resident with its mesh-to-slot map (once per
  revision); `applyLights`'s law per corner on the turned normal (eight
  lights unrolled, unused ones zero). A body over 23 slots, with a
  shimmering corner or over eight lights is posed and lit on the CPU into the
  ring by the same law. The renderer answers `posesBodies()` and
  `maxVertexLights()` 8, which moves five frontend sites onto it: the staged
  bodies, the walkers, the player, the sky and the set's moving meshes. In
  Azahar: **1960 posed draws on the GPU, 0 on the CPU** over the street run,
  its frame 99.8% within 24 levels of the desktop (which poses on the CPU),
  the Impasse at 800x448 96.1% with the letterbox exact.

### THE THIRD CONSOLE RUN - 3a to 3d (the reader, 2026-10-06)

* **The CPU work collapsed**: world submit 9-13 -> **2.7-3.9 ms**,
  pedestrians/traffic 10-14 -> **3.1-3.6**, staged bodies 8-10; the
  **sim+draw phase ~30 ms**, inside the 33 ms budget. The straight frames
  were right on the console (captures).
* **What was left was this port's own present: 22-36 ms.** 3c handed the
  frontend a 400x240 picture every frame, and its general fitting loop did
  two integer divisions a pixel - ~190 000 SOFTWARE divisions a frame, the
  ARM11 having no divide instruction. Now a frame the screen's size is a
  straight copy into the turned framebuffer and anything else samples
  through row and column tables made once per size (the 2:1 path never
  divided). Proved in Azahar against the screen itself - CAPTURE now also
  writes the top screen as the hardware holds it (`screen-<n>.bin`): **the
  exact path 96000 of 96000 pixels, the 2:1 path 89600 of 89600, the bands
  6400 of 6400 black.**
* **The run was 640x480, not 16:9**: args.txt held `--profile` alone, which
  took the next argument - the default `--res` - for its path. args.txt
  lines are now split on spaces (`--profile sdmc:/omk/run.prof` on one
  line), and a `--profile` with no path gets `sdmc:/omk/run.prof`.
* Still to measure: the frame with the present fixed, and the profiler's own
  cost on the card (a chunk written every frame) - a run with `--profile` is
  not the number to quote for speed.

### THE FOURTH CONSOLE RUN - the present fixed (the reader, 2026-10-06)

16:9 at last (800x448). **present, swap 22-36 -> 11 ms.** But "world begin..end
(submit)" read **16-21 ms** where the 640x480 run had 2.7-3.9, and sim+draw
47-56 in the street; 171 slow frames against 1001. The section cannot say
why - it holds the CPU posing fallback, the uniform writes into the command
buffer, and any wait on the GPU should the buffer fill - so the backend now
logs its own line every 60 frames (`c3dReport`): draws, posed on the GPU and
on the CPU with the CPU's time, the time waited on the GPU at a transfer,
the command buffer's peak, citro3d's drawing and processing times. In
Azahar: ~80 draws a pass, 11-19 posed on the GPU, 0 on the CPU, the buffer
3-5% at most (the emulator's GPU waits read 0 - the console's will not).

### THE FIFTH CONSOLE RUN - CPU or GPU? (the reader, 2026-10-06)

The backend's line answered it: **waiting on the GPU at the transfer 0.00
ms**, citro3d's "drawing" 37-50 ms (it spans the recording - citro3d feeds the
GPU as commands are recorded), the command buffer 6-9% at most, 22-54 posed
draws a frame on the GPU and none on the CPU. **The GPU keeps up; the frame
is CPU** (**REFUTED by the seventh run below**: the 0.00 ms was a timer
around a call that never waits) - sim+draw 33-43, present 10.6. Finer timers (`c3d CPU` and
`frontend:` lines) then showed, in Azahar, the straight present's per-pixel
loop at 15.4 ms: `quantise888Dither` a pixel, whose `quantise888` divides by
255 three times - software divisions on the ARM11. Moved to the
table-driven `quantise888DitherRow` (bit for bit the same, `engine: pixel
tables`; the transfer's word byte-swapped into its RGBA order) it fell only
to 10.4 - touching every pixel on the CPU is the cost. **THE 16-BIT
EXPERIMENT** (`sdmc:/omk/c3d-rgb565`, an instrument): the target RGB565 - the
original's framebuffer depth - and the transfers 565, no CPU conversion:
**0.9 ms**, the picture right. Whether the PICA DITHERS into a 16-bit buffer
- section 3's open question, which decides whether this is the faithful
default - only the console can show (Azahar cannot). The panel's redraw is
~7.5 ms twice a second, the frontend's copy 1.5 ms a frame (Azahar's CPU).

### THE SIXTH CONSOLE RUN - the console's own CPU figures (the reader, 2026-10-06)

The default pass (RGBA8 + CPU dither; the 16-bit pass not yet run): submit
**3.3 ms**, the straight present's dither loop **5-6**, **the frontend's copy
6-7.6** (1.5 in Azahar - the emulator does not model the cache), the panel's
redraw 6-15 twice a second; sim+draw 38-48 in the street. **The copy was a
cache fault**: the framebuffer runs in columns, so copying column by column
read one pixel per 800-byte row. Now in 8x8 TILES (eight short rows read,
eight short columns written) - proved 96000/96000 against the screen as
written. And the "world begin..end (submit)" section (10-21 ms) holds more
than the backend's 3.3 ms of submits - `drawWithMirror`'s `splitList` scan
of every draw's `cornerMirror` before the pass, and whatever the frontend
does between submits; the report now also times `begin` and the whole
begin..end of a pass, so the next run separates them.

### THE SEVENTH CONSOLE RUN - the GPU's wait found, and a present that read too early (the reader, 2026-10-06)

`omk-play-20170614-164918.log` (the console's clock says 2017): the films,
the restaurant (AREA 217), Anekbah's street; ~1225 frames, RGBA8, no capture,
at `102b751` (the M3's first 3DS build).

* **The tiled copy: 6-7.6 -> 1.5-2.9 ms a frame.** The films and the menu
  still read 7.7: the frontend's 2:1 average (an 800x448 frame onto the
  400x240 screen) was the same column-by-column walk, untiled. Now tiled too
  (proved on the host against the old loop: 480000 of 480000 words over five
  frame sizes, and 438672 differing with the source one row off).
* **The "waiting on the GPU at a transfer" figure never measured anything.**
  Inside a frame citro3d's `C3D_SyncDisplayTransfer` only QUEUES the
  transfer and returns (citro3d 1.7.1, `renderqueue.c:417`); only out of a
  frame does it wait for the queue and the transfer. So the timer around it
  reads 0.00 whatever the GPU does, and the fifth run's verdict rested on it.
  The wait is in the NEXT pass's `C3D_FrameBegin`, which waits for the whole
  queue - and this run's new `begin` timer caught it:

  | where | draws a pass | `begin` | citro3d drawing |
  |---|---|---|---|
  | the restaurant | 8-14 | 0.0-0.2 ms | 5-15 ms |
  | the street, frames 419-1079 | 61-149 | 0.2-10 | 17-43 |
  | the dense street, frames 1139-1199 | 217-224 | **13-16** | **52** |

  So in the dense street the GPU is as long as the CPU, both over the
  33 ms budget: the PICA200's side has to come down too, not only the CPU's.
* **And the same misreading was a CORRECTNESS bug.** `readback()` and
  `presentHalf()` read their buffer straight after queuing the transfer into
  it - before the GPU had written it. Azahar finishes every command as it is
  queued, so every emulator capture was exact; on the console the present
  showed the previous frame's picture, or a tear where the GPU was writing
  the new one as the CPU read. Fixed both ways the callers need:
  `readback()` (an interface composited over the picture, the mirror's
  passes of one frame) is SYNCHRONOUS - the transfer out of the frame, which
  waits; `presentHalf()` (nothing over the world) runs ONE FRAME BEHIND by
  the reader's decision ("a 1 frame delay is acceptable for this kind of
  game it allows better performances"): two buffers, this pass's transfer
  queued into one while the CPU shows the other, which the last pass filled
  and this pass's `begin` waited for. Only when the last pass was a straight
  present too; otherwise that frame waits for its own. `sdmc:/omk/c3d-sync`
  forces every one synchronous, for laying the two side by side. The `c3d`
  line now reads the wait at begin and at a synchronous transfer apart, and
  counts the presents each way. **Proved in Azahar** (2026-10-06, the M3,
  Azahar 2126.1.2; the street start, `--frames 360`, CAPTURE at present 300,
  against the desktop's 800x448 frames halved, % of pixels within 24
  levels): the default capture peaks at desktop frame 300 (**99.86%**, its
  neighbours 99.35/99.38) and the `c3d-sync` capture at 301 (**99.85%**,
  99.36/99.41) - exactly one frame apart, each a clean single frame; 359 of
  360 presents a frame behind, the first synchronous (no last picture yet).
  Azahar cannot show the race the fix closes; the console run owes the speed.
* The rest: the dither loop 5.2-6.2 ms, unchanged; submit 1.1-3.4 with 5-6 ms
  spikes where textures go resident; the panel's redraw grew 6 -> 15 ms in
  the street (twice a second); 109 slow frames, the worst 4.8 s at
  Anekbah's load; `pump` 39 ms over the last 60 frames only (most likely the
  HOME button on the way out - unconfirmed). GAME.MPG still "not
  decodable".

### THE EIGHTH CONSOLE RUN - the five-run card session (the reader, 2026-10-07)

`omk_play.3dsx` built from `842bd42` (in a clean worktree, so other
sessions' uncommitted edits stayed off the card; smoke-run in Azahar first).
Five runs on the street start (`--nofmv --save ... --area 0 --stand
1804,0,-6890,336`), the reader walking into the dense part, 900-1740 frames
each; the logs and both captures are outside the repo (the reader's
`3DSruns/`). **No FATAL, no refused allocation, in any of the five**; the
only ~1 s frames are the street's load (frame 2). The routes differ, so the
runs are compared by DRAWS a pass (the `c3d` line's 60-pass windows from
frame 119, binned), never window for window. "Frame" below is sim+draw plus
the present (ms, mean of the bin):

| run | 0-90 draws | 90-140 | 140+ | wait at `begin` (90-140 / 140+) | citro3d drawing (90-140 / 140+) | present |
|---|---|---|---|---|---|---|
| A one frame behind (the default) | 33.9 | 47.4 | 58.7 | 9.1 / 14.5 | 39.4 / 52.2 | 10.7-11.7 |
| B `c3d-sync` | 52.8 | 62.9 | 78.9 | (its wait is at the transfer: 31 / 42) | 32.4 / 41.3 | 30-50 |
| C `--res 400x224` (1:1) | 29.8 | 34.1 | 40.1 | 0.2 / 2.1 | 22.8 / 31.1 | 10.7-10.9 |
| D = A + `capture-at` | 42.1 | 48.1 | 61.5 | 10.8 / 13.9 | 41.1 / 51.4 | 11-15 |
| E `c3d-rgb565` | 33.6 | 46.9 | 53.8 | 12.6 / 15.5 | 40.9 / 48.4 | 6.9-8.3 |

* **A against B - what the one-frame-behind present buys: ~25% of the
  frame** (47 against 63 ms in the street, 59 against 79 dense; ~21 against
  ~16 fps). Confirmed on the console; whether anything tears or lags to
  the eye is the reader's to say.
* **C - the GPU's side is largely FILL.** A quarter of the pixels cuts
  citro3d's drawing by ~40% (39 -> 23, 52 -> 31) and the wait at `begin`
  all but vanishes: the GPU keeps up at 400x224 and the frame is the CPU's
  (~34 ms in the street, ~40 dense: ~29 and ~25 fps). Fitted as `a + b x
  pixels` over the two sizes, the 800x448 dense frame is ~28 ms of fill and
  ~24 of the rest (vertices, commands - growing with the draws), so fill is
  about half: the target size is the larger lever and the draw count the
  other. 400x224 has no 2x2 average (no anti-aliasing) and is still the
  READER'S decision; `GX_TRANSFER_SCALE_X` alone (800x224 halved
  horizontally) would be a middle point, half the pixels, not measured.
* **E - the 16-bit present costs 4 ms less** (the dither loop 6.0 -> 1.8 ms,
  the present 11 -> 7) and the GPU draws no faster in 565 (41/48 against
  D's 41/51).
* **D and E - the 16-bit QUESTION, by eye and by count** (`screen-301.bin`
  of each, decoded 240-a-column, the content rows only, dark = green level
  under 10 of 63; the ground enlarged 6x with the contrast raised). D, the
  RGBA8 target with the CPU's 4x4 ordered dither, shows the fine regular
  pattern: 36% of neighbouring dark pixels one level apart, identical runs
  1.34 long. E, the RGB565 target, shows NO regular pattern - flat blotches
  of one level, identical runs 2.32 long, 25% one level apart: the
  posterised look of a 565 picture without a dither. So by the plan's rule
  (a fine pattern = the PICA dithers) **it does not, as seen**. One caveat
  this capture cannot close: E's 800x448 picture reaches the screen through
  the transfer's 2x2 average, which would smear a GPU dither; E at
  `--res 400x224` (1:1, nothing averaged) is the clean test if it matters.
  The default is the reader's choice (16-bit: 4 ms faster, posterised;
  RGBA8 + CPU dither: the original's DITHERENABLE look).
* **E showed distant parts as PLAIN BLUE, sometimes** (the reader). Every
  pixel nothing draws - past the clip distance, gaps in the set - shows the
  cleared target, and the clear was `0x000000FF`, black only in RGBA8:
  citro3d hands it to `GX_MemoryFill` unconverted and a 16-bit fill keeps
  the low half, `0x00FF` = red 0, green 7, blue 31. Fixed (the clear value
  by the target's format); in Azahar, the street start with `--clip 10`,
  `c3d-rgb565` and a capture at present 60: **5187 pixels of `0x00FF`
  before, 0 after**, black up by about as many. The default RGBA8 target
  never had it. Not run on the console yet.
* **...and the picture now clears to the FOG's colour, which the fog fades
  into** (the reader's go, 2026-10-07). The backend cleared to black and
  faded toward black ("keeps its fade, not its colour"), from the reading
  that the shipped fog is black - corrected 2026-10-06 (`renderer.h`,
  View::fog: the AREA's colours lerped by the clock). The vertex shaders now
  put the fog factor in the colour's alpha and texture combiner stage 1
  blends the textured colour toward the fog colour by it (`r f + fog (1 -
  f)`, the reference's order); the untextured stage 0 takes an opaque
  constant alpha, the colour key's alpha passes through. In Azahar (the same
  `--clip 10` start, capture at present 60, against the desktop software
  reference's frames 59-62 halved): **99.90% within 24 levels on the RGBA8
  target, 99.91% on RGB565, 44.2% before**; the band past the clip distance
  is 12 25 35 (`0x08C4` in 565) on both. Not on the console yet.
* The rest: the frontend's copy 2.7 ms median (1.4 in B), the panel's redraw
  ~13.5 ms median twice a second (8 in B) - larger than the seventh run's
  6-15 and still a CPU cost worth cutting (section 4 of the handoff, item 4).
  The film copy was not measured (every run `--nofmv`).

### THE YARDSTICK - the original's machine against the New 3DS (2026-10-07)

The reader's question: does the 3DS have to draw LESS than the original, or
does the port lack optimisation? Sources are thin and partly disagree, and
the table says so.

| | the original, minimum | the original, recommended | New 3DS |
|---|---|---|---|
| CPU | Pentium 200 or 233 MMX (sources differ; SPECint95 7.12 at 233) | Pentium II 266 or 300 | ARM11 MPCore 804 MHz (the app's core), in-order ARMv6K, VFPv2 - no NEON, no divide |
| RAM | 32 MB | 64 MB | 124 MB to the application |
| video | a 4 MB DirectX 6.1 card | an 8 MB 3D card | PICA200 at 268 MHz, 6 MB VRAM; DMP quotes 800 Mpixel/s peak at 200 MHz |
| picture | 640x480, 16-bit (`engine-spec-1999.md`) - 307 200 pixels | same | the port draws 800x448 RGBA8 (358 400), the screen shows 400x240 |

Sources: the minimum - dvd-fever's review quoting the box (233) and
gamepressure/vgtimes listings (200; recommended PII 266, or 300); the
Pentium MMX 233's SPEC - cpushack; the 3DS - 3dbrew.org Hardware and
Memory map; the PICA200 rate - Wikipedia "PICA200" (DMP's figure); the
period cards - Tom's Hardware (Voodoo2 90 Mpixel/s a TMU, TNT ~180
delivered). On period hardware the original was NOT smooth outdoors: the
dvd-fever reviewer's Pentium 200 MMX "slows to a crawl" in the street with
pedestrians and cars, and reviews cite the frame rate as a fault
(Wikipedia). The engine has no frame cap (`sixty-fps.md`).

**The runs used the HEAVIEST settings the game has.** `save-appart.bin` (the
card's copy byte-identical to `traces/`) carries: clip 200 m (the largest of
25/50/100/150/200), crowd 4 (0..4), sky on, shadows on, level of detail 2
(0..2, `ASSETS` - the street models' whole LOD chain and every shadow).

**What the eighth run's numbers say against that:**

* **The GPU is the port's, not the chip's.** The fill-dependent half of
  citro3d's drawing is ~28 ms a dense frame at 800x448 (the two-size fit
  above). At an overdraw of 3 to 5 that is ~40-65 Mpixel/s actually
  delivered - a 1999 card's rate, from a chip quoted several times faster.
  And run E rules one suspect OUT: halving the colour target's bytes (RGB565)
  did not shorten the drawing (41/48 ms against 41/51), so colour bandwidth
  is not it. Left to measure, none established: the textures in linear
  (FCRAM) memory rather than VRAM, no mipmaps (a distant surface sampling a
  large texture thrashes the texture cache - the original had none either,
  but on a card with its own memory), the 24-bit depth buffer's traffic,
  and the vertex half (corner soups, 3 vertices a triangle; the posed
  shader's eight lights a vertex).
* **The CPU is the port's too.** With the GPU's wait taken out a dense frame
  is ~33 ms of CPU plus ~11 of present - while the original's CPU did all of
  that AND the transform, the lighting and the skinning (pre-transformed
  vertices) on a 233-300 MHz Pentium, and the port has moved those to the
  GPU. The present (dither loop 6 ms, copy 2.7) is pure port cost - the
  original flipped. The instrumented spans cover ~15-18 ms of the 33; the
  rest is unattributed. (No sourced per-clock comparison of an ARM11 with a
  Pentium was found; none is claimed.)
* **So the answer is optimisation first.** Lowering the settings is not
  lowering the original's quality - they are the original's own options,
  which its minimum machine needed - but choosing 3DS defaults is the
  reader's call and should come AFTER the port's own costs, measured: a run
  at a lower clip / crowd says what the options buy.

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

* **The emulator's window takes the keyboard** - Azahar maps keys to the
  3DS buttons, so a person typing while it is in front drives the run (it
  opened the load panel in one, 2026-10-06, and the frame was then evidence
  about the keys). Read the log's `pad:` lines before comparing a frame: a
  run with none had no input.

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
