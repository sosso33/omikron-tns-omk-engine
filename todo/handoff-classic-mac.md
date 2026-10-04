# Handoff — the CLASSIC MAC (PowerPC) PORT (2026-10-03)

**Read this first to pick up the PowerPC Mac work.**
[`classic-mac-port-1999.md`](classic-mac-port-1999.md) is the plan and the
full record (the 1999 what-if, every finding with its evidence, sections 3a-i
to 3d-ii); this file is the state, the recipes and what to do next. It is
NOT a modern macOS port - OMK already runs on today's Macs.

---

## 1. Where it stands

* **`omk-play` RUNS ON MAC OS 9 AND TIGER through the Carbon frontend**
  (2026-10-03, `e08bc23`, plan 3d-iv): 30 frames of Anekbah's street start
  BYTE-IDENTICAL to the host on both. Software renderer, AUDIO through the
  Sound Manager (`8b536ad`: 72 s played in a 75 s run on Tiger; choppy on
  emulated OS 9, where a frame takes seconds), and nobody has driven it by
  hand. Getting there fixed three big-endian bugs in
  the viewer (the PowerPC Darwin build had them too), worked round Retro68's
  linker leaving weak `.bss` objects without storage, and `%zu`.
* **THE ENGINE RUNS ON MAC OS 9** (2026-10-03, `aa7aa43`, plan 3d-iii): the
  headless boot (`tools/omk.cpp`) as ONE Carbon binary (`make classic`),
  run on Mac OS 9.2 and on Tiger - its boot dump BYTE-IDENTICAL to the
  Mac's. It took `int32_t`-is-`long` fixes, a File Manager arm in `DataFs`
  with `omk::hostPath()` (HFS paths), no threads, and NO C++ STREAMS in
  `src/` (a Retro68 linker fault, trap below). `verify.py: engine: classic
  build` keeps it building and stream-free.
* **The viewer's game code reaches the host only through `omk::Frontend`**
  (the gateway, `12d77ec`), so a Carbon frontend can replace SDL.
* **OMK runs on Mac OS X 10.4 (Tiger) on PowerPC**, in QEMU (ppcosxkvm,
  emulated Radeon 9700): the films with sound, the menu, the intro, and the
  street **at 6-10 fps through the fixed-function OpenGL 1.x backend**
  (`engine/backends/gl1/`), against ~1 fps in software. Colours and sound
  confirmed by the reader.
* **Big-endian is done.** `formats/le.h` (`loadLE`, `writeLE`) at every raw
  read; every tool a `verify.py` check runs, 1928 distinct calls over 146
  tools, replayed on PowerPC: **1927 identical** to the Mac (the last is
  directory order). On the Mac all 1928 byte-identical before and after.
* **One Carbon binary runs on Mac OS 9.2.1 and on Tiger** (a Retro68 test
  program, 3d-i): C++20, exceptions and RTTI work; no `std::thread`, no
  `std::filesystem` (does not link), `%zu` prints "zu".
* **The fixed-function backend** follows the original's D3D path (one draw
  per bucket, every state cached, texture by `key & 0x3F`, 16-bit textures,
  the original's blends) and its CPU side: vertices transformed, clipped
  (near plane AND screen edges) and culled on the CPU, handed over already
  projected; fog per vertex. Against the software reference on the M3:
  coverage 0.9932, 153 px - `verify.py: engine: gl1 backend`.
* **ASSETS 4 corrected (2026-10-03)**: on a 3D card (driver mode 0, the
  default) the original FILTERS bilinear, no mipmaps; POINT is the software
  devices'. `render states` asserts both arms. The DEFAULT followed the
  same day, the reader's decision: bilinear on every GPU backend (4, item 1).

Commits, oldest first: `9cabf0e` PowerPC build, `0a4a33f` le.h,
`16f2ea1` every tool on PowerPC, `8287f8b` one Carbon binary, `2a7e025` the
game on Tiger, `db78df4` the emulator's colour bug, `523d530`
`AUDIO_F32SYS`, `d50cba8` the gl1 backend, `80c83f4` its window title,
`8dad555` screen-edge clip, `f826df6` its check, `86ef012` the filtering
correction. All pushed.

