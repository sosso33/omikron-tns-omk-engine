# Handoff — the NINTENDO 3DS PORT (begun 2026-10-05, written 2026-10-06)

**Read this first to pick up the 3DS.** [`3ds-port.md`](3ds-port.md) is the
plan (its steps 0-10) and the running record - every console run, every
finding, with its numbers; this file is the state, the recipes and what to do
next. Everything 3DS-only lives in `engine/backends/n3ds/` and
`scripts/3ds-toolchain.sh`; nothing in `src/` or `backends/sdl/` was changed
for it (the reader's rule while other sessions work there: "focus on new 3ds
specific code").

---

## 1. Where it stands

* **The game runs on the reader's New 3DS** (custom firmware, Homebrew
  Launcher, 124 MB application memory): the films, the start menu, a save
  loaded, the restaurant, Anekbah's street with its crowd, drawn by the
  3DS GPU and checked by eye in the reader's captures. Sound works (the DSP
  firmware is on the card). The touch panel works.
* **Proved in Azahar against the desktop**: `omk_boot` reproduces
  `traces/intro.log` **42 of 42** in order; `omk_play`'s menu frame 300 is
  **byte-identical** to the desktop's; the citro3d world agrees with the
  software reference at **99.8-99.9%** of pixels within 24 levels (street),
  **96-97%** with the letterbox exact (Impasse); the straight present and
  the frontend's two copy paths are pixel-exact against the screen as written
  (96000/96000, 89600/89600).
* **Speed, on the console** (street, mean of 60 frames; `3ds-port.md` has
  every run): first light ~10 fps (median slow frame 99 ms) -> after 3a-3d
  the game side (sim+draw) **~30-48 ms**, the GPU **keeping up** (0.00 ms
  waited at the end of a frame), the frame **CPU-bound**. The present went
  22-36 -> 10 ms; what is left of it is the per-pixel dither loop (5-6 ms) and
  the frontend's copy (6-7.6 ms - a cache fault, FIXED in `b6c0016`, **not
  yet run on the console**).
