# The NINTENDO SWITCH port — the plan

Written 2026-11-25, on the reader's request ("plan a Switch port; install
heavy libraries on the external disk"). This file is the plan and, from
step 1 on, the record; a `handoff-switch-port.md` takes over the "where it
stands" half once there is a console run to report.

> **THIS TREE IS SHARED.** Stage by explicit path - never `git add .`, `-A`
> or `commit -a`. The Switch files will live under `engine/backends/switch/`,
> outside the Makefile's `src/*/*.cpp` and `tools/*.cpp` globs, so they are in
> nobody's default build (`PORTING` A8 rule 1, as the Vita's and the 3DS's).

---

## 0. The state when this was written: the engine already builds

**A probe, not yet a port.** Before planning anything, the whole engine was
put through the Switch toolchain to find out what breaks. Nothing did:

| what | result |
|---|---|
| `src/*/*.cpp` + `backends/sdl/*.cpp` (minus `playgpu_{gl1,vulkan,none}`, `playharness_off`) + `backends/gles/glesrender.cpp`, `-DOMK_GLES=1` | **168 of 168 compile**, devkitA64 GCC 15.2, `-std=gnu++20 -O2 -ffp-contract=off`, ~75 s on 6 cores of the M1 |
| `play.cpp` with `-Dmain=omk_play_main` (the Vita/Android arrangement) + a 15-line `main` (romfs, then `omk_play_main(gamedata, tables)`) | **links**: `-lSDL2 -lEGL -lGLESv2 -lglapi -ldrm_nouveau -lnx -lm`, no undefined symbol |
| `elf2nro` | `omk.nro` **10.0 MB** (ELF 19.5 MB) |

**Zero lines of the engine changed.** The newlib include traps that bit the
Vita three times (`<cmath>` pulled in transitively on the host only) did not
show here: the Vita build's fixes already cover them. Nothing has RUN yet -
no emulator, no console. The GLES backend compiled down its generic
`<GLES2/gl2.h>` branch (no MSAA, no occlusion query); step 2 gives it the
GLES 3 branch the Quest already has.

### The toolchain: devkitPro's own image, in a VM on the external disk

* **pkg.devkitpro.org answers 403 to this machine** (Cloudflare) - the same
  wall the 3DS hit (`handoff-3ds-port.md`), so `dkp-pacman -S switch-dev`
  is out. `/opt/devkitpro` holds a pacman and devkitPPC from another
  project; nothing for the Switch.
