# Where the frame's time goes, against the original

The reader's ask (2026-10-05), after `todo/ram-vs-original.md` took the
street from 72.7 to 40.1 MB: *"Now same thing with CPU/GPU use: look in the
previous capture what function are the longest to execute, compare them to
the code of original, and see how the duration could be shortened on the
port."*

## How

1. **An inventory by FUNCTION, on the paths a player takes.** The
   profiler's zones say which phase a millisecond is in; for "every
   function" the running viewer is SAMPLED as well (macOS `sample`, the
   whole call stack a millisecond, no instrumentation), and the two are read
   together.
2. **Per item, the original's mechanism**, read from the binary - what it
   computes per frame, per mesh, per vertex - and the port's, side by side.
3. **The cuts**, each shown to change no frame (or labelled where it is an
   enhancement) and held by a check.

## Step 1 - the inventory, DONE 2026-10-05

The street, Anekbah, CAPPED at 30 Hz (no `--frames`: the real clock), the
player walked by `--hold 0*45,k200*150,k200+54*200,k203*40,k200+54*200,
k200*150,0*60` - stand, walk, run, turn, run back, walk, stand, **3000 units
and no door** (the first path tried went through one into `AHALL09` at
frame 415: a reader heard it). M1 (`sysctl`), the engine at `8709c97`,
640x480. Every figure below is the main thread's.

**A trap met on the way, and fixed**: `OMK_NO_GPU_PRESENT=1`, which a GPU
measurement run sets to hide its window, ALSO turns the GPU present off -
every frame goes through `readback()` (`glReadPixels`), the 888 -> 565
dither and the upload back (`presentSurface`), the round trip `vita-port.md`
G6 removed. Profiled that way, GLES spent 2.7 ms a frame in `readback` and
Vulkan (`--world-vulkan` under the dummy driver) 1.4 ms in the dither and
SDL's software present - none of which a player pays. `OMK_HIDDEN_WINDOW=1`
(new) hides the window and leaves the present alone; the counters
(`OMK_GPU_PRESENT_STATS=1`) then show GLES keeping 810 of 810 frames on the
GPU and Vulkan 780 of 840.

| | software | Vulkan (direct present) | GLES (the Vita's path) |
|---|---|---|---|
| frame, median | 73.0 ms (13.7 fps) | 32.8 (capped) | 32.8 (capped) |
| the game's work a frame | ~73 | ~3.8 | ~2.3 |

**Software**: the frame IS the rasterizer. `raster: drawGeometry` 71.3 ms
of 73.0 - 66.7 calls a frame at ~1.07 ms each, sampled at 76% of the main
thread with everything inlined into it. The set reaches it already culled
by distance (594 mesh runs drawn, 1038 culled). Everything else together is
under 1.5 ms.

**The GPU paths**, in ms a frame (zones, with the sampled function inside):

| owner | Vulkan | GLES | what is in it |
|---|---|---|---|
| world submit (`world: mirror`, `drawWithMirror`) | 1.81 | 0.70 | the renderer's submit; on Vulkan **`vkWaitForFences` 1.9 ms** of waiting for the GPU inside the frame - CPU and GPU never overlap |
| `input: motion` | 0.41 | 0.30 | the moving set meshes' placement and patch |
| `world: draw lists` | 0.37 | 0.36 | the frame's batch lists |
| `world: staged` | 0.28 | 0.25 | the scripted bodies |
| `world: crowd` + `crowd: tick` | 0.39 | 0.22 | the walkers and the traffic |
| `session: frame` | 0.20 | 0.20 | the scripts, the zones |
| `pump` | 0.16 | 0.10 | SDL events |
| the profiler's own control poll (`prof::control`) | 0.37 | 0.36 | a file looked at every frame - measurement cost, absent from `make release` |
| `particleGeometry` | 0.22 | 0.16 | the effect quads |

On the M1 the GPU paths have 30 ms to spare, so these numbers are a
RANKING, not a problem: the targets that need it are the Vita (GLES) and the
G3 (the fixed-function GL1, not measured here), where the same work costs
many times more.

## What step 2 reads in the original for each

1. **The software rasterizer**, 97% of the software frame. The engine has
   none (D3D's device rasterizes), but what reaches the device is the
   engine's: `Render_SubmitMesh` (0x004951C0) transforms each mesh's
   VERTICES once into the shared pool (75000 x 48) and culls and clips
   there, and the faces index it - where the port transforms per CORNER,
   three a triangle (Anekbah: 139245 corners for 63079 distinct vertices,
   `ram-vs-original.md`). Also the per-mesh tests before submission, and
   what `Raster_DrawTriangles` hands the device per bucket.
2. **The GPU frame's pacing**: what the original waits on (D3D's `Flip`,
   `DrawPrimitive` per bucket) against the port's fence wait inside the
   frame.
3. **The draw lists** against `Render_FlushBuckets`' 0x4000 buckets.
4. **The moving meshes** against a scene program writing a node's matrix.
5. **The bodies and the crowd** against `sub_4947F0`'s per-frame pool and
   `Sliders_Tick`.
6. **The instruments' own costs** (`prof::control` every frame; the media
   line sending Vulkan frames to the CPU path) - the port's, no original
   to read.