---

## 2. The tools - outside the repo, never named by it

Everything lives in an APFS sparse bundle on the external SSD (exFAT's
128 KB clusters made sources cost 10x). Its own `README.md` says what is
where and why; the build scripts in its `scripts/` are re-runnable.

    hdiutil attach /Volumes/Crucial_X8/decomp/tools/omk-devtools.sparsebundle
    # -> /Volumes/omk-devtools (must mount under THAT name: binaries refer to it)

| what | where on the volume |
|---|---|
| PowerPC cross-compiler (GCC 14.4, `powerpc-apple-darwin8`) | `toolchains/ppc-darwin8/bin` |
| Retro68 (classic Mac OS / Carbon, Universal Interfaces) | `toolchains/retro68/bin` |
| SDL 2.0.3 for Tiger (built IN Tiger, its Cocoa half is ObjC) | `sdl2-tiger/` |
| ppcosxkvm (Tiger VM, **locally patched** - see 5) | `emulators/ppcosxkvm/` |
| QEMU with Screamer sound, for OS 9 | `emulators/qemu-screamer/`, `scripts/run-os9.sh` |
| the 10.4u SDK (cross sysroot), MPW GM, Xcode 2.5 dmg | `sdk/` |
| the replay harness for the tool comparison | `work/bereplay.py` |
| the upstream ppcosxkvm report, DRAFTED NOT SENT | `upstream/ppcosxkvm-txo-endian/` |

OS install images: `/Volumes/Crucial_X8/decomp/tools/os-images/`.

---

## 3. Recipes

**Start Tiger** - DETACHED, or a host background command is killed at 2 h:

    cd /Volumes/omk-devtools/emulators/ppcosxkvm
    nohup ./ppcosx --ssh-port 2222 --ram 2048 > /tmp/tiger.log 2>&1 < /dev/null & disown
    ssh -F /Volumes/omk-devtools/vm/ssh/config tiger      # user qemudev, key auth

Shut it down from inside (`sudo shutdown -h now`), not by closing the window.

**Build** (from `engine/`; `ppc-darwin.mk` is this port's own makefile -
the shared `Makefile` is untouched by all of it):

    PATH=/Volumes/omk-devtools/toolchains/ppc-darwin8/bin:$PATH
    make -f ppc-darwin.mk                                   # 196 tools, build/ppc-darwin8/
    make -f ppc-darwin.mk SDL2_PREFIX=/Volumes/omk-devtools/sdl2-tiger play   # omk-play, gl1
    # PLAY_GPU=none for software only; OMK_GL1=0 at run time to compare
    # the same, for the MAC (what the check builds):
    make -f ppc-darwin.mk PPC_CXX=c++ PPCFLAGS=-ffp-contract=off LDFLAGS= OUT=build/host-gl1 \
         SDL2_PREFIX=/opt/homebrew PLAY_LIBS="$(pkg-config --libs sdl2) -framework OpenGL" play

**Run the game in Tiger** - it must go through `open` on the bundle
`~/omk/OMKPlay.app` (a launcher script): started over SSH it is not in the
desktop session, gets no window and exits silently. Data in `~/omk/fr`,
tables and traces in `~/omk/repo`, binaries in `~/omk/bin`. Extra
arguments go, one line, in `~/omk/play.args` (e.g. the street start
`--save repo/traces/save-appart.bin --area 0 --stand 1804,0,-6890,336 --nofmv`);
the log is `~/omk/play.log`; `screencapture -x` over SSH takes a screenshot.

**Copy into Tiger with `scp -O`** (or `ditto -c -k --sequesterRsrc` for
anything with a resource fork). Never a macOS `tar` of a sparse file: it
goes as a pax sparse entry and Tiger's GNU tar 1.14 unpacks a DIRECTORY.

**The engine for OS 9 / Carbon** (from `engine/`; `retro68 =` in `omk.conf`
lets `verify.py` find the toolchain too):

    make classic RETRO68=/Volumes/omk-devtools/toolchains/retro68   # build/classic/OMKBoot.APPL

