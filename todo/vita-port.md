# The PS VITA port — the plan

Written 2026-09-18. Picks up [`handoff-vita.md`](handoff-vita.md), which
recorded the DECISION (6.0 ms of main-thread work a frame on an M1, 137 MB live
heap, the device factor unknown) and deliberately started nothing. This file
starts it: what is missing, in what order, and what already landed.

> **THIS TREE IS SHARED.** Stage by explicit path — never `git add .`, `-A`
> or `commit -a`. The Vita files live under `engine/backends/gles/` and
> `engine/backends/vita/`, outside the Makefile's `src/*/*.cpp` and
> `tools/*.cpp` globs, so they are in nobody's default build.
>
> **KEEP EVERY TARGET COMPILING.** The same `src/` builds for the host
> (`make`), the Vita (`make vita`, VitaSDK) and, through `glesrender.cpp`,
> any GLES2 host. A change can build on one and break another - it already
> happened three times (below). `verify.py: engine: vita build` runs the Vita
> build and skips where no SDK is installed; run it with `--only` after any
> change to `src/` that adds an include, a platform call or a new file.

---

## 0. The state when this was written

**Done before this file** (`handoff-vita.md`, `optimization.md` steps 7-14):
the decision's measurements; the portable thread manager
(`platform/threads.{h,cpp}`, Vita half never compiled); the shadow bone index
(built, **not wired** — `pending/vita-meshidx-playcpp.md`); the depth tie's
memory localised (not cut).

**Landed by this file** (new files only; the host ones run on the M1, the
Vita ones built with VitaSDK 2026.08 - see below):

| file | what | state |
|---|---|---|
| `engine/backends/vita/bench_main.cpp` | THE DEVICE FACTOR: `composePose` + `applyPose` + `applyLights` over 45 street bodies, inline and through `omk::Threads`, per-stage ms, a hash across both | **run on the M1** (below); Vita side needs the SDK |
| `engine/backends/gles/glesrender.cpp` | the GLES2 `Renderer` behind `o3de/renderer.h`: vitaGL / macOS GL 2.1 / GLES2-WebGL from one source; present pass with the 565 dither on the GPU | **run on the M1** through the probe |
| `engine/backends/gles/gles_probe.cpp` | `run_vulkan`'s differential for GLES, headless (CGL, no window) | **green, and shown to fail** (below) |
| `engine/backends/vita/vitapad.h` | the Vita pad as the game's own JOYSTICK device | superseded the same day by `src/input/pad.h` and deleted (below) |
| `engine/backends/vita/smoke_main.cpp` | one set on the Vita GPU, stick-flown, SQUARE toggles the depth tie, timings to `ux0:data/omk/smoke.txt` | **builds** (VPK); not yet run on a console |
| `engine/backends/vita/CMakeLists.txt` | VitaSDK build: `omk_bench`, `omk_smoke`, `omk_vita` (the game, OFF until F1-F4) | **builds** `omk_bench.vpk` + `omk_smoke.vpk` |

### What the M1 runs said

**The bench** (`vita_bench <gamedata> 45 300`, Anekbah's 155 lights, HO1_FNM +
PSH_FN alternating on line 125338):

```
inline   compose 0.150  apply 0.688  light 0.470  total 1.311 ms/frame  (lights reached 1.8/body)
threads  8 runners      total 0.527 ms/frame  speedup 2.48x
hash inline 14de822f23fe92e4 threads 14de822f23fe92e4  threads: EXACT
```

1.31 ms against the handoff's profile attribution of ~1.34 (0.24 + 0.45 +
0.65) for the same three stages — so the bench measures the work the decision
was about, and a device run of the same binary gives the ratio directly.
**The first Vita action is to run `omk_bench` and divide.** The threaded pass
is byte-identical (each body writes only its own geometry), so posing is safe
to spread across the Vita's three game cores as far as these three functions
go. Note the apply/light split differs from the profile's (apply is the
larger here); the profile was of the live frame with the crowd's LOD, the
bench poses full models.

**The GLES probe** (Aapkayl through dialog 402's camera 4555, 640x480):

```
gl: Apple M3 | 2.1 Metal - 90.5
plain     coverage 0.9961   present world: EXACT   present surface: EXACT
dithered  coverage 0.9977   present world: EXACT   present surface: EXACT
letterbox 640x352 at row 64                        present: EXACT
```

Coverage matches or beats Vulkan's quoted 0.995 on the same set. The two
"present" lines are exact claims: the GPU's float-shader 888→565 dither
agrees with `quantise888DitherRow` on every pixel. **Shown to fail**, each
mutation on a scratch copy and asserted to have applied: dither offset -8→-7
(46449 + 33324 pixels red), the target's row flip off by one (186802 red), the
letterbox row placement off by one (187442 red), the projection's Y sign
(coverage 0.9495 red). **One blind spot:** removing the cutout discard leaves
it green — Aapkayl has 8 cutout triangles and none in shot. G2 needs a
cutout-heavy camera before the cutout rule counts as checked.

A lesson from building it, the §1 kind: the first version compared the
present in 888 and reported **113303 differences** on the surface path. All
of them were the Mac driver expanding 565→888 by rounding (blue 3 → 25) where
the probe assumed bit replication (24). `ui/surface.h` already says it: *the
expansion is the host's; compare in 565.* Nothing was wrong with the backend.

### 2026-09-18, later: the SDK is in, both VPKs build

VitaSDK 2026.08 (GCC 15.2) installed at `~/vitasdk` from `vdpm`'s bootstrap
(no sudo), with `vitaGL`, `vitaShaRK`, `libmathneon` and **`sdl2_vitagl`** -
SDL2 with vitaGL as its video backend, which settles F1's open question: SDL
and vitaGL coexist, so `play.cpp` keeps SDL on the Vita.

**The whole `src/` compiled for the Vita at first attempt except three
faults, all FIXED in the sources** (first worked around in the CMake file
while the files were held, then fixed properly the same day):

* `actor/walk.cpp`, `script/area.cpp` - `std::sqrt`/`floor`/`pow`/`isfinite`
  without `#include <cmath>` (the host's headers pull it in; newlib's do not).
  The include is added to both.
* `platform/threads.cpp` - the Vita worker was a free function naming the
  PRIVATE `Threads::Impl`; the Vita half had never been compiled. It is now
  a static member, `Impl::workerMain`. (`thread_probe` on the host: 0
  mismatches - the host half is untouched.)
* **`std::filesystem` compiles** (datafs, savefile, area) - B2 is closed for
  compilation; whether it BEHAVES on `ux0:` is the bench's first file read.

Built: `engine/build/vita/omk_bench.vpk` and `omk_smoke.vpk` (gitignored).
**To run** (the only benchmark that counts - on the console):

1. copy the game's data tree to `ux0:data/omk/gamedata/` (the `MESHES/`,
   `MORPH/`... folders directly inside it);
2. install the VPK with VitaShell; for `omk_smoke` also have
   `ur0:data/libshacccg.suprx` extracted (VitaShell / ShaRKBR33D);
3. launch; `omk_bench` exits by itself and writes `ux0:data/omk/bench.txt`;
   `omk_smoke` writes `ux0:data/omk/smoke.txt` every 300 frames, START quits.

Rebuild: **`make vita`** from `engine/` (finds `$VITASDK` or `~/vitasdk`,
drives the CMake build in `build/vita-cmake`, copies the VPKs to
`build/vita/`; ~2 min fresh, incremental after; prints a note and exits 0
without an SDK). Also new, and needing nothing installed: `make
build/vita_bench` (the bench on this machine) and `make gles-probe` (macOS).