## Step 2 - item by item against the original, DONE 2026-10-05

Four parallel readings of the decompilation (the software rasterizer, the
GPU frame's pacing, the draw lists and the moving meshes, the bodies / the
crowd / the scripts), and the decisive line of each re-read here before it
was written down. The largest finding was then MEASURED, not estimated.

**THE SOFTWARE RENDERER IS A HASH.** `drawGeometry` ends with an FNV hash of
the WHOLE framebuffer (`raster.cpp`, after the clip fan), on every one of
the frame's ~67 calls, and `SoftwareRenderer` keeps only the last
(`renderer.cpp`: `st_.hash = s.hash; // the last one wins`). Timed: 300
uncapped street frames take **23.0 s with it and 3.2 s without** (twice
each, M1), frames byte-identical - the loop is ~86% of the software frame.
The original has no such thing; the hash is this port's instrument for the
checks, which can ask for it once.

| item | the original | the port | what costs |
|---|---|---|---|
| software raster | per VERTEX transform into a pool, one divide a vertex; spans of 16 with one divide a run, 8.8 fixed-point u/v, one table load a pixel, 16-bit z (`ASSETS.md` 4b, read 2026-10-05) | per CORNER transform (139245 corners for 63079 vertices), bounding-box scan with three edge functions a pixel, 4 divides and 2 `%` a pixel, float z - and the frame hash per call | the hash (measured), then the box scan and the per-pixel divides |
| GPU pacing | `Flip(NULL, DDFLIP_WAIT)`, one back buffer, no wait in the frame: the GPU draws frame N while the CPU simulates N+1; one `DrawPrimitive` a non-empty bucket from user memory, render states shadow-cached | **Vulkan**: three serial submits a frame, `vkWaitForFences` right after the scene's (1.9 ms on the M1), a readback copy recorded EVERY frame although nothing reads it on a GPU frame, a one-shot command buffer + `vkQueueWaitIdle` for the present pass, no state cache in `submit`. **GLES**: no CPU wait, a state cache - but six clock reads a draw, not compiled out. **GL1** (the G3): the world read back and composited on the CPU every frame | the Vulkan waits; GL1's round trip (measured on emulated Tiger at ~5 -> 17 fps without it, `handoff-classic-mac.md`) |
| draw lists | rebuilt each frame face by face into 0x4000 head lists, a `memset` of 64 KB | rebuilt each frame into a fresh `std::vector` (capacity thrown away), a fresh `vis` per slot, a `stable_sort` with its buffer; on CPU-posed bodies `castBones` scans every posed corner for a log-only value | allocation and the corner scan |
| moving meshes | a script writes 3 floats and a 3x3 (`o3de_SetNodePos`, `sub_437160`); the draw transforms every visible node anyway; collision tests the mesh IN ITS FRAME (`Sweep_MeshTest`) | only GLES poses a moving mesh on the GPU (`posesBodies`); Vulkan, GL1 and software re-place ~30 meshes' 8229 corners a frame; every backend re-places their ~2730 collision triangles and re-grids | the CPU re-place |
| bodies | every live actor ticks (state, channel, motion); the hierarchy compose and the vertex work only for an object past the distance and frustum cull (`sub_48D3B0`) | the 25 staged bodies `composePose` BEFORE the cull (22 then culled), a crowd-model extra composes all four skeletons (76 meshes) for the one drawn; the idle (frame 0) recomposed every frame; the parent table re-sorted every call | compose work for the culled |
| crowd | posed only in the draw, after the cull, its LOD skeleton only | the same order; on the CPU paths three passes over each walker's CORNERS (`applyPose`, the placement, `applyLights`) and 8 per-corner arrays re-copied each frame | per-corner passes |
| particles | spawned unculled; dropped at submit by view depth (near / clip distance) | a quad for every particle, no depth gate; batches emitted keys x particles | small |
| scripts | every object of both slots, every frame | the same | nothing |
| the profiler | - | `tellState` writes `.state` EVERY frame (the frame number is in the line): 0.37 ms a frame of every capture, measured by sampling | the instrument's own cost |

## Step 3 - the cuts, proposed in three tiers

