# The PS VITA port — the plan

Written 2026-09-18. Picks up [`handoff-vita.md`](handoff-vita.md), which
recorded the DECISION (6.0 ms of main-thread work a frame on an M1, 137 MB live
heap, the device factor unknown) and deliberately started nothing. This file
starts it: what is missing, in what order, and what already landed.

> **THIS TREE IS SHARED.** Stage by explicit path — never `git add .`, `-A`
> or `commit -a`. The Vita files live under `engine/backends/gles/` and
> `engine/backends/vita/`, outside the Makefile's `src/*/*.cpp` and
> `tools/*.cpp` globs, so they are in nobody's default build.
>
> **KEEP EVERY TARGET COMPILING.** The same `src/` builds for the host
> (`make`), the Vita (`make vita`, VitaSDK) and, through `glesrender.cpp`,
> any GLES2 host. A change can build on one and break another - it already
> happened three times (below). `verify.py: engine: vita build` runs the Vita
> build and skips where no SDK is installed; run it with `--only` after any
> change to `src/` that adds an include, a platform call or a new file.

---

## 0. The state when this was written

**Done before this file** (`handoff-vita.md`, `optimization.md` steps 7-14):
the decision's measurements; the portable thread manager
(`platform/threads.{h,cpp}`, Vita half never compiled); the shadow bone index
(built, **not wired** — `pending/vita-meshidx-playcpp.md`); the depth tie's
memory localised (not cut).

**Landed by this file** (new files only; the host ones run on the M1, the
Vita ones built with VitaSDK 2026.08 - see below):

| file | what | state |
|---|---|---|
| `engine/backends/vita/bench_main.cpp` | THE DEVICE FACTOR: `composePose` + `applyPose` + `applyLights` over 45 street bodies, inline and through `omk::Threads`, per-stage ms, a hash across both | **run on the M1** (below); Vita side needs the SDK |
| `engine/backends/gles/glesrender.cpp` | the GLES2 `Renderer` behind `o3de/renderer.h`: vitaGL / macOS GL 2.1 / GLES2-WebGL from one source; present pass with the 565 dither on the GPU | **run on the M1** through the probe |
| `engine/backends/gles/gles_probe.cpp` | `run_vulkan`'s differential for GLES, headless (CGL, no window) | **green, and shown to fail** (below) |
| `engine/backends/vita/vitapad.h` | the Vita pad as the game's own JOYSTICK device | host-tested over all four groups |
| `engine/backends/vita/smoke_main.cpp` | one set on the Vita GPU, stick-flown, SQUARE toggles the depth tie, timings to `ux0:data/omk/smoke.txt` | **builds** (VPK); not yet run on a console |
| `engine/backends/vita/CMakeLists.txt` | VitaSDK build: `omk_bench`, `omk_smoke`, `omk_vita` (the game, OFF until F1-F4) | **builds** `omk_bench.vpk` + `omk_smoke.vpk` |

### What the M1 runs said

**The bench** (`vita_bench <gamedata> 45 300`, Anekbah's 155 lights, HO1_FNM +
PSH_FN alternating on line 125338):

```
inline   compose 0.150  apply 0.688  light 0.470  total 1.311 ms/frame  (lights reached 1.8/body)
threads  8 runners      total 0.527 ms/frame  speedup 2.48x
hash inline 14de822f23fe92e4 threads 14de822f23fe92e4  threads: EXACT
```

1.31 ms against the handoff's profile attribution of ~1.34 (0.24 + 0.45 +
0.65) for the same three stages — so the bench measures the work the decision
was about, and a device run of the same binary gives the ratio directly.
**The first Vita action is to run `omk_bench` and divide.** The threaded pass
is byte-identical (each body writes only its own geometry), so posing is safe
to spread across the Vita's three game cores as far as these three functions
go. Note the apply/light split differs from the profile's (apply is the
larger here); the profile was of the live frame with the crowd's LOD, the
bench poses full models.

