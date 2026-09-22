# Handoff — the PS VITA PORT, as it stands on 2026-09-18

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

VitaSDK is at `~/vitasdk`; vitaGL is OUR build (`scripts/vita-vitagl.sh`,
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

## 4. What to do next, in order

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

The **full `--slow` sweep is due** (`sweep-log.md`: 11 tasks since the last);
the Vita work so far was verified with `--only` over `engine: vita bench`,
## 5. Traps that cost time - the short list (`vita-port.md` §4 has all of them)

* **The standard library is not the platform.** `std::filesystem`,
  `std::mutex`, `freopen(stdout)` all broke on the Vita (heap corruption,
  silent failure). File access goes through `DataFs` / `readWholeFile` /
  `fileSize` / `makeDirectories` (`platform/datafs.h`), which are `sceIo` there.
* **newlib's printf has no `%zu`/`%td`**: `printf_c99.cpp` wraps the printf
  family and strips the length modifier. Do not assume a format "just works".
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
