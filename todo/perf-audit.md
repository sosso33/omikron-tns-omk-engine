# Performance audit - CPU, GPU, RAM, VRAM (2026-10-20, M3)

The reader's ask (2026-10-20): *"Do a full performances audit (cpu/gpu/ram/vram
usage) and how to optimize it (especially on low-end or old devices). You can
also look at the original's code to see how the optimizations are done
there."*

This file is the STATE after `optimization.md` (steps 1-38),
`cpu-vs-original.md`, `ram-vs-original.md`, `gpu-skinning.md` and the
console work of 2026-10-11. It re-measures the desktop on HEAD (`f5f3266`) and
gathers the low-end targets' last device figures, then ranks what is left. The
mechanisms the original uses, with their addresses, are already in those files
and are cited here, not re-derived.

## 1. How it was measured

* An M3 (24 GB) with load average 2-3, so the figures are clean enough to
  quote. They are still M3 figures: compare them only with other M3 runs
  (`two-machines` rule).
* `make play play-gles vulkan` was built at HEAD. The working tree had no
  source edits.
* The street walk is `cpu-vs-original.md`'s: `--save ../traces/save-appart.bin
  --area 0 --stand 1804,0,-6890,336 --hold
  '0*45,k200*150,k200+54*200,k203*40,k200+54*200,k200*150,0*60'`. It runs 900
  frames and walks 3115 units, with no door on the route.
* The menu run is a plain boot with `--nofmv`, 400 frames, on screen 29.
* Every run used `--profile`, with `OMK_HIDDEN_WINDOW=1` (the GPU present is
  kept), a private `--saves` and `/usr/bin/time -l`.
* `--frames` runs are not paced. On this panel the swap still blocks on vsync
  at 120 Hz, so `present, swap` is mostly that WAIT, and the work is the rest
  of the frame.
* There is no GPU timer in any backend. GPU cost is bounded from the vsync
  slack: a frame whose present waits is not GPU-bound.

## 2. The frame (M3, ms per frame, the profiler's means)

| run | frame median | present / swap (vsync wait) | the game's work | largest sections |
|---|---|---|---|---|
| Vulkan, street, 640x480 | 8.31 | 5.69 | **~2.6** | world submit 1.30 (`world: mirror` self 1.59, mostly `vkWaitForFences`), audio 0.32, hud 0.30, crowd 0.18, staged 0.13 |
| GLES, street, 640x480 | 8.31 | 6.92 | **~1.4** | hud 0.33, audio 0.31, world submit 0.29, crowd 0.14, staged 0.12 |
| software, street, 640x480 | 8.22 | 0.66 | **~7.6** | raster 6.69. It was 73 ms on 2026-10-05; the frame hash is gone, and the software renderer now holds 120 Hz |
| Vulkan, street, 4K | 8.83 | 5.11 | ~3.7 | world submit 2.89 |
| GLES, street, 4K | 8.30 | 6.96 | ~1.3 | flat: the GPU absorbs the resolution |
| Vulkan, street, 4K, `--enhance-all` | 8.85 | 3.46 | ~5.4 | world 4.12, crowd 0.55 (per-pixel light lists). It was 8.6 fps on 2026-10-01 and 21.5 after `03c57c3` |
| Vulkan, start menu, 640x480 | 8.25 | 6.99 | ~1.3 | hud 0.63 |
| Vulkan, start menu, 4K | **10.30** | 5.52 | ~4.8 | **hud 4.73**: the interface composed on the CPU per display pixel |
| GLES, start menu, 4K | **16.60** (60 fps) | **11.47** | ~5.1 | hud 4.93 plus the 4K interface texture upload inside the swap |

Whole process, all threads (`time -l`): the Vulkan street spends ~2.1 ms of
CPU a frame across threads (1.86 s user over 900 frames). Software spends
7.9 ms.

**What it says.** On a modern desktop nothing is close to a limit at 640x480
or at 4K in the 3D. The only frames that scale badly with resolution are
the INTERFACE frames: the menu at 4K costs 4-5 ms of CPU and, on GLES, a
33 MB texture upload a frame. That is the same cost the Vita pays at 960x544
on a CPU ~20x slower (`optimization.md`, the 4K benchmark, point 2). The
Vulkan world submit carries the one structural wait, `vkWaitForFences`
right after the scene's submit (`vkrender.cpp:2638`). The CPU and the GPU
never overlap. The original's `Flip(DDFLIP_WAIT)` chain did overlap
(`cpu-vs-original.md` step 2, "GPU pacing").

## 3. Memory - RAM

| run | heap live at the end (peak) | max RSS | peak footprint (incl. GPU on unified memory) |
|---|---|---|---|
| Vulkan street 640x480 | 38.8 MB (41.9) | 170 MB | 246 MB |
| GLES street 640x480 | 46.5 MB (48.4) | 187 MB | 234 MB |
| software street 640x480 | 38.7 MB (41.9) | 171 MB | 199 MB |
| Vulkan street 4K | **69.3 MB** (72.4) | 204 MB | 521 MB |
| GLES street 4K | **107.5 MB** (109.3) | 315 MB | 633 MB |
| Vulkan street 4K `--enhance-all` | - | 248 MB | 845 MB |
| Vulkan menu 640 / 4K, GLES menu 4K | - | 128 / 193 / 273 MB | 172 / 373 / 574 MB |

The street's game data is ~39 MB, down from 72.9 on 2026-10-04
(`ram-vs-original.md` tiers A-C). The original holds ~12 MB of the same
things. The largest holders are the same as after tier C: geometry 8.24,
textures 4.77 (8-bit indices + palette, as the original), SCX 5.67,
collision 3.40, bodies 2.54, motion 1.89, world rebuild 1.68.

**New in this audit: the RAM that scales with the DISPLAY, not the game**
(`omkprof.py --sites`):

| holder | 640x480 | 4K | what it is |
|---|---|---|---|
| `PlayState::setupSplash` / `Surface(dispW, dispH)` (`playsetup_splash.cpp:32,53`) | ~0.6 + 0.6 | **15.8 + 15.8 MB** | the interface's compose surface `fb` and its twin, 16-bit at DISPLAY size. The original composes at 640x480 and blits 1:1 (`optimization.md` step 28 row b, `sub_4B19C0`) |
| `GlesRenderer::init` -> `rgba_` (`glesrender.cpp:1400,1418,1611,1725`) | 1.8 MB | **47.5 MB** | the readback buffer `rw x rh x 4`, allocated at init and at every resize, although only the CPU-composite path (`glReadPixels`, `:3000`) reads it. A GPU-presented frame never touches it. On the Vita that is 2 MB of the 192 MB heap for nothing; with `--ssaa 4` it is 4x that |
| `GlesRenderer::uploadGeometry` -> `up_` scratch (`glesrender.cpp:1163-1166`) | 4.79 MB | 4.79 MB | the upload scratch keeps its high-water mark, the whole set's 139245 corners x 36 B, after the set is on the GPU. That is RAM and VBO both, on a target where they are the same memory (the Vita, the Quest) |

## 4. Memory - VRAM / GPU-visible

**Measured** - the renderers report every GPU allocation to the profiler
(`OMK_GPU_ALLOC`, `omkprof.py` "GPU memory"), the street walk's last frame,
MB, at HEAD before section 8's changes:

| category | Vulkan 640x480 | GLES 640x480 | Vulkan 4K | GLES 4K | Vulkan 4K `--enhance-all` |
|---|---|---|---|---|---|
| textures | 9.83 | 9.75 | 9.83 | 9.75 | 13.12 (mips) |
| vertex buffers | 8.24 | 4.94 | 8.24 | 4.94 | 24.08 |
| render targets | 2.86 | 1.76 | 73.22 | 47.46 | 325.25 (MSAA x SSAA) |
| **readback** (never read on a GPU frame) | **2.34** | - | **63.28** | - | 63.28 |
| present image | 1.27 | - | 32.38 | - | 32.38 |
| buffers (the Vulkan pose buffer) | **8.00** | - | 8.00 | - | 8.00 |
| shadow map (an enhancement, made whether asked or not) | **4.03** | - | 4.03 | - | 4.03 |
| posed bodies | - | 0.93 | - | 0.93 | - |
| **total** | **36.6** | **17.4** | **199.0** | **63.1** | **470.1** |

The Vulkan start menu at 4K holds 148.5 MB with no world at all (render
targets 73.2, readback 31.6, staging 31.6); GLES's 63.3 (its interface
texture 15.8).

**From the code** (2026-10-20): byte counts are arithmetic on the stated
formats (139245 corners on 63079 shared vertices; the street's textures 3968
KB at 16 bits, `3ds-port.md:750-760`).

| item | Vulkan (`vkrender.cpp`) | GLES - Vita, Quest (`glesrender.cpp`) | GL1 - iBook (`gl1render.cpp`) | 3DS (`c3drender.cpp`) | the original |
|---|---|---|---|---|---|
| set vertices | 48 B a CORNER, unindexed, host-visible (`:155`, `:2103`): **6.7 MB** | 36 B a corner, unindexed (`:738`, `:2205`): **5.0 MB** | none resident: CPU transform, client arrays, 16-bit indices | 28 B a VERTEX, **indexed** (`VertexIndex`): **2.0 MB** | the file's vertices, transformed into a 75000 x 48 pool each frame |
| textures | **RGBA8**, per texture, mips only with trilinear (`:1905`): ~7.9 MB | **RGBA8** (`:1936`): ~7.9 MB | **RGB5_A1**, scaled by the original's rule (`texscale.h:34-47`) | **RGBA5551** in VRAM: 3968 KB | 58 slots of 256x256, 16-bit 1555 or PAL8 on the card, halved until (58+5) x 128 KB fit (`sub_45F4F0`) |
| depth | D32S8 first, 8 B/px (`:769`) | D16 (`:1707`) | D24 | D24S8 | 16-bit |
| pose buffer | **8 MB** fixed, host-visible (`:388`) | uniforms | - | resident pose verts | - |
| shadow map (an enhancement) | 1024^2 D32 = 4 MB, created at init whatever the setting (`:842-869`) | 1024^2 **RGBA8 + D16** = 6 MB, created on demand | - | - | none |
| per-frame sync | `vkWaitForFences` after the scene (`:2638`) and the present (`:1582`); `vkQueueWaitIdle` per one-shot (`:582`) | none in the GPU-presented frame; `glReadPixels` only on CPU composite | `glFinish` per overlay frame | one frame behind | `Flip(DDFLIP_WAIT)`, no wait in the frame |

## 5. The low-end targets - the last device figures

(From the handoffs; the dates are the device runs. **No target has run on its
device since 2026-10-11**, and every optimization made since is "built, not
run".)

| target | last run | street | menu | other | memory |
|---|---|---|---|---|---|
| **PS Vita** (GLES/vitaGL, 960x544) | 2026-10-05 (`handoff-vita-port.md:182-215`) | sim+draw 22-25 + present 2-7 = **~27-31 ms**, at 30 fps. It was 40-50 on 09-30 | no recent figure | area arrival **2480 ms** frame, set load 1292 ms (09-30); first line 258 ms | heap 192 MB, CDRAM 128 MB; no "used" figure recorded |
| **New 3DS** (citro3d, 800x448) | 2026-10-07, 8th run (`3ds-port.md:600-678`) | 34 / 47 / 59 ms by draw density; **GPU fill ~half**; at 400x224, 30-40 ms | not measured | load frame ~1 s | VRAM full by design (3200 of 3968 KB textures); heap 42 + linear 12 MB. The Old 3DS is short by ~12 MB of heap |
| **iBook G3** (GL1, Rage 128, 8 MB) | 2026-10-11, 7th run (`handoff-classic-mac.md:902-916`) | **103.7 ms** (~10 fps); `world: mirror` 77 ms of an Anekbah frame | 88.8 ms (`present: rows` 44.9, fixed since, not run) | fight 135 ms (overlay 42.9, fixed since, not run) | ~80 MB live against the 64 MB goal (old, OS 9) |
| **Quest 2** (GLES 3, XR) | 2026-10-07 | 72 fps at 1.0x and 1.1x | - | shoot/fight upload lag fixed, not played | none recorded |

## 6. The ranked list - what to do, low-end first

Each row: the cost, the original's mechanism where it has one, the target it
helps most, and whether frames stay identical (the `optimization.md` rule:
**a step that moves a pixel is a bug in the step** unless labelled).

### 0. OPEN (2026-10-21): the reader's 4K slowdown with the reflecting floors

Qalisar at 4K, `--remaster`: ~16 fps after walking the pavement back and
forth, GPU-bound, not reproduced headless - the whole record, and the next
step (GPU timestamps per pass), in `todo/remaster.md` (A3 continued, "OPEN
BUG"). Measure only with no other viewer running: a game left open beside a
measurement voided two rounds of it.

### 0b. DONE (2026-10-21): the interface at 4K - 8.9 -> 18.4 fps with a menu open

The reader's play test: the inventory at ~22 fps at 4K; the start menu
measured worse - "screen draw" 77 ms a frame, the whole frame 112 (M3,
`--remaster --enhance-all`, screen 31 by `--keys 1`). A `sample` named two
costs, both now cut with every pixel the same (8 of 8 menu frames
byte-identical to `1f3ff2fa`: software, Vulkan, GLES, 1280x960 and 4K):

* `blt`'s scaled rows on the thread pool (`ui/surface.cpp` `rows`): each row
  alone, writing only itself - a blit of a quarter of a 640x480 frame and up;
  the column tables passed BY REFERENCE (a `thread_local` name inside a
  worker's row is that worker's empty copy).
* `fillQuad` (`ui/screendraw.cpp`, the screens' translucent fill and the
  full-screen dim): the overlay planes looked up once, and a big fill's blend
  taken from a TABLE of all 65536 565 colours made by the same integer
  arithmetic, kept while colour and alpha are the same.

"screen draw" 77 -> 25.5 ms, the frame 112 -> 54.5. Left: the world still
drawn and read back behind a full-sheet screen (~26 ms) - a sheet may have
keyed holes, so skipping it wants a proof that none shows; the many small
blits (glyphs, tiles) run serially.

### A. Before writing any code: run what is built

1. **One device session per target on HEAD.** It costs a run and changes no
   code. The Vita (overlay by block, steps 36-38, the load gate, `make
   vita-tuned`), the iBook (3i-3o: the fight's 42.9 ms overlay, the menu's
   44.9 ms present), the 3DS (6.2-6.7, texture rule, runs A-H) are each
   waiting on one. Until they report, every estimate below for those
   devices is a desk estimate. This is the highest-value step on the list.

### B. Exact, small - frames identical by construction

**B2-B5 DONE 2026-10-20 - section 8.** B4 and B6 are NOT done; the reason is
given at each.

2. **DONE - the GLES readback buffer allocated on first use**, not at init or
   resize (`glesrender.cpp:1400/1418/1611/1725`). It saves 2 MB on the Vita,
   3.7 MB at 960x544 with `--ssaa 2`, and 47 MB at 4K. Nothing reads it on a
   GPU frame.
3. **DONE - the GLES upload scratch released (or capped) after a set's upload**
   (`up_`, `:1163`). It saves ~4.8 MB of shared RAM on the Vita and the Quest.
   Keep it for the per-frame bodies, which are ~16 KB each, and drop the
   set-sized high-water mark. A chunked upload of 64K corners at a time also
   bounds it.
4. **NOT DONE - the Vulkan shadow map created on first use**
   (`vkrender.cpp:842-935`). It would save 4 MB of device memory when the
   enhancement is off, which is the default. But the main pipeline's
   descriptor set binds the shadow sampler and view (`:1330`), and the light
   UBO is made in the same block. A lazy map therefore needs a 1x1
   placeholder and a descriptor rewrite when the setting is turned on live,
   which is not a small change. Vulkan also runs only on modern hosts.
5. **DONE - the Vulkan pose buffer sized from the bodies actually posed**
   (`kPoseBytes`): 8 -> 2 MB. The street walk's busiest frame writes 288 KB.
6. **NOT DONE, CANNOT BE TESTED HERE - Vulkan depth D24S8 before D32S8** (`:769`).
   MoltenVK on Apple silicon has no D24S8, so the M3 would pick D32S8
   either way and could prove nothing about the other path. It halves the largest
   target (8 -> 4 B/px, more under MSAA x SSAA). The engine's depth
   decisions are the tie's and the 16-bit original's; check with `engine: gpu
   present` and the mirror checks that the picture is identical before taking
   it.

### C. Exact, medium - the original's mechanisms not yet carried over

7. **The interface composed at 640x480 and scaled on the GPU** - the
   original's way (`sub_4B19C0` / `sub_4B1B00`, 1:1 at 640x480, then the
   card). Today `fb` is display-sized (15.8 MB x2 at 4K) and every
   full-screen primitive writes every display pixel: 4.7 ms of M3 CPU in the
   4K menu, the GLES 4K menu capped at 60 fps by the upload, and the same
   shape on the Vita at 960x544 (step 28 rows b, c, o). Compose at the game's
   own 640x480 into a 640x480 texture and let the present pass scale it, as
   the GPU already scales the world. The ONE caveat is fidelity at the
   output: today's display-res compose draws text and lines sharp at 4K. A
   640x480 compose is what the original drew, so it is the faithful default,
   and display-res stays an enhancement option (`enhancements.md` rule:
   enhancements off by default). Saves CPU on every target and RAM that
   grows with the screen.
8. **Indexed set geometry on GLES and Vulkan.** This is the original's
   transform-each-vertex-once (`Render_SubmitMesh` 0x004951C0) and what the
   3DS (`VertexIndex`) and GL1 already do. Anekbah: 139245 corners -> 63079
   vertices, so GLES 5.0 -> ~2.5 MB and Vulkan 6.7 -> ~3.6 MB, and the
   vertex shader runs ~2.2x less. The 3DS measured no GPU gain on its
   console (<= 5%, `handoff-3ds-port.md:216-222`), so on the Vita expect
   memory, not speed. The risk is already written down (`ram-vs-original.md`,
   "DEFERRED"): keep the CORNER index as the shared address and the vertex
   index beneath it. The 3DS's code is the template.
9. **16-bit textures on GLES** (RGB5_A1, the original's card format and what
   GL1 and the 3DS already upload). Street textures 7.9 -> 3.9 MB, and half
   the texture bandwidth on the SGX543 and the Quest. **It moves pixels**
   inside the 5-bit step: the original's D3D device had exactly this, so it
   is fidelity, but it needs the reader's call and a new reference picture
   for the GLES checks. Vulkan can keep RGBA8 (modern hosts).
10. **The original's texture size rule on GLES under a memory budget**
    (`texscale.h`, already on GL1 and the 3DS): only where the device
    reports little memory - an old Android/ANGLE GPU, the Old 3DS. On the
    Vita and the Quest it is not needed, so do NOT turn it on there.
11. **Vulkan: the fence waited at the point of reuse, one submit a frame**
    (`cpu-vs-original.md` tier B, deferred). It overlaps CPU and GPU as the
    flip chain did. On the M3 it is ~1.5 ms; on a low-end Vulkan device (an
    Android phone, a Steam Deck in power-save) it is the difference between
    serial and parallel frames. The seven one-shot `vkQueueWaitIdle`s need
    auditing first.
12. **Area loads spread over frames** - `Async_LoadDuringFrame`, one 128 KB
    chunk a frame from `Game_Tick` (`optimization.md` step 28 row f). The
    Vita's arrival frame was 2480 ms. Step 35's load gate hides the WAIT;
    what remains on the frame is the Session's tick (`game frame` 1530),
    `props, guns` 442, `screens` 297 and the rebuild's 139 ms of soups and
    grids. Size them on the console first (row A1), then move the rebuild to
    the set's worker.

### D. Changes a decision or a picture - the reader's call

13. **A lower render target on fill-bound GPUs** - the 3DS at 400x224 runs
    ~30 fps where 800x448 runs ~20 (`handoff-3ds-port.md:240-246`), and the
    iBook could do the same. The original ran at 640x480 or lower. A
    per-platform DEFAULT, never a change to the engine's answer.
14. **Crowd density / clip distance defaults per platform** - the game's own
    options rows 3, 6 and 7 (`optimization.md` "not in scope"). A platform
    may default them lower.
15. **RGB565 render targets** (3DS `c3d-rgb565`, -4 ms of present; GLES
    960x544 -0.5 MB). Without the dither it posterises (3DS run E). Only with
    the dither moved into the target's own write.

### What is NOT worth doing (measured)

* **Anything for the desktop's 3D frame**: 1.3-3.7 ms of an 8.3 ms vsync
  frame on the M3, at 640x480 and at 4K.
* **Texture compression (PVRTC/ETC/DXT)**: no backend has it, and the
  street's textures are already 4.77 MB of indices in RAM. On the GPU, 16-bit
  (row 9) is the original's own answer and moves fewer pixels.
* **Mipmaps by default**: the original had none (`MIP NONE`). They are an
  enhancement, at +33% memory.

## 7. Suggested order

1. A1 - the device sessions (nothing to build).
2. B2-B6 - an afternoon. Each is exact, each needs a check that FAILS when
   the buffer is read on a GPU frame (PORTING B2).
3. C7 - the interface at 640x480. This is the one change that helps every
   target's CPU and RAM at once, and the original does it this way.
4. C8 / C9 - the Vita's memory, behind the reader's go for C9's pixels.
5. C11 / C12 - after the device logs from step 1 say how much they are worth.

Captures of this audit (not committed): the scratchpad's `prof/*.prof`;
re-run them with the commands in §1.

## 8. Done 2026-10-20 - the readback buffers on demand (B2, B3, B5, and Vulkan's readback)

The measured GPU memory (section 4) showed one more item than the code
reading did. Vulkan made its world readback (`readBuf_`, `rw x rh x 4`) and
its present readback (`presRead_`, `w x h x 4`) at the start: 2.3 MB at
640x480 and **63 MB at 4K**, and a GPU-presented frame reads neither. The
original has no counterpart: `Flip` presents what the card drew.

| change | where | street 640x480 | 4K |
|---|---|---|---|
| Vulkan `readBuf_` and its `fb_` made by the first `readback` | `vkrender.cpp`, the target's allocation and `readback()` | GPU -2.34 MB (`presRead_` too) | GPU -63.3 MB, CPU -16 MB |
| Vulkan `presRead_` made by the first `worldPicture` | `makePresentPass`, `worldPicture` | (in the above) | (in the above) |
| Vulkan pose buffer 8 -> 2 MB | `kPoseBytes` | GPU -6 MB | GPU -6 MB |
| GLES `rgba_` and `fb_` made by the first `readback`, released at a resize | `glesrender.cpp` `init`, `readback()` | CPU -1.2 MB (+0.6 `fb_`) | CPU -33 MB (+16 `fb_`) |
| GLES upload scratch released past 16384 corners | `uploadGeometry`, `TrimScratch` | CPU -4.7 MB | CPU -4.7 MB |

Measured after (the same 900-frame street walk, M3):

| run | GPU memory | heap live | peak footprint |
|---|---|---|---|
| Vulkan 640x480 | 36.6 -> **28.2 MB** | 38.8 -> 38.2 MB | 246 -> 237 MB |
| Vulkan 4K | 199.0 -> **129.7 MB** | 69.3 -> 53.5 MB | 521 -> 431 MB |
| GLES 4K | 63.1 -> 63.1 MB | 107.5 -> **55.4 MB** | 633 -> 583 MB | The street walk released
the scratch ONCE in 900 frames: the set's own 139245-corner upload.

**Frames identical**, old against new. HEAD was built in a worktree and the
same 600-frame street walk run on both binaries: Vulkan, GLES, Vulkan with
`--ssaa 2`, and GLES with every frame read back (`OMK_NO_GPU_PRESENT=1`).
The four dumps are byte-identical and the four logs identical once the
timings are filtered out. `OMK_VERIFY_GPU_PRESENT` found 0 pixels
differing in any GLES frame. Vulkan presented 59 of 60 frames from the GPU
and read back only the dump frame, so the lazy buffer was made in the middle
of a run.

`engine: gpu readback on demand` holds it (3 s). It was shown to fail four
ways, each mutation alone: the eager `readBuf_`, the 8 MB pose buffer, the
eager `rgba_`, and the scratch kept. Run with `--only` and green: `gpu
present`, `supersampling`, `fight gpu present`, `gles backend`, `gles pose`,
`vulkan pose`, `gles overlay`, `profiler gpu`, `gles tie bake`, `mirror
pass`, `pose equivalence`, `street memory`, `profiler` and `profiler
control`, plus `vita build`. Also run: `engine: release build` is RED on its
second element, the `OMKSNAP1` marker absent from the DEVELOPMENT binary.
The binary built at `f5f3266`, before this work, lacks it as well, so the
red is pre-existing and was not attributed.

## 9. Done 2026-11-06 - three per-frame costs found by reading the code

A read-only review of the per-frame paths (the reader's "fix 4.1, 4.2 and
4.3"). None moves a pixel; none is measured on a device yet.

1. **The IAM string files were read from the data EVERY FRAME** a screen
   needing them was open: `iamStrings` for `IAM/Buy` (the shops), `IAM/Save`
   (the hints), the terminal family's file, `IAM/Lift` and `IAM/Multip`,
   each an open, a read and a close plus a `std::string` per entry. Now
   `PlayState::iamText` reads each file once a run and keeps it (the shipped
   data never changes under a run). The cost it removes is the device file
   system's, ~30 opens a second: `sceIo` on the Vita, the SD card on the
   3DS, the File Manager on Mac OS 9.
2. **The profiler's sums were `std::map<std::string, double>`**, written
   ~45 times a frame whether or not anything profiles: a key built each
   time, a heap block for every name past 15 characters on the 32-bit gcc
   targets. `PlayState::PhaseSums` keys them by the literal's pointer;
   the 60-frame report merges names that arrive by two pointers and prints
   them in name order, so the `spans` and `sections` lines are unchanged.
   The setup's own probe now reads `a named span add 0.00 us` on the M3.
3. **The subtitle was re-wrapped every frame**: `layOutBlock` over the
   line and every reply, one vector per row, then each row copied again to
   be toned, plus a `parseMarkup("Ag")` for the pitch and, for a
   `media.play` line, a `parseMarkup` of the whole text. `drawSubtitle` now
   keeps the toned rows of its last two calls, keyed by everything that
   feeds them (the text, the replies, the selection, the box, the face, the
   display and the glyph scale), and the media line keeps its parse.

Checks run with `--only`, all green: `subtitle box`, `credit layout`,
`engine: text scroll`, `engine: dialogue play`, `engine: hint shop`,
`engine: terminal family`, `engine: dossier reader`, `engine: shop open`,
`ui shop titles`, `engine: multiplan rows`, `engine: lift` (and its family),
`engine: profiler` (and `control`, `gpu`, `windows`). `make play play-gles
vulkan` build clean. No check measures a cost, so none can fail on these.

## 10. Done 2026-11-07 - the rest of that review's list

The reader's "finish part 4": items 4 to 6 of the same review. None moves a
pixel or changes a log line on the shipped data.

4. **The slider's door clip was copied every frame of the swing**
   (`playframe_world_crowd.cpp`): the whole `NodeTracks` - a quaternion per
   track per frame - copied, then each track's name compared with every mesh
   name of the model to remap `ids`. The remap now runs in place, once per
   model (`PlayState::doorMappedFor`, keyed by the model NAME: a model is
   one `.3DO` whatever its pointer, and since `f38148a` a released model's
   pointer can be reused).
5. **The WAV chunk walk was in `size_t`** (`audio/mixer.cpp`, `parseWav`):
   `off + 8 + sz` with a 32-bit chunk size wraps a 32-bit `size_t` (Vita,
   3DS, the PowerPC Macs) on a size near 0xFFFFFFFF - the walk loops, or the
   `TooShort` test passes and the PCM copy reads past the file. The walk is
   in 64 bits now. No shipped file comes near; only a damaged one could, and
   on the 64-bit desktops the old sum could not wrap at all - so no check
   here can show it.
6. **A `vis` vector allocated per world slot per frame**
   (`worldDrawSetRuns`): a buffer kept per slot, refilled. And **the
   "said once" log sets never emptied** (`static std::set` of actor numbers
   in the shoot logs, the body push, the prop lighting): those keys are the
   area's own, so after an area change a line for the same number in the new
   area was never printed. `ToldOnce` (`playstate.h`) empties itself when
   `Session::areasEntered()` moves. The members (`propsTold`, `gun.*Told`)
   are reset with their state and were left alone.

## 11. Measured 2026-11-13 - the remaster at 4K on the M1, where 60 fps is lost

The reader: *"le mode remaster en 4K ne tient generalement pas les 60 fps sur
cette machine"*. M1 (MacBookPro17,1), Vulkan, `--res 3840x2160 --remaster
~/omk-pack --framerate 60`, the window SHOWN (the reader's ask), the reader's
own `upscale = 1.5` (FSR quality - their `omk.ini`; a run with a private
`--saves` has none, which the first runs here missed). The street walk of
section 1 at Qalisar (`--area 101 --stand 15300,0,-10050,270`); the rain at
full by `OMK_WEATHER_SKIP_MS=900000`, its ramp (~1000 frames) left out of
every figure. `--hold` counts PRESENTED frames, so a walk at another frame
rate is another route, and a pedestrian can push it into a wall (one run did):
every run here logs its end (`player: ends at`), and the eight compared
ended in the same street.

* **Dry, FSR 1.5: 60 held** (Anekbah 16.57 / Qalisar 16.60 ms median, ~3 ms
  idle). Without FSR Qalisar is ~55 fps.
* **Full rain, FSR 1.5: ~47-50 fps.** Unpaced (`OMK_PRESENT_IMMEDIATE=1`,
  `--framerate 240`) 20.15 ms; the rain's passes off one at a time: bloom
  -1.7, AO -1.25, halos -1.1, veils -0.7, wet floors -0.7, the floors'
  reflection 0; `--rain3d 0` -4.4 - but it takes ALL the remaster's rain
  with it (`weatherRain` is only fed through the 3D rain: no wetness at all).
  The drops' strips, the splashes, the drips cost nothing measurable
  (`OMK_NO_RAIN_STRIPS`, new).
* **The GPU's own times** (`OMK_GPU_TIMES=1`, now with the PRESENT path -
  the upscale, the present pass, the blit): reflection 0.65, scene 4.4,
  post 3.6, FSR EASU 3.2, the present pass (RCAS) 1.5, blit 0.16 - **13.5
  ms of GPU in a 20 ms frame**. The rest is the three submits a frame
  (scene; upscale + present pass; blit + present), each waited for whole
  (`vkWaitForFences`, `oneShotEnd`'s `vkQueueWaitIdle`) - the GPU idle while
  the CPU waits and records the next. That is C11 (§7), now the world's
  first lever.
* **A screen open (the sneak): 30 fps** - 32.6 ms, `screen draw` 25 ms of
  CPU, the GPU 1.7 ms. The interface composed at the display's size, C7.

Order from here: C7 (the screens), then the submits pipelined (C11), then
the passes themselves (EASU 3.2, post 3.6).
