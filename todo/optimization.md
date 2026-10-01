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
| 15 | **the audit of 2026-09-29** (M3, GLES build) - what is left after 1-14 and GPU skinning 1-4, ranked for the Vita; see "15. The audit" below | **DONE** 2026-09-29 |
| 16 | C2: the MUSIC decoded through a table - `AdpcmStereoStream` looks up a delta and a next index instead of branching per nibble; `pull` reserves what it appends | **DONE** 2026-09-29 - exact (93,323,264 states, 145 tracks, 17 voices); M3 whole-file decode 1469 -> 1012 ms, a 120 s music pull 52.0 -> 31.7 ms; `engine: adpcm table`. Smaller than the audit said - see "16." below |
| 17 | C1: the GLES SUBMIT - state that has not changed is not set again (blend, depth mask, texture, uniforms, attributes), and a buffer's dirty runs a small gap apart go in one write | **the state cache DONE** 2026-09-29 - exact (probe and street, same binary); 1809 -> 92 state calls over 248 draws a frame; `engine: gles state cache`. **The run merge REFUTED and not committed** - it assumed the dirty list sorted, and it is not; see "25." below |
| 25 | ~~the GLES dirty upload draws a different street from a full upload~~ | **CLOSED 2026-09-29 - NOT A BUG, and the finding was mine**: the different street came from step 17's own uncommitted run merge. The dirty list is complete and the partial upload exact (`OMK_DIRTY_AUDIT`); see "25." below |
| 26 | RAM/GPU: `GlesRenderer::vbo_` / `poseVbo_` are keyed by `Geometry*` and never release a buffer - a geometry that is gone keeps its GPU buffer for the process's life | **DONE** 2026-09-29 (`1ff5119`): `Geometry::resident` (`GpuResidency`) notifies the GLES and Vulkan backends when a geometry they hold is destroyed; entries erased at once, GPU objects deleted at the next `begin`. `engine: geometry release` (a new geometry at the SAME address draws as a fresh one, 0 pixels) |
| 27 | **the DEPTH TIE BAKED AT LOAD** - the original's answer to a coincident pair never changes at run time, so compute it once per set load and drop the loser, instead of resolving ties every frame; see "27." below | **the census DONE** 2026-09-29 (`engine: tie census`) - and it REFUTES the pure bake: 1540 of the sets' 5361 coincident groups span two meshes, door leaves among them, which move apart. The exact design is a HYBRID (within-mesh groups baked, cross-mesh groups resolved at run time); not built - see "27." |
| 18 | C3: the engine's per-frame HEAP churn, ~600 allocations and ~2.4 MB a frame - the sites listed in step 15 | **DONE** 2026-09-29 - 1214 -> 651 allocations a frame in the software street (-46%), and outside the software rasterizer ~724 -> ~160 (-78%); every step byte-identical; see "18." below. **Leftovers, later the same day (`071c7f0`)**: the walkers' and staged bodies' pose buffers reused, `applyPose`'s `tieClass` in place, the mirror pass's three draw-list copies gone - **651 -> 567** |
| 19 | C4: per-body lookups that never change - foot bones, `charModelFor`/`lodRestFor`, `skeletonRootOf`, shadow bone meshes, `composePose`'s parent table - cached per model or body | **DONE** 2026-09-29 for the walkers (`071c7f0`): root, cut rest and both foot bones cached on the walker, keyed on its model, tracks, model name and a generation; byte-identical. `composePose`'s own parent table and the staged bodies' shadow-bone lookups stay |
| 20 | C7: the Vita's flags - `-mcpu=cortex-a9`, LTO - against `omk_bench` | **BUILT, EXACT, NOT MEASURED** 2026-09-29: `OMK_VITA_TUNE` (off by default) and `make vita-tuned` -> `build/vita-tuned/`. The bench in Vita3K gives the three hashes of the default build bit for bit (`166ee410140f9954`, `8f591196556c0243`, `e8818bdc60e4b190`); the emulator's times (41.1 against 48.3 ms/frame of body stages) are not the console's. LTO needed `printf_c99.cpp` compiled `-fno-lto` (its `__wrap_*` symbols). The tuned VPKs are ~4% smaller. **A console `omk_bench` of each build decides** |
| 21 | C8: the VM's per-instruction `getenv` and operand vector (cutscenes and transitions only - the steady street runs ~50 instructions in 150 frames) | open |
| 22 | RAM: textures kept as palette + indices on the CPU (15.8 MB of RGB888 today), the converted-sound cache bounded, the logs that only grow | open |
| 23 | GPU: a smaller `GpuVert` (36 bytes of floats), and the texture format the original device was asked for | open |
| 24 | GPU: the interface frame's round trip - upload changed rows only, or compose on the GPU (step 4's open half) | open |

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

**How much do BODIES tie, over the whole cast?** `handoff-vita.md` §2 item 1
says the rule "has to be derived, not guessed", and gives two points: PSH_FN 3
coincident faces at rest, HO1_FN 0. Over all **181** `MESHES/PERSOS/*.3DO` at
rest: **44 of them tie at all (24%)**, 438 faces in total, worst `SLI_FN` at 46,
and the largest model in the cast is `AST_FNM` at 1070 triangles (mean 463). So
a quarter of the cast has coincident faces standing still and the tie cannot be
skipped for bodies on a "they never tie" argument. What is still NOT derived is
what POSING does to that - skinning can create and destroy coincidences, which
is why step 10b could not prove a walk reusable across poses.

**And a hitch attributed to bodies was a SET** (2026-09-17). A play session on
Jaunpur's streets logged six frames over 66 ms; one carried
`vulkan: depth tie - 168 of 3075 triangles coincide` and was read as the tie
being built for a posed body. It cannot be: the backend's line gates on
`g->corners.size() > 3000` and prints `corners / 3`, so it can only fire above
1000 triangles and **no character model in the game reaches that**. The
geometry is `MESHES/DECORS/SRest02.3DO` - 3075 triangles, 168 drops, both exact
and unique in the tree (near misses `QResto03` 3031/174, `QResto08` 3047/174,
`Aresto` 2567/164). So the frame is a SET becoming resident, alongside the
texture-pool rebuild in the same frame, and the cost is the tie's full walk at
LOAD - a one-off, and a different fix from the per-frame body work this item is
about. `tie_mem` is what identified it.

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

> **Corrected 2026-09-29 (the reader).** "At a fraction of this detail" is not
> established and the crowd says otherwise: its density is the engine's own
> rule - `Slider_Init` places one walker every `39 x (5 - density) x h[3]`
> units along each pedestrian lane of the same `.OPT` circuit (a SPACING, not
> a count), on the same models (`docs/STREET_LIFE.md` §2) - so the
> port draws the SAME characters the original did, and the original posed
> and transformed every one of them on the CPU (`D3DTLVERTEX`: the engine
> hands D3D vertices it has already transformed). What the port adds is its
> own - GL driver calls, uploads, and the depth tie (step 27) - not detail.

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

### 15. The audit - 2026-09-29, on an M3

**MACHINE: an Apple M3**, not the M1 every earlier figure in this file comes
from - the reader alternates the two, so a millisecond here is NOT comparable
with one above. The COUNTS (allocations, patches, instructions, bytes) are.

The run: `build/omk-play-gles` (the Vita's renderer, drawn by macOS's GL 2.1)
on the street start, `OMK_NO_GPU_PRESENT=1` (a hidden window - which also
forces a readback the console does not make, so `readback` and the 888 -> 565
dither in the samples are the measurement's, not the game's). **The GL swap
BLOCKS while the display is off**: two runs sat on frame 1 until killed, so a
GLES measurement needs the screen awake.

What the M3 says, and why the console decides it: the main thread mostly
WAITS on the swap; the engine's own spans are small (pedestrians ~0.2 ms,
session 0.1, staged 0.1) and the largest engine-owned one is the MUSIC,
0.5-0.6 ms a frame. The console's last city log on the CPU path (2026-09-27)
was ~150-160 ms of work: submit 42-47, staged 29, pedestrians 20, music
25-34 in the frames it ran, placement 6.8, grids 7, lights 5 - and GPU
posing has since taken most of the body cost, which leaves SUBMIT largest.

| # | found | measured / read | exact fix |
|---|---|---|---|
| C1 | `GlesRenderer::submit` sets blend, depth mask, texture, 7 uniforms and 4-6 attributes on EVERY draw; 69-108 buffer patches a frame, each a driver call | M3 sample: `glBufferSubData` ~19% of the main thread, the tie's patches ~6% more; ~85% of all heap allocations are the Mac driver's, one per GL call | skip unchanged state; merge dirty runs a small gap apart (step 17) |
| C2 | the music decoded a byte at a time through branches, on the main thread | console 25-34 ms a decoded second, ~1 ms a frame averaged at 30 fps; the M3's "0.5-0.6 ms a frame" was WRONG - see 16 | a delta / next-index table (step 16) |
| C3 | the engine's heap churn | a malloc interposer: 115-160k allocations a second at 30 fps, 14% of them from `main` - ~600 and ~2.4 MB a frame | `phSpan` (a `std::map<std::string>`, four lookups a pedestrian), `composePose`'s returned vectors, `draws` not reserved, `vis` per slot, the three motion maps rebuilt a frame, `motionLogged`'s string a motion a frame, `session.props()` by value, `pumpZoneSlots`' map and vector, `sweepSphere` / `soupInBox`'s id vectors, `particleGeometry`'s map of vectors, `applyPose`'s `tieClass` |
| C4 | per-body lookups that never change | read | cache per model / body |
| C5 | the player still posed on the CPU | `gpu-skinning.md` step 5 | **done 2026-09-30**, step 30 |
| C6 | the moving collision grid rebuilt a frame from 2730 triangles | console "grids" 7 ms | incremental |
| C7 | Vita flags: `-O2 -mfpu=neon`, no `-mcpu=cortex-a9`, no LTO | `backends/vita/CMakeLists.txt` | measure on `omk_bench` (`-ffp-contract=off` keeps the hash comparable) |
| C8 | `getenv("OMK_VMTRACE")` and a heap operand vector per VM instruction | the street runs **~50 instructions in 150 frames** (`OMK_VMTRACE`, software build) - cutscenes only | cache the flag; a fixed array |