`make classic` also builds `OMKPlay` (the viewer); add
`-DOMK_CLASSIC_TOOLS="ped_probe;..."` to the cmake call for tool apps
(`OMKTool_<name>`). **OMKPlay on Tiger**: its folder with `OMKPlay` and
`omk.args` (one argument a line: `Macintosh HD:Users:qemudev:omk:fr`,
`Macintosh HD:Users:qemudev:omk:repo:tables`, `--save`, `...:traces:
save-appart.bin`, `--area`, `0`, `--stand`, `1804,0,-6890,336`, `--nofmv`,
and `--frames 30 --res 640x480 --dump cplay.bin` for a comparison), launched
with `open ./OMKPlay` (LaunchServices puts it in the desktop session;
`LaunchCFMApp` from SSH gives a windowless run). Compare against the host
built by `ppc-darwin.mk` with `PPC_CXX=c++ PPCFLAGS=-ffp-contract=off
PLAY_GPU=none` - not `build/omk-play`, which contracts. **On OS 9**:
`untitled:omk` holds the data (595 MB - the boot's three folders plus
FONTS, I2D, ANIMS, SOUNDS, MAP2D, RADAR, TRAJECTOIRES and, since 2026-10-04,
TRACKS - without it the game has no music), `save-appart.bin`,
`OMKPlay` and `omkplay.args`; copy the last two into Startup Items (the
args as `omk.args`), boot, wait ~3 minutes, stop QEMU, read `play.bin` and
`omk-out.txt`, empty Startup Items.

`OMKBoot` reads its arguments from `omk.args` beside it, one per line, HFS
paths (`untitled:omk:fr`, `NOFMV`, `--tables`, `untitled:omk:tables`,
`--dump`, `boot.bin`), and writes `omk-out.txt`. **On Tiger**: `ditto -c -k
--sequesterRsrc` a folder holding it, `scp -O`, `ditto -x -k`, then
`/System/Library/Frameworks/Carbon.framework/Versions/A/Support/LaunchCFMApp
./OMKBoot` from its folder; arguments as `Macintosh HD:Users:qemudev:omk:fr`
etc. **On OS 9**: the data is already on its disk (`untitled:omk:fr` - IAM,
SCPTDATA, MESHES - and `untitled:omk:tables`, with `OMKBoot` and its
`omk.args` in `untitled:omk`); copy the app and `omk.args` into
`System Folder:Startup Items`, boot, wait ~3 minutes, stop QEMU, mount, read
- and take the app OUT of Startup Items again, or it runs at every boot.
A run WITHOUT `--frames` (audio on) never ends by itself there: put
`omk.quit` beside the app holding a number of seconds, and it quits after
that long and writes its closing lines (`audio: Sound Manager ... played`).
On Tiger, `osascript -e 'tell application "OMKPlay" to quit'` does it.

**Mac OS 9**: no shell. Put a program in the OS 9 disk's
`System Folder/Startup Items` (mount `vm/macos9.img` on the Mac with
`hdiutil attach -imagekey diskimage-class=CRawDiskImage`), boot with
`run-os9.sh`, have it write a file and call `FlushVol`, stop QEMU, read the
file off the image.

---

## 4. What is next, in the order proposed

1. ~~**The filtering DEFAULT**~~ - **DONE 2026-10-03**, the reader's
   decision ("the default should be what the original does"): every GPU
   backend, GL1 included, defaults to bilinear without mips (what a 3D card
   drew), the software reference stays POINT (the software devices). GL1 and
   GLES learned bilinear for it (`f2875ee`), the default flipped in
   `c7a5443`; `--filter nearest` gives the old picture. LOOKED at in Tiger
   the same day: the street start through the emulated Radeon 9700,
   bilinear, still 10 fps, no artefact seen in one still.
