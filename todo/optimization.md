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
| 1 | a clean BASELINE: the same street run on an idle machine, per-frame time and the sample, recorded here | open |
| 2 | a SPATIAL GRID over the walkable soup (H1, H3): built once per resident set, `floorUnder` / `surfaceUnder` walk only the cells under the probe | **reading done 2026-09-12**: the engine culls per-MESH bounding spheres, ~20x on Anekbah; port not started |
| 3 | the depth tie only where it can matter (H2) | open |
| 4 | the interface composited on the GPU in adventure mode (H4) | open |
| 5 | break down `main`'s own time (H5) and re-rank | open |
| 6 | the memory pass: where 236 MB goes, against a 365 MB Vita budget that vitaGL's textures also come out of | open |
| 7 | re-measure everything, and decide whether the Vita is in reach | open |

Each step ends in a commit and a report, per the working rhythm; the full
sweep follows the cadence in `todo/sweep-log.md`, not these steps.

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

The tie exists for the two SIDES of a shop sign (18 pairs in Anekbah,
`verify.py: engine: sign tie`), which are static set geometry. Options, to be
settled by reading which geometries have ever produced a dropped face: resolve
only geometries flagged static, or key the state on topology (index layout)
so a pose that moves vertices without changing faces does not reset it.
Check: `engine: sign tie` stays green and the per-frame dropped count is
unchanged on the street run; the `std::set` samples disappear.

### 4. The interface on the GPU

The viewport item's picture reaches the composer through `readback()`. Draw
the interface into the same frame on the GPU (the I2D layer is 16 layers of
7 primitives - quads and blits), or present the 3D view directly and draw the
interface over it. Check: a `--dump` of an adventure frame is byte-identical
before and after; `readback` disappears from the sample.

### 5. `main`'s own time

`main` is 16k lines; a sample's self time there is every inlined helper. Build
once with `-fno-inline` (or mark the suspects `noinline`) for a profiling run
only, re-rank, and add rows to the table above.

### 6. Memory

Measure, then cut, in the order the measurement says. Suspects from the
reading, none measured yet: scene files read whole (up to 7.8 MB,
`Sconcert.SCX`) with two areas resident, textures kept decoded at 32 bits,
`IAM\GAMES` (8 MB) read whole, the per-frame shadow/pose vectors reallocated.
The shipped data fitted a 32 MB machine, so nothing in it forces the current
figure.

### 7. The Vita decision

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