**RAM** (M3, street, live heap ~113 MB; the 297 MB peak footprint is ~106 MB
macOS GL driver): textures 15.8 MB of RGB888 on the CPU beside the GPU copy
(the `.3DT` is PALETTED, so index + palette is exact at a third), the two
scene files 8.3 MB whole, the converted-sound cache 6.5 MB and unbounded,
`Threads::Threads` 4 MB unexplained, and logs that only grow (`ActorRuntime::log_`,
`zoneLog_`, `nothingHere_`, `Fight::events_` - whether anything clears them
is NOT checked).

**GPU**: the tie on the Vita (`--no-tie`, the reader's console run, still
owed); `GpuVert` 36 bytes of floats; P8 textures on GXM, after reading what
format the original device was asked for; the interface frame's round trip;
and draw ORDER is the engine's decision, so batching is limited to what C1
does.

Refuted on the way, so nobody repeats them: the VM's per-instruction costs
are real but not the street's (C8); `composePose`'s parent table was already
`n log n` since `67c7848` - it is still per call, which is C4.

### 16. The music through a table - 2026-09-29, on an M3

**A CORRECTION FIRST.** The audit called the music "the largest engine-owned
span, 0.5-0.6 ms a frame" on the M3. That came from a `--frames` run, which is
UNCAPPED and does not pace audio in real time; the same M3 paced at 30 fps
reports `music 0.0` and `music top-up 0.0-0.1`. A standalone pull at the
game's rate costs **0.014 ms per 1/30 s** before this step. What remains true
is the console's own measurement: 25-34 ms for a decoded second, so about
**1 ms a frame averaged** at 30 fps. A span read off an uncapped run
measures the run, not the game - quote spans from a paced one.

The change: a channel's state is its predictor and its index (the step is
always `step[index]`), so a nibble's effect is a function of (index, nibble)
alone - a signed delta added before the clamp, and the next index.
`AdpcmTables` builds the 89 x 16 of them once; `adpcmDecode` (voice lines,
the `.3DM` audio) and `AdpcmStereoStream` (the music) look them up.

| M3, best of 5 | branching | table |
|---|---|---|
| 145 tracks decoded whole, 195 MB | 1469 ms | **1012 ms** (1.45x) |
| 120 s of music pulled in quarter seconds | 52.0 ms | **31.7 ms** (1.64x) |

Exact three ways in `tools/adpcm_equiv` (the branching law kept verbatim):
all 93,323,264 channel states, every track whole AND streamed, every
voice-over; and `engine: morph+ADPCM` 777/777 sample-identical to
`tools/adp.py`, an independent decoder. The mutation (next index clamped at
87) turns `engine: adpcm table` red on 3,145,728 states, 50 tracks and 5
voices. What the A9 gains is not measured: its branch predictor is weaker
than the M3's, which favours the table, and a 7 KB table sits in its L1.

### 17. The GLES draw-state cache - 2026-09-29, on an M3

`GlesRenderer::submit` set every piece of state a draw needs on every draw:
blend and depth mask, the texture, the texture size, the cutout flag, the fog
range and colour, the posed program's lights, and four to six vertex
attributes with their pointers. `DrawState` now remembers what the last draw
left and skips a call that would set the same value. It is FORGOTTEN at
`begin` and at `setTextures` (which binds textures and may free ids), and the
uniforms are kept per PROGRAM, since that is where GL keeps them. The
attributes are safe to keep across the uploads' and tie patches' own binds
because a pointer captures its buffer when it is set, and no buffer is ever
deleted mid-life (which is step 26's point).

| M3, same binary, `OMK_GLES_NO_STATE_CACHE` off / on | off | on |
|---|---|---|
| Aapkayl, dialog 402's camera, fog on: state-call groups over 25 draws | 175 | **33** |
| Anekbah street, a frame at 1400 frames: state calls over 248 draws | 1809 | **92** |
| pictures | - | **identical**: the probe 0 pixels (and 0 on a second frame), the street byte for byte at frames 300 and 1400 |

A "group" is one decision - the attribute group alone is up to twelve GL
calls - so the calls themselves fall further than the groups. What the
console gains is NOT measured: the submit section was 42-47 ms of the city's
frame, and how much of it is state setting rather than uploads and draws is
the next console log's to say (the `gles state` line prints every 60 frames).

**The other half, merging dirty runs a small gap apart, is REFUTED and was
not committed.** Two reasons, each enough alone:

* it saves almost nothing - at a 96-corner gap the street's patch count did
  not move at all (the moving meshes are far apart in corner order), and a
  gap wide enough to take everything only went from 4 to 2 a frame;
* it was NOT exact, and not for the reason first given here. It assumed the
  dirty list was sorted (as the loop's own comment said) and so skipped a
  run that started below the one it was widening; the list is built mesh by
  mesh and is not sorted. The first account blamed "stale corners" in the
  buffer and opened step 25 on it - see "25.", which closes that.

**Step 25** was opened here on the strength of a street frame that drew
537 pixels differently with whole and partial uploads. It is CLOSED as not a
bug - the next section says why, and why the evidence for it was wrong.

### 18. The frame's heap allocations - 2026-09-29, on an M3

**Measured, not guessed.** The GLES build's allocations are ~85% the macOS
GL driver's, so the count is taken in the SOFTWARE build (dummy video,
`--res 320x240`): a malloc interposer counts every allocation over a 100-frame
and a 400-frame street run, and the difference over 300 frames is the steady
state. A sampling interposer (1 in 31, 18-frame backtraces) against a `-g`
build of `play.cpp` with `dsymutil`, symbolised with `atos -i`, attributed
them - and attributed by the innermost `omk::` function, because line numbers
alone send every allocation inside an object built without `-g` to its call
site in `play.cpp`.

| site | share of the frame's allocations | what it was | now |
|---|---|---|---|
| `SoftwareRenderer::submit` | 40% (76% of the bytes) | the reference rasterizer's own - **not the Vita's**, and out of scope | untouched |
| `Program::chain` | 22% | a `std::vector` and a `std::set` (a node an element) every tick of every program | a by-value buffer, 16 inline; "seen" is a scan of what is already out |
| `applyLights` | 11% | three vectors and 768 ramp entries per reaching light per body | one 256 x 256 ramp built at load, the lights 32 inline |
| `shadowBonesFor` | 6% | its list built per body per frame | four tables built once |
| the motion patch, `motionAt` | 6% | two maps rebuilt every frame | scratch vectors; the patches sorted by mesh index before the walk |
| `particleGeometry` | 3% (12% of the bytes) | a map of vectors, every corner copied twice | scratch kept across frames, batches key by ascending key |

| software street, 300 frames, M3 | before | after |
|---|---|---|
| allocations a frame | 1214 | **651** |
| bytes a frame | 9.35 MB | 7.9 MB |
| allocations a frame outside the software rasterizer (~490) | ~724 | **~160** |

**Exact at every step**: the software street at frame 300 is byte-identical
(`c9095c50...`) with the crowd lit on the CPU, the shadows drawn, 1101
particles alive and 33 meshes moving - each change touches that frame - and
the checks that pin each path stay green (`engine: programs`, `scene steps`,
`intro beat`, `editing hold`, `character shadow`, `threaded bodies`,
`gles pose`, `engine vertex light`, `per-pixel lighting`, `particles`,
`impasse fx`, `scene sprites`, `patch index`, `dirty corners`,
`tunnel doors`, `scene sounds`). No new check: the frame that proves it is
the one `engine: threaded bodies` and `patch index` already render.

**Left**: `composePose` (~3.5%) and `applyPose` (~2.4%) returning vectors
per body, `allMotions` / `allScales` copying names every frame (made views
into the runners' storage they would be a lifetime question for ~30
allocations), `clipTracks`, the mirror pass's `std::vector<Draw>` copies.

**A QUESTION FOUND ON THE WAY, not changed**: `shadowBonesFor(-1)` returns
NOTHING - the chest's `minLevel` is 0 and the test is `detail >= minLevel` -
while the function's own comment reads `Actor_DrawShadow`'s switch as "-1 and
0 fall to the chest", and an npc takes the level minus one. At detail 0 that
would mean npcs cast no shadow where the switch says they cast the chest's.
It is the engine's switch that decides it, and it has not been re-read here.

### 27. The depth tie, baked at load - the plan (2026-09-29)

**Why the per-frame tie exists at all.** The original shows the FIRST-drawn
of two coincident faces: a strict `GREATER` on a quantised z-buffer, draw
order = the bucket key ascending (`docs/ASSETS.md` §4b). A GPU's float depth
compare cannot reproduce that - the two faces of a sign are the same four
vertices wound the other way, split on different diagonals, their depths
2e-7 apart - so the port degenerates the loser in the vertex buffer, and
decides WHICH every frame (`o3de/depthtie.*`, the Vulkan and GLES backends).
A 16-bit depth buffer with a strict test is NOT enough: the GLES backend has
exactly that, and without the tie the GPU gave the second face the whole sign.

**Why it need not be per frame** (the reader's point: the original has no
flicker, so its answer is reproducible without a costly one):

* the pairs sit in ONE mesh (all 18 of Anekbah's, the shop signs' two sides),
  so the state bits cancel and the order is decided by the TEXTURE SLOT alone
  - the lower material index, drawn first, wins;
* a mesh that moves moves every corner through the same transform
  (`placePoints`), so a pair stays coincident, bit for bit, and keeps its
  winner; a posed body's pairs likewise, which the posed tie already relies
  on ("the answer is the same in every pose");
* the slots are fixed when a set LOADS - the 58-slot cache may hand a set its
  neighbour's slots, which can reorder a pair, but only at a load.

So the original's answer is a property of (set, load), and computing it once
there - dropping the later-drawn face from the geometry - reproduces the
original INCLUDING its texture-cache quirk, at no per-frame cost.

**What must be checked first - the census.** Across all 635 models: the
coincident pairs within one mesh against across meshes, sets against
characters, and whether any sits on geometry that deforms non-rigidly. A pair
across two meshes that move independently is the one case a load-time answer
could get wrong. Then the bake, proven by same-binary GLES street frames baked
against the per-frame tie, byte for byte, over frames with moving cargo, and a
check that goes red when a loser is left in.

**The census, done 2026-09-29** (`engine/tools/tie_census.cpp`, `verify.py:
engine: tie census`; faces keyed exactly as the tie keys them):

| | coincident groups | within ONE mesh | ACROSS meshes |
|---|---|---|---|
| sets (`DECORS`, 220 models) | 5361 | 3821 | **1540** |
| characters (`PERSOS`, 191) | 476 | 302 | 174 |
| objects (`OBJETS`, 211) | 99 | 99 | 0 |
| Anekbah | 169 | 165 | 4 - `Ported30`/`Porteg30`, `Ported26`/`Porteg26`, `Ported07`/`Porteg07`, `Porte25d`/`Porte25g`: door leaves |
| AImpasse | 0 | 0 | 0 |
| Aapkayl | 84 | 16 | 68, every group touching a blended draw (`eclair`, `cent`, `lum fx`), and a blended face claims nothing |

**So the plan above is wrong in its premise, and the census is what said so.**
"Both faces sit in one mesh" held for Anekbah's 18 shop-sign pairs and does
not hold for the corpus: across-mesh groups are common, and the leading
examples are DOOR LEAVES, which move - open, the faces separate and the loser
must draw again; closed, they coincide again. A load-time answer would leave
that face missing while the door is open.

**The exact design is a hybrid**, argued here and not built:

* a group WITHIN one mesh is motion-invariant: a moving mesh moves every corner
  through one transform (`placePoints`), so its coincident faces get
  bit-identical positions from bit-identical inputs and stay coincident; they
  share the mesh's visibility, so both draw or neither does; and their order
  is the bucket order, fixed per load. Resolving these ONCE per set load gives
  the per-frame tie's answer exactly;
* a group ACROSS meshes keeps a run-time resolution, but only over its own
  units - four groups in Anekbah instead of the set's 30089 units;
* a coincidence CREATED by motion between faces not coincident at rest needs
  bit-identical positions from different transforms, which the float math does
  not produce in practice - but "in practice" is a claim, so the build would be
  proven the way every step here is: the hybrid's losers against the per-frame
  tie's, frame by frame, over runs with moving cargo and opening doors, 0
  differences, beside `engine: tie equivalence`.
* characters need nothing: the posed tie already keys by position AND mesh
  (`Geometry::tieClass`), resolves on the rest geometry once, and cross-mesh
  groups never tie for a body.

The prize is not measured. On the M3 the tie's walk and replay are ~1% of the
main thread (a `sample` of the GLES street, 2026-09-29); the console's figure
since the patch cut (~150 -> 0 patches a frame) has not been logged.

### 25. The "dirty upload bug" - closed, and it was the merge's (2026-09-29)

**What was claimed**: the GLES partial upload draws a different street from a
whole upload (537 pixels at frame 300, in a 42x49 patch at x 440-481,
y 155-203 of the 800x600 dump), with the whole upload the closer to the
software reference, 511 pixels to 14.

**What is true**:

* the partial upload is EXACT. `OMK_DIRTY_AUDIT=1` (kept, in
  `glesrender.cpp`) keeps a copy of each buffer's last corners and, on a
  partial upload, finds **0 corners changed that the dirty list does not
  name**; on the desktop it reads the buffer back after the write and finds
  **0 corners different** from a whole upload's bytes;
* the dirty list is **NOT SORTED** - `play.cpp` builds it mesh by mesh, ~20
  descents a street frame. The loop that writes it does not care (each run of
  consecutive corners is written wherever it lies), and neither do the Vulkan
  uploader or the depth tie (per-corner writes, per-triangle stamps) - but
  step 17's merge DID: it widened a run to the runs "after" it and so skipped
  any run that started below it. Its comment said "sorted by construction",
  and so did the GLES loop's; the loop's now says what is true;
* so every "partial" street frame in step 17's comparison was drawn by the
  merge build (at every gap, 0 included - the order bug does not need a
  gap), and every "whole" one was not. Restored to the committed loop, the
  partial frame is the whole upload's frame, `0b3f...`, under every condition
  tried: a different environment, `MallocPreScribble`/`MallocScribble`, eight
  busy cores, the thread pool off, 40 ms of sleep a frame.

**How the wrong turn went, because two of its steps are traps worth naming:**

1. A GLES `--frames` run BLOCKS AT FRAME 0 while the display sleeps (the
   reader was away), and a watchdog that kills it still gets a `--dump` - of
   frame ONE. Four such dumps agreed with each other, and that agreement was
   read as a fix: an interface clock read off the wall (`SDL_GetTicks()` in the
   composer) was moved onto the frame and committed (`446beb7`) as the cause.
   It was not the cause, and it is REVERTED; its check could not be shown to
   fail on any scenario tried, which is what gave it away. **A dump is
   evidence only with the frame count it came from** - read `N frames
   presented` in the log before comparing it, and hold the display awake
   (`caffeinate -d -u`) for any GLES run.
2. Timing looked like the cause because the different pictures grouped by
   WHEN they were made - which they did, because they grouped by which BUILD
   was on disk at the time. A cross-build comparison was taken for a
   within-build one.

The interface clocks DO read the wall in a `--frames` run, against the rule
the frame delta follows. That is a real inconsistency but no frame tried shows
it (software 90 and 300 frames, 320x240, GLES 60/150/300 frames, all
identical with 0 and 40-60 ms of sleep a frame), so it is noted here rather
than changed.

### 28. The port against the ORIGINAL on the console - where it does more (2026-09-29)

From the reader's console log `omk-play-20260929-205655.log` (default build,
GPU posing DROPPED by the self-test, see `vita-port.md` 2026-09-29) and four
read-only comparisons of each section against the original's code. "READ" is
the original's code at the address; ms are the console's, per frame unless
said. The original's model throughout: one rigid 3x3 per visible mesh, each
unique vertex transformed ONCE into a `D3DTLVERTEX` (`sub_494650`,
`sub_4947F0`), bodies culled whole before any of it, interface blits and
blended quads on the card, sound mixed by DirectSound at 22050, loads spread
over frames by an async reader.

