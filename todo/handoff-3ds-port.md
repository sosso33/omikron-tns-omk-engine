# Handoff — the NINTENDO 3DS PORT (begun 2026-10-05, updated 2026-10-07)

**Read this first to pick up the 3DS.** [`3ds-port.md`](3ds-port.md) is the
plan (its steps 0-10) and the running record - every console run, every
finding, with its numbers; this file is the state, the recipes and what to do
next. Everything 3DS-only lives in `engine/backends/n3ds/` and
`scripts/3ds-toolchain.sh`. Shared code was touched in ONE place:
`src/platform/profile.cpp`'s refused-allocation line (its site everywhere,
and under `__3DS__` the stack's return addresses) - the reader's rule while
other sessions work in `src/` and `backends/sdl/` is "focus on new 3ds
specific code".

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
  (96000/96000, 89600/89600), and the 2:1 path's tiling is word-for-word the
  old loop (480000/480000 on the host, shown to fail).
* **Speed, on the console** (street, mean of 60 frames; `3ds-port.md` has
  every run, the seventh the latest): first light ~10 fps -> after 3a-3d the
  game side (sim+draw) **~30-48 ms**. **The GPU does NOT simply keep up**
  (the seventh run corrected the fifth): in the dense street (~220 draws)
  the CPU waits **13-16 ms** for it at `C3D_FrameBegin` and citro3d's drawing
  reads **52 ms** - both sides over the 33 ms budget there. The present went
  22-36 -> ~10 ms: the dither loop 5-6 ms, the frontend's copy 1.5-2.9 ms
  (tiled), the films' and menus' 2:1 copy 7.7 ms (tiled since, not yet on
  the console).
