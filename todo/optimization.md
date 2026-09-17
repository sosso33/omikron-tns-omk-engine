# Optimization - the frame costs the port adds, and a PS Vita as the yardstick

Omikron shipped for a Pentium II with 32 MB of RAM and was ported to the
Dreamcast. A replica of that engine that cannot hold 30 fps on a PS Vita
(Cortex-A9, 512 MB, homebrew MAIN budget 365 MB with `ATTRIBUTE2=12`) is not
paying for the game - it is paying for the port. This file is the list of what
the port pays for, measured, and the order to take it in.

**Nothing here changes what is drawn or decided.** Every step replaces HOW an
answer is computed, never the answer, so every step's check is "same output,
less time" - and a step that moves a pixel or a position is a bug in the step.

## How it was measured (2026-09-12)

`sample <pid> 15` (macOS) over `omk-play` on Anekbah's main street,
25 characters staged, density from `traces/save-appart.bin`:

```
build/omk-play ../gamedata ../tables --save ../traces/save-appart.bin \
    --area 0 --stand 1804,0,-6890,336 --vulkan --nofmv --frames 1200
```

then the call tree attributed by caller (the script is trivial: parse the
indentation of `sample`'s call graph and sum each hotspot's parents).

**LABELLED: the machine was heavily loaded** - load average 18 to 40 while a
parallel session compiled and ran checks - and the reader reports the port
normally runs noticeably faster than that run did (1200 frames in 59 s wall,
start-up included). So the absolute milliseconds below are INFLATED, plausibly
by 2x or more. The RANKING and the call attribution are what this file rests
on; re-measure on an idle machine before quoting a number anywhere else.

The software renderer is not the baseline for any of this: it is unplayable in
the street even on the M1 (93% of its samples in `drawGeometry`), which says
nothing about the engine and everything about a CPU rasterizer at that size.

## The hot spots, by cost

About 300 frames fell inside the 15 s sample; per-frame figures are
samples / 300, under the load caveat above.

| # | cost / frame | where | what it is | the game's? |
|---|---|---|---|---|
| H1 | ~17 ms | `floorUnder`, 5042 of 5118 samples from the `castBones` lambda, `backends/sdl/play.cpp` ~14455-14540 | the character SHADOWS' ground probe. `Actor_DrawShadow` (0x00467E20) does probe straight down from each shadow bone (`World_ProbePoint(-1, ...)`, `docs/ASSETS.md` §"The actor path"), so the PROBE is faithful. What is not is that the classic path (the default) hands `floorUnder` the whole `playerSoup` - every walkable triangle of the resident sets, 15137 in Anekbah - so each bone of each staged body is a linear scan of the city. The fitted path already gathers a `soupInBox` once per body; the classic path does not | probe yes, scan no |
| H2 | ~3 ms | `std::set<std::array<uint32_t,9/12>>` inside `VulkanRenderer::submit` → `resolveTies`, `backends/vulkan/vkrender.cpp` ~310-400 | the coincident-face DEPTH TIE. It is written to run once per geometry REVISION, but posed bodies and the per-frame shadow geometry bump their revision every frame, so the two ordered sets are rebuilt from scratch each frame for every dynamic geometry | no - backend bookkeeping |
| H3 | ~2 ms | `surfaceUnder`, `play.cpp` ~14558 | the CROWD's foot shadow: one whole-soup scan per drawn pedestrian | scan no |
| H4 | ~2 ms | `VulkanRenderer::readback()` from `play.cpp` ~14765 | in adventure mode the 3D view is a viewport item composited into the interface on the CPU, so the frame is copied back from the GPU every frame and direct presentation (`play.cpp` ~1290) never applies | no |
| H5 | ~4 ms | `main` self time, `main::$_37` (317) | not broken down yet | unknown |
| - | small | `applyLights` 118, `sweepSphere` 112, `applyPose` + `composePose` ~84, `MusicPlayer::pull` | the crowd lights, the walker's wall sweep, posing, the music stream | fine for now |

Memory, same runs: **236 MB** peak RSS with `--software`, **1.13 GB** with
`--vulkan` (on the M1 the latter includes the GPU's unified memory, so it is
not a CPU figure). Against the original's 32 MB, both are the port's.

## Steps

| # | step | state |
|---|---|---|
| 1 | a clean BASELINE: the same street run on an idle machine, per-frame time and the sample, recorded here | **DONE** 2026-09-13 - see "Before and after" below |
| 2 | a SPATIAL GRID over the walkable soup (H1, H3): built once per resident set, `floorUnder` / `surfaceUnder` walk only the cells under the probe | **DONE** 2026-09-13 - the street from 24.2 to 45.2 fps, frames byte-identical, `engine: probe grid` |
| 3 | the depth tie only where it can matter (H2) | **DONE** 2026-09-13 - same decisions on hashed sets; uncapped 42.7 -> ~51.5 fps, capped CPU 72 -> 57; `engine: tie equivalence` |
| 4 | the interface composited on the GPU in adventure mode (H4) | **the conversions DONE** 2026-09-13 - readback and upload by table, uncapped ~51.5 -> ~59.5 fps, capped CPU 57 -> 49, `engine: pixel tables`. **4b DONE** 2026-09-14 - a frame nothing is drawn over is dithered and presented on the GPU: same binary on/off/on, capped CPU time 12.9 / 15.1 / 14.0 s, `engine: gpu present`; frames WITH interface still round-trip |
| 5 | break down `main`'s own time (H5) and re-rank | open |
| 6 | the memory pass: where 236 MB goes, against a 365 MB Vita budget that vitaGL's textures also come out of | **first cut DONE** 2026-09-13 (as "step 5" in the log below) - music at its own rate, the headless audio queue dropped: live heap 234 -> 140 MB, window footprint 243 -> 185 MB; the rest ranked |
| 7 | re-measure everything, and decide whether the Vita is in reach | **DONE** 2026-09-14 - main thread 6.0 ms a frame capped (11.1 with `--enhance-all`, 6.9 of it the supersample resolve), live heap 137 MB; break-even ~5.5x slower than the M1; NOT in reach at 30 fps on one core without GPU skinning / lighting - see below |
| 7a | the moving set meshes' per-frame patch (found by the step-4 re-profile) | **DONE** 2026-09-13 - a per-mesh index and an in-place merge; capped CPU 50 -> 34% of a core, `engine: patch index` |
| 7b | the depth tie re-keying the whole set every frame (the cargo moves) | **DONE** 2026-09-13 - claimed keys stored flat with a fingerprint; back to back capped CPU 39 -> 36%, run CPU time -7.6% |
| 7c | the ground probe grid rebuilt from scratch every frame (the cargo moves) | **DONE** 2026-09-14 - two layers, fixed and moving; the per-frame rebuild 0.528 -> 0.016 ms, probes no slower; `engine: split grid` |
| 8 | a revision that says WHICH corners moved: the set's vertex upload and depth tie take only those | **DONE** 2026-09-14 - exact (tool and game); back to back capped CPU 28 -> 25%, run CPU time -11%; `engine: dirty corners` |
| 11 | the player's body and camera sweeps through grids - a steep-soup grid beside `playerGrid` | **DONE** 2026-09-14 - exact (tool, play, frames); `engine: sweep grid` |
| 10 | the posed bodies: `applyPose` in place instead of copying the rest geometry first | **in progress** 2026-09-14 - exact (tool, frames); 2000 poses 62 -> 19-25 ms; capped with steps 9 and 11: ~1-3% standing; `engine: pose equivalence` |
| 9 | the walker's ground probe and `decorUnder` through the grid (step 2's grid never reached them) | **DONE** 2026-09-14 - exact (tool and game); ~0.5 ms a frame timed directly (0.17 ms a linear probe, ~3 a frame); capped with steps 10-11 standing: 18.6 -> 18.1-18.4 s CPU, at the edge of the noise (see below); `engine: ground grid` |

Each step ends in a commit and a report, per the working rhythm; the full
sweep follows the cadence in `todo/sweep-log.md`, not these steps.

## Before and after - step 2 (2026-09-13)

