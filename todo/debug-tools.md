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
5. VRAM by tag in the GPU backends (Vulkan, GLES, GL1).
6. Zones through the engine's hot paths (session tick, scripts, actors,
   pose, render submit, raster) - the "every function" the ask names, as
   deep as a measurement shows it is worth.
7. ~~`make RELEASE=1` and its check~~ - done in step 3 (`make release`);
   left: the same for the classic Mac and Vita builds.

Nothing here is a reading of the original; it is this project's own
instrument (PORTING B6), and its one obligation is to change NOTHING the
game does - which step 1's check holds it to.
