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

**S1 - the build in the tree.** ~1 day.
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
