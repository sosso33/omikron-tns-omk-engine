# Handoff — the PS VITA PORT, as it stands on 2026-09-18

**Read this first to pick up the Vita.** [`vita-port.md`](vita-port.md) is the
plan (phases, item ids B/P/G/F/A/M) and the running record of every trap met;
this file is the state, the recipes and what to do next. The older
[`handoff-vita.md`](handoff-vita.md) is the PRE-port decision (the 6.0 ms CPU
frame on the M1 and why 30 fps is far off) - still true, not repeated here.

---

## 1. Where it stands

* **The game builds for the Vita and runs on a real console** up to the start
  menu: the three intro films, the splash, screen 29, the pause menu on START.
  It **has not yet been seen past the menu on a console** - see §3.
* **In Vita3K it plays**: films, menu, area 118 (`GRID`), the intro script.
  3D reads back black in the emulator (`glReadPixels` returns zeros there), so
  the emulator proves boot, input, I/O and logic, not pixels.
* Every Vita change is committed (`1622783` .. `0500a32`); host builds
  (`make`, `make play`) stay green - the Vita code is all `#if defined(__vita__)`
  or under `engine/backends/vita/`, and `play.cpp` builds everywhere.

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

1. The next console log: films found? area 118 = `GRID`? then how fast is a
   frame in the apartment (the first real P1 number on hardware).
2. ~~**G6**~~ - DONE on the Mac 2026-09-21 (steps 1-4, `vita-port.md`): fades,
   subtitles, conversations, the three gauges, the shoot HUD and every open
   screen but three go over the GPU picture with no readback. Not yet seen on a
   console - the `present` line of the next log says how many frames took it.
3. Precompiled shaders: the machinery is in (2026-09-21, `vita-port.md`), the
   cache is NOT made - it needs one run where `libshacccg.suprx` is, then
   `scripts/vita-shader-cache.sh` and `make vita`.
4. P2..P6 in `vita-port.md` §2 - the CPU plan; `pending/vita-meshidx-playcpp.md`
   is P2, ready to apply.
5. The `play.cpp` split is PLANNED ONLY ([`play-split.md`](play-split.md),
   `tools/play_split_scan.py`); do it before the Vita work grows `play.cpp`
   further.

The **full `--slow` sweep is due** (`sweep-log.md`: 9 tasks since the last);
the Vita work so far was verified with `--only` over `engine: vita bench`,
`gles backend`, `vita build`, `input poll`, `vita printf`, `engine: boot`,
`engine: intro` and `licence headers` (470).

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