**Three checks** (all `engine:`, so `--slow` / `--only`):
`engine: vita bench` (the threaded pass EXACT), `engine: gles backend` (the
probe's coverage and five EXACT presents; macOS only), `engine: vita build`
(the cross-compile; skipped without an SDK). The licence-header count moved
443 -> 458, attributed in the check (9 of it was already red at `202937c`).

### 2026-09-18, later still: THE GAME BUILDS FOR THE VITA (`omk_vita.vpk`)

`make vita` now builds three VPKs, the game first. What it took:

* **F1, the GLES window mode in `play.cpp`** (`-DOMK_GLES`, beside
  Vulkan's): an SDL window with a GL context (GLES 2 attributes except on
  macOS), `makeGlesRenderer`, the world drawn through it, and every frame
  presented COMPOSED - readback, the interface on the CPU,
  `glesPresentSurface`, `SDL_GL_SwapWindow`. **Played on this Mac** as
  `make play-gles` / `build/omk-play-gles`: the Anekbah street start, 120
  frames, against `--software` - coverage **0.9919**, crowd, Kay'l, the fire,
  the sky and his shadow all present. (The FIRST such run stopped after one
  frame and dumped frame 0 - a letterboxed, half-drawn picture; it did not
  reproduce in three reruns, with the event stream logged, and is recorded
  here unexplained rather than guessed at.)
* **F2, the pad**: `src/input/pad.h` (platform-free) maps a gamepad onto the
  engine's own JOYSTICK device; `SdlFrontend` reads SDL's game controller
  (SDL2 and SDL3) into `HostInput::pad`, and the frame hands it to
  `pad::toDevices`. START is ESCAPE, straight into `held`, because the pause
  check reads that before any device state exists. Replaces the stand-in
  `backends/vita/vitapad.h` (deleted). SDL2 on the Vita reports the Vita's
  own pad as a game controller, so desktop and Vita share one path. **Not yet
  tried with a physical pad** - on the play-test list.
* **F3, READ AND PORTED** (`a2256ca`, `engine: input poll`): `Input_Poll`
  hardwires the stick to slots 0..3 (threshold 0 - `dword_52F498` is never
  stored to - over DIPROP_RANGE -1000..1000, no engine dead zone) and reads
  the joystick TABLE only from slot 4; a joystick code is a byte offset into
  `DIJOYSTATE`. And its keyboard fixes - the shifts mirrored, left Ctrl
  sets right Ctrl, TAB dropped under ALT - were missing from the port: left
  Shift ran nothing, and `play.cpp`'s left-Ctrl remap, which called itself
  "the viewer's choice", was the game's own rule.
* **F4, the entry point**: `backends/vita/vita_main.cpp` is `main`;
  `play.cpp` is compiled with `-Dmain=omk_play_main` and handed the
  arguments (data `ux0:data/omk/gamedata`, tables packaged in the VPK as
  `app0:tables`, saves `ux0:data/omk/saves/GAMES`, `--res 640x480`, an
  `omk.ini` and extra lines from `ux0:data/omk/args.txt` when present). It
  raises the heap to 300 MB and the main thread's stack to 8 MiB - `main` is
  one 18800-line frame. stdout / stderr go to `ux0:data/omk/omk-play.log` /
  `.err`. `SDL_StartTextInput` is skipped on the Vita (it would raise the
  system keyboard over the game); the name field needs the IME, which is open.
* **The link**: `vita-elf-create` failed with *"Cannot allocate 4280 bytes
  for SCE data at end of segment 0; segment 1 overlaps"* - the import stubs go
  at the end of the code segment and the data segment starts at the next
  64 KiB boundary, so it is luck of alignment (the same fault as
  vitasdk/buildscripts#186, an SDK change of 2026-09). The SDK's own linker
  script reserves `__sce_headroom` when it is defined; the CMake file
  defines it (16 KiB) for every target.

**To try it**: install `build/vita/omk_vita.vpk`, have
`ur0:data/libshacccg.suprx`, copy the data to `ux0:data/omk/gamedata/`, and
read `ux0:data/omk/omk-play.log` after. A first run with `--nofmv` in
`ux0:data/omk/args.txt` skips the ~143 s of intro movies. **None of this has
run on a console**: SDL2-vitagl's window size, `std::filesystem` on `ux0:`,
the audio device and the frame rate are all the device's to answer.

### 2026-09-18: THE EMULATOR - Vita3K runs our VPKs, and found a crash

**Vita3K** (continuous build of 2026-09-17, native macOS arm64, `macos-arm64-
latest.dmg`) runs homebrew and needs the Vita firmware (`PSVUPDAT.PUP` /
`PSP2UPDAT.PUP`, installed from its setup window). How it is driven here:

* a config of its own (`-c <dir>/config.yml`) with `pref-path` set, so the
  emulated storage is a scratch folder and the user's own Vita3K is untouched;
* `ux0:data/omk/gamedata` a SYMLINK to the data tree - no 1.7 GB copy;
* the VPKs INSTALLED BY HAND (a VPK is a zip; unzip it to `ux0/app/<TITLEID>/`)
  - this build's Qt window ignores a VPK given on the command line - and
  launched with `-r <TITLEID>`;
* the bench runs to completion headless-enough (its window opens and closes).

**Driven by one command**: `scripts/vita3k-run.sh setup` fetches the emulator
into `engine/build/vita3k/` (gitignored, with its storage `fs/` and the data
symlink); `scripts/vita3k-run.sh bench|smoke|game [seconds]` unpacks that VPK
into `fs/ux0/app/` and launches it, the log in `engine/build/vita3k/<target>.log`
and the programs' output in `fs/ux0/data/omk/`. Vita3K keeps ONE storage path,
in its GLOBAL config (`~/Library/Application Support/Vita3K/Vita3K/config.yml`),
and checks `-r` against it before any `-c` config applies - so the script
points that global setting at `fs/` each run (a machine using Vita3K for other
things should know). The firmware comes from Vita3K's own setup window the
first time. **The shader compiler goes at
`engine/build/vita3k/fs/ur0/data/libshacccg.suprx`.**

**What it gave, in the first hour:**

1. **A CRASH A CONSOLE WOULD HAVE HAD, fixed**: the SDK's newlib has no C99
   printf formats, so `%zu` prints "zu" and consumes NO argument - every later
   argument shifts. The bench's `lights %zu from %s` passed the light count to
   `%s` as a pointer (Vita3K: invalid read at 0x98, `r0 = 0x9b` = 155). The
   engine has **125** `%zu`/`%zx` (`play.cpp` 66), so the game would have
   faulted in its first log line with one. Fixed at the platform edge:
   `backends/vita/printf_c99.cpp`, linked into every target via
   `-Wl,--wrap=<printf family>`, strips `z`/`t` (32-bit on the Vita, so
   exact) before newlib sees the format. Re-run: the model lines are the
   host's, 0 invalid reads. `verify.py: engine: vita printf`.
2. **The bench's plumbing works**: `ux0:` reads through `DataFs` (so
   `std::filesystem` behaves there), the Vita thread pool starts 3 runners,
   2.78x, threaded output EXACT. **Its TIMINGS are the emulator's, not the
   device's** (35 ms against the M1's 1.3 - a JIT on an M3), so they are not
   the device factor and are not quoted as one. And the emulated hash
   (`166ee410...`) differs from the M1's (`14de822f...`): the Vita build turns
   FMA contraction off and the host compiler may not - noted, not chased.
