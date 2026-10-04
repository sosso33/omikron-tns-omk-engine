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

1. The probe layer, the capture file, `OMK_PROFILE`, and the viewer's frame
   phases and sections as zones. A check: the capture parses, the zones nest,
   and a frame is BYTE-IDENTICAL with profiling on and off.
2. The external tool: frame graph and zone tree from a capture.
3. Pause / step / resume through `omk.ctl`, and the paused frame's dump.
4. Memory by tag: the counting `operator new` (host and classic), tags at
   the large owners (`classic-mac-port-1999.md` 3b's table: corners, posed
   bodies, textures, collision, audio, scripts).
5. VRAM by tag in the GPU backends (Vulkan, GLES, GL1).
6. Zones through the engine's hot paths (session tick, scripts, actors,
   pose, render submit, raster) - the "every function" the ask names, as
   deep as a measurement shows it is worth.
7. `make RELEASE=1` and its check: no profiler symbol in the binary.

Nothing here is a reading of the original; it is this project's own
instrument (PORTING B6), and its one obligation is to change NOTHING the
game does - which step 1's check holds it to.
