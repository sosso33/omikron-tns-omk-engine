# Debugging tools - the profiler, the frame inspector, the memory map

The reader's ask (2026-10-04): *"create debugging tools, to pause the game and
analyze a frame, rendering time, call stack with execution duration for every
function, used ram/vram and for what, ... it could be an external tool to avoid
having it having an impact on the execution. Every debugging code could be
disabled and/or removed from the code using macro for release version."*

What it is for first: the classic Mac budget (64 MB, the goal -
`classic-mac-port-1999.md` 3b) is ~18 MB over in Anekbah's street, and the
frame is ~1 s under emulation. Both need to be ATTRIBUTED before they can be
cut: which code allocates what, which function takes the frame.

## What exists (and is folded in, not duplicated)

* `PlayState::mark` - 24 named SECTIONS of the frame, `spanned` - 9 SPANS, and
  the four PHASES (sim+draw, readback, compose, present), all printed as text
  every 60 frames. Flat totals, no nesting, viewer-only.
* `backends/classic/heapcount.*` - the exact live-block count by
  `operator new`, classic Mac only, untagged.
* `INSTRUMENTS=0` - the harness flags (`--money`, `--fight`...) compiled out.

## The design

* **The probe layer** - `src/platform/profile.h`, macros only:
  * `OMK_ZONE("name")` - a scope's begin and end, nested, into a FIXED
    per-frame event buffer (no allocation on the hot path; a full buffer
    drops and counts). Each frame becomes a call tree with durations.
  * `OMK_FRAME_END()` - closes the frame and hands it to the sink.
  * `OMK_MEM_TAG(tag)` - the allocation category for a scope; a counting
    `operator new` (a size header, so it works on every host) keeps live bytes
    and counts per tag.
  * `OMK_GPU_ALLOC(tag, bytes)` / `OMK_GPU_FREE` - what a renderer holds in
    VRAM, reported by the renderer (textures, buffers, targets).
  * The clock is the frontend's (`Frontend::perfCounter`), handed in once -
    `src/` stays behind the gateway.
* **`OMK_PROFILE=0` compiles every one of them to nothing** - and a release
  build (`make RELEASE=1`) is that plus `INSTRUMENTS=0`.
* **The game only WRITES** - frames stream to a capture file (`omk.prof`,
  binary, little-endian, self-describing). A file is the one transport every
  target has, Mac OS 9 included. Cost in the game: copying a buffer per frame.
* **Control by a file, read once a frame** - `omk.ctl`: `pause`, `step N`,
  `resume`, `snapshot`. The same on every target; the tool writes it.
* **The tool is EXTERNAL** - `tools/omkprof.py` and a page, like the web
  viewers: the frame-time graph; a frame's zone tree with each zone's own and
  total time; memory by tag over time and at the frame; VRAM by tag; pause /
  step / resume buttons. It reads a capture live (tailing) or after the run.

## Steps

1. ~~The probe layer, the capture file, `OMK_PROFILE`, and the viewer's
   frame phases and sections as zones~~ - **DONE 2026-10-04** (`18ae78c`):
   `src/platform/profile.*`, `omk-play --profile <file>`, `tools/omkprof.py`
   (summary, `--frame N`, `--slowest K`), `verify.py: engine: profiler`
   (shown to fail both ways). The viewer's marks turned out to be a
   different thing from zones - a flat partition that CROSSES the phases -
   so they are written as SECTIONS (kind 1) and summed beside the tree, not
   in it. First reading, the street on an M1 (corrected from "M3" 2026-10-04: `sysctl` says M1), software: 77 of 78 ms is the
   `world` phase, with no zone inside it yet - step 6's first target.
2. ~~The external tool: frame graph and zone tree from a capture~~ -
   **DONE 2026-10-04** (`4167c3e`): `tools/omkprof.py <capture> --serve
   PORT` and `tools/omkprof.html` - the frame graph (30/60 fps lines, slow
   frames marked; click or arrow keys), the frame's call tree (total, self,
   share of the frame, folding), its sections, the capture's zone table;
   LIVE, the capture re-read incrementally as the game writes it.
   `verify.py: profiler page` writes a capture in Python to the documented
   format, queries the server and runs the page's own script under node
   (`tools/profcheck.js`) - which found the page doubling its frame list
   when two polls overlapped.
