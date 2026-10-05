# Meta Quest - a VR prototype, planned and not started

Asked 2026-10-04..06, in one conversation: a Meta Quest port - on this code
or by moving the VM and the file readers to Unity; what performance to
expect; how long a first test version would take; and a preload mode that
prepares nearby interiors ahead of time. **Nothing is built.** This file is
the record of what was decided and why, so the work can start from it.

## 1. The decision: this code, not a Unity rewrite

* **The VM and the readers are a third of the engine.** On 2026-10-04
  `engine/src`: `formats/` ~4.4k lines, `script/` ~14.6k - beside `actor/`
  ~15.3k (the .CTL channel, the walker, fight, shoot, the crowd), `ui/`
  ~12.9k, `o3de/` ~7.4k, and ~22k in the SDL host, which still holds real
  orchestration (staging, transitions, `playframe_world_staged.cpp`'s loop).
  A Unity port that stopped at the VM would run scripts in a world that does
  not move, fight, talk or draw as the game does.
* **The evidence stays with the C++.** The ~485 checks, the golden traces, the
  DB byte-identical to `tools/sim` and every played confirmation are about
  this code. A C# port starts from zero proven, and would have to be kept in
  step with every later fix.
* **The seams are already there.** Quest is Android/ARM with GLES and Vulkan,
  and both backends exist; the Vita port proves the code on far weaker ARM.
  `renderer.h` is decision-level, so stereo is the same submitted list drawn
  per eye (or once with Vulkan multiview). Game code reaches the host only
  through `omk::Frontend`, so OpenXR is one more frontend beside `sdlfront`
  and `carbonfront`.
* What Unity would buy (the Meta XR SDK's hand tracking, passthrough, store
  packaging) can be had later by building the engine as a native plugin with
  Unity owning only the XR session. Not for the prototype.

## 2. Performance - estimated, and one proxy measured

No Quest has run OMK. The estimate scales measured frames.

**The proxy, 2026-10-05, M1** (`sysctl`: Apple M1): the existing
`build/omk-play-gles` (built 2026-10-04 20:53, not rebuilt because another
session had uncommitted edits in `backends/sdl/`), the Anekbah street start
(`--save ../traces/save-appart.bin --area 0 --stand 1804,0,-6890,336`),
`--framerate 60 --res 800x600`, `OMK_NO_GPU_PRESENT=1`,
`SDL_AUDIODRIVER=dummy`, 60 s each, ended by SIGINT. The E-core run is
`taskpolicy -c background`, which on Apple silicon keeps the process on the
efficiency cores. Means of 60 frames, from the viewer's `phases` / `gles`
lines:

| | P-cores (3.2 GHz) | E-cores only (~2 GHz) |
|---|---|---|
| sim+draw (the engine's work) | **0.9-1.3 ms** | **1.9-2.5 ms** (6.0 in the loading window) |
| GL draws | 0.2 ms, 71 draws | 0.5 ms, 79 draws |
| readback (glReadPixels + to-565 + upload) | ~2.2 ms | ~4 ms |
| swap wait | 7-13 ms | **~95 ms** |
| frames in 60 s | ~3360 | ~540 |

* **sim+draw is the row that transfers.** A Quest 2/3 core is at least an M1
  E-core per thread, so ~1.5-2.5 ms a frame plus a fraction of a ms for the
  second eye, against 11.1 ms at 90 Hz and 8.3 at 120. The CPU is not the
  limit on either headset.
* **The E-core ~9 fps is the swap, not the CPU.** Background QoS appears to
  deprioritise the GPU or compositor work too; not attributed further.
* **The readback exists only because the window was hidden**; a headset
  renders into its eye swapchains.
* One hitch on the P-cores: frames 1275-1276, 66 and 41 ms of work, standing
  on the street, not attributed.
* The `SLOW FRAME` line says "the budget is 33" at a 60 fps cap; the
  threshold is two periods, so the label is stale and the test is right.
* Not covered by the proxy: the Quest's GPU driver, its thermal levels, the
  OpenXR compositor.

**The 60 fps mode matters here**: the engine is variable-step (§1 of
`todo/sixty-fps.md`), so it runs at 72/90/120 Hz as it is, and the poses
smoothed between keys are what keeps bodies from stepping at 30 Hz against a
head-tracked view.

**The VR risk is the load spike, not the mean.** In a headset the compositor
keeps the head smooth but the world freezes. §4 is the answer.

## 3. The first test version - about 4-6 working days

Scope, as the reader asked:

* **Every authored camera** (conversations, cutscenes, the slider ride,
  fights) becomes the headset's ORIGIN: `view = authored_camera x
  headset_local_pose`. Its fov and the letterbox are dropped.
* **Adventure mode alone** becomes first person: the eye at Kay'l's head
  (shoot mode's first-person view already exists), his head hidden.

| # | step | estimate |
|---|---|---|
| 1 | Android/Quest build: NDK (not installed on the M1 on 2026-10-05; the SDK and `adb` are) + CMake, SDL's Android glue, `gamedata` pushed to the device, controllers mapped onto the engine's input word as the Vita pad was | 1 day |
| 2 | An OpenXR frontend: session, one swapchain per eye, the GLES backend rendering into the eye's framebuffer, OpenXR's frame wait driving the loop instead of the 30/60 pacer | 1-2 days |
| 3 | A per-eye `View`: an optional eye pose and an asymmetric frustum beside `RCamera` | 0.5 day |
| 4 | The camera rule above, one function for every mode, with adventure's first-person override | 1 day |
| 5 | The 640x480 interface (menus, subtitles, reply choices) as an OpenXR QUAD layer in front of the player | 0.5-1 day |

The rest of the range is debugging on the device (`logcat`, Adreno's GLSL
ES, the eye framebuffer instead of the window). Calibration: the Vita port
went from its first commit to a VitaSDK build, Vita3K and console frame times
in one day (2026-09-18).

**Decisions, each a flag in the test build:**

* the authored camera's roll and pitch - keep only position and heading (the
  horizon stays level; recommended) or the full orientation (faithful, and
  the one that makes people sick);
* whether a cut re-centres the headset origin at every shot or only on
  entering a cutscene;
* adventure's turn: the headset's yaw plus a snap turn on the stick; what
  near the eye is hidden.

**Traps known before starting:**

* OpenXR works in METRES and the game's unit is the INCH: the eye separation
  and every head offset need x39.37.
* The game is Y-down and OpenXR Y-up, and the conversion is a REFLECTION
  (CLAUDE.md §5): it flips the sense of every rotation, so an authored roll
  composed with the head's rotation comes out mirrored if it is converted only
  once.

**Not in the first version:** controller pointing in menus, comfort options
(vignette, teleport), anything past the controllers as a gamepad.

## 4. The preload mode - interiors prepared ahead, on a thread

An ENHANCEMENT: off by default, a flag and an `[Enhancements]` line, on for
the VR build or a machine with RAM to spare. The 64 MB classic-Mac goal is
untouched, and with `OMK_THREADS 0` it does nothing.

**What exists** (`todo/optimization.md` steps 31 and 35): a set is already
prepared on a `BackgroundJob` (`platform/threads.h`) once the Session asks
for it; `Session::setLoadGate` holds the load until it is ready while frames
keep drawing (0.0 ms waited under a 1.5 s artificial delay on the M3); the
default path is byte-identical to `OMK_SYNC_SETS=1`. A conversation's next
lines are read and decoded ahead the same way.

**What still lands on the arrival frames** (the Vita log of 2026-09-30): the
Session's arrival cases, model loads mostly (1530 ms on the console); the
world rebuild's soups and grids (139 ms there, 1.1-1.3 on the M3); `props,
guns` 442; `screens` 297; the texture uploads and the tie bake.

**The radius: 25 m** (984 inches), chosen 2026-10-06 against the clips' own
speeds (`docs/ASSETS.md`, `H1AVNT`): walk 2.1 m/s, run 5.6 m/s; a slider in
traffic is capped near 14.7 m/s, but a door needs a dismount first. 25 m is
~4.5 s of warning running and ~12 s walking, against an estimated 0.1-0.3 s
to prepare one interior on a Quest - Anekbah, the largest city set, prepared
in 26 ms warm and 74 ms cold on the M1 on 2026-10-05, and in 1292 ms on the
Vita. 50 m was asked about and rejected: the lead time was not needed and
the ground covered, so the number of interiors held, is four times larger.

The mode:

1. **Which interiors are near.** Every door is a trigger zone whose script
   loads an area; a one-off scan gives, per city, door position -> target
   area -> its `.3DO` set and its characters. Lifts give several targets from
   one zone.
2. **Prepare at 25 m, release beyond ~40 m** - the gap stops a player on the
   edge from loading and dropping the same interior. Nearest first, on the
   worker thread: set, textures, soups and grids, character models.
3. **GPU uploads spread over the walk**, a few textures a frame on the render
   thread (or a shared EGL context on the Quest).
4. **The arrival takes the cached result** the way it takes a thread-prepared
   set today, so the gate never holds.
5. **Transitions no door is near** (lifts, scripted moves, cutscenes): with
   RAM to spare, also prepare every target the current area's zones and
   scripts can reach, after the doors inside 25 m.

**What must not move:**

* **The game logic.** Startup scripts, the DB and the zone lifecycle run on
  the main thread at the original's moment; only file and data preparation
  moves ahead.
* **The texture cache's behaviour.** The original keeps TWO sets resident and
  binds 58 slots by texture name alone, so a location draws differently by
  where you came from (Anekbah's signs, `docs/ASSETS.md` 4b). A preloaded set
  is decoded pixels in a cache, never RESIDENT in that sense: the binding and
  any substitution are decided at the real load against the two sets the
  original would hold. The byte-identity check against `OMK_SYNC_SETS=1` is
  what would catch a breach.

**Memory, estimated:** the street is ~41 MB live after the 2026-10-05 cuts
(`todo/ram-vs-original.md`); an interior with its characters perhaps a few to
~10 MB - a guess; `omkprof.py --sites` gives the real figure. Twenty around a
busy street is ~100-200 MB against several GB on a Quest 2 or 3.

**The first step**: the zone scan, counting door targets per city within
25 m of the streets. That number decides whether step 5's "everything
reachable" fits a budget. About 2-4 days for the whole mode with its checks.