2. ~~**The engine on OS 9, headless**~~ (`aa7aa43`) and ~~**the Carbon
   frontend**~~ (`e08bc23`) - **DONE 2026-10-03**, above. **NEXT, in order**:
   (a) PLAY it by hand on Tiger and OS 9 - keys, the menu, walking; the key
   map is untested by a person; **2026-10-04, first look by the reader on Tiger**: the SDL
   build through the GL1 backend booted through the films toward the menu
   (rebuilt that day, the previous binary kept as `~/omk/bin/omk-play.1003`),
   and the CARBON `OMKPlay` at the street start (`~/omk/cplay/omk.args`; the
   boot's args are `omk.args.boot`) - "carbon build is running correctly":
   25 bodies, 200 walkers, adventure mode, Sound Manager audio, 67 frames in
   35 s (~2 fps, the software renderer on the emulated G4). **Walked in
   BOTH builds** (SDL/GL1 and Carbon), the reader: "controls felt
   correct" - (a) is done on Tiger; **OS 9 by hand, 2026-10-04** (the
   street start from Startup Items), the reader: the MUSIC played, STOPPED
   when he started running and never came back, while the sound effects
   (footsteps) kept playing. NOT the mixer: the OS 9 disk's `omk:fr` had
   no `TRACKS` folder, so `music switch to 2` found no file and the game's
   music never played at all; what was heard was the street's own scene
   sounds (`anekbah.sfx`, one-shots of 6.6-8 s at the area load), which end;
   the footsteps are one-shots too. TRACKS (145 files, 187 MB) COPIED onto
   that disk the same day, byte-identical re-run, the reader: "good now" - the
   music plays on OS 9 through the Sound Manager ring at emulated speed.
   So (a) is done on Tiger AND OS 9; (b) ~~Sound Manager audio~~ DONE (`8b536ad`) -
   left: the ring is six 100 ms buffers refilled between frames, so a frame
   slower than 0.6 s underruns (OS 9 under emulation); a bigger ring costs
   the interface blips latency, so measure on a real G3 before choosing;
   (c) ~~the films~~ DONE without QuickTime (`32784eb`): the vendored
   pl_mpeg decodes them on PowerPC and the Sound Manager plays their 44100
   track - Tiger drops 3-9% of frames once the offscreen matches the
   window's depth; emulated OS 9 plays all three but drops ~74%, and the
   films stretch to minutes because they pace by an audio clock that
   underruns there; (d) speed - the software renderer is ~2 fps on the
   emulated G4, and the gl1 backend is CGL (Tiger only): AGL for OS 9.
   Measure on REAL hardware first: emulated timing is not a G3's or a G4's.
   ~~**The gateway class**~~ - **DONE 2026-10-03** (`12d77ec`): the
   viewer's game code reaches the host only through `omk::Frontend` (the
   clocks, the window title, fullscreen, the display modes, text input and
   the error string joined the interface; `makeHostFrontend()` is the one
   function a frontend defines besides its class). 30 game files compile
   with no SDL header (`verify.py: engine: frontend gateway`, a poisoned
   `SDL.h`); the SDL side is `sdlfront.*`, `playgpu_*.cpp` and
   `playscene.cpp`. Proved by the 28-scene golden record and by building
   for the Mac, Tiger (run: the title through the gateway), the Vita and
   the software-only `INSTRUMENTS=0` variant. **What a Carbon frontend now
   implements**: `CarbonFrontend : omk::Frontend` and `makeHostFrontend`,
   plus a `playgpu_*.cpp` for its GPU window if it has one - `playgpu_gl1`
   is the model, since it presents on the CPU through the frontend. The
   play split had finished (S5/S6, 2026-10-02), so nothing was in flight.
3. **What is left on OS 9 after the frontend**: the memory budget (the
   partition is 192 MB preferred, 64 MB minimum - a guess, not measured);
   `%zu` is handled (`classic_printf.h`), audio and the films work. `DataFs` and no threads
   are DONE (2 above); with `OMK_THREADS 0` the voice read-ahead already
   runs on its frame, so "ticked loading" is a speed question, not a gap.
4. **Speed**: the 1999 budget (`classic-mac-port-1999.md` §3b, steps 1-2).
   Tiger's emulated G4 is not a G4's timing.