3. ~~Pause / step / resume through `omk.ctl`, and the paused frame's
   dump~~ - **DONE 2026-10-04** (`f79c425`): three files beside the capture
   (`.ctl` the tool's command, `.state` and `.snap` the game's answer); held,
   the game pumps its window and presents its last frame. The page's
   buttons, the paused frame's picture, `omkprof.py --ctl CMD [N]`.
   `engine: profiler control`. A first version wrote no snapshot while
   paused - a held game never steps, and the snapshot was written after a
   step. **And the release build was brought forward from step 7, at the
   reader's word** ("make all the debugging code easily removable in release
   builds"): `make release` (`OMK_PROFILE=0`, `INSTRUMENTS=0`, its own object
   tree, its flags stamped) and `engine: release build` - no profiler symbol,
   the frame byte-identical. From here every debugging addition goes inside
   `#if OMK_PROFILE` and that check is run with it. Left in release: the
   instruments' FLAG NAMES, parsed and stubbed (`playoptions.cpp`).
4. ~~Memory by tag~~ - **DONE 2026-10-04** (`2b47a8a`): the profiler's
   `operator new`/`delete` in every profiling build (a 16-byte header: size
   and category), `OMK_MEM_TAG(name)`, and every open ZONE names the
   category otherwise - so an untagged block still says what made it. A MEM
   chunk per frame: live bytes and blocks per category, the frame's peak.
   Tags at textures, geometry, collision (+ grids), sounds, music, archives.
   The page: a memory graph (live, the frame's peak, the 64 MB line) and the
   frame's categories. The classic `heapcount` now reads these totals (one
   counter); the Vita's own `operator new` is the release build's.
   **The street on an M1 (corrected from "M3" 2026-10-04: `sysctl` says M1) (64-bit)**: 69.7 MB live - textures 14.1, geometry
   12.0, collision 7.2, the area load (`input`) 11.9, the `world` phase 8.8
   (both untagged yet: step 6's zones will split them). **And a lead**: with
   frees uncounted (the check's mutation), geometry reached 49 MB in 30
   frames - about 1.2 MB of geometry is ALLOCATED AND FREED every frame,
   churn the live figure never shows. A per-frame "allocated" counter would
   show it directly; noted for step 6 or the optimisation work.
5. ~~VRAM by tag in the GPU backends~~ - **DONE 2026-10-04** (`76d62c6`):
   `OMK_GPU_ALLOC(tag, key, bytes)` / `OMK_GPU_FREE(key)`, reported by the
   renderer - Vulkan at every `vkAllocateMemory`/`vkFreeMemory` (a buffer's
   category from its usage flags), GLES and GL1 at every `glTexImage2D` /
   `glBufferData` / `glRenderbufferStorage` and delete. The street on an M1:
   Vulkan 26.4 MB (textures 9.8 in 41 atlases, vertex buffers 8.5, shadow
   map 4.0, targets 2.9, readback 1.2); GLES 18.6 MB. **The cross-check**:
   GLES's 41 atlases at 9.75 MB (width x height x 4) against Vulkan's 9.83
   (the device's own sizes) - two accountings 1% apart. And seen through the
   check's mutation: the renderer replaces some 30 texture images over the
   run, which correct accounting absorbs. `engine: profiler gpu`.
6. ~~Zones through the engine's hot paths~~ - **DONE 2026-10-04**
   (`be63d3e`): the software renderer (begin, submit, `drawGeometry`), the
   session (frame, area load, tick load, slot load), the interpreter's run,
   the crowd's tick, the viewer's set loading, and all 31 parts of the
   frame's phases; each zone also names the memory allocated inside it.
   Zones and categories are the MAIN thread's (on the Vita a `thread_local`
   is shared by the kernel threads - `actor/pose.cpp`). **What it showed, the
   street, software, M1**: the world phase is 77 renderer submits at ~1 ms
   each in `drawGeometry` - the frame IS the rasterizer - and `submit` copies
   each batch's corners into a new `Geometry` (an allocation per batch, and
   likely the geometry churn step 4 saw). Memory with owners: scripted
   motion 9.2 MB, sounds 2.8, the crowd 1.9, staged bodies 1.4, draw lists
   1.4, set loading 3.4; 2.2% left without one. Deeper than this (inside
   `drawGeometry`, per triangle) costs more than it tells: a sampling
   profiler is the tool there. Was: "Zones through the engine's hot paths
   (session tick, scripts, actors, pose, render submit, raster) - the "every
   function" the ask names, as
   deep as a measurement shows it is worth.
7. ~~`make RELEASE=1` and its check~~ - done in step 3 (`make release`),
   and **for the classic Mac and the Vita 2026-10-04** (`eaa6c55`): `make
   classic-release` (no profiler, no `heapcount`) and `make vita-release`
   (no profiler, instruments off), each scanned by `engine: release build`
   where its toolchain is present.

## Done - what there is, in one place

| what | where |
|---|---|
| record a run | `omk-play ... --profile run.prof` |
| read it | `python3 tools/omkprof.py run.prof` (summary), `--frame N`, `--slowest K` |
| the page | `python3 tools/omkprof.py run.prof --serve 8753`: frame graph, call tree, sections, memory and GPU memory by category, pause / step / resume / snapshot |
| steer a game from a shell | `python3 tools/omkprof.py run.prof --ctl pause` (`resume`, `step N`, `snapshot`) |
| time a scope | `OMK_ZONE("name")` - also names the memory allocated in it |
| name memory | `OMK_MEM_TAG("name")` |
| report GPU memory | `OMK_GPU_ALLOC(tag, key, bytes)` / `OMK_GPU_FREE(key)` |
| build without any of it | `make release`, `make classic-release`, `make vita-release` (`OMK_PROFILE=0`) |
| the checks | `engine: profiler`, `profiler control`, `profiler gpu`, `release build`, `profiler page` |

What it already found, for the work it was built for: the street's software
frame is the rasterizer (77 submits, ~1 ms each); `submit` copies each
batch's corners into a new `Geometry`; ~1.2 MB of geometry churns every
frame; and the street's 69.7 MB (64-bit, M1) by owner - textures 14.1,
geometry 12.0, scripted motion 9.2, collision 7.2, set loading 3.4,
sounds 2.8 - the list the 64 MB classic budget will be cut from.

Nothing here is a reading of the original; it is this project's own
instrument (PORTING B6), and its one obligation is to change NOTHING the
game does - which step 1's check holds it to.

## The first full capture - the street, 2026-10-04 (M1, `sysctl`)

Capped runs (no `--frames`: the real clock and the 30 Hz cap), headless, the
street start in Anekbah, the player walked by `--hold` (stand 1.5 s, walk,
turn left, run, turn right, walk, stand): **A** the software renderer (95 s,
709 frames), **B** the Vulkan world renderer (50 s, 1343 frames), and **C**
standing still with Vulkan for 3 minutes (5537 frames). "Work" is a frame
minus the cap's sleep (`present`'s own time).