Ranked by what it would save, with what is already done marked:

| # | where | the port | the original (READ) | ms | fix |
|---|---|---|---|---|---|
| a | bodies: `staged skin` 19, `ped apply` 22, and ~29 of `world begin..end` | every body posed on the CPU (two `qrot` a corner, position AND normal, then a second pass for yaw/offset), then re-uploaded whole: 53 uploads, 7.1 MB, 34 ms; vitaGL's `glBufferSubData` on a buffer drawn in the last 4 frames allocates a new one and copies (`buffers.c:474-500`), into UNCACHED memory | `sub_4947F0`: each unique vertex once (200 in PSH_FN against 1143 corners), no normal work when the body is unlit | ~60-70 | **the self-test fix** (`int(floor(aSlot + 0.5))`, built, awaiting the console); CPU fallback: fold yaw/offset into the per-mesh affine, one pass, normals only when a light reaches, a streaming buffer skinned into directly |
| b | start menu `screen draw` 73 | the cloud computed at 640x480 into a NEW 614 KB surface every frame (`screendraw.cpp:363-376`), a new 128 KB work buffer (`cloud.cpp:88`), then a nearest STRETCH of 522K pixels to 960x544 | `sub_4B19C0` allocates both once, at open; `sub_4B1B00` does the two passes; the blit is 1:1 at 640x480 | ~40-50 | buffers as members; the warp and stretch fused into one pass (byte-identical); or the 640x480 cloud as a GPU texture |
| c | `dialogue text` 28-34 | the subtitle box per pixel on the CPU; `overlayPlanes()` called 5x a pixel, a function-local static: `__cxa_guard_acquire` and FIVE `dmb ish` barriers in the loop (read in the Vita object); plus the per-pixel divide (fixed) | `sub_4400D0`: ONE `I2D_SubmitQuad`, mode 2/4, layer 10 - a blended D3D quad | ~20-30 | hoist `OverlayPlanes&` out of the loops (also `fillQuad`, `surface.cpp`, `hudbar.cpp`); or the box as a GPU quad under the overlay blend |
| d | moving set meshes: meshes placed 7, grids rebuilt 7, soups patched <4.5, partial upload 4-5 | the set baked into world corners and soups, so 8229 moving corners (5 Cargo, 3 Mirador, tete03, 9 Epale, 15 CA*) and 2730 collision triangles rewritten, two grid layers rebuilt, 69 buffer patches - every frame | `o3de_SetNodePos` (0x004370A0) writes 3 floats; the vertex pass runs anyway; collision per-mesh spheres (`o3de_ForEachMeshInBox` 0x004430A0) with the probe moved into the mesh's frame (`sub_4434B0`) - no grid, no rewrite | ~18-20 | a moving mesh drawn from rest corners with one matrix (the posed program, one slot); collision in the mesh's local frame against a per-mesh soup built once. Note: the patches overwrite ranges the GPU may still read |
| e | music track switch: `controller` 166/360/445 ms | `music.cpp:36` counts the resampled length one frame at a time, `while (size_t(n*step) < frames) ++n` - 6.3M double multiplies for track 3 (fits three tracks at ~55 ns a pass) | `Music_PlayTrack` 0x0041E110 queues the read; nothing counts samples | 150-430 once | closed form `ceil(frames/step)`, corrected by +-1 on the same predicate - byte-identical |
| f | area change: 1-2.5 s over 2-3 frames | `loadWorldSlot` reads .3DO + .3DT whole, builds geometry, decodes textures, four soups in ONE frame; `completeLoad` the next | `Async_LoadDuringFrame`: a 64-request queue, ONE 128 KB chunk a frame (`sub_41F320` from `Game_Tick` 0x004200F0); `Area_TickLoad` 0x0040C7E0 waits on `sub_41EFA0` | 1000-2500 once | reads through `FileFetch` from `areaLoad`, geometry and soups on a worker, uploads spread. `Music_SetFadeMode` 0x0041EFF0 is the async mode/chunk setter - rename |
| g | each line start: 133-295 ms (345 the first) | the whole .3DM read (3.4 MB, of which ~340 KB audio), decoded whole with `push_back`, COPIED into `speakerMorph` (`play.cpp:10597`), then resampled to 44100 float stereo (10.8 MB for 30 s) on the main thread | `Morph_Open` + `sub_42D960`: mmio walk, small rings, ADPCM decoded straight into a 22050 DirectSound buffer every 15 ms on the timer thread | 130-300 once | device at 22050 (no resample at all); decode in the read-ahead thread; share the bytes |
| h | `lights` 5 | per-pixel light lists and mapped-shadow slabs built for the GLES renderer, which implements NEITHER (`glesrender.cpp:35-39`) - the console's `omk.ini` has both on | `sub_4380B0` registers street life in "Lights Collisions"; per vertex, street life only; no shadow map | ~5, only with the enhancements ON | NOT a port inefficiency: both are ENHANCEMENTS, off by default in every backend, and the preparation is already gated on them (`lighting > 0`, `shadowQuality >= 2`); the console paid it because its `omk.ini` turns them on, and GLES draws neither. Fix: a renderer that cannot draw an enhancement REFUSES it at start-up with a log line (a capability query on `Renderer`), so a config shared with the Mac cannot cost the console; meanwhile take them out of the Vita's `omk.ini` |
| i | music per frame 2-10 | `pull` per output frame at 44100 (double multiply, `at()`, two `push_back`), a synchronous 32 KB read every 1.5 s, the mutex held over the callback | decoded on the winmm timer thread at 22050, mixed by DirectSound (`sub_46C3A0`) | 2-10 | 22050 S16 device; decode in the callback or a stream thread |
| j | bodies: no frustum cull | staged bodies skip on distance only (`play.cpp:15944-15963`), walkers on a 40 m sphere; `composePose` runs for all 25 before the skip | far distance + four planes on the root sphere before any animation (0x0048D7F0, 0x0048D3B0); an npc's shadow gated on having been transformed (`Actors_TickAll` 0x004681C0) | 30-60% of (a) before the GPU fix, 1-3 after | a frustum from the view, tested at both skip points; compose after the cull where only drawing reads the pose |
| k | crowd LOD not applied: `ped compose` 2-2.5 | always LOD0, and `composePose` poses all 76 meshes of a crowd model (+ a sort) | `sub_453A70` sorts the four skeletons, `sub_453910` chains them at 10/20/30/40 m (`dword_4C8870`), `sub_48D7F0` walks the chain by view depth and binds that level's tracks | 4-6 before, ~1 after | pick the level by the same rule, compose its 19 meshes. Caveat: past 40 m the original keeps the last level to the clip distance - the port's 40 m cut draws FEWER walkers |
| l | fixed per-body lookups, `staged resolve` 2.9 | `hasSeveralSkeletons` (76x76), `headMeshOf` (O(n^2) + strings, twice), the parent table, per body per frame | bound once in `Actor_LoadModel` 0x0041A730 (actor slots 3..19) | 1-2 | cache per model, as step 19 did for the walkers |
| m | `playSound` / `sounds` 9-29 spikes | each play copies the cached float sample (~1 MB for 3 s) under the lock; the cache clears on every scene change | `Sound_Play3D` 0x0046CDC0: `DuplicateSoundBuffer`, shared memory | spikes | shared int16 samples at 22050 |
| n | load panel (not in this log) | `DataFs::readPath` of the whole GAMES file (~8.4 MB) EVERY FRAME while a row is selected (`screendraw.cpp:568-569`) | the picture read when the selection moves | 100s? | cache per (path, slot) |
| o | start menu present 10-15 | `presentSurface` uploads the whole 960x544 surface each frame with `glTexSubImage2D` (vitaGL copies a recently used texture first) | a DirectDraw flip | 10-15 | the overlay's direct-pointer, changed-rows sync |
| - | `fades, flicker` 46-57 | the black fade's bands: a runtime divide per pixel in `OverlayPlanes::row` | the ticker's two quads | 46-57 | **DONE** (row cache + per-row table), awaiting the console |

