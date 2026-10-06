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
  the game side (sim+draw) **~30-48 ms**. **The GPU does NOT simply keep
  up** (the seventh run corrected the fifth): in the dense street (~220
  draws) the CPU waits 13-16 ms for it at `C3D_FrameBegin` and citro3d's
  drawing reads 52 ms, so both sides are over the budget there. The present
  went 22-36 -> 10 ms; what is left of it is the dither loop (5-6 ms) and the
  frontend's copy (1.5-2.9 ms tiled; the 2:1 path for films and menus, 7.7,
  tiled since and **not yet run**).
* **The straight present runs ONE FRAME BEHIND** (the reader's decision,
  2026-10-06): the GPU draws frame N while the CPU shows N-1. It read its
  buffer too early before - a stale or torn picture on the console that
  Azahar could never show - and `readback()` is now truly synchronous.
  **Proved in Azahar** one frame apart from `c3d-sync` (99.86% at desktop
  frame 300 against 99.85% at 301); the speed is owed by the console.
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

**Azahar** (2126.1.2 on the M3 since 2026-10-06 - the release's arm64 zip,
ad-hoc signed, so Gatekeeper rejects it: the reader accepted it into
`/Applications`; installed in `/Applications` on both; the emulated card is
`~/Library/Application Support/Azahar/sdmc/`, its `omk/gamedata` a SYMLINK to
the repo's tree): `/Applications/Azahar.app/Contents/MacOS/azahar <3dsx>`.
It ignores SIGTERM - `pkill -9`. Wait for the previous instance to be gone
before launching another (a launch racing an exit silently does nothing).
Its timings are not the console's (no cache model, a different CPU speed).
The log is `omk/omk-play-<date>.log` - with a HYPHEN, while the program is
`omk_play.3dsx`; a wait loop globbing `omk_play-*.log` never ends. A run with
`--frames N` in args.txt stops at "N frames presented" and waits for START.

**The instruments** (all 3DS-only, `n3dsfront.cpp` / `c3drender.cpp`):

| file on the card / log line | what it does |
|---|---|
| `omk/capture-at` (a number) | CAPTURE that present: `captures/frame-*.bin` (the frame handed over) and `screen-*.bin` (the top screen as the hardware holds it, 240 a column) |
| `omk/panel-dump` | each panel redraw also to `omk/panel.bin` (320x240 RGB565) |
| `omk/c3d-rgb565` | THE 16-BIT EXPERIMENT: RGB565 target, 565 transfers, no CPU conversion |
| `omk/c3d-sync` | every straight present SYNCHRONOUS (waits for its own picture) - to lay beside the default one-frame-behind present |
| `frame N c3d (...)` | draws, posed on GPU/CPU, the GPU wait at begin and at a synchronous transfer, the straight presents behind / synchronous, command buffer peak, citro3d's times |
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
2. **The one-frame-behind present** (the seventh run's fix): a run as
   before, then the same with `sdmc:/omk/c3d-sync` - the frame time and the
   `c3d` line's wait at begin, side by side; and whether the street still
   tears or lags to the eye. And the films' copy (7.7 ms before the 2:1
   tiling).
3. GAME.MPG is still "not decodable" on the reader's card (an incomplete
   copy).

## 4. What to do next, in order

1. **Read the next log** (section 3): the pipelined present against
   `c3d-sync`, and decide the 16-bit default with the reader.
1b. **THE FILL EXPERIMENT IS READY**: `--res 400x224` in args.txt draws the
   whole frame at the screen's own size - a quarter of the 800x448 pixels,
   and no 2x2 average, so no anti-aliasing either - and the straight present
   goes over 1:1 (Azahar: the capture 99.51% against the desktop's own
   400x224 frame, one frame behind as the default). On the console: the
   dense street at 400x224 against 800x448 - if citro3d's drawing and the
   wait at begin fall by much, fill is the GPU's cost. A measurement, not a
   default: 800x448 stays the reader's.
1c. **The GPU's side** in the dense street (52 ms drawing at ~220 draws):
   what the PICA spends it on - fill (800x448 is 2.6x the screen's pixels;
   a 400x224 target would be the screen's own) or vertices - before any CPU
   work, since the CPU now waits for it there.
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
   world frame; the desktop draws it - unexplained, open). **A second one,
   2026-10-06, in Azahar at `--res 400x224`**: 2147483632 bytes =
   `0x7FFFFFF0`, which in the 32-bit build is `max_size()` of a
   `std::vector` with 16-byte elements - what libstdc++ asks for when a vector
   GROWS believing its size is over half of `max_size()`, i.e. a `push_back`
   on a vector whose pointers are garbage (uninitialized, freed or
   overwritten). It did NOT come back on the next four runs with the same
   args, the only change one `printf` in the allocator: layout-dependent,
   the signature of an uninitialized read or a use-after-free (not a thread
   race: the 3DS build runs none, and Azahar has no sound device). Not
   reproduced in 15 more runs over four sizes, nor by rebuilding that commit.
   The desktop under UBSan + libc++'s DEBUG hardening (every `vector[]`
   checked; shown to trap) is clean through the crash window. THE NEXT ONE
   EXPLAINS ITSELF: the refusal prints its SITE and the stack's RETURN
   ADDRESSES (a word in the program whose previous instruction is a call);
   resolve them with `arm-none-eabi-addr2line -i -f -C -e
   engine/build/n3ds/omk_play.elf <addresses>` - `-i` matters, the site is
   usually inside an inlined `std::vector` chain whose outermost frame is the
   caller. Proved with a forced refusal in Azahar: the site named the very
   line, the scan the call above it, with stale libc entries among them.
   **Keep the `.elf` of every build sent to the card** - without it the
   addresses mean nothing. The window: after the start-up's `screen 29.`
   line, before the first frame's `motion: mesh ...` line. AddressSanitizer on
   the desktop is NOT the way on this M3: its runtime deadlocks in its own
   initialiser on macOS 26 (`AsanInitFromRtl`, a spin lock, before `main`).
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
* **`C3D_SyncDisplayTransfer` INSIDE a frame does not wait** - it queues
  the transfer and returns (citro3d 1.7.1, `renderqueue.c:417`); out of a
  frame it waits for the queue and the transfer. A timer around the
  in-frame call reads 0 whatever the GPU does - the fifth run's "the GPU
  keeps up" rested on one - and the CPU reading the buffer next reads it
  before the GPU wrote it. Azahar finishes every command as it is queued, so
  it shows neither. Read citro3d's source (`~/.cache/omk-3ds-toolchain/src/
  citro3d-*/source/`) before trusting a call's name.
* **zsh heredocs**: an inner `EOF` line ends an outer `<<'EOF'`; patch
  scripts went to files with every anchor asserted before writing.