* **The eighth run (2026-10-07, the five-run card session, `3ds-port.md`)**:
  the one-frame-behind present CONFIRMED on the console (~25% of the frame:
  47 against 63 ms in the street); the GPU's side is about HALF FILL
  (`--res 400x224` cuts citro3d's drawing ~40% and the frame to ~34 ms);
  the RGB565 target saves 4 ms of present and shows NO dither pattern
  (posterised blotches) where the RGBA8 + CPU dither shows its fine one.
  Both decisions - the 16-bit default and the target size - are the
  reader's, and wait on them.
* **The straight present runs ONE FRAME BEHIND** (the reader's decision,
  2026-10-06: "a 1 frame delay is acceptable for this kind of game"): the
  GPU draws frame N while the CPU shows N-1. Before, it read its buffer
  before the GPU had written it - a stale or torn picture on the console that
  Azahar never showed - and `readback()` (interface frames, the mirror) is
  now truly synchronous. **Proved in Azahar**: one frame apart from
  `c3d-sync` (99.86% at desktop frame 300 against 99.85% at 301). The speed
  it buys is owed by the console.
* **16:9 by default** (`--res 800x448`, the reader's decision), halved to
  400x224 on the top screen by the transfer. `--res 400x224` draws at the
  screen's own size and presents 1:1 - the FILL EXPERIMENT, a measurement and
  not a default (no 2x2 average, so no anti-aliasing).
* **One open fault**: an intermittent `bad_alloc` at the first world frame
  (section 5). Seen twice in Azahar, never on the console so far.

What is done, by the plan's numbering (details in `3ds-port.md`):

| step | state |
|---|---|
| 0 toolchain + `engine: 3ds build` | done on BOTH machines (`scripts/3ds-toolchain.sh`, no pacman, no sudo) |
| 1 headless boot | done in Azahar (42/42), runs on the console |
| 2 frontend (libctru) | done; console-confirmed |
| 2b instrument panel (bottom screen) | done; console-confirmed (touch, CAPTURE) |
| 3 citro3d backend | first light, 3a texture cache, 3b GPU transform, 3c straight present, 3d GPU posing: console-confirmed. The seventh run's fixes (one frame behind, synchronous readback, the 2:1 tiling) and the 400x224 1:1 present: Azahar-proved, **not yet on the console** |
| 4 memory fit / `.cia` | not started |
| 5 controls, saves, films | partly: films play in software (MPEG-1 kept, by decision); name-field keyboard owed (shared code) |
| 6 performance (second core) | not started |
| 7 Old 3DS | not started |
| 8 stereoscopic 3D | not started |
| 9 packaging | `.3dsx` only |
| 10 per-screen survey (EXTRA, last) | not started - the reader made it the final step |

## 2. Recipes

**The toolchain** (once, ~1 h, into `~/devkitpro`; installed on the M1 and
the M3):

```
scripts/3ds-toolchain.sh            # builds what is missing; --check reports
OMK_GNU_MIRROR=https://mirrors.kernel.org/gnu scripts/3ds-toolchain.sh   # the M3: ftp.gnu.org does not answer it
```

devkitPro's download hosts answer 403 to this machine (Cloudflare), so the
script feeds devkitPro's own buildscripts with the sources from their
upstreams; eight macOS fixes are commented in it. A pacman install
(`/opt/devkitpro`) works as well - the Makefile takes `$DEVKITPRO`.

**Build** (both programs, ~1 min fresh):

```
export DEVKITPRO=$HOME/devkitpro DEVKITARM=$HOME/devkitpro/devkitARM
make -C engine/backends/n3ds                  # -> engine/build/n3ds/omk_{boot,play}.3dsx (+ .elf)
python3 tools/verify.py --exact "engine: 3ds build"
```

**Keep the `.elf` beside every `.3dsx` sent to the card** - a crash's
addresses (section 5) mean nothing without the exact ELF that made them.

**The card** (`sdmc:/`):

```
3ds/omk_play.3dsx, omk_boot.3dsx, dspfirm.cdc (DSP1's dump - no sound without)
omk/gamedata/      the game's tree (IAM/, MESHES/, ... directly inside; copy in BINARY mode)
omk/args.txt       optional extra arguments, several per line allowed, # comments
omk/saves/GAMES    optional saves file
omk/save-appart.bin  traces/save-appart.bin, for starting in a city
```

`args.txt` for the street: `--save sdmc:/omk/save-appart.bin --area 0 --stand
1804,0,-6890,336` (add `--nofmv` to skip the films, `--frames N` to stop at
"N frames presented"). **No `--profile` for a speed reading** - the capture
is written to the card every frame.

**Azahar** (2126.1.2, the release's arm64 zip; ad-hoc signed, so Gatekeeper
rejects it - the reader accepted it into `/Applications` on both machines).
The emulated card is `~/Library/Application Support/Azahar/sdmc/`, its
`omk/gamedata` a SYMLINK to the game tree (`omk.conf`'s `data`); on the M3
`omk/args.default` holds the street args to restore after an experiment.
Run: `/Applications/Azahar.app/Contents/MacOS/azahar <3dsx>`. It ignores
SIGTERM - `pkill -9`. A launch racing the previous instance's exit silently
does nothing, so a scripted run waits for it and finds its log by a marker:

```
pkill -9 -f Azahar.app/Contents/MacOS/azahar
while pgrep -f Azahar.app/Contents/MacOS/azahar >/dev/null; do sleep 1; done; sleep 2
touch MARK; (/Applications/Azahar.app/Contents/MacOS/azahar "$SD/3ds/omk_play.3dsx" &)
# poll: find "$SD/omk" -maxdepth 1 -name 'omk-play-*.log' -newer MARK  (until "frames presented" or FATAL)
```

The log is `omk/omk-play-<date>.log` - a HYPHEN, while the program is
`omk_play.3dsx`. Its timings are not the console's (no cache model, a
different CPU speed, every GPU wait 0) and it has no sound device.

**Against the desktop**: render the same start with `build/omk-play ...
--res 800x448 --filter nearest --frames N --dump dN.bin` (raw RGB565) for a
window of N around the capture's present, halve each 2x2, and score the %
of pixels within 24 levels per channel against the capture's picture rows -
the best N is the frame shown. That is how the one-frame-behind present was
told apart from `c3d-sync`.

**The instruments** (3DS-only, `n3dsfront.cpp` / `c3drender.cpp`):

| file on the card / log line | what it does |
|---|---|
| `omk/capture-at` (a number) | CAPTURE that present: `captures/frame-*.bin` (the frame handed over) and `screen-*.bin` (the top screen as the hardware holds it, 240 a column) |
| `omk/panel-dump` | each panel redraw also to `omk/panel.bin` (320x240 RGB565) |
| `omk/c3d-rgb565` | THE 16-BIT EXPERIMENT: RGB565 target, 565 transfers, no CPU conversion |
| `omk/c3d-sync` | every straight present SYNCHRONOUS (waits for its own picture) - to lay beside the default one-frame-behind present |
| `--res 400x224` in args.txt | THE FILL EXPERIMENT: the frame at the screen's own size, presented 1:1 |
| `frame N c3d (...)` | draws, posed on GPU/CPU, the GPU wait at begin and at a synchronous transfer, the straight presents behind / synchronous, command buffer peak, citro3d's times |
| `frame N c3d CPU (...)` | begin, begin..end, submit (residency, pose uniforms), the dither loop |
| `frontend: present copy ...` | the frontend's copy a frame, the panel's redraw |
| `new: N bytes REFUSED at 0x...` + `new: return addresses ...` | a refused allocation's site and the stack's return addresses (section 5) |

Decode a capture: raw little-endian RGB565, the size in the log line.

## 3. Waiting on the reader - ONE card session (DONE 2026-10-07)

Run on `842bd42`; the results are `3ds-port.md`'s EIGHTH run. Still owed
from it: one run WITH the films (the 2:1 copy), and - only if the 16-bit
question needs it - E at `--res 400x224` (1:1, so the transfer's 2x2
average cannot hide a GPU dither). The checklist as it was run:

Copy the current `omk_play.3dsx` (and keep its `.elf`), then run the street
start (`args.txt` above, with `--nofmv`), walking into the dense part, once
per line - a minute each is enough:

| run | on the card | answers |
|---|---|---|
| A | nothing extra | the one-frame-behind present's frame time and the `c3d` line's wait at begin; whether anything tears or lags to the eye |
| B | `omk/c3d-sync` | the same, synchronous - A against B is what the pipeline buys |
| C | `--res 400x224` in args.txt | the fill experiment: citro3d's drawing and the wait at begin against A's |
| D | `omk/capture-at` on a dark area (the ground, the fog) | the 16-bit question, pass 1 (RGBA8 + CPU dither) |
| E | `omk/c3d-rgb565` + `omk/capture-at` | pass 2: a fine regular pattern in the dark means the PICA dithers - then 16-bit is the original's arrangement and the default; bands mean it does not, and the reader chooses |

Remove each file before the next run. One run WITH the films (no `--nofmv`)
reads the 2:1 copy (7.7 ms before its tiling). And GAME.MPG is still "not
decodable" on the reader's card (an incomplete copy). Any `FATAL` - send the
log; the `.elf` of that build names its line (section 5).

## 4. What to do next, in order

1. **The reader's two decisions** from the eighth run: the 16-bit default
   (565: 4 ms faster, posterised; RGBA8 + CPU dither: the original's look),
   and the target size (fill is about half the GPU's 52 ms dense; 400x224
   gets ~34 ms frames without anti-aliasing; `GX_TRANSFER_SCALE_X` from
   800x224 is an unmeasured middle point).
2. **The GPU's other half** (~24 ms dense at 800x448, growing with the
   draws): the vertices and the draw count.
3. **Zero-copy present**: render the world ROTATED (the screens' own
   orientation) so the display transfer writes straight into the top
   framebuffer - no dither loop, no frontend copy, and it keeps the
   one-frame-behind shape. Needs the 16-bit decision (or an RGB8 top screen).
4. **The rest of the CPU frame**: `splitList`'s per-corner mirror scan
   (shared code - `src/o3de/renderer.cpp`), the panel's redraw (6-15 ms twice
   a second: redraw less, or only what changed - doable without the
   console), the interface frames' CPU path (the GLES overlay's GPU blend,
   not ported).
