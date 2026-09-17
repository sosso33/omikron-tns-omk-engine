# Handoff — the PS VITA PORT (`todo/optimization.md` step 7)

Written 2026-09-17, at the end of the session that finished the optimization
plan's steps 2-12 and closed step 7 with a decision. **Nothing here starts the
port**: it is what the next session needs to know before it does, and the
honest state of every number it would rest on.

The plan and the full record are [`optimization.md`](optimization.md) - step 7
is the decision section, steps 2-12 are what was done to the frame and what was
looked at and left alone. This file does not repeat them; it collects what
bears on the Vita specifically.

---

## 1. THE DECISION, and the one number nobody has

**A 30 fps Vita build of this port, doing this CPU work on one core, is not in
reach** - by an estimated factor of several, not a few percent.

Measured on the M1, capped at 30, the Anekbah street start, standing, main
thread only (`sample` 20 s, waits separated from work by each stack's leaf):

| | main thread working | work a frame |
|---|---|---|
| defaults, 640x480 | 18.0% | **6.0 ms** |
| defaults, 960x544 (the Vita's own screen) | 18.9% | 6.3 ms |
| `--enhance-all --density 4` | 33.3% | 11.1 ms, of which 6.9 is the 4x supersample's CPU resolve |

Resolution moves the GPU's work, not this thread's. The enhancements are not a
Vita build's business; **6.0 ms is the figure the decision rests on.**

Where those 6.0 ms go (largest leaves, ms a frame): the face-overlap walk
(`DepthTie`) 0.79, the crowd's per-vertex light 0.65, `main` itself 0.57,
`applyPose` 0.45, `composePose` 0.24, `sincos` from `rotateYaw` 0.23,
`memmove` 0.19, the moving collision grid 0.15.

**By binary, so the translation layer can be told from the engine:**

| | 640x480 | 960x544 | all enhancements |
|---|---|---|---|
| `omk-play` (the engine) | 4.51 ms (75%) | 4.69 (75%) | 9.78 (88%) |
| MoltenVK | 0.17 (3%) | 0.21 (3%) | 0.11 (1%) |
| Metal / AGX driver | 0.20 | 0.23 | 0.22 |
| IOKit | 0.07 | 0.08 | 0.06 |
| system libraries | 1.08 (18%) | 1.07 | 0.91 |

and the system-library time is the engine's own calls - `sincos` from
`rotateYaw` 0.23, `memmove` from geometry copies 0.12, `memchr`/`strlen`/
`memcmp` from `findMeshContaining` 0.09, sorting inside `DepthTie` 0.04.
**So MoltenVK is not a significant CPU cost**, and a vitaGL backend inherits
the 4.5 ms of engine work whatever it replaces the translation layer with.
(Caveat: this measures the CPU only. What MoltenVK costs inside the GPU, and
its shader compilation, is not in these numbers; Metal's own threads showed
~0.3 ms a frame.)

**Memory** (headless street run under `MallocStackLogging`, snapshot at 40 s):
**136.8 MB live heap**, 44876 allocations; physical footprint 206 MB, of which
~41 MB is the GPU driver's and 4 MB the profiler's. Largest sites: render
corners 18.2 MB, posed bodies' geometries 16.4, the music held whole 16.1,
shared texture pixels 12.6, collision soups 11.6, `main` 11.5, playing sounds
9.4, the two resident scene files 7.3, the depth tie's tables ~9.7. Against the
homebrew budget of 365 MB this is comfortable **before** vitaGL's own texture
copies.

**THE MISSING NUMBER.** How much slower a Vita core is than an M1 performance
core is **not measured and not sourced**. What is sourced: the Vita runs its
Cortex-A9 at 444 MHz (333 by default, 444 with Wi-Fi off), and the Cortex-A9 is
2.5 DMIPS/MHz, so ~1,110 DMIPS a core. No source found gives the M1's core in
the same unit, and Geekbench 5 - where the M1 is ~1700 single-core - does not
run on Cortex-A9 devices, so there is no ratio to quote. The decision is
therefore framed by the BREAK-EVEN: 6.0 ms fits 33.3 ms on a core up to ~5.5x
slower, with nothing left for the GL driver, audio or the interface. **A
device measurement replaces this whole paragraph and should be the first thing
the next session does.**

---

## 2. WHAT TO DO, in the order that removes the most measured work

The reader's own point, and it is the right frame: the original ran this on a
Pentium II, posing characters and lighting them per vertex on the CPU, and the
Dreamcast port ran it too. The work is not inherently too big for a Vita. What
the port ADDS is what to remove first.

1. **Remove work the original never did** (~1.5 ms of the 6.0):
   * the **face-overlap check** (`o3de/depthtie.*`) on posed bodies, ~1.0 ms a
     frame over 45 of them. It exists only because a Vulkan depth compare
     cannot reproduce the engine's strict test on a quantised z-buffer; a GL
     backend owns its depth format and may not need it at all. **Do not assume
     bodies never tie**: measured, `PSH_FN` has 3 coincident faces at rest
     (`tie_equiv`'s static losers), `HO1_FN` has 0. The rule has to be derived,
     not guessed;
   * ~~`findMeshContaining` searching by NAME every frame (0.09 ms): an index
     per model, built once~~ - **the index is BUILT and proven, and it is not
     wired up** (2026-09-17, `optimization.md` step 13). `omk::MeshNameIndex`
     answers in 31 ns against the scan's 924 on a crowd model, 0 mismatches in
     5130 calls against the scan transcribed verbatim, shown to fail four ways;
     `verify.py: engine: mesh name index`. The three call sites are in
     `play.cpp`, which another session held, so it has **no consumer** and the
     frame is unchanged - apply
     [`pending/vita-meshidx-playcpp.md`](pending/vita-meshidx-playcpp.md) and
     then measure, because the 0.09 ms is a profile attribution and has never
     been confirmed saved. Note also what this refutes: the cost is the NAME
     SCAN, not the ancestry walk this file's wording points at - replacing the
     walk is exactly equivalent and SLOWER (1145 ns against 852), because bone
     chains are shallow;
   * the per-frame allocations the tie's tables make (~10 MB across bodies) -
     **LOCALISED 2026-09-17** (`optimization.md` step 14): it is one group of
     the twenty-odd vectors, the CLAIMED KEYS, at 97% of the tie's bytes
     (Anekbah 2560 KB of 2652, a PSH_FN body 67.0 of 68.6), because a key
     copies the face's positions out at 36 bytes a face. Storing the face's
     corner INDEX instead and reading positions from `g.corners` is worth ~1.5
     MB on the set; `tie_mem` measures it and `tie_equiv` is the oracle. Not
     cut yet.
2. **Make the remaining work cheaper without changing a result**:
   * **share poses**: crowd bodies often play the same clip at the same frame
     on the same model - `composePose` once, used by all of them;
   * **skip a body whose pose and placement did not change** since last frame:
     that also skips its upload and its overlap walk;
   * `rotateYaw`'s `sincos` (0.23 ms) for a yaw that rarely changes.
3. **Use the other cores** - `src/platform/threads.h`, §3 below. Posing,
   lighting and the overlap walk are per body and independent; the script VM
   and the Session are not and must stay on one thread.
4. **Move per-vertex work to the GPU** - skinning and the crowd's light in a
   vertex shader, ~1.9 ms off the CPU plus most uploads. This is the one item
   that changes WHERE the answer is computed, so it needs its own exactness
   story (GPU float against CPU float, per platform) rather than this file's
   "same bytes" rule.
5. **The porting work itself**, not started and a separate plan: an OpenGL ES 2
   backend behind `src/o3de/renderer.h` through vitaGL; SDL2 for Vita or a
   frontend that does not need it; `std::filesystem` out of `platform/datafs`;
   the controls onto the four control schemes; 960x544 (free, measured); and
   the memory items - stream the music (16 MB), share or GPU-side the posed
   geometries (16 MB), textures on the GPU only.
6. **Measure on the device before any of 1-4.** One vitaGL spike running
   `composePose` + `applyPose` + `applyLights` over a street's worth of bodies
   gives the factor §1 could only frame, and says which of these are needed.

---

## 3. THE THREAD MANAGER - `src/platform/threads.{h,cpp}`, new and UNUSED

Written this session at the reader's request, ahead of item 3 above.

* **One class, three implementations in one file**, because the platforms do
  not agree on how a thread is made: `std::thread` with a persistent pool
  (desktop), the **Vita SDK** (`sceKernelCreateThread` / `sceKernelStartThread`
  / semaphores, explicit stack and priority, three cores for a game), and a
  build with **no threads at all**.
* **`OMK_THREADS`** picks it: 1 uses the platform's threads, 0 compiles a
  single-threaded build where `parallelFor` calls the body inline and no thread
  header is included. It defaults to 0 under Emscripten without pthreads (the
  WebGL case) and 1 elsewhere; `-DOMK_THREADS=0` forces it.
* **The rule that keeps it exact**: the only thing offered is a `parallelFor`
  over a half-open range cut into disjoint chunks, each writing its own slice.
  No ordering, no reduction, so a parallel run gives bit-identical results. A
  caller that must accumulate produces per-chunk results and merges them itself,
  in index order, on the calling thread.
* **`engine/tools/thread_probe.cpp`** is the check: the same workload inline and
  through the pool compared byte for byte, every grain from 1 item to the whole
  range, the chunk boundaries asserted to tile the range exactly once, empty and
  single-item ranges, and a NESTED `parallelFor` (which runs inline). Measured
  on this M1: 7 workers, 8 chunks, 0 mismatches, 26.1-26.4 ms against 54-55 ms
  inline (~2x on a per-item workload); the `OMK_THREADS=0` build gives the same
  bytes at the inline cost.
* **The probe earned itself immediately**: the first version deadlocked, because
  a nested `parallelFor` reused the pool's single call state. Fixed with a
  per-thread depth guard around the body wherever it runs - see `DepthGuard`.
* **THE VITA HALF HAS NEVER BEEN COMPILED OR RUN ON A DEVICE.** It is written
  from the SDK's documented shape and says so in its own comment: the first
  Vita build must check the function names, `kVitaPriority`, `kVitaStack` and
  the affinity mask against the SDK it links, and the per-call semaphore
  handshake against how that SDK schedules.
* **Nothing includes these files yet.** They compile clean (`-Wall -Wextra`) and
  add compile time to `make` and nothing else. The first caller should be one of
  item 2's per-body loops, with a check comparing its output against the
  single-threaded build.

---

## 4. TRAPS, measured the hard way this session

* **Measure CAPPED at 30** (`omk-play` with no `--frames`), never uncapped: the
  animations are 30 Hz with no interpolation, and an uncapped run measures the
  display's pacing as much as the code.
* **Machine load ruins a capped A/B.** Two runs of the SAME binary read 27% and
  35% CPU under load ~3-5 on one occasion; a quiet machine (load 1.4-3.1) gave
  32 / 33 / 32 for three runs. Always run old / new / old back to back and quote
  the spread; a single pair proves nothing. The same `004d28f` build read 22-28%
  in the morning and 33% in the evening - the machine's state, not the code.
* **The exactness pattern that caught every mistake**: keep the old version
  VERBATIM in a tool, drive both over adversarial inputs, compare every field,
  and then SHOW the check failing by mutating the new code. Every step of this
  plan that shipped has one (`tie_equiv`, `probe_grid`, `pose_equiv`,
  `present_probe`, `light_equiv`).
* **A mutation can be equivalent rather than caught**: green's rounding `+127`
  made `+128` changes nothing, because `63 v mod 255` is never 127. Red and
  blue's `31 v` does have solutions. If a mutation does not go red, prove it is
  equivalent before believing the check is blind - or find a discriminating one.
* **A probe's own parameters can hide a bug**: `present_probe` first placed its
  picture 4 rows down, and 4 - like the street's 0 and the letterbox's 64 - puts
  the picture row and the window row in the same dither cell, so a shader using
  the wrong row passed. It uses 3 now.
* **This tree is shared by several sessions.** The Makefile compiles
  `src/*/*.cpp` and `tools/*.cpp` by wildcard, so a new file is in everyone's
  build and a file that does not compile reads to them as "build failed" in an
  unrelated check. Stage by explicit path, never `git add .`, and read
  `git log origin/main..HEAD` IMMEDIATELY before a push - on this tree it goes
  stale in minutes.

---

## 5. WHAT IS OPEN

* **The device factor** (§1). Everything else in the decision is measured.
* **The bodies' overlap check** (`optimization.md` 10b): the exact improvement
  available - a larger hash table - is worth ~0.08 ms a frame and makes the big
  set slower; reusing a body's walk across poses could not be proven exact,
  because skinning and the rounding of street coordinates can both create and
  destroy coincident faces.
* **The crowd lighting** (step 12): the float-ramp table was exact and NO
  faster; the per-corner loop is the cost, not its divisions. Reverted.
* **Interface frames still round-trip through the CPU** (step 4b): a frame with
  a screen, the HUD, a subtitle or a fade is composed on the CPU because several
  of those passes READ the pixels under them. Only frames with nothing over the
  3D stay on the GPU.
* **The black-fade gate is conservative**: a cutscene's opening frames take the
  CPU path while the fade draws nothing (its bands are at grey 255). Gating on
  the block's own draw condition would put them on the GPU.
* **A 600-frame run of `--area 230 --scene-chunk 56`** takes a different path
  from a 240-frame one (no renderer line, no stats) and was not chased.
* **Six SLOW FRAMES on Jaunpur's streets, 69-78 ms each** (a play session,
  2026-09-17; the instrument is a `SLOW FRAME` line in the pacer, always on in
  a windowed run, so `--frames` runs never reach it). Two fall on a MUSIC TRACK
  SWITCH - the new stream is opened and started on the main thread, which is
  the obvious next hitch to chase and is nothing to do with the frame's mean
  cost. One is the depth tie's full walk on a newly resident SET
  (`SRest02.3DO`, 3075 triangles, 168 drops - see `optimization.md` step 14,
  and note it was first attributed to a posed body, which the backend's own log
  gate makes impossible). Three near x 13200 z 14000 are unattributed.
  **This is the shape §1's mean-cost figures cannot see**: 6.0 ms a frame
  standing still says nothing about a 78 ms frame when a set loads, and on a
  Vita a load hitch is likely to be worse than the mean, not better.

---

## 6. HOW TO REPRODUCE THE MEASUREMENTS

```
cd engine && make play
# capped, windowed, the street: let it run, then sample the main thread
build/omk-play "$OMK_DATA" ../tables --save ../traces/save-appart.bin \
    --area 0 --stand 1804,0,-6890,336 --vulkan --nofmv --fps
sample <pid> 20 -f /tmp/cap.txt      # 15 s in, so the start-up is not in it
# the Vita's screen, and the enhancements' worst case
    ... --res 960x544
    ... --enhance-all --density 4
# memory, headless, live heap by allocation site
SDL_VIDEODRIVER=dummy MallocStackLogging=1 build/omk-play ... --world-vulkan \
    --frames 100000 &
sleep 40; heap -s <pid>; footprint -p <pid>
```

The main thread's work is its samples minus the leaf frames that are waits
(`semaphore_*`, `mach_*`, `__psynch_cvwait`, `nanosleep`, `kevent`, `__select`,
`__ulock_wait`); at 30 fps, `work / total * 1000 / 30` is milliseconds a frame.
