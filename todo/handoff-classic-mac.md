# Handoff — the CLASSIC MAC (PowerPC) PORT (2026-10-03)

**Read this first to pick up the PowerPC Mac work.**
[`classic-mac-port-1999.md`](classic-mac-port-1999.md) is the plan and the
full record (the 1999 what-if, every finding with its evidence, sections 3a-i
to 3d-ii); this file is the state, the recipes and what to do next. It is
NOT a modern macOS port - OMK already runs on today's Macs.

---

## 1. Where it stands

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
   `c7a5443`; `--filter nearest` gives the old picture. Not yet LOOKED at in
   Tiger: bilinear on the emulated Radeon is still to be seen.
2. **The gateway class** (the reader's direction): every SDL call outside
   the frontend files behind the `Frontend` interface, so a Carbon frontend
   can stand in for SDL on OS 9 and Tiger. Seven `backends/sdl/play*.cpp`
   call SDL directly; they are the play split's - **coordinate with that
   session first**.
3. **The engine on OS 9** (rest of step 5): ticked loading instead of the
   voice read-ahead thread, `DataFs` instead of `std::filesystem`, `%lu`
   for `%zu`, Sound Manager audio, 555 video, QuickTime for the MPEG-1
   films - after 2.
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
* **Tiger has no `seq` or `pgrep`** (`jot`, `ps | grep`); a host loop that
  greps for `done` can be fooled by a script that wrote one early.