**What the enhancement settings cost on the Vita**: the GPU side NOTHING -
the GLES backend implements none of MSAA, filtering, anisotropy, per-pixel
light or mapped shadows - but with the last two ON in its `omk.ini` the CPU
still prepares them (row h). With the defaults it prepares nothing.

**The device rate** is 44100 only because the films are 44100 MP2 and
`openAudio` keeps the first device it opened (`play.cpp:5635/5833`). 22050 is
the original's and removes rows g's resample, half of i and m's conversions;
48000 has no cheap ratio from 22050 (320/147). Reopen after the films (mind
the "movie audio under the next" fault at `play.cpp:815`) or decimate the
films 2:1.

**The cheap exact ones - DONE 2026-09-29** (built into the VPK, not yet run
on the console):
- **c**: `overlayPlanes()` returns a namespace-scope object (`ui/overlay.h`),
  so no call pays a guard - `drawSubtitleBox`'s Vita object went from five
  `dmb ish` and repeated `__cxa_guard_acquire` to none. Every caller of the
  planes benefits (`fillQuad`, the 50% blits, the HUD bars). The box region
  of a 960x544 conversation frame is byte-identical to the build before
  (67379 drawn pixels, 0 differ; the talker's own pixels vary run to run).
- **e**: the music length from `ceil(frames / step)` nudged on the same
  predicate - 0 mismatches against the loop over 6 device rates x 500 lengths.
- **b**: the cloud's buffers kept on `MenuCloud` (as `sub_4B19C0` keeps its
  own), and at any display but 640x480 the effect is written straight onto
  `fb` at the pixels a NEAREST `blt` would have read (`MenuCloud::drawScaled`);
  the filtered enhancement keeps the two steps on a kept surface. Start menu at
  960x544, frames 10 and 47: byte-identical to before.
- **h**: `Renderer::drawsPixelLights()` / `drawsShadowMap()` (Vulkan yes, the
  rest no); `omk-play` REFUSES the enhancement at start-up with a line, mapped
  falling back to fitted. This fixed two faults beside the 5 ms: each
  enhancement switches off what it replaces, so on the Vita, whose config asked
  for both, the CROWD WAS UNLIT (`lit = lightCrowd && lighting == 0`) and NO
  BODY CAST A SHADOW (the classic shadows run only below mapped).
- **n**: the load panel decodes a slot's picture once per (file, slot, write) -
  `saveFileWrites()` counts `writeSaveFile`, so a save or a delete refreshes it.

**j and l - DONE 2026-09-30** (`play.cpp`, all three backends alike):
- **j, the side planes**, for the set's meshes, the staged bodies and the
  crowd, on the engine's terms (`sub_48D3B0` / `sub_48D7F0`: `n . c + d > r`
  with the node's `+88` radius, after the distance test). Conservative where
  the engine's frustum cannot be matched exactly - a ROLLED camera takes the
  view rectangle's half-diagonal on both axes, the height is the larger of the
  camera's and the letterbox strip's, 179 degrees and up takes no planes - and
  OFF under a live mirror and under mapped shadows. Short runs of side-culled
  meshes between two drawn runs of a batch are DRAWN THROUGH (<= 300 corners):
  they cannot put a pixel on screen, and dropping them split one draw into two
  (Bowie 175 -> 221 draws); distance-culled runs never are. **Byte-identical
  with and without (`OMK_NO_SIDECULL=1`)** on the street, three Bowie frames
  (one rolled 3 degrees), the Impasse at a -28 degree roll (letterboxed), and
  on GLES; the street's set runs 1264 -> 593, walkers drawn 13 -> 4, the Bowie
  frame's draws 175 -> 149.
- **l**: the head mesh, the several-skeletons count and the skeleton root are
  cached on the model (`CharModel::headOf`, `severalSkeletons`,
  `skelRootByFirstId`), as `Actor_LoadModel` binds its bones once.

**k - DONE 2026-09-30**: the crowd's LOD, `sub_48D7F0`'s chain walk by view
depth with row 7's skip and `Anim_BindNodeTrack`'s index offset
(`docs/STREET_LIFE.md`), and `composePose` given a mesh mask so a walker poses
its drawn skeleton's 19 meshes and not the model's 76. Level 0 with the mask is
byte-identical to no LOD (`OMK_PED_LOD_MAX=0` against `OMK_NO_PED_LOD=1`); with
LOD on, the worst foot-to-body offset is unchanged (22.8 / 5.3 units, against
~240 for a bone off the wrong skeleton) and a 20 m walker draws on the coarser
skeleton in the same place and pose. The CPU gain is below the M1's 0.1 ms
resolution; the console's `ped compose` (1.2-2.5 ms) is what it should cut.

