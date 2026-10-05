# Handoff — the PS VITA PORT (begun 2026-09-18; §1 rewritten 2026-09-22, §4 added to 2026-09-29, §3b5 rewritten 2026-09-30)

**Read this first to pick up the Vita.** [`vita-port.md`](vita-port.md) is the
plan (phases, item ids B/P/G/F/A/M) and the running record of every trap met;
this file is the state, the recipes and what to do next. The older
[`handoff-vita.md`](handoff-vita.md) is the PRE-port decision (the 6.0 ms CPU
frame on the M1 and why 30 fps is far off) - still true, not repeated here.

---

## 1. Where it stands (rewritten 2026-09-22)

* **The game runs on a real console INTO THE CITY.** 2026-09-21/22, three
  console logs: the intro films, the menu, the flat, the intro cutscene at
  17-54 ms a frame with G6 presenting every held frame from the GPU, and
  Anekbah - which first died of `bad_alloc` (the music's 16 MB, fixed), then
  took 8.4 s a frame (vitaGL's whole-buffer copy on every tie patch, fixed),
  then 190-300 ms with the camera "shaking" (the port's delta clamp, fixed
  with the engine's cap of three frames). **The city is now SLOW, not
  frozen**: ~200 ms a frame, all CPU, a uniform ~40x the M1.
* The frame is instrumented to the section: a frame over 150 ms prints its
  eight largest blocks (`sections -`), a frame over 500 ms its GL counts.
  What the 08:44 log named: staged bodies 46, pedestrians 35, scripted motion
  17, and ~100 ms AFTER the last mark of that build, now marked too.
* Since that log, without a console: the body tie replayed instead of
  re-walked (~40 of those ms by the M1's ratio), far bodies not skinned (9 of
  25), P2 applied, one `cos`/`sin` a body instead of two a corner, the crowd's
  light 4x cheaper, the GLES scratch buffers kept, and the frame marked to the
  section and the call. **The M1's street, outside the software rasterizer:
  pedestrians 1.0 -> 0.5 ms, staged bodies 0.4 -> 0.3, scripted motion
  0.2 -> 0.1** - and every step is a BYTE-IDENTICAL render.
  **None of it seen on a console yet.**
* **In Vita3K it plays** up to area 118; 3D reads back black there. The
  emulator and the Mac's GL window both need the DISPLAY ON - with it off the
  GL swap blocks and Vita3K never starts (found 2026-09-22).
* Every Vita change is committed; host builds stay green.

## 2. Recipes

```bash
cd engine && make vita          # -> build/vita/omk_vita.vpk (+ omk_bench, omk_smoke)
scripts/vita-movies.sh          # -> engine/build/vita-movies/{EIDOS,QUANTIC,GAME}.mp4
scripts/vita3k-run.sh game 45   # the emulator; setup | bench | smoke | game [seconds]
```

VitaSDK is at `~/vitasdk` (`~/.zprofile` exports it; until 2026-09-29 it
exported `/usr/local/vitasdk`, a 2021 SDK with no `psp2/avplayer.h` and an older
vitaGL API, which the Makefile preferred - `make vita` now refuses such an SDK by
name). vitaGL is OUR build (`scripts/vita-vitagl.sh`,
`engine/build/vitagl/`, no splash, Vita3K support) - the vdpm one opens a second
GXM context the emulator cannot take. Vita3K needs `VITA3K_ARGS="-B OpenGL"`;
its Vulkan backend crashes on vitaGL.

**The console's layout** (all created or read by `backends/vita/vita_main.cpp`):

| path | what |
|---|---|
| `ur0:data/libshacccg.suprx` | the runtime shader compiler. **Missing = a FATAL line and exit** (it was a SceGxm crash in `glLinkProgram` before) |
| `ux0:data/omk/gamedata/` | the game data, **copied in BINARY mode** - see §3 |
| `ux0:data/omk/movies/*.mp4` | the hardware films; optional, the MPEG-1 path is the fallback |
| `ux0:data/omk/omk.ini`, `args.txt` | config (`backends/vita/omk.ini` is the full template) and extra flags, one per line |
| `ux0:data/omk/saves/GAMES` | the save file |
| `ux0:data/omk/omk-play-YYYYMMDD-HHMMSS.log` / `.err` | one dated pair per run - the reader sends these |

## 3. What is waiting on the reader (the console)

1. **Re-copy `gamedata/` in BINARY mode.** The black screen after the splash
   was **FileZilla's ASCII mode**: it treats a file with no extension as text,
   which is every IAM archive (54 files). Each CR LF became CR CR LF - the
   console's `IAM\AREA` is 1253386 bytes, FNV-1a 0x9c68a65d, reproduced
   exactly from the shipped 1253376 / 0x2e637003. Area 118 then read with no
   set and no startup script, so nothing ever started. The log prints that
   size and hash whenever an area chunk comes out with neither, so the next log
   says at once whether the copy is good.
2. **The films on the hardware decoder** (`backends/vita/avmovie.*`,
   SceAvPlayer). Played at 60 fps in Vita3K; on the console the only log so
   far predates the lookup's own report. The new build finds the name
   case-insensitively in `ux0:data/omk/movies`, `<data>/FLIS`, `app0:movies`,
   and on a miss lists what each held. **Read that line in the next log.**
3. **Untested on a console**: the IME for the name field (`backends/vita/ime.*`),
   the green tint the dither showed in Vita3K (possibly the emulator), and
   everything past the menu - frame rate above all.

## 3b. THE FILMS DO NOT PLAY ON THE CONSOLE - the reader's report, and it had been lost

*"The mp4 files do not play on the real Vita, I go directly to the splash
screen"* - reported more than once and not written down until 2026-09-23. The
console logs agree: each `.mp4` is found, opened and sized correctly (the three
sizes match `engine/build/vita-movies/` to the byte, so it is not transfer
damage), and then SceAvPlayer reports event `0x01` three times - STOP, in the
SceAvPlayer family's numbering - and never `0x02` READY: `0 frames shown, sound
at 0 Hz`. The files are what the decoder is documented to take (`ffprobe`:
Constrained Baseline, level 3.0, 320x240, yuv420p, AAC-LC 44.1 kHz stereo).
Vita3K plays them because it decodes through ffmpeg.

The code's own history says each way of giving the decoder its frame buffers
has ended the same way - a fresh CDRAM block, vitaGL's RAM pool, vitaGL's CDRAM
pool. **The 2026-09-23 build runs an EXPERIMENT**: EIDOS keeps the vitaGL pool,
QUANTIC gets a dedicated PHYCONT memblock and GAME a dedicated CDRAM memblock,
both with the alignment guaranteed by the KERNEL
(`SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT`) - the one thing none of the
earlier tries did. Each film logs `film: strategy N ... free: main / CDRAM /
PHYCONT` and then its frame count, so one console run says which (if any)
plays. `ux0:data/omk/film-alloc` holding one digit forces a strategy for all
three. **If none plays**, the next suspect is the resolution: re-encode at
480x272 or 960x544 and try again.

**The 2026-09-23 morning log answered it differently**: none played, and the
log showed `CDRAM 0 KB, PHYCONT 0 KB` free - vitaGL, initialised by SDL, takes
all of both. Our vitaGL build now leaves 16 MB of each (`vita-port.md`, entry
of that morning). **Read the three `film:` blocks in the next log** - a READY
(`0x02`) event and a frame count above 0 is the answer.

## 3b2. The reader, 2026-09-23 midday (the build with the vitaGL memory patch)

*"A bit better on some aspects, but still laggy. Now, the GAME video plays
(EIDOS and QUANTIC are skipped), but the video is played accelerated while the
audio is played at normal speed. [...] Each time a new line of dialog is loaded
(when I pressed the key), it is a bit long to load the dialog (graphics were
less good, but this loading was faster using a dreamcast emulator on the ps
vita so there is definitely an issue somewhere)."*

So: strategy 1 (a dedicated CDRAM block) is the one that plays, once vitaGL
leaves memory for the decoder; the film's PICTURE runs fast against its sound;
and a line's start is still a visible wait on the console.

**2026-09-24, the reader on `--no-tie`**: *"I tested without and, while
being a little smoother, it does not make a great difference."* The tie is not
the city's lag; the ~100 ms left per frame is.

## 3b3. The reader, 2026-09-26: two faults on the GPU-posing builds

*"Characters animations in cutscene are broken (looks like hands, feet and
head are placed way too far from the body that they should be) in both
versions"* - both being the pre-NEON build (`5c16f8b`, GPU skinning steps
1-4) and the NEON build. *"The game crashes when loading the crowd in the
neon build"* - logs `omk-play-20260926-003535.log` (pre-NEON),
`omk-play-20260926-003919.log` (NEON) and the core dump
`psp2core-1790376118-0x00009425ab-eboot.bin.psp2dmp`.

## 3b4. The reader, 2026-09-29: the Bowie sequence slow, and no fire

*"Still laggy, especially the intro cutscene [the Bowie title sequence] ...
only a fire normally, but the fire effect doesn't work here, it is working on
the vulkan backend"* - `omk-play-20260929-205655.log`. Three causes, all in
`vita-port.md`'s 2026-09-29 entry: the pose self-test still dropping the GPU
posing (the real cause is `int()` ROUNDING on the Vita, not the row stride),
the set's emitters bound into the outgoing pool on a walk-in (the city had
none of its 153 - not a GLES fault), and a per-pixel divide in the overlay
planes (the fade's 46-57 ms). Fixed, built, not yet run on the console.
The same evening, the cheap exact fixes of `optimization.md` step 28 (the
subtitle box's barriers, the music's counting loop, the menu cloud, the load
panel's 8.4 MB read a frame) - and the finding that the console's `omk.ini`
enhancements had left the CROWD UNLIT and EVERY BODY SHADOWLESS on GLES,
which cannot draw per-pixel light or a shadow map: now refused at start-up
(`lighting: per pixel REFUSED`, `shadows: mapped REFUSED ... fitted`).

## 3b4b. 2026-10-05: the GLES backend DRAWS the enhancements now

What the paragraph above calls refused is no longer: GLES draws trilinear,
supersampling, per-pixel lighting and mapped shadows (`todo/enhancements.md`,
"On the GLES backend"), so the console's `omk.ini` - every enhancement at its
top - now COSTS on the Vita, supersampling 4 above all (a 3840x2176 world).
MSAA and anisotropy are refused by vitaGL and say so. **Owed, on a console**:
a run with the shipped `omk.ini` to see what holds (start from
`supersampling = 1`), and the shader cache re-made with the enhancements on
(`scripts/vita-shader-cache.sh`) - the five new programs are not in it, and
without `libshacccg.suprx` each enhancement needing one is refused.

## 3b5. WHERE THE OPTIMIZATION STANDS (2026-09-30) - start here

The port-vs-original audit is `todo/optimization.md` step 28 (the table, then
dated "DONE" paragraphs); the console results are `todo/vita-port.md`'s
2026-09-29/30 entries. Last console run: **2026-09-30 14:00**, the build of
`a87e2c3` - steps 29-34 on the console, the city at 40-50 ms a frame (72 in
the first minute), the player and the sky moved by the renderer, 0-0.2 whole
uploads a frame (`todo/vita-port.md`'s 14:00 entry). The run before it: the
apartment ~19 ms a frame (native mirror), the Bowie sequence ~50-67 ms. The
reader cannot test on the Vita for a while, so the next work should be
provable on the Mac.

**Done since the run before the 14:00 one** - steps 29-34 SEEN in that log,
step 35 its answer, and steps 36-38 (the texture pool set once, moving
meshes by one matrix and their collision as whole meshes; not listed here,
`todo/optimization.md`) seen on no console (each is a step of
`todo/optimization.md`):
* **step 29 - the depth tie decided once per set** and drawn a step back, on
  GLES and Vulkan: the per-frame tie's CPU (0.5-4.4 ms on the M3, ~50 ms of
  the console's city frame by the 2026-09-23 log) is gone for the set, and the
  frame is byte-identical to the per-frame tie's. It replaces step 27's plan.
  **The console question is now the LOOK**: whether the signs hold on the
  Vita's own depth buffer with the bake. `OMK_NO_TIE_BAKE=1` is the per-frame
  tie, `--no-tie` none (the reader has already seen that one flicker).
* **step 30 - Kay'l posed by the renderer and the sky moved by one
  translation**: two whole vertex uploads a frame fewer (the street 4.6 -> 2.7
  on the Mac) and his CPU pose, place and upload (4-8 ms on the console) off
  every frame that does not read his corners. `OMK_CPU_PLAYER=1` /
  `OMK_CPU_SKY=1` are the old paths. In the next console log read `the player
  posed by the RENDERER` and `the sky MOVED BY THE RENDERER`, and the `gles
  world` line's whole uploads.

* **step 31 - a set prepared on a thread while the Session streams it**, as
  the original reads a set one 128 KB piece a frame: the read, the geometry,
  the texture decode and the four soups (17 ms on the M3 for Anekbah, the
  first stall frame of an area change on the console) are off the frame, and
  the set enters the world on the Session's count, not the thread's clock.
  `OMK_SYNC_SETS=1` is the old way. In the next console log read `set load:`
  (its `the frame waited W ms`) and `world: rebuild -`.

* **step 32 - a line's voice decoded on the read-ahead thread**: the lines
  that can come next were already READ while a line played; they are now
  decoded there too, so a line's start is the hand-over. The first line of a
  conversation still loads on its frame. In the next console log read `line
  load:` - `read and decoded AHEAD`, and the `ms here in all`.

* **step 33 - an effect's samples shared with the mixer**, as `Sound_Play3D`
  duplicates a buffer and not its memory: a play from the cache copies
  nothing and no longer holds the mixer's lock over a megabyte. In the next
  console log read the `sounds` section's spikes.

* **step 34 - a composed screen sent by its changed rows**: the start menu,
  the pause screen, the sneak and every other CPU-composed screen went to the
  GPU whole, through vitaGL's copy; they now take the overlay's path (changed
  rows, written into the texture's memory). Put `OMK_PRESENT_CHECK=1` in the
  environment for one run if a screen shows stale rows: it says so by count.

* **step 35 - the console's answer (14:00 log) and four changes**: the
  Session's load HELD until the set is ready (the frame waited 727 ms for
  Anekbah; on by default on the Vita), the IAM archives read once, a line's
  device samples and face bytes off its frame, and a build that refuses a
  stale vitaGL. `todo/vita-port.md`'s 14:00 entry has the log read line by
  line and what to look for next.

* **step 37 (2026-10-01) - a moving set mesh drawn with one matrix**: the
  33 meshes Anekbah's scene moves are no longer rewritten into the set's
  buffer and re-sent every frame; the renderer draws each from its own corners
  with one affine, culled where it is. Collision still patched (the other half
  of row d). `OMK_CPU_MOTION=1` is the old path.

* **step 38 (2026-10-01) - the moving collision layer as whole meshes**: no
  grid rebuilt for the moving triangles each frame; a query takes a moving
  mesh whole by its current extent, as the original does, with the same
  answers bit for bit. In the next log: `scripted motion: grids rebuilt`.
  `OMK_MOVING_GRID=1` is the old path.

**The reader's rule for this work (2026-09-30)**: the original is faster than
the port, so **read the original's function for each task first** and take
what it does - it found the mirror's two passes, the crowd LOD, the camera
clock, and in step 30 that neither the player nor the sky is rewritten.

**Next, in order**: what is left of an area change AFTER the set - the world
rebuild, the texture uploads, the tie bake and the Session's cases 2..9 (the
`.SCX`, the models), which the original does in one tick too, so there is no
mechanism of its to take and the console log has to size them first; a scene's sounds converted with its `.SCX` rather
than on their first play; a conversation's first line. Anekbah's moving-mesh grid + patch (~12 ms, 70x
the Mac, unexplained) waits for a console profile - and row d's fix (a moving
set mesh drawn from rest with one matrix, as `o3de_SetNodePos` does) is now
the same mechanism the sky uses. Open: the original draws far walkers
(coarsest LOD) to the clip distance, the port stops at 40 m; and step 30's
lead on the pose-dirty flag `0x40000`.

**How every step here was proven**: a byte-compare of dumps with the change
on and off (env switches: `OMK_NO_SIDECULL`, `OMK_NO_PED_LOD` /
`OMK_PED_LOD_MAX`, `OMK_STREAM`, `OMK_CPU_MIRROR`, `--cpu-bodies`) on the
street (`--area 0 --stand 1804,0,-6890,336`), the Bowie sequence (`--area 0
--stand 6423,-3,1675,154 --zone-enable 78`) and the Impasse (`--area 222
--scene-chunk 55`). Three traps from step 30, each of which produced an
"identical" that meant nothing: **the data is not at `../gamedata` on the M3**
(`python3 tools/omkpaths.py` says where - a run with no data dumps a black
frame and exits 0); **the player is not drawn in the Impasse cutscene or under
the start screen** (use `--area 237 --address 687` for the flat); and **the sky
is not in every frame** (`--sky 0` against `--sky 1` says whether it is).

## 3c. The transition hitches (2026-09-23)

Not model RE-loads (0 in the fight, the shoot phase and Telis's scene); the
repeated cost was the GLES texture pool re-uploading every texture at each
hand-over, now cached by pixel storage (`vita-port.md`, entry of the same
day). The console log now carries `model load: NAME in X ms` for each model
built and `gles: texture pool of N - K kept, U uploaded` for each pool change:
**read both in the next log** - the first says which FIRST loads make the
1-1.5 s frames, the second that the pool holds.

**Read 2026-09-23 morning**: the pool holds, and the models were not it -
three whole-file READS were (a music switch, a line's `.3DM` read twice, the
two sprite libraries at every scene change). All three fixed; see
`vita-port.md`. The city's frame was then led by the depth tie (~50 ms);
the reader ran `--no-tie` and the signs FLICKER, so the tie stays and is now
decided once per set (§3b5, step 29).

**The reader, 2026-09-25**: *"If needed, you can use arm-specific
instructions (like NEON) as long as you also provide a generic alternative."*
So NEON is allowed in a hot loop, always beside a portable C++ version chosen
at compile time, and the two measured against each other - NEON flushes
denormals and a fused multiply-add rounds once, so equality is shown, not
assumed.

## 4. What to do next, in order

**Added 2026-09-29 (`todo/optimization.md` steps 15-18, 27).** Since the last
console log the GLES backend skips GL state a draw would set to the value it
already has (1809 -> 92 state calls a street frame on the Mac; the frame
identical), the music decodes through a table, and the frame makes ~160 heap
allocations outside the software rasterizer where it made ~724. None of it
is measured on a console. **The next city log answers it**: its `sections -`
lines against 2026-09-27's (submit was 42-47 ms), and the new `frame N gles
state` line every 60 frames - draws, state calls made, skipped. **Step 27 is
superseded by step 29** (§3b5): the tie is decided once per set and the
`--no-tie` question is answered - it flickers.
**And the compiler flags** (step 20): `make vita-tuned` builds the same three
VPKs with Cortex-A9 scheduling and LTO into `engine/build/vita-tuned/`. Run
`omk_bench` from each build on the console and compare the `inline ... total`
and `place`/`texkey` lines - the hashes are already proven equal (Vita3K); the
times are what only the console can give. Only if the tuned one wins does
`OMK_VITA_TUNE` become the default.

0. Nothing below can be sized without **one console log from the city**. The
   frame now prints its eight largest SECTIONS (over 150 ms) and, every 60
   frames, the per-body SPANS inside the two body sections - `ped compose`,
   `ped apply`, `ped place`, `ped light`, `staged skin`, `staged place`,
   `motion gather` - so the next log apportions the whole frame.
1. **The next console log, in the city**: the `sections -` lines (now with
   the submission, the fades, the present and the three scripted-motion marks)
   say where the remaining ~200 ms go; `staged bodies -` says how many were
   skipped; the `present` line says G6 holds. Then the shader-cache test:
   rename `ur0:data/libshacccg.suprx`, launch - the log should say `ABSENT`
   and the game start (the five `.gxp` travel in the VPK since `e0f05da`).
2. **P4 is BUILT for the walkers and off by default** (2026-09-22): the
   crowd's pose pass is split serial-resolve / parallel-body / merge, the
   frame is byte-identical with `--thread-bodies`, and the pass halves on the
   M1. The console's `omk_bench` said `threads: EXACT`, 2.71x on three
   runners (2026-09-22), so it is ON BY DEFAULT since 2026-09-23 - nothing
   to add to `args.txt`; `--no-thread-bodies` there turns it off. The STAGED bodies (the larger section) are still
   serial and are the next candidate.
3. **P5** - the crowd's lights in the vertex shader (`applyLights` is now the
   largest engine leaf on the M1; needs the normal back in the GLES vertex).
4. The scripted motion's grids (17 ms): whichever of the three marks is the
   cost. The moving grid is rebuilt every frame from 2730 triangles.
5. The `play.cpp` split ([`play-split.md`](play-split.md)); P3's walker hold
   was tried and does not fire on a street.

The full `--slow` sweep was last run 2026-09-29 (`sweep-log.md` carries the
count since); the Vita work is verified with `--only` over the checks each
change touches.

## 5. Traps that cost time - the short list (`vita-port.md` §4 has all of them)

0. **`thread_local` is NOT per thread on the Vita** for the pool's kernel
   threads - shared, and raced (the crowd crash of 2026-09-26). Never use it in
   code a worker runs.

* **The standard library is not the platform.** `std::filesystem`,
  `std::mutex`, `freopen(stdout)` all broke on the Vita (heap corruption,
  silent failure). File access goes through `DataFs` / `readWholeFile` /
  `fileSize` / `makeDirectories` (`platform/datafs.h`), which are `sceIo` there.
* **newlib's printf has no `%zu`/`%td`**: `printf_c99.cpp` wraps the printf
  family and strips the length modifier. Do not assume a format "just works".
* **`int()` in a Vita shader ROUNDS (half to even), it does not truncate** -
  `int(k + 0.5)` is `k + 1` for odd `k`. `floor` first (2026-09-29; it was the
  pose self-test's "odd slots", misread for two days as a row-stride rule).
* **The Vita pool must hand out chunks, not assign them by slot** - a woken
  worker is any worker (2026-09-30, the Bowie crash). Test a pool at the job
  counts a culled frame gives it (1-2), not only at big ones.
* **A job on its own thread** is `BackgroundJob` (`platform/threads.h`): it
  must own its inputs and outputs, and the FRAME it is used on must come from
  the game's state, never from when it finished - or two runs differ.
* **`engine/build/vitagl/libvitaGL.a` IS PER MACHINE** and nothing rebuilt it
  when a patch was added: a VPK from the M3 went out on a library of
  2026-09-18 and the films stopped playing (`CDRAM 0 KB`). The build now
  refuses one whose `omk-recipe.stamp` does not match; run
  `scripts/vita-vitagl.sh` on each machine.
* **The A9 has no integer DIVIDE**: a `/` by a runtime value is a library
  call. Keep it out of per-pixel loops (the overlay planes' `row`, 2026-09-29).
* **vitaGL overflows uniform ARRAYS** - use separate `vec4`s (`uWave0..7`).
* **The heap**: 192 MB (`_newlib_heap_size_user`); 300 MB was refused on the
  console and aborted on the first allocation.
* **A missing file used to be silent** - three rounds of this in one day
  (the area, the films, the chunk). Every lookup that can miss now says what
  it looked for and what it found; keep it that way.
* **The emulator's log is not flushed on a kill**; Vita3K's `game.log` shows the
  `sceIoOpen`/`Dread`/`Write` calls, which is how a run is read when the
  game's own log is short.
* A SceAvPlayer frame is **NV12, U first** - V-first drew red as blue.