| segment | A median ms | A draws | B work ms | RAM MB (B) |
|---|---|---|---|---|
| stand | 76.7 | 77 | 4.0 | 72.2 |
| walk | 65.5 | 66 | 4.3 | 74.8 |
| run | 180.5 | 192 | 4.9 | 77.8 |
| walk 2 | 155.0 | 162 | 4.5 | 79.2 |

* **The software frame is the rasterizer and nothing else**: ~0.94 ms per
  `drawGeometry` call, and the calls follow the view - 66 walking the quiet
  end, 192 running into the busy part. Everything else in the frame is under
  1 ms together. 6.7 fps median over the walk.
* **Vulkan holds 30 fps with the CPU at 4-5 ms of the 33** (14%), worst
  7.3 ms after the first frames (frame 1: 19.8 ms).
* **The setup is 7.5 s, and 5.0 of it is the original's**: the splash is
  `Sleep(0x1388)`, five seconds, reproduced. The rest: reopening the audio
  device at 22050 Hz (0.1 to 2.4 s - it varies between runs), the session
  and the area 20 ms, the bodies 11 ms.
* **RAM grows 57 -> 82 MB along the walk (64-bit)** - the standing figure
  of step 4 (69.7) understated the street. By when and what:
  * frame 1, once: the scripted motion 9.2 MB, the input bindings 1.5 MB;
  * **the music stream buffer, 5.4 MB, of which ~5 MB is waste**:
    `HostMixer` appends 176 KB/s (22050 Hz stereo floats) and compacts only
    when its read head passes `1 << 20` floats - 4 MB of already-played
    music kept, and the vector's doubling takes it to 5.4 MB (the
    profiler shows the doublings: +0.34, 0.67, 1.35, 2.69 MB at frames 28,
    88, 208, 449). The queue it serves is one second long. The same mixer
    runs on the classic Mac - ~5 MB of the 64 MB budget for nothing;
  * **the crowd's per-model caches, ~9.4 MB after 3 minutes and still
    filling** (C: +2.3, 1.4, 1.2, 0.7, 0.4, 0.2, 0.5, 0.2 MB per 20 s) - a
    cache, not a leak: each new walker model brings its rest geometry and
    LOD cuts (`lodRestFor`, `pedLodTracks`...), more rarely as the pool has
    shown them all. GPU memory follows the same curve, 23.8 -> 30.9 MB.
  After 3 minutes standing: 88 MB RAM, 31 MB GPU.
* **For the 64 MB classic budget** (measured on 32-bit at ~82 MB standing):
  the two cheapest cuts are now named - the music buffer (compact when the
  played part exceeds the queue: ~5 MB back) and a bound on the crowd's
  model caches (~9 MB of growth over a session) - before any of the 3b
  table's structural work.