**g, i, m - the device at the primary's 22050 - DONE 2026-09-30.** `Sound_Init`
(`sub_46C3A0`) sets the DirectSound primary to 22050/16/stereo and what the game
ships is 22050 (61 of 63 WAVs, the other two 22080; the voice ADPCM; the
music), but the port's device ran at 44100 because the FILMS opened it first -
so every world sound was stretched 2x: the music per output frame, each voice
line whole on the main thread. The films keep their 44100 (the original played
them through DirectShow, whose output never met the primary), and the world
REOPENS the device at 22050 after them (`Frontend::reopenAudio`: the device
closed, then every queued sample and voice dropped, then opened). The voices
and effects convert at the same rate - a float conversion and nothing else
(`resampleToDevice`'s new same-rate path, equal to the generic loop at step 1),
and the music pulls half the frames. Headless with SDL's dummy driver: the
films play in real time at 44100, the device reopens at 22050 when they end,
and track 109's ten-second peak is 0.790 - the console log's own figure. The
Vita's SDL (2.32.8) opens its BGM port for a rate under 48000, which 22050 is.

**The whole-buffer uploads - DONE 2026-09-30.** The console's Bowie frame sent
16.7 vertex buffers a frame, 15.7 of them WHOLE, for 15.9 ms - about 1 ms an
upload whatever its size, vitaGL's allocate-and-copy for a buffer drawn in the
last frames (a size change is a new `glBufferData`). `OMK_UPLOAD_LOG=1` names
them: the sky (864 corners, rewritten every frame because it follows the
camera), a 1626-corner geometry every frame, bodies posed on the CPU (~1530),
the particles (~6650, a new size every frame), shadow quads. **GLES**: a
geometry sent whole in consecutive frames is STREAMED into one ring allocated
once, a third per presented frame (reused two frames later), drawn at an
offset - on the Vita written through `glMapBuffer` into the ring's own memory.
The frame is counted on present (the mirror pass begins twice). Off with the
depth tie; on by default on the Vita, `OMK_STREAM=1` elsewhere. Byte-identical
streamed and not on the Bowie frame, the street and the Impasse cutscene;
whole uploads 7.1 -> 0.1 a frame on the Mac, 7.0 streamed. **Vulkan** had the
same fault in its own form: a size change destroyed the buffer and allocated
device memory, every frame for the particles; a buffer now keeps a CAPACITY, a
re-sized geometry getting half as much again (a first allocation stays exact).
Byte-identical to the previous Vulkan code on the Bowie frame.

**The grid and the patch - instrumented, not changed.** On the Mac the moving
layers are 39 x 49 and 64 x 49 cells with ~1500 and ~2800 entries, and the
patch ~16000 points: well under a millisecond on the A9, against the console's
6.9 + 5.7 ms. Something inflates them there; one candidate is the vitaGL
garbage collector freeing the fifteen buffers a frame the whole uploads
queued, which the ring removes. The next log splits them: `grid moving (floor)`
/ `(steep)`, `motion corners` / `motion soups`.

**"pedestrians, traffic", read against the original - 2026-09-30.** The
console's section was 9.5-15.6 ms while its named walker spans came to under
2 ms, because the section also holds the VEHICLES and the PLAYER - the two
geometries OMK_UPLOAD_LOG saw re-sent whole every frame (~1530 corners at
several addresses, and 1626). What each does, against the original:

- **Vehicles.** The port copied the composed model (`sv.posed = sv.atRest`,
  every 48-byte corner and its per-corner arrays), yawed and moved every
  corner on the CPU and re-sent the buffer, for each of ~10 vehicles in reach,
  every frame, whether or not the camera saw them. The original treats a
  vehicle as an INSTANCE: `o3de_SetNodePos` and the facing set its node
  matrix, `sub_48D7F0` rejects it whole outside the view, and the matrix is
  applied in the draw. Ported as that: the view cull on a radius measured from
  the composed model (never smaller than the node's), and on a backend that
  poses bodies the static `atRest` drawn with one affine per mesh - the
  renderer applies the node matrix, nothing is rewritten or re-sent. The door
  animation of a boarding and the non-posing backends keep the CPU path. The
  GPU-placed vehicles match the CPU ones to 6-18 pixels of rounding (as the
  walkers do).
- **The player.** Kay'l's 1626 corners were posed, placed and re-sent every
  frame; the original draws him as any actor, and `sub_48D3B0` skips an actor
  outside the view before its matrices. Ported: while he is outside the view
  and nothing this frame reads his corners (the feet latch, a variant clip,
  melee's fist, shoot mode's arm, per-pixel light, mapped shadows), his
  corners and his draw are skipped - his pose, head, bones and shadow bones
  still come from the pose. Byte-identical with the cull off on the street and
  the Bowie sequence. His head mesh is also cached per model (an O(n^2) walk
  with a string per mesh, every frame).
- **The ring counted a frame per PRESENT**; fades and overlays present twice,
  so nothing streamed there and a third was reused after one frame. Now one
  count per frame (the first `begin` after a present).
- The console log will split the section: `ped serial`, `vehicles`, `player`.

**Correction, 2026-09-30 - the upload counter overstated.** GLES booked a
PARTIAL upload at the whole geometry's size, so the set's 69-run motion patch
read as 5 MB every frame; row (a)'s "53 uploads, 7.1 MB, 34 ms" and its split
are therefore overstated, and so is (d)'s upload share. Counted where the bytes
are sent, the Bowie sequence on the Mac sends ~0.8 MB a frame in ~8 uploads, 7
or so of them WHOLE buffers (geometry rebuilt every frame: particles, shadow
quads). The Vulkan backend keeps no such counters.

**The console log now attributes a frame under 150 ms**: every 60 frames
`sections (ms, mean of 60)` (every gap between two marks - it printed only past
`OMK_MARKS_MS`, and only when paced, so a 65 ms frame was ~50 ms unaccounted)
and `gles world` (vertex uploads, how many whole, KB sent, ms), with new spans
`motion patch`, `grid fixed`, `grid moving`. On the Mac the Bowie sequence
(`--area 0 --stand 6423,-3,1675,154 --zone-enable 78`, zone 78 'BOWIE OMIKRON
THEME') is ~5 ms of sim+draw, so the console's 65 ms has to be attributed on
the console: scripted motion is 0.1 + 0.1 ms here against ~7 + 7 there, a
ratio (~70x) far past the frame's (~13x).

**Order proposed**: confirm the self-test on the console (a); then the cheap
exact ones - c's hoist, e's closed form, b's buffers and fused pass, h's skip,
n's cache; then j and k (the cull and the LOD, both the original's mechanism);
then d (moving meshes as matrices) and the 22050 device (g, i, m); then f (the
async area load). Evidence notes: (a)'s split of the 34 ms is a fit over three
frames; (b), (c) and (d)'s shares are pixel/corner counts, not profiles; (d)'s
7 ms a section is ~40x the M3's figure and not explained by arithmetic alone -
record `motionPatch0` (declared at `play.cpp:6497`, never recorded) first.

### 29. The depth tie the ENGINE'S way - decided once, drawn a step back (2026-09-30, M3)

The reader's report, and the brief: *"I already did many runs with --no-tie,
it has the flickering issue. Do it the way the original does."*

**Why the GPU's own compare is not enough, measured.** The GLES backend
already has the original's compare: a 16-bit buffer and a strict test
(`GL_LESS` against the engine's reversed `GREATER`). The two would give the
tie for free if the coincident faces interpolated BIT-IDENTICAL depth, which
a rasterizer does for the same triangle fed in the same corner order. The
census (`tie_census`, new order classes) says they mostly are not: of the
sets' 5361 groups 1115 are the same sequence, 2986 the same triangles in
another corner order and **1260 another diagonal**, 21 of the 28 groups on
Anekbah's shop-sign meshes among them. Their depths differ by interpolation
noise, the 16-bit rounding boundaries fall inside it here and there, and the
later face wins those pixels: at the reader's sign with `OMK_NO_TIE=1`, 2 to
26 dots a frame, re-rolled, in 58 of 59 frames standing and 136 of 179
walking.

**What the engine does that the GPU does not**: a later face wins a pixel
only by being a whole buffer step nearer - `raster.cpp`'s `kDepthTie` band
reconstructs exactly that. So the loser is drawn **one step back**:

* `Renderer::bakeDepthTie(geo, order)` - the frontend hands the backend a
  set's WHOLE draw order once (its batches keyed and stable-sorted as the
  frame sorts them, nothing culled), on every `rebuildWorld` (`worldGen`);
* the GLES backend runs the same `DepthTie` over it once and MARKS the losers'
  corners in the upload (`foldBias`: phase moved down by 8192, decoded
  exactly in the scene shader), and the shader adds two 16-bit steps of window
  depth to a marked corner (`z += 4/65535 * w` in NDC);
* a baked geometry skips `resolveTies` - so it may also stream (step 28's
  ring was off while the tie was on).

Stepping back instead of removing is what the census's objection to a
load-time answer needed: a door leaf that opens off its frame still DRAWS,
the step changing nothing where nothing is coincident; and a loser whose
winner is culled shows, as in the engine.

**Measured, same binary, env toggles** (`OMK_NO_TIE_BAKE=1` the per-frame
tie, `OMK_NO_TIE=1` none), Anekbah at the reader's sign:

| | standing, 59 frames | walking + turning, 179 frames |
|---|---|---|
| baked losers | 248 (the Vulkan pass's count; the per-frame tie drops 120, what survives the cull) | 248 |
| frames byte-identical to the per-frame tie | **59 / 59** | **179 / 179** |
| frames differing with no tie | 58 | 136 |
| tie CPU a frame (mean of 60) | 0.5 ms -> **0.0** | up to 4.4 ms (the turn) -> **0.0** |
| tie buffer patches a frame | 6 -> 0 | - |

`verify.py: engine: gles tie bake` (4 s): 248 baked, a 70-frame walk
byte-identical to the per-frame tie, some frames different without it.
Shown to fail with the shader's step removed: `(248, False, False)`, the
baked frames then equal to the no-tie run's.

**And the VULKAN backend, the same day** (the reader: *"fix vulkan"*): the
same bake and mark, `scene.vert` adding the two steps to its 0..1 window
depth. Over the same 179-frame walk through `--world-vulkan`: 248 baked,
179 / 179 byte-identical to its per-frame tie, 0 equal to the no-tie run, and
the per-frame tie's loser reports 12676 -> 897 (the rest are the bodies').
The check's Vulkan half is shown to fail the same way, `(248, False, False)`.

**Not done**: bodies keep the posed tie (resolved once on the rest geometry already); a
coincidence CREATED by motion is not baked, as it was not before. **The
console has not run it**: the Vita build takes the same backend and its
shader cache re-keys on the changed source, but the tie's console cost and
the look of the signs on its depth buffer are for the reader.

### 30. Kay'l and the sky as the original moves them (2026-09-30, M3)

The two geometries step 28's `OMK_UPLOAD_LOG` still saw sent whole every frame
outside the particles and the shadow quads: 1626 corners (the player) and 864
(the sky). Both read against the original first.

* **The player** - `todo/gpu-skinning.md` step 5. The original poses no copy of
  a body (`sub_48D3B0` / `sub_494650` / `sub_4947F0`: one 3x3 a mesh, each
  vertex through it once); on a renderer that poses bodies the port now draws
  his rest with one affine a mesh, and keeps the CPU corners only on the frames
  that read them. 7-30 pixels of 480000 against the CPU path on the street,
  walking and turning, and in the flat with the mirror.
* **The sky.** `sub_41CF10` writes the node's three floats - the camera's x
  and z, its own y - and relinks it; the 12.5x scale is `Area_LoadMiscModel`'s,
  once, at load. The port rewrote all 864 corners and re-sent them every
  frame. Now, on a renderer that poses bodies, the plane is a static geometry
  scaled about node 0's origin at load (`Sky::rest`) and drawn with one
  translation (`OMK_CPU_SKY=1` for the old path); on the others the corners
  are rewritten only when the camera's x or z CHANGED, so a standing camera
  sends nothing. **Byte-identical**: GLES static against rewritten on the
  street (40733 sky pixels in the frame) and on the Bowie sequence's frame 900
  (29327, the camera flying); the software viewer against `07efd81` on the
  street, a walk and 900 Bowie frames. SHOWN TO FAIL: the translation 40 units
  out moves 4428 pixels. Three earlier "identical" results were VACUOUS - the
  sky was not in those frames, which `--sky 0` against `--sky 1` showed; a
  comparison of a thing needs a frame the thing is in.
* **Measured, GLES, a frame (mean of 60)**: the street 5.6 vertex uploads, 4.6
  whole, 667 KB -> **3.7, 2.7 whole, 585 KB**; the Bowie sequence 5.1, 4.1
  whole -> **3.1, 2.1**. What is left whole is the shadow quads (180 corners)
  and the particles (a new size most frames) - per-frame pools in the original
  too, and what the ring streams on the Vita. On the console each whole upload
  was ~1 ms (step 28), and the player's CPU pose 4-8 ms; neither is measured
  there yet.
* **A lead from the same read, NOT established**: `sub_48D3B0` rebuilds a
  body's per-mesh matrices from its track quaternions only while node flag
  `0x40000` is set, and clears it - so the original may skip that work for a
  body whose pose did not change, where the port runs `composePose` for every
  drawn body every frame. A literal search of the listing finds ONE writer of
  the bit (`sub_41D3F0` case 3, on a decor slot's node); who sets it on an
  actor is not read. Read that before building a pose cache on it.

### 31. A set prepared while the game streams it, as the original reads it (2026-09-30, M3)

Row f of step 28: an area change was 1-2.5 s over 2-3 frames on the console.

**The original, read** (`05_sys.c`, `01_file.c`):
* `sub_41EC20(buf, size, callback, arg)` queues a read - 64 requests of 16
  bytes at `0x4E91D0`, one open file; `sub_41F320`, called once a frame from
  `Game_Tick`, reads ONE `ElementSize` piece of the current request and calls
  its callback when the request is whole; `sub_41EFA0` says whether the queue
  is drained. `sub_41ECB0` is the same queue drained in a loop - what a read
  does in mode 0.
* `Music_SetFadeMode` (0x0041EFF0) is misnamed: it is the async MODE -
  `dword_4E91C0`, 1 = 0x20000 a frame, 2 = 0x10000. A rename is owed.
* `Area_TickLoad`: case 1 queues the SET (`Area_LoadSet`), case 2 waits for
  it and then calls `Music_SetFadeMode(0)` - so **only the set streams**. The
  `.SCX`, the map, the sky, the actors, the props, the `.ani` and the slider
  track (cases 2..8) are each read whole, and all of it lands in one tick. The
  original has a hitch there too; it spreads the big file and nothing else.

**The port had the wait without the benefit.** The Session has counted the
slices since 2026-09-02 (`loadSlicesLeft`: Anekbah's 3 MB is 17 frames), but
`omk-play` read the `.3DO` and `.3DT`, built the geometry, decoded the textures
and sorted four collision soups in the FIRST of those frames, then idled
through the other sixteen.

**Now** (`play.cpp`'s `SetLoad` / `prepareSet` / `askSet` / `integrateSet`,
`platform/threads.h`'s new `BackgroundJob`): that work - everything that is a
function of the two files' bytes - runs on a thread of its own while the
slices count down, and the set enters the world on the frame BEFORE the last
slice is served, so it is there when cases 2..9 run and the frame loop binds
its emitters to the arriving `.SCX`. **When it enters is the Session's count,
never the thread's clock**: the frame waits for the job if it must, so a run
does not depend on how fast the machine reads. A load the engine makes in
mode 0 (the boot, a save) or of one slice is prepared where it is asked for,
as before. `OMK_SYNC_SETS=1` does every load that way.

| walking out of the restaurant into Anekbah (M3) | on the frame | on its thread |
|---|---|---|
| ANEKBAH asked / in the world, frame | 3 / 3 | 3 / **19** |
| read 3038 KB, geometry, textures, soups, the rest (ms) | 0.6, 4.7, 5.3, 5.6, 0.3 on frame 3 | the same 17 ms, off the frame |
| the frame's wait for the job | - | 0.0 ms |
| emitters bound to `anekbah.SCX`, frame | 20 | 20 |

Byte-identical frames with `OMK_SYNC_SETS=1` and without: the software viewer
at frames 22 and 90 of that walk, GLES at 60 and 120, and the Impasse sequence
at 1500; the two logs differ only in the frame the set arrives and in timings.
Green over it: `the sky`, `engine: airlock walk`, `walk-in scene`, `tunnel door
walk`, `shop door`, `lift doors`, `threaded bodies`, `gles tie bake`. What is
NOT the same as before, and is the original's: for the slice frames the
arriving set's floor and walls are not in the world yet.

Two things found on the way, both fixed:
* **a new sky rebuilt the whole world** - both sets' soups and grids again and
  a new `worldGen`, which bakes the depth tie again - on the frame after the
  set that names it had just done all of that. It is a new section of the
  texture pool and nothing else (83 sky pixels in the walk's frame 90, equal
  to the build before);
* **a set that does not resolve was asked for every frame**, with a world
  rebuild each time (`want != w.stem` can never settle when nothing loads) -
  seen as ninety `world: rebuilt` lines in a run pointed at no data. A slot now
  remembers what it was ASKED for.

**For the console log**: `set load: NAME - N KB read in X ms, geometry, textures,
soups, the rest; asked at frame A, in at frame B - prepared on its own thread,
the frame waited W ms` and `world: rebuild - soups and grids X ms, the texture
pool Y ms`. If W is large the sixteen frames were not enough and the wait is
the stall that is left; what then remains on the arrival frames is the rebuild
(1.1-1.3 ms on the M3 for Anekbah beside the restaurant), the texture uploads,
the tie bake and the Session's cases 2..9 (`model load:` lines) - the part the
original does not spread either. Not run on a console.

### 32. A line's voice decoded on the read-ahead thread (2026-09-30, M3)

Row g of step 28: a line's start was 133-295 ms on the console (345 the
first). The 22050 device (step 28 g/i/m) removed the resample; what was left
on the frame a line starts was the ADPCM decode of the WHOLE voice, and the
read when the line had not been read ahead.

**The original** (row g's read: `Morph_Open` 0x0042C300, `sub_42D960`) never
decodes a line where it starts - the file is walked, and the timer thread
decodes the ADPCM a piece at a time into the sound buffer while the line
plays. The port's mixer takes a whole sample, so the nearest thing it can take
over is WHERE the decode runs: `DialogPlayer` already read the `.3DM` of every
branch target of the playing line on a thread (`FileFetch`, 2026-09-23); that
thread now decodes the voice too (`DialogPlayer::loadLine` in a
`BackgroundJob`), and the line's start takes the bytes and the samples as they
are. With no threads (`OMK_THREADS 0`) nothing is prepared ahead at all - it
used to be read on the line's first frame for every branch.
`OMK_NO_LINE_AHEAD=1` is the comparison.

* **The same line**: `play_dialog` now writes the FNV-1a of each line's
  samples and file. Conversations 272, 387, 401 and 402, ahead against not:
  the same bytes; 2 of 3 and 4 of 5 lines came from the thread (a
  conversation's FIRST line is the script's choice and is read where it
  starts - 401 and 402 are one line each). `verify.py: engine: line ahead`,
  shown to fail by not waiting for the job.
* **In the viewer** (`--call 387`, software): `line load:` now says `read and
  decoded AHEAD ... its own thread spent N ms on it`, and the frame's share of
  such a line is 0 ms on the M3 (it was the 1-2 ms decode). The frame outside
  the talker's picture is identical; that picture differs run to run with no
  change at all (9904 pixels between two identical runs), so a dump cannot
  judge this step and the hashes do.
* **Left**: the first line of a conversation still reads and decodes on its
  frame; the face tracks (`nodeTracks`) and the 3.4 MB copy into
  `speakerMorph` stay on the frame (0 ms on the M3, unmeasured on a console);
  streaming the decode as the original does would cover the first line too and
  needs a mixer voice that pulls. Not run on a console.

### 33. An effect's samples shared with the mixer, not copied a play (2026-09-30, M3)

Row m of step 28: `playSound` / `sounds` spikes of 9-29 ms on the console.

**The original**: `Sound_Play3D` (0x0046CDC0) plays a `DuplicateSoundBuffer`
of the bank's buffer - a second voice on the SAME sample memory. **The port**
kept each converted effect in a cache (2026-09-27) and then copied it whole
into the mixer on every play - a megabyte of floats for a three-second sample -
WITH THE MIXER LOCKED for the copy, so the audio callback waited on it; and
the log line beside two of the four play sites measured the sample's peak
again each time, a second pass over all of it.

**Now**: a mixer voice holds a `shared_ptr` to its samples
(`Frontend::playSound`'s shared form; `SdlFrontend::Shot`), the cache holds
the same pointer, and a play from the cache copies nothing. A caller's own
span is still copied, but before the lock; what the eight-voice cap pushes out
is freed after it. The peak is measured once, where the sample is converted
(`SfxSample::peak`). A sample still sounding when the scene's cache is cleared
lives until its voice ends.

* **The same sounds**: against `7b533a8`'s viewer, a 160-frame walk down
  Anekbah's street logs the same 17 `audio:` effect lines (lengths, gains and
  peaks) and renders the same frame; the shoot phase (`--area 230
  --scene-chunk 56`) the same 4. `engine: fight library` and `engine: audio
  queue bound` green. **The mix itself has no check**: nothing in the tree
  captures the device's output, and the dummy audio driver's callback runs on
  its own clock.
* **Seen on the way, not this step's**: the shoot phase's frame 400 differs
  run to run in a column at x 73-120 (902-1459 pixels) on BOTH builds, as the
  talker's picture does in a sneak call (step 32). Two comparisons by dump are
  blind there.
* **Left**: the scene's cache still clears on a scene change, so an effect's
  first play in a scene converts its WAV on the frame; the original loads a
  scene's sounds with its `.SCX`. Not run on a console.

### 34. A composed screen sent to the GPU by its changed rows (2026-09-30, M3)

Row o of step 28: the start menu's `present` was 10-15 ms on the console.
**The original** flips a DirectDraw surface it composed in place; there is no
upload to take over. **The port** on GLES sent the whole CPU-composed surface
with `glTexSubImage2D` every frame (`presentSurface` - every interface screen,
the sneak, a film), and vitaGL copies a texture the GPU used in the last
frames to a new allocation before it writes one byte of it.

**Now** `presentSurface` does what `presentOverlay` has done since the G6
work: a hash a row, only the rows that differ are sent, and on the Vita they
are written into the texture's own memory (`vglGetTexDataPointer`) rather than
through GL. A surface that is mostly moving would pay the hashes for little,
so once half its rows have changed the next fifteen frames are copied whole
without asking and the hashes retaken after. A desktop GL gets one upload when
more than a quarter of the rows changed, not a call a row.

| 120 frames, 800x600, M3 | rows sent | rows kept |
|---|---|---|
| the start menu (its cloud) | 70691 | 1309 |
| the pause screen | all | 0 |
| a sneak call (`--call 387`) | 19457 | **52543** |

* **`OMK_PRESENT_CHECK=1`** keeps a shadow of what the texture holds BY THE
  ROWS SENT and compares it with the surface every frame: `gles: present
  check - N frames, 0 rows that would show stale`. 0 on the three screens
  above. Shown to fail - skipping the odd rows reports 24426 stale - but only
  under **`OMK_PRESENT_ROWS=1`**, which makes a desktop take the console's
  row-by-row decision: without it the mutation PASSED, because a desktop sends
  the whole surface when a quarter of the rows changed and so never reached
  the broken path. The check is of the DECISION, not of GL: nothing reads the
  texture back.
* **Not tested anywhere**: the Vita's direct write. It is `presentOverlay`'s
  code, which a console has run since 2026-09-27. No verify.py check (a GL
  window). Not run on a console - there, read `present, swap` on the menu.

### 35. What the console said of steps 29-34, and four answers (2026-09-30)

The log and its reading are `todo/vita-port.md`'s 14:00 entry. The four
changes it asked for, each against the original:

* **The load waits for the set.** `Area_TickLoad` case 2 is `if
  (!sub_41EFA0()) return 0`: it waits until the reader has served the file,
  and how many frames that is belongs to the disc. The port's slice count is
  the FASTEST that can be, and a console needed 1292 ms where the count gave
  ~565 - so step 31's frame waited 727 ms. `Session::setLoadGate` lets a
  frontend hold the load until its set is ready; `omk-play` sets it on the
  Vita (`OMK_NO_LOAD_GATE=1` off) and under `OMK_LOAD_GATE=1` elsewhere, so
  every check still runs by the count alone. With `OMK_LOAD_DELAY_MS=1500` on
  the M3: asked at frame 3, in at 127, the frame waited 0.0 ms, the Session
  held 108 frames, the emitters bound the frame after, the area shown the
  next. `engine: airlock walk`, `walk-in scene`, `tunnel door walk` and `the
  sky` are green with the gate on and a 400 ms delay, and without. The default
  path is byte-identical to `OMK_SYNC_SETS=1`, as before.
* **An archive is read once.** `Archive_ReadChunk` (0x0040FF90) reads the
  2048-byte directory group and the chunk; `readChunk` read the whole file for
  every chunk. `archiveBytes` keeps AREA, SCENE and DIALOG (2.6 MB). A ranged
  read would be the original's mechanism and this port's file layer has none.
* **A line's start**: `resampleToDevice` was 47-68 ms on the console at the
  SAME rate (a 4.8 MB fill) and the `.3DM` was copied for the face. The device
  conversion is a hook the frontend gives the conversation
  (`DialogPlayer::setToDevice`), run on the read-ahead thread; the bytes are a
  `shared_ptr`. The divide a sample became a multiply by 1/32768 - the same
  float. `engine: line ahead` and `dialogue play` green.
* **A Vita build refuses a stale vitaGL** (`omk-recipe.stamp`).

**Still on the arrival frames, sized by this log and not yet touched**: the
Session's tick (`game frame` 1530 ms - now said case by case), `props, guns`
442, `screens` 297, the world rebuild's 139 ms of soups and grids (and again
for every set that joins). The set's own preparation could also be halved in
wall time by splitting geometry from textures-and-soups over two threads, and
`buildGeometry` pays five map lookups a triangle.

### 36. The texture pool set once, and the grid builder's ranges kept (2026-09-30, M3)

Two more from the 14:00 console log, neither run on a console.

* **The pool was handed to the renderer twice a change.** `rebuildWorld` set
  the SETS' textures alone, and the frame's own pool - sets, characters,
  player, sprites - was composed and set a moment later. A backend that keeps
  what it has uploaded (GLES, by pixel storage) therefore dropped every
  character and sprite texture and sent them again: the console's walk into
  Anekbah logs `4 kept, 0 uploaded, 27 dropped` then `18 uploaded, 57.1 ms`,
  and on the arrival `20 uploaded, 18 dropped, 60.4 ms` then `18 uploaded, 52.1
  ms`. `rebuildWorld` now only bumps `poolComposition`; nothing is drawn
  between it and the pool's composition (the pool block is the first thing
  under `drawWorld`). `OMK_POOL_TWICE=1` is the old order. The restaurant
  walk-out on the M3: **74 texture uploads -> 54, 18 dropped -> 0**; frames
  20, 21 and 60 on GLES, 20 and 45 on the software viewer and a cold boot's
  140th are byte-identical to the old order.
* **`buildSoupGrid` worked each triangle's cell range out twice** - once to
  count, once to fill: the min/max of six floats as doubles, four divides and
  four `std::floor` calls each time. The counting pass now keeps the four cell
  numbers (16-bit) for the filling pass, and the floor is written out. **The
  grids are byte-identical**: 32 of them - Anekbah, Aapkayl, Qalisar and
  AImpasse, walkable and steep, whole, by mask, by id list and at a 64-inch
  cell - hash the same from the old builder and the new. **No gain measured on
  the M3** (0.69 against 0.70 ms for Anekbah's pair, with a sweep running
  beside it): the M3's cost is the cell loops, not the arithmetic. It is kept
  for the A9, where a double divide and a library `floor` are dear and the
  moving layers are rebuilt EVERY frame (`grids rebuilt` 6.9 ms) - a guess
  until a console says. `engine: probe grid` and `ground grid` green.
* `world: rebuild -` now splits its soups-and-grids into the merge, the
  walkable grid and the steep one, with their triangle counts.
* **Two standing reds met again, not this step's**: `engine: split grid` and
  `engine: sweep grid` - probe and hit COUNTS against 0 mismatches, as the
  2026-09-29 sweep row records.
* **A sweep in a worktree is not a sweep**: started beside the work to save
  the two and a half hours, it reported six `... not built`, a stale
  `INDEX.md` and `readable/src absent` - a worktree has none of the
  uncommitted inputs. Killed; the sweep runs in the main tree or not at all.

### 37. A moving set mesh drawn with one matrix, as `o3de_SetNodePos` moves it (2026-10-01, M3)

Row d of step 28, the render half. The console's city frame spends `meshes
placed` 7.2 ms and a partial upload every frame on the set meshes a scene
moves - in Anekbah 33 of them (the `Cargo` ships and their `CA*` parts, the
`Epale*`, the `Mirador*`, the turning `tete03`): 8229 corners rewritten into
the set's buffer and the changed ranges sent again.

**The original**: `o3de_SetNodePos` (0x004370A0) writes the node's three
position floats and nothing else; `sub_494650` builds the node's matrix and
`sub_4947F0` sends each vertex through it on the way to the card. And the
view cull, `sub_48D3B0`, tests the node at THAT position (`+36..+44`, radius
`+88`) - where the port culled a moving mesh at its AUTHORED one.

**Now**, on a renderer that poses bodies: the first time a set mesh moves it
is taken out of the set's draw and given its own geometry - its corners as
built, less its origin, uploaded once - and drawn every frame with one affine
(scale, rotation, `at`: `placeOne`'s order), culled at its placed origin with
its radius times its largest scale. The set's buffer keeps it at rest; nothing
is rewritten or re-sent. Its COLLISION is unchanged: its triangles are still
patched into the soups and the moving grid rebuilt - the original's other half
(collision in the mesh's own frame, `sub_4434B0`) is not done. The software
and Vulkan backends keep the CPU patch; `OMK_CPU_MOTION=1` forces it, and
`OMK_MESH_AT` (which reads the set's buffer) does too. The log says `motion:
mesh 'X' drawn by the RENDERER from its own N corners` once a mesh.

* **The same picture**: GLES against `OMK_CPU_MOTION=1`, byte-identical on
  Anekbah's street (frame 90), the Bowie sequence (300, 600, 900), Kay'l's flat
  and the Impasse (600). Corners kept relative to the origin make a pure
  translation land bit for bit where the CPU patch put it, and the turning
  `tete03` lands the same too. The comparison sees them: the affine shifted 40
  units moves 534 pixels on the street and 253-2171 in the Bowie frames.
* **One frame differs, and it is the cull**: Bowie frame 1200, 1766 pixels.
  With the clip distance lifted (`--clip 0`) and the side planes off
  (`OMK_NO_SIDECULL=1`) the two paths are byte-identical, so the drawing is
  exact and the difference is WHERE the mesh is tested - its placed origin
  now, as `sub_48D3B0` tests it, its authored one before.
* **Uploads, the street on the M3**: 3.7 vertex uploads a frame -> 2.8, 585 KB
  -> 313 KB, 1.9 ms -> 0.2 ms. Green: `engine: gles tie bake` (which walks
  Anekbah with its moving meshes on the new path), `gles state cache`, `patch
  index`, `node rest`, `shop door`. Not run on a console - there, read the
  `scripted motion: meshes placed` section and `gles world`'s KB.

### 38. The moving layer as whole meshes, as `o3de_ForEachMeshInBox` tests them (2026-10-01, M3)

Row d of step 28, the collision half, after step 37's drawing half.

**The original**: `o3de_ForEachMeshInBox` (0x004430A0) tests a moving mesh
WHOLE - its sphere against the query - before any of its faces, with the probe
moved into the mesh's frame (`sub_4434B0`); when a mesh moves, nothing is
rebuilt. **The port** kept the moving triangles in a grid of their own
(step 7c) and rebuilt it every frame something moved - on the console
`grids rebuilt` 6.9 ms a city frame.

**Now** (`SplitSoupGrid::parts`, `MovingPart`, `measurePart`): the moving
layer is the moving triangles grouped by the mesh they belong to, each group
with the extent its triangles have NOW. A query takes every group its point or
box falls in, whole. That is a superset of a grid cell's list, and every
query already puts its candidates through an EXACT per-triangle test in
ascending order - so the answers are the same bit for bit. Per moving frame a
group costs one pass over its points for the extent and allocates nothing;
the ids are re-gathered only when the moving set or the merge changes. The
triangles themselves are still RE-PLACED in the soups (`soups patched`): the
probe-into-the-mesh's-frame half was not taken, because it would change the
answers in the last bits and nothing could then be proven equal.
`OMK_MOVING_GRID=1` rebuilds the grid as before.

* **The same answers**: a 150-frame walk down Anekbah's street with
  `OMK_VERIFY_SPLIT`, `OMK_VERIFY_GROUND` and `OMK_VERIFY_PATCH` - 22662 grid
  probes, 226 walker and 121 decor probes, 664 sweeps, all 0 mismatched
  against the linear scans, the moving layer 24 + 18 whole meshes; the frame
  and the player's end state identical to `OMK_MOVING_GRID=1`. Over Anekbah's
  754 moving walkable triangles and 20000 random probes, 0 answers differ.
* **Cost on the M3**: the per-frame layer 18.3 -> 3.3 us; a probe ~7% dearer
  (3.42 -> 3.67 ms for 20000), since a probe that falls in a group tests all
  of its triangles. The console's 6.9 ms is far more than this arithmetic on
  any reading (step 28's ~70x); what the new layer removes there that the
  M3 cannot show is the per-frame ALLOCATION - the grid's four vectors - and
  that is a guess until a log says.
* Green: `engine: probe grid`, `ground grid`, `patch index`, `airlock walk`,
  `shop door`, `tunnel door walk`, `walker falls`. `split grid` and `sweep
  grid` are the standing reds with the same values as before, 0 mismatches in
  their in-game halves (11322 probes; 692 sweeps).

### The 4K benchmark - 2026-10-01, ON AN M3

**The reader's yardstick (2026-10-01):** on the M3, run optimization tests at
`--res 3840x2160` and reach **30 fps or more**. A game that ran on 1999
machines has no reason to lag on an M3 at any size. At 4K the per-pixel stages
cost ~27x what they cost at 640x480, so the overheads that also slow the Vita
at 960x544 stand out clearly. The reader saw lag at 4K on Vulkan **with
`--enhance-all`**. The Vita's GLES build lags at its own resolution as well,
so the costs are not confined to one backend.

**Method.** A detached worktree at `626b74a`, built there so that no other
session's `build/` or uncommitted edits were involved (`make play
play-gles`). Each run used `--frames N --fps` and therefore never reached the
30 Hz pacer: the numbers are THROUGHPUT, and on this display Vulkan FIFO
vsync caps them at 120 (ProMotion). **The machine was HEAVILY LOADED** - load
average 22-50 from parallel sessions - so the milliseconds are an upper bound.
The ratios are what to quote. Street: `--save ../traces/save-appart.bin --area 0
--stand 1804,0,-6890,336 --nofmv`. Menu: a plain boot with `--nofmv`, which
leaves screen 29 asking.

Where the 3D is drawn at 4K: `vr->init(dispW, dispH)` makes the Vulkan
target 3840x2160. The swapchain stays the window's size (1512x867 points on
the built-in 3024x1964 panel), so the frame is drawn at 4K and only scaled
down when presented.

| run (M3, loaded) | fps | per frame (ms, mean of 60) |
|---|---|---|
| Vulkan, street, 640x480 | 120 (vsync) | sim+draw 2.6, present 5.7 |
| Vulkan, street, 4K | **120 (vsync)** | sim+draw 5.0, present 3.5 |
| GLES, street, 4K | **120 (vsync)** | sim+draw 1.5, present (swap) 6.8 |
| Vulkan, street, 4K, `--enhance-all` | **8.6** | sim+draw 41.4, **readback 74.6**, present 5.6; world submit 39.0 |
| Vulkan, street, 640x480, `--enhance-all` | 90-97 | sim+draw 5.8, readback 2.7 |
| Vulkan, street, 4K, `--enhance-all --ssaa 1` | **120 (vsync)** | sim+draw 5.7, present 2.6 |
| Vulkan, start menu (screen 29), 4K | **26** | `screens, hud` 28.5-30.1 |
| GLES, start menu (screen 29), 4K | **24** | `screens, hud` 29.4, texture upload 5.7, swap 6.0 |

**What it says - two costs, neither of which is the game's 3D:**

1. **SUPERSAMPLING (an enhancement) takes the frame off the GPU present path.**
   Everything else `--enhance-all` turns on (8x MSAA, trilinear, anisotropy
   16, mapped shadows, per-pixel lighting, unlimited distance) holds 120 at
   4K. `--ssaa` alone drops it to 8.6. `vulkanCanPresentWorld` refuses a
   supersampled frame (the log says `on the CPU: supersampling 60`). The
   whole oversized image is then read back and averaged ss x ss on the CPU,
   in a scalar loop in `VulkanRenderer`'s readback (`vkrender.cpp`, "the
   SUPERSAMPLE RESOLVE happens on the 8-bit side"), before being quantised
   and uploaded again. Lead (NOT built): resolve and dither on the GPU, the
   way `present.frag` already dithers the plain frame, so a supersampled
   frame can be presented directly. The original has no counterpart - it
   never supersampled.
   **DONE 2026-10-01 (`03c57c3`)**: `present.frag` does the resolve - the
   rounded mean of each ss x ss block, then the one dither - and the
   "supersampling" gate is gone. `OMK_VERIFY_GPU_PRESENT` finds it
   byte-identical to the CPU resolve at `--ssaa` 2 and 4 (0 pixels differ in
   60 frames). Truncating red's mean turns 60 of 60 frames red, 952966
   pixels. M3, 4K, `--enhance-all`, the same binary with
   `OMK_NO_GPU_PRESENT=1` as "before", load ~3: **9.0 -> 21.5 fps**, readback
   67.7 -> 0 ms. Still under 30: what remains is the GPU's own draw, 34 ms
   for 4x4 supersampling times 8x MSAA (~128 samples a pixel), plus 11 ms of
   present. Fewer samples when the two are stacked is the reader's call,
   because it changes what `--enhance-all` means.
2. **The interface is composed on the CPU at display resolution, on BOTH
   backends.** With screen 29 open, `sample` puts ~74% of the main thread's
   on-CPU samples in `ScreenComposer::draw`, then `MenuCloud::drawScaled`,
   `vulkanPresentSurface` and `blt`. At 4K every full-screen primitive (the
   tile map, the dim quad) writes 8.3 M pixels. This is step 4's "composing
   on the GPU is still open", and it is the same cost the Vita pays at
   960x544 on a CPU some 20x slower - so it plausibly explains the GLES lag
   on the console as well. Lead (NOT built): read how `I2D` blits scaled
   primitives in the original (the DirectDraw back end, `docs/UI.md`) and
   compose the 640x480 layer once, scaling it on the GPU.

**The opposite end - 320x240, to strip the per-pixel costs (same day, M3,
load 4-8, `bb21d5f`).** GLES, the same street, `--frames 1500`, with `sample`
running for 10 s in the middle. `sim+draw` is **1.3-1.6 ms a frame**. The
largest sections are audio 0.3, world submit 0.3 and pedestrians 0.2. The
`present, swap` 7 ms is the wait for vsync. Across all threads the process
was on the CPU for only 950 of ~80500 samples (~0.8 ms per 120 Hz frame);
567 of those were in omk code. Ranked by self time:

* **bodies ~23%**: `composePoseAt` 69, a `std::sort` of `pair<int,int>`
  inside it ~31, `meshAffines` 29. The sort runs on every pose, which makes
  it a candidate;
* **audio ~17%**: `AdpcmStereoStream::frame` 47 and `MusicPlayer::at`/`pull`
  48 - decoding on the fly;
* **particles ~7%**: `particleGeometry` 39;
* `GlesRenderer::submit` 24; the rest is spread thin (`main`'s inlined
  code 71, deduplicated symbols 98).

The M3 has ~22x headroom on this frame, and the ranking may not carry to the
console: an in-order Cortex-A9 punishes the float-heavy posing and the
branchy decoder differently. Step 28's console sections are what decide.

Side note: the GLES build's `--fps` line labels itself `software` (the
counter tests only `vkRen`). It is an instrument label, not a finding.

## What is NOT in scope

* The software renderer's speed. It is the reference and a comparison tool;
  a Vita build would never use it.
* Anything that changes a decision to save time - fewer shadow bones, a lower
  crowd density, a shorter clip distance. Those are options the game already
  has (rows 3, 6, 7), and a platform may DEFAULT them lower; the engine's
  answer for a given setting stays the engine's.