**How it is measured now** (scratch scripts, not committed): the street start
above on Vulkan with `--fps --frames 1500`, the viewer's own once-a-second
rate and worst frame, and every 2 s the process's CPU and RSS (`ps`), the
GPU's device / renderer utilisation and in-use memory (`ioreg -c
IOAccelerator` - on the M1 the GPU's memory IS system memory, so "VRAM" is
that figure), plus one `footprint` snapshot. The GPU figures are system-wide,
so an idle reading is taken first and subtracted. Load average 5-7 during
both runs (other applications), the same for both.

**Two measurement traps, both of which gave a plausible wrong reading:**
* `--frames N` switches OFF the 30 Hz cap (`if (!frames)` around the pacing
  sleep) AND fixes the step at 1/30 s a frame for determinism - so a
  measured run is uncapped and its WORLD runs at fps/30 times real speed. The
  rate it reports is the cost; what a person sees in that window is not the
  game's speed. A run to look at must omit `--frames`.
* peak RSS of two WINDOW runs is not comparable: the faster run reached the
  same frame count in half the time and was sampled at a different moment of
  play (540 -> 914 MB, which looked like a regression). On an identical
  headless workload (`--software --frames 60`) the two binaries peak at
  **234 / 232 MB** RSS and **226 / 227 MB** footprint - no change.

| | before (`db1f548`) | after (grid) |
|---|---|---|
| fps, median of 1 s windows | **24.2** | **45.2** |
| fps, slowest window | 11.1 | 31.7 |
| worst frame, median of windows | 44 ms | 25 ms |
| CPU, mean (100 = one core) | 96 | 92 |
| GPU device / renderer utilisation (idle 12-13) | 12 / 12 | 19 / 17 |
| GPU in-use memory above idle | +56 MB | +111 MB |
| peak RSS, identical headless 60 frames | 234 MB | 232 MB |

Reading it: the main thread is still one saturated core, as an uncapped run
must be, but it now makes nearly twice the frames; the GPU, idle before, does
more because it draws more. The game's cap is 30 fps, so the street now holds
it with room (the slowest second is 31.7) where it could not before.

**Same output:** 20 headless software frames of the street start are
byte-identical before and after (960000 bytes, 133 shadow blobs in each).

**The grid itself** (`engine/tools/probe_grid.cpp`, both soups, 7 sets:
Anekbah, Aapkayl, AImpasse, Lahoreh, jaunpur, Qalisar, Sprison): **0
mismatches** in about 250000 probes and 7000 boxes - centroids, vertices, edge
midpoints, random points past the extent, points exactly on cell boundaries.
Anekbah's walkable soup: 15137 triangles, a 78x71 grid of 256-inch cells,
35465 entries, built in 0.56 ms; 18098 floor probes take **2165 ms** linear
and **4.6 ms** on the grid. The city sets gain 250-700x; the two smallest
sets gain little and `soupInBox` there is marginally slower, which costs
nothing that matters.

**What changed:** `SoupGrid` / `buildSoupGrid` and grid overloads of
`floorUnder`, `surfaceUnder` and `soupInBox` in `o3de/collision.*` - the
linear versions untouched, the per-triangle arithmetic copied statement for
statement, candidates in ascending order, a grid that does not match its soup
falling back to the scan. `omk-play` keeps `playerGrid` beside `playerSoup`,
rebuilt at both refills (area change, a moved door), and hands it to the
classic shadow probe, the fitted shadow's `soupInBox` and the crowd's foot
probe. The walker, the staging and the ride still scan linearly: once a
frame or less, not worth the change yet.

**Check:** `verify.py: engine: probe grid` (Anekbah, AImpasse, Sprison, both
soups). SHOWN TO FAIL: `kGridEps = -1.0` gives 65 / 96 / 236 / 1143 / 154 /
193 mismatches; restored by editing the line back, green again. The other
checks this could touch all pass: `engine: character shadow`, `fitted
shadows`, `city crowd`, `crowd push`, `walker falls`, `shoot ray soups`.

### Capped at 30 - the rate that matters (2026-09-13)

The game's animations are 30 Hz and the port interpolates nothing, so a frame
past 30 shows nothing new: the reader asked for the comparison CAPPED. That
means launching WITHOUT `--frames` (so the viewer's 30 Hz cap and its real
clock apply) and closing the window after a fixed time - 45 s of play past a
12 s start-up, same street, same sampling. With the cap on, CPU is headroom.

| capped, 45 s | before (`db1f548`) | grid (`86c82ea`) | grid + deadline pacer |
|---|---|---|---|
| fps, median of 1 s windows | 23.9 | 28.8 | **30.0** |
| fps, slowest window | 15.4 | 28.6 | 29.3 |
| worst frame, median of windows | 44 ms (windows up to 145) | 37 ms | 39 ms |
| CPU, mean (100 = one core) | 96 - saturated | **62** | 72 |
| CPU time over the run | 50.6 s | 34.5 s | 38.2 s |
| physical footprint | 260 MB | 243 MB | 241 MB |
| frames presented | 1212 | 1541 | 1545 |

* **Before**, the street could not reach the cap at all and hitched.
* **With the grid** it was pinned against the cap with a third of a core
  spare - but at a FLAT 28.8 in every window, which was the cap's own fault:
  it slept `33 - spent` whole milliseconds, a 33 ms budget plus
  `SDL_Delay`'s oversleep, ~34.7 ms a frame.
* **The deadline pacer** (`play.cpp`, the bottom of the adventure loop) ends
  each frame on a 1/30 s grid of the performance counter - whole-ms sleeps to
  1.5 ms short, then yields; a late frame shortens the next, a stall of more
  than a frame resynchronises. It holds **30.0**. The yield costs about ten
  points of one core, and the worst frame widens a little (37 -> 39 ms)
  because a late frame is now followed by a short one to keep the average.
  The simulation is untouched (it steps on the measured delta) and
  `--frames` runs never reach the pacer.
* GPU utilisation is not comparable across these three: the idle reading
  before the pacer run was already 78% (another application), against 11-12%
  for the other two.

**Next, re-ranked:** the step-2 sample is not retaken yet; from the first one,
H2 (the depth tie rebuilt each frame, ~3 ms) and H4 (the readback, ~2 ms) are
now the largest known costs, and `main`'s own time (H5) is unexplained.

### 1. The baseline

Idle machine (no parallel builds - check `uptime`), same command, `sample` for
15 s after the splash. Record: frames / wall time for the whole run, per-frame
cost of each row above, peak RSS for `--software` and `--vulkan`. Also one run
with `--no-shadows` and one with `--no-crowd`: if H1 and H3 vanish with them,
the attribution is confirmed from outside the profiler.

Two traps met while taking the first sample, both of which returned a
plausible WRONG answer rather than an error:

* `pgrep -n -x omk-play` found ANOTHER session's `omk-play` (a worktree,
  software-rendered) and the sample was of that. Pick the pid by the full
  command line AND `comm == build/omk-play`.
* a "wait until no `make play` is running" loop matched ITS OWN shell's
  command line and waited for ever. Write the pattern so it cannot match
  itself (`pgrep -f "[m]ake play"`), and note that a parallel worktree's
  builds never touch this tree's binary, so waiting on them is wrong anyway.

### 2. The spatial grid (the big one)

**Read first, per the standing rule, before writing the structure:** what
`World_ProbePoint` walks. `docs/RECONSTRUCTION.md` 2026-08-28 names it in the
spatial/collision service layer beside the spatial index; how it reaches the
triangles under a point has not been read. The grid should reproduce the
engine's decision (the same surface chosen, the same tie-breaks between two
floors under one point) - the ACCELERATION may be our own, the ANSWER may not.

#### Read 2026-09-12: the engine does not scan triangles, it culls MESHES

(`readable/src/16_o3de.c`; bodies still as generated, so these are readings of
the control flow, not of every field.)

* `World_ProbePoint` (0x004433B0, 34 callers) builds a DEGENERATE BOX - x..x,
  y..FLT_MAX, z..z, the vertical line from the probe point downward - and
  hands it to `o3de_ForEachMeshInBox` with the callback `sub_4434B0`.
* `o3de_ForEachMeshInBox` (0x004430A0) walks each resident scene's FLAT node
  array (46 dwords = 184 bytes a node, count at the model's `desc+224`) - no
  hierarchy descent. Per node it takes a BOUNDING SPHERE: centre
  `node[19..21]`, turned by the node's matrix at `node+14*4` when the mesh
  carries `0x80000`, plus the node position `node[9..11]`; radius
  `node[22]`. A node whose sphere's box meets the query box is passed on.
* `sub_4434B0` skips meshes with `0x41`; a `0x80000` mesh goes to
  `sub_444460` with the probe set up in the mesh's own frame; every other
  mesh has only the node position subtracted and goes to `sub_498930`.
* `sub_498930` classifies the mesh's VERTICES against the probe point first
  (an outcode array sized by the mesh's vertex count, `mesh+64`) and then
  walks its faces - trivial rejection per vertex, not per face.

So **`0x80000` is a ROTATED mesh** as far as this path is concerned. The port's
mesh reader already has the fields: position `+36`, centre `+76..+84`, radius
`+88` (`formats/mesh3do.h`), and every soup already records which mesh each
triangle came from (`soupMesh` / `steepMesh`, what `patchSoup` uses to move a
door). A moving mesh therefore moves only its sphere.

**Measured on Anekbah** (860 meshes, 15467 triangles + 15482 quads; none
`0x80000`, none `0x41`): over 401 probe points (400 random walkable-triangle
centroids and the street start), the meshes whose sphere reaches the vertical
line number **median 6, max 14**, carrying **median 733 faces, p95 1235, max
1736** - against **15161** walkable triangles the linear scan tests today, and
those face counts include walls, so the real triangle tests are fewer. The
street start is 3 meshes, 378 faces. Centre `+76..+84` is zero across the
city, so position-only and position+centre agree here; the code must still
add it. Radii: median 153, max 1871.

**The design this points to, and it is the engine's own:** group each
resident set's walkable and steep soups by mesh, keep one sphere per mesh
(updated where `patchSoup` moves one), and give `floorUnder` /
`surfaceUnder` / `soupInBox` a front that tests spheres first. That is a
~20x cut from the broad phase alone. The 860 sphere tests per probe are then
the remaining cost; a coarse XZ grid over the SPHERES (not the triangles)
removes most of those and cannot change an answer, because it only skips
meshes the sphere test would have rejected.

**Not yet read, and needed before the rotated path is ported:** `sub_444460`
(the `0x80000` narrow phase) and the tie-break between two floors under one
point - the linear scan keeps the nearest hit below `y + 1`, and whether the
engine's per-mesh walk agrees when two meshes offer the same height has to be
checked, not assumed.

Shape: a uniform XZ grid over each resident set's walkable soup (and the steep
soup `surfaceUnder` reads), triangles bucketed by their XZ bounding box, built
once when the set is loaded and dropped with it. The two resident slots each
own one; `decorUnder` asks both. `soupInBox` becomes a cell walk.

Check - the one the data can fail: for EVERY probe the street run makes over
N frames, the grid's answer is bit-identical to the linear scan's (same y,
same normal, same "nothing"). Run it as a `--slow`-free engine check over a
recorded probe list, not a render. Then re-sample: H1 and H3 should fall to
well under a millisecond together.

### 3. The depth tie

#### Done 2026-09-13

**Why it cost so much.** The Vulkan backend's submit-time tie pass kept two
ordered `std::set`s of sorted vertex positions per geometry and rebuilt them
from nothing at every REVISION. A revision changes every frame for every posed
body (`s.posed`, `p.posed`, `sv.posed`, `playerPosed`), the shadow quads, the
effects and the sky - and for the WHOLE decor set whenever any mesh in it moves
(`play.cpp`, `w.geo.revision = ++worldGeoRev` beside the corner patch), which
on the Anekbah street is every frame, so all 46415 of the city's triangles
were re-keyed every frame. The re-profile after step 2 ranked it FIRST
uncapped: ~2640 samples in 15 s at 42.7 fps, about 4 ms a frame.

**What changed.** The decision moved to `o3de/depthtie.*` (`DepthTie`), the
backend keeping only the vertex-buffer write, the log and its counters. Same
decisions in the same order - the revision/size reset, the quad pairing, "a
claimed key loses, else a writing draw claims" - on hashed sets that keep
their buckets across revisions, plus one shortcut that cannot change a
decision: a draw that writes no depth while nothing is claimed yet only marks
its range handled. The unused face counter is gone.

**Same decisions, proved.** `engine/tools/tie_equiv.cpp` keeps the old pass
verbatim as `Reference` and compares every draw's losers, in content and
order, over one pass, the same revision again (the mirror), and 40 revisions
with a mesh moved, triangles snapped onto others to force ties, a shuffled
subset of batches and non-writing draws first. Anekbah, Aapkayl, Lahoreh,
HO1_FN, PSH_FN, JEN_FNM: **0 mismatches in 3056 draws, 18097 losers**;
Anekbah's single pass drops **248**, the number `engine: sign tie` pins in its
Vulkan render, which still passes, as does `mirror pass`. The tool's own
timing puts the new pass at about 2.7x the old's speed on the same draws.
`verify.py: engine: tie equivalence`. SHOWN TO FAIL: the shortcut returning
without marking its range gives 63 / 86 / 22 mismatching draws (Anekbah /
Lahoreh / PSH_FN) with the loser totals unchanged; restored, green.

**Before and after** (same street, same method as step 2):

| | grid + pacer (`1070a4b`) | + the tie on hashed sets |
|---|---|---|
| uncapped, typical 1 s window | ~42.7 fps | **~51.5 fps** |
| capped: fps median / slowest window | 30.0 / 29.3 | 30.0 / **29.8** |
| capped: worst frame, median of windows | 39 ms | **35 ms** |
| capped: CPU, mean (100 = one core) | 72 | **57** |
| capped: physical footprint / peak RSS | 241 / 275 MB | 245 / 273 MB |

GPU utilisation is again not comparable (an idle reading of 20% against 78%
before, other applications). The tie pass now costs ~600 samples in 15 s
(key sorting and hash lookups), about 1 ms a frame.

**The re-profile, uncapped, after step 3** (15 s): `main`'s own time 2755,
`VulkanRenderer::readback` 1320 (H4), the per-frame collision-soup patch of a
moving mesh (`main::$_41`, the `patchSoup` lambda) 805, `_xzm_free` 698, 
`buildSoupGrid` 329, key sorting 299, `uploadGeometry` 277. Two of those are
costs these steps brought or exposed and are the obvious next small ones:

* **the frees** are mostly the hashed sets' NODES being released at every
  revision reset (`std::unordered_set::clear` keeps buckets, not nodes) - a
  flat open-addressed table would keep them;
* **`buildSoupGrid` every frame** is step 2's grid rebuilt because the soup is
  refilled whenever a mesh moves, which is every frame on this street - the
  moved mesh's triangles could be re-bucketed alone instead;
* and **the patch itself** (`patchSoup` walks every triangle's mesh index to
  find one mesh's) and **why something on the street moves every frame** are
  worth a look before either.

The tie exists for the two SIDES of a shop sign (18 pairs in Anekbah,
`verify.py: engine: sign tie`), which are static set geometry. Options, to be
settled by reading which geometries have ever produced a dropped face: resolve
only geometries flagged static, or key the state on topology (index layout)
so a pose that moves vertices without changing faces does not reset it.
Check: `engine: sign tie` stays green and the per-frame dropped count is
unchanged on the street run; the `std::set` samples disappear.

### 4. The interface on the GPU

#### 4b. The frame nothing is drawn over stays on the GPU - 2026-09-14

**What the round trip was for.** On Vulkan the adventure frame was drawn on
the GPU, read back (`readback`: 888 -> dithered 565), copied into the 640x480
framebuffer at the letterbox row, drawn over by the interface, and uploaded
again (`presentSurface`: 565 -> RGBA8). Of the blocks that draw over the
picture, several also READ it - the radar samples `fb` pixels, both screen
fades rewrite every pixel from its value - so composing the interface on the
GPU in general would mean porting those reads too. But on most adventure
frames none of them runs, and then the round trip only reproduces the
attachment's own pixels.

**What changed.**

* `backends/vulkan/shaders/present.frag`: the same bytes per pixel, from the
  same attachment - `quantise888Dither` with its truncating division written
  out, the cell taken from the WORLD picture's row (the readback dithers
  before placement), black bands outside `[vy, vy + vh)`, `rgb565` when the
  dither is off, then the bit replication `expand565Rgba` does, written as
  k/255 into a UNORM8 target.
* `VulkanRenderer`: the pass into its own image (own descriptor pool, so a
  texture reload cannot free it), `presentImage` - the swapchain blit that
  `presentDirect` was, now for any image - and `worldPicture` / `probeUpload`
  for the checks. `colour_` gained SAMPLED usage.
* `play.cpp` decides per frame where the world is placed into `fb`: the GPU
  path only when no screen, no shoot HUD, no media bitmap or line, no
  conversation, neither screen fade, no flicker or clip log, no snapshot and
  not the final `--dump` - each the test that gates the block further down -
  and only at `ss 1`. `OMK_NO_GPU_PRESENT=1` turns it off,
  `OMK_GPU_PRESENT_STATS=1` counts the frames, `OMK_VERIFY_GPU_PRESENT=1`
  composes the CPU frame as well and compares every pixel (and runs in the
  offscreen `--world-vulkan` harness too, which is how it is checked
  headless).

**Same bytes, proved two ways.** `engine/tools/present_probe.cpp` (built by
`make vulkan`) loads a 4096x4102 picture holding every 24-bit colour into the
attachment and runs the pass 3 rows down, sixteen times with the colours
shifted so each meets all 16 dither cells and once with the dither off: 0
mismatched pixels in all 17 runs, bands included. In play, the street start
through `--world-vulkan`, standing and walking, 90 frames each: 89 of 90 frames
took the GPU path (the last is the `--dump`), all compared equal to the CPU
composite, and the dumped last frame is byte-identical to the step-9 build's.
The shoot phase start (`--area 230 --scene-chunk 56`, 240 frames) keeps 237 on
the CPU path - and `OMK_GPU_PRESENT_STATS` names the gates: the BLACK fade 226,
the colour fade 11. That corrects a first reading that the HUD gate was
holding them; the phase's HUD never comes up in those frames, so nothing here
exercises it. (A 600-frame run of the same start took another path - no
renderer line, no stats - and was not chased.) `engine: gpu present` (new); `dither`,
`pixel tables`, `sign tie`, `mirror pass`, `shimmer`, `supersampling`,
`anti-aliasing`, `texture filter`, `mipmaps`, `dirty corners` and
`ground grid` pass.

**SHOWN TO FAIL, and two lessons in it.**

* The shader taking the dither cell from the WINDOW row instead of the
  picture's: 173169326 mismatched probe pixels. It needed the probe's picture
  3 rows down: the first version placed it 4 rows down, and 4 - like the
  street's 0 and the letterbox's 64 - puts both rows in the same cell, so
  that mutation would have passed every test here.
* Green's rounding `+ 127` made `+ 128` stays green, and cannot do otherwise:
  the two differ only where `63 v mod 255 = 127`, and 3 divides 63 and 255
  but not 127. An equivalent mutation, not a blind check; red and blue's
  `31 v` has solutions and is the one to mutate - and red's `+ 128` gives
  1048576 mismatched probe pixels, all 60 compared street frames differing
  (272723 pixels) and all 3 of the shoot phase's.
* The black fade's gate taken out: 229 of 240 shoot-phase frames go to the
  GPU and the count moves (`black fade 226` gone) - and 0 of them differ. So
  the check caught it by the COUNT alone, and it says something about the
  gate: for those 226 frames the fade is running but draws nothing (the block
  only touches pixels while a band grey is under 255), so the gate is
  conservative there. A LEAD, not taken: gating on the block's own draw
  condition would put a cutscene's opening frames on the GPU too.

**Measured - the same binary, the path switched by `OMK_NO_GPU_PRESENT`**
(capped at 30, 45 s each, on / off / on, load 2.2-3.5 throughout - a quiet run):

| capped, street start | GPU present | off | GPU present again |
|---|---|---|---|
| fps median / slowest window | 30.0 / 29.9 | 30.0 / 29.9 | 30.0 / 29.9 |
| worst frame, median of windows | 37 ms | 36 ms | 37 ms |
| CPU, mean (100 = one core) | **22** | 26 | **24** |
| CPU time over the run | **12.9 s** | 15.1 s | **14.0 s** |
| physical footprint | 180 MB | 180 MB | 183 MB |
| GPU renderer utilisation | 13% | 6% | 6% |

So ~7-15% of the viewer's CPU time on the street (2.2 and 1.1 s of 15.1),
the two "on" runs bracketing the "off" one. The GPU's own utilisation reads
the same in the last two runs; the first run's 13% came with an idle reading of
14% before it started, so it is the machine, not the pass.

**What this does NOT cover.** A frame with any interface on it still makes the
round trip; the gates were exercised on the street (none) and the shoot phase
(HUD), not on a conversation, a media line or a fade, where only the verify
mode's comparison would show a missing gate.

#### The conversions, done 2026-09-13 - composing on the GPU is still open

**Where the time was.** The adventure frame on Vulkan makes a round trip: the
GPU draws the 3D view, `VulkanRenderer::readback` brings it to the CPU as the
engine's RGB565, the interface is composited over it on the CPU, and
`presentSurface` sends the result back out. The 1320 samples `readback` had
after step 3 were its OWN loop, not the transfer: 888 -> dithered 565 a pixel at
a time, each pixel paying a division and a modulo for its matrix cell
(`i % w_`, `i / w_`) and a call to `quantise888Dither`. `presentSurface`'s
565 -> RGBA8 was the same shape.

**What changed.** `quantise888` works on each channel alone and the dither's
offset is per channel, so for each of the 16 matrix cells the 565 word is three
table lookups ORed together: `quantise888DitherRow` in `ui/surface.*` converts a
whole row that way, and `expand565Rgba` is one 65536-entry table for the
upload. `readback`'s dithered, non-supersampled path and `presentSurface` use
them; the undithered and supersampled paths are unchanged. Composing the
interface on the GPU - which would remove the round trip itself - is NOT done,
and stays the larger half of this step.

**Same bits, proved two ways.** `engine/tools/pixel_tables.cpp` checks every
colour at every cell (2^24 x 16 = 268435456), whole rows at widths 1 / 3 / 640 /
641 / 800 over rows 0..7, and all 65536 565 values: 0 mismatches. And from
outside the tool, 30 DITHERED Vulkan frames of the street through the real
readback - the step-3 build (per pixel) against this one (tables) - are
byte-identical, 960000 bytes. (`run_vulkan` renders with the dither OFF, so its
identical frames said nothing about this path; the dithered comparison is the
one that counts.) `verify.py: engine: pixel tables`. SHOWN TO FAIL: the green
table given the 5-bit channels' offset gives 74252288 colours and 4570 row
pixels wrong; restored, green. `engine: dither`, `engine: sign tie` and
`engine: tie equivalence` pass.

**A LEAD, not chased: `quantise888Dither` is not stable under
auto-vectorisation.** The first version of the tool called it INLINE as the
reference inside the row loop, and reported ~8000 row "mismatches" whose count
changed with the link (8615 against every engine object, 8308 against
`surface.o` alone) and vanished with a `printf` in the loop, under ASan/UBSan
at -O1, or with `-fno-vectorize -fno-slp-vectorize`. Summing each side's output
over the same pixels: the row tables give **542859991** in every build; the
inline function gives **542859991** unvectorised and **578065495** vectorised.
So Apple clang's vectorised evaluation of `quantise888Dither` returns different
values - a compiler fault or undefined behaviour the vectoriser exposes, not
settled. The tool now calls a never-inlined wrapper. It matters beyond the tool
because `raster.cpp`, the software reference, calls the same function in its
per-pixel loops; the Vulkan readback it replaced was evidently not affected
(the real frames match the tables).

**Before and after** (same street, same method; load average 8-9 during these
runs, higher than before):

| | depth tie on hashed sets (`cb00577`) | + pixel tables |
|---|---|---|
| uncapped, typical 1 s window | ~51.5 fps | **~59.5 fps** |
| capped: fps median / slowest window | 30.0 / 29.8 | 30.0 / 29.7 |
| capped: worst frame, median of windows | 35 ms | 36 ms |
| capped: CPU, mean (100 = one core) | 57 | **49** |
| capped: physical footprint | 245 MB | 243 MB |

(Peak RSS read 147 MB in the second run against 273 before while the
footprint did not move - a sampling artefact, not a saving.)

**The re-profile, uncapped, after step 4** (15 s): `main`'s own time 3092,
the moving mesh's soup patch (`main::$_41`) 872, `_xzm_free` 724,
`_platform_memmove` 451, the tie pass's key sorting 403, `buildSoupGrid` 390,
`floorUnder` 312, `quantise888DitherRow` 274, the tie pass's hash lookups
268 + 240, `uploadGeometry` 259, `applyLights` 244. `readback` itself is gone
from the list.

The viewport item's picture reaches the composer through `readback()`. Draw
the interface into the same frame on the GPU (the I2D layer is 16 layers of
7 primitives - quads and blits), or present the 3D view directly and draw the
interface over it. Check: a `--dump` of an adventure frame is byte-identical
before and after; `readback` disappears from the sample.

### 7a. The moving set meshes' patch - done 2026-09-13 (committed as "optimization 8")

**What moves, measured.** `OMK_TRACE_MOTION=1` over 30 frames of the street: 33
set meshes are re-placed every frame, and **30 really move on every one** - the
`Cargo` containers and their `CA0A`..`CA2C` parts riding the cranes; only the
three `Mirador` watchtowers never change. So skipping unchanged patches would
buy little: the cost was the WORK around each patch.

**What each frame did, per resident slot.** Found each moving mesh by name
through all 860 of the set's meshes; re-placed it by scanning **all 139245 render
corners** for its own (33 x 139245 tests - most of `main`'s unexplained self
time in the earlier profiles); scanned the whole walkable and steep soups for its
triangles; then cleared and re-merged `playerSoup` / `playerSteep` (~46000
triangles) and rebuilt the grid.

**What changed** (`play.cpp`, the patch block). Each `WorldSlot` builds, on the
first patch after a load - `loadWorldSlot`'s `w = WorldSlot{}` drops it - a
lower-cased name -> FIRST index map (the replaced scan took the first mesh whose
name matched ignoring case), and each mesh's own render corners and walkable /
steep triangles in ascending order under the scans' exact bounds. A patch walks
only those, with the same `place()`. The merge copies only the triangles that
moved, at their slot's offset, while it is known to match the slots
(`mergedValid`: set by a full merge, cleared by a slot load, with the sizes
checked); anything else takes the full merge as before. The grid is still
rebuilt from scratch when anything moved. `sameName`, used only by the old scan,
is gone.

**Same result, proved.** 20 headless street frames byte-identical before and
after. `OMK_VERIFY_PATCH=1` does the full merge beside the in-place one on every
moving frame: over 60 moving frames, **0 mismatched**, with **754 walkable +
1976 steep** triangles re-placed a frame against ~46000 re-merged.
`verify.py: engine: patch index`; SHOWN TO FAIL: skipping the steep copy
mismatches every moving frame (30 of 30, 60 of 60). `engine: walker falls`,
`crowd push`, `probe grid`, `character shadow` and `tunnel door walk` (a moving
door) pass.

| capped, 45 s | music + queue build (`4cfa444`, last capped window run) | + patch index |
|---|---|---|
| fps median / slowest window | 30.0 / 29.9 | 30.0 / 29.9 |
| worst frame, median of windows | 35 ms | 35 ms |
| CPU, mean (100 = one core) | 50 | **34** |

(The texture and sprite steps changed memory, not CPU, and had no capped window
run of their own; the footprint read 198 MB here against 185 MB then, within
what live window runs vary by - not claimed either way.)

**The uncapped measure has hit a ceiling.** Both this build and the step-4 one
now hold ~60 fps uncapped: that is the display's vsync (the swapchain is FIFO),
not the engine. From here the CAPPED CPU share is the number that shows a CPU
change, which is also the one a slower target like the Vita cares about.

**The re-profile, uncapped** (15 s at the vsync ceiling): the main thread's top is
now `semaphore_timedwait_trap` 3561 - waiting on presentation, i.e. idle - then
`_xzm_free` 725, the depth tie's key sort 417, **`buildSoupGrid` 412** (still
every frame, since the cargo moves every frame), `main` **383** (was 3092),
`sweepSphere` 341, `floorUnder` 332, `uploadGeometry` 312 (the whole set's vertex
buffer re-uploaded every frame for the same reason), `applyLights` 295, the depth
tie's hash lookups 350 + 273, `quantise888DitherRow` 272. The obvious next ones
all trace back to the moving cargo: a grid updated for the moved triangles
alone, a vertex buffer updated for the moved corners alone, and a depth tie that
does not re-key the whole set when only a mesh moved.

### 7b. The depth tie stored flat - done 2026-09-13 (committed as "optimization 9")

**Why it was back.** Step 3 made the tie pass cheap per face, but a set's
REVISION changes on every frame its cargo moves (7a's patch bumps it), so the
whole city - 46415 triangles - is re-keyed every frame, and the hashed sets
allocated a node per claimed face and freed them all at the next reset. The 7a
re-profile: hash lookups 350 + 273, key sorting 417, and most of `_xzm_free`'s
725.

**What changed** (`o3de/depthtie.*`). The same decisions in the same order; only
the claimed keys' storage. Each claimed face's positions are copied into a flat
array reused across revisions (so the key is the positions themselves, as the
sorted array was, and nothing depends on the geometry not changing under it),
found through an open-addressed table keyed by an ORDER-FREE hash of the
positions (a sum and an xor of per-position mixes), and reset by bumping a
generation instead of freeing. The exact multiset compare - sort both, compare -
runs only on a hash match.

**The first flat version needed a fix, measured.** It ran the exact compare on
EVERY occupied cell a probe walked past, and linear probing clusters, so the key
sort stayed in the profile (331) and a capped run read no better. Each cell now
carries a 16-bit fingerprint of the hash beside a 16-bit generation and the key
number, and a probe skips a cell whose fingerprint differs; only a fingerprint
match goes on to the compare, which alone decides.

**Same decisions, proved.** `tie_equiv` keeps the ORIGINAL ordered-set pass: 0
mismatches over Anekbah, Aapkayl, Lahoreh, HO1_FN, PSH_FN, JEN_FNM (3056 draws).
The tool's timing on Anekbah: original **119.8 ms**, step-3 hashed sets 40.0,
first flat version 31.7, flat with fingerprint **21.0** - 5.7x the original.
`engine: tie equivalence` (asserted totals unchanged), `engine: sign tie` and
`mirror pass` pass. SHOWN TO FAIL, on the new code: a compare that always
matches gives 367 / 426 / 76 mismatching draws; an inverted fingerprint test
(skipping the matching key) gives 197 / 280 / 69; restored, green.

**Measured back to back - the only fair way.** Capped CPU readings moved by
several points between sessions for the SAME binary (the patch build read 34%
one run and 39% the next, with other applications on the GPU), and the uncapped
rate that sat at exactly 60 in two earlier sessions read ~105-111 in this one -
so the display's pacing ceiling is not fixed either. So the two builds were run
capped one after the other under the same load (~5):

| capped, 45 s, back to back | patch index (`74f893a`) | + flat depth tie |
|---|---|---|
| fps median / slowest window | 30.0 / 29.6 | 30.0 / 29.8 |
| worst frame, median of windows | 36 ms | 35 ms |
| CPU, mean (100 = one core) | 39 | **36** |
| CPU time over the run | 21.6 s | **20.0 s** (-7.6%) |
| physical footprint | 174 MB | 170 MB |

**The re-profile, uncapped:** `DepthTie::resolve` 713 (its own walk - the sort and
the frees are gone from the list), `buildSoupGrid` 537, `main` 430, `floorUnder`
362, `quantise888DitherRow` 321, `uploadGeometry` 297, `applyLights` 295,
`sweepSphere` 263, `applyPose` 208. What is left of the tie's cost is walking
46415 faces a frame because the revision says the whole set changed - removing
THAT needs a revision that says WHICH corners moved, the same thing the grid
rebuild (537) and the whole-set vertex upload (297) are waiting on. That is the
next step, and it is one change serving three consumers.

### 7c. The ground probe grid in two layers - done 2026-09-14 (committed as "optimization 10")

**Why.** On the Anekbah street 754 walkable triangles move every frame (the
cargo on the cranes), and `playerGrid` - step 2's grid - was rebuilt from
scratch for them every frame: `buildSoupGrid` 537 in the 7b re-profile.

**Tried first and measured useless: keep the grid when no moved triangle
crossed a cell.** 0 of 120 frames kept it - with that many moving triangles one
crosses on nearly every frame. Reverted.

**What changed** (`o3de/collision.*`, `play.cpp`). `SplitSoupGrid`: a `fixed`
layer over the triangles that have not moved since the sets changed, rebuilt
only when that set grows, and a `moving` layer over the rest, rebuilt every
frame from an ascending id list. Each triangle is in exactly one layer, and
`floorUnder` / `surfaceUnder` walk the two cells' lists as one ascending merge -
the same candidates in the same order as a single grid, so the first-hit rule
and the answer are unchanged. `soupInBox` gathers both layers' ids, sorts them
and runs the same linear test.

**Same answers, proved.** `probe_grid`'s split rows: every probe and box of
the single-grid test through a split by a 1-in-16 mask and its complement,
against the linear scan - 0 mismatches in 14 rows over seven sets (Anekbah
walkable 946 / steep 1945 moving, AImpasse 7 / 25, Sprison 111 / 173 in the
three the check asserts). In play, `OMK_VERIFY_SPLIT=1` on the street: 0 of
22680 probes over 120 moving frames. Headless frames byte-identical.
`engine: split grid`, `engine: probe grid`, `engine: patch index` pass.
SHOWN TO FAIL, on the committed code: a merge that skips the moving layer
(`else ++pb`) gives 2637 - 29451 mismatches per tool row and 6526 / 13006 in
game; `play.cpp` not adding a newly moving triangle to the id list leaves the
tool rows at 0 and gives 6526 / 13006 in game - each half fails on its own
side. Restored by editing back, green.

**Measured - and the capped A/B could not see it.** Old / new / old at 30 fps,
load ~3: CPU 27 / 36 / 35%, CPU time 15.8 / 19.9 / 19.7 s. The two runs of
the SAME old build disagree by 8 points, more than this step can move, so the
comparison says nothing either way. Timed directly instead (a scratch
microbenchmark over Anekbah's walkable soup, 15137 triangles with 754 moving,
400 builds each):

| | per build / probe |
|---|---|
| the whole grid, what every moving frame did | 0.528 ms |
| the moving layer from the id list, what it does now | **0.016 ms** |
| the moving layer from a mask (the first version) | 0.056 ms |
| `floorUnder`, one grid vs two layers | 0.3 µs both |

So ~0.51 ms a moving frame, ~1.5% of a 33 ms frame here - below a capped
run's noise, but real, and several times larger on a Vita-class CPU.
`buildSoupGrid` is gone from the profile.

### 8. A revision that says which corners moved - 2026-09-14

**Why.** 7b's re-profile left two costs that exist only because a set's
revision says "everything changed" when its cargo moves: `DepthTie::resolve`
walking all 46415 of Anekbah's triangles (667 in the 7c profile) and
`uploadGeometry` re-writing all 139245 corners (284). 7c had already taken the
third consumer, the ground grid, off the per-frame path.

**The boundary** (`o3de/geom3do.h`). `Geometry` gains `dirtyFrom`, `dirtyTo`
and `dirtyCorners`: while `revision == dirtyTo`, only the listed corners may
differ from revision `dirtyFrom`. It is only ever a shortcut - a writer that
bumps `revision` without it leaves `dirtyTo` stale, which reads as "everything
changed" - and the set motion patch in `play.cpp` is its one writer: the
corners of the meshes it re-placed that frame. `OMK_NO_DIRTY=1` leaves it
unset.

**The vertex upload** (`vkrender.cpp`). When the buffer holds `dirtyFrom`, only
the listed corners are written. What that leaves behind is the depth tie's
degenerated triangles, which a full upload used to wipe; so `DepthTie` now keeps
which triangles it has told the backend to degenerate, names in `restore` those
that are no longer losers (written back before the draw's losers are
degenerated), and is told by `vboReplaced()` when a full upload put everything
back.

**The depth tie REPLAYS its walk** (`o3de/depthtie.*`). For a geometry that
carries the list, the walk is logged: every face visited (a triangle or a
quad) in order, with its draw, and the faces sharing each key chained in walk
order. A face's decision depends only on whether an EARLIER face of its chain
writes depth, so the next revision takes the moved faces out of their chains
(by identity, no positions compared while other moved faces still sit under
their old keys), puts them into the chains of their new keys, re-decides only
those chains, and updates each draw's loser list from the net changes. Then
each draw that arrives as logged gets its losers read back. It is abandoned
for a full walk when it cannot be the same answer:

* the first draw that differs from the log (start, count, writes) - a mesh
  crossing the clip distance - throws the replay away; the draws already
  answered are walked afresh (their losers are what the replay gave) and what
  is degenerated but no longer a loser of them is restored;
* a moved face that would now pair into a quad differently, or a single before
  it that could pair with it - the pairing is positional - makes the revision
  a full walk before anything changes;
* the previous revision's log must be complete and `dirtyFrom` its revision.

Geometries without the list (posed bodies, the sky, effects) keep 7b's flat
walk unchanged.

**Same answers, proved.** `tie_equiv` gained 60 fixed-sequence frames - three
meshes moving every frame, ties snapped onto and from a moving face, an
invalid list, a missing draw, a doubled pass, a frame with nothing moving -
and a SIMULATED VERTEX BUFFER driven the backend's way (partial upload,
restores, degenerations) compared every frame, on every drawn triangle, with a
full upload plus the reference's losers. Over Anekbah, Lahoreh, PSH_FN,
Aapkayl, HO1_FN and JEN_FNM: 0 mismatching draws, 0 wrong buffer triangles in
101 frames each, and every path taken (Anekbah: 64 revisions replayed, 31
walked, 29 replays abandoned). The earlier columns did not move (Anekbah 792
draws, 4646 losers). In the game, the street start through `--world-vulkan`,
90 frames: 88 revisions replayed, 1 walked (the first moving frame has no log),
0 abandoned, and frames 65 and 90 byte-identical to `OMK_NO_DIRTY=1`.
`engine: tie equivalence` (extended), `engine: dirty corners` (new),
`engine: sign tie`, `mirror pass` and `engine: split grid` pass.

SHOWN TO FAIL, each on the committed code and restored by editing back:

* the replay never re-deciding the chains the moved faces joined: 9 / 10 / 24
  mismatching draws in Anekbah / Lahoreh / PSH_FN and 9 / 9 / 26 wrong buffer
  triangles, every older column unchanged;
* a face that stops losing not RESTORED: every loser list still right (0
  mismatches) and 17 / 5 / 155 wrong buffer triangles - only the simulated
  buffer sees it, which is why it is there;
* the motion patch listing no corners (`play.cpp`): the revisions still
  replay (88 / 1 / 0) and the frame differs from `OMK_NO_DIRTY=1` in 334
  pixels - the cargo frozen in the GPU buffer.

**Measured back to back, capped, old / new / old** (load ~3.7-4.8, and this
time the two old runs agree):

| capped, 45 s | 7c (`210037a`) | + dirty corners | 7c again |
|---|---|---|---|
| fps median / slowest window | 30.0 / 29.6 | 30.0 / 29.9 | 30.0 / 29.9 |
| worst frame, median of windows | 37 ms | 36 ms | 35 ms |
| CPU, mean (100 = one core) | 28 | **25** | 28 |
| CPU time over the run | 16.7 s | **14.6 s** | 16.2 s |
| GPU device utilisation | 28% | 20% | 20% |
| physical footprint | 179 MB | 175 MB | 199 MB |

So ~11-13% of the viewer's CPU time on the street. The footprint moves by
more between the two old runs than between old and new; the log costs about
3 MB for Anekbah, counted from the structures (24 bytes a face and 8 of hash,
a 131072-cell table of 8, and a few per-triangle arrays).

**Walking, measured afterwards** (2026-09-14, uncapped, holding forward for
1500 frames): 185 of the 1500 revisions abandoned a replay part way - a mesh
crossing the clip distance - so the replay holds for ~88% of a walk. And the
`OMK_TIE_STATS` per-geometry counter added then says where the remaining full
walks go: 45 posed bodies of 386-542 triangles, ~17300 faces a frame between
them; the set itself is replayed.

**What this leaves.** The step replays only while the draw sequence repeats;
walking the street changes the visible set whenever a mesh crosses the clip
distance, and each such frame is a full walk plus the replayed prefix again.
Not measured walking yet.

### 9. The walker and the decor probe through the grid - 2026-09-14

**Found by the step-8 re-profile.** Standing on the street (uncapped, 20 s),
the single largest CPU cost was `floorUnder` - the LINEAR scan, 458 samples:
`decorUnder` 183, `PlayerController::tick` 139, `Walker::step` 136. Step 2 had
built `playerGrid` over exactly that soup, but only `play.cpp`'s shadow and
body probes ever used it; `Walker::ground` (every step, every controller tick)
and `decorUnder` (event 9's "which decor is under his feet", every frame over
both shown sets) still scanned every walkable triangle - 15137 in Anekbah's
soup (46415 is its RENDER triangle count, a different number).

**What changed.**

* `Walker::setGrid` (actor/walk.h): `ground` walks the two-layer grid. The grid
  overload already scans when the grid does not match the soup, so a stale
  pointer costs time, not correctness - and in the viewer every write to
  `playerSoup` (`rebuildWorld`, the motion patch) rebuilds the grid in the same
  block, with no probe in between. `PlayerController::setGroundGrid` forwards.
* `decorUnder(decors, merged, grid, ...)`: the viewer's `playerSoup` IS the
  shown decors' soups concatenated in order (`rebuildWorld` builds both lists in
  one loop), so the nearest floor over the merged soup, through the grid, names
  the decor by the offset its triangle falls at. A new `floorUnder` overload
  returns that triangle: the first in soup order to reach the answer, which is
  what the linear scan's strict `<` keeps - so a floor shared by two decors
  goes to the EARLIER one, exactly as the per-decor loop keeps the first
  strictly nearer. It falls back to the loop unless every decor has an area and
  the sizes add up.
* `OMK_NO_GROUND_GRID=1` keeps the scans; `OMK_VERIFY_GROUND=1` computes every
  grid answer the linear way too and prints the counts every 30 frames.

**Same answers, proved.** `probe_grid --decor` merges two walkable soups with
a 1-in-16 split and asks both `decorUnder`s over every sampled triangle's centre,
vertices and edge midpoints and 4000 random points: Anekbah + AImpasse 25343
probes (21785 answered Anekbah, 153 AImpasse), 0 mismatches; Anekbah twice -
every floor a tie - 21785 to the first decor, 0 to the second, 0 mismatches;
a decor with no area, the fallback, 0. In the game, the street start walking
forward 150 frames through `--world-vulkan`: by frame 120, 233 walker probes
and 121 decor probes, 0 differing from the scan; the last frame byte-identical
to the same walk with `OMK_NO_GROUND_GRID=1` (and 96% of its pixels differ from
a standing run's, so the walk walked). `engine: ground grid` (new), and
`engine: walk`, `walker falls`, `player vertical`, `airlock walk`,
`probe grid`, `split grid` pass.

SHOWN TO FAIL, on the committed code: the walker's grid probe with x and z
swapped gives 237 of 237 probes mismatched and 460091 pixels different. And
worth recording: moving its window by one unit did NOT go red - a flat street
has no surface inside that unit, so it is a mutation too weak to see, not a
blind check. The decor half, each restored by editing back:

* the merged probe keeping the LAST triangle of a tie (`<=`): 21785
  mismatches in the Anekbah-twice row and 0 in the other two - only the tie
  case can see the tie rule, which is why it is there;
* the merged answer always naming the first decor: 153 mismatches in the
  Anekbah + AImpasse row, exactly its AImpasse answers, and 0 in the others.

**Measured - and the capped A/B did NOT measure it.** Old / new / old at 30 fps:
CPU 23 / 31 / 23%, CPU time 13.5 / 17.4 / 13.6 s. The two old runs agree, but
the load average was 5.6 when the new run started against 3.5-3.8 for the old
ones (another application at ~60% of a core, Finder at 40%), and the change
cannot cost that: timed directly, the linear `floorUnder` over Anekbah's walkable
soup is 3002 ms for 18098 probes (0.17 ms each, itself measured under load
~8) against 5.4 ms through the grid, and the walk makes ~3 such probes a
frame (233 walker probes and 121 decor probes in 120 frames) - ~0.5 ms a
frame, ~1.5% of a core at 30 Hz, matching the profile's 458 samples in 20 s
at 60 Hz. A same-binary A/B (`OMK_NO_GROUND_GRID=1` on and off) on a quiet
machine is owed.

### 10. The posed bodies: `applyPose` in place - 2026-09-14

**Why.** The standing street's profile after steps 8/9 put the posed bodies
together at the top: the depth tie re-walking all 45 of them (~600 samples),
`applyPose` (189 of its own) and the `Geometry` copies it began with (~260 in
`Geometry::operator=` and its memmoves), their vertex uploads (~150). This
step is the copy.

**What changed** (`actor/pose.cpp`). `applyPose` began with `g = rest` and then
overwrote every corner's position and normal. When the posed geometry already
has the rest's corner count it now assigns the per-corner metadata vectors
(which keep their memory) and refreshes each corner inside the pose loop:
`u`, `v`, `r`, `g`, `b` and `phase` copied from the rest corner, the position
and normal computed as before, an unposed corner copied whole. A different
count - the first call, a crowd model's LOD switch - keeps `g = rest`.
Nothing may be assumed to survive from the previous frame: the viewer rewrites
the posed corners' positions (placement) and colours (the crowd's lights)
after every pose, so every field is refreshed every call.

**Same geometry, proved.** `engine/tools/pose_equiv.cpp` keeps the copying
version verbatim and drives both over 240 calls a model - random poses, the
face morph off / on / wrong-sized, the geometry edited between calls in every
field the viewer's passes touch and some they do not, a switch to a cut rest
and back, a body posed from itself - comparing every field after every call:
0 mismatches on HO1_FN, PSH_FN, JEN_FNM, DOC_FNM, V5H_FNM (three with a face
mesh). It prints `sizeof(Geometry)` (192), which the check pins: the in-place
path copies the fields it names, so a field added later must be added there
too, and the pin makes forgetting it a failure rather than a silent
difference. In play, the street start through `--world-vulkan`, standing and
walking, crowd on: frame 90 byte-identical to the step-9 build.
`engine: pose equivalence` (new); `engine: pose`, `pose blend`, `city crowd`,
`pedestrians`, `street frame`, `mapped shadows`, `crowd push`, `head look`,
`crowd nan`, `player vertical`, `player jump` pass.

**Timed directly** (the tool, 2000 poses of the same model): HO1_FN 62.1 ->
18.8 ms, PSH_FN 62.9 -> 24.8, JEN_FNM 62.8 -> 24.9, DOC_FNM 54.9 -> 20.6,
V5H_FNM 57.8 -> 21.0 - about 30 -> 10 microseconds a body pose. A capped A/B
in play is owed (load 4.9 when this was committed).

**SHOWN TO FAIL**, each restored by editing back: the in-place loop not
refreshing `phase`, and the in-place path not assigning `cornerVertex` -
each gives **229 of 241** calls differing on all three checked models. Only the
edits between calls can expose either: a posed geometry nobody wrote to since
the last call still holds the rest's `phase` and `cornerVertex`, so a test that
only posed would have passed both.

### 10b. The bodies' depth tie - looked at, NOT changed (2026-09-14)

The step-10 re-profile still put the posed bodies' flat tie walk first (~490
samples standing). Two routes were weighed and neither taken:

* **An exact one, measured too small.** The flat walk's table is sized
  1.25x the triangles, so it runs to 80% full; a scratch build with the table
  2.5x and 5x (a bigger table changes probing only, never a decision): per face
  PSH_FN 28.3 -> 24.0 -> 21.8 ns, HO1_FN 24.0 -> 21.3 -> 19.8, and the
  Anekbah set 18.6 -> 22.3 -> 22.3 (slower). For 45 bodies that is ~0.08 ms a
  frame, and the set gets worse. Left as it is.
* **A large one, not provable.** Reusing a body's walk across poses rests on
  "which faces coincide does not change", and it can: faces of different bones
  can meet or part, and the float rounding of placing a body at street
  coordinates can make two different positions equal to the bit.

### 11. The player's sweeps through grids - 2026-09-14

**Why.** The step-10 profile: `sweepSphere` 256 samples standing (the body
sweep 150, the camera 106), the top entry of the walking profile - every call a
linear scan of the steep soup (31141 triangles in Anekbah), the camera's
second ray of the walkable one too.

**What changed.** `o3de/collision.*`: the per-triangle sweep test moved into
one function the linear scan and a new grid overload both call, and the
two-layer grid's box gathering into one function `soupInBox` and the sweep
both call - so the grid overload is the linear scan restricted to the
triangles whose horizontal box meets the swept box, visited in the same
ascending order. It scans while the grid does not match the soup or a
coordinate is NaN. `play.cpp`: `playerSteepGrid` over `playerSteep`, fixed and
moving layers kept exactly like `playerGrid` (rebuilt in `rebuildWorld`, the
moving steep triangles marked in both merges, the moving layer rebuilt every
moving frame). `Walker::setBlockerGrid` and `PlayerController::setCameraGrids`
take them; `OMK_NO_SWEEP_GRID=1` keeps the scans; `OMK_VERIFY_GROUND=1` now
also compares every body and camera sweep with the scan.

**Same answers, proved.** `probe_grid` sweep rows - random and aimed segments,
radius 0 / 12 / 40, both mask splits, walkable and steep: 0 mismatches over
17095 sweeps on Anekbah / AImpasse / Sprison, 5193 of them hits. In play,
walking forward 150 frames: 692 body and camera sweeps by frame 120, 0
mismatched; frame 90 byte-identical to the step-10 build standing and walking.
`engine: sweep grid` (new); `probe grid`, `split grid`, `ground grid`,
`engine: walk`, `walker falls`, `player walk`, `airlock walk`, `player
vertical`, `player jump`, `crowd push` pass.

**Timed directly** (`probe_grid`, the same sweeps, scan against one split):

| set, soup | sweeps | scan | grid |
|---|---|---|---|
| Anekbah walkable | 3014 | 99.7 ms | **1.94 ms** (51x) |
| Anekbah steep | 3058 | 195.3 ms | **4.37 ms** (45x) |
| AImpasse walkable / steep | 1605 / 1888 | 0.6 / 2.6 ms | 0.49 / 2.28 ms |
| Sprison walkable / steep | 3288 / 4242 | 18.3 / 34.7 ms | 8.08 / 21.57 ms |

On the street a sweep goes from ~64 to ~1.4 microseconds; a small set gains
little, as the grid's cells hold most of it. A capped A/B in play is owed.

**SHOWN TO FAIL**: the grid overload gathering on the swept box's Y extent
instead of its Z - 2474 / 1490 / 360 / 558 / 2360 / ... mismatching sweeps in
the six rows; restored by editing back, green.

### 13. `findMeshContaining` - the cause in the plan was WRONG (2026-09-17)

`todo/handoff-vita.md` §2 item 1 asks for "`findMeshContaining` searching by
NAME every frame (0.09 ms): an index per model, built once". The index is
right; the *reason* the file gives for the cost is not, and the first attempt
was aimed at the wrong half.

**What the function does.** A shadow bone is resolved by NAME, on the LAST
match, scoped to one LOD skeleton by an ancestry walk - and the walk turned
each parent ID into an index by scanning the whole mesh array, so one call was
O(matches x depth x meshes). That quadratic walk is the obvious target and it
is **not the cost**.

**Attempt 1, exact and REFUTED by measurement.** The walk moved onto a sorted
id -> first-index map built once per call (kept in
`scratchpad/shadow.cpp.idindex-rejected`). Exactly equivalent - 0 mismatches
over 5490 calls including duplicate ids, an absent parent, a self-parent, a
two-mesh cycle and a 70-deep chain past the 64-step guard - and **slower**,
three runs each, per call:

| | scan (old) | sorted map |
|---|---|---|
| PSH_FN, 76 meshes | 852-930 ns | **1145-1178** |
| HO1_FN, 19 meshes | 102 ns | **204-213** |

Bone chains are shallow, so `O(N log N)` a call never earns itself back.
Reverted the same hour.

**What the cost actually is.** The NAME SCAN: every call reads every mesh name
in the model, and `std::string_view(m.name)` is a `strlen` with `.find` a
`memchr`/`memcmp` - which is exactly what the Vita profile already said
(`handoff-vita.md` §1 lists `memchr`/`strlen`/`memcmp` from
`findMeshContaining`, not self time). Reading the profile properly would have
skipped attempt 1. Only remembering the answer removes it.

**Attempt 2, kept.** `omk::MeshNameIndex` (`o3de/shadow.{h,cpp}`), built once
per model for `MeshNameIndex::shadowNames()` - the ten `kShadowBones` plus the
crowd's two feet. It resolves every parent id to an index once and keeps, per
name, the matching indices ascending; `find` walks one short list BACKWARDS, so
the first hit under the root is the last match the full scan kept. Per call,
quiet machine: PSH_FN 924 -> **31 ns**, FSH_FN 871 -> 32, HO1_FN 106 -> 16,
JEN_FNM 125 -> 16.

`engine/tools/meshidx_equiv.cpp` drives the index against the scan transcribed
verbatim, over the four real models and eight synthetic arrays each aimed at
one rule, crossed with every root (-1, -2, each mesh index, two off the end)
and 15 names including the EMPTY string, which matches every mesh, and one
longer than the 21-byte field: **0 mismatches in 5130 calls, 0 missing**.
`verify.py: engine: mesh name index`. SHOWN TO FAIL four ways, each restored to
0: the match list walked forwards 201, the parent lookup keeping the last
duplicate id 201, the guard at 65 **8** (only the 70-deep chain sees it), and
`root < -1` for the unscoped case **8**.

**IT HAS NO CONSUMER YET, and that is the honest state.** `play.cpp` - which
holds all three call sites and owns the mesh arrays - belonged to another
session in the same working tree, so the index ships unused and the frame is
not faster by a nanosecond. The adoption is written out edit by edit in
[`pending/vita-meshidx-playcpp.md`](pending/vita-meshidx-playcpp.md), including
the trap (a stale index after `player.become`) and how to show it failing.
**Nothing here may be quoted as a frame saving**: the numbers are per call from
a tool, and no capped A/B has been run.

### 14. WHERE the depth tie's memory is - measured, not yet cut (2026-09-17)

`handoff-vita.md` §2 item 1's third bullet asks for "the per-frame allocations
the tie's tables make (~10 MB across bodies)" to go. Before cutting anything:
**which** of `DepthTie`'s twenty-odd vectors holds it? A struct layout cannot
say, because every one is sized from the geometry at run time, so
`DepthTie::bytes()` now reports `capacity` by group and
`engine/tools/tie_mem.cpp` drives a real pass to read it.

| geometry | tris | drops | claimed keys | per-triangle | log/table/scratch | total |
|---|---|---|---|---|---|---|
| Anekbah (set) | 46415 | 248 | **2560.0 KB** | 91.7 | 0 | 2651.7 KB |
| PSH_FN (a body) | 790 | 3 | **67.0 KB** | 1.6 | 0 | 68.6 KB |
| HO1_FN (a body) | 542 | 0 | 41.5 | 1.1 | 0 | 42.6 KB |

**The claimed keys are 97% of it, and everything else is a rounding error.** A
key is the face's corner POSITIONS copied out - 9 words a triangle, 36 bytes a
face - so `keys` alone is 1.67 MB of Anekbah's 2.65.

**The pass is the backend's, confirmed three ways.** Anekbah drops **248**, the
figure `engine: sign tie` pins in the Vulkan render and `tie equivalence` in its
own; PSH_FN **3** and HO1_FN **0**, which is `handoff-vita.md` §2's own
measurement. A probe driving the class wrongly would miss all three - and the
first version of this one did, by reading `DepthTie::dropped`, which is the
BACKEND's counter that the class only ever zeroes (`vkrender.cpp`:
`t.dropped += losers.size()`). It read 0 for everything.

**Two limits, declared.** `log`, `table` and `scratch` read 0 because step 8's
tracked replay never engages without a valid dirty-corner revision, so the
set's real residency in a frame is HIGHER than the row above; and the ~9.7 MB in
the handoff is a whole street's heap - two resident sets (hidden is not
unloaded), the player, four LOD body sizes, the shadow, effect and sky
geometries - which this does not attempt to reproduce. One geometry at a time,
exactly, and `tie_mem`'s last row multiplies by a body count as arithmetic.

**THE CUT THIS POINTS AT, not taken yet.** `keys` copies the positions so the
exact multiset compare has something to read on a fingerprint match. It could
instead store the face's first CORNER INDEX - 4 bytes, not 36 - and read the
positions back out of `g.corners` at compare time. Within one revision that is
the same bytes by construction, because a revision is precisely "the corners
changed" and `claimed` is reset at every one; the care needed is that the
tracked replay's `table_`/`units_` persist ACROSS revisions and must not be
folded into the same argument. Worth ~1.5 MB on the set and ~60% of the tie's
total, and `tie_equiv` is already the oracle for it. Not attempted here.

`verify.py: engine: tie memory`, which asserts the three drop counts and the
share `claimed` takes, and deliberately NOT the byte totals - `capacity`
follows the allocator's growth policy, so a total asserted would be a claim
about libc++ on an M1.

### 12. The crowd lighting - looked at, NOT changed (2026-09-14)

`applyLights` was 316 samples of the step-10 standing profile. Measured first:
at frame 300 of the street start 17 bodies are drawn and the lights reach them
86 times - about 5 lights a body out of Anekbah's 155 - so the reach test
(155 x 17 distance checks) is nothing and the cost is the per-corner loop, run
once per reaching light over a body's ~2400 corners.

The exact candidate was that loop's three `lightRamp(c, t) / 255.0f` a lit
corner - an int-to-float and a division each - read instead from a 256 x 256
float table built once from the same expression. `light_equiv` (kept in the
session's scratch, not committed) held it to the old loop verbatim: **0
mismatches** over 600 calls each for PSH_FN and JEN_FNM under Anekbah's lights,
with random base colours and synthetic lights at colour bytes 0 / 255 and
`t` past 255. And **no gain**: 35.9 -> 39.0 ms and 19.9 -> 20.5 ms. The
divisions were not what the loop costs. Reverted; nothing committed but this
note.

### Steps 9, 10 and 11 measured together, capped - 2026-09-14

Owed since each was timed only in its own tool. Four capped 45 s runs of the
street start, standing, one after the other on a quiet machine (load 1.4-3.1):
the build at `ed81d9b` (steps 9-11), the step-4b build `004d28f` (none of them;
the jump fix between them only acts on a jump, and this run does not jump), the
new build again, and the new build with `OMK_NO_GROUND_GRID=1` and
`OMK_NO_SWEEP_GRID=1` (steps 9 and 11 off, step 10 still on):

| capped, 45 s, standing | new | old (`004d28f`) | new again | new, grids off |
|---|---|---|---|---|
| fps median / slowest window | 30.0 / 29.8 | 30.0 / 29.8 | 30.0 / 29.8 | 30.0 / 29.9 |
| worst frame, median of windows | 36 ms | 37 ms | 37 ms | 36 ms |
| CPU, mean (100 = one core) | 32 | 33 | 32 | 34 |
| CPU time over the run | **18.07 s** | 18.57 s | **18.41 s** | 18.94 s |
| physical footprint | 206 MB | 206 MB | 206 MB | 205 MB |

**Small, at the edge of the noise, and that is the finding.** The new build
used 0.2-0.5 s less CPU than the old one (1-3%), while its own two runs differ
by 0.34 s. Standing is the case these steps help least: the player barely
probes or sweeps when he does not move (steps 9 and 11 were about the walker
and its camera), and step 10 saves ~20 microseconds a body over ~17 drawn
bodies. The tool timings stand - 45-51x per sweep, 3x per pose - but at 30 Hz
standing they are about a percent of the frame. A WALKING capped run is where
steps 9 and 11 would show, and was not taken.

Note the absolute level: every run here reads ~32% where the same `004d28f`
build read 22-28% this morning - the machine's own state, not the code - which
is why only back-to-back columns are compared.

### 5. `main`'s own time

`main` is 16k lines; a sample's self time there is every inlined helper. Build
once with `-fno-inline` (or mark the suspects `noinline`) for a profiling run
only, re-rank, and add rows to the table above.

### 6. Memory

#### First cut, done 2026-09-13 (committed as "optimization 5")

**Where it goes, measured, not guessed.** A headless street run (the same
Anekbah start, `--software`, which leaves the CPU-side memory the same as
Vulkan: 234 MB headless against a 243 MB window footprint) under
`MallocStackLogging=1`, snapshotted after 45 s with `heap -s` (live allocations
grouped by the call that made them), `footprint` and `vmmap --summary`. 233.7 MB
live in 12786 allocations; the top of it:

| live | allocations | made in | what |
|---|---|---|---|
| **64.4 MB** | 1 | `MusicPlayer::play` | the whole track resampled to 44100 Hz interleaved floats |
| **45.2 MB** | 1 | `SdlFrontend::queueAudio` | the device stream, with NO device - see below |
| 18.2 MB | 14 | `std::vector<Corner>` inserts | render geometry, its base copies for moved meshes, posed bodies |
| 14.1 MB | 7 | `SdlFrontend::playSound` | each playing sound copied whole into its own float buffer |
| 11.6 MB | 6 | `collisionSoup` | the collision soups and their copies |
| 10.6 MB | 421 | `main` | assorted |
| ~27 MB | ~100 | `omk::textures`, `std::vector<Texture>` copies | the texture list, held several times |
| 7.3 MB | 2 | `ScxRuntime` | the two resident scene files, kept whole |
| 5.5 MB | 1 | `std::vector<SpriteFrames>` | the sprite library |

Audio was **124 MB of 234**.

**The audio queue with no device.** The world opens the audio device only when
the run is not bounded by `--frames` (`play.cpp`, `if (!frames)` before
`openAudio`) - so every check, every headless render and every uncapped
benchmark in this file ran with none. With no device the callback that
consumes the stream never runs and `queuedSeconds` answers 0 for ever, so the
music's "keep about a second queued" pulled ANOTHER second every frame into a
vector nothing drained. `queueAudio` now drops the block while no device is
open and says so once (`audio: no device - the stream is dropped, not
queued`); with a device open nothing changes. It also means the uncapped
`--frames` numbers earlier in this file carried that growth in their RSS.

**The music at its own rate.** The decoder's output is 16-bit stereo at
22050 Hz; the player resampled all of it to the device's 44100 as floats when
a track started - four times the size. It now keeps the decoder's samples and
resamples in `pull` with the same nearest index (`size_t(i * step)`), the same
scaling, the same loop and end rules, and the same `playing()` / `seconds()`.
The longest tracks (79 and 80, 344 s) were **118.6 MB** each and are **29.6 MB**;
the street's track 2 went 62.9 -> 15.7 MB.

**Same samples, proved.** `engine/tools/music_equiv.cpp` keeps the old player
verbatim and plays each track through both, looped and not, in irregular pulls
(1, 3, 777, 44100, 100000, 132300 frames) through two wraps or to the end:
tracks 2, 15, 29, 79, 80, 86 - **0 mismatches over 425 million samples**.
`verify.py: engine: music storage` (tracks 2, 15, 29); SHOWN TO FAIL: wrapping
one frame early gives 251 / 47 / 183 mismatching pulls. `verify.py: engine:
audio queue bound` runs 60 headless frames and reads that process's own peak
from `os.wait4`: the drop announced once, peak under 180 MB. SHOWN TO FAIL -
and honestly: disabling the guard is caught by the ANNOUNCEMENT (0 for 1), not
by the bound (that run peaked at 142 MB); the bound catches the music going
back to floats. `engine: audio`, `voice over`, `stop sound`, `scene sounds`,
`actor sounds` and `movies` all pass.

**Before and after:**

| | before (`bc41c1d`) | after |
|---|---|---|
| headless, 60 street frames: peak RSS / peak footprint | 215 / 203 MB | **149 / 141 MB** |
| live heap, snapshot at 45 s | 233.7 MB | **140.2 MB** |
| capped window, 45 s: physical footprint | 243 MB | **185 MB** |
| capped window: fps / slowest window / CPU | 30.0 / 29.7 / 49% | 30.0 / 29.9 / 50% |

**What is left, from the new snapshot** (140.2 MB live): render corners 18.2 MB,
the music 16.1 MB, the copied sound buffers 14.1 MB, the collision soups
11.6 MB, `main` 10.6 MB, the texture-list copies ~27 MB together, the scene
files 7.3 MB, geometry copies 5.9 MB, the sprite library 5.5 MB. The obvious
next ones: the **texture list held several times** (one copy, shared), the
**sound buffers copied per play** (play from the bank's own buffer), and the
**music kept as ADPCM** and decoded as it plays (another ~4x: 16 -> ~4 MB).
Against the 32 MB the original shipped for, the shipped DATA is not what
forces any of this.

#### Second cut, done 2026-09-13 (committed as "optimization 6") - texture pixels shared

**What was copied where.** `Texture::rgb` was a `std::vector<std::uint8_t>`, so
copying a `Texture` copied its pixels, and the viewer copies textures between
lists as a matter of course: each decor set keeps its own (`w.tex`),
`rebuildWorld` concatenates both sets into `worldTex`, and every composition
change rebuilds the renderer's `pool` as `pool = worldTex` plus each character
model's, each prop's, the sky's, the shadow's, the player's and the sprites'.
The world's textures were in memory three times. The software rasterizer keeps
only a `span` of the pool and Vulkan re-uploads from it at each rebuild, so the
pool's pixels must stay alive - the copies had to become cheap, not go.

**What changed.** `Texture::rgb` is a `PixelBuffer` (`formats/tex3dt.h`): one
reference-counted buffer shared by copies, with every read the vector served
(`data`, `size`, `empty`, `operator[]`, iteration). Writing is EXPLICIT -
`mutableData()` detaches from any other copy first - so a copy still behaves as
an independent value. The decoder takes its write pointer once; `run_anekbah`
(the only other texel writer) paints through one; two probes' `= {255, 255,
255}` get fresh storage.

**The trap the test caught.** The first version also had a non-const
`operator[]` that detached. C++ chooses it for EVERY `[]` on a non-const
Texture, reads included - so any sampler reading through a non-const reference
would have copied the whole texture on its first texel, silently undoing the
saving with a full copy mid-frame. `pixel_sharing` failed on it ("the untouched
copy still shares") because its own reads detached. There is no non-const
`operator[]` now, and the tool asserts that reading a non-const copy leaves
every copy sharing.

**Same pixels, proved.** `engine/tools/pixel_sharing.cpp`: a copy shares and
reads the same bytes; reading a non-const copy does not detach; a write detaches
without touching the source or another copy; `=` and `assign` give fresh
storage; Anekbah's 20 textures pooled three times over all share their source
and read byte-identical. From outside the tool: 20 headless software frames of
the street and the Vulkan offscreen frame of Anekbah byte-identical before and
after. `verify.py: engine: pixel sharing`; SHOWN TO FAIL: a `detach()` that
never detaches breaks "a write detaches the copy" and "the source and another
copy are unchanged". `engine: 3DT`, `textures`, `texture name cache`,
`engine: scene sprites`, `engine: texture filter` and `engine: sign tie` pass.

| | music + queue (`4cfa444`) | + shared texture pixels |
|---|---|---|
| headless, 60 street frames: peak RSS / footprint | 149 / 141 MB | **138 / 129 MB** |
| live heap, snapshot at 45 s | 140.2 MB | **126.5 MB** |
| texture pixel allocations | several sites, ~27 MB with the lists | **one site: 62 buffers, 12.6 MB** |

**What the new snapshot puts next** (126.5 MB live): render corners 18.2 MB; the
music 16.1 MB; the sound buffers copied per play 13.7 MB; the shared texture
pixels 12.6 MB; the collision soups 11.6 MB; `main` 10.6 MB; the scene files
7.3 MB; and two single allocations that are almost all EMPTY -
`std::vector<Texture>` 6.4 MB and `std::vector<SpriteFrames>` 5.5 MB. `spriteTex`
is indexed BY SPRITE ID (an effect names its sprite by id), and Anekbah's ids run
to 49591, so both arrays are ~50000 slots of which a few dozen hold anything:
about 12 MB of placeholders, and the cheapest next cut.

#### Third cut, done 2026-09-13 (committed as "optimization 7") - the sprite tables sparse

**What it was.** `play.cpp` kept `spriteTex` (`std::vector<Texture>`) and
`spriteFr` (`std::vector<SpriteFrames>`) indexed BY SPRITE ID, resized to the
highest id plus one, and Anekbah's ids run to **49591** for **23** sprites that
decode - so ~50000 slots each of default-constructed placeholders, and a third
array of ints (`spriteSlot`) the same length.

**What it is.** `SpriteTable`: textures and frames in hash maps by id, and
`idCount` - the old vectors' size, grown even for a record whose data was bad,
because the resize ran BEFORE that test. Every consumer already treated "past
the end" and "empty slot" alike (`particleGeometry`'s `si < size && !empty`,
the scripted sprites' and the `.CTL` effects' `>= size || empty`, the pool's
`>= size` then `rgb.empty()`), so an absent id answers null wherever an empty
slot answered empty; a texture is stored only when one decodes (a previous
id's texture survives a later record that fails, as the resize-then-assign
did); frames are always stored. `spriteSlot` is a map where absent is -1.
`particleGeometry` gained a `SpriteLookup` overload - the loop body unchanged
but for asking the lookup - and the vector overload forwards to it with the
old test, so `particle_probe` is untouched.

**Same decisions, proved.** 20 headless software frames of the street, with
the fire and smoke drawn, byte-identical before and after; the viewer's own
lines identical (`23 decoded over ids 0..49591, 133 frames`, the frame-1 pool
`+ 3 sprite = 39 slots`, `952 particles alive`). `verify.py: engine: sprite
table` pins those three; SHOWN TO FAIL: a `texOf` that never finds a texture
gives a pool of 0 sprites / 36 slots while the load line and particles do not
move. `engine: particles`, `engine: FX`, `engine: impasse fx`, `engine: scene
sprites`, `sprite ids scene-local`, `ctl effects` and `effect sprites` pass.

| | shared texture pixels (`900ea07`) | + sparse sprite tables |
|---|---|---|
| live heap, snapshot at 45 s | 126.5 MB | **114.1 MB** |
| headless, 60 street frames: peak RSS / footprint | 138 / 129 MB | **132 / 123 MB** |

The two measures disagree by design: the live heap lost both placeholder
allocations (6.4 + 5.5 MB), but the process's PEAK is reached at a moment the
sprite tables do not dominate, so it moves by half that. Quote the live heap
for what a change frees; quote the peak for what the machine must provide.

**What is left at 114.1 MB live:** render corners 18.2 MB; the music 16.1 MB;
the sound buffers copied per play 13.4 MB; the shared texture pixels 12.6 MB;
the collision soups 11.6 MB; `main` 10.4 MB; the two resident scene files
7.3 MB; geometry copies 5.9 MB. Since the snapshot that started this pass
(233.7 MB), **119.6 MB** have gone, with every frame, sample and decision
checked unchanged.

Measure, then cut, in the order the measurement says. Suspects from the
reading, none measured yet: scene files read whole (up to 7.8 MB,
`Sconcert.SCX`) with two areas resident, textures kept decoded at 32 bits,
`IAM\GAMES` (8 MB) read whole, the per-frame shadow/pose vectors reallocated.
The shipped data fitted a 32 MB machine, so nothing in it forces the current
figure.

### 7. The Vita decision

#### Measured 2026-09-14, after steps 2-11

**CPU, the main thread, capped at 30 on the M1** (`sample` 20 s, the street
start standing, windowed Vulkan, load ~3; waits separated from work by the
leaf frame of each stack):

| | main thread working | work a frame | largest leaves, ms a frame |
|---|---|---|---|
| defaults (the game's own settings, crowd 4 from the save) | 18.0% | **6.0 ms** | depth tie 0.79, crowd lights 0.65, `main` 0.57, `applyPose` 0.45, `composePose` 0.24, sin/cos 0.24 |
| `--enhance-all --density 4` | 33.3% | 11.1 ms | **`readback` 6.9** - the 4x supersample's CPU resolve, and supersampling also takes the frame off step 4b's GPU present - then depth tie 0.51, `main` 0.40, `applyPose` 0.30 |

**At the Vita's own resolution** (`--res 960x544`, defaults, same method):
main thread 18.9% working = **6.3 ms** a frame, largest leaves depth tie 0.77,
crowd lights 0.70, `main` 0.56, `applyPose` 0.54 - the same work within the
run-to-run spread - with 900 of 900 frames on step 4b's GPU present path and
30.0 fps (worst frame 35 ms). Resolution moves the GPU's work, not this
thread's: nothing measured here changes with the pixel count.

Both held 30.0 fps (worst frame 41 / 35 ms). The enhancements' extra 5 ms is
almost all the supersample resolve, which a Vita build would not ship; the
number that matters for the decision is the default **6.0 ms**. The other
threads (Metal's command queues ~0.3 ms a frame, the audio IO thread) belong to
this Mac's drivers and would be replaced on a Vita.

**Memory** (headless street run under `MallocStackLogging`, snapshot at 40 s):
**136.8 MB live heap** in 44876 allocations; physical footprint 206 MB, of
which ~41 MB is the GPU driver's (IOAccelerator 21 MB + unmapped graphics
20 MB) and 4 MB the profiler's. Largest sites: render corners 18.2 MB, posed
bodies' geometries 16.4 MB (921 allocations), the music 16.1 MB, shared
texture pixels 12.6 MB, collision soups 11.6 MB, `main` 11.5 MB, playing
sounds 9.4 MB, the two scene files 7.3 MB, the depth tie's tables ~9.7 MB.

**The CPU factor - the part that is not measured.** A Vita core is a Cortex-A9
at 444 MHz (333 by default, 444 with Wi-Fi off). The one sourced figure: the
Cortex-A9 is 2.5 DMIPS/MHz, so ~**1,110 DMIPS a core** at 444 MHz. No source
found gives the M1's performance core in DMIPS or CoreMark, and the Geekbench 5
single-core figure the M1 has (~1700) has no Cortex-A9 counterpart (Geekbench 5
does not run on those devices). So the factor stays a question, and the
decision is framed by the BREAK-EVEN instead:

* 6.0 ms of main-thread work at 30 Hz fits a 33.3 ms frame on a core up to
  **~5.5x slower** than the M1's, with nothing left for the GL driver, audio
  or the interface;
* the break-even corresponds to an M1 core of only ~6,100 DMIPS against the
  A9's ~1,110 - and the M1's performance core is a 2020 desktop-class core,
  where DMIPS/MHz alone for recent designs is several times the A9's at seven
  times the clock. That last clause is a reading of the gap, not a measurement.

#### The decision

**A 30 fps Vita build of this port, doing this CPU work on one core, is NOT in
reach** - by an estimated factor of several, not a few percent. What the port
now spends is no longer the port's overhead in the sense this file started
from (linear scans, whole-set copies, a GPU round trip): it is PER-VERTEX and
PER-FACE work - posing ~20 bodies, lighting the crowd per vertex, the depth
tie over every posed face - that the 1999 engine did on a Pentium II at a
fraction of this detail, and that the Dreamcast port presumably did with
fewer, simpler bodies.

What would change the answer, in order of how much it would move:

1. **GPU skinning and GPU lighting** - the posed bodies and the crowd's
   per-vertex light as vertex-shader work behind `renderer.h`. That removes
   `applyPose`, `composePose`'s matrix work, `applyLights` and most of the
   uploads from the CPU, and vitaGL has shaders. It changes WHERE the answer is
   computed, which this file's rule allows, but it needs its own exactness
   story (GPU float against CPU float) - PORTING's tiers, not this file's.
2. **The depth tie on a GL backend** decided differently: it exists because a
   Vulkan depth compare cannot reproduce the engine's quantised strict test;
   a backend that owns its depth format and compare may settle coincident
   faces without walking them.
3. **Using the other three cores**: posing and lighting are per body and
   independent, so they parallelise; the script VM and the session do not
   need to.
4. **Measuring on the device** before any of the above - one vitaGL spike
   running `composePose` + `applyPose` + `applyLights` over a street's bodies
   gives the factor this section could only frame.

The memory side is closer: 137 MB of live heap against the 365 MB budget
leaves room, but vitaGL keeps its own texture copies and a Vita build would
want the music streamed rather than held (16 MB) and the posed geometries
shared or on the GPU (16 MB).

Nothing in this decision starts the port; it closes step 7.

Re-run step 1. The Vita's CPU is several times slower per core than the M1
(order of magnitude, not measured here), so the question is whether the
per-frame CPU work left after steps 2-5 fits well inside 33 ms divided by that
factor, and whether step 6 brought peak memory under the budget with room for
vitaGL. The porting work itself (an OpenGL ES backend behind `renderer.h`,
SDL on Vita, `std::filesystem` replaced in `datafs`, controls, 960x544) is a
separate plan and is not started from here.

## What is NOT in scope

* The software renderer's speed. It is the reference and a comparison tool;
  a Vita build would never use it.
* Anything that changes a decision to save time - fewer shadow bones, a lower
  crowd density, a shorter clip distance. Those are options the game already
  has (rows 3, 6, 7), and a platform may DEFAULT them lower; the engine's
  answer for a given setting stays the engine's.