5. **Step 6, the second core** (`OMK_THREADS`, the New 3DS gives a whole
   core) - the reader's decision on threads first.
6. **Step 4**: the `.cia` (makerom, its own memory mode), memory measured.
7. Owed in SHARED code, for when the other sessions allow: the name-field
   keyboard (`swkbd`, a `Frontend` call instead of the Vita's `#if`).
8. Then steps 7 (Old 3DS), 8 (stereoscopic 3D, OFF by default), 9
   (packaging), and 10 last (the per-screen survey).

## 5. The open fault - an intermittent bad_alloc at the first world frame

* **Seen twice, both in Azahar, both at the first world frame**: a 135 MB
  request at `--res 800x450` (step 3's first light) and **2147483632 bytes =
  `0x7FFFFFF0`** at `--res 400x224` (2026-10-06). The second is a
  fingerprint: in the 32-bit build it is `max_size()` of a `std::vector`
  with 16-byte elements - what libstdc++ asks for when a vector GROWS
  believing its size is over half of `max_size()`, i.e. a `push_back` on a
  vector whose pointers are garbage (uninitialized, freed or overwritten).
  The desktop (64-bit) has never shown either.
* **Layout-dependent, not input-dependent**: it did not recur after a
  one-`printf` change, nor in 15 more runs over 800x448, 400x224, 800x450 and
  640x480, nor in a rebuild of the crashing commit (whose layout differs
  anyway - the worktree's path is in the binary's strings). Not a thread
  race: the 3DS build runs no thread, and Azahar has no sound device.
  ~1 run in 17.
* **Ruled out on the desktop**: UBSan + libc++'s DEBUG hardening (every
  `vector[]` and iterator checked; shown to trap on an out-of-range index)
  ran clean through the window. So no shared-code out-of-range `[]` or UB
  there on 64-bit - what is left is 32-bit-only arithmetic, 3DS-only code, or
  an uninitialized read the hardening cannot see.
* **The window**: after the start-up's `screen 29. arrows move...` line,
  before the first frame's first `motion: mesh ... moved by the ACTIVE pool`
  line - the end of setup and the first frame up to its scripted-motion
  gather.
* **THE NEXT ONE EXPLAINS ITSELF**: the refusal prints its SITE and the
  stack's RETURN ADDRESSES (a word in `__start__`..`__exidx_start` whose
  previous instruction is an ARM or Thumb `BL`/`BLX`). Resolve them with
  `arm-none-eabi-addr2line -i -f -C -e engine/build/n3ds/omk_play.elf
  <addresses>` - **`-i` matters**: the site usually sits in an inlined
  `std::vector` chain whose OUTERMOST frame is the caller. Proved with a
  forced refusal in Azahar (a scratch build): the site named the very line,
  the scan the call above it, with stale libc entries (`_malloc_r`, `apt*`)
  among the candidates - read the program's own functions nearest first.