* **Docker Hub answers.** devkitPro publishes `devkitpro/devkita64`, an
  `arm64` Linux image (no emulation on the M1) holding devkitA64, libnx,
  switch-tools (`elf2nro`, `nacptool`, `build_romfs`, `nxlink`, `uam`) and
  every portlib this port needs: **SDL2 2.28.5** (`switch-sdl2`, EGL),
  **Mesa 20.1** (`libEGL`, `libGLESv2`, nouveau for the Tegra X1),
  `libdrm_nouveau`, **deko3d**, plus ffmpeg/mpv/opus/vorbis we do not need.
  ~650 MB compressed. It is devkitPro's own pacman install, not a rebuild -
  which is also what devkitPro asks for (`scripts/3ds-toolchain.sh`'s header).
* **Pinned**: `devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282`
  (built 2026-02-19). A toolchain that moves under the port is a build that
  changes without a commit; raise it deliberately.
* **Where it lives**: Docker runs in a **colima** VM whose whole home is on
  the external drive - inside the `omk-devtools` APFS image
  (`/Volumes/Crucial_X8/decomp/tools/omk-devtools.sparsebundle`, mounted at
  `/Volumes/omk-devtools`), under `switch/colima/`. Not on the bare drive: it
  is exFAT (no sparse files, 128 KB clusters - the classic Mac work's reason
  for the image, `decomp/tools/README.md`). Nothing lands on the internal
  disk but colima's empty `~/.colima/_lima`.

```sh
hdiutil attach /Volumes/Crucial_X8/decomp/tools/omk-devtools.sparsebundle
export COLIMA_HOME=/Volumes/omk-devtools/switch/colima
colima start                      # the VM made 2026-11-25: vz, 6 CPU, 6 GB, 30 GB disk,
                                  # mounts the repo and /Volumes/omk-devtools/switch
export DOCKER_HOST=unix://$COLIMA_HOME/default/docker.sock
docker run --rm -v "$PWD":/omk:ro -v /Volumes/omk-devtools/switch:/sw \
    devkitpro/devkita64@sha256:1fc388c3... /sw/probe/compile.sh   # the probe
colima stop                       # 6 GB of RAM back
```

**Trap**: `colima start` rewrites the docker context named `colima` to its
own socket. Another project on this machine uses that context
(`/Volumes/mps3-outils/colima`); starting either one's VM re-points it.
Always pass `DOCKER_HOST` explicitly, never rely on the context.

**A native route exists and is not needed yet**: devkitA64 built from
devkitPro's buildscripts as `scripts/3ds-toolchain.sh` does for devkitARM,
with the target libraries (`libnx`, `portlibs/switch` - target code, host
independent) copied out of the image's layers. Worth it only if the Docker
round trip (~2 s a build) ever costs more than an hour of GCC.

---

## 1. The target, and what carries over

| | Switch (Tegra X1, "Erista"/"Mariko") | for scale: the Vita |
|---|---|---|
| CPU | 4x Cortex-A57 @ 1020 MHz (1785 in CPU boost); **3 cores** for an application, the 4th the system's | 4x Cortex-A9 @ 444 MHz, 3 for the game |
| RAM | 4 GB shared; a homebrew NRO launched from the album (applet mode) gets **a few hundred MB**, one launched by holding R over a game (title takeover) ~3.2 GB | 512 MB + 128 MB VRAM; OMK's heap 192 MB |
| GPU | Maxwell GM20B, 256 cores, 307-460 MHz handheld / 768 docked; **GLES 3.2 / GL 4.x through Mesa nouveau**, or deko3d (native, Vulkan-like) | SGX543MP4+ through vitaGL |
| screen | 1280x720 handheld; 1920x1080 docked (TV) | 960x544 |
| input | Joy-Con pair / Pro Controller (sticks, ABXY, L/R/ZL/ZR, +/-), touchscreen in handheld | pad + touch |

The Vita's release run put the street at **22.8 ms sim+draw** (median, max
40) on a 444 MHz A9 (`684ad40`). A57s at 1020 MHz are several times that per
core, so the original's 30 fps should be comfortable with room for the
remaster - **an expectation to MEASURE in step 5, not a figure** (the Vita's
own "the GPU keeps up" was corrected by its seventh run, `handoff-3ds-port.md`).

**What carries over unchanged** (proven by the probe compiling it):

* the whole SDL frontend, `backends/sdl/*.cpp`, through the Vita/Android
  arrangement: `play.cpp` built `-Dmain=omk_play_main`, a platform `main` that
  supplies the arguments;
* `backends/gles/glesrender.cpp` and `playgpu_gles.cpp` - the Quest's GLES 3
  path is the closest (`#include <GLES3/gl3.h>`, MSAA, `GL_ANY_SAMPLES_PASSED`
  queries, the orphaned whole-buffer upload);
* the films: `platform/movie.cpp` decodes MPEG-1 with the vendored `pl_mpeg`
  in software - the Vita needed AvPlayer for speed; an A57 should not (measure);