**The GLES probe** (Aapkayl through dialog 402's camera 4555, 640x480):

```
gl: Apple M3 | 2.1 Metal - 90.5
plain     coverage 0.9961   present world: EXACT   present surface: EXACT
dithered  coverage 0.9977   present world: EXACT   present surface: EXACT
letterbox 640x352 at row 64                        present: EXACT
```

Coverage matches or beats Vulkan's quoted 0.995 on the same set. The two
"present" lines are exact claims: the GPU's float-shader 888→565 dither
agrees with `quantise888DitherRow` on every pixel. **Shown to fail**, each
mutation on a scratch copy and asserted to have applied: dither offset -8→-7
(46449 + 33324 pixels red), the target's row flip off by one (186802 red), the
letterbox row placement off by one (187442 red), the projection's Y sign
(coverage 0.9495 red). **One blind spot:** removing the cutout discard leaves
it green — Aapkayl has 8 cutout triangles and none in shot. G2 needs a
cutout-heavy camera before the cutout rule counts as checked.

A lesson from building it, the §1 kind: the first version compared the
present in 888 and reported **113303 differences** on the surface path. All
of them were the Mac driver expanding 565→888 by rounding (blue 3 → 25) where
the probe assumed bit replication (24). `ui/surface.h` already says it: *the
expansion is the host's; compare in 565.* Nothing was wrong with the backend.

### 2026-09-18, later: the SDK is in, both VPKs build

VitaSDK 2026.08 (GCC 15.2) installed at `~/vitasdk` from `vdpm`'s bootstrap
(no sudo), with `vitaGL`, `vitaShaRK`, `libmathneon` and **`sdl2_vitagl`** -
SDL2 with vitaGL as its video backend, which settles F1's open question: SDL
and vitaGL coexist, so `play.cpp` keeps SDL on the Vita.

**The whole `src/` compiled for the Vita at first attempt except three
faults, all FIXED in the sources** (first worked around in the CMake file
while the files were held, then fixed properly the same day):

* `actor/walk.cpp`, `script/area.cpp` - `std::sqrt`/`floor`/`pow`/`isfinite`
  without `#include <cmath>` (the host's headers pull it in; newlib's do not).
  The include is added to both.
* `platform/threads.cpp` - the Vita worker was a free function naming the
  PRIVATE `Threads::Impl`; the Vita half had never been compiled. It is now
  a static member, `Impl::workerMain`. (`thread_probe` on the host: 0
  mismatches - the host half is untouched.)
* **`std::filesystem` compiles** (datafs, savefile, area) - B2 is closed for
  compilation; whether it BEHAVES on `ux0:` is the bench's first file read.

Built: `engine/build/vita/omk_bench.vpk` and `omk_smoke.vpk` (gitignored).
**To run** (the only benchmark that counts - on the console):

1. copy the game's data tree to `ux0:data/omk/gamedata/` (the `MESHES/`,
   `MORPH/`... folders directly inside it);
2. install the VPK with VitaShell; for `omk_smoke` also have
   `ur0:data/libshacccg.suprx` extracted (VitaShell / ShaRKBR33D);
3. launch; `omk_bench` exits by itself and writes `ux0:data/omk/bench.txt`;
   `omk_smoke` writes `ux0:data/omk/smoke.txt` every 300 frames, START quits.

Rebuild: **`make vita`** from `engine/` (finds `$VITASDK` or `~/vitasdk`,
drives the CMake build in `build/vita-cmake`, copies the VPKs to
`build/vita/`; ~2 min fresh, incremental after; prints a note and exits 0
without an SDK). Also new, and needing nothing installed: `make
build/vita_bench` (the bench on this machine) and `make gles-probe` (macOS).

**Three checks** (all `engine:`, so `--slow` / `--only`):
`engine: vita bench` (the threaded pass EXACT), `engine: gles backend` (the
probe's coverage and five EXACT presents; macOS only), `engine: vita build`
(the cross-compile; skipped without an SDK). The licence-header count moved
443 -> 458, attributed in the check (9 of it was already red at `202937c`).

---

## 1. The issues, and what is missing

Ordered by how much they can sink the port, not by how much work they are.

1. **The device factor is unknown** (`handoff-vita.md` §1). Everything below
   is sized against it. The bench now exists; it needs one device run.
2. **`play.cpp` is the game loop AND the SDL frontend AND the CLI harness**, in
   20233 lines. There is no Vita entry point without editing it. It has two
   window modes (SDL_Renderer upload; Vulkan direct) chosen by
   `#if defined(OMK_VULKAN)` blocks, and the GLES path has to be a third
   (F1). Only ~120 SDL calls, so it is an `#if`, not a rewrite.
3. **Input: `HostInput` has no joystick**, and `play.cpp` builds
   `DeviceState.keyboard` only. The engine's own tables carry a joystick (all
   four groups share one default: axis 0 turn, axis 4 move, button k → slot
   4+k), which is the faithful pad. **And the joystick AXIS arm is not
   ported**: `Input::poll` skips code 0 and one code serves two slots, so the
   direction must come from `Input_ReadOneControl`'s joystick arm, which
   nobody has read. `vitapad.h` routes the buttons through the joystick and
   the directions through the group's own keyboard slots as a labelled
   stand-in.
4. **A frame with an interface on it round-trips through the CPU**
   (`handoff-vita.md` §5): `readback()` → CPU compose → `presentSurface`. On a
   Vita `glReadPixels` is a full pipeline stall every such frame, and that is
   every frame with the HUD, a subtitle, a fade or a screen. This is probably
   the largest GPU-side cost the port has and no desktop measurement shows it.