## 6. Traps that cost time

* **The viewer takes the FIRST `--res`** - a default put before args.txt's
  silently won. `n3ds_main.cpp` adds the default only when none is given;
  that is also why `--res 400x224` in args.txt is enough for the fill
  experiment.
* **A bare `--profile` swallows the next argument** as its path (it took
  `--res`). Now defaulted to `sdmc:/omk/run.prof`; args.txt lines are split
  on spaces.
* **Azahar's timings lie**: the frontend's copy read 1.5 ms there and 6-7.6
  on the console (no cache model); every GPU wait reads 0. Measure on the
  console.
* **`C3D_SyncDisplayTransfer` INSIDE a frame does not wait** - it queues
  the transfer and returns (citro3d 1.7.1, `renderqueue.c:417`); out of a
  frame it waits for the queue and the transfer. A timer around the
  in-frame call reads 0 whatever the GPU does - the fifth run's "the GPU
  keeps up" rested on one - and the CPU reading the buffer next reads it
  before the GPU wrote it. Azahar finishes every command as it is queued, so
  it shows neither. Read citro3d's source (`~/.cache/omk-3ds-toolchain/src/
  citro3d-*/source/`) before trusting a call's name.
* **No divide instruction on the ARM11**: any `/` in a per-pixel loop is a
  software division (the present's nearest path cost 22-36 ms; `quantise888`
  divides by 255).
* **With `GX_TRANSFER_SCALE_XY` the transfer halves the OUTPUT size it is
  given** - give it the input's.
* **The display transfer hands rows top-down**; the viewport's origin is the
  bottom-left.
* **After any compile or link error, delete `engine/build/n3ds/*.elf
  *.3dsx` before rebuilding** - the old outputs survive a failed build, and
  copying one to the card (or to Azahar) tests the previous code. Other
  sessions rebuild shared sources mid-edit, too: a failed link followed by a
  "clean" rebuild once packaged a broken ELF that hung at the second frame.
* **A machine without devkitARM reports `engine: 3ds build` SKIPPED, and
  shared-code changes break the 3DS unseen.** The drift audit's `3603df2`
  widened the texture-cache key in `c3drender.cpp` at its two use sites but
  not at the map's declaration, so the 3DS did not compile from that commit
  until the M3 got a toolchain (fixed the same day). Run the check on a
  machine that HAS the toolchain after touching anything the 3DS shares.
* **The M3 needed three more toolchain fixes than the M1** (2026-10-06, all
  in the script now): `ftp.gnu.org` refuses connections from it
  (`OMK_GNU_MIRROR`); Homebrew's zstd is found by binutils' configure and not
  linked - "Undefined symbols `_ZSTD_compress`" (`--without-zstd`); and an
  INTEL Homebrew left in `/usr/local` by a migration put an x86_64
  `libgmp.dylib` first on devkitPro's hard-coded `-L/usr/local/lib`, the
  linker ignored it and never reached GCC's in-tree gmp - "libgmp not found
  or uses a different ABI" (the `/usr/local` paths are dropped). A failed
  stage leaves its build directory stamped `configured-*`: delete
  `~/.cache/omk-3ds-toolchain/src/buildscripts-*/.devkitARM/arm-none-eabi/<stage>`
  before re-running, or the fix never reaches configure.
* **AddressSanitizer is not the way on the M3**: its runtime deadlocks in
  its own initialiser on macOS 26 (`AsanInitFromRtl`, a spin lock, before
  `main`) - the process sits at ~80% CPU with an empty log for ever. UBSan
  alone plus `-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG` works
  (`make play OBJDIR=build/obj-hard PLAY_BIN=build/hard/omk-play
  CXXFLAGS=... LDFLAGS=-fsanitize=undefined`).
* **The licence census** counts every new `.h/.cpp` under `engine/backends`
  and `.sh` under `scripts/` - re-pin `licence headers` in `tools/verify.py`
  with each new file (its docstring has the current count; other sessions
  move it too).
* **zsh**: an unquoted `$A` holding several addresses is ONE argument -
  `${=A}` (a loop over it ran once and "resolved" every address to one
  function); and an inner `EOF` line ends an outer `<<'EOF'` heredoc - patch
  scripts went to files with every anchor asserted before writing.