* `platform/threads_std.cpp` (libnx has pthreads), `platform/datafs_posix.cpp`
  (newlib's `sdmc:` / `romfs:` devices are POSIX paths), the SDL mixer path;
* `src/input/pad.h`, the game's own JOYSTICK device, fed from an SDL
  GameController;
* the remaster's packs: Maxwell samples BC1-7 **and** ASTC, so either the
  desktop pack (BC7) or the Android one (ASTC) can load - which ones Mesa's
  GLES context EXPOSES is a console question (step 2 logs the extensions).

---

## 2. Decisions (recommendations; the reader's to overrule)

1. **Renderer: the GLES backend through Mesa first, deko3d only on evidence.**
   Zero new backend code to first light. A deko3d backend (the citro3d of
   this port) is step 7, opened only if the console's GPU time says Mesa's
   overhead is what stands between the port and 30 fps.
2. **Capabilities by name, not by platform.** `glesrender.cpp` selects GLES 3
   features with `#if defined(__ANDROID__)`; adding `|| defined(__SWITCH__)`
   to six sites is how ports rot. Step 2 replaces them with `OMK_GLES3`
   (defined for Android and the Switch) feeding `OMK_GLES_MSAA` /
   `OMK_GLES_QUERY` / the orphan upload - the ONE touch to shared code, and
   the Quest build must stay byte-for-byte the same feature set
   (`engine: vita build` and the Android build run after it).
3. **Package: an NRO for hbmenu** (Atmosphère), as the Vita's VPK. An NSP
   forwarder (home-menu icon, full memory without holding R) is optional,
   later, and the reader's call.
4. **Layout on the SD card**, the Vita's with `sdmc:` for `ux0:`:

   ```
   sdmc:/switch/omk/omk.nro         the program (hbmenu lists /switch/*)
   sdmc:/switch/omk/gamedata/       the game's data tree, copied from the disc
   romfs:/tables/                   the exe's tables as JSON, inside the NRO
   sdmc:/switch/omk/saves/GAMES     saves - never into the data tree (CLAUDE.md §1)
   sdmc:/switch/omk/omk.ini         the config, read when present
   sdmc:/switch/omk/args.txt        extra arguments, one per line (--area, --fps, ...)
   sdmc:/switch/omk/omk-play-YYYYMMDD-HHMMSS.log / .err
   ```

5. **The frame**: render at the mode's own size - 1280x720 handheld,
   1920x1080 docked - the 3D keeping its horizontal FOV, the 640x480
   interface stretched by I2D as on the Vita, `--res 960x720` in `args.txt`
   for the pillarboxed 4:3. 30 fps as the original (swap interval 2 on the
   60 Hz panel, or the engine's own clock - whichever the Vita settled on).
6. **Enhancements stay OFF by default** (CLAUDE.md §5): rumble, gyro aiming
   in shoot mode, 60 fps, MSAA - each a flag / `[Enhancements]` row, built
   only after the original plays.

---

## 3. The steps

Each ends usable or measured, is committed on its own, and records its
numbers here (CLAUDE.md §4: a finding that produces a number gets a check).

**S0 - toolchain and probe.** DONE 2026-11-25 (§0).

**S1 - the build in the tree.** DONE 2026-11-25 - the record first, the
plan as written after it.
* `make switch` builds `build/switch/omk.nro`, **10.7 MB**, through the
  pinned image (~2.5 min fresh, ~15 s incremental on the M1); `make
  switch-release` the same with `OMK_RELEASE`. The ROMFS holds the 14
  tables. 0 warnings, every source unchanged. One fault, the CMake file's:
  SDL2's pkg-config link line does not carry Mesa (`glGetError` undefined),
  so `EGL GLESv2 glapi drm_nouveau nx` are named after it, the probe's order.
* `switch_main.cpp`: romfs, the `sdmc:/switch/omk/` tree, `args.txt`, the
  dated log pair (or `nxlink` stdio when `nxlink -s` launched it), the
  frame from `appletGetOperationMode` (1920x1080 docked, 1280x720
  handheld), the applet type and the process memory logged at start and
  exit, **the game on a thread with an 8 MB stack**.
* **The pool on the Switch's cores** (`platform/threads_std.cpp`, two
  `__SWITCH__` blocks): libnx makes every pthread on the process's default
  core - core 0, the main thread's - so worker i is pinned to core
  1 + i % 2, and `hardwareDefault()` answers 2 without asking newlib. Host
  unchanged: `thread pool` and `engine: threaded bodies` green.
* `verify.py: engine: switch build` - exit 0, no `error:`, the NRO written
  by THIS run, its `NRO0` / `ASET` and a table name inside; skipped with no
  devkitA64 and no Docker daemon. Shown to fail by a Switch-only `#error`
  in `threads_std.cpp`: (2, 1, False, (False, False, False)). `engine: vita
  build` green after it.
* Not yet: nothing has run. Whether the pinning is right (the worker's core
  as `svcGetCurrentProcessorNumber` reports it) is S2's first log line to read.

The plan as written:
* `engine/backends/switch/CMakeLists.txt` on the Android file's model: the
  same `src/*/*.cpp` glob, the same `PLAY_SDL_SRCS` filter, `OMK_GLES=1`,
  `-ffp-contract=off` (A57 has FMA; the host's floats must match), the
  toolchain's `Switch.cmake` and `nx_create_nro` (icon, NACP, `ROMFS` holding
  `tables/`), `OMK_RELEASE` / `OMK_INSTRUMENTS` options as the Vita's.
* `engine/backends/switch/switch_main.cpp`, the Vita's `vita_main.cpp`
  re-said for libnx: `romfsInit`, the `sdmc:/switch/omk/` tree created,
  `args.txt` appended after the defaults, the dated log pair (and `nxlink`
  stdio when launched with `nxlink -s`, the dev loop over Wi-Fi),
  **`omk_play_main` on its own thread with an 8 MB stack** - `play.cpp`'s
  `main` frame is the one that needed 8 MB on the Vita, and an NRO's main
  stack is whatever the loader gave it - and `appletGetAppletType()` logged
  with the memory it implies (`svcGetInfo` total/used).
* `Threads::hardwareDefault()` capped to 2 workers on the Switch (3 cores
  for the application, the main thread is one): read what libnx reports
  first, then cap only if it says 4.
* `make switch` / `make switch-release` driving it: native `$DEVKITPRO` with
  devkitA64 when present, else the pinned image through `$DOCKER_HOST`, else
  the "optional, everything else builds" message every cross target prints.
* `verify.py: engine: switch build` - builds the NRO, skips (never fails)
  without a toolchain; **shown to fail** with a Switch-only break (an include
  removed on a scratch copy, asserted applied).

**THE FIRST CONSOLE RUN - the reader, 2026-11-25** (the S1 NRO, on the
reader's Erista): *"j'ai testé la version switch. Elle se lance mais quelques
problèmes: 1) la clavier s'ouvre tout seul au lancement et il n'est plus
possible de le rouvrir ensuite (nouvelle partie) 2) menu remaster absent
3) Les boutons sont en configurations ps vita (avec b pour valider, au lieu
de a, inverse les deux boutons."* So it boots, the films and the menu run,
and the pad works. The three faults and what was done (S3's first half, and
the remaster's build half of S6 brought forward):
1. **The keyboard.** `front.startTextInput()` at boot calls
   `SDL_StartTextInput`, and devkitPro's SDL2 answers it with libnx's
   INLINE keyboard (`SWITCH_StartTextInput`, `swkbdInline*` in `libSDL2.a`),
   shown once at launch, unaware of the name field. Now: the Switch stops
   SDL's text input instead (`sdlfront.cpp`), and the name field opens the
   system keyboard - the library applet, modal, `swkbdShow` - when the focus
   lands on it, the Vita's arrangement exactly (`backends/switch/swkbd.cpp`,
   the hook in `playframe_modes_parts.cpp` now `OMK_SYSTEM_KEYBOARD`, defined
   in `playshared.h` for the Vita and the Switch). The name already in the
   field comes up in the keyboard; Latin-1 both ways. A cancel does not
   reopen it every frame: move off the field and back to type again.
   **Not yet seen on the console** - whether `swkbdShow` runs from the game's
   own thread (not the process's main thread) is the next run's question.
2. **The Remaster page.** It exists only in a build with `OMK_REMASTER`
   (`playomkoptions.cpp`), and the Switch build had none. Now the CMake file
   defines it for every object and compiles `backends/remaster/*.cpp`, as
   Android's does. One newlib include trap followed: `rgbcx.h` calls
   `memcpy`/`memset` without `<cstring>` - included by
   `src/formats/blockdecode.cpp` before it, the vendored header untouched.
   Its shaders are `#version 100`, so the GLES 2 context runs them; which
   pack fits applet mode's memory is still S6's measurement.
3. **The buttons.** devkitPro's SDL2 maps the pad by POSITION - its
   `SDL_CONTROLLER_BUTTON_A` is the bottom button, Nintendo's B - so the
   confirm (`pad::South`) sat on B. On the Switch the map takes
   `SDL_CONTROLLER_BUTTON_B` (Nintendo's A, the right button) for the
   confirm and `..._A` for the back (`sdlfront.cpp`); X and Y keep their
   positions (left = West, top = North). **The decision S3 asked for: the
   game's validate is Nintendo's A.**
The NRO 11.1 MB; `engine: switch build` and `engine: vita build` green,
`make play play-gles` builds.

**What that first run's log says** (`omk-play-20261125-222600.log`, read
off the card; the run stayed in the start menu and its options, 3738 frames,
quit by `Quitter -> Oui`, 0 areas entered):
* launched in **title takeover** (application, full memory), **handheld**,
  1280x720;
* **the pinning is right**: `worker 0 on core 1 (asked 1)`, `worker 1 on
  core 2 (asked 2)` - S1's open question closed;
* `memory at main: 3185 MB used of 3189 MB`, the same at exit: newlib's heap
  takes the whole of what the process may have at start, so `svcGetInfo`'s
  used/total says nothing about the game. The peak heap needs mallinfo (the
  Vita's way), for S5;
* `renderer: GLES2` - the generic `<GLES2/gl2.h>` branch, so the reader's
  `[Enhancements] all = max` got `8x MSAA refused` (S2's `OMK_GLES3` gives it
  MSAA); no `GL_RENDERER` / extension line yet (S2);
* the menus: sim+draw 3.2-32.0 ms a 60-frame mean, of which `screen draw`
  2.5-14.9 ms (the CPU composer); 49 slow frames, the two worst explained -
  726 ms saving the options to the SD card, 776 ms applying `all = max`
  (every program relinked, every texture re-sent);
* SDL opened `Switch Controller` eight times - libnx's eight pad slots.
* The reader moved files with **hekate's USB mass storage / a card reader**;
  Haze (hbmenu's "USB File Transfer", MTP) was SEEN by the Mac (057e:201d)
  but OpenMTP could not use it - Kalam "an error occurred", Legacy nothing,
  `ptpcamerad` killed and back. `nxlink -a <ip>` from the image is the
  untested alternative.

**THE PACK ON THE CARD, 2026-11-25** (the reader: *"le mode remaster ne peut
pas fonctionner car il manque le pack"*). The card had 2.3 GiB free (59 GiB,
the emuMMC and the game data on it), so the pack went over in ASTC only:
`sdmc:/switch/omk/remaster/` = `pack/` - `full-anchor3`'s 3347
`.astc.ktx2` (colours, normal maps, light maps; 1340 MB, copied `-L` through
its symlinks) - plus `ui/`, `films/`, `fonts/`, `grades/` from the M1's
`~/omk-pack`; 1.9 GB, every file compared, no `._` files, **366 MiB left**.
`omk.ini` gained `[Remaster] dir = sdmc:/switch/omk/remaster` (the options
menu edits the file, so the key survives a save). **The open question it
rests on: does Mesa's nouveau name `GL_KHR_texture_compression_astc_ldr` on
the GM20B?** The GLES backend reads ASTC only then, and the CPU fallback
decodes BC7/BC4 only (`texpack.cpp`), which this card does not hold. The
next log answers it: `remaster: sdmc:/switch/omk/remaster - the preset:
texture pack .../pack` and `gles: the texture pack's ASTC files - N colours
read as blocks` (N should be 1255). Without it, the BC7 pack is 2.9 GB - a
bigger card, or a pack trimmed to the street.

**THE SECOND CONSOLE RUN - the reader, 2026-11-25 23:18** (the NRO of
`5b8b1e4` and the pack above): *"Le jeu a crashé. Les lumières semblaient
ne pas fonctionner, tout était affiché de manière uniforme, comme en plein
jour."* The log (`omk-play-20261125-231844.log`) says:
* **the keyboard works**: `name field: the system keyboard gave "test"` -
  `swkbdShow` from the game's own thread is fine;
* **ASTC works**: Mesa names it - `gles: the texture pack's ASTC files -
  1255 colours read as blocks`; the film, interface and font packs load;
* the new game ran from GRID through AIMPASSE (area 222) to the first door,
  AIMPASAS (142) at frame 3650, and the log stops mid-line there.
**The crash**: Atmosphère's report (`atmosphere/crash_reports/
01795645311_01003fc01670c000.log` - the NRO is not in its module list, so
the load base was found as the one 0x1000-aligned base that puts 34 of 35
return addresses after a BL/BLR: `0x52fdca3000`) symbolises, on the game
thread, to `PlayState::phaseWorld` -> `phasePreload` -> `std::thread::
detach` -> `__cxa_throw` -> `std::terminate` -> `abort`. libnx defines no
`__syscall_thread_detach`, so newlib's `pthread_detach` (in libsysbase)
returns ENOSYS (88) and `std::thread::detach()` throws: the FIRST background
job - the remaster's preload, at the first door - ends the game. Fixed in
`platform/threads_std.cpp`: on the Switch `hostthread::spawnDetached` makes
a pthread with the stack size asked (std::thread's is libnx's 128 KB, and a
job is a set loader; 1 MB asked), moves it to core 2, and keeps it to be
JOINED once it says it is done, at the next spawn. Elsewhere the same
function now keeps `hostthread.h`'s promise - false when no thread could be
made, a failed detach joined - instead of letting a `std::system_error`
reach `std::terminate`. `thread pool`, `engine: threaded bodies`, `engine:
preload`, `engine: switch build` green. The new NRO copied to the card,
compared.
**The light**: the reader's `omk.ini` has `[Enhancements] all = max`, so
per-pixel lighting is on with the remaster - and that pair floods a set on
EVERY GPU backend, not the Switch's alone (the Mac's GLES and Vulkan
measured, `todo/remaster.md` "OPEN: per-pixel lighting floods a set").
Per-pixel lighting off is the workaround until it is read.

**S2 - first light.** ~1-2 days, needs a console or an emulator (§4).
* `playgpu_gles.cpp`: on the Switch a GLES 3.0 context (the Quest's branch;
  Mesa offers 3.2); decision 2's `OMK_GLES3` in `glesrender.cpp`.
* Log `GL_RENDERER`, `GL_VERSION` and the extension string once - the BC7 /
  ASTC / timer-query answer for steps 5-6.
* Boot through the FLIS films to the start menu, then `--area` into
  Anekbah. The Vita's first-light faults to check for first: data copied in
  ASCII mode (the Vita's black screen), a stale `omk.ini`, a missing
  `tables/`.

**S3 - input.** ~1-2 days.
* Joy-Con pair / Pro Controller through SDL's GameController into
  `pad.h`'s device, buttons laid out as the Vita's (confirm = A on a
  Nintendo pad - its RIGHT face button; the Vita's cross is BOTTOM; decide
  and write down which the game's "validate" maps to). Single detached
  Joy-Con held sideways: later, if at all.
* Touchscreen (handheld) as the mouse for the menus, as the Vita's front panel.
* Text entry (save names): libnx `swkbd`, the Vita `ime.cpp` pattern - a
  modal call returning UTF-8 into the engine's own text path.
* HOME / sleep: SDL turns applet exit into `SDL_QUIT`; a sleep-resume must
  not run the 30 fps clock's catch-up for the hours asleep (the transition
  invariant, CLAUDE.md §1) - test it.

**S4 - docked and handheld.** ~1 day.
* The mode changes at run time (dock in/out): SDL reports a resize; the GLES
  backend's targets must re-create at the new size, or the port keeps one
  size and lets the present scale. Read what the backend already does on a
  desktop window resize before choosing.
* `appletGetPerformanceMode()` logged per frame bucket: docked clocks the
  GPU 768 vs 307-460 MHz, so a handheld run is the budget.

**S5 - the measured baseline.** A release NRO, handheld, the reader's run:
the street's sim+draw median/max, an open screen's composing time, film
decode ms, peak heap + Mesa's share - laid beside the Vita's 22.8 ms and the
3DS's figures in `handoff-switch-port.md`, which this step creates. The
CPU-boost question (`appletSetCpuBoostMode`) is answered here, not before.

**S6 - the remaster.** Once S5 says what is free: which pack (BC7 or ASTC,
from S2's extension string), at which tier (`remaster.md`'s Vita tiers are
the template), in which memory mode - a full pack needs title takeover or a
forwarder, an applet-mode NRO likely cannot hold it.

**S7 - optional, each on evidence or request.** deko3d backend (decision 1);
NSP forwarder; rumble on hits; gyro aim in first-person shoot mode
(`shoot-mode-is-first-person`); 60 fps (`sixty-fps.md`). All enhancements,
all off by default.

---

## 4. What waits on the reader

* **The console: the reader HAS one** (2026-11-25) - an early model, booted
  through the **RCM exploit** (fusée-gelée), so an unpatched **Erista**
  (Tegra X1 T210, the 1020 MHz / 307-768 MHz GPU figures of §1, not
  Mariko's). Its firmware and Atmosphère version are not known yet; record
  them at the first console run. Every step from S2 on is judged on it, as
  the Vita and the 3DS were. RCM means a cold boot needs the payload pushed
  again (jig + injector); hbmenu then launches the NRO - from the album
  (applet mode, little memory) or by holding R over a game (title takeover,
  full memory): log which.
* **The SD card, prepared 2026-11-25** (outside the repo, in
  `/Volumes/omk-devtools/switch/`): hekate 6.5.5 and Atmosphere 1.12.2
  (release digests checked), `sd-stage/` the tree to copy, `install-sd.sh
  <card> <repo>` the copy - it refuses a card with less than 32 GiB free,
  writes no `._` files (`cp -X`, `dot_clean`), and compares every file it
  copied. **The reader's rule: no CFW on the real eMMC.** So
  `hekate_ipl.ini` offers two entries only - Atmosphere with
  `emummcforce=1` (refuses to boot without the emuMMC) and stock firmware
  (`stock=1`, `emummc_force_disable=1`); `exosphere.ini` blanks the
  emuMMC's PRODINFO; `atmosphere/hosts/emummc.txt` sends Nintendo's hosts
  to 127.0.0.1 (conntest to 90DNS, so Wi-Fi - nxlink - still connects).
  The emuMMC itself is made ON THE CONSOLE (hekate: emuMMC > Create emuMMC
  > SD File, ~29.2 GiB for a 32 GB Erista). The game data goes to
  `switch/omk/gamedata/` as its directories and `.TAB` files - not the
  executables or the disassembler's databases. Tested on a 40 GB exFAT
  image: 1.6 GB in 32 s, verified, 0 AppleDouble files.
* **An emulator** as the Vita3K / Azahar stand-in: Ryujinx's maintained fork
  runs NROs on macOS arm64; it would go on the external disk image beside the
  toolchain. Its GPU timings mean nothing (the Azahar lesson: no cache model),
  its correctness a lot.
* **The data on the SD card**: `gamedata/` copied in BINARY (the Vita's black
  screen was FileZilla's ASCII mode), into `sdmc:/switch/omk/gamedata/`.
  OMK ships none of the game's data, as on every port.

---

## 5. Traps already known

* The external drive is **exFAT**: tools go inside the `omk-devtools` APFS
  image, never the bare drive.
* `colima start` **re-points the `colima` docker context** (§0); use
  `DOCKER_HOST`.
* **FMA**: AArch64 contracts `a*b+c` unless told not to; `-ffp-contract=off`
  everywhere, as the Android build does.
* **The main stack** (8 MB on the Vita for `play.cpp`'s frame): run the game
  on a thread whose stack the port sets, not the loader's.
* **Applet mode's memory** is a fraction of the console's; a run that dies of
  `bad_alloc` from the album may be fine from title takeover - log which.
* **devkitPro's terms**: the toolchain is used, never redistributed; nothing
  it produces but OMK's own NRO is committed or shipped (as the 3DS script).
