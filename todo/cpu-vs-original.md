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