**A. Exact, small code** (frames byte-identical, each held by a check):
the frame hash once a frame and only when asked (software ~7x, measured);
`tellState` only on a change (0.37 ms off every capture); Vulkan: the
readback copy only on a CPU frame, the fence waited at the point of REUSE
(the next frame's buffer writes) rather than after the submit, the present
pass in the same submit - the GPU overlapping the next frame as the
original's flip chain lets it; GLES: the per-draw clock reads behind the
instrument that wants them; and the housekeeping the readings found - the
draw list's vector reused, the log-only corner scan gated, the staged
bodies composing only the drawn skeleton and caching the idle, the parent
table once a model, `applyPose`'s per-corner arrays copied only when the
rest changes, `%` -> `&` in the sampler and a per-row bound on the box scan.

**B. Exact, larger**: moving meshes and bodies posed on the GPU on Vulkan
and GL1 as GLES already does (`posesBodies`); GL1 presenting its world
directly with the interface drawn over it in GL (the G3's 5 -> 17 fps); the
Vulkan overlay path so a subtitle no longer sends a frame to the CPU; the
vertex transform once a vertex (it needs the corner -> vertex index - the
geometry item `ram-vs-original.md` deferred).

**C. Changes pixels**: a span rasterizer with a divide every N pixels (the
original's method, and a new reference picture); dropping the per-pixel
divides; the per-face far reject and the particles' depth gate (both
TOWARD the original); collision in mesh space as the original does it.

## Tier A DONE 2026-10-05 - the software renderer 7x, frames identical

| cut | measured |
|---|---|
| the frame hash out of `drawGeometry` (`surfaceHash()`, asked for once) | 300 uncapped street frames **23.0 -> 3.2 s**; capped, the software renderer HOLDS 30 fps (median 33.1 ms, raster 11.0 of it, where it ran at 13.7 fps) - `engine: raster cost` (0.125 ms a call; 1.013 with the hash put back) |
| the profiler's `.state` written on a change, the frame counter once a second | the 0.37 ms a captured frame `tellState` cost (sampled in step 1) - the instrument's own, so release builds never had it |
| Vulkan: the readback copy recorded only when a frame is read back | a 640x480x4 GPU copy a frame off the GPU path; within noise on the M1 |
| GLES: the six clock reads a draw compile out of release builds | ~1500 reads a frame on the Vita's release build; not measurable here |
| the shadows' log-only corner scan behind `OMK_SHADOWLOG`, the draw list's capacity kept, a staged body's idle pose composed once (keyed on the model and its name), the sampler's wrap by mask | small; frames identical |

Frames identical against the build before the work (software, Vulkan, a
walk). 39 checks green but one: `engine: profiler`, whose "textures over 5
MB" predated the palette textures (`ram-vs-original.md` tier C, 4.77 MB) -
re-pinned at 4 with the reason.

**Taken OUT of tier A, and why** (each was proposed there):
* **the Vulkan waits moved to the point of reuse and one submit a frame** -
  seven one-shot submits end in `vkQueueWaitIdle` (uploads, the present
  pass, the shadow pass), so the CPU/GPU overlap needs every one audited:
  not small. And Vulkan runs only on modern hosts (here 85% idle); the
  constrained targets are GLES (the Vita) and GL1 (the G3). -> tier B;
* **the per-row bound on the triangle scan** - exact only with a
  conservative interval proven against the float edge tests; the software
  renderer is no target's -> tier B;
* **the staged bodies composing only the drawn skeleton** - NOT exact: a
  staged body's shadows find their bones by the LAST name match
  (`shadowBonesFor`), which in a four-skeleton crowd model can be an
  undrawn skeleton's bone, so the mask could move a shadow;
* **`applyPose`'s per-corner arrays copied only when the rest changes** - the
  dirty lists in them are rewritten by the GLES upload, so skipping the copy
  needs a validity rule for each: a modest gain for a stale-data risk;
* the parent table cached a model, and the motion gather's strings - tens
  of microseconds.

## Tier B, 2026-10-05 - what was done, what waits, what was deferred

**DONE, exact, measured here:**
* **a body posed once a VERTEX, not once a corner** (`752099a`): a corner
  takes the posed position and normal of the first corner of its vertex,
  only when its mesh, rest position and rest normal are bitwise that
  corner's. Kay'l: 1626 corners on 272 vertices, 83% of the rotations gone;
  `engine: pose equivalence` green, frames identical. Unmeasurable on the M1
  (posing is ~0.1 ms of the crowd's zone); aimed at the G3, which poses every
  body on its one CPU.

**DONE, NOT YET RUN where it matters:**
* **the classic Mac's GL1 presents the world straight** (`a98ca45`): drawn at
  its letterbox place in the window's back buffer; a frame nothing covers is
  swapped as it stands, with the driver's dither (`GL_DITHER`) at the
  drawable's depth - **the reader's choice of 2026-10-05**, as the original's
  16-bit D3D device dithered; a frame with the interface over it blends it
  there (`C + world * M`, then the fade - the GLES overlay's law, the CPU
  folding key and mask into one RGBA band, one quad with `(GL_ONE,
  GL_SRC_ALPHA)`, and the fade a second untextured quad, `(GL_SRC_ALPHA,
  GL_ONE_MINUS_SRC_ALPHA)` - 2026-10-05, fade frames 25-31 -> 3-4 ms on the
  emulated Tiger). Only for a window the frame's size; `--gl1-composite`
  keeps the old path. The SDL host's GL1 is unchanged (`engine: gl1 backend`
  green). **Waiting on the tools disk** (`omk-devtools` on the external
  Crucial X8, not attached 2026-10-05) for the classic build and a Tiger run:
  the frame rate against `--gl1-composite`, the street, a subtitle or a
  screen over it (the overlay), and a fade.

**DEFERRED, and why** (each in the tier as proposed):
* **GL1 posing moving meshes through its own transform** - GL1 already
  transforms every vertex on the CPU, so the saving is one pass over ~8000
  corners, well under a millisecond on a G3, and folding the affine into its
  camera transform changes the rounding;
* **Vulkan: the waits at the point of reuse, one submit a frame, and the
  overlay path** - real, but only modern hosts run Vulkan, here 85% idle;
* **the per-row bound on the triangle scan** - the software reference only,
  and it needs a conservative-interval proof to stay exact;
* **the set's vertices transformed once** - the corner -> vertex index for
  the decor, which `ram-vs-original.md` deferred with its risk.

## Tier C, 2026-10-05 - the particles' depth gate, DONE (the reader's pick, for the Vita)

`Render_SubmitSprites` (0x004969C0) takes a particle's CENTRE into view
space and submits it only when `depth > flt_6A2BBC` (the camera's near
plane, `+0x144`) and `depth < dword_6A2B9C` (the clip distance - the
visible-set radius and the fog's end); no side planes. The port made every
particle a quad. Now `particleGeometry` gates it the same way - near the
renderer's own cut (`kNearCut`), far the clip distance in inches - except
on a frame after a mirror reflected (the engine submits the sprites once a
PASS, with that pass's camera; the port builds them once a frame).

Measured standing in the street: **88% of the particles left out** (~970 a
frame, 131 kept), the draw-list zone 0.09 -> 0.04 ms on the M1 - and the
PICTURE UNCHANGED, as it must be: a camera-facing quad has its four corners
at its centre's depth, so one behind the near plane was wholly clipped, and
one past the clip distance is fogged to black, which an additive or a
multiply particle draws as nothing. On the Vita that is seven times fewer
quads built on its CPU and filled on its GPU. `engine: particle gate` (the
builder's own counts, and the frame byte-identical with `OMK_NO_FX_GATE=1`);
16 particle, effect, mirror and GPU checks green.

The rest of tier C stays as proposed: the span rasterizer and the per-pixel
divides are the software renderer's alone; the per-face far reject is done
per mesh already and by the GPU per face; collision in mesh space is the
Vita's other candidate (the `input: motion` zone), to be chosen from a
console profile.

## Tier C, 2026-10-05 - the moving collision placed on demand, DONE (exact)

The reader chose this over collision in MESH SPACE (the original's way),
because an audit found ~12 readers of the collision soups that bypass the
grid - the camera's collision, the fight's walker, the shoot setup, a slider
ride's surface probe, the walker's own steep scans, the scene camera
instrument, the verify instruments - each of which mesh space would have had
to rewrite, and a miss being silent (a door the camera passes through).

**Now**: a moving mesh's placement is RECORDED each frame (`lazyMeshes`), and
its walkable and steep triangles are placed - the same rest, the same
`placePoints`, so the same bits - the first time a query reaches it:
* a grid query through its part: `movingList` / `gatherSplitIds` call
  `SplitSoupGrid::place` on a pending part they gather, whose extent is the
  rest box carried through the placement and padded a unit - a SUPERSET of
  where its triangles will be, so the answers do not move (step 38's
  argument);
* any LINEAR query through `omk::ensurePlaced`, which `floorUnder`,
  `surfaceUnder`, `soupInBox`, `sweepSphere`, `buildSoupGrid` and the
  single-grid probes all call first, and which places every pending mesh: no
  reader that bypasses the grid, today's or a later one, can read a stale
  triangle. The registry is cleared and refilled each frame (a soup vector
  that reallocated would leave a dead address), it says so loudly if it ever
  overflows, and a set change forgets every record (`rebuildWorld`, main
  thread; `prepareSet` may run on a job) behind a generation guard.

Measured, the street walk: **9896 placements recorded over 300 frames, 70
made - 99% gone**; the frame and the log identical to `OMK_EAGER_SOUPS=1`.
45 collision, door, lift, crate, slider, fight and camera checks green;
`engine: lazy collision`, with two mutations (everything placed every frame
- red; the grid's hook unset - `engine: tunnel door walk` red, the street
check not, which is why they run together).
