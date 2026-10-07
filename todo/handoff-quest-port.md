# Handoff — the META QUEST VR PROTOTYPE (begun 2026-10-06, updated 2026-10-07 evening)

**Read this first to pick up the Quest.** [`quest-port.md`](quest-port.md) is
the plan and the record: §1-4 why this code and not Unity, the performance
proxy, the camera rule, the preload mode; **§5 the seven-step implementation
plan**, each finished step followed by its block with what was read,
measured, played and checked. This file is the state, the recipes and what
to do next.

The reader's way of working on this task: **one step at a time** - finish
it, commit it, report it in a message that stands on its own, then wait.
The reader PLAYS each build on the headset and reports; a fix is not done
until it has been played.

---

## 1. Where it stands

**OMK runs IMMERSIVE on the reader's Quest 2**: the game drawn per eye at the
runtime's resolution, 72 fps, first person with the Touch controllers, the
interface as a transparent layer, the screens as windows fixed in the world
over a shaded world. Steps 1-5 done, step 6 mostly done (6a, 6b; 6c the
keyboard not started), step 7 not started.

| step | state | commits |
|---|---|---|
| 1 the stereo seam, the fake headset | done | up to `2b22f9e` |
| 2 the cameras, mode by mode | done | `94c1987`, `54178a4` |
| 3 shoot mode aims with the controller | done on the desktop; NOT yet played on the headset | `d4a8bb4`, `93a7c66` |
| 4 an Android build | done - played flat in a 2D panel, then superseded by 5 | `3d63f43`, `b679f0e` |
| 5a OpenXR, the frame on a quad | done, played | `4a25630` |
| 5b the eyes, the walk, the scale row, the pacing | done, played | `c3f5718` |
| - the headset's enhancements and clocks | done, played | `7797ac3`, `934d17b` |
| - the street's slow frames (the re-floor upload) | done, played | `e3c6ac0` |
| 6a the interface as a transparent layer | done, played | `f1d1d28` |
| 6b the screens: world-fixed window, shaded world | done, played | `16dca15` |
| 6b the videophone's MONITOR (the caller's picture) | **built, installed, NOT yet played** | `7382903` |
| 6c the system keyboard (the name field) | not started | |
| 7 the first play pass | not started | |

**On the headset (Quest 2, Horizon OS / Android 14, runtime Oculus 201.124.0):**

* **The eyes** at the runtime's recommended size (1440x1584 on a Quest 2),
  `--vr-scale=S` or OPTIONS ROW 2 (the resolution line, live: the recommended
  x 0.8 .. 1.5, labelled `W x H (S x)`). **Default 1.0x - the reader's
  decision**; 1.1x also holds 72 with MSAA, 1.3x does not (60-65).
* **72/72 fps**, the headset pacing (`Frontend::paces()` skips the game's
  30/60 pacer; the simulation steps on the measured delta).
* **Enhancements, the Quest's defaults** (`android_main.cpp`): 4x MSAA
  (GLES 3 on Android), trilinear with 16x anisotropy, fitted text. Per-pixel
  light, mapped shadows, unlimited distance OFF (they took the game thread
  from ~6 to ~13.6 of 13.9 ms). `XR_EXT_performance_settings`: CPU
  SUSTAINED_HIGH, GPU SUSTAINED_HIGH above 1.0x (the GPU was already at its
  top level; the CPU went from 2-3 to 5).
* **First person**: the left stick's analog direction, relative to the head,
  SETS his facing and he walks; the right stick snap-turns 30 degrees; under
  a screen the stick is the menu's (the kind held, no rewrite).
* **The Touch controllers as the pad** (`xr::readControllers`): A confirm /
  action, B back / jump, X, Y, grips the shoulders, right trigger the shoot
  group's action, left trigger or a stick click the sneak, left menu START.
* **The interface over the eyes** (6a): the flat GPU path's overlay frame
  presented as a premultiplied quad layer; fitted glyph edges blended on the
  overlay planes (they were pink). The panel 1.2 m away, 1.2 m wide.
* **The screens** (6b): the panel PLACED where the head looks (level, 1.2 m)
  when a screen opens or a conversation starts, fixed in the world. Behind a
  screen the original hides the world behind (the sneak, terminals...), a
  headset draws it on, SHADED (`--vr-shade=0.45`), the panel opaque; a panel
  that dims the world itself (shops, SAVE GAME, PAUSE GAME) keeps its own dim
  and no extra shade. Only the drawing: Kay'l held, the sound suspended.