* **16:9 by default** (`--res 800x448`, the reader's decision), halved to
  400x224 on the top screen.

What is done, by the plan's numbering (details in `3ds-port.md`):

| step | state |
|---|---|
| 0 toolchain + `engine: 3ds build` | done (`scripts/3ds-toolchain.sh`, no pacman, no sudo) |
| 1 headless boot | done in Azahar (42/42), runs on the console |
| 2 frontend (libctru) | done; console-confirmed |
| 2b instrument panel (bottom screen) | done; console-confirmed (touch, CAPTURE) |
| 3 citro3d backend | first light + 3a texture cache + 3b GPU transform + 3c straight present + 3d GPU posing, all done; console-confirmed except the last two fixes |
| 4 memory fit / `.cia` | not started |
| 5 controls, saves, films | partly: films play in software (MPEG-1 kept, by decision); name-field keyboard owed (shared code) |
| 6 performance (second core) | not started |
| 7 Old 3DS | not started |
| 8 stereoscopic 3D | not started |
| 9 packaging | `.3dsx` only |
| 10 per-screen survey (EXTRA, last) | not started - the reader made it the final step |

## 2. Recipes

**The toolchain** (once, ~1 h, into `~/devkitpro`):

```
scripts/3ds-toolchain.sh            # builds what is missing; --check reports
OMK_GNU_MIRROR=https://mirrors.kernel.org/gnu scripts/3ds-toolchain.sh   # the M3: ftp.gnu.org does not answer it
```

**Installed on BOTH machines** since 2026-10-06 (the M1 first, the M3 the
same day, `~/devkitpro` on each).

devkitPro's download hosts answer 403 to this machine (Cloudflare), so the
script feeds devkitPro's own buildscripts with the sources from their
upstreams; eight macOS fixes are commented in it. A pacman install
(`/opt/devkitpro`) works as well - the Makefile takes `$DEVKITPRO`.

**Build** (both programs, ~1 min fresh):

```
export DEVKITPRO=$HOME/devkitpro DEVKITARM=$HOME/devkitpro/devkitARM
make -C engine/backends/n3ds                  # -> engine/build/n3ds/omk_{boot,play}.3dsx
python3 tools/verify.py --exact "engine: 3ds build"
```

**The card** (`sdmc:/`):

```
3ds/omk_play.3dsx, omk_boot.3dsx, dspfirm.cdc (DSP1's dump - no sound without)
omk/gamedata/      the game's tree (IAM/, MESHES/, ... directly inside; copy in BINARY mode)
omk/args.txt       optional extra arguments, several per line allowed, # comments
omk/saves/GAMES    optional saves file
omk/save-appart.bin  traces/save-appart.bin, for starting in a city
```

`args.txt` for the street: `--save sdmc:/omk/save-appart.bin --area 0 --stand
1804,0,-6890,336` (add `--nofmv` to skip the films). **No `--profile` for a
speed reading** - the capture is written to the card every frame.

**Azahar** (installed in `/Applications`; the emulated card is
`~/Library/Application Support/Azahar/sdmc/`, its `omk/gamedata` a SYMLINK to
the repo's tree): `/Applications/Azahar.app/Contents/MacOS/azahar <3dsx>`.
It ignores SIGTERM - `pkill -9`. Wait for the previous instance to be gone
before launching another (a launch racing an exit silently does nothing).
Its timings are not the console's (no cache model, a different CPU speed).

**The instruments** (all 3DS-only, `n3dsfront.cpp` / `c3drender.cpp`):

| file on the card / log line | what it does |
|---|---|
| `omk/capture-at` (a number) | CAPTURE that present: `captures/frame-*.bin` (the frame handed over) and `screen-*.bin` (the top screen as the hardware holds it, 240 a column) |
| `omk/panel-dump` | each panel redraw also to `omk/panel.bin` (320x240 RGB565) |
| `omk/c3d-rgb565` | THE 16-BIT EXPERIMENT: RGB565 target, 565 transfers, no CPU conversion |
| `frame N c3d (...)` | draws, posed on GPU/CPU, GPU wait at a transfer, command buffer peak, citro3d's times |
| `frame N c3d CPU (...)` | begin, begin..end, submit (residency, pose uniforms), the dither loop |
| `frontend: present copy ...` | the frontend's copy a frame, the panel's redraw |

Decode a capture: raw little-endian RGB565, the size in the log line.

## 3. Waiting on the reader (the console)

1. **The two passes of the 16-bit question** (`3ds-port.md` section 3): a
   run with an empty `args.txt` and one with `sdmc:/omk/c3d-rgb565`, each with
   a CAPTURE on a dark area (the ground, the fog). A fine regular pattern in
   the 16-bit capture means the PICA dithers - then 16-bit is the original's
   arrangement exactly and the default; bands mean it does not, and the reader
   chooses.
2. **The new timers**: the tiled copy (expected far below 6-7.6 ms), and the
   `begin..end` line, which splits the 10-21 ms "world submit" section into
   the backend's submits (3.3 ms) and what is not them.
3. GAME.MPG is missing from the reader's card (an incomplete copy).

## 4. What to do next, in order

1. **Read the next log** (section 3) and decide the 16-bit default with the
   reader.
2. **Zero-copy present**: render the world ROTATED (the screens' own
   orientation) so the display transfer writes straight into the top
   framebuffer - no dither loop, no frontend copy. Needs the 16-bit decision
   (or an RGB8 top screen).
3. **The rest of the CPU frame**: `splitList`'s per-corner mirror scan
   (shared code - `src/o3de/renderer.cpp`), the panel's redraw (6-15 ms twice
   a second: redraw less, or only what changed), the interface frames' CPU
   path (the GLES overlay's GPU blend, not ported).
4. **Step 6, the second core** (`OMK_THREADS`, the New 3DS gives a whole
   core) - the reader's decision on threads first.
5. **Step 4**: the `.cia` (makerom, its own memory mode), memory measured.
6. Owed in SHARED code, for when the other sessions allow: the name-field
   keyboard (`swkbd`, a `Frontend` call instead of the Vita's `#if`), and the
   **800x450 bad_alloc** (a 135 MB allocation in the 32-bit build at the first
   world frame; the desktop draws it - unexplained, open).
7. Then steps 7 (Old 3DS), 8 (stereoscopic 3D, OFF by default), 9
   (packaging), and 10 last (the per-screen survey).

## 5. Traps that cost time

* **The viewer takes the FIRST `--res`** - a default put before args.txt's
  silently won. `n3ds_main.cpp` adds the default only when none is given.
* **A bare `--profile` swallows the next argument** as its path (it took
  `--res`). Now defaulted to `sdmc:/omk/run.prof`; args.txt lines are split
  on spaces.
* **Azahar's timings lie about the cache**: the frontend's copy read 1.5 ms
  there and 6-7.6 on the console. Measure on the console.
* **No divide instruction on the ARM11**: any `/` in a per-pixel loop is a
  software division (the present's nearest path cost 22-36 ms; `quantise888`
  divides by 255).
* **With `GX_TRANSFER_SCALE_XY` the transfer halves the OUTPUT size it is
  given** - give it the input's.
* **The display transfer hands rows top-down**; the viewport's origin is the
  bottom-left.
* **Other sessions rebuild shared sources mid-edit**: a failed link followed
  by a "clean" rebuild packaged a broken ELF that hung at the second frame.
  After any link error, delete `engine/build/n3ds/*.elf *.3dsx` and rebuild.
* **The licence census** counts every new `.h/.cpp` under `engine/backends`
  and `.sh` under `scripts/` - re-pin `licence headers` in `tools/verify.py`
  with each new file (572 as of `b6c0016`).
* **The M3 needed three more toolchain fixes than the M1** (2026-10-06,
  all in the script now): `ftp.gnu.org` refuses connections from it
  (`OMK_GNU_MIRROR`); Homebrew's zstd is found by binutils' configure and not
  linked - "Undefined symbols `_ZSTD_compress`" (`--without-zstd`); and an
  INTEL Homebrew left in `/usr/local` by a migration put an x86_64
  `libgmp.dylib` first on devkitPro's hard-coded `-L/usr/local/lib`, the
  linker ignored it and never reached GCC's in-tree gmp - "libgmp not found
  or uses a different ABI" (the `/usr/local` paths are dropped). A failed
  stage leaves its build directory stamped `configured-*`: delete
  `~/.cache/omk-3ds-toolchain/src/buildscripts-*/.devkitARM/arm-none-eabi/<stage>`
  before re-running, or the fix never reaches configure.
* **A machine without devkitARM reports `engine: 3ds build` SKIPPED, and
  shared-code changes break the 3DS unseen.** The drift audit's `3603df2`
  widened the texture-cache key in `c3drender.cpp` at its two use sites but
  not at the map's declaration, so the 3DS did not compile from that commit
  until the M3 got a toolchain (fixed the same day). Run the check on a
  machine that HAS the toolchain after touching anything the 3DS shares.
* **zsh heredocs**: an inner `EOF` line ends an outer `<<'EOF'`; patch
  scripts went to files with every anchor asserted before writing.