3. **vitaGL's boot splash opens a SECOND sceGxm context**, which Vita3K
   refuses (`ALREADY_INITIALIZED`, then a fault at 0x78). `scripts/vita-
   vitagl.sh` builds vitaGL at the SDK package's own commit (`cd3791e`, whose
   header is byte-identical to the SDK's) with `NO_SPLASHSCREEN=1
   HAVE_VITA3K_SUPPORT=1` into `engine/build/vitagl/`, and the CMake file
   links it when present. A game wants no vitaGL logo on a console either, so
   one library serves both.
4. **The GPU path then stops at the SHADER COMPILER**: vitaGL loads
   `ur0:data/libshacccg.suprx`, Sony's runtime compiler from the PSM runtime,
   which only exists extracted from a console. Without it every fragment
   program is invalid and `omk_smoke` faults. **Nothing GL - the smoke test,
   the game, even SDL's own renderer on vitaGL - can be tested in Vita3K until
   a `libshacccg.suprx` is placed in its `ur0/data/`.** This repo does not
   fetch it; it has to come from the user's own console.

### 2026-09-18, with the user's `libshacccg.suprx`: the GPU and THE GAME in Vita3K

With the shader compiler in `fs/ur0/data/`:

* **our GLSL ES 1.00 compiles on the Vita's compiler** - the emulator log
  lists the scene program's parameters by name (`aPos`, `aUV`, `aCol`,
  `aPhase`, `uMvp`, `uTexSize`).
* **Vita3K's Vulkan renderer** (on MoltenVK) faults translating that program
  ("Mask not implemented" then an access violation in the emulator); its
  **OpenGL renderer runs it** - `VITA3K_ARGS="-B OpenGL"`.
* **`omk_smoke` draws Aapkayl**: 3419 triangles, 21 textures, the frame
  pillarboxed into 960x544 at 60 fps (the emulator's vsync). Geometry, depth
  and texturing are right by eye. **COLOUR IS NOT SETTLED**: dithered, the
  picture is GREEN (mean over a region 77/115/45 against 62/53/31 with the
  dither off - green more than doubles); `smoke.nodither` shows the right
  colours, and `smoke.bars` (the CPU-surface present) shows exact bars. The
  cause is not found. **Withdrawn, the same afternoon**: an in-app "present =
  readback" test reported EXACT at 1:1 and scaled - it was VACUOUS, both sides
  black, because **Vita3K never writes a render target back to emulated
  memory, so `glReadPixels` returns zeros**. The test now reports INPUT EMPTY
  on a black readback instead of a pass. Whether the tint is ours or the
  emulator's is the CONSOLE's to say: the dithered picture is on the play-test
  list.
* **THE GAME RUNS in the emulator** (`scripts/vita3k-run.sh game`, the street
  start in `fs/ux0/data/omk/args.txt`): the tables from `app0:`, the data from
  `ux0:`, `renderer: GLES2`, the audio device open at 44100 Hz stereo,
  `pad: PSVita Controller` (SDL's game controller, as designed), and ADVENTURE
  MODE on Anekbah's walk mesh with the crowd and the scene sounds - at ~3-4 s
  a frame (the JIT, not the console). **The picture is BLACK**, for the reason
  above: the game composes every frame on the CPU from a readback (G6 is still
  open), and in Vita3K a readback is zeros. **G6 is therefore also what makes
  the game VISIBLE in the emulator.**
* **Two faults a console would have had, both fixed:**
  1. `freopen` on stdout/stderr, which `vita_main.cpp` used for the log,
     **corrupts newlib's heap on the Vita** - the first game runs died inside
     `_malloc_r` a second in, on a free-list pointer of 0xbd20a0a1, with the
     same result at a 192 MB heap as at 300 (so not the heap size); and even
     where it did not crash, newlib kept writing the standard streams to the
     TTY, so the log file stayed EMPTY. `printf_c99.cpp` now routes stdout /
     stderr itself (`omk_vita_redirect`, unbuffered; `puts`/`putchar` wrapped
     too, since GCC turns `printf("text\n")` into them). After it: 0 invalid
     reads and a 195-line log.
  2. `%zu` (above).
* **Driving it**: `scripts/vita3k-run.sh` now kills with SIGKILL after a grace
  period - Vita3K ignores SIGTERM while a title runs, and six emulator windows
  had piled up. The smoke app takes mode files in `ux0:data/omk/`:
  `smoke.autoexit` (quit after 900 frames, so its log is flushed),
  `smoke.bars`, `smoke.worldbars`, `smoke.nodither`.

**So, for testing**: the emulator answers "does it boot, load, run the
scripts, open audio and the pad, and stay up" - which found two crashes the
console would have had - but not "is the picture right" or "is it fast
enough". Those stay the console's.

### 2026-09-18: THE START-UP CRASH - vitaGL and a uniform ARRAY

The game died at start on every path but the street start: newlib's
`_malloc_r` on a free-list pointer of **0xBD20A0A1**, the same value every run.
Found by bisection, not by guessing:

* the game's log in the emulator is written but not flushed to the host file
  before a kill; the emulator's own log records every `sceIoWrite` SIZE, and
  those sizes match the Mac log line for line - so the last line printed is
  readable even from an empty file;
* **heap checkpoints** (`OMK_HEAPCHECK`, Vita only, `mallinfo` walks the free
  lists) at each start-up banner of `play.cpp`: the heap was whole before the
  RENDERER section and corrupt before the MUSIC one - the GL set-up;
* the corrupt pointer, read as a float, is **-10/255**: a value of the
  SHIMMER table, which `GlesRenderer::init` uploaded as `uniform float
  uWave[32]` with `glUniform1fv(loc, 32, ...)`. **vitaGL (at the SDK's commit)
  sizes a uniform float ARRAY's storage short and writes the rest over the
  heap.** Now eight `vec4` uniforms and an index picked by arithmetic.

After it: the normal start, the save start and the street start all run with
0 invalid reads; the normal start reaches the start menu. **Rule for the GLES
backend: no uniform arrays.**

Also removed on the way, on the reader's point that the standard library's
OS layer is not to be trusted on the Vita: `std::filesystem` (DataFs's
directory walk, `create_directories`, `file_size` - `sceIo` calls on the Vita,
`makeDirectories` / `fileSize` in `datafs.h`) and the audio mixer's
`std::mutex` (SDL's mutex now). Neither was THE crash - the heap checkpoints
say so - but both sit on SDK glue the engine does not need. And
`-DOMK_VITA_ASSERTS=ON` builds with libstdc++'s bounds checks, for the next
overwrite of this kind (it found nothing here: the write was vitaGL's).

### 2026-09-18, evening: THE FIRST CONSOLE FRAME TIMES - and G6 step 1

`play.cpp` now logs every 60 frames a `frame N phases` line (sim+draw /
readback / compose / present), a `gles` line (glReadPixels / to-565 / texture
upload / present draw / swap), a `present` line (how many frames went straight
from the GPU, and which gate kept the others) and a `spans` line (named calls:
pump, session, music, screen draw, dialogue text).

**The console's intro cutscene, steady state, ~106 ms a frame:** the game
itself (sim + draw submission) **9 ms**; `glReadPixels` 36; the 888 -> 565
dither on the CPU 35; the CPU compose 10; the texture upload 16; the present
draw and swap <1. So ~90 of the 106 ms were the ROUND TRIP (item 4 of §1), not
the game - the device factor is not what sinks the port. The conversation adds
88 ms of CPU compose (its text box, unexplained yet). **The start menu costs
~775 ms a frame** with no readback at all; the `spans` line is there to say
where. Re-reading the menu's BMP and IAM text every frame
(`ScreenComposer::draw`, now cached per screen as `UI_LoadScreen` does) was a
real waste but NOT the cause: the menu did not change on the console.

**G6 step 1** (not yet seen on a console): the Vulkan window's "nothing drew
over the 3D" gate now serves the GLES window too, and such a frame is presented
by `GlesRenderer::presentWorld` (the dither on the GPU) with no readback, no
CPU dither and no upload. `OMK_VERIFY_GPU_PRESENT=1` on `omk-play-gles` reads
the window back and compares with the CPU frame IN 565: **0 pixels differ over
90 frames** of Anekbah's street (compared in 888 it reports ~94000 - the Mac
driver's own 565 -> 888 expansion, the trap recorded above).

**The films on the console:** all three found and opened, then "0 frames
shown, sound at 0 Hz": the loop tested `sceAvPlayerIsActive` FIRST, and a
console's player is not active until it has buffered (Vita3K's is at once). It
now waits up to 3 s for the start and logs why each film ended.

**Second console log (19:37), and what it changed:**

* **Step 1 presented 0 frames of the intro**: every frame of it carries the
  conversation or a `media line` subtitle, so it is CPU-composed until step 2
  (the 2D layer as an overlay) exists. Step 1 is right, and the intro cannot show it.
* **The menu: `screen draw` 358 ms.** The console's `omk.ini` has
  `uiscaling = linear`, and the 640x480 interface is scaled to 960x544 by
  `blt`, which did two `x * sw / dw` divisions and a floor per PIXEL. **The
  Cortex-A9 has no integer divide instruction**, so each is a library call.
  The column tables are now built once per blit (the same expressions, so the
  output is byte-identical: menu frames at 960x544 compared, linear and
  nearest). The linear path still does three double divisions a pixel.
  **Division by a CONSTANT is not this trap**: GCC turns `/ 255` into a
  multiply.
* **The films started and stopped with nothing decoded** ("the film ended",
  0 frames). The decoder's frame buffers came from a fresh CDRAM block, and
  vitaGL takes most of CDRAM at `vglInit`. They now come from vitaGL's pool
  (`vglMemalign`) first, and every allocation is logged. Not yet confirmed.

**G6 step 2 - the interface as an OVERLAY** (20:00, not yet seen on a console).
The second log also showed why step 1 could never fire in the intro: the
BLACK FADE is `running()` for the whole of it (its two bands), and so is a
subtitle or the conversation. So on the GLES window a frame held only by a
SOFT gate - either screen fade, a media bitmap, a media line, a conversation -
is no longer composed over a readback: the world's rows of `fb` hold a KEY
(`kOverlayKey`), opaque drawing overwrites it, and the passes that READ the
picture (the two dialogue boxes, the two fades) are all affine in it, so on a
key pixel they run on two side planes instead (`g_ov`: the law on C from 0, its
factor on M from 255). `GlesRenderer::presentOverlay` draws `C + world * M` in
one blend (GL_ONE, GL_SRC_ALPHA), C as 565 and M as an 8-bit texture, and
uploads either ONLY WHEN IT CHANGED. Measured on the Mac through the intro
(`OMK_GLES_WINDUMP` against `--dump`, compared in 565): under the black fade's
bands **0 pixels differ**; with the conversation box up, 20849 differ and
**none by more than one 565 level** (the box's multiply rounds on 8 bits on the
GPU and truncates on 565 on the CPU). `OMK_NO_OVERLAY=1` turns it off. The HARD
gates stay on the CPU: an open screen, the CPU mirror, the shoot / fight HUDs
and the breath gauge (not yet read for whether they read the picture).

**G6 step 3 - the GAUGES on the overlay** (2026-09-21, not yet seen on a
console). The fight HUD and the breath gauge were HARD gates, so every frame of
a melee and every frame underwater still paid the readback. Both are
`Hud_DrawBar`, and its only reads of the picture are `fillQuadD3d`'s three
blends - alpha (`src*(1-a) + dst*a`), multiply (`dst*(1-src)`, always the grey
0xE0E0E0 in the HUD, so ONE factor plane is enough) and additive - all affine,
so on a key pixel they now run on the planes like the dialogue boxes do. The
planes moved out of `play.cpp` into `src/ui/overlay.h` so `hudbar.cpp` can
reach them. The two gates moved BELOW the hard ones: a soft gate that answers
first would hide `dump` and `instrument`. `OMK_GPU_PRESENT_STATS` now names the
soft gate behind an overlay frame (`overlay (fight hud)`). Measured on the Mac,
`OMK_GLES_WINDUMP` against `OMK_NO_OVERLAY=1 --dump`, in 565:
`--fight-supermarket` at frames 470 and 500 (gauges and the stat card up):
**230 and 197 pixels of 480000 differ, none by more than one 565 level**, all
inside the gauges' rows; the canal dive (`engine: swim`'s own run) at frame
350: **0 differ**. Host `make`, `make play`, `make play-gles` and `make vita`
build.

**G6 step 4 - the SHOOT HUD and the OPEN SCREENS** (2026-09-21, not yet seen
on a console). The census first, and it removed the worry step 3 ended on:
`ScreenComposer` calls `Surface`'s mode-2 quad only as `quadMode(4)` and
`quadMode(0xC)`, which are both MODE 1, the 50% blend - **nothing draws the
saturating SUBTRACT at all**, and no composer blit is destination-keyed. So the
composer has two readers, its own alpha fill (`Ui_DrawPanelDim` among its
callers) and the 50% quad, and both now run on the planes; the add does too,
the subtract is left alone and says so. The turning models, the crosshair, the
radar's dots and the text write opaque pixels.

* **The shoot HUD is a soft gate.** `--area 230 --scene-chunk 56` at frame 400
  against the CPU compose: 14581 pixels differ, **249 by more than one level,
  every one inside the ring's and the weapon's boxes** - the two turning
  models spin on `SDL_GetTicks`, and two CPU runs of the same command differ
  there too (911 pixels).
* **An open screen is a soft gate, with three HARD exceptions**, each a reader
  the planes cannot carry: the VIEWPORT item (the world goes into the screen),
  **SAVE GAME** (screen 30: the slot's thumbnail is taken from `fb` itself) and
  a panel with the monitors' **INTERFERENCE** (`0x00477ED0`: row shifts and an
  OR mask). The pharmacy's shop (`--area 39`, four ENTERs, frame 330), whose
  panel dims the whole world: outside the spinning preview, 413108 pixels
  differ by ONE level and 517 by two, none by more - the dim's `* 40 / 255`
  truncates on 565 on the CPU and rounds on 8 bits on the GPU, the same
  one-level law as the conversation box, over the whole picture.

What still reads back on the GLES window after this: those three screens, the
CPU mirror, the instruments and a `--dump`'s last frame. **G6 is done as far as
the Mac can show it; what it is worth is a console number.**

### 2026-09-21: THE CITY'S LOAD - the 16 MB the heap refused was the MUSIC

The console dies (the reader: *freezes*) when a city loads, of `bad_alloc` with
~106 MB of the 192 free - one request, not the sum. Measured on the Mac with an
interposed `operator new` logging every request of 2 MB or more through
`--area 0`: **the four largest were all `MusicPlayer::play`** - `adpcmDecode`'s
`push_back` growing 4, 8, then 16 MB in one piece, and then a 16 MB COPY of the
finished track (`std::move` of a `const` vector copies). So a city's
three-minute track asked for 16 MB contiguous twice, with 8 + 16 and then
16 + 16 MB live, on a heap the load has just fragmented.

A stereo `.ADP` has no header and no blocks - one byte is one frame - so the
player now keeps the FILE'S 4 MB and decodes forward as `pull` advances
(`AdpcmStereoStream`, `formats/adpcm.h`), restarting the decoder on the loop's
wrap; it also stops decoding three minutes of audio inside the load, which on
an A9 is a stall of its own. `build/music_equiv` (the old player kept verbatim)
over tracks 2, 5 and 12, looped and not: **0 mismatches in 82 million
samples**. `adpcmDecode` reserves its output for its other callers.

After it the load's largest requests are `buildGeometry`'s corner list (2.2,
4.4, 8.7 MB - doubling, per bucket), the GPU light list (6.7 MB) and the
track's 4 MB. **Not confirmed on a console**: the next log's
`new: N bytes REFUSED` line names the size if something still fails - 8745984
would be the corner list.

### 2026-09-21: PRECOMPILED SHADERS - the machinery is in, THE CACHE ITSELF IS NOT YET MADE

There is no offline compiler a homebrew may ship, so "precompiled" means
vitaGL's own **shader cache**, made once where the compiler is and carried in
the VPK:

* **vitaGL is built with `HAVE_SHADER_CACHE=1`** (`scripts/vita-vitagl.sh`):
  a compiled shader is kept as `ux0:data/shader_cache/OMKE00001/v<N>/{v,f}/<XXH3
  of its source>.gxp`. GLSL is compiled at LINK time here (`VGL_MODE_POSTPONED`
  is the default), and `glLinkProgram` looks in the cache first.
* **One patch, `scripts/vita-vitagl-patch.py`**, because at the pinned commit a
  full cache STILL needed the compiler: `glCompileShader` starts
  `libshacccg.suprx` before it looks at anything and gives up when it is
  missing. Patched so the start is not fatal and only a cache MISS with no
  compiler refuses (in `glCompileShader` and in `glLinkProgram`'s postponed
  compile, which before would go on into SceGxm with a NULL program - the
  2026-09-18 crash dump). Three asserted anchors; the script re-applies it on a
  clean checkout.
* **The VPK carries `engine/backends/vita/shader_cache/**.gxp`** as
  `app0:shader_cache/` (CMake logs the count - **0 today**), and `vita_main`
  copies what is missing into `ux0:data/shader_cache` at start (`app0:` is
  read-only and vitaGL writes its cache) and logs `shaders: N precompiled
  file(s) copied ...; the runtime compiler is present/ABSENT`. A missing
  compiler is no longer FATAL at start.
* **All three GLES programs link at start** - the overlay's was linked on first
  use, so a short cache-making run could have missed it.
* `scripts/vita-shader-cache.sh [folder]` collects the `.gxp` from the Vita3K
  storage or from a copy of a console's `ux0:data/shader_cache`.

**What is missing is the run that writes the cache**: this machine's Vita3K
storage has no `libshacccg.suprx` any more (`engine/build/vita3k/fs/ur0/data`
is empty), so nothing here can compile. Either put the module back and run
`scripts/vita3k-run.sh game 60`, or run the new VPK once on the console and
copy `ux0:data/shader_cache` off it IN BINARY MODE, then
`scripts/vita-shader-cache.sh <that folder> && make vita`. **Untested end to
end**: that a console with the cache and WITHOUT the module starts is the claim
to check, by renaming the module once the cache is in.

vitaGL's own fixed-function shaders have a cache of their own
(`ux0:data/shader_cache/v<M>/`); the collect script takes every `.gxp` under
the folder, so they travel too if anything made them.

### 2026-09-21, the console log of 21:15: THE CITY LOADS - and a frame of it takes 8.4 SECONDS

The music fix held: no `REFUSED` line, no `bad_alloc`, `world: slot 1 set
ANEKBAH (AREA 0) - 139245 corners, 21 batches` and the area shown at frame
3326. The intro before it runs at 17-54 ms of sim+draw a frame with every
held frame on the overlay (G6 works on hardware; 30 of 60 frames straight from
the GPU once the fades end). Then **seven frames in a row of 8343-10453 ms**,
which a player reads as a freeze, and nothing in the log to split them.

**The suspect, read out of vitaGL and not yet confirmed on the console:**
`glNamedBufferSubData` (buffers.c), for any buffer drawn within
`FRAME_PURGE_FREQ` frames, **allocates a new buffer of the WHOLE size, copies
the old one into it and frees the old one some frames later** - the same trap
as its `glTexSubImage2D`. The depth tie (`resolveTies`) patches three vertices
at a time, before each batch's draw; the draw marks the buffer used again; so
Anekbah's 5 MB set buffer is copied, and 5 MB allocated, up to 21 times a
frame, with the frees deferred. The partial (dirty-corner) upload has the same
shape. Both now go through `patchArrayBuffer`, which on the Vita writes IN
PLACE through `glMapBuffer` (vitaGL hands out the buffer's own memory): a tie
patch touches only its own batch's range, not yet submitted this frame.
A whole-buffer rewrite (a posed body) still takes vitaGL's copy path, which
for `offset 0, size all` is one allocation and one copy of the new data.

**And the log can now say**: a frame over 500 ms prints
`frame N: of which sim+draw X ms; gles frame: D draws .. ms, U uploads (KB) ..
ms, ties .. ms, P buffer patches`. If the next log still shows seconds, that
line splits CPU from GL, and uploads from ties from draws. `OMK_NO_TIE=1` in
`ux0:data/omk/args.txt`... is an environment variable and cannot be set there;
the tie's switch on a console is a rebuild.

**The shader cache came back from the console** (5 files: three fragment, two
vertex - the three programs) and is committed under
`engine/backends/vita/shader_cache/`; the VPK carries it. The no-compiler start
is still to be tried: rename `ur0:data/libshacccg.suprx` and launch.

### 2026-09-22, the console log of 08:15: 8.4 s -> 200 ms, and the SHAKE was the port's clamp

The in-place buffer patches took Anekbah's frame from 8.4 s to **190-300 ms**
(`sim+draw 166-241` on the `phases` lines), and the GL side is nothing:
`gles frame: 16 draws 0 ms, 3 uploads (139 KB) 1 ms, ties 1 ms, 2 buffer
patches`, `swap 0.2`. So what is left is CPU work in the frame, and the named
spans cover 6 ms of it (`session 2.8, music 2.5`). The reader: *"better, but
still very laggy and the camera is shaking"*.

* **The shake is the port's delta clamp, and the fix is the engine's rule.**
  `play.cpp` took a frame over 0.25 s as a hitch and fed the game 1/30 for it;
  the console's frames straddle 0.25 s, so six frames of motion alternated with
  one. `Game_Frame` caps `30 / fps` at **3.0** (docs/BOOT.md 4): below 10 fps
  the game slows down and every step is at most three frames. Ported as such.
* **SECTION MARKS** down the frame (`mark(...)`, 16 of them): a frame over
  `OMK_MARKS_MS` (150) prints its five largest sections, so the next console
  log says which block of `play.cpp` holds the 190 ms. On the M1 a street
  frame is 2 ms top to last, so nothing to attribute here.

### 2026-09-22, the console log of 08:44: the frame's SECTIONS - and the body tie made rigid

The marks named 108-129 ms of a 209-225 ms frame: **staged bodies 46,
pedestrians and traffic 35, scripted motion 17**, audio/controller/lights
4-15 each; the rest sat after the last mark, so five more marks went in
(the world submission, the screens, the fades, the present) for the next log.
The same street on the M1 under the software renderer: staged bodies 1 ms,
pedestrians 1 ms, scripted motion < 0.5 ms - a **uniform ~40x**, which is the
A9 against the M1 and not a port-side anomaly in any one section. So the
frame is cut by removing CPU work, not by finding a bug, and `handoff-vita.md`
§2's order stands. Its first item is done:

**THE BODY TIE IS REPLAYED, NOT RE-WALKED** (`Geometry::tieClass`,
`tieRigidFrom`; `engine: body tie`). A posed body got a new revision every
frame, so `DepthTie` re-keyed every face of every body every frame - ~1.0 ms
of the M1's 6.0, ~40 ms of the console's 200. A body is skinned RIGIDLY
(`applyPose`: one transform per mesh, the same arithmetic for every corner of
it), so two corners of one mesh coincide in every pose or in none, and two of
different meshes only by chance or where a seam closes for a frame. Keyed by
position AND class (the mesh; the corner's own vertex where the face is
morphed, since two face vertices meet only when the mouth closes) the walk's
answer is pose-invariant, and the next revision REPLAYS it through the same
machinery step 8 built for the set's moved cargo, with no moved units at all.
`engine/tools/body_tie` holds it to the original per-frame pass over four
models and 240 rigid poses: **0 mismatches, 0 cross-mesh ties, one walk per
class change (HO1_FNM: 5 walks, 235 replays)**, the tie's cost per body per
frame 0.12 ms -> 0.0004 on the M1. `engine: tie equivalence` and `pose
equivalence` unchanged (the latter re-baselined for `sizeof(Geometry)` 192 ->
224). Not yet seen on a console.

Two Mac notes: the M1's GL window BLOCKS in `SwapWindow` while the display is
off (`pmset -g log`: "Display is turned off" at 09:00), so no GLES run or
profile is possible on a machine nobody is in front of - the software renderer
under `SDL_VIDEODRIVER=dummy` is what runs; and the section marks print only
on a PACED run (no `--frames`).

**A staged body beyond the clip distance is not skinned** (same day). The
staged loop posed every extra every frame - Anekbah's 25, most of them
hundreds of metres from the camera - where the engine's visible set is the
clip distance around the camera and it skins only what it draws. The program,
placement, facing and nodes are still computed; the skinning, the corner
transform, the lights and the upload are skipped for a body whose last drawn
position is past `clipInches` plus its root radius - unless its FEET latch
(`seatFeet`, taken once per clip from the posed corners) is not yet taken for
the current clip, in which case it is skinned once. At the default 200 m clip
the street start holds 9 of 25 beyond it; a 700-frame software render is
BYTE-IDENTICAL with and without the skip (`OMK_SKIN_FAR=1` skins all, for the
A/B), `engine: street frame` and `city crowd` green. And a walker hold (skip a
walker whose clip frame and place did not change) was written, measured and
REMOVED: on a street every walker's body point, heading or foot height moves
every frame, even at an action point, so it never fired.

**P2 applied** (same day): `MeshNameIndex` is the consumer of every shadow-bone
and crowd-feet name lookup in `play.cpp` (`pending/vita-meshidx-playcpp.md`,
now marked integrated); the four checks green and unchanged, the mutation red.

**ONE `cos`/`sin` A BODY, NOT TWO A CORNER** (2026-09-22). `rotateYaw` takes an
angle in DEGREES and computes its cosine and sine every call, and the three
per-corner loops that turn a body to its heading called it per corner - twice
for a walker (the position and the normal). Anekbah's street: 17 walkers x 446
corners x 2, plus the staged bodies and the vehicles, is ~15000 `sincos` a
frame, and `__sincosf_stret` + `rotateYaw` were 67 of ~600 engine samples on
the M1. `yawSinCos` + `rotateYawCS` (`actor/player.h`) take the pair already
computed and `rotateYaw` is now those two calls, so the arithmetic is the same
expression on the same values - **bit-exact, and shown so**: Anekbah at frame
900 and Jaunpur (address 4, density 4, the vehicles drawn) are BYTE-IDENTICAL
before and after. The M1's street sections: pedestrians 1.0 -> 0.8 ms, staged
bodies 0.4 -> 0.3.

The marks also print to a TENTH of a millisecond now: at `%.0f` a section
costing 0.4 ms printed `0 ms`, which is what made the scripted-motion split
unreadable on the Mac.

**The GLES backend's scratch buffers are kept** (same day): `uploadGeometry`
built a fresh `std::vector<GpuVert>` on every call and `resolveTies` a fresh
pair of loser lists on every draw - so a street frame allocated and freed
~16 KB per posed body plus two vectors per draw, on a device whose allocator is
not the M1's. They are members now, cleared rather than rebuilt. Proved with
`build/gles_probe`, which makes its own CGL context and so runs with the
display off: its whole report is IDENTICAL before and after (world and surface
presents EXACT, coverage 0.9977, the letterbox row), and `engine: gles backend`
is green.

**A note for whoever profiles next**: on a Mac with the DISPLAY OFF the GL
window blocks in `SDL_GL_SwapWindow` and Vita3K never starts, so `omk-play-gles`
and the emulator cannot be run at all; `gles_probe` and the software renderer
under `SDL_VIDEODRIVER=dummy` are what is left.

**THE PER-BODY SPANS, and what they said** (same day). The two body sections
named no call inside them, so `ped skin`, `ped place`, `ped light`, `staged
skin` and `staged place` are accumulated per body and printed in the `spans`
line every 60 frames. On the M1's street: **`ped light` 0.4 ms, `ped skin` 0.2,
`staged skin` 0.1**, the two `place` sums below 0.05 - so the crowd's LIGHT is
the largest single thing in either section, and at the ~40x ratio it is ~16 ms
of the console's 35. Replacing its three `lightRamp` lookups with constants
takes it 0.4 -> 0.3, so a quarter of it is the ramp and the rest is the walk.

**The crowd's light is 4x cheaper, and every step is byte-identical.** Split
finer, `ped skin` is compose 0.1 + apply 0.1, so the light at 0.4 was four
times either. Three changes took it to **0.1 ms**:

1. the corners walked ONCE (below) - no measurable change on the M1;
2. **no branch on the sign of `t`** (0.4 -> 0.2, and this was the one that
   paid): a corner faces away from about half the lights that reach it, so the
   `t > 0` test mispredicted about half the time. `lightRamp` clamps a
   non-positive `t` to zero and so returns zero, and adding zero to a colour
   already in [0, 1] leaves it and its clamp alone - so the test only ever
   skipped work that would have changed nothing;
3. **the ramp tabulated per light COLOUR** (0.2 -> 0.1): `(t * c) >> 8 / 255`
   depends only on the colour and the 0..255 index, and a set's lights carry a
   handful of colours, so it is 256 x rgb floats built once per colour and kept
   (capped at 64 tables). The inner loop is now a dot product, a clamp and
   three loads.

`applyLights` also **walks the corners ONCE, not once per light**: the reaching
lights are gathered first and each corner takes them in the same order, so
every corner's colour is the same sequence of additions and clamps - the street
render is BYTE-IDENTICAL, and `engine vertex light`, `light consumers`, `city
crowd`, `street frame`, `per-pixel lighting` and `crowd nan` are green, and the mutation
(the ramp's scale) turns `engine vertex light` red. **That first step changes
nothing measurable on the M1** (0.4 ms either way): a body's 21 KB of corners
fits this machine's cache. The reason to keep it is the A9's - five times less
traffic through a 32 KB L1 - and that part is unproven until a console log
shows the span; steps 2 and 3 are measured here.

**And a trap that cost twenty minutes: `engine vertex light` RAN A BINARY IT
DID NOT BUILD.** It executes `build/vlight_probe` and never made it, so a
mutation stayed red through its own restore and read as a broken fix. It
builds the probe now, which is CLAUDE.md 1's rule about exactly this.

### 2026-09-22: P4, THE CROWD POSED OVER SEVERAL CORES - and it is the same frame

The reader's own point, and it was the right one: the pool's VITA half needs a
device, but the DECOMPOSITION does not - a desktop `omk::Threads` proves it
here. The walker pass is now three:

* a **serial** half that resolves the shared caches - `charModelFor` loads a
  model, `pedTracksFor` binds a clip's tracks, `lodRestFor` cuts and keeps a
  rest geometry - and counts the crowd, recording one `PedJob` a drawn walker;
* the **body** pass, `composePose` / `applyPose` / the turn / the feet / the
  light, which writes only its own walker's `PedStaged` and reads everything
  else, so `parallelFor`'s disjoint chunks hold;
* a **merge** in index order: the geometry REVISIONS, the counters, and the
  per-body times. The revisions matter - assigned inside the pass they race,
  and a backend keys its vertex cache on them.

The per-body spans had to move with it: `spanned` writes a shared map, so each
job times itself and the merge sums them. `ped bodies (wall)` is the elapsed
time of the pass and the others are now sums ACROSS runners, which is why they
rise when it is threaded while the wall time falls.

**Anekbah at density 4, 200 frames: the framebuffer is byte-identical with and
without `--thread-bodies`**, and the wall time of the pass is **0.4 ms -> 0.2
on the M1** (8 runners, 17 drawn walkers). `engine: threaded bodies` pins it,
and is shown to fail by dropping the `thread_local` from `applyLights`'
scratch - the threaded run then dies rather than drawing differently, so the
check reports a run that did not finish as itself.

**OFF by default** (`--thread-bodies` / `--no-thread-bodies`), because the
pool's Vita half has still never run on a device: the console turns it on in
`args.txt` once `omk_bench` says `threads: EXACT` there. **The STAGED bodies
are not threaded** - that loop queries the scene runner, logs, and calls back
into the Session, so it is not a disjoint-chunk shape without more work.

**The STAGED section, apportioned** (same day, after P4). It is the larger of
the two on the console (46 ms against 35) and it is NOT mostly skinning: on the
M1 `staged resolve` 0.1 (the pose source, the scene runner's queries, the clip,
the look-at, the ground probe, `composePose`), `staged skin` 0.1, the mesh
placement about 0.1, `staged place` below 0.05. So threading it the way the
walkers were threaded would reach only a third of it, and its serial half is
the part that queries the Session - which is why it is still serial.

Two things were fixed in it anyway, both BYTE-IDENTICAL in both cities and
both **below what this machine can measure**, so they are kept on the reading
and not on a number:

* **the three ground probes go through the GRID.** `floorUnder(playerSoup, ...)`
  with no grid is a linear scan of the whole city's walkable soup - 15137
  triangles in Anekbah - and the rest of the frame already used the grid form,
  whose header contract is "the same answers, visiting only the candidates"
  (and `OMK_VERIFY_SPLIT` compares the two every moving frame). It shows
  nothing here because most city extras are pelvis-anchored and never reach
  that arm; a scene of floor-anchored bodies is where it counts.
* **one `cos`/`sin` a body in the MESH loop.** It turns every mesh's origin and
  each of its three axes, so a crowd model's 76 meshes cost four apiece - ~4900
  a frame across the staged bodies. The two angles it ever uses are the body's
  and zero, and `cos`/`sin` of zero are exactly 1 and 0. On the M1 that is
  ~0.05 ms; on an A9, where `sincos` is a library call rather than an
  instruction, it is worth several times that.

### 2026-09-22, the console log of 20:14: THE SHADER CACHE WORKS, and the frame is fully named

**`libshacccg.suprx` is ABSENT and the game runs.** The log says so in as many
words, so the precompiled-shader work is CONFIRMED on hardware and a player
needs no extracted compiler.

**And `ped light` is 0.0 ms.** The crowd's per-vertex light - the biggest
single item in either body section, and the one change of the day that the M1
could measure - costs nothing at all on the console now. `readback 0.0,
compose 0.0` and **58-60 of 60 frames straight from the GPU**, so G6 holds on
hardware too.

The frame is 174-191 ms and every millisecond of it is now named (the pacer's
own 174 and the marks' 174.3 agree, so the instrument is not inflating it):

| section | ms |
|---|---|
| world begin..end (submit, GL) | 44 |
| **fades, flicker** | **42** |
| pedestrians, traffic | 26 |
| staged bodies | 17 |
| present, swap | 16.5 |
| scripted motion (meshes 9, grids 7) | 16 |
| lights | 5 |
| audio / controller, intermittently | 14-17 |

and inside the two body sections: `staged skin` 10-11, `ped apply` 5-11,
`staged resolve` 5.5, `ped compose` 0.6-1.5, `ped place` 0.9-2.2, **`ped light`
0.0**. So the SKINNING is now the top per-body cost, where the light was.

**The 42 ms is the thing nobody expected**, because on the M1 that span is
0.1 ms and every loop in it - the colour fade, the black fade's bands, the
flicker ring - is gated and none of them was running. What is NOT gated is
`std::getenv`: the frame loop made **25 uncached calls a frame**, six of them
for `OMK_NOUI`, one per staged BODY for `OMK_BODYLOG`, and three for
`OMK_CLIPLOG` - one of those inside the per-frame present gate. `getenv` walks
the environment comparing strings, and this is the third time the Vita's C
library has cost this port real time (`%zu`, the missing integer divide).

`omk::envSet` (`src/app/playhelpers.h`) looks a name up once and keeps it,
scanned by the literal's own POINTER first. **22 call sites converted**; the
three left take the value and already sit inside `static` initialisers. The
golden record is identical, all four targets build. **Whether it is the 42 ms
is UNPROVEN** - the M1 cannot see it - so `fps counter` and `the fades` are
now marks of their own, and the next log either shows the span collapsed or
says which third of it is left.

**AND THE CITY IS WORSE THAN THE REST, which the reader said and the log
shows.** Split the 174 ms frame by what depends on the city:

* **city-dependent, ~108 ms**: the GL submit 44, pedestrians 26, staged bodies
  17, scripted motion 16, lights 5. Fewer bodies, fewer moving meshes and less
  geometry indoors, so all five fall away there;
* **everywhere, ~58 ms**: `fades, flicker` 42 and `present, swap` 16.5.

So `envSet` can only ever have been half the story, and the city half is the
bigger one. The `gles frame` line names it - **on the worst frames** (it only
prints over 500 ms, so these are 1.0-1.5 s frames and not the typical 174):

    278 draws 4 ms, 32 uploads (6220 KB) 25 ms, ties 59 ms, 431 buffer patches

**The DEPTH TIE is the largest GL-side item, and the reason is a interaction
this port created.** `uploadGeometry` rewrites a posed body's whole buffer
every frame and calls `tie_[g].vboReplaced()`, which clears the applied set -
so every loser that upload just overwrote has to be written back, one
`glMapBuffer`/`glUnmapBuffer` pair each. Forty bodies of ten losers is the
431 patches, every frame. The rigid replay of 2026-09-22 stopped the tie
WALKING per frame; it did not stop it PATCHING, because the upload keeps
wiping what it wrote.

**The fix is to fold the degeneration into the upload**: for a rigid body the
losers are the same as last frame's - that is exactly what the replay
establishes - so `uploadGeometry` can emit those triangles degenerate as it
builds the vertex array, and `resolveTies` then finds nothing to patch. Not
written yet, and it needs its own byte-identical proof.

And **6220 KB of vertex uploads a frame** is the other half: every posed body
re-uploaded whole. That is P5's territory (GPU skinning) and P3's (skip a body
whose pose did not change).

### 2026-09-22, later: THE TIE PATCHES, fixed - and a false claim of mine, withdrawn

**A CORRECTION FIRST.** The entry that stood here said *"the rigid replay of
2026-09-22 is not firing in the viewer at all"*, and commit `92595dd` says the
same. **That was wrong.** The debug print behind it was capped at its first
dozen lines, and those were all FIRST sightings of each body - where there is
no previous walk to replay, so `replays 0` is simply correct. Printed only on
later frames, every posed body reads `REPLAYED 1`. The replay works; the
conclusion was an artefact of the instrument. Recorded here because it is
exactly the CLAUDE.md 1 lesson with the sign flipped: a capped log is a sample
of the START of a run, and it said so about nothing past it.

**What the patches really were.** A windowed split of the tie's buffer
patches by caller, on Anekbah's street at density 4: **~150 a frame, all on the
SET, none on the bodies.** The set's revision bumps every frame because its
cargo moves; that sends it down the dirty-REPLAY path, which reports each
call's WHOLE loser set - and `resolveTies` wrote every one back, one
`glMapBuffer`/`glUnmapBuffer` pair each, although the dirty upload had never
touched those triangles and they were already degenerate.

**The fix keeps `losers`' contract and adds a delta beside it:**

* `DepthTie::newlyApplied()` - the triangles the last `resolve` NEWLY marked;
* the GLES backend keeps its buffer equal to the tie's applied set on every
  upload - a same-size full rewrite FOLDS the applied losers in instead of
  `vboReplaced()`, and a dirty upload does the same over its runs, WIDENED TO
  WHOLE TRIANGLES so a run cutting through a degenerate one cannot leave two of
  its corners at the old first-corner position;
* `resolveTies` writes only `newlyApplied()` and the restores.

**Measured**: the tie's patches on the street **~150 -> 0 a frame** (8 in a
window where a loser genuinely changed), all patches 219 -> 69, and the frame
**byte-identical** through the real GLES backend over 400 frames at density 4.

**And proved where the street cannot.** Neither fold is exercised in Anekbah:
its moving meshes share no triangle with its losers, and its bodies (cut to
one LOD skeleton) carry none. So the proof is in simulation, on both tools'
own vertex buffers held to the tie's expectation every draw:

* `engine/tools/body_tie` - posed bodies, where a coincident pair moves
  RIGIDLY and stays tied: **0 bad triangles, 3 writes against 720** for
  PSH_FN and FSH_FN over 240 poses. **Without the full-rewrite fold: 717 bad**
  - the fold is necessary and correct, which is the test the street could not
  give;
* `engine/tools/tie_equiv` - the set, with ties made and broken on purpose
  through dirty corners: 0 bad triangles, 18621 writes against 27260 on
  Anekbah. (Its moves never carry a tie along, so its fold is not exercised
  there either - which `body_tie` is for.)

`engine: body tie` and `engine: tie equivalence` assert both. **Only the GLES
backend has the delta**; the Vulkan backend still writes every loser, which is
correct and only slower.

**Threaded bodies are the DEFAULT since 2026-09-23.** The one reason they
started off - a Vita half that had never executed - is gone with the console's
`threads: EXACT`, and they are not an enhancement in the off-by-default sense:
the frame is bit-identical either way. `--no-thread-bodies` puts the crowd back
on one core for an A/B; `engine: threaded bodies` now compares that against
the default. The golden record, captured single-threaded, matches all six
scenes with threads on.

---

## 1. The issues, and what is missing

Ordered by how much they can sink the port, not by how much work they are.

1. **The device factor is unknown** (`handoff-vita.md` §1). Everything below
   is sized against it. The bench now exists; it needs one device run.
2. ~~**`play.cpp` has no Vita entry point**~~ - **DONE 2026-09-18** (F1, F4:
   `omk_vita.vpk` builds). What stays true: it is the game loop AND the SDL
   frontend AND the CLI harness in ~20300 lines (`todo/play-split.md`). It has two
   window modes (SDL_Renderer upload; Vulkan direct) chosen by
   `#if defined(OMK_VULKAN)` blocks, and the GLES path has to be a third
   (F1). Only ~120 SDL calls, so it is an `#if`, not a rewrite.
3. ~~**Input: no joystick, and its axis arm unread**~~ - **DONE 2026-09-18**
   (F2, F3): `Input_Poll`'s axes and keyboard fixes ported and checked, the
   pad in through `src/input/pad.h`. Open: the button layout and the dead
   zone are choices waiting for a play test.
4. **A frame with an interface on it round-trips through the CPU**
   (`handoff-vita.md` §5): `readback()` → CPU compose → `presentSurface`. On a
   Vita `glReadPixels` is a full pipeline stall every such frame, and that is
   every frame with the HUD, a subtitle, a fade or a screen. This is probably
   the largest GPU-side cost the port has and no desktop measurement shows it.
5. **The posed bodies re-upload every frame** (16 MB of posed geometry;
   `uploadGeometry` rewrites a body's whole buffer each pose). On vitaGL,
   `glBufferSubData` on a buffer the GPU may still be reading is the open
   question (G3). The GLES vertex is already cut to 36 bytes (no normal), 40%
   less than Vulkan's.
6. **The depth tie costs ~0.8 ms of CPU a frame on the M1** and exists
   because Vulkan's float depth compare differs from the engine's strict test
   on a quantised z-buffer. The GLES target uses a **16-bit** depth
   renderbuffer — the engine's own depth width — which may reproduce the
   engine's tie without the walk, or may make it worse. It must be looked at,
   not guessed (G4; `smoke_main` has the toggle).
7. **`std::filesystem`** in `platform/datafs.cpp`, `script/savefile.cpp` and
   `script/area.cpp`. VitaSDK's libstdc++ support for it is unverified; the
   fallback is a small POSIX (`opendir`/`stat`) shim behind DataFs. The first
   SDK build answers it (B2).
8. **Memory**: 137 MB live on the M1 with the music held whole (16 MB),
   posed geometries (16 MB), and texture pixels kept on the CPU (12.6 MB)
   which a GPU backend also copies. Comfortable against a homebrew's ~365 MB,
   but vitaGL's own copies and CDRAM (128 MB) are not in that number.
9. **Load hitches** (`handoff-vita.md` §5: 69-78 ms frames on a set load and a
   music switch, on the M1). A memory card reads far slower than an SSD. Not
   a mean-cost problem, and on a Vita likely the worst visible one.
10. **The movies**: `pl_mpeg` decodes the FLIS intro on the CPU. Unmeasured on
    an A9.
11. **Shaders on vitaGL**: GLSL ES 1.00 is translated at run time, which needs
    `ur0:data/libshacccg.suprx` extracted on the device (the standard
    requirement for vitaGL ports, and a user-facing install step). Shipping
    precompiled `.gxp` removes it later.

**Two findings for OTHER owners, found on the way:**

* **The Vulkan shimmer is per FRAGMENT, the reference per VERTEX.**
  `raster.cpp` adds `shimmerOffset(phase, clock)` to each corner before
  interpolation — the engine's own per-vertex loop — while
  `backends/vulkan/shaders/scene.frag` interpolates `vPhase` and looks the
  wave up per pixel, which differs wherever a triangle's corners carry
  different phases (the phase comes from the vertex address, so they usually
  do). The GLES backend follows the reference. Whoever holds `vkrender` should
  decide whether Vulkan should too — it would also be cheaper.
* **`expand565Rgba` and `present.frag` expand blue with `b >> 3`**, red with
  `r >> 2`. Replication for a 5-bit channel is `>> 2`. Harmless for anything
  compared in 565 (it truncates back to the same value), wrong if anything is
  ever compared in 888. Recorded, not changed.

---

## 2. The plan

Each step ends in something a person can check. **B** = build, **G** =
graphics, **F** = frontend/`play.cpp`, **P** = performance, **M** = memory.
Steps marked ✎ edit an existing file and wait until its owner is free.

### Phase 1 — measure on the device (no `play.cpp`)

* **B1** ✓ DONE 2026-09-18 — Install VitaSDK (`vdpm vitaGL sdl2`), `cmake -S engine/backends/vita
  -B build-vita`, build `omk_bench`. Fix whatever the first compile of
  `src/*/*.cpp` under arm-vita-eabi says; each fix is a portable change.
* **B2** ✓ COMPILES (behaviour on `ux0:` unknown) — `std::filesystem` on the SDK: `fs_selftest` built for the device,
  or the POSIX shim ✎ `platform/datafs.cpp` if it fails.
* **P1** — Run `omk_bench` on the device at 444 MHz. **Divide by the M1's
  1.311 ms.** That ratio replaces `handoff-vita.md` §1's missing-number
  paragraph and decides how much of P3-P6 is needed. Also check the threaded
  line: 3 runners, EXACT hash, the speedup; if `threads: DIFFERENT`, the Vita
  half of `threads.cpp` is wrong, not the posing.
* **G3** — Build and run `omk_smoke`: vitaGL takes the shaders, the 565
  upload, the 16-bit depth renderbuffer; frame time and CPU submit time over
  300 frames. First GPU number for the port.
* **G4** — In `omk_smoke`, look at the depth tie both ways (SQUARE) on a
  set with coincident faces — Anekbah's shop signs, CLAUDE.md §6. Decide per
  measurement: tie off on GLES, tie on, or `OES_depth24` + tie.

### Phase 2 — the game boots on the Vita

* **F1** ✓ DONE 2026-09-18 (the GLES window mode; the world is still read back - G6) ✎ `play.cpp`: an `OMK_GLES` window mode beside `OMK_VULKAN`: SDL GL
  window + context (SDL2's Vita video driver on vitaGL, or `vglInit` directly
  if SDL2-vita's GL path does not coexist), `makeGlesRenderer`, and the two
  present calls in the two places `vulkanPresent*` are called
  (`play.cpp:1344`, `3436`, `20018`). The declarations are in
  `glesrender.cpp`'s tail; declare them the way the Vulkan ones are declared
  (`play.cpp:111`).
* **F2** ✓ DONE 2026-09-18 (`src/input/pad.h`; `vitapad.h` superseded) ✎ `play.cpp` + `platform/frontend.h`: `HostInput` gains `joystick`,
  the SDL path fills it from `SDL_GameController`, and `DeviceState.joystick`
  is fed from it. With `vitapad.h` on the Vita side.
* **F3** ✓ DONE 2026-09-18 (`Input_Poll`, not `Input_ReadOneControl`, is the frame's reader) — READ `Input_ReadOneControl`'s joystick arm from the listing (the
  axis sign, the dead zone); port it ✎ `input/bindings.*` with a check shown
  to fail; then drop `vitapad.h`'s keyboard stand-in for the directions.
* **F4** ✓ DONE 2026-09-18 (`backends/vita/vita_main.cpp`; the IME for the name field is open) ✎ `play.cpp`: the Vita paths — data root `ux0:data/omk/gamedata`,
  tables and config beside it, saves under `ux0:data/omk/`, no command line
  (defaults instead), 960x544, the SDL_GetKeyboardState paths guarded.
* **B3** ✓ BUILDS (`omk_vita.vpk`; LiveArea assets and a device boot still to do) — `-DOMK_VITA_GAME=ON`, the VPK, LiveArea assets (`sce_sys/`), boot
  to the menu. Title id `OMKE00001`.
* **A1** — Audio: SDL2-vita's audio device through the existing `Frontend`
  audio calls; measure the music stream (M1) and the sound mixing cost.

### Phase 3 — make it hold 30 fps

In the order `handoff-vita.md` §2 gives, re-sorted by P1's factor:

* **P2** ✎ apply `pending/vita-meshidx-playcpp.md` (the name index; 0.09 ms).
* **G6** — Stop reading back interface frames: compose the 2D layer on the
  CPU into an RGBA surface with a KEY for "nothing here" and blend it over the
  GPU picture in the present pass. The passes that READ the pixels under them
  (the fade, the darkening) become present-pass uniforms. Needs the list of
  those passes first; it is the largest unknown here.
* **P3** — Share poses between bodies on the same clip and frame; skip a body
  whose pose and placement did not change (also skips its upload and tie).
* **P4** — Thread the per-body loop with `omk::Threads` (bench says EXACT on
  the three stages; the check is the whole frame's bytes, threaded against
  inline).
* **P5** — GPU skinning and the crowd's light in the vertex shader (~1.9 ms
  on the M1). Its own exactness story: GPU float against CPU float, per
  platform — not "same bytes".
* **M1** — Stream the music (16 MB), drop CPU copies of texture pixels once
  uploaded, measure vitaGL's memory with its own counters.
* **P6** — Load hitches: the set and music loads on the card; move the music
  open off the main thread; measure.
* **G5** — The native mirror on the GLES stencil (today: `drawWithMirror`'s
  CPU path, a readback — see item 4).

### Phase 4 — ship

Precompiled shaders (no `libshacccg` step), the controls play-tested and
written up, `todo/play-test.md` entries for each confirmed step.

---

## 3. How to run what exists

From `engine/`, after `make` has built `build/obj/` (nothing here needs
`make` to know about it):

```
# the bench - the same source the Vita runs
c++ -std=c++20 -O2 -Isrc -Ithird_party -o build/vita_bench \
    backends/vita/bench_main.cpp build/obj/src/*/*.o
build/vita_bench "$OMK_DATA" 45 300

# the GLES backend against the software reference, headless, macOS
c++ -std=c++20 -O2 -Isrc -Ithird_party -o build/gles_probe \
    backends/gles/gles_probe.cpp backends/gles/glesrender.cpp \
    build/obj/src/*/*.o -framework OpenGL