* **The videophone's caller** (NOT yet played): on a frame whose panel holds
  a 3D viewport item the eyes are drawn AND the world once more through the
  game's view kept before the eyes rewrite it (`vr.monitorView`), read back
  into the panel - a flat monitor - with the side-plane cull off for that
  frame so one draw list holds both cameras' sight. Before it, the video
  rectangle stayed black (Telis's call, the restaurant).

**The flat game is unchanged**: with no head pose every frame is
byte-identical; every VR option is a path BESIDE the original.

## 2. Where the code is - and the two rules for it

* `engine/src/vr/xrspace.*` - the platform-free arithmetic. `HeadPose` also
  carries the frontend's eye size. Tested by `engine/tools/vr_probe.cpp`.
* `engine/backends/vr/` - `playvr.h` (`VrState`: ALL the VR state),
  `playvr_setup.cpp` (the `--vr-*` flags: `--vr-flat`, `--vr-scale`,
  `--vr-shade` among them), `playvr_camera.cpp` (the modes, the recentre, the
  first-person walk, the aim, `vrDrawsBehindScreen`, `vrNoSideCull`, the
  panel anchoring), `playvr_draw.cpp` (the eyes - into the headset's
  swapchains under `OMK_OPENXR` - the monitor, the overlay frame).
* `engine/backends/openxr/xrhost.*` - THE HEADSET (Android only,
  `OMK_OPENXR`): the loader, instance, session on SDL's EGL context, LOCAL
  and VIEW spaces, the quad and the two eye swapchains (`Chain`), the frame
  lifecycle (`headPose` BEGINS the frame - `xrWaitFrame` paces - `eyeTarget`
  / `eyeDone`, `frameTarget` for a flat present, `submit` with the
  projection and/or the quad), the actions, the performance levels,
  `anchorQuad`, `eyeModes` / `setEyeSize`.
* `engine/backends/android/` - `CMakeLists.txt` (SDL2 from source, the
  OpenXR loader imported, `OMK_VR=1 OMK_GLES=1 OMK_OPENXR=1`),
  `android_main.cpp` (`SDL_main`: the arguments, the Quest's enhancement
  defaults, the log pipe to the dated file and logcat, the `[in]` raw input
  log, an 8 MiB game thread), `AndroidManifest.xml` (immersive, the loader's
  permissions and queries), `java/` (`OMKActivity`).
* `scripts/android-build.sh` - no Gradle: finds the NDK / SDK / JDK per
  machine, fetches SDL2 2.32.10 and the OpenXR loader 1.1.63 into
  `engine/build/android/`, builds, packages, signs.
* **Seams in shared files** (one-line calls, `#if OMK_OPENXR` / `OMK_VR`):
  `playgpu_gles.cpp` (`presentTarget` / `swapOrSubmit`, the overlay as a
  layer, `xr::start` BEFORE the renderer's `init`, the GLES 3 context on
  Android), `sdlfront.*` (`headPose`, `paces`, `displayModes`, the pad, no
  text input on Android), `playframe_present.cpp` (the pacer skipped when
  the host paces), `playframe_input_parts.cpp` (row 2 = the eye size),
  `playframe_world.cpp` (`vrDrawsBehindScreen`), `playframe_world_draw.cpp`
  (`vrNoSideCull` in `outsideView`), `frontend.h` (`paces()`),
  `glesrender.cpp` (`presentEye` with a shade, `setOverlayAsLayer`, MSAA on
  Android, the orphaned whole upload past 64 dirty runs on Android, the
  `heavy upload` log), `text.cpp` (the fitted glyph on the overlay planes -
  every build).

**Rule 1 (the reader's): VR code in its own files**; an existing file gains
only a call at its seam. **Rule 2: everything VR is under `#if OMK_VR`**,
defined by the main `engine/Makefile` (`VR ?= 1`) and the Android CMake; the
headset half under `OMK_OPENXR`, the Android CMake's alone. The Vita, classic
Mac, 3DS and PowerPC builds compile none of it.

## 3. Recipes

**On the headset** (Quest 2 in developer mode, USB, `adb devices` lists it):

```bash
scripts/android-build.sh install            # build + adb install -r
# once: the data, 1.7 GB, sideloaded (<gamedata> = what tools/omkpaths.py resolves)
adb push <gamedata> /sdcard/Android/data/org.omk.play/files/gamedata
adb push tables     /sdcard/Android/data/org.omk.play/files/tables
adb logcat -s OMK                           # the game's stdout/stderr
```

Launch from Library > Unknown sources WITH THE CONTROLLERS AWAKE - an
`am start` while they sleep is refused by the shell's "controller required"
dialog and the game never starts (`adb logcat | grep LaunchCheck`).
`.../files/args.txt` holds extra flags, one a line, AFTER the defaults (the
last of a flag wins); the reader's test file is `--slot` / `2` / `--nofmv`
(the restaurant slot of `traces/games-resto.bin`, pushed as
`.../files/saves/GAMES`). Each run's log: `.../files/omk-play-<date>.log`.

**Reading a run**: the headset's own line,
`adb logcat -d | grep "VrApi.*FPS" | grep " <pid> "` - `FPS=a/72`,
`CPU4/GPU=c/g` (**c the CPU level, g the GPU's** - read the other way round
once, and a wrong conclusion followed), `App=` the GPU ms. The game's own:
`SLOW FRAME`, `: sections -`, `gles: heavy upload`, `openxr:` lines.

**On the desktop** (the fake headset, numpad head; see `--vr-help`):

```bash
cd engine && make play
build/omk-play ../gamedata ../tables --save ../traces/save-appart.bin \
    --area 0 --stand 1804,0,-6890,336 --vr-sim
```

**Checks**: `python3 tools/verify.py --only "engine: vr" "licence headers"
"play usage"`; after a GLES or text change also `"engine: gles overlay"
"engine: text scaling" "subtitle box" "engine: texture filter"
"engine: mipmaps"`. `licence headers` counts **592**. No check runs the
headset; the reader's play is its evidence.

## 4. The machines

The toolchain differs PER MACHINE and `android-build.sh` finds it: the M3's
Unity installs each bundle NDK r27c, the SDK platforms, build-tools and a
JDK; the M1 had no NDK (install the command-line tools and
`sdkmanager 'ndk;27.2.12479018'`, ~3 GB, or set `ANDROID_NDK_HOME`). **Done on the M1 2026-10-07**: the
command-line tools are in `~/Library/Android/sdk/cmdline-tools/latest`, NDK
r27c beside them; build-tools 34's `d8` dies with a NullPointerException on
the M1's default JDK 23, so the script now picks a JDK 17/21/11 through
`java_home` (it found 11) and puts it first on PATH. `adb` is
`/opt/local/bin/adb` on the M3, `/opt/homebrew/bin/adb` on the M1. Push with
the credentials file, as on every push from this machine.

## 5. What to do next, in order

1. **Play the videophone monitor** (Telis's call in the restaurant): the
   caller in the video rectangle, the shaded world around, the frame rate
   (that frame draws the world three times and reads one picture back).
2. **6c, the keyboard**: the name field. `startTextInput()` is OFF on Android
   (it opened the keyboard under the panel at boot); try the system keyboard
   through `oculus.software.overlay_keyboard`, else `XR_META_virtual_keyboard`;
   `backends/vita/ime.cpp` is the frontend's shape.
3. **Open from the play so far**: the reading strain of the panel ("a bit
   better" at 1.2 m - a depth that follows the scene, or a smaller panel,
   are the candidates); the few slow frames left (96 ms entering the street,
   a 67, one 224 ms in `world begin, set`); the HUD is still on the panel
   where the last anchor put it.
4. **Step 7, the play pass**: shoot mode on the headset (step 3 has never
   run there), fights, slider rides, conversations through their cameras;
   write what it decides back into §5.

**Open since steps 2-3** (recorded in the step blocks): shoot-mode movement
follows the gun, not the head; the body turns instantly with the controller;
only the right hand aims; the calm fight camera's catch-up; the original's
own first-person key (Aventure `0x80`, L) is not read.

**Not in this prototype**: §4's preload, multiview (`GL_OVR_multiview2`,
available on GLES 3 - the backend's shaders are `#version 100`), standing
play, menu pointing with the controller ray, 90 Hz (plausible at 1.0x; the
CPU's real cost must first be measured apart from the `xrWaitFrame` wait,
which sits inside `world begin, set`).

## 6. Traps that cost time

* **The C++ runtime must stay STATIC on Android** (`c++_static`): the
  profiler replaces `operator new`, and a shared libc++ loaded first keeps
  the system allocator for its own code - the first device run died in
  `free()`.
* **A 2D panel gets no controller input** but the laser trigger (a touch);
  an immersive app gets them only through OpenXR actions.
* **The session can stay VISIBLE (5) without FOCUSED (6)** and still get
  input: `readControllers` reads in both.
* **`am start` is refused while the controllers sleep** ("controller
  required") - the old process is force-stopped and NOTHING starts; a log
  that looks fresh may be the previous run. Check the log's timestamp.
* **The USB link drops when the headset sleeps**:
  `adb shell am broadcast -a com.oculus.vrpowermanager.prox_close` keeps it
  awake; wait for `adb get-state` before each install.
* **An `||` hides a call with a side effect**: `vrDrawsBehindScreen` behind
  `screenKeepsWorld ||` never cleared its shade.
* **The key colour (0xF81F) leaks through anything that BLENDS with the pixel
  beneath** on an overlay frame - any such pass must run on the planes
  (`ui/overlay.h`), as the fitted glyph now does.
* **Adreno stalls on many `glBufferSubData` into a buffer in flight**: the
  day/night re-floor (~55000 scattered corners) cost 135-180 ms a step until
  the whole buffer went through `glBufferData` (Android only).
* **The shell's working directory drifts** between commands in a session
  (`cd engine` persists): a relative `adb install engine/...` then installs
  NOTHING and a stale APK stays on the headset. Use absolute paths.
* **The recentre zeroes a fixed head** on the desktop: turn after it
  (`--vr-head-after=F:...`). **First person is the default**: an identity
  check against the flat game needs `--vr-adventure=authored`.
* **Changing `VR=` wipes `build/obj`**; **other sessions commit to this tree**
  (commit by path, `git diff --cached` first); **zsh** needs `${=var}`.
