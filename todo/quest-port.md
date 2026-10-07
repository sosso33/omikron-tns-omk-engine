# Meta Quest - a VR prototype, steps 1-3 of 7 done

**State and recipes: [`handoff-quest-port.md`](handoff-quest-port.md).** The
paragraph below is the record as first written.

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
  per eye, or once with MULTIVIEW - which GLES has too (`GL_OVR_multiview2`,
  what Meta's Unity single-pass uses on GLES; this file first said
  Vulkan-only, and the reader corrected it). The catch is ours:
  `glesrender.cpp` is GLES 2.0 with `#version 100` shaders (for the Vita and
  WebGL), and multiview needs a GLES 3 context and `#version 300 es` - a
  mechanical third shader prelude (`in`/`out`, `texture`, a declared output). Game code reaches the host only
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

(The estimate of 2026-10-05. §3b's answers add controller aim, look-relative
movement, the fight camera and the keyboard; §5 is the plan that replaces
this table, at 6-8 days.)

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
(vignette, teleport). (It also said "anything past the controllers as a
gamepad" until §3b: shoot mode's aim is now the controller's pose.)

## 3b. The reader's answers, 2026-10-06

1. **Quest 2 is the minimum.** Budgets are set by the XR2 Gen 1.
2. **GLES first**, two passes (one per eye) to bring it up; multiview on GLES
   3 is the first optimisation after, not Vulkan.
3. **The data is SIDELOADED**, the player's own copy pushed with `adb` into
   the app's own folder - never in the APK. A sideloaded test build, which is
   also what a public GPL repo can carry.
4. **Seated first**, with the reference space behind ONE seam (the eye height
   and the floor origin chosen in one place), so a standing option is a
   setting later and not a rewrite.
5. **What lies outside the authored frame** (set edges, empty rooms, the
   T-posed actors parked below the floor until their scene) is SHOWN in the
   prototype; playing it decides whether to black it out.
6. **Typing a name uses the system keyboard.** To confirm on the device first:
   in an immersive native app the Android IME does not appear by itself - the
   ways are the `oculus.software.overlay_keyboard` manifest feature (what
   Unity's "Requires System Keyboard" adds) or `XR_META_virtual_keyboard`,
   whose model the app has to draw. Not yet read which one a NativeActivity
   gets for free.
7. **Movement is relative to where the player LOOKS** (the head's yaw), not
   the body's facing.
8. **Shoot mode aims with the CONTROLLER**, not the head ("aiming with the
   head will lead to headache in two minutes"). The aim ray is the
   controller's pose; shoot mode's own first-person aim
   (`engine/src/actor/player.h`, "FIRST-PERSON AIM", the body turned by the
   mouse) is where it plugs in - not yet read how far aim and view can part.
9. **Fights cannot be first person as they are.** The base is the port's
   flat-screen fight camera as the headset's origin, WITHOUT its zoom in and
   out, and with its moves slowed.

Also agreed: the sound listener follows the headset; mirrors (a whole second
scene pass, so four with two eyes) are measured in the one room that has one;
the near plane needs nothing (`kNearCut` = 2 inches, ~5 cm, `raster.h`).

**And the iteration loop**: macOS has no OpenXR runtime, so a desktop FAKE
HEADSET in `omk-play` - two eyes side by side, the head on the mouse - lets
the camera rule, the stereo and the interface panel be built on the Mac and
checked headless by `verify.py`; the device is then for what only it shows.

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

## 5. The first prototype - the implementation plan (2026-10-06)

Six steps of work and one of play, each ending in a commit and a report.
The first three run on the Mac alone, through a fake headset, so they are
built and checked before any device is involved; the Quest arrives at step 4.
About **6-8 working days**, more than §3's 4-6 because §3b added controller
aim, look-relative movement, the fight camera and the keyboard.

**Three rules hold every step:**

* **The head is a RENDER transform.** Everything that is game logic reads the
  AUTHORED camera, kept as it is at the end of `PlayState::worldCamera()`
  (`playframe_world_scene.cpp`); only the drawing, the culling and the
  sound listener take the head-composed one. A head turn must never change
  what the game decides. The exceptions are deliberate and named: adventure
  mode's first-person view and movement (§3b 7) and shoot mode's aim (§3b 8).
* **Everything VR is behind the gateway.** Game code reaches the host only
  through `omk::Frontend` (`platform/frontend.h`); the head and the
  controllers arrive there, and no OpenXR header leaves the new frontend -
  as no SDL header leaves `sdlfront.*` (`verify.py: engine: frontend
  gateway`).
* **Off unless asked, and the flat game unchanged.** With no head pose the
  frame is byte-identical to today's; every check below asserts that first.
* **A VR option ADDS a path; it never removes the original's** (the reader,
  2026-10-06: "the quest options should not remove the original behaviour
  from the code"). The level horizon, the recentring, the first-person
  adventure view, look-relative movement, the frozen and slowed fight
  camera, controller aim, the host-paced loop: each is a second path chosen
  by its option, beside the original one, which stays in the code, stays the
  default and stays selectable on every build - the Quest build included.
  None of them rewrites `worldCamera()`'s branches, `Fight_TickCamera`'s
  port, the walker's input or the pacer in place; each wraps or follows them.
* **VR code lives in its OWN files** (the reader, 2026-10-06: keep it in
  specific files "instead of making the current ones heavier"). The viewer's
  split (`todo/play-split.md`: `playsetup_<section>`, `playframe_<phase>`,
  state as `PlayState` members) is what makes this cheap:
  * `engine/src/vr/` - the pure, platform-free half: `xrspace.*` (units, the
    Y-down reflection, the per-eye composition), `vrcamera.*` (the camera
    rules, the recentring), `vrfight.*` (the frozen and slowed fight camera
    as a wrapper over `FightCamera`), `vrmove.*` (the stick in the head's yaw
    into the existing input bits), `vraim.*` (a controller ray into the
    arm's aim angles);
  * `engine/backends/sdl/playvr_<part>.cpp` - the viewer's half, one file per
    part (`playvr_camera.cpp`, `playvr_input.cpp`, `playvr_sim.cpp` for
    `--vr-sim`), with ALL its state in one `VrState` struct declared in
    `playvr.h`, so `playstate.h` gains one member and not twenty;
  * `engine/backends/openxr/` - the OpenXR frontend, as `sdlfront.*` and
    `carbonfront.cpp` are frontends;
  * **an existing file gains only a CALL at its seam** - one line where
    `worldCamera()` ends, one where the pacer decides to sleep, one where the
    adventure input is read - and `playvr_off.cpp` stubs them, as
    `playharness_off.cpp` stubs the instruments, so a build without VR
    (`make play VR=0`, the Vita, the classic Mac) links none of it.
  It is this project's own work - there is no original to be faithful to -
  and each slice says so in PORTING B2's three places.

### Step 1 - the stereo seam and the fake headset (Mac; ~1-1.5 days)

* **The pose types**, in `frontend.h`: a `HeadPose` (per eye a position in
  metres, an orientation, and the four field-of-view tangents OpenXR gives;
  `valid` false by default) and a virtual `bool headPose(HeadPose&)` that
  returns false everywhere today.
* **One conversion module, pure and testable** (`src/platform/xrspace.h`):
  metres to inches (x39.37); OpenXR's Y-up into the game's Y-down - a
  REFLECTION, so a rotation's sense flips (CLAUDE.md §5) and the conversion
  is done once, here, and nowhere else; and `authored camera x head local
  pose -> per-eye eye/at/roll + an asymmetric frustum`.
* **`View` gains an optional per-eye frustum** (the four tangents). `RCamera`
  is eye/at/hfov/roll (`o3de/raster.h` 71) - symmetric, so the per-eye
  frustum is new. All three renderers honour it: the software reference too,
  because the checks render headless through it.
* **The seam**: at the end of `worldCamera()`, beside the `--eye`/`--at`
  instrument override (line ~589), keep the authored view, then compose the
  head. The CPU culling (`frustumFromFov`, `playframe_world_staged.cpp` 105)
  takes ONE frustum covering both eyes.
* **`omk-play --vr-sim`**: the head on the mouse and keys, the two eyes side
  by side through `View`'s viewport (the one the letterbox already uses).
* **Checks** (each shown to fail): `engine: vr camera rule` - an identity head
  gives a frame byte-identical to the flat one; a head turned 30 degrees right
  turns the picture right (the reflection's sign, mutated by flipping it); the
  two eyes 2.52 inches apart for 64 mm.

**Step 1 DONE 2026-10-07** (`d5e25e9` and the commit after it). What was
built, and where it departed from the plan above:

* **`OMK_VR`, the reader's macro** ("not compiled on non-vr targets"): only
  the main Makefile defines it (`VR ?= 1`); every VR line is under it. The
  Vita, classic Mac, 3DS and PowerPC builds compile every `backends/sdl/*.cpp`
  and `src/*/*.cpp`, so the viewer's VR half lives in **`engine/backends/vr/`**
  (`playvr.h`, `playvr_setup.cpp`, `playvr_camera.cpp`, `playvr_draw.cpp`),
  which they never see, and its stubs in `backends/sdl/playvr_off.cpp`, which
  they pick up by their own globs; `src/vr/xrspace.*` compiles empty for them.
  None of their build files changed. `make VR=0` builds the same way.
* **The object tree records its VR value** (`build/obj/.vr`) and is emptied
  when it changes: `OMK_VR` changes `RCamera`'s and `PlayState`'s layout, and
  make does not track flags, so a mixed link would be memory corruption.
* **The main code's share**: one call where `worldCamera()` ends
  (`vrAfterWorldCamera`), one where `worldMirror()` draws (`vrWorldDraw`,
  returning true when the eyes were drawn), one after the options
  (`vrSetup`), the `VrState` member and three declarations in `playstate.h`,
  `headPose()` on the Frontend, the off-axis fields on `RCamera` (with the
  arithmetic in the software, GLES and Vulkan projections), and one usage
  line naming `--vr-help`.
* **The flags are read by the VR code**, one word each (`--vr-head=30,0,0`):
  the main parser ignores what it does not know, but would take a separate
  value word for a screen number. `--vr-sim` (side by side), `--vr-sim=mono`
  (one eye over the frame - the identity check's form), `--vr-camera=level|
  full`, `--vr-head=Y,P,R` (yaw right, pitch up, roll to the right shoulder;
  the numpad turns it live, 5 recentres), `--vr-headpos`, `--vr-ipd`,
  `--vr-fov=quest2` (a NOMINAL asymmetric eye, 52 degrees outer / 44 inner,
  not measured from a headset).
* **The composition is a proper rotation, so the reflection trap does not
  arise**: the head is composed in the authored camera's own frame,
  `(x, y, z) -> x s + y u - z f`, det +1 since `u = s x f`. The Y-down
  reflection only bites a conversion through world axes, and none is made.
* **Measured**: `vr_probe` - a 30-degree right turn looks along 0.5 of the
  camera's right, 20 up along 0.342 of its up, a 15-degree roll to the right
  shoulder gives roll +15; 64 mm is 2.5197 inches along the right; an
  unturned head is the authored camera to the bit; each edge of the nominal
  Quest eye lands on its picture's edge; the culling camera holds 144 of 144
  eye-frustum corners. On Anekbah's street: the flat frame and the unturned
  mono frame are byte-identical; the Quest-shaped eyes on GLES against the
  software reference differ in 258 of 480000 pixels, on edges. Vulkan's
  off-axis rows are built and NOT run.
* **Checks** (`--slow`): `engine: vr camera rule`, `engine: vr frame`, each
  SHOWN TO FAIL - the rotation's `- z f` made `+` gave `yaw30_ahead -0.8660`
  and roll -15; both eyes drawn from eye 0, and the mono seam not applying the
  head, turned "both halves different" and "turned != flat" False.
  `licence headers` 580 -> 588 for the eight new files.
* **Still open from step 1**: what besides the culling reads `view.cam` after
  `worldCamera()` - side by side it is now the CULLING camera (the head's
  orientation, a few centimetres behind the eyes), which the crowd's
  distance tests, the sky and anything sound-side see. Not yet audited; the
  authored camera is kept in `vr.authored` for whichever must have it. The
  interface is composed flat over both halves (step 6 moves it to a quad).

**To look at it**: `build/omk-play ../gamedata ../tables --save
../traces/save-appart.bin --area 0 --stand 1804,0,-6890,336 --vr-sim`, the
numpad for the head.

### Step 2 - the cameras, mode by mode (Mac; ~1-1.5 days)

* **Authored cameras** (conversations, editings, world cameras, the slider
  ride) become the origin as they are. Two flags, as §3 asked:
  `vrcamera = level | full` (the authored roll and pitch dropped or kept;
  level by default) and `vrrecentre = cut | scene`. The cuts are already
  known where they happen: an editing's shot change, a dialogue camera pair
  change.
* **Adventure first person**: the eye where shoot mode's is - the pelvis plus
  `player->headLift()` (`worldCamera()`'s shoot branch, ~line 470) - and the
  body hidden by the same not-drawable flag `Shoot_Enter` sets (the port's
  `sub_436CE0`, `playframe_world_scene.cpp` ~655). **Look-relative
  movement** is done by TRANSLATING, not by new movement: the stick's
  direction in the head's yaw becomes a wanted heading, and the existing
  input bits (turn left / right, forward) drive the body toward it, so the
  `.CTL` channel and the walker stay the engine's own. To read first: how
  the adventure controller turns today (`playframe_control_adventure.cpp`
  33: "the mouse turns the BODY in yaw and the CAMERA in pitch").
* **The fight**: `FightCamera` (`actor/fight.h` 207, `Fight_TickCamera`) is
  the origin, with a VR variant behind a flag - the orbit's distance FROZEN
  at the fight's start (no zoom in or out), the eye's ease (0.25 m a frame)
  and the heading's turn slowed, the throw swing and the steady orbit
  (states 2 and 3) clamped to the same speed. Flags so playing can tune
  them.
* **Seated, behind one seam**: a `ReferenceSpace` setting where the eye
  height and the floor origin are chosen; seated uses the head's LOCAL
  offsets around the authored eye. Standing is a second value of it later.
* **Checks**: an `OMK_CAMEYE`-style line per mode under `--vr-sim`, and a
  headless run through dialogue 402 asserting the authored trajectory is
  unchanged by a moving head while the drawn eye follows it.

**Step 2 DONE 2026-10-07** (`94c1987`). `backends/vr/playvr_camera.cpp`
names the frame's camera KIND - world, first-person, dialogue, editing,
fight, ride, shoot, from the flags `worldCamera()` already set - and picks
the headset's origin for it. Every choice is a flag beside the authored path
(`--vr-help`); `--vr-adventure=authored --vr-fight=authored` leaves the head
on the authored camera alone.

* **First person** (`--vr-adventure=first`, the default): when the player is
  at the keys in adventure. The eye is where shoot mode's is - the pelvis
  lifted by `headLift`, the engine's own first-person height (0.7 of the
  model's extent, `sub_414520` case 4), which puts it 71 inches (1.81 m)
  over his feet: a little above a real eye line, and left as the engine's.
  His body is hidden (one line where `drawPlayer` is decided). The view's
  heading starts along his body and turns only by the SNAP TURN (30 degrees,
  numpad 1/3 on the Mac).
* **The look-relative walk** is a TRANSLATION of the input word, one line
  after `bits = in.frame(st)`: the four direction bits of the Aventure group
  (1 turn left, 2 turn right, 4 `Avancer`, 8 `Reculer`) are read as a
  direction relative to the head, and turned back into the same bits for
  the body - turn toward it beyond 12 degrees, walk while it is within 70.
  The `.CTL` channel and the walker take their usual word. Measured: the head
  turned 90 degrees right, `Avancer` held, he walks ~76 degrees right of his
  first heading (he turns while he walks).
* **The recentre**: the raw head's yaw and position become zero at a change of
  kind and, with `--vr-recentre=cut` (the default), at a CUT - read off the
  authored camera alone, a jump of over 1 m or a turn of over 20 degrees in
  one frame, never in first person or a fight. In the Impasse's 1200 frames
  every cut jumped 41-505 inches against under 8 a frame for a travel; it
  recentres at every editing that starts away from the last camera (331,
  556, 629, 980, 1164) and NOT at 464, where 'demsuite' takes over exactly
  where 'sautdemon' held. A conversation's cameras TRAVEL between their
  pairs (387: 4166 -> 4167), which is not a cut and does not recentre.
* **The calm fight camera** (`--vr-fight=calm`): over `Fight_TickCamera`'s
  own output, untouched - its target as chosen, the distance FROZEN at the
  fight's first frame, the direction turned toward the authored one by at
  most `--vr-fight-turn` (1 degree) a frame. The supermarket fight: 131
  inches held where the authored camera moves between 110 and 170, at most
  1.01 degrees a frame where the authored one jumps by up to 120 (its
  re-placements). **For the play pass**: such a jump now takes ~4 s to catch
  up; a fade-cut past some angle may be better than a sweep.
* **Seated**, behind `VrState::Space` - the one place a standing mode changes.
* **The authored camera is never touched**: conversation 386's authored eyes
  are identical with the head still and turning 1.5 degrees a frame.
* **Step 1's open item, closed**: what reads `view.cam` after `worldCamera()`
  is all on the DRAWING side - the culling, the crowd's level of detail and
  posing reach, the particles' facing, the sky, the lights - plus ONE sound:
  the street traffic's listener (`session.sliders().setListener(view.cam.eye)`,
  `playframe_input_parts.cpp`), where following the head is what a headset
  wants. No game logic reads it.
* **Not read, worth knowing**: the original has its OWN first-person view in
  adventure - Aventure bit `0x80`, `Vue première personne`, key L. What it
  does in the engine is not read; it may matter for how first person feels.
* **Check** (`--slow`): `engine: vr modes`, SHOWN TO FAIL with four
  mutations at once, each turning its own assertion red - the turn bits
  swapped (walks left), the fight distance not frozen (spread 60.3), the cut
  test disabled, the logged authored eye taken after the head.
  `engine: vr frame` now turns its head AFTER the first frame's recentre.

### Step 3 - shoot mode aims with the controller (Mac; ~1 day)

* `HostInput` gains the controllers' poses (an aim ray each, in head space);
  `--vr-sim` drives one from the mouse.
* The engine already parts aim from body: the arm's aim angles
  (`dword_6579A0/A4`, `actor/shootaim.h`) bend the upper body through
  `S_AUTOLK`'s keys by bands of yaw (+-40 degrees) and pitch, and the
  `--aim-at` harness (`playharness.cpp` 347) aims the player's shot at a world
  point. The controller's ray gives the point, the angles follow from it,
  and the body turns with the head. To read first: what the shot ray is
  built from when the aim yaw is not 0 (the player arm sets its target to 0,
  so this path has run only for the gunmen).
* **Check**: headless, the head looking 30 degrees away while the simulated
  controller points at a gunman in `--area 230 --scene-chunk 56`, and the
  bolt hits him.

**Step 3 DONE 2026-10-07** (`d4a8bb4`). What was read first, and it decided
the design: the engine aims the player's shot along his FACING and the look
PITCH (`rs.yawDeg = player->facing()`, `rs.pitchDeg = shootPitch` where
`Actor_TickProjectiles` fires, `playframe_control_adventure.cpp`), and the
arm slews toward the same two (`shootAimSlew(shootAim, 0, shootPitch)` - yaw
0 because the BODY turns with the look). So the controller does exactly
what the mouse did, and nothing downstream changes:

* the right controller's ray, taken into the world through the frame's
  origin, gives a point 10000 inches along it, and `shootGunmanAim` (the
  solver `--aim-at` uses) turns that into his facing and the look pitch,
  written before the tick (`vrShootAim`, called from the input hook). The
  shot, the arm's raise and his body follow as they always do. The pitch is
  held to the mouse's own +-45, the range the arm's keys are authored over;
* the Tirer group's `Tourner a gauche/droite` and `Regarder En-Haut/En-Bas`
  (0x1, 0x2, 0x200, 0x1000) are taken out of the word: the controller owns
  both. The mouse moves the FAKE controller on the Mac and is zeroed before
  `adventureAim` can turn him with it too;
* shoot mode takes first person's ORIGIN (his head, the heading turned only
  by the snap turn): riding his facing, the view would turn twice. A
  script's camera in shoot mode (`shootCameraLive` false) stays authored.
* `vr::HeadPose` gains the two controllers' poses (`hand[2]`, `handValid`);
  the fake right one sits below and right of the head's start, NOT turned
  with the head (`--vr-aim=Y,P`); `--vr-head-after=F:Y,P,R` turns the fake
  head after a recentre, since one turned from the start is zeroed.

**Measured** in the supermarket (`--area 230 --scene-chunk 56`, shoot mode
from frame 394): the controller 30 degrees right and 10 up gives facing
-30, pitch 10, and shots at yaw -30 pitch 10 whose direction's y is -0.174 -
up, Y pointing down; with the controller straight, the head turned 40 away
and numpad 6 (the group's turn key) held, the view turns and the facing
stays 0. **Check**: `engine: vr aim`, SHOWN TO FAIL - aiming from the head
(facing 0, pitch 0), the turn bits left in (facing 68), and alone the
controller's pitch sign flipped (pitch -10, shots downward).

**For the play pass**: shoot mode's MOVEMENT is relative to his facing, so
now to the gun, not the head (`actor/shootmove.h`); whether it should follow
the look as adventure's does is a question for a hand on a stick. The body
turns with the controller at once, as it did with the mouse. The shoot HUD
is still composed flat over the frame's left part (step 6). Only the right
controller aims; a left-handed choice is a flag later.

### Step 4 - an Android build that boots on the Quest (device; ~1 day)

* **What the reader does once**: developer mode on the Quest, the USB cable,
  `adb devices` seeing it. **What is installed**: the Android NDK (absent on
  the M1 on 2026-10-05; the SDK and `adb` are there), SDL's Android sources.
* A CMake build modelled on `backends/vita/CMakeLists.txt` (`omk_engine` +
  the viewer's sources + the GLES backend + SDL), an APK with the Quest's
  manifest entries.
* The data SIDELOADED to the app's folder (`/sdcard/Android/data/<package>/
  files/`, `gamedata` and `tables`), found through `omk.conf`'s resolver;
  `DataFs` is already case-insensitive.
* **Milestone**: still FLAT - the game in the Quest's 2D panel, booting to the
  start menu with sound, its log over `adb logcat`. It proves the build, the
  data and GLES on the device before OpenXR is added.

**BUILT 2026-10-07 on the M3 - NOT YET RUN on a headset** (no Quest was
attached). `scripts/android-build.sh` -> `engine/build/android/omk.apk`
(3.0 MB; arm64-v8a, minSdk 29, target 32, package `org.omk.play`):

* **The toolchain differs per machine and the script FINDS it**: the M1 had no
  NDK; the M3 has two Unity installs (6000.0.76f1, 6000.5.2f1) each bundling
  NDK r27c (27.2.12479018), SDK platforms 33-36, build-tools 36 and an
  OpenJDK, beside `~/Library/Android/sdk` (platforms 34/35, build-tools 34).
  Order: `$ANDROID_NDK_HOME`, the SDK's `ndk/`, a Unity editor's `NDK`. No
  Gradle: CMake + the NDK's toolchain file, javac + d8, aapt2, zipalign,
  apksigner with a local debug key (`engine/build/android/debug.keystore`).
* **SDL2 2.32.10 from source** (the Mac's version), fetched once into
  `engine/build/android/`, built as `libSDL2.so`; SDL's own Java glue and a
  one-method `OMKActivity` (`backends/android/java/`).
* `backends/android/CMakeLists.txt` takes the viewer's sources by the
  Makefile's rule (every `backends/sdl/*.cpp` but the other GPU windows and the
  unused instruments half, plus `backends/vr/*.cpp`), `OMK_VR=1`, `OMK_GLES=1`.
  `play.cpp` compiles as `omk_play_main` (the Vita's arrangement) and
  `android_main.cpp` is `SDL_main`. **No `#if` was needed in the engine**: the
  GLES backend's `GLES2/gl2.h` and `threads.cpp`'s `std::thread` paths are the
  non-Apple, non-Vita defaults.
* **Not `omk.conf`**: the C++ viewer takes its roots as arguments, so
  `android_main.cpp` passes `<files>/gamedata <files>/tables --saves
  <files>/saves/GAMES --res 1280x720`, then `omk.ini` (`--config`) and
  `args.txt` (extra flags, one a line; the last `--res` wins) when present.
  `<files>` is `SDL_AndroidGetExternalStoragePath()` =
  `/sdcard/Android/data/org.omk.play/files`.
* stdout/stderr are piped to a dated `omk-play-*.log` / `.err` in `<files>`
  AND to `adb logcat -s OMK`. The game runs on its own thread with an 8 MiB
  stack (SDL's thread has Java's ~1 MiB). The packaged `.so` are stripped;
  `engine/build/android/cmake/libmain.so` keeps the symbols for `ndk-stack`.

**RUN ON THE READER'S QUEST 2 (2026-10-07, Horizon OS on Android 14):** the
APK installs, the 1.7 GB push takes 38 s, and the game boots FLAT in a 2D
panel - GLES2 at 1280x720, area 118, the three films with their sound (EIDOS
dropped 13 of 386 frames to keep up with it), the start menu reached (the
reader). Two faults, both fixed:

* **The first launch died in `free()`** inside the first table load, with an
  argument string already corrupted: the profiler's own `operator new`
  (`platform/profile.cpp`) against a `libc++_shared.so` loaded before
  `libmain.so`, which kept the system allocator for its own out-of-line code.
  The runtime is now STATIC (`android-build.sh` says why).
* **The system keyboard opened under the panel** at boot: `SDL_StartTextInput`
  on Android, as on the Vita - `sdlfront.cpp` skips it there too.

**THE CONTROLLERS DO NOT REACH A 2D PANEL** - measured, not assumed:
`android_main.cpp` logs every raw SDL input event (`[in]`, the first 600 of
a run), and with every button, grip, stick and stick click pressed the ONLY
events were the laser's trigger, as a touch AND SDL's synthesised left mouse
button at the panel point. SDL opens one controller as a pad and it sends
nothing; the shell keeps A/B/X/Y, the grips and the sticks. The game's menus
read the binding word and have no pointer, so the flat build cannot be driven
by the controllers at all - they arrive only through OpenXR (step 5).

### Step 5 - the OpenXR frontend (device; ~1.5-2 days)

* `backends/openxr/`: an instance with `XR_KHR_android_create_instance` and
  `XR_KHR_opengl_es_enable`, a session, the LOCAL (seated) reference space,
  one GLES swapchain per eye, and `xrWaitFrame` / `xrBeginFrame` /
  `xrLocateViews` / `xrEndFrame` around one turn of the loop.
* The GLES backend draws into the eye's swapchain image: it already renders
  to its own framebuffer objects (`fbo_`, `windowFbo_`), so this is a target
  it is handed.
* **The frontend paces**: `xrWaitFrame` blocks, so the 30/60 pacer
  (`playframe_present.cpp` ~84) is skipped when the frontend says it paces.
  The simulation keeps stepping on the measured delta, which is what makes
  72 or 90 Hz free.
* The controllers into `pad::Pad` (sticks, A/B/X/Y, triggers, grips) as the
  Vita pad does, so the four control schemes take them unchanged; their
  poses into §3's aim rays. The headset taken off pauses the game (the
  session leaving FOCUSED).
* The sound listener follows the head - first find where the listener is
  fed from the camera today (the mixer's `Sound_SetListener` port).

**STEP 5a RUN ON THE QUEST 2 (2026-10-07): immersive, the frame on a screen.**
`backends/openxr/xrhost.*` (the Android build's alone, `OMK_OPENXR`): the
Khronos loader 1.1.63 (fetched from Maven Central by `android-build.sh`, its
`.so` packaged), `xrInitializeLoaderKHR`, an instance with
`XR_KHR_android_create_instance` + `XR_KHR_opengl_es_enable`, the session on
SDL's OWN EGL context (display, context and config queried from it), the LOCAL
space, and ONE quad layer: the composed frame - films, menus, the world - on a
2.4 m screen 2 m ahead. The GLES glue draws each present pass into the quad's
swapchain image (`glesSetWindowTarget`, which existed for the probe) and
SUBMITS where the window swapped (`presentTarget` / `swapOrSubmit` in
`playgpu_gles.cpp`); `xrWaitFrame` paces. Measured on the device: runtime
Oculus 201.124.0; it wants GLES 3.0-3.2, so the Android context is now ES 3
(the backend's `#version 100` shaders run unchanged on 3.2); the swapchain is
sRGB8_ALPHA8 with `GL_EXT_sRGB_write_control` - the game's colours are already
gamma-encoded and go in unconverted; 30 of 72 fps through a film, 58-73 at the
menu. The manifest carries the VR and IMMERSIVE_HMD categories and the
loader's permissions and broker queries (no Gradle merges its AAR).
The reader: "it is working but as a VR app, but on a 2D plane" - 5a as
designed; 5b is the eyes.

**OPEN - which path the input took.** The reader walked the start menu and
loaded `games-resto.bin`'s slot 2 (area 217), yet the log has the session at
VISIBLE (5), never FOCUSED (6), so `xr::readControllers` read nothing; SDL's
pad and keyboard handed over nothing (`[in] sdl state` never printed) and the
raw event log has no key. To settle before 5b relies on the controllers.

**STEP 5b RUN ON THE QUEST 2 (2026-10-07): the eyes.** `xr::headPose` begins
the headset's frame (`xrWaitFrame`, so the pose is predicted for this frame's
display) and returns `xrLocateViews`' eyes and FOVs, the VIEW space's head and
the controllers' aim poses; `playvr_camera.cpp` composes them exactly as it
does the fake headset's. Each eye's pass presents straight into that eye's
swapchain image (`glesPresentEye`, the target's top-left `eyeW x eyeH`), no
readback, and the frame submits a PROJECTION layer with the RAW eye poses (the
recentre is in the picture, not the layer). A headset build is in VR by
default (`--vr-flat` for 5a's screen). The reader's play, in order:

* "resolution in the street is too low": the eyes were the 1280x720 frame's
  halves, 640x720. They are now the RUNTIME'S size (1440x1584 on a Quest 2),
  the world target sized to hold the frame and an eye (`xr::start` moved
  BEFORE the renderer's `init`), and `--vr-scale=S` multiplies it.
* "the stick does not respond correctly ... depending on the direction":
  step 2's first-person walk turned his body toward the stick's direction
  with the tank `Tourner` bits and walked only within 70 degrees - forward
  worked, sideways turned first, backward flipped the turn's sign each frame.
  Now the stick's ANALOG direction relative to the head SETS his facing
  (`setFacing` through `shootGunmanAim`, as shoot mode's controller does) and
  `Avancer` walks; the right stick is the 30-degree snap turn and its slots
  (8, 9, 12) are out of the word in first person. `engine: vr modes` green.
* The game's 30/60 pacer is skipped when the frontend paces
  (`Frontend::paces()`, true while an OpenXR session runs): 72/72 fps.
  The reader: "Moves are smooth".
* OPTIONS ROW 2 IS THE EYE SCALE in a headset (the reader's request): the
  list is the recommended size x 0.8 .. 1.5 labelled `W x H (S x)`
  (`xr::eyeModes`), applied LIVE between frames (`xr::setEyeSize`, the world
  target re-`init`ed); the interface frame keeps 1280x720. Default 1.0x.
* A screen open (`openScreen >= 0`) puts the composed frame on the quad OVER
  the eyes, opaque - the sneak was invisible behind the eyes otherwise.
* "stick input was not recognized" in the sneak: `vrAdventureInput` ran under
  a screen with the last world frame's kind and turned every push into
  `Avancer`; it now returns while a screen is up, and first person also
  needs `adventure`. Confirmed: the reader walked the Video page with it.

**Measured, 1.3x (1872x2059 an eye), every option at its top, Anekbah's
street:** 72/72 fps in 11 of 12 one-second samples (one at 54); the app's GPU
8.1-8.5 ms of 13.9, the runtime raising the GPU level from 2 to 3 of 4. At
1.0x it was 6.7 ms at level 2. The game thread's sections sum to ~13.7 ms but
`world begin, set` (6.6) now CONTAINS the `xrWaitFrame` wait (`headPose` runs
inside it) - measure the wait apart before any 90 Hz decision. THREE SLOW
FRAMES of 190-278 ms at one spot (3388, -5540), ~15 s apart, standing: not a
load - an open hitch to find.

### Step 6 - the interface, the films and the keyboard (device; ~0.5-1 day)

* The composed 640x480 interface (menus, the sneak, subtitles, reply choices)
  into its own texture, shown as an `XrCompositionLayerQuad` - the
  compositor draws it sharp, at about 1.5 m. The FLIS films on the same quad.
  On the Mac, `--vr-sim` keeps compositing it into each eye flat.
* **What the world does behind a screen - already read and already ported**
  (`docs/UI.md` "Which screens stop the world", 2026-09-07; `verify.py:
  engine: screen world`, `engine: sneak`). Asked 2026-10-07 whether the port
  letting the world run is right: it is.
  * The world TICKS behind every screen: `Game_Tick` (0x004200F0) runs the
    scripts, the projectiles, the traffic and the ride with no test for an
    open screen. Only the script that called `ui.open` waits.
  * The one exception is PAUSE GAME (31): its open callback sets the pause
    flag `dword_4E9728` (two writes in the image), which forces the frame
    delta to 0.
  * What a screen stops is the world's DRAWING, its SOUND and Kay'l:
    without bit `0x40000` in its record's `+112`, `UI_LoadScreen` calls
    `sub_466B30` - the full-screen 3D view off, every sound buffer suspended,
    the player in ACTOR_STATE 9 - and the close undoes all three. Three of
    37 screens carry the bit: PAUSE GAME, SHOOT MECA, SHOOT HUMAN.
  * A panel with bank B `0x800` turns the world back on, DIMMED: the ten
    shops, SAVE GAME, PAUSE GAME, SHOOT HUMAN, HIGH-SCORE. And a 3D viewport
    item (the videophone's caller) draws the world into a rectangle through
    the game's live camera.
* **So four kinds of screen, and a VR form for each:**

  | kind | screens | world | VR form |
  |---|---|---|---|
  | world hidden | the sneak, MULTIPLAN, the terminals, most | ticking, not drawn, silent | the window in a DARK space - the original's black, comfortable while the window is fixed in the world |
  | world dimmed | shops, SAVE GAME, high scores | ticking, drawn dimmed | the window before the live, dimmed world |
  | world frozen | PAUSE GAME | delta 0, drawn | the window before the frozen world, still looked around in |
  | heads-up | SHOOT MECA / HUMAN | live | not a window: an overlay on the view or the gun, later |

  A 3D viewport item becomes a flat MONITOR texture on its window, rendered
  once and not per eye, through the game's camera and not the head's.
* **Placement**: a window is FIXED IN THE WORLD where the head looked when the
  screen opened, level, at about 1.5 m - never following the head. The
  sneak's own forms come later.
* **A VR-only option, beside the faithful path** (§5's rules): the world
  drawn dimmed behind a world-hidden screen. Less disorienting, but it shows
  what the original hid - the traffic moving while Kay'l stands held in
  state 9. The faithful dark space is the default; a faint floor or horizon
  line in it is the smallest departure, and the play pass decides whether
  it is needed.
* Conversations are not screens: the subtitles and reply choices are a
  small panel while the world is drawn through the conversation's camera.
* **The keyboard, tried first**: `startTextInput()` showing the system
  keyboard through the `oculus.software.overlay_keyboard` manifest feature;
  if a native app does not get it, `XR_META_virtual_keyboard`, which costs
  drawing its model. The Vita's `backends/vita/ime.cpp` is the shape of the
  frontend's half either way.

### Step 7 - the first play pass (device; ~0.5-1 day)

One route, at 72 Hz on a Quest 2, the frame log over `logcat`: boot, the
films, the start menu with a name typed, the apartment, the street, one
conversation (camera rule, recentre), a shoot phase (`--area 230
--scene-chunk 56`, controller aim), a fight (`--fight-supermarket`, the
slowed camera). What it decides is written back here: what lies outside
the authored frame (§3b 5), the two camera flags' defaults, the fight
camera's speeds, and which hitches §4's preload has to remove.

**Not in this prototype**: §4's preload mode (after the play pass, which
says which hitches matter), multiview (needs the GLES 3 shader prelude - the
first optimisation after), standing play (the seam is there), menu pointing,
comfort options.

**Read before the step that needs it**, so no estimate rests on a guess:
the adventure controller's turn (step 2); the shot ray when the aim yaw is
not 0 (step 3); where the listener is fed (step 5); and whether anything
besides the culling and the crowd's distance tests reads `view.cam` - those
read the drawn camera today, and each one has to choose authored or head.