build/gles_probe "$OMK_DATA" "$OMK_DATA/MESHES/DECORS/Aapkayl.3DO" \
    3526,1015,-905 3412,1032,-882 83
```

Both are now `make` targets and `verify.py` checks (above). The probe's
cutout blind spot is still open and is written into its check's docstring.

---

## 4. Traps met while writing this

* **Compare in 565.** See §0 — a present check in 888 measures the driver.
* **A mutation must be shown to apply** (CLAUDE.md §1). The five here were
  made on scratch copies with an asserted single anchor and a `cmp` against
  the original; the file under `backends/gles/` was never mutated, so there
  is nothing to restore and no stale object (the probe links from source).
* **GLSL ES 1.00 has no integer operators, no `textureSize`, no
  `texelFetch`**, and guarantees dynamic uniform indexing only in the vertex
  stage. The shimmer, the UV normalisation and the Bayer lookup are each
  shaped by one of those; the comments in `glesrender.cpp` say which.
* **GLES2 REPEAT needs power-of-two textures.** All 2534 under `MESHES` are
  (max 256x256, counted 2026-09-18); the backend clamps and SAYS for any that
  is not.
* **The films are decoded in HARDWARE, from H.264 copies** (2026-09-18).
  `pl_mpeg` showed 41 of EIDOS's 386 frames on a console. `scripts/vita-movies.sh`
  converts `FLIS/*.mpg` to Baseline H.264 + AAC at 320x240 under
  `engine/build/vita-movies/`; copied to `ux0:data/omk/movies/`, the game plays
  them through SceAvPlayer (`backends/vita/avmovie.*`) and falls back to the
  MPEG-1 path when they are absent. The frame's chroma is **U first** (NV12):
  read V-first, GAME's advert drew its red cap blue in Vita3K. Played at 60 fps
  in the emulator; NOT yet seen on a console.
* **A console read area 118's chunk as ZEROS** (`set ''`, no startup script,
  so the run never leaves the splash) while Vita3K read the same build's
  `IAM\AREA` whole. The binary loaders' `std::ifstream` + `tellg` read is
  silent when short, so they now go through `readWholeFile` (`datafs.h`):
  `sceIo` in 1 MiB reads on the Vita, and a `datafs: SHORT READ` line when
  bytes are missing. If a console still shows the empty set, the log now
  prints the chunk's nonzero count and the file's FNV-1a against the
  shipped `0x2e637003` - which separates a bad READ from a bad COPY.
  **Answered the same day: a bad COPY, not a bad read.** The console's
  `IAM\AREA` is 1253386 bytes (FNV-1a 0x9c68a65d) against the shipped 1253376
  (0x2e637003): about ten bytes inserted before chunk 118's offset 579584, with
  the directory untouched, so the chunk still reads 2516 bytes and 1563
  nonzero ones - just starting ten bytes early, where +88 is not the set. Not
  an ASCII-mode FTP conversion, which would have added 5643 bytes.
  **And the ten bytes are FileZilla's ASCII mode, reproduced exactly**: the
  file holds ten CR LF pairs, and turning each into CR CR LF gives 1253386
  bytes and FNV-1a 0x9c68a65d - the console's file to the byte. FileZilla
  treats a file with NO EXTENSION as text, and that is every IAM archive (54
  files: AREA, SCENE, DIALOG, GLOBAL, START, OBJECT, the screens' text...).
  Copy `gamedata/` with the transfer type forced to BINARY.
* The film lookup matches the name case-insensitively in
  `ux0:data/omk/movies`, `<data>/FLIS` and `app0:movies`, and when it finds
  none it lists what each folder held (or the kernel's error), so a console
  log tells a wrong folder from a wrong name.