5. **The posed bodies re-upload every frame** (16 MB of posed geometry;
   `uploadGeometry` rewrites a body's whole buffer each pose). On vitaGL,
   `glBufferSubData` on a buffer the GPU may still be reading is the open
   question (G3). The GLES vertex is already cut to 36 bytes (no normal), 40%
   less than Vulkan's.
6. **The depth tie costs ~0.8 ms of CPU a frame on the M1** and exists
   because Vulkan's float depth compare differs from the engine's strict test
   on a quantised z-buffer. The GLES target uses a **16-bit** depth
   renderbuffer — the engine's own depth width — which may reproduce the
   engine's tie without the walk, or may make it worse. It must be looked at,
   not guessed (G4; `smoke_main` has the toggle).
7. **`std::filesystem`** in `platform/datafs.cpp`, `script/savefile.cpp` and
   `script/area.cpp`. VitaSDK's libstdc++ support for it is unverified; the
   fallback is a small POSIX (`opendir`/`stat`) shim behind DataFs. The first
   SDK build answers it (B2).
8. **Memory**: 137 MB live on the M1 with the music held whole (16 MB),
   posed geometries (16 MB), and texture pixels kept on the CPU (12.6 MB)
   which a GPU backend also copies. Comfortable against a homebrew's ~365 MB,
   but vitaGL's own copies and CDRAM (128 MB) are not in that number.
9. **Load hitches** (`handoff-vita.md` §5: 69-78 ms frames on a set load and a
   music switch, on the M1). A memory card reads far slower than an SSD. Not
   a mean-cost problem, and on a Vita likely the worst visible one.
10. **The movies**: `pl_mpeg` decodes the FLIS intro on the CPU. Unmeasured on
    an A9.
11. **Shaders on vitaGL**: GLSL ES 1.00 is translated at run time, which needs
    `ur0:data/libshacccg.suprx` extracted on the device (the standard
    requirement for vitaGL ports, and a user-facing install step). Shipping
    precompiled `.gxp` removes it later.

**Two findings for OTHER owners, found on the way:**

* **The Vulkan shimmer is per FRAGMENT, the reference per VERTEX.**
  `raster.cpp` adds `shimmerOffset(phase, clock)` to each corner before
  interpolation — the engine's own per-vertex loop — while
  `backends/vulkan/shaders/scene.frag` interpolates `vPhase` and looks the
  wave up per pixel, which differs wherever a triangle's corners carry
  different phases (the phase comes from the vertex address, so they usually
  do). The GLES backend follows the reference. Whoever holds `vkrender` should
  decide whether Vulkan should too — it would also be cheaper.
* **`expand565Rgba` and `present.frag` expand blue with `b >> 3`**, red with
  `r >> 2`. Replication for a 5-bit channel is `>> 2`. Harmless for anything
  compared in 565 (it truncates back to the same value), wrong if anything is
  ever compared in 888. Recorded, not changed.

---

## 2. The plan

Each step ends in something a person can check. **B** = build, **G** =
graphics, **F** = frontend/`play.cpp`, **P** = performance, **M** = memory.
Steps marked ✎ edit an existing file and wait until its owner is free.

### Phase 1 — measure on the device (no `play.cpp`)

* **B1** — Install VitaSDK (`vdpm vitaGL sdl2`), `cmake -S engine/backends/vita
  -B build-vita`, build `omk_bench`. Fix whatever the first compile of
  `src/*/*.cpp` under arm-vita-eabi says; each fix is a portable change.
* **B2** — `std::filesystem` on the SDK: `fs_selftest` built for the device,
  or the POSIX shim ✎ `platform/datafs.cpp` if it fails.
* **P1** — Run `omk_bench` on the device at 444 MHz. **Divide by the M1's
  1.311 ms.** That ratio replaces `handoff-vita.md` §1's missing-number
  paragraph and decides how much of P3-P6 is needed. Also check the threaded
  line: 3 runners, EXACT hash, the speedup; if `threads: DIFFERENT`, the Vita
  half of `threads.cpp` is wrong, not the posing.
* **G3** — Build and run `omk_smoke`: vitaGL takes the shaders, the 565
  upload, the 16-bit depth renderbuffer; frame time and CPU submit time over
  300 frames. First GPU number for the port.
* **G4** — In `omk_smoke`, look at the depth tie both ways (SQUARE) on a
  set with coincident faces — Anekbah's shop signs, CLAUDE.md §6. Decide per
  measurement: tie off on GLES, tie on, or `OES_depth24` + tie.

### Phase 2 — the game boots on the Vita

* **F1** ✎ `play.cpp`: an `OMK_GLES` window mode beside `OMK_VULKAN`: SDL GL
  window + context (SDL2's Vita video driver on vitaGL, or `vglInit` directly
  if SDL2-vita's GL path does not coexist), `makeGlesRenderer`, and the two
  present calls in the two places `vulkanPresent*` are called
  (`play.cpp:1344`, `3436`, `20018`). The declarations are in
  `glesrender.cpp`'s tail; declare them the way the Vulkan ones are declared
  (`play.cpp:111`).
* **F2** ✎ `play.cpp` + `platform/frontend.h`: `HostInput` gains `joystick`,
  the SDL path fills it from `SDL_GameController`, and `DeviceState.joystick`
  is fed from it. With `vitapad.h` on the Vita side.
* **F3** — READ `Input_ReadOneControl`'s joystick arm from the listing (the
  axis sign, the dead zone); port it ✎ `input/bindings.*` with a check shown
  to fail; then drop `vitapad.h`'s keyboard stand-in for the directions.
* **F4** ✎ `play.cpp`: the Vita paths — data root `ux0:data/omk/gamedata`,
  tables and config beside it, saves under `ux0:data/omk/`, no command line
  (defaults instead), 960x544, the SDL_GetKeyboardState paths guarded.
* **B3** — `-DOMK_VITA_GAME=ON`, the VPK, LiveArea assets (`sce_sys/`), boot
  to the menu. Title id `OMKE00001`.
* **A1** — Audio: SDL2-vita's audio device through the existing `Frontend`
  audio calls; measure the music stream (M1) and the sound mixing cost.

### Phase 3 — make it hold 30 fps

In the order `handoff-vita.md` §2 gives, re-sorted by P1's factor:

* **P2** ✎ apply `pending/vita-meshidx-playcpp.md` (the name index; 0.09 ms).
* **G6** — Stop reading back interface frames: compose the 2D layer on the
  CPU into an RGBA surface with a KEY for "nothing here" and blend it over the
  GPU picture in the present pass. The passes that READ the pixels under them
  (the fade, the darkening) become present-pass uniforms. Needs the list of
  those passes first; it is the largest unknown here.
* **P3** — Share poses between bodies on the same clip and frame; skip a body
  whose pose and placement did not change (also skips its upload and tie).
* **P4** — Thread the per-body loop with `omk::Threads` (bench says EXACT on
  the three stages; the check is the whole frame's bytes, threaded against
  inline).
* **P5** — GPU skinning and the crowd's light in the vertex shader (~1.9 ms
  on the M1). Its own exactness story: GPU float against CPU float, per
  platform — not "same bytes".
