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
| SDL 2.0.3 for Tiger (built IN Tiger, its Cocoa half is ObjC) | `sdl2-tiger-g3/` - generic PowerPC, runs on a G3 (2026-10-04; the build tree is Tiger's `~/src/panther-sdl2-g3`); `sdl2-tiger/` is the first build, `-maltivec`, G4 only |
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
    make -f ppc-darwin.mk SDL2_PREFIX=/Volumes/omk-devtools/sdl2-tiger-g3 play   # omk-play, gl1, any G3
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

**OpenGL in the Carbon build** comes with `make classic` when the tools
volume has its two inputs - `sdk/MacOSX10.4u.sdk` (AGL's and OpenGL's
headers) and `sdk/os9-opengl/OpenGLLibrary`, Mac OS 9's library WITH its
resource fork, which `MakeImport` turns into the import stub (CMake cache
variables `OMK_MACOS_SDK` / `OMK_OPENGL_PEF` relocate them). Without them
OMKPlay is the software build, and `engine: classic build` says which. The
log names the path: `display: an AGL context on the window (accelerated)`
and `gl1: ATI Radeon 9700 OpenGL Engine, OpenGL 1.5 ...`.

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

## 3d. THE GL1 STRAIGHT PRESENT, RUN ON TIGER (2026-10-05) - and the iBook

`a98ca45` (`todo/cpu-vs-original.md` tier B): the AGL build draws the world
at its letterbox place in the window's back buffer and SWAPS it - no
readback, no CPU composite, no `glDrawPixels` - the interface blended over it
in fixed function on a frame that has one, the DRIVER's dither (the
reader's choice). `--gl1-composite` in `omk.args` keeps the old path.
Measured on the emulated Tiger (the street start, 640x480, `OMKPlay` in
`~/omk/cplay`, logs `out-direct.txt` / `out-composite.txt` there):

| present | sim+draw | readback | present | a frame |
|---|---|---|---|---|
| composite | ~47 ms | ~74 | ~95 | ~216 ms (4.6 fps) |
| straight | ~43 ms | 0 | ~0.7 | ~44 ms (~23 fps) |

EMULATED timing - the reader ordered an **iBook G3 500 MHz, 640 MB** (ATI
Rage Mobility 128, 8 MB; OS 9.2 and Tiger) to measure on: its numbers
replace these. (`screencapture` over SSH writes nothing, and the host may
not script the emulator's window - a person looks.)

**The OVERLAY, watched by the reader on Tiger (2026-10-05)**: the supermarket
fight (`--fight-supermarket`) - *"The UI was showing correctly"*. (Kay'l dies
at once there: `save-appart.bin` gives him Vie 10 and the harness presses
nothing; `--fight-health` for a longer fight.) Its first version re-sent the
whole band of interface rows every frame - `present` 41-58 ms an overlay frame
on the emulated Tiger; now a row keeps its place in the texture and is sent
only when its 565 row or mask row changed (a hash a row, the GLES
overlay's): the subtitle's frames **45-52 -> 2.7-4.6 ms**, the gauges ~42 ->
26. A FADE folded into the texture changed every row every frame (25-31 ms);
since 2026-10-05 it is a SECOND QUAD, untextured over the window at the fade's
colour and weight, `(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)` - `dst*(1-f) +
fade*f`, the GLES shader's law done by the blender: the same fight, same
frames (KO at 423, replay out at 511), `present` over the fade windows **26.0 /
25.0 / 31.0 -> 9.6 / 3.0 / 3.6 ms** (means of 60 frames; the first window
also carries the KO's gauges). Watched by the reader: see below.
Not run on OS 9 (its emulation has no 3D card).

## 3e. MAC OS X 10.2 JAGUAR - the reader's real PowerPC Mac (2026-10-09)

The reader ran the SDL `omk-play` (`ppc-darwin.mk`, Tiger target) from a
terminal on a real Mac under **Mac OS X 10.2**: `dyld: can't open library:
/usr/lib/libiconv.2.dylib`. Jaguar has no libiconv (Panther and Tiger do);
SDL 2.0.3's `SDL_iconv` imports it. It is only the first missing piece: the
Mach-O build targets 10.4 (`-mmacosx-version-min=10.4`, GCC 14's static
runtime against Tiger's libSystem, a Cocoa SDL port for 10.3+), so that
binary is not for Jaguar. The route for 10.2 is the CARBON `OMKPlay` (CFM,
runs on OS 9 and Tiger; Carbon runs on every Mac OS X) - handed over as
`engine/build/classic/jaguar/` (`OMKPlay` with its fork as `._OMKPlay` on
the reader's FAT32 stick, `OMKPlay.bin` MacBinary as the fallback, both
`make classic` of 2026-10-09 at `4b9d27d` + the profile fix, AGL inside;
`omk.args` with the relative paths `fr` / `tables`). **It RUNS on the
reader's iBook G3 under 10.2** (2026-10-09) - but "the UI are extremely slow
(even the city is faster)". Cause read in the code: a frame the world is not
swapped straight for (the menus, the films, a composite) went through
`CarbonFrontend::presentGL` as ONE `glDrawPixels` of the whole frame with
`glPixelZoom` - a software path in the period's drivers - while the city's
world is swapped straight and only its interface rows go up as a texture.
Fixed the same day: `presentGL` uploads the frame as RGB565 into a 16-bit
texture, only the runs of rows whose hash changed, and draws one quad.
Built and `engine: classic build` green; NOT yet run on any machine - the
reader's iBook is the test.

**WHITE SCREENS IN CUTSCENES, never in adventure mode** (the reader, same
machine, same day, on the build BEFORE the texture present). Not diagnosed.
Ruled out by reading: the fade colour (normalised to 0..1 in
`playframe_screens_huds.cpp`). Candidates: a frame forced onto the CPU
composite (`keep` = `cpu mirror`, `screen`...), whose `readback()` copies the
back buffer through a texture - on the Rage 128 that copy may fail and the
frame show the texture's undefined content; or a refused straight/overlay
present. The AGL build now LOGS its present path where it changes (`gl1
present: <straight | overlay (why) | why> from frame N`, and a one-time
`REFUSED` line), beside the existing `gl1: first readback ... centre pixel`
line: the next `omk-out.txt` with a white cutscene answers which.

## 3f. FASTER ON THE iBOOK - the reader's ask of 2026-10-09, IN STEPS

*"The current version is a good start but I think it could be optimized a
bit more (cpu/gpu). Could you, comparing with the original and also with
your own ideas, find ways to optimize it more?"* No iBook figure exists yet,
so the ranking comes from the host GL1 build (`verify.py: engine: gl1
backend`'s recipe, `OUT=build/host-gl1`) SAMPLED on the M1 (macOS `sample`,
Anekbah's street start, uncapped): of the main thread's work outside the
host-only readback and SDL blits, the GL1 `submit` and its vector inserts
were ~650 samples, the body posing (`composePoseAt`, `applyPose`,
`applyLights`) ~360, the draw lists ~50. On a G3 the submit weighs more:
it divided 6 times a triangle and 3 a vertex, and the 750 divides in 17-31
cycles without pipelining. The iBook's own split will come from the log's
`frame N sections (ms, mean of 60)` line, printed in every build.

**Step 1 - DONE 2026-10-09** (`gl1render.cpp`, `backends/classic/CMakeLists.txt`):
- **One draw call a run of equal state**, the original's mechanism (one
  `DrawPrimitive` a non-empty bucket, `Render_FlushBuckets`): the vertices
  are already clip-space with the fog folded into their colours, so only
  blend, alpha test and texture divide a batch. Street: **89 draws a pass
  -> 24 draw calls** (`gl1: 60 passes drew 5376 draws in 1466 draw calls`).
- **The wholly-inside triangle takes no clip**: each corner's outcode is
  the clip's own test, so all-inside comes out unchanged and all-outside-
  one-plane empty, decided without running the five clips.
- **No division**: the cull is the view-space determinant's sign, the
  position `(x/th, y/tv, z - 2 near, z)`; the fog's range a reciprocal.
- **One interleaved array** pushed per vertex (was four `insert`s of an
  initializer list - out-of-line calls in the sample).
- **The mirror on the card** (`drawMirrorScene`, the GLES/original order:
  reflection first, the room over it, no clear). Before, a mirror in view
  ran `drawWithMirror`'s CPU fallback - three passes, three readbacks, a
  composite, and the frame sent to the CPU present ("cpu mirror"): Kay'l's
  bathroom mirror, in the apartment where the opening cutscenes play.
  Probed on the host (scratch probe, Aapkayl, two cameras on `AP_mirror`):
  native against the fallback **68 and 12 pixels of 307200 differ by > 24**,
  the reflection ~25000 pixels; laid side by side, the same picture.
- **`-mtune=750`**: scheduled for the G3, instruction set unchanged.
- Measured on the M1: `world begin..end (submit, GL)` **0.75 -> 0.44 ms**
  (twice each); the street frame differs from the old one in 3 pixels.
  `engine: gl1 backend` green (coverage 0.9945, 145 px > 24 - it was
  0.9932 / 153), `engine: classic build` green, the Tiger GCC compiles the
  file clean. NOT yet run on the iBook.

**THE iBOOK'S FIRST MEASUREMENTS (2026-10-09, the 13:54 build, the reader's
run from the new game through the intro to the street, 3099 frames; the log
kept as `omk-out-1354.txt` on the reader's stick).** The card is an **ATi
Rage 128 Pro, OpenGL 1.1 ATI-1.3.28**. Means of 60 frames:
- **the start menu**: `present` **220-310 ms** a frame (`presentGL`'s
  RGB565 texture) plus `screens, hud` 52-175 ms (the menu's composing) -
  "the UI extremely slow", NOT fixed by the texture present;
- **a subtitle or a fade over the world** (`gl1 present: overlay`):
  `present` **25-57 ms**, against 0.1-3 straight;
- **the intro's scenes**: sim+draw 23-45 ms - `staged bodies` 8-20,
  `submit` 7-11, `world begin, set` a steady ~8 even with 7 draws a pass
  (on the emulated Tiger's street it is 0.2-0.5: not yet explained);
- **the street**: sim+draw 165-200 ms (~5 fps) - `submit` **65-99**,
  `pedestrians, traffic` **34-44**, `scripted motion: meshes placed` 13,
  `staged bodies` 14;
- one-off stalls: `audio` 95-137 (a voice line read and decoded on the
  main thread - no threads on the classic build), `game frame` 123-199
  (area loads), `controller` 81.
- the batching saved nothing in the intro (420 draws, 420 calls - every
  draw there a different texture); no `REFUSED`, no readback, no mirror
  line in the whole run, so no frame was ever composed on the CPU.

**Step 1b - DONE 2026-10-09, NOT yet run on the iBook**: measure what the
log could not split, and stop guessing the upload format.
- **`CarbonFrontend::probeUploads`**, run once when the AGL context is
  made: a 640x480 frame sent AND drawn with `glFinish` inside the time (a
  driver may defer an upload to the texture's first use), best of four, in
  RGB565, ARGB1555 and ARGB8888 (BGRA 8-8-8-8-REV, Mac OS X's own layout),
  and the overlay's RGBA bytes against ARGB8888; the presents then use the
  fastest the driver did not refuse. It logs the drawable's bits (thousands
  or millions of colours). On the emulated Tiger (Radeon 9700): **RGB565
  13.4 ms, ARGB1555 7.9, ARGB8888 7.3 -> ARGB8888; overlay bytes 7.5,
  ARGB8888 5.5 -> ARGB8888**.
- the overlay's changed rows sent in RUNS (one call a run, not a row);
- profiler zones `gl1: draw call` (the driver's share of submit),
  `gl1: overlay rows`, `gl1: swap`, `present: rows`, `present: swap`;
- the profiler looks for its `.ctl` every 30th frame while running (it
  opened a file every frame - on the iBook, from FAT32, inside the frame).
- Run on the emulated Tiger (the street, 240 frames): runs, exit 0,
  sim+draw ~40 ms as before. `engine: classic build`, `engine: gl1
  backend`, `engine: profiler` (3) green. The reader's stick now has the
  build and `--profile omk.prof` in `omk.args` (the old one beside it as
  `omk.args.old`); `python3 tools/omkprof.py omk.prof` reads the capture.
  The VM shuts down without a password by
  `osascript -e 'tell application "System Events" to shut down'`.

**What is next, ranked by what it should save on the G3 (each to be
confirmed by the iBook's `sections` line before it is started):**
2. **The bodies posed per VERTEX, not per corner** - the original's
   `sub_4947F0` transforms each unique vertex once (200 in PSH_FN against
   1143 corners); the port's `applyPose` turns position AND normal per
   corner, then `applyLights`. Or GL1 poses them itself (`posesBodies`):
   the bone folded into the camera, one 3x4 a mesh, so a body corner costs
   what a set corner costs. The second cost in the sample, ~360.
3. **The set transformed per VERTEX** (`Render_SubmitMesh` into its 75000 x
   48 pool): Anekbah is 139245 corners over 63079 vertices, 2.2 a vertex.
   Needs `cornerVertex`, which `dropCharacterArrays` frees for a set today
   (~0.5 MB in Anekbah, inside the 64 MB budget).
4. **The display at 16 bits** - no code: the original ran a 640x480 16-bit
   device, and a Rage 128 fills a 16-bit buffer about twice as fast. The
   reader can try "Thousands" in the Displays panel; FULL SCREEN at 640x480
   (AGL's own, as the original's DirectDraw exclusive mode) would be the
   code step after it.

**THE READER'S THREE FURTHER REPORTS (2026-10-09, same iBook)**, none yet
diagnosed past the reading below:
- **the launch is very long** before the window appears: the area 118 load,
  `Grid.SCX` (1.3 MB), the sneak's preview models, `aventure.scx` (3.0 MB,
  20 sprites and 53 sounds) all come BEFORE the window, read from the
  reader's USB stick - and the iBook's USB is 1.1 (~1 MB/s). The first log
  already has `set load: GRID - 131 KB read in 1069.9 ms ... soups 369.4`.
- **the intro films at ~0.5 fps**: the log's `GAME.mpg: 82 of 115 frames
  dropped`. Read: on the classic build `glRen` is null, so
  `playsetup_play.cpp` decodes into the DISPLAY-sized `mv` (the CPU 2x
  scale, `Movie::nextFrame`) and `presentGL` uploads all 640x480 rows a
  frame through the slow format - the menu's 220-310 ms present, every
  film frame. `presentGL` already stretches its texture to the window with
  GL_NEAREST, which is the same replication as the CPU's exact 2x, so
  handing it the 320x240 `film` surface should be pixel-identical at a
  quarter of the upload.
- **Mac OS 9 freezes the whole computer** (which binary and whether an
  `omk-out.txt` was written: not yet asked). The AGL path has NEVER run on
  OS 9 with a real 3D card (its emulation has none, 3d above); `delayMs`
  spins on `Delay(1)` and never yields - harmless on OS X, a hog under OS
  9's cooperative scheduler; and a line-buffered log is not on the disk
  after a hard freeze without `FlushVol`.

**Step 2a - THE FILMS THROUGH QUICKTIME, DONE 2026-10-09** (the reader:
*"use quicktime for video"*; NOT yet run on the iBook). `Frontend::filmOpen /
filmStep / filmClose` (`platform/frontend.h`, default "not taken"), tried by
`playsetup_play.cpp` before the software decoder with the same skip rules;
`CarbonFrontend` implements it: `EnterMovies`, `FSMakeFSSpec` on the
`hostPath`, `NewMovieFromFile`, QuickTime playing the sound itself. Under GL
the picture is drawn into a 32-bit GWorld at the film's 320x240 (a
drawing-complete proc says when) and sent as ARGB8888 - the GWorld's xRGB
word is BGRA 8-8-8-8-REV as it stands (RGBA bytes when the probe saw that
refused) - and the card doubles it GL_NEAREST; without GL QuickTime draws
into the window, fitted. Refused anywhere (no QuickTime, no MPEG importer -
OS 9 needs the QuickTime MPEG extension), the software decoder plays it as
before. CarbonLib's import stub carries QuickTime: no new library.
Emulated Tiger (QuickTime 7): **EIDOS 277 pictures in 13.2 s, QUANTIC 610
in 23.7, GAME 3065 in 108.2** - the whole films at nearly their own rate,
colours right in a screenshot, then the world's 22050 device, the splash and
the menu as before. `engine: classic build`, `engine: frontend gateway` green.

**THE iBOOK'S SECOND RUN (2026-10-09, the QuickTime build, from its own
disk, the display in THOUSANDS of colours)** and what it changed:
- the films through QuickTime at **~9 fps** (EIDOS 113 pictures in 13.0 s,
  GAME 126 in 13.8 s before a skip), against ~0.5 - not their own rate yet;
- the probe in 16 bits: **RGB565 262 ms, ARGB1555 63, ARGB8888 113** a
  640x480 frame -> the menus as ARGB1555; overlay bytes 223, ARGB8888 120;
- **THE WHITE SCREENS, the reader's diagnosis**: the WHOLE window goes white
  while a non-dialogue text is up - a subtitle (`media line`), the Bowie
  intro's credits, an examine line - and comes back when it goes; never a
  standard dialogue. What differs is the overlay's ALPHA: a dialogue box is
  opaque (alpha 0/1 only), a subtitle's glyphs lie on the world with partial
  alpha. Reproduced NOT on the emulated Tiger (`--area 222 --scene-chunk 55`,
  two subtitles: drawn right), so the Rage 128's own;
- **the letterbox bars blue-ish "like the sky"**: on a STRAIGHT frame the
  bars were the window's rows, cleared to the FOG colour.

**Step 2b - DONE 2026-10-09, NOT yet run on the iBook:**
- the overlay blended as the STANDARD premultiplied over -
  (GL_ONE, GL_ONE_MINUS_SRC_ALPHA) with alpha = 1 - M - in place of
  (GL_ONE, GL_SRC_ALPHA) with alpha = M: the same `C + world * M` to the
  bit, without a destination factor of source alpha. The likeliest cause of
  the white, NOT proven: `gl1: the overlay raised GL error` now logs any
  error the overlay raises, once each, and `--gl1-composite` (the CPU
  composite, no overlay) is the reader's A/B if it stays white;
- the bars cleared BLACK on AGL (`begin`, two scissored clears);
- the films into a 16-bit GWorld sent as ARGB1555 when the probe found that
  faster than ARGB8888 (the iBook in thousands of colours).
On the emulated Tiger: the Impasse's two subtitles drawn as before, bars
black, no GL error, exit 0; `engine: classic build` green.

**Step 2c - A LINE STREAMED AS IT PLAYS, DONE 2026-10-10, NOT yet run on the
iBook** (the reader's item 4, "go ahead with 4 then 3"). The iBook's second
log put a dialogue line's start at **0.2 to 1.1 s of ONE frame**: `line load:
125338 - read 210 ms (3424 KB), voice decode 30 ms, ... resample 551 ms` (and
125339's read 770 ms) - the whole `.3DM` read, its whole voice decoded, then
copied to floats. **The original** never does that: `Morph_Start` (0x0042C870)
calls `Morph_Open` (0x0042C300), which opens the file (`mmioOpenA`), reads the
header and primes the buffer, then `timeSetEvent(15, 0, fptc, ...)` reads and
decodes the next blocks every 15 ms while the line plays (`fptc` ->
`sub_42D960`). Now the port does the same, once a frame:
- `formats/morphstream.{h,cpp}` (new): the line opened (`HostFile`, a file
  kept open - new in `platform/datafs.h`, the same three platform arms as
  `readFileRange`), its header and FIRST SECOND read (30 records, ~115 KB),
  and `fillTo` reading the next records with one read and decoding their
  audio blocks through `AdpcmStream` (new in `formats/adpcm.h`; it carries the
  decoder's state across blocks, and `adpcmDecode` is now that stream called
  once, so the two cannot differ). The bytes land at a whole read's offsets in
  a buffer of the file's size;
- `DialogPlayer::tick` keeps the stream TWO SECONDS ahead of the line's
  clock; the line's length comes from the layout, before any decode;
- the voice is a sound that GROWS while it plays (`HostMixer::open/feed`,
  `Frontend::openSound/feedSound` on the SDL, Carbon and 3DS frontends): kept
  16-bit - no float copy, the iBook's 551 ms - silent where it runs short, ended
  only once whole. The viewer feeds it what is new every frame and extends the
  bone tracks (`extendNodeTracks`; `NodeTracks::built`), the latched copy too;
- `media.play` voice-overs kept 16-bit the same way (read whole: at most 190 KB);
- the threaded READ-AHEAD of 2026-09-23/30 is gone: it read every possible next
  line whole (up to four files of 3-4 MB) for one to be used, and a threadless
  build had none of it.
Measured: Telis's first line in the apartment, `opened and read 1 ms (the
first 30 of 174 frames, a 654 KB file)`, and with a device the voice plays to
its end (`dialogue: line '000000' at 5.84 of 5.81 s`). `engine: line stream`
(new, replacing `engine: line ahead`: five lines of 387 streamed, their
samples and bytes hashing as the file read and decoded whole; red when the
decoder is reset per block and when a fill reads a byte short), and `dialogue
play`, `dialogue`, `pose blend`, `voice over`, `morph+ADPCM`, `line sync`,
`stop sound`, `adpcm table`, `device sounds`, `frontend gateway`, `line
facing`, `audio`, `music storage`, `music ring`, `audio queue bound`,
`dialogue stands still`, `dialogue line states`, `vita build`, `3ds build`
green. **`engine: classic build` NOT run**: the tools drive was not connected.

**Step 2d - THE LAUNCH IN `Game_Main`'s ORDER, DONE 2026-10-10, NOT yet run
on the iBook** (the reader's item 3). The original's window exists before any
game code (`WinMain`), then `Game_Init`, the three films, `Game_Start
("aventure.scx")` - the 3 MB global library, read ONCE, after the films - the
5 s splash, and the first area in the frame loop. The viewer instead read
about 12.6 MB during its launch, 5 MB of it before the window: `setupBoot` ran a
whole headless `omk::boot` for its report line (`aventure.scx` read to count
its sprites and sounds, a Session built, area 118 loaded - all thrown away;
the iBook's log shows area 118 "loaded" twice), and `setupBodies` read
`aventure.scx` twice more, before the films. Now:
- the viewer runs no `omk::boot` (its line counts the films; `build/omk` keeps
  the headless boot, `engine: boot` green);
- `setupPlay` (the films) runs BEFORE `setupBodies` (the library), as in
  `Game_Main`: the films need only the window;
- `aventure.scx` read once, its bytes making both the runtime and the sprites
  (`PlayState::loadSpritesFrom`).
On the M3 the setup before the window went from 20.4 to 7.5 ms; the reads
before it from ~5 MB to ~2 MB (`Grid.SCX`, 1.3 MB, and the tables, 0.7), and
the launch's total from ~12.6 to ~7.3 MB, the library now behind the films.
NOT done: area 118 and its `Grid.SCX` are still loaded before the window
(the original loads them in its frame loop) - the window section takes its
size and settings from the middle of the 694-line `setupSession`, so moving
it means splitting that section first. `engine: intro`, `intro beat`, `boot`,
`movies`, `fight library`, `scene sprites`, `sprite table`, `sprite ids
scene-local`, `start menu quit`, `frame rate` green.

**Step 2e - THE FULL-FRAME UPLOADS THROUGH APPLE'S CLIENT STORAGE, DONE
2026-10-10, NOT yet run on the iBook** (the reader agreed to "the interface on
the card"; what was built instead, and why). The iBook's second log put a menu
frame at ~150 ms - `present, swap` 74 (the 640x480 upload: 63 ms as ARGB1555,
113 as ARGB8888 in the probe), `screens, hud` 45 - and an overlay frame's
`present, swap` at 40-185. **The interface drawn as card quads would not have
removed the menu's upload**: the start menu's background is the CLOUD
(`ui/cloud.h`), embossed and then resampled through a per-row AND per-column
displacement every frame on the CPU - which the original does on the CPU too
- and OpenGL 1.1 has no way to draw that warp exactly, so the menu would
still send a whole frame. What every slow frame shares is THE UPLOAD, so that
is what this step attacks:
- `probeUploads` now prints the driver's `GL_APPLE_client_storage`,
  `GL_EXT/ARB_texture_rectangle` and `GL_APPLE_texture_range`, and where they
  are offered times the frame sent as CLIENT STORAGE (the texture's memory is
  ours, the card reads it there - no copy into the driver), then as a
  RECTANGLE texture (640x480, not 1024x512), then with the SHARED storage
  hint; the conversion is inside the time, written straight into the
  texture's memory;
- the fastest is used by the three full-frame paths: `presentGL` (the menus,
  every screen without the world), the GL1 overlay (`gl1OverlayPath()` in
  `gl1host.h`; ARGB8888 words) and the films (`filmShow`: QuickTime's GWorld
  IS the texture's memory - the period's own QuickTime-under-OpenGL recipe;
  the texture is deleted before the GWorld). The client memory is rewritten
  only after a `glFinish`, once a frame and only when a row changed;
- each falls back to the copy where the driver offers nothing.
On the emulated Tiger (Radeon 9700): all three extensions offered; ARGB8888 a
frame 18.3 ms as client storage, 12.7 as a rectangle, **2.0 with the shared
hint**, against 3.4 for the best copy - so the menus, the overlay and the
films all took client storage, rectangle, shared; the splash, the menu (30
fps), a film (EIDOS 290 pictures in 12.9 s, against 277 before) and the
Impasse's subtitles over the world drew right in screenshots. **Whether the
Rage 128 under 10.2 offers these at all is what the next run says** - the
`gl1: upload extensions` line and the three `gl1: the ... will be sent`
lines. `engine: classic build`, `engine: gl1 backend` green.

**Steps 2f-2i - ONE BATCH FOR ONE iBOOK RUN, DONE 2026-10-10, NOT yet run on
the iBook** (the reader: "to test everything in one time instead of several
small tests"; each its own commit and log line):
- **2f, the texture pool kept** (`2a374c4`): the frame hands the pool over
  again whenever it changes (a sprite, a character in view) and the GL1
  backend deleted and re-sent EVERY texture, a call a texel - the likeliest
  owner of the iBook's `props, guns` (12-65 ms). Now a texture already on the
  card (same pixel storage, palette, size) is kept, as the GLES and 3DS
  backends and the original's 58-slot cache keep it; new ones go up as 16-bit
  ARGB1555 words through the palette. Vulkan did the same and leaked its
  white fallback on every change (`engine: profiler gpu` 41 -> 40); fixed too.
  Log: `gl1: texture pool N, S slots - K kept on the card, U uploaded`.
  `engine: gl1 backend` asserts the street's second pool keeps 31 and
  uploads 8 (red with the cache broken).
- **2g, Mac OS 9 diagnosable** (`c35c083`): `checkpoint:` lines, each flushed
  to the disk on OS 9 (its File Manager caches writes), before the window,
  the AGL pixel format / context / drawable, the upload probe, the sound
  channel, QuickTime and each film, and the first presented frame; the log
  flushed every 2 s on OS 9; `system: Mac OS x.y.z` said first; `delayMs`
  yields through `WaitNextEvent` with an EMPTY mask instead of spinning on
  `Delay`. **Run on the emulated OS 9.2.1** (QEMU, Apple's software
  renderer): every checkpoint written, the probe finding no Apple upload
  extension and keeping the copy, the menu at 100-180 ms a frame - and the
  log read off the disk after QEMU was KILLED at frame 479 ran to frame 479:
  the tail a hard stop used to lose is there.
- **2h, the menu's cloud for a slow CPU** (`76e8f73`): the same pixels (menu
  dumps byte-identical at four frames and two sizes, a one-bit mutation
  changes them), the emboss's clamp and ramp one lookup, its rows fetched
  once; profiler zones `ui: cloud emboss` / `ui: cloud warp` so the next
  profile splits the menu's ~45 ms.
- **2i, a third less vertex data without the fog sum** (`016459f`): a pass
  whose fog is black (Kay'l's apartment and most interiors) sends a 28-byte
  vertex, not 40; host-GL1 dumps byte-identical for the street, the Impasse
  (coloured fog) and the apartment (the new path). The SQUARE ROOTS were
  read and left: on the per-frame path each one's VALUE is used (falloff,
  speeds, normals), the one pure reach test already compares squared, and the
  original itself has 190 `fsqrt` and compares the root
  (`if (sqrt(dx*dx + dy*dy + dz*dz) < limit)`).
The whole batch on the emulated Tiger: the street at 29 fps drawn right,
every checkpoint, pools 31 -> 31 kept + 8 uploaded, exit 0.
(`system:` reads 10.4.9 there: `gestaltSystemVersion` stops at .9.)

**What the iBook's next run should bring back**: `omk-out.txt` and
`omk.prof`. Read first: `gl1: upload extensions` and the three `will be
sent` lines (2e), `gl1: texture pool` (2f), the `sections` lines of the menu
and the street against the second run's (above), a dialogue `line load:`
(2c), and the profile's `ui: cloud *` and `gl1: draw call` zones.

**THE iBOOK'S THIRD RUN (2026-10-10, steps 2c-2i, the machine now on Mac OS
X 10.3.9)** - the reader: *"White screen issue is not resolved"*. What the
log (`omk-out.txt`, 3330 frames, exit 0) and `omk.prof` say:
- **the white is the MEDIA LINE overlay and nothing else**: every white
  stretch the run passed through is a `gl1 present: overlay (media line)`
  span (the Impasse's *"Te voila enfin"* from frame 1637, the robot's
  notice from 1931, the four tutorial lines of AREA 142, the credits'
  *Eidos Interactive* / *Quantic Dream*); `overlay (conversation)`, `(colour
  fade)` and `(black fade)` frames came through, and NO `GL error` line and
  no `REFUSED` line was written;
- **so step 2b's reading was BACKWARDS**: a subtitle is opaque text drawn
  over the key with NO box (`SubBox::None`), so its overlay alpha is 0 or
  255 only and no plane row is touched - it is the CONVERSATION's box (the
  planes' affine pass) that carries partial alpha, and that draws right.
  The blend change could not have mattered;
- the overlay texture is 1024x512 RGBA8 = 2 MB; the profile's GPU memory is
  3.25 MB in AREA 118 (the conversation) and 5.5 MB in the Impasse (28
  texture slots + the overlay), 8 MB in Anekbah's street - against the
  iBook's Rage 128 Mobility's 8 MB with ~1.8 MB of window buffers. A lead,
  not a cause: the white covers the WHOLE window while the overlay quad
  covers only the band of rows that differ from the key;
- upload extensions: client storage YES, rectangle and texture range no; the
  menus as ARGB1555 through client storage, 49.4 ms in the probe (85.1 sent
  the old way);
- a line's start: `opened and read` 40-50 ms (from 0.2-1.1 s); the first
  line's frame still 650 ms (`audio 323`, `game frame 199`);
- the texture pool kept: 192 kept / 43 uploaded over ten pools;
- the street: 85-180 ms a frame - `world begin..end (submit, GL)` 28-78,
  `pedestrians, traffic` 14-37, `scripted motion: meshes placed` 8-12;
  `props, guns` down to 0.4-6;
- the profile: `gl1: overlay rows` ~26 ms an overlay frame, `ui: cloud
  warp` ~20 ms and `emboss` ~7 ms a menu frame.

**Step 2j - THE WHITE-SCREEN DIAGNOSTIC, 2026-10-10, for the iBook's fourth
run** (temporary - it goes when the fix lands). Each new media line is drawn
by the next of four variants, `gl1: media line N drawn as variant V (...)`:
0 as always (RGBA8 blended), 1 without blending (an alpha test - the same
picture while the alpha is 0/255, a subtitle's), 2 through a 16-bit ARGB1555
texture of its own, 3 NOT drawn (the world alone). The first frame of every
present path and every line, and the 15th after it, logs `gl1 overlay
probe`: the band and its texels by kind, `glAreTexturesResident` for the
overlay and the world's textures, and three back-buffer pixels BEFORE the
swap (centre, a text texel, a key texel). On the intro's path the lines come
in order: *Te voila enfin* 0, the robot's notice 1, *Je me demande ce
qu'etait* 2, *Je me demande ou* 3, then 0, 1 (the tutorial lines), the
credits 2, 3. What the reader notes: which lines went white, and whether the
text showed on the white.
**A lead the probe found on the emulated Tiger (`--area 222 --scene-chunk
55`)**: in a LETTERBOXED cutscene the band is rows **0..479** - the two
64-row bars are opaque black in the composed frame (81920 texels), so the
overlay quad covers the whole window; a quad whose texture the card fails to
sample comes out the vertex colour, WHITE, over everything - the reader's
symptom. Without a letterbox (AREA 142's lines) the band is the text's rows
alone (414..460). On the emulated Radeon the overlay texture reads `NOT
resident` (client storage keeps it in system memory) and `glReadPixels`
returns zeros, as it did before (3d) - both to be read on the Rage 128.

**THE iBOOK'S FOURTH RUN (2026-10-10, the 2j build, 4512 frames, exit 0) -
THE CAUSE.** `glReadPixels` WORKS on the Rage 128 (it returned zeros only
under emulation), so the probe read what the card drew before each swap:
- variants 0 (RGBA8 blended) and 2 (16-bit blended): the band's KEY texels
  read **255 255 255** - a transparent texel drawn white - letterboxed or
  not (the tutorial lines' band 414..465: centre, outside the band, the
  world; key texel at (0,439) white);
- variant 1 (no blending, alpha test): key texels the world (148 173 148,
  41 49 49), text 255 - right; variant 3 (not drawn): the world - right;
- the one CONVERSATION (frame 283, AREA 118): blended RIGHT (centre 16 33
  41) - and AREA 118's fog is BLACK, while every white line was under a
  COLOURED fog (the Impasse 162 197 210, AREA 142, the credits' street);
- `overlay texture NOT resident` on every probe, the right frames included:
  not the discriminator (client storage keeps it in system memory).
The white fades the probe also read (frames 272, 563, 2958...) are the
game's own: `Screen_FadeFromColor(0xFFFFFF, ...)` and ops 118/119 fade from
WHITE.

**Step 2k - THE FIX, 2026-10-11: the fog's colour sum ends with the world.**
`begin` enables `GL_COLOR_SUM_EXT` (and the secondary colour array) for a
coloured fog - the per-vertex fog is the secondary colour added after
texturing - and NOTHING turned it off: the overlay's 2D state disabled fog,
depth, cull, alpha test, scissor and lighting but not the colour sum. So the
overlay quad (and the fade quad, and the frontend's menus/films after a
world frame) had the CURRENT secondary colour added - which after a vertex
array is undefined by the spec: the Rage 128 kept the last fog colour, so a
transparent texel became fog colour + the world and saturated to white; a
text texel was white anyway, which is why variant 1 (the key texels dropped
by the alpha test) looked right. The emulated Radeon left it zero. Now
`Gl1Renderer::end` turns the colour sum and its array off (and `begin` turns
them on again), and `presentOverlay`'s 2D state turns it off too. The four
variants are gone; the probe stays until a run confirms (it now reads its
pixels by `glCopyTexSubImage2D` into a 16x16 texture and `glGetTexImage`, as
`readWindow` does, so it reads on the emulated Radeon too). `engine: gl1
backend` green (the world frames unchanged).
**REPRODUCED ON THE EMULATED TIGER** by setting the current secondary colour
to white before the overlay (`glSecondaryColor3fEXT(1, 1, 1)` - what the Rage
128 keeps; a temporary build, not committed), the Impasse
(`--area 222 --scene-chunk 55`): WITHOUT the fix every probe read
255 255 255 in the letterboxed band, and on AREA 142's line (band 414..460)
the centre the world (31 59 74) with the text and key texels white - the
iBook's fourth run to the pattern; WITH it the same frames read the world
(7 7 4, 92 105 102, key 12 7 3...). Both binaries checked different by md5
before running (the first rebuild of the pair had come out IDENTICAL: GNU
Make 3.81's one-second timestamps, CLAUDE.md 1 - `sleep 2; touch` fixed it).

**Step 3a - GL1 POSES BODIES, DONE 2026-10-11, NOT yet run on the iBook**
(the reader: *"continue with the optimizations in the meantime"*). The
fourth run's street (frames 3800-4500, 146 ms a frame) by the profile and
the `spans` lines: `world: mirror` self 35 ms (the submit's CPU, and
whatever the first clear waits), `gl1: draw call` 23, `world: crowd` 21,
`input: motion` 12; spans `motion corners` 8-11 + `motion patch` 10-13 (the
moving set meshes placed corner by corner and patched into the set's buffer
every frame), `vehicles` 9-16, `player` 9-12 (it also covers the draw lists),
`ped bodies` 3-10. So GL1 now answers `posesBodies()`: a posed draw comes
as REST corners and one affine a mesh (`Draw::meshPose`), and `submit`
folds the camera into each mesh's affine once a draw - a corner is
transformed ONCE, rest to view - and applies the `vertexLights` per corner
with `applyLights`' law on the posed normal (`maxVertexLights` 32), from
the scene's grey where `lightsFromBlack`: the GLES shader's rule. The
walkers, the vehicles, the player, the moving set meshes and the sky take
that path. Also in `submit`: a triangle's positions and outcodes first, its
colour work only once it is neither wholly outside (two in five of a street
frame's triangles) nor facing away. And a zone `gl1: clear` around the
frame's first clear, to tell the iBook's fixed 7 ms of `world begin, set`
(waiting for the card?) from work.
**A fault on every CPU-posing backend found on the way**: the player's
NORMALS were turned by his bones but not by his facing (`worldCrowd`'s
placement loop rotated positions only) since his lighting arrived (L1,
2026-10-06), so the set's lights fell on him as if he faced the way his
model is authored - GL1's posed player differed from its CPU one by ~7800
pixels; with the normal turned, 57 (`engine: gl1 backend` now renders a
fourth frame with `--cpu-bodies`: at most 0.1% of the pixels, 8 by more
than 24; shown to fail with the posed light on the rest normal - 8366, 1218).
`street frame`, `actor lighting`, `gles pose`, `per-pixel lighting`,
`traffic frame`, `pose equivalence` green. On the emulated Tiger (Anekbah,
`--density 0`, the same 90 s, the 2k build against this one): `sim+draw`
27-29 -> 22-24 ms; spans `motion corners` 3.0 -> 0.1, `motion patch` 3.3
-> 0.3, `player` 3.7 -> 0.8, `vehicles` 0.6 -> 0.1; the submit unchanged.

**Step 3b - THE INTERFACE OVERLAY'S FIXED COST, DONE 2026-10-11, NOT yet run
on the iBook.** The fourth run's 977 overlay frames (a subtitle, a dialogue,
a fade): `gl1: overlay rows` median **14 ms with nothing changing** (p90 66
ms when rows changed), `present` 34 ms an overlay frame, of which ~8 ms its
own. The floor was scanning: every row of the band HASHED every frame - in a
letterboxed scene all 480 - after another whole-frame scan for the key, and
the glue zero-filling the mask of every untouched row. Now:
- each row is COMPARED word by word with the copy last seen (pixels and
  mask): an unchanged row costs that compare, its band flag and texture row
  stand; exact, where the hash was only nearly so (`gl1: overlay compare`);
- the glue flags the rows a blended pass touched (`maskRow`, as GLES's) and
  writes an untouched row's zero mask once, when it stops being touched;
- ONE QUAD A RUN of rows that hold something: rows of nothing but the key
  (the world through) are not drawn, nor a letterbox bar's row that is opaque
  black and nothing else - `begin` cleared the bars black already. A
  letterboxed subtitle's quad went from rows 0..479 to the text's own 16
  (437..452); a fade with no interface draws only the fade's quad. Rows are
  uploaded only where drawn.
On the emulated Tiger (the Impasse, `--profile`): compare + rows 0.7 ms an
overlay frame against the hash's 1.0, the probes' text and world pixels as
before; `present` stays ~21 ms there whatever the overlay does (the emulated
driver's own cost) - the iBook's figure is the one to read.

**THE iBOOK'S FIFTH RUN (2026-10-11, the 3a build `6c12852`, 6672 frames,
exit 0)**: the Impasse, AREA 142, Anekbah's street (frames ~3110-4980), a
hall, the apartment and a FIGHT (frame 6312, `fight hud` overlay):
- THE WHITE SCREENS ARE GONE: every `overlay (media line)` probe reads the
  world behind its key texels (24 16 8, 41 33 8, 33 16 0...), the text
  texels their own colour; the whites left are the game's own white fades;
- the STREET 146 -> **120 ms** a frame (1700 frames, median 119):
  `world: crowd` 21 -> 4.1, `input: motion` 12 -> 1.8 (step 3a); the
  submit's own CPU (`world: mirror` self) 35 -> 39 (it now poses), `gl1:
  draw call` 23.6; `gl1: overlay rows` 11.4 in the street (HUD/subtitles);
- `gl1: clear` **0.09 ms**: the CPU does not wait for the card at the
  frame's start - the fixed ~7 ms of `world begin, set` is `world`'s own CPU
  work outside every zone, still unplaced;
- the FIGHT **187 ms** a frame (320 frames): `gl1: overlay rows` **74 ms**,
  `present` self 28, `screens: huds` 18, `gl1: swap` 11 - the two gauges
  down the screen's edges spark every frame, so every row they span changed
  and was sent WHOLE.

**Step 3c - THE OVERLAY SENT BY BLOCK, DONE 2026-10-11, NOT yet run on the
iBook.** A changed row's changes are tracked per 32-pixel column block (at
most 32 a row): the compare finds which blocks differ, and each block's run
of stale rows goes up as one rectangle (`glTexSubImage2D` at its x) - the
fight's sparks are two narrow strips, not the band's whole rows. A line
`gl1 overlay: over 300 frames N rows drawn and M rows' worth of texels sent
a frame` says what it costs. The Impasse on the emulated Tiger: the probes'
pixels as before; a subtitle standing, 42-66 rows drawn and 0.0-0.2 rows'
worth sent a frame. (The 3DS's overlay - `3ds-port.md` 6.7, which first
replaced GL1's hash by a kept copy, the fix GL1 itself got only in 3b -
still sends a changed row whole; the same block tracking applies there.)

**Step 3d - A VERTEX ONCE, AND INDEXED DRAWS, DONE 2026-10-11, NOT yet run on
the iBook.** The street's submit was ~39 ms of CPU (`world: mirror` self) and
`gl1: draw call` 23.6 - and the Rage 128 has no transform unit, so the
driver's vertex work is the CPU's too - with every triangle's three corners
transformed and sent apart: Anekbah's set is 139245 corners on 63079
vertices, Kay'l 1626 on 272. Now `indexFor` builds, once per geometry and
size, which corners are one vertex (every byte equal, one mesh - the 3DS
backend's `VertexIndex` rule), and within a draw a corner whose vertex was
transformed takes its view position and outcode, and once sent in the batch
its INDEX: the batch goes up as `glDrawElements` (16-bit, OpenGL 1.1, so OS 9
has it). A corner shares only while its bytes still equal the computing
corner's - a stale index loses sharing, never the picture. Colours are now
computed for a sent vertex only. Host GL1: the street (renderer-posed and
`--cpu-bodies`), the apartment (its mirror) and the Impasse BYTE-IDENTICAL
to the unindexed backend; per 60 street passes 1415922 corners, **659816
transformed, 389772 vertices sent** (713000+ before). Emulated Tiger street:
`sim+draw` 25-29 -> 19-23 ms, the submit's own CPU 16.5 -> 11.9.

**THE iBOOK'S SIXTH RUN (2026-10-11, the 3d build `75042b3`, 6329 frames,
exit 0)**, the fifth's route (the Impasse, AREA 142, Anekbah, the hall, the
apartment, the fight at 5995):
- the STREET 120 -> **113.6 ms** (median 108.5): `gl1: draw call` 23.6 ->
  20.3 (the driver's share of the indexed draws), but the submit's own CPU
  38.9 -> **41.9** - the vertex cache did not pay on the G3; overlay frames
  `gl1: overlay compare` 8.7 ms a street frame (0.7 calls);
- the FIGHT 187 -> **191 ms**: `gl1: overlay rows` 74 -> 55, `gl1: overlay
  compare` 19.5 (new), so nothing gained - though the stat line says only
  **7.4 rows' worth** of texels went up a frame;
- per 60 street passes the set was indexed (139245 corners, 63079 vertices).
**THE LESSON: THIS MACHINE IS MEMORY-BOUND.** The compare reads the frame
AND a kept copy - 1.2 MB a frame - and costs more than its arithmetic could
(~170 MB/s: the iBook's bus); the vertex cache kept six arrays (~1.9 MB at
the street) indexed at random, a cache miss each, and lost what the
transforms saved. On the M3 and the emulated Tiger neither shows. And the
fight's 55 ms with 7.4 rows' worth sent is the CALLS (a spark row a
rectangle, a block at a time) and the `glFinish` before writing the
client-storage texture - not the conversion.

**Step 3e - FEWER BYTES, DONE 2026-10-11, NOT yet run on the iBook** (the
sixth run's lesson): the overlay's compare reads the frame ONCE - two
checksums a 32-pixel block of a row (a*31+w and a rotate-xor, 64 bits a
64-byte block) against the ones it had, no kept copy (-0.9 MB of memory too);
the stale blocks go up in a FEW RECTANGLES (each block's stale drawn rows
as one span, neighbouring blocks merged) instead of a spark row a call, a
`gl1: overlay wait` zone around the `glFinish` before the client memory is
written, and the call count in the `gl1 overlay:` line; the vertex cache as
ONE 32-byte record a vertex instead of six arrays. Host GL1: street and
apartment byte-identical; emulated Tiger: the Impasse's probes as before,
0-0.2 upload calls a frame.

**THE iBOOK IS 640 MB / 8 MB VRAM (the reader, 2026-10-11), AND THE GAME'S
OFFICIAL MINIMUM AND RECOMMENDED SPEC IS 4 MB OF VRAM.** So the original
fitted 58 texture slots and a 640x480x16 front, back and depth buffer (~1.8
MB) into 4 MB. Read 2026-10-11, how: every texture surface is created
`DDSCAPS_TEXTURE | DDSCAPS2_TEXTUREMANAGE` (`sub_463310`, `sub_463420`, the
DDSURFACEDESC2's `ddsCaps` 0x1000 / 0x10) - D3D MANAGED textures, the
runtime keeping a system copy and paging them into video memory - AND the
textures are DOWNSCALED to fit: `sub_45F4F0` (0x0045F4F0) asks
`GetAvailableVidMem` for that caps pair, needs `(dword_80B060 + 5) << 16`
bytes (`<< 17` when `dword_53ADE4 == 1`), and while the scale's product
times that exceeds the memory, halves `flt_6A5388` or `flt_6A5384` (the
larger first) - the texture's height and width factors, which `sub_463420`
applies by creating a smaller surface and stretch-blitting the 256x256 into
it. NOT YET READ: what `dword_80B060` counts (the slots?), what
`dword_53ADE4` is (32-bit textures?), `dword_53ADEC`'s squaring of the two.
So on a 4 MB card the original drew SMALLER textures; the port's GL1 sends
every slot at 256x256 16-bit (6 MB at Anekbah) plus a 2 MB RGBA8 overlay,
and Mac OS X's GL pages them as D3D's manager did. Porting the rule -
the scale chosen from the card's memory (AGL's `AGL_VIDEO_MEMORY`) - is
what the original does on this machine: DONE as step 3f, below.

**Step 3f - THE ORIGINAL'S TEXTURE SIZE RULE, DONE 2026-10-11 (the reader:
"port the original's rule"), NOT yet run on the iBook.** Read to the end:
`dword_80B060` is the slot count (`D3D_AllocTextureSlots(58, ...)` from
`SetMaterialsMemory(58, 0)`), `dword_53ADE4` is 1 when the texture format is
16 bits a texel (`dwRGBBitCount == 16`, so a 256x256 is 128 KB: `<< 17`),
`dword_53ADEC` the device's SQUARE-ONLY cap (D3DPTEXTURECAPS_SQUAREONLY in
the triangle caps, `sub_463610`), and the memory compared is the FREE
memory after the screen buffers; the width factor `flt_6A5388` is the one
halved first. So the need is 63 x 128 KB = 7.9 MB: a 4 MB card drew 128x128,
an 8 MB one 128x256. The Carbon frontend now asks AGL for the accelerated
renderer's `AGL_VIDEO_MEMORY` / `AGL_TEXTURE_MEMORY` and subtracts the
window's back and depth buffers (`gl1TextureMemory`, logged as `display:
video memory ...`), and GL1 halves as the original does (`gl1: texture size
- ... textures at WxH of their size`), each texture uploaded at its scaled
size by nearest sampling (the stretch blit's - a reconstruction) with the
texture matrix on the logical size. `--full-textures` keeps them whole (an
enhancement). A host with no answer keeps them whole: the street
byte-identical. `OMK_TEXTURE_MEMORY=bytes` stands in for a card:
`engine: gl1 backend`'s fifth frame at 5.5 MB -> 0.5x1, 33.5% of pixels
changed, coverage 0.994; shown to fail with the height halved first. On the
emulated Tiger the card answers 124 MB: whole. The port's own interface
texture (2 MB) and the menus' (1 MB) are not in the sum, as the original
had neither.


**Step 3g - THE FIGHT'S OVERLAY GLUE AND THE TITLE, DONE 2026-10-11, NOT yet
run on the iBook.** The sixth run's fight frame: `present` 112 ms, of it 27.7
its own (the glue and the quads), and `screens: fps` 3.8 ms a frame - the
once-a-second window RETITLE with the rate, which the window server redraws
(~20 ms on the iBook; 5 fps puts it in every fifth frame). Now:
- `OverlayPlanes::rowBlocks` (`ui/overlay.h`, shared): `row()` records which
  32-pixel block of the row a pass touched; the GL1 glue resolves only those
  blocks (an untouched pixel stays the key - the same transparent texel the
  planes' c = 0, m = 255 gave) and zeroes a block's mask once when it stops
  being touched - the fight's gauges touch ~300 rows a few blocks each;
- `Frontend::cheapWindowTitle()`: the Carbon frontend says no, so its title
  carries the rate only under `--fps`;
- zones `gl1: overlay glue`, `gl1: overlay draw` (the quads, apart from the
  uploads) and `frame: pace` (the frame-rate wait, which `present` held).
`gles overlay`, `fight hud`, `subtitle box`, `frontend gateway`, `classic
build`, `gl1 backend`, `frame rate` green; the Impasse on the emulated Tiger:
the probes as step 3e's.

**Step 3h - RUNS OFF THE SCREEN SKIPPED UNREAD, DONE 2026-10-11, NOT yet run
on the iBook.** The street's submit was the iBook's largest cost (~42 ms of
CPU, sixth run) on a machine bound by its bus, and two in five of a street
frame's triangles lie wholly off the screen - each read (48 bytes a corner)
and transformed before its outcodes rejected it. Each geometry of 4096+
corners now keeps a bounding sphere a run of 4 TRIANGLES (`clustersFor`,
kept per geometry and revision, the dirty list re-fitting only its runs, the
caches dropped on the geometry-gone notice), and a run whose sphere lies
wholly beyond the near plane or a screen edge - with a margin - is skipped
without reading its corners: every triangle in it carries that plane's bit,
so the picture is the same. Posed draws excluded (rest corners). Runs of 32
triangles skipped 95040 corners over 60 street passes on the host, 16:
185436, 8: 248580, **4: 298980** (a fifth). The street, the apartment and
the Impasse BYTE-IDENTICAL; `engine: gl1 backend` renders the street with
and without (`OMK_GL1_NO_SKIP=1`) and requires both - shown to fail with the
radius halved. The emulated Tiger reads its memory through the M3 and shows
no gain (submit 11.92 -> 11.86 ms with 285792 corners skipped): the iBook is
the measure. The GPU backends transform on the GPU from resident buffers,
so the skip does not apply to them.

**THE iBOOK'S SEVENTH RUN (2026-10-11, the 3h build `cb96d6f`, 7399 frames,
exit 0)**, the same route:
- `display: video memory 8192 KB, 6624 KB for textures by the driver (the
  screen's buffer 1536 KB), back and depth 1200 KB -> 5424 KB free` and the
  original's rule: **128x256** - as predicted for the 8 MB card;
- the STREET 113.6 -> **103.7 ms** (median 99.1): the submit's own CPU 41.9
  -> 38.5 (the runs skipped), `gl1: overlay compare` 8.7 -> 4.9, `gl1: draw
  call` 20.3 -> **20.1** - the textures fitting the VRAM did not speed the
  draws: the rule is the original's look, not a speed-up here;
- the FIGHT 191 -> **135 ms**: `gl1: overlay draw` (the QUADS) **42.9 ms** a
  frame against `gl1: overlay rows` (the uploads) 1.1 and `overlay compare`
  8.9; in the street the same draw is 0.75 ms. So a draw of the 2 MB client
  overlay texture after it CHANGED is what costs - the driver moving it whole
  to the card again - and the fight's sparks change it every frame; `gl1
  overlay:` 14-25 rows' worth sent a frame in 0.6-1.4 calls.

**Step 3i - THE OVERLAY IN TILES, DONE 2026-10-11, NOT yet run on the iBook.**
The seventh run's fight: drawing the overlay after it changed cost 42.9 ms a
frame (0.75 in the street, where it rarely changes) - the driver sending the
whole 2 MB client texture again. The overlay is now 64-row TILES two blocks
wide (64 pixels at 640), a texture each, made the first time a tile holds
something; a change re-sends only its tiles (the stale blocks with client
storage), a tile with nothing drawn is neither sent nor drawn, and a tile's
stale blocks wait for it to be drawn. The emulated Tiger: the Impasse's
probes as step 3g's; the supermarket fight's HUD frames draw the gauges
(band 18..454) with the world through a transparent texel of tile 0; `gl1
overlay:` 21 rows' worth in 3.9 calls a frame. Not for GLES or the 3DS:
the Vita copies into the texture memory itself and the PICA reads its
overlay from linear memory - no driver sends a whole texture there.

**Step 3j - THE FRAME'S CLEAR DEFERRED, DONE 2026-10-11, NOT yet run on the
iBook.** `world` held ~7 ms of the iBook's frame outside every zone; on the
host the same phase's own time is ~0 (7 of 1749 samples), its one direct
cost a `__bzero`: `phaseWorld` cleared all of `fb` every frame (614 KB at
640x480) before knowing how the frame would leave. Now (shared viewer code,
every backend): a frame with no world clears it whole; an OVERLAY frame
writes the key and the zeros in one pass (they were two); a READBACK frame
clears only the rows its picture does not land on (a viewport screen, all);
a STRAIGHT frame, which nothing reads, not at all (`fbStale`), and
`phasePresent` clears it if that present is refused after all; the VR path
clears it whole. 12 over the present paths green - `gpu present`, `fight gpu
present` (every GPU frame against its CPU composite), `gles overlay`, `gles
backend`, `gl1 backend`, `vulkan pose`, `street frame`, `traffic frame`,
`frame hold`, `fight hud`, `subtitle box`, `classic build`.

**A READER'S SCREENSHOT, 2026-10-11, NOT REPRODUCED**: the host GL1 window
(from a sampling run whose window took the keyboard) showing the street with
a dark flat wedge - the clear colour - at its bottom left, where the ground
near the camera should be. Not this session's skipping or vertex sharing:
skip against no skip is byte-identical at 36 viewpoints (three spots, twelve
facings) and along a walk of 400 frames, which also matches the build from
before both. The viewpoint is unknown - asked the reader for one (`P` in the
viewer prints the camera as flags). The reader: it came from one of THIS
session's host runs (the GL1 window is not hidden by `OMK_HIDDEN_WINDOW`, and
both sampling runs took keyboard input - the player walked 84 and 209 units,
the second ending at 1949, -7041 facing 61 with the skip on). Searched
further: 40 more viewpoints along that path (skip = no skip, all), and a
900-frame scripted wander at density 0 dumped every 30 frames in three modes
- skip, no skip, the software reference: skip = no skip in all 29, GL1
within a few dozen strongly-differing pixels of the software frame (one
frame 822: a moving sign and the fire's particles, timing), no clear-colour
wedge anywhere. NOT reproduced; candidates left: the shared per-mesh cull
(`cullMesh`, the original's own sphere radii - 3776 meshes ship a radius
smaller than their extent, CLAUDE.md 4) with the camera close to a ground
mesh, or something the run's keys toggled.

**Step 3k - THE DEPTH TIE ON GL1, DONE 2026-10-11, NOT yet run on the iBook.**
The reader, running Anekbah through the host GL1 window: *"flickering
texture on the ground"*. GL1 had NO depth tie (`bakeDepthTie`), which Vulkan
and GLES have had since 2026-09-30: of two coincident faces the engine's
strict test on its quantised depth shows the one drawn first. Now GL1 bakes
the losers once per set with the shared `DepthTie` and draws a loser two
16-bit depth steps back (`4/65535 w` in clip z), sending its own vertices
(a shared one is at the plain depth); `gl1: 60 passes ...` counts the losers
drawn back. Anekbah: 87 losers baked, ~2 drawn a frame along the wander; the
host's frames unchanged (its offscreen depth is 24-bit and the same face
pair in a fixed order resolves alike) in the frames dumped - but **the
reader, running the street again in the host GL1 window with it: "issue
resolved"** (2026-10-11). So the ground's coincident faces DID fight on the
host, at views the dumps did not hit - the reading "not the flicker the
reader saw" was wrong. On the iBook's 16-bit depth it matters more. `gl1
backend`, `classic build` green.

**Step 3l - THE MENUS' PRESENT IN TILES, DONE 2026-10-11, NOT yet run on the
iBook.** The seventh run's menu frames (5-60) cost 88.8 ms, and half was
`present: rows` - 44.9 ms - where `CarbonFrontend::presentGL` sends an
interface-only frame as ONE 1024x512 client-storage texture. The start
menu's cloud (`ui/cloud.h`) changes every row below the title band every
frame, so a run of changed rows was nearly the whole frame, and the Rage's
driver sent the whole texture (padding included) as it had for the overlay
before step 3i. Now the frame is 64x64 TILES, a texture each, the overlay's
shape: one sum a row a tile column (`overlayBlockSums`) says which tiles
changed, only their changed rows are written into their memory and only
they are sent. Emulated Tiger, the start menu for 11100 presents: `present:
... 10x8 tiles, 58.9 a frame of 80` - the 20 tiles of the title band stay
put, the 60 sent are 640x384 texels against the 1024x512 before (47% of
the bytes); the picture right, no seam at a tile edge. The other backends
were checked and need nothing: the Vita's GLES present already sends only
changed rows into unified memory, the 3DS copies an interface frame on the
CPU, and the SDL GL1 build presents through SDL. On the iBook read
`present: rows` in the menu frames and the `present:` line's tiles a frame.
The cloud itself (`ui: cloud warp` 13.2 ms, `emboss` 4.6) is the menu's
next cost.

**Step 3m - NO CLEAR UNDER THE MENU'S CLOUD, DONE 2026-10-11, NOT yet run on
the iBook.** Step 3j left a frame with no world clearing all of `fb` - 614
KB of zeros, each line read in before it is written on the G3 - and on a
menu frame the cloud then writes every pixel again (`MenuCloud::draw` at
640x480, `drawScaled` at any other size): the seventh run's menu frames
carried 6.7 ms of `world` self. `phaseWorld` now skips that clear when the
cloud will cover the frame (`PlayState::cloudCoversFrame`: a screen open
with no player, the cloud loaded, the composer's table holding the screen -
the one early return before the cloud), and `screensHuds` settles the debt
before the composer: if the cloud is not drawn after all it clears there and
logs `the cloud was expected and not drawn`. Shared code, so every backend
has it. Proved byte-identical: the start menu at 640x480 (frames 40, 97),
800x600 and 1024x768, and the name dialog after ENTER, from the build before
and after; and a MUTATION filling `fb` with 0x1234 where the clear is
skipped left all three frames identical while counting the skip on every
frame (97, 60, 140) - the cloud covers whatever is under it. **Its first
build broke PORTING A10** - `cloudCoversFrame` inline in `playstate.h` with
a local static, a weak object Retro68's linker left without storage;
`verify.py: engine: classic build` caught it after that build had already
gone to the stick, and the next copy (with step 3n) replaced it. The
helper is in `playframe_world.cpp` now.

**Step 3n - THE STREET'S SUBMIT: SPLIT IN THE PROFILE, AND THE SHARED
CORNERS NOT RE-READ, DONE 2026-10-11, NOT yet run on the iBook.** The
seventh run's Anekbah frames (3200-3480) spent 103-115 ms, ~77 of them in
`world: mirror`: 29.5 in `gl1: draw call` and **47 of SELF**, which is the
whole GL1 backend between its draw calls - the submit loop, `begin`, and
`end`'s `glFlush`, where a driver with no hardware T&L (the Rage 128) may
do its vertex work. The profile could not say which, so three zones now do
(`gl1: begin`, `gl1: submit` - the draw calls nest inside it - and `gl1:
end`), and the `gl1: 60 passes` line, printed ONCE at pass 120, now
repeats over the last 60 of every 600 passes, so a run that reaches the
street minutes in reports it (host, Anekbah: ~25 draw calls, 20k corners
read, 8.9k transformed, 7.1k vertices sent a frame).
And one saving on the bytes: of a street frame's 20k corners ~11k are a
vertex already transformed in the draw, and each still read its own 48
bytes and its representative's to confirm the vertex index's word
(`sameCorner`, a `memcmp`) - ~0.5 MB a frame on a bus that reads ~170 MB/s.
The index now keeps a byte a corner, "may have changed since it was
built", set from the geometry's dirty lists as its revisions pass
(`dirtyFrom`/`dirtyTo`/`dirtyCorners`, the run clusters' protocol), or for
every corner on a revision with no list (logged once for a large
geometry); two corners both still as built are equal by construction, so
the compare runs only when one is in doubt. Host, Anekbah: 589246 of the
604k shared corners in 60 passes taken without the compare, no geometry
ever all in doubt over 600 frames; the transformed and sent counts
unchanged to the vertex, and the frames byte-identical to the build
before at the street (frames 200 and 600, the traffic patching the set)
and the Impasse (300). On the iBook read `gl1: submit` / `gl1: end` self
against `gl1: draw call` in the street, and the repeated `60 passes` line.

**Step 3o - WHOLE LINES WITHOUT A READ, THE CLOUD'S EMBOSS INTERLEAVED, THE
CORNERS PREFETCHED, DONE 2026-10-11, NOT yet run on the iBook.** Three G3
habits. (1) A store to a line not in the cache makes the G3 READ the line
first, so a pass writing a frame whole moves every byte twice:
`platform/cacheline.h` claims the whole lines of such a range with `dcbz`
(zeroed in the cache, no read) - the cloud's warp and its emboss rows, the
frame's clear (`clearFrame` -> `zeroBytes`), the overlay frame's key fill
and the readback's row copy. The line `dcbz` clears is MEASURED once on a
scratch buffer (`cache: dcbz clears 32-byte lines` in the log; a wrong
size would zero bytes outside the range), and off a PowerPC all of it is a
`memset` or nothing. (2) The emboss was ~35 cycles a pixel for 21
instructions - a chain the G3 waited on link by link; two pixels an
iteration give it two chains (32 instructions for the two), the arithmetic
of each unchanged; the warp likewise. (3) The GL1 submit asks for the
corners of the triangle two ahead (`__builtin_prefetch`, `dcbt` on a
PowerPC): the G3 has no hardware prefetch. Byte-identical menu frames on
the host at 640x480, 800x600 and 1024x768; emulated Tiger measured 32-byte
lines and drew the menu, the Impasse and a subtitle over it right; `gl1
backend`, `classic build`, `menu cloud`, `licence headers` green (the
census 601: `texscale.h`, missed by its own commit, and `cacheline.*`).
On the iBook read `ui: cloud warp` / `emboss`, the menu's and the
conversation's `world` self, and `gl1: submit`.

**FURTHER CANDIDATES, listed for the reader 2026-10-09 (none started):**
GL1 posing bodies and moving meshes itself (`posesBodies`, one camera x
mesh 3x4 a mesh: GL1 answers no today, so the street's moving meshes are
placed per corner and patched into the set every frame - `scripted motion:
meshes placed` 12-14 ms on the iBook); the set and bodies per VERTEX (2, 3
above); the G3's missing `fsqrt` (the 750 has no square-root instruction:
every `sqrt` is a library call - squared-distance tests where a distance is
only compared); float<->int conversions per vertex (`toByte` x3 in `emit`,
a store-and-reload each on a 32-bit PowerPC - colour bytes kept per corner,
one fog weight a vertex); the vertex stride 40 bytes with the fog's 12
sent even when there is no fog; the films through QuickTime (what
`todo/classic-mac-port-1999.md` says a period port would have done) or
straight YCbCr to the upload format; the boot's loads after the window is
up (step 2d above, all but the first area); the voice lines decoded in pieces across frames (the `audio` stalls) - DONE,
step 2c above.

## 3g. THE SDL BUILD ON ITS WINDOW - the reader's iBook G4 (2026-10-27)

**The report.** The reader ran the SDL `omk-play` (`ppc-darwin.mk`) on an
**iBook G4 1.42 GHz, 512 MB, Mobility Radeon 9550**: 6-8 fps, on a machine
that runs heavier games than the original. Its `--profile` capture (954
frames, the street at 640x480): median **182.6 ms** a frame, of which
`world: mirror`'s OWN time 135.4 ms - and the section after `world end,
submit` 136 ms: the GL1 backend's `readback()`. The SDL build drew the world
offscreen (an FBO on a context of its own, a 16x16 pbuffer as its drawable)
and every frame read it back with `glReadPixels(GL_RGBA, GL_UNSIGNED_BYTE)` -
the CPU waiting there for the card to finish, then the bytes through the
driver - dithered it on the CPU and presented it through an `SDL_Renderer`.
The simulation and the submit were ~25 ms; the mirror is not involved
(`gl1: begin` 0.9 a frame - one pass, no CPU-mirror fallback). Only the AGL
build had the straight present (3d).

**The fix, two steps**, both measured on the emulated Tiger (ppcosxkvm,
Radeon 9700, the street start at 640x480 with the 30 fps cap on, `--profile`,
100 s each; `OMK_GL1_OFFSCREEN=1` on the same binary for the old path):

1. **The world on the SDL window** (`OMK_GL1_SDL_WINDOW`, set by
   `ppc-darwin.mk`'s `play`): the window carries an OpenGL context
   (`backends/gl1/gl1sdlhost.cpp`) instead of an `SDL_Renderer`, the GL1
   backend takes that context and every AGL path is its path - the world
   drawn into the back buffer at its letterbox place and swapped, the
   interface blended over it (the overlay), a read through a texture where a
   frame must be composed. A frame composed on the CPU (a menu, a film, a
   screen that reads the picture) goes up as one ARGB8888 texture, only the
   band of changed rows, and is fitted at its aspect. Decided at RUN TIME
   (`Gl1Renderer::onWindow_`): no GL window (`SDL_VIDEODRIVER=dummy`, which is
   how `engine: gl1 backend` runs) keeps the offscreen target. The straight
   present needs the window 1:1 with the frame (`gpuWorldOnWindow`); a
   scaled window - the SDL build's fullscreen - is composed on the CPU and
   fitted, i.e. still pays the readback. Median **76.0 -> 46.1 ms**; the
   readback 42 ms -> 0, the present 5 -> ~1 ms.
2. **The roof ray through the meshes' boxes.** Step 1 made the build a
   GPU-window build, which runs the CITY WEATHER (`weatherFrame` is called
   only inside `if (gpuWindowBuild())`, see the bug below): its roof ray
   (`sub_481200` -> `sub_444810`) cast `sweepSphere` over EVERY face of the
   shot soup each frame - `world: weather` 8.9 ms with no rain falling. The
   original walks only the meshes whose box meets the ray
   (`o3de_ForEachMeshInBox`); `omk::soupRunBoxes` gives each mesh's run of
   the shot soup a box (`WorldSlot::shotRunBoxes`), and the bolts, the fight
   and swim cameras and the slider ride take it too. The same faces through
   the same test in the same order: `engine: shot run boxes`, 88000 rays over
   the 220 sets, 0 differing, 15% of the faces tried. `world: weather` **8.9
   -> 0.05 ms**; the frame median **46.1 -> 32.2 ms - the 30 fps cap** (5.2 ms
   a frame now waits in `frame: pace`).

**Looked at in Tiger**: the street, the start menu (composed) and a fight's
subtitles over the world (the overlay), colours right. NOT yet run on the
iBook G4: emulated timings are indicative only (the emulated readback was 42
ms, the G4's 135).

**The emulator's byte order again (5, ppcosxkvm).** The composed frame first
went up with a plain `glTexSubImage2D` of BGRA 8-8-8-8-REV words and drew
black as BLUE on the emulated Radeon, though `OMK_R300_TEXLOG=1` shows the
texture bound with TXO_ENDIAN 0, which the local patch reverses - so the
emulator gets the plain copy's bytes wrong some other way. Through Apple's
CLIENT STORAGE (the card reads our memory, as SDL 2.0.3's own renderer sends
its window texture, which drew right in Tiger and on the reader's G4) the
same words draw right, and the menu's present fell 6.5 -> 3.9 ms. The SDL
host's overlay takes client storage for the same reason
(`gl1OverlayPath`). `OMK_GL1_NO_CLIENT=1` sends both by copy again.

**A BUG FOUND, FIXED THE SAME DAY** (the reader's call: "fix the weather
issue in software renderer"): `weatherFrame` ran only in GPU-window builds
(`playframe_world_draw.cpp`, inside the `gpuWindowBuild()` gate block since
`36b0d00`), so the software-only `omk-play` (no Vulkan), the classic build
without AGL and the offscreen GL1 build never had the city's rain or snow.
The call now stands before that block, in every build (the Vulkan and GLES
builds, where it already ran, are unchanged). `engine: weather` gained a leg
with NO GPU window - the host `ppc-darwin.mk` gl1 build under the dummy
driver with `OMK_GL1=0` - asserting the ramp's 29 drops at frame 90 draw
streaks; with the call put back inside the block that leg reads `None` while
the Vulkan legs stay green.

**Next**: the reader's G4 run (`--profile` again, windowed 640x480, and
fullscreen now that 3h scales on the card).

## 3h. THE GL1 FULLSCREEN, SCALED ON THE CARD (2026-10-27)

The reader's "fix then fullscreen mode". Until now the straight present (3d,
3g) needed the window to be the frame's own size; the SDL build's fullscreen
(`SDL_WINDOW_FULLSCREEN_DESKTOP`: the desktop's mode, the frame scaled into
it, `sdlfront.cpp`) and any dragged edge sent every frame to the CPU
composite - the readback again. Emulated Tiger, 1024x768 fullscreen over a
640x480 frame, the street: median **108.7 ms** a frame, `world: mirror`
(the readback) 76 ms of it.

**The fix.** The world and the overlay are still drawn at the frame's size,
in the back buffer's bottom-left corner (where every coordinate in `begin`,
`readWindow` and `presentOverlay` already put them, measured from `h_`).
Before the swap, `Gl1Renderer::fitToWindow` copies that corner into a
texture (`glCopyTexSubImage2D` - OpenGL 1.1, the readback's own texture
reused, no CPU read), clears the window black and draws the texture over it
fitted at its aspect and centred, NEAREST - as the composed frame is fitted
(`sdlGl1PresentFrame`), so a frame changing path keeps its look.
`gpuWorldOnWindow` takes any window that HOLDS the frame (as large or
larger); a smaller one still goes to the composite. Result, emulated Tiger
1024x768 fullscreen: **32.1 ms**, the 30 fps cap; `gl1: fit to the window`
0.25 ms a frame.

**The emulator draws it in the wrong colours.** The emulated Radeon samples
the copied texture with its bytes out of order, in every format tried:
RGBA8 and RGB8 come out red (R saturated, black kept), RGB5 in the classic
byte-swapped 565 colours. The copy itself is right - the readback reads the
same copy with `glGetTexImage` and gets the right colours - so it is the
emulator's sampler (the same family as the TXO_ENDIAN flaw of 3g). On this
Mac (Apple M1, OpenGL 2.1 Metal) the fit is exact: `engine: gl1 window fit`
(a hidden 1400x960 window over a 640x480 frame, `OMK_GL1_WINDOW`; the fit
probe `OMK_GL1_FIT_PROBE` reads the window back) - all 1228800 picture
pixels the corner's nearest pixel, all 115200 band pixels black; shown to
fail with the quad's t coordinates swapped (1336 matching). The real G4's
Radeon is expected to draw it right; if it does not, `OMK_GL1_NO_FIT=1`
composes scaled frames on the CPU as before.

**Next**: the reader's G4 run fullscreen (`--fullscreen --profile`).

## 3i. MAC OS 9 TESTED AGAIN, AND THE LAUNCH GUARD (2026-10-29)

The reader: *"test mac os 9 version to avoid performance issue (low fps,
excessive ram usage, ...) or freezes (it happened at start the last time I
tested on an ibook g3), fix it if needed"*. No log of that freeze exists in
the tree: the iBook's OS 9 run was the `jaguar/` build of 2026-10-09, before
the checkpoints (2g), so it could not have said where.

**Run on the emulated Mac OS 9.2.1** (QEMU, `make classic` at `c925e0b` + this
work, Startup Items recipe 3; the OS 9 disk's `omk:fr` now also holds MORPH,
VOICEOFF, IMAGES and HASHCODE.TAB, so it is the whole data, as on the
reader's stick):
- **the default launch** (`omk.args` = data and tables only): every
  checkpoint passed, the three films (EIDOS by pl_mpeg - QuickTime 5.0.2
  refused it with -1404 - QUANTIC and GAME through QuickTime), the splash,
  the start menu running; **no freeze**. 19.1 MB of C++ blocks at the peak.
  Frames ~0.45 s, almost all `present, swap`: Apple's SOFTWARE GL renderer,
  the only one the emulation has - not a G3's figure;
- **the street start** (Anekbah, 25 bodies, 200 walkers, 90 frames):
  through OpenGL peak **42959 KB** of live C++ blocks, at most **43713 KB
  of the partition** (lowest FreeMem 53560 KB of 97274, MaxBlock 53545 -
  almost no fragmentation); through the software renderer (`--software`)
  40490 KB and 42387 KB. **Against 2026-10-04's 79886 KB / ~82 MB: the
  street is now INSIDE the 64 MB budget**, with ~20 MB to spare.
- a MEASUREMENT FIX: `classic_heap_sample` ran in `present`, which a world
  frame swapped straight by GL1 never calls - the street sampled FreeMem once
  in 90 frames. It runs in `pump` now, every frame on every path.

**So the emulator does NOT reproduce the freeze**: it has no 3D card, and
what is left untried on OS 9 is exactly the iBook's own - the ATI Rage 128's
ACCELERATED AGL path (pixel format, context, drawable, the upload probe, the
first GL frame), on real QuickTime and Sound Manager hardware.

**The fix that does not need the cause: THE LAUNCH GUARD**
(`backends/classic/launchguard.{h,cpp}`, OMKPlay only). On OS 9 each
`checkpoint` also writes its step into `omk.guard` beside the application
(flushed); the guard is removed 15 s after the last checkpoint and at a
clean exit; the launch itself arms it before the window ("the launch - the
session and area 118"). A guard found at the next launch means the last run
stopped in that step: its log is kept as **`omk-freeze.txt`** (the new run
would overwrite `omk-out.txt`), and the step is appended to **`omk.safe`**,
read at every launch - `opengl` adds `--software`, `quicktime` sends the
films to pl_mpeg, `sound` opens no Sound Manager channel; any other step is
reported and nothing skipped. Deleting `omk.safe` tries every step again.
Mac OS X never writes a guard. **Shown on the emulated OS 9**: a planted
guard naming the upload probe + a fake `omk-out.txt` -> `omk-freeze.txt`
holds the fake, `omk.safe` reads `opengl - the run stopped at: the upload
probe ...`, the run said so, went `--software` and exited 0 with no guard
left; and an emulator KILLED at 200 s left `omk.guard` naming the step its
log ended in (the upload probe), truncated right over the longer launch line.

**For the reader's iBook**: `engine/build/classic/ibook/` - `OMKPlay` (its
resource fork as `._OMKPlay` on a FAT32 stick; `OMKPlay.bin` MacBinary the
fallback) and `omk.args` (`fr`, `tables`). If OS 9 freezes, restart and
launch again: the second launch skips the step and `omk-freeze.txt` names
it - that file (and `omk-out.txt`) is what to bring back.

**The reader's iMac G3 refused to LAUNCH it (2026-10-30)**: OS 9's alert
"L'application OMKPlay n'a pu être ouverte car OpenGLLibrary--
glSecondaryColorPointerEXT est introuvable". The import stub is MakeImport's
over the 9.2.2 disk's `OpenGLLibrary` (OpenGL 1.2.1); an older OS 9 OpenGL
has no `glSecondaryColorPointerEXT`, and CFM refuses an application with an
unresolved strong import before a line of it runs (no log, no guard). It was
the build's ONLY EXT import (the fog's colour sum). It is now looked up by name
(`GetSharedLibrary` + `FindSymbol`, `gl1render.cpp`'s
`findSecondaryColorPointer`). Not found means no colour sum: the fog fades
without its colour, which the game's black fog never shows. The PEF's loader
section confirms it: 61 OpenGLLibrary imports, none of them
`glSecondaryColorPointerEXT`. Every launch logs `gl1: EXT_secondary_color
... by the renderer, glSecondaryColorPointerEXT found|NOT found in
OpenGLLibrary`. The rest of the imports are OpenGL 1.1 core and AGL 1.0. **Rule: no
extension entry point is linked on the classic build; look it up.** `verify.py: engine: classic build` reads the PEF's imports and holds it
(shown to fail: the function imported again, 62 imports, red).

**The iMac G3 with the fix (the reader, 2026-10-30; an iMac DV SE, 400 MHz,
384 MB, 8 MB VRAM - the Rage 128 VR)**: "No error". It
LAUNCHES, but **the start takes more than a minute**, and **after the films
the window stays WHITE** while the game runs: the sound plays and the menu
reacts to keys. So the menu's frames are composed and sent, but nothing of
them reaches the screen, while the films did. A white frame is what GL draws
from a texture the card refused (texturing then falls back to the plain
colour, white). **Diagnosed from the iMac's `omk-out.txt`**:
`Rage 128 OpenGL Engine, OpenGL 1.1.ATI-5.69`, 8160 KB of video memory, and
the upload probe read `RGB565 -1.0, ARGB1555 -1.0, ARGB8888 -1.0 -> the
menus as RGB565`. Every 16- and 32-bit menu format uses OpenGL 1.2 (the
packed types `GL_UNSIGNED_SHORT_5_6_5` / `_1_5_5_5_REV` /
`GL_UNSIGNED_INT_8_8_8_8_REV`, and `GL_BGRA`), the driver refused all three,
and the probe fell back to RGB565 anyway, so every menu tile was sent in a
refused format. The films (QuickTime, `sent to the card as RGBA bytes`) and
the overlay (`RGBA bytes 96.3`) drew; the menu ran under the white (`frame
229: start menu: Quitter -> Oui`, `exit 0`). The world's textures
(`gl1render.cpp`, `GL_BGRA` + 1555_REV) would have been white too. Fixed:
  * the menus: `kUiFmts` gains a fourth format, `RGBA bytes` (`GL_RGBA`,
    `GL_UNSIGNED_BYTE`, stored as `GL_RGB5`). It is probed ONLY when the other
    three are all refused, so a 1.2 driver's pick (the iBook's) is unchanged.
    With nothing accepted, the pick is now RGBA bytes, not RGB565;
  * the world: the first texture upload tries the packed 1555; refused
    (`glGetError`), every texture from then on is widened to RGBA bytes
    (`gl1: the driver refuses OpenGL 1.2's packed 1555 texels`). On the host
    with the bytes path forced, `engine: gl1 backend` stayed green (146 pixels
    off by > 24, 0.05%). Red and blue swapped in the widening turned it red
    (49427 pixels, 16.09%), so the check reads that path.
The menus' path shown on the emulated OS 9.2.1 with `uiFmt_` forced to RGBA
bytes (a temporary line, removed): the start menu drawn whole, title and four
entries, in QEMU's screendump. That run ended with `exit 0`.
**Then the reader, on the iMac with the build BEFORE this fix: "New game, 3D
visible then crash"** - after a blind ENTER on the white menu. Not
diagnosed; it waits on that run's `omk-out.txt` / `omk-freeze.txt`, from the
folder the iMac ran it from.
**CONFIRMED ON THE iMAC (the reader's "it ran", 2026-10-30, the build with
both fixes, its log 432 KB)**: the probe read `RGB565 -1.0, ARGB1555 -1.0,
ARGB8888 -1.0, RGBA bytes 61.4 -> the menus as RGBA bytes`; the world logged
`the driver refuses OpenGL 1.2's packed 1555 texels - the textures are sent
as RGBA bytes`. Textures were at `0.5x1` (6360 KB free against the rule's
8064). The reader went from a new game through AIMPASSE and AIMPASAS into
ANEKBAH: 4 areas, `exit 0`, peak 56917 KB of the partition (inside 64 MB).
But it is SLOW: 1391 slow frames; the street's `sim+draw` is **330-405 ms**
a frame (~3 fps), present 0.1-4 ms. ANEKBAH's set load: geometry 3730 ms,
soups 3150 ms.
**THE LONG LAUNCH (the reader: "more than one minute", 2026-10-30)**. The
log had no times before the window, so every line of `omk-out.txt` is now
STAMPED with the seconds since `main` (`classic_main.cpp`, a `funopen`
stream for stdout and stderr), and the first line gives the system's load
time before `main` (Process Manager launch date). On the emulated OS 9.2.1,
temporary markers put **7.5 s of the 7.8 s before `options:` into two parses
of `tables/ui_widgets.json`** (567 KB): `UiWidgets::loadJson` 3.7 s,
`OptionTree::loadJson` 3.8 s. It is parsed once now (`UiWidgets::fromJson`,
`OptionTree::fromJson`, `playsetup_options.cpp`): the options tree 0.13 s,
the stretch 4.3 s. An integer fast path in `Json` (no `strtod`; tree
bit-identical over every table) measured NO gain - the parse stayed 3.9 s -
and was not kept: the cost is elsewhere in the parser (allocation, the maps).
Emulator, `--nofmv`: first frame at 9.4 s, the start menu at 14.8 s; the
system's load before `main` 0.7 s; the upload probe ~6 s on the emulator's
software GL. The iMac's stamped log will say where ITS minute goes.
**THE iMAC'S STREET, TWO FAULTS (the reader's photos and "3fps ... even for
a software renderer, it is suspicious", 2026-10-30)**:
  1. **A FLAT LIGHT-BLUE BAND along the horizon**, the far buildings and the
     lower sky black against it. The log says why: `day/night (sub_41E7A0) -
     area 0 ... fog and clear colour 91 160 171`, and `gl1: no
     EXT_secondary_color - a coloured fog draws black`. GL1 draws the fog as
     colour * (1 - a) plus fog * a through the colour sum; with no colour sum
     the far scene fades to BLACK while the cleared background stays the fog
     colour. `gl1render.cpp`'s comment that "the game's fog is BLACK ... the
     shipped scenes never get here" is WRONG: the streets' day/night fog is
     coloured. Not reproduced on the host's GL1 (it has the colour sum): the
     same street through GL1 against software, 1098 pixels off by > 24.
     **FIXED (step 1, the reader's "go with step 1")**: an OpenGL 1.1 SECOND PASS where there is no
     colour sum - the same triangles with the texture env GL_BLEND, env colour
     and vertex colour both the fog (so C = fog exactly) and alpha `a * texel
     alpha`, blended GL_SRC_ALPHA/GL_ONE at GL_EQUAL depth: dst += fog * a, and
     keyed texels (alpha 0) add nothing. Exact for opaque and keyed surfaces;
     translucent ones keep the black fog. (ARB_multitexture would do it in
     one pass with a luminance ramp on unit 1 and GL_BLEND, but whether the
     iMac's driver has it is unknown - its extensions are not logged.)
     Made as `fogSecondPass` (`gl1render.cpp`), the vertex's (fog, a) in
     `fogCol_` beside `outN_`, `OMK_GL1_NO_COLOUR_SUM=1` forcing it on a host.
     On the M1 with the fog forced to 91 160 171: the pass's frame against
     the colour sum's, 0 pixels off by > 24 (8144 by less); both against the
     software reference 138/139. Without the pass, 20362 pixels off -
     `engine: gl1 backend` gained the two frames and is shown to fail so. On
     the EMULATED OS 9 (no colour sum there either): `a coloured fog is added
     in a second pass`, 996 passes in 60 (as on the host), the dumped frame
     619 pixels (0.2%) off the host's colour sum. The AGL log now prints the
     driver's whole `gl1: extensions:` line. Other backends, read: Vulkan, GLES
     and the software reference lerp to the fog in their own shading, and
     citro3d's TEV stage 1 blends the fog colour by the vertex alpha
     (`c3drender.cpp` "THE FOG'S COLOUR") - only GL1 leaned on the colour sum.
  2. **~3 fps in ANEKBAH, and it is the CPU**: `world begin..end (submit,
     GL)` is 14-20 ms; `staged bodies` 124-210 ms with all 25 staged bodies
     BEYOND the clip distance (not skinned - the cost is the per-body work
     before the skin gate: the program lookup, the clip, the pose), and
     `scripted motion: meshes placed` ~75 ms (`motion patch` 50, `motion
     soups` 50, `motion gather` 26). Before ANEKBAH (Grid, the Impasse) the
     frames were 10-30 ms. On the M1 the same spot (`--area 0 --stand
     3688,2,-2142,3`, new-game state, 25 scene-driven far bodies) costs 0.3 ms
     of staged bodies and 0.0 of motion: either the iMac's state after walking
     in from AIMPASAS differs (the outgoing pool still ticking, its moving
     meshes), or the work is memory-bound (a ~500x ratio; the G3's bus is
     ~170 MB/s). Next: reproduce the walked arrival on the host, then read the
     original for what it does with a far, script-driven actor.
     **STEP 2, FOUND (2026-10-30): EVERY ALLOCATION IS A MEMORY MANAGER CALL.**
     Retro68's `libretro/malloc.c` maps `malloc` to `NewPtr` and `free` to
     `DisposePtr` one for one - no sub-allocator - and `operator new`, every
     container and every string (libstdc++'s copy-on-write `std::string`,
     no small-string buffer) go through it. The classic Memory Manager
     searches the heap for each nonrelocatable block. Measured at launch by
     a new probe (`playsetup_splash.cpp`, `profile:` line) on the EMULATED
     OS 9: a 32-byte `new` + `delete` **53.4 us** (69.3 with 2000 held at
     once), a named span add 56.2 us (its key string's allocation), a clock
     read 1.25 us; on the M1 a span add is ~0.05 us. So the slow spans are the
     ones that allocate (or are timed through `phases.span[...]` per body or
     per mesh): `staged 2 drive+clip` 7.6 ms on the emulator (new per-step
     spans `staged 1..7` in `worldStagedBody`), and the motion spans. Why the
     iMac is so much worse than the emulator: its bus. The same walk over the
     heap's block list costs a G3 at 100 MHz far more than QEMU on host RAM.
     On the reader's host, the street's frame allocates 15-34 blocks in the
     staged bodies and ~30 in the motion (counted by a temporary counter in
     the profiler's `operator new`, removed) - under libc++'s small-string
     buffer; the classic build allocates more. A 1999 port would not have
     called `NewPtr` per block: Metrowerks' MSL `malloc` sub-allocates from
     pools. Step 3: the same for the classic build - small blocks from size
     classes carved out of large `NewPtr` slabs.
     **STEP 3, DONE: `engine/backends/classic/classic_alloc.cpp`**, linked
     into every classic application in place of `libretro/malloc.c` (it
     defines `malloc`, `free`, `realloc`, `calloc`, `memalign`,
     `aligned_alloc`, `posix_memalign` and newlib's `_r` forms; that archive
     member is then never pulled). Requests up to 4080 bytes take a block of
     one of 28 size classes (32..4096 with a 16-byte header) from a 32 KB
     slab (64 KB past 2048), each slab its own free list; a slab whose last
     block is freed goes back to `DisposePtr`, one empty slab kept per class;
     larger requests are their own `NewPtr`. Single-threaded by construction
     (the Sound Manager callback sets a flag and nothing more). On the
     EMULATED OS 9, the street start (`--save save-appart --area 0 --stand
     1804,0,-6890,336`, 200 frames, exit 0): a 32-byte new+delete **53.4 ->
     0.40 us**, a span add 56.2 -> 0.48 us; the launch reaches `options:` at
     **0.34 s instead of 7.84** (the JSON parse was its allocations); the
     frame's sections, against the same street before (run of 2026-10-29):
     scripted motion 45.3 -> 0.5 ms, props/guns 44.2 -> 0.1, pump 95.8 ->
     30.1, the set at frame start 35.1 -> gone, pedestrians/traffic 29.8 ->
     7.2, staged bodies 20.2 -> 3.0 - the CPU outside the draw ~345 -> ~87
     ms (the emulator's software GL and the audio mix unchanged). The
     partition: at most 44816 KB used (43713 before), 171 slabs at most, 70
     at the end - inside the 64 MB budget. `engine: classic build` asserts
     `.malloc` follows `classic_alloc.cpp`'s file entry in each app, shown to
     fail with the entry points compiled out (`malloc from malloc.c`).
     **CONFIRMED ON THE iMAC (the reader's `omk-out_03.txt`, 2026-10-30)**: a
     new game through AIMPASSE, AIMPASAS, ANEKBAH, into the restaurant and
     out again. A 32-byte new+delete **0.63 us** (1.38 with 2000 held); the
     ANEKBAH street **37-48 ms of work + ~6 ms present a frame (~20 fps),
     from 330-400 ms (~3 fps)**: staged bodies 4-5 ms (124-210), scripted
     motion 1.6 (~75); the draw (`world begin..end`, GL) is now the largest
     at 21-23 ms. ANEKBAH's set load: geometry 790 ms, soups 770 (3730 and
     3150 before). THE LAUNCH: the system's load 1.1 s, `options:` at 0.43 s,
     the window at 1.69 s, the GL probe done at 3.70 s, then the three films
     (skipped by keys) and the original's unskippable 5 s splash - the menu
     at 21.6 s. QuickTime drew the films at ~5-7 pictures a second (18 in
     3.7 s), each sent to the card as RGBA bytes - the next thing to look at.
     The driver's extensions: `GL_APPLE_specular_vector GL_APPLE_transform_hint
     GL_EXT_compiled_vertex_array GL_ARB_multitexture GL_EXT_texture_env_add`
     (and on): ARB_multitexture IS there, so the fog could be one pass.
     The reader: "the band is gone, and the fog was correct" - the second
     pass CONFIRMED ON THE iMAC.
  3. **NO PEDESTRIANS IN THE STREETS on the iMac (the reader, 2026-10-30)**.
     Both iMac logs (`omk-out.txt` of the first run, `omk-out_03.txt`) have
     the crowd library loaded (`ANIMS/PASSANTH.ANI`) but NEVER the line
     `the circuit's slot N is SHOWN/HIDDEN`, printed the first frame
     `session.sliders().loaded()` - so `Slider_Init` never loaded ANEKBAH's
     circuit. The area load's `the traffic circuit` took 0.0 ms (10.0 in the
     first run, the clock's step), where reading the circuit includes
     `PASSANTH.ANI`'s 395 KB, hundreds of ms off the USB stick: it returned
     EARLY. Ruled out: `--no-crowd` (not in the reader's `omk.args`, so
     `loadTraffic` ran); the walked arrival - a host probe (Session, IAM\START,
     `loadTraffic` first, AREA 142 then `requestArea(0)`) loads it: slot 1
     ANEKBAH, 200 live; the classic code - the EMULATED OS 9's street start
     has 200 walkers (its data is on the HFS+ disk). Left: the file -
     `TRAJECTOIRES/ANEKBAH.OPT` absent or unreadable on the reader's FAT32
     stick (`TRAJECTOIRES` is the one long folder name in the data; long
     names do work there - `SOUNDS\sliderm01.wav` loaded). Waiting on the
     stick's listing.
     **SAID IN THE LOG FROM NOW ON** (2026-10-30): `Session::loadTrafficFor`
     returned in silence on a circuit that would not load. It now prints
     `street: NO CROWD AND NO TRAFFIC - the circuit TRAJECTOIRES/<opt>.OPT is
     not loaded: <the parse's reason> (<the resolved path, or "the file is
     not found">, N bytes)` and, when the file is not found, the data's
     `TRAJECTOIRES` folder and every `.OPT` the File Manager lists in it.
     Seen on the host with a data copy lacking the folder; the real data's
     street unchanged (slot 0 SHOWN). The iMac's next log says which.
  4. **FASTER ON THE iMAC, items 1 and 5 of the list given to the reader
     (2026-10-30, "go with 1 and 5")**:
     * **the fog in ONE pass on ARB_multitexture's second unit** (`fogMT_`,
       `fogMTUnitOn`): a 256x1 luminance ramp sampled at the vertex's `a` under
       GL_BLEND with the fog as env colour, the vertex colour not darkened -
       the colour sum's texel * colour * (1 - a) + fog * a for every batch,
       translucent and additive included. `glActiveTextureARB` /
       `glClientActiveTextureARB` looked up by name. Taken when there is no
       colour sum and the extension (2+ units) is there; `--gl1-fog-pass` /
       `OMK_GL1_FOG_PASS` keeps the second pass. On the M1 with the fog forced:
       0 pixels off the colour sum's by > 24 (1-3 five-bit steps on fogged
       pixels: the 8-bit ramp's rounding), 1476 draw calls against the second
       pass's 2472. `engine: gl1 backend` gained the frame, shown to fail with
       the colour darkened as well (6172 pixels). Not yet run on the iMac.
       `OMK_GL1_NO_COLOUR_SUM` had been left out of `env.cpp`'s table by the
       second pass's commit (`8694ddd`), so `env switches` was red until now;
     * **the films**: CORRECTION - QuickTime's pictures were already sent at
       the film's 320x240, not 640x480 as the list said; on the iMac (no
       EXT_bgra, no packed pixels) each is converted to RGBA bytes on the CPU
       and uploaded. Where its time goes is now logged at each film's close
       (`MoviesTask` total and per picture; converted / uploaded / drawn and
       swapped per picture). And `--films-direct`: QuickTime draws STRAIGHT
       INTO THE WINDOW, the AGL drawable set aside (`aglSetDrawable(ctx, 0)`)
       and put back at the film's close - its own path to the screen. On the
       EMULATED OS 9: QUANTIC 151 (and 111) pictures in 23.5 s direct against
       51 through GL; GAME 312 in 51 s / 436 in 107 s against 200 in 107 s;
       the film shown in the window (QEMU screendump) and the start menu drawn
       by GL after it. Not yet run on the iMac: the A/B decides the default.
**Rule: on the classic build, an OpenGL 1.2 pixel format is never assumed;
OpenGL 1.1's RGBA bytes is the fallback of every upload.** The iMac's menu
frames: 67-116 ms of work (`screens, hud` ~35-49 ms). The launch has no
wall-clock times in the log; the slow start is likely the USB 1.1 read of
its loads (see the 2026-10-09 iBook report above).

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
   emulated G4. ~~the gl1 backend is CGL (Tiger only): AGL for OS 9~~ -
   **DONE 2026-10-04, the reader's ask ("integrate opengl in the carbon
   build, it would be easier to test on tiger")**: the Carbon `OMKPlay`
   draws the world through the fixed-function backend in an AGL context on
   its own window (`OMK_GL1_AGL`, `backends/gl1/gl1host.h`) - one binary
   for OS 9's OpenGL 1.2.1 and Tiger's. On Tiger: ATI Radeon 9700, OpenGL
   1.5, accelerated, the street drawn; ~5 fps, because the frame is read
   back (through a texture - see 5), composited on the CPU and drawn back
   with `glDrawPixels`; 17 fps with the world swapped straight, which is
   the next step: the interface drawn OVER the world in GL, as the GLES
   window's overlay does, instead of the world under the interface on the
   CPU. NOT run on OS 9 yet (its emulation has no 3D card: Apple's software
   renderer). `--software` in `omk.args` keeps the reference.
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
3. **What is left on OS 9 after the frontend**: ~~the memory budget~~ -
   **MEASURED 2026-10-04** (`backends/classic/heapcount.*`: this build's own
   `operator new`/`delete` count every C++ block by `GetPtrSize`, exactly,
   and `FreeMem`/`MaxBlock` are sampled each frame; a flushed line every 30
   frames, the totals at exit). Retro68's `malloc` is `NewPtr`, so all of it
   comes out of the partition. On OS 9, the street start in Anekbah: peak
   **79886 KB** of live C++ blocks (during the load), **79009 KB** used by
   FreeMem at the low-water mark - about 82 MB with the C allocations and
   the blocks' overhead - and MaxBlock 5 MB under FreeMem (fragmentation).
   Kay'l's apartment (`games-resto.bin` slot 0): **49741 KB**. The
   conversation itself (402, ~600 frames in) was NOT reached: twice the run
   on Tiger ended early with exit 0 and no reason given (the frontend now
   prints each quit's cause), and OS 9 runs ~5 s a frame. **2026-10-29:
   the street at 42959 KB / 43713 KB of the partition - inside the budget
   (3i).** **THE BUDGET IS
   64 MB, A GOAL** (the reader, 2026-10-04: "the original game works
   correctly with 32MB, we already ask for twice the requirement of the
   original") - so the street is **~18 MB OVER**, and that is the code's to
   fix, never the budget's: `classic-mac-port-1999.md` 3b's table is the
   list. The partition is 96 MB preferred (what today's code needs to reach
   the street), **64 MB minimum**. Against 3b's 136.8 MB on a 64-bit host,
   the 32-bit build holds ~80;
   `%zu` is handled (`classic_printf.h`), audio and the films work. `DataFs` and no threads
   are DONE (2 above); with `OMK_THREADS 0` the voice read-ahead already
   runs on its frame, so "ticked loading" is a speed question, not a gap.
4. **Speed**: the 1999 budget (`classic-mac-port-1999.md` §3b, steps 1-2).
   Tiger's emulated G4 is not a G4's timing.
5. ~~**A G3 build**~~ - **DONE 2026-10-04.** Our own objects were always
   generic (`PPC ALL`); the 119 objects of SDL were `ppc7400`, because its
   configure finds AltiVec and adds `-maltivec` to every unit, and the
   linker marks the binary with the highest subtype. Rebuilt in Tiger with
   `--disable-altivec` (`~/src/panther-sdl2-g3`, installed to
   `~/sdl2-tiger-g3`, copied to the tools volume) - which needed ONE patch:
   `SDL_cocoavideo.m` and `SDL_cocoamessagebox.m` include `altivec.h` under
   `__POWERPC__ && !__APPLE_ALTIVEC__` to undo its `bool`/`vector`/`pixel`
   macros, which errors without `-maltivec`; the guard now also asks for
   `__ALTIVEC__`. The binary is `ppc`; a scan of `otool -tv` finds 24
   vector instructions against 230 in the G4 build, all in libgcc's
   `save_world` / `eh_rest_world_r10`, which test `__cpu_has_altivec` and
   return before them on a CPU without it - and no `fsqrt`. Its 30-frame
   street dump is BYTE-IDENTICAL to the G4 build's on Tiger. Not run on a
   G3 (QEMU's `mac99` is a G4 here): the evidence is the subtype, the
   scan and the identical frame.
6. **The ppcosxkvm report**: test the fix seriously (the checklist in the
   draft), and narrow the second artefact (below) to a minimal GL repro.

---

## 5. Traps that cost time

* **ppcosxkvm is patched locally** (`scripts/ppcosxkvm-r300-txo-endian.patch`):
  32-bit textures with TXO_ENDIAN 2 drew byte-reversed (black as BLUE) -
  SDL 2.0.3's window texture is the only one. `ppcosx update` discards it;
  re-apply, `ninja qemu-system-ppc`, then `scripts/bundle-dylibs.py` on it.
  `OMK_R300_TEXLOG=1` logs each distinct texture setup (set it on the HOST,
  for `ppcosx`, and restart Tiger). A plain `glTexSubImage2D` of 32-bit
  words still drew black as blue with TXO_ENDIAN 0 (3g): send 32-bit frames
  through client storage.
* **A texture filled by `glCopyTexSubImage2D` is sampled in the wrong
  colours by the emulated Radeon** (3h: red in RGBA8/RGB8, byte-swapped in
  RGB5), while `glGetTexImage` reads it right. The fullscreen fit looks wrong
  in Tiger and right on the host; judge it on the host or the G4.
* **Still open, suspected emulation**: a wall grazed by the camera flashes
  FLAT for a frame in Tiger; the M3 never shows it (60-position sweep, worst
  169 px). Not cured by the screen-edge clip.
* **`glReadPixels` FROM A WINDOW SURFACE RETURNS ZEROS on the emulated
  Radeon** (2026-10-04): the AGL build's world drew correctly - swapped
  straight, the street showed - while every read of the back buffer came
  back black, even a cleared red. The backend copies the back buffer into a
  texture (`glCopyTexSubImage2D`) and reads THAT (`glGetTexImage`). A frame
  at frame 1 is legitimately black (the area's fade-in): look at frame 30.
* **Retro68's `MakeImport` CRASHES on Tiger's `CFMSupport/OpenGLLib`** (its
  `cfrg` holds three fragments) - make the stub from OS 9's `OpenGLLibrary`,
  which exports the same names under the same fragment name.
* **`scp` DROPS A RESOURCE FORK**: an app copied that way fails with
  `cfragRsrcForkErr` (-2856), a library loses its fragment name. `ditto -c
  -k --sequesterRsrc` on the Mac, `ditto -x -k` in Tiger; onto the OS 9
  disk image, `cp -p` keeps it.
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
* **NO 8-BYTE ATOMIC ON 32-BIT POWERPC** (2026-10-08): GCC lowers
  `std::atomic<long long>` to `__atomic_*_8` calls into libatomic, which the
  ppc-darwin8 toolchain does not ship, so `omk-play` stopped LINKING once the
  profiler's memory counters landed (`2b47a8a`, 2026-10-04) and nobody
  rebuilt for Tiger until today. `profile.cpp` now picks the widest LOCK-FREE
  type (`long` here; they count live memory, bounded by the address space).
  Any new `std::atomic` of 64 bits in shared code breaks this build again.
* **Tiger has no `seq` or `pgrep`** (`jot`, `ps | grep`); a host loop that
  greps for `done` can be fooled by a script that wrote one early.