5. **A G3 build**: SDL 2.0.3 rebuilt without `-maltivec` (the binary is
   `ppc_7400` today).
6. **The ppcosxkvm report**: test the fix seriously (the checklist in the
   draft), and narrow the second artefact (below) to a minimal GL repro.

---

## 5. Traps that cost time

* **ppcosxkvm is patched locally** (`scripts/ppcosxkvm-r300-txo-endian.patch`):
  32-bit textures with TXO_ENDIAN 2 drew byte-reversed (black as BLUE) -
  SDL 2.0.3's window texture is the only one. `ppcosx update` discards it;
  re-apply, `ninja qemu-system-ppc`, then `scripts/bundle-dylibs.py` on it.
  `OMK_R300_TEXLOG=1` logs each distinct texture setup.
* **Still open, suspected emulation**: a wall grazed by the camera flashes
  FLAT for a frame in Tiger; the M3 never shows it (60-position sweep, worst
  169 px). Not cured by the screen-edge clip.
* **A CGL context on Tiger needs a DRAWABLE**, or nothing is drawn or read
  back, even into an FBO - the backend attaches a 16x16 pbuffer (a current
  Mac refuses pbuffers and goes on without).
* **Do not use the GPU's transform or fog on the emulated Radeon**: the
  first let near-plane triangles through as garbage, the second darkened
  the near scene. The backend does both on the CPU (as the original did).
* **`verify.py` gives every `omk-play` run `--res 800x600`** unless the
  check names a size - a dump of the wrong size first read as a bug.
* **`make` does not track flags**: after changing `PPCFLAGS`, delete the
  object directory, or the old objects are linked (it happened with
  `-ffp-contract=off`, which `ppc-darwin.mk` sets because GCC's fused
  multiply-add drifted a traffic run by 0.1 unit).
* **NO WEAK OBJECT WITH A CONSTRUCTOR** (an `inline` variable built at
  start-up, a static local in an inline function): Retro68's linker gives a
  weak `.bss` object NO STORAGE, so its constructor writes over its
  neighbour - `recursive_init_error` from a guard, or silent corruption
  (PORTING A10, `engine: classic build`). The symbol-table test: a
  C_WEAKEXT label whose BS csect has length 0.
* **`%zu` CRASHES on Retro68** (it consumes no argument; a later `%s` reads a
  size as a pointer) - `classic_printf.h` redirects the printf family; keep
  it force-included.
* **A run that "exits 0 and prints nothing"** is `std::terminate`: the
  classic entry now says so and faults on purpose, so Tiger's crash reporter
  writes the backtrace. Read it there.
* **Compare frames against a host built WITHOUT fused multiply-add**
  (`ppc-darwin.mk`, `-ffp-contract=off`); `build/omk-play` differs by float
  noise.
* **NO C++ STREAMS in anything the classic build links.** Retro68's linker
  lays libstdc++'s `num_get<char>::id` over `timepunct_cache_w`, so the
  locale's own set-up corrupts it and the first stream (even an
  `ostringstream`) crashes - but only in a LARGE program: a stream test
  passes alone, so a hello-world proves nothing. `src/` reads and writes
  files through `DataFs` (`readTextFile`, `writeWholeFile`); `engine:
  classic build` fails if stream symbols reappear. Bisecting the objects
  found it; a crash log's backtrace on Tiger
  (`~/Library/Logs/CrashReporter/<app>.crash.log`) names PEF symbols.
* **Retro68's `open()` takes an HFS path and opens read-WRITE**, and has no
  `stat`/`mkdir`/listing: everything goes through `DataFs` and
  `omk::hostPath()`. A '/' in a path is a character of a file name there.
* **Tiger's display sleeps after 10 minutes** and `screencapture` then writes
  nothing: `/tmp/omk-wake` in Tiger (a 3-line `UpdateSystemActivity` program)
  wakes it without root.
* **Tiger has no `seq` or `pgrep`** (`jot`, `ps | grep`); a host loop that
  greps for `done` can be fooled by a script that wrote one early.