* **M1** — Stream the music (16 MB), drop CPU copies of texture pixels once
  uploaded, measure vitaGL's memory with its own counters.
* **P6** — Load hitches: the set and music loads on the card; move the music
  open off the main thread; measure.
* **G5** — The native mirror on the GLES stencil (today: `drawWithMirror`'s
  CPU path, a readback — see item 4).

### Phase 4 — ship

Precompiled shaders (no `libshacccg` step), the controls play-tested and
written up, `todo/play-test.md` entries for each confirmed step.

---

## 3. How to run what exists

From `engine/`, after `make` has built `build/obj/` (nothing here needs
`make` to know about it):

```
# the bench - the same source the Vita runs
c++ -std=c++20 -O2 -Isrc -Ithird_party -o build/vita_bench \
    backends/vita/bench_main.cpp build/obj/src/*/*.o
build/vita_bench "$OMK_DATA" 45 300

# the GLES backend against the software reference, headless, macOS
c++ -std=c++20 -O2 -Isrc -Ithird_party -o build/gles_probe \
    backends/gles/gles_probe.cpp backends/gles/glesrender.cpp \
    build/obj/src/*/*.o -framework OpenGL
build/gles_probe "$OMK_DATA" "$OMK_DATA/MESHES/DECORS/Aapkayl.3DO" \
    3526,1015,-905 3412,1032,-882 83
```

Both are now `make` targets and `verify.py` checks (above). The probe's
cutout blind spot is still open and is written into its check's docstring.

---

## 4. Traps met while writing this

* **Compare in 565.** See §0 — a present check in 888 measures the driver.
* **A mutation must be shown to apply** (CLAUDE.md §1). The five here were
  made on scratch copies with an asserted single anchor and a `cmp` against
  the original; the file under `backends/gles/` was never mutated, so there
  is nothing to restore and no stale object (the probe links from source).
* **GLSL ES 1.00 has no integer operators, no `textureSize`, no
  `texelFetch`**, and guarantees dynamic uniform indexing only in the vertex
  stage. The shimmer, the UV normalisation and the Bayer lookup are each
  shaped by one of those; the comments in `glesrender.cpp` say which.
* **GLES2 REPEAT needs power-of-two textures.** All 2534 under `MESHES` are
  (max 256x256, counted 2026-09-18); the backend clamps and SAYS for any that
  is not.
