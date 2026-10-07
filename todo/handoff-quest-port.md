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
"engine: mipmaps"`. `licence headers` counts **593**. No check runs the
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

**The reader's play report of 2026-10-07 evening** (the M1's first build,
`a03c4bd`, the restaurant save; recorded as said, before any reading):

1. **Shoot and fight modes are very laggy**, where the SAME environment is
   smooth in adventure mode.
2. **Shoot mode: the stick should also STRAFE** (lateral steps), not only go
   forward.
3. **Leaving some interiors**: every npc in the street is T-POSED and the
   effects carry RANDOM TEXTURES; at some point there seem to be MORE npcs
   than normal (the reader suspects the preload, or scripts run twice).
4. **Main menu: the sticks are too sensitive** - a held stick keeps changing
   the selection for as long as it is held, not once per push.
5. **No virtual keyboard** when starting a new game (6c, not built yet).

**Report 1, the lag - FIXED, installed 2026-10-07 20:24, NOT yet played.**
The run's own lines: `present, swap` 45-60 ms a frame in the shoot (frames
18402-21610) and the fight (25452-26921) against ~1.5 in adventure, and
inside it the `gles` line's `texture upload` 40-60 ms against 0.5. The HUD
over the world (shoot screen 34, the fight's bars) is the OVERLAY frame, and
its two planes changed ~700 rows a frame (`overlay: ~42000 plane rows
re-sent in 60 frames`), each sent as its own `glTexSubImage2D` into a
texture the eyes' draws still read - the Adreno shape the buffer re-floor
had (§6). The desktop GLES build on the shoot route shows the same ~350
rows a plane, so the HUD really changes them; the cost was the call count.
Now `presentOverlay`'s `sync` sends contiguous rows as one call, and on
Android the span first..last changed row as ONE call a plane.
`engine: gles overlay` green (0 of 307200), red with the run's source
pointer broken (57701). To confirm on the headset: `texture upload` and
`present, swap` in a shoot back near adventure's. The menus' and
conversations' ~10 ms upload is `presentSurface`'s, already one call.

**And the Vita build was broken by 6b** (found on the way): `glBlendColor`
(the eye shade) is not in vitaGL - now outside `__vita__` - and the Vita's
CMake lists its sources by hand without `playvr_off.cpp`, so `PlayState::vr*`
did not link. `engine: vita build` green.

**Report 4, the menu stick - FIXED, installed 2026-10-07 20:33, NOT yet
played.** The interface's 0x203F mask already turns a held bit into one
press, and `vrAdventureInput` stands aside under a screen - so a held stick
repeats only if its BIT flickers. It does: `Input_Poll` sets a bit per axis
on each side of 0, the frontend's dead zone is 250 a component, and a thumb
pushed down drifts 0.2-0.35 sideways, crossing it frame after frame (the
log could not show this - the `[in]` lines stop after 40 presses and do not
carry the stick; it is the mechanism the probe reproduces, 22-23 presses
from one held push). Under a screen the stick is now ONE direction, the
dominant axis, entered past 500 and held to 300 (`pad::MenuStick`,
`input/pad.h`, applied in `playframe_input_parts.cpp` while `walk`) - every
pad (Vita, SDL, Quest) gets it; the world keeps both axes.
`engine: menu stick` (new; `engine/tools/menu_stick.cpp`): eight cases raw
and filtered, shown to fail by both rules removed. NOT covered: the
conversation's reply list, which is not a `walk` screen - if a held stick
runs down the replies too, that is where.

**Report 3, the street after an interior - two causes found and fixed,
installed 2026-10-07 21:06, NOT yet played.** Neither is the headset's: the
flat game has both.

* **The effects' random textures = the texture pool past 64** (`f4c6cf9`).
  The run's own `WARNING: texture pool is 72` (26 set + 43 character + 3
  sprite at the screenshots): the sprites at 69..71 bound slots 5..7, set
  textures - the street lights' glow in stone. The engine's six-bit slot
  never wraps (58 slots); this port's pool does, so the full index now rides
  above the 14-bit key (`omk::drawTextureSlot`, all five renderers).
  `engine: pool past 64` (`OMK_POOL_PAD=64`, software and GLES). Why the
  headset's pool grew so large is NOT settled: 43 character textures for 10
  models where a fresh street has 14 for 7 - the kept player model (hidden
  in first person) and the last speaker's are the likely two; harmless now.
* **The T-poses = walkers drawn from FREED tracks** (this commit). A
  walker's `p.tracks` points into `pedTracks`, which a new animation library
  clears; it was rebound only when its CLIP POINTER changed, and the circuit's
  clips are copy-assigned into the same vectors on a reload, so the same
  index keeps the same address. The run's route was Anekbah -> the sewer
  (area 157, its own 8 walkers and library) -> 218 -> Anekbah. Now a
  library change rebinds every walker (`pedCacheGen`), and `shootClips` - the
  gunmen's descriptors into the old library - is cleared with the rest. NOT
  reproduced on the desktop (the route is long); the evidence is the code
  and the route. **Two new log lines confirm it on the next play**:
  `crowd library - ANIMS/X.ANI ... generation G` at each change and
  `crowd library - N walker(s) rebound ... though their clip pointer was
  unchanged` - N above 0 is the fault caught. A first desktop look at the
  staged extras in `--vr-sim` read a GESTURE as a T-pose; it was not.
* **"More npcs than normal"** - not changed. Two things add bodies in this
  run: five more scripted extras (59, 62, 63, 69, 70) are staged in Anekbah
  from the shoot's story state on, and since today's `engine: crowd reach`
  the walkers are drawn to the clip distance, not cut at 40 m. If it is
  something else, say where.

**The reader's account of report 3, 2026-10-07 21:10** (recorded as said,
after the fixes above were installed and before they were played):
* the T-posed npcs happened SEVERAL times, not once; going into another
  interior and out again sometimes fixed it, sometimes not;
* after a while there were normally animated npcs IN ADDITION to the
  T-posed ones - "which made me wonder if the T-pose npcs were not npcs that
  were supposed to be destroyed";
* "more npcs than normal" means: several times the crowd DENSITY seemed
  bigger on going outside after entering a building - first noticed on
  leaving the TEMPLE in Qalisar;
* in VR, through the small gap between doors, the street's npcs are still
  there and moving while he is indoors;
* the reader suspects the preload radius, the Quest build's main new thing,
  in how scripts are loaded.

**What the account changed, 2026-10-07 21:30 - installed, NOT yet played.**
Read against the run's log:
* the street crowd is NOT duplicated in the simulation: Qalisar holds 167
  live walkers before, inside and after the temple, and staging is one body
  per actor. So extra bodies are DRAWING faults, and two were found in the
  walkers' drawing, both latent in the flat game:
  - **a walker's model was taken ONCE** (`if (!p.mo)`), and the per-walker
    list is rebuilt only when the number of movers changes - which a city
    reloading its circuit to the same cap (Anekbah's 200) does not. After any
    interior each slot kept its previous occupant's model, and once the
    eviction let that model go, a pointer to a freed one. Now resolved from
    the walker's own model every frame, the tracks rebound with it;
  - **the crowd's track cache was keyed (sex, clip) only**, its mesh indices
    the first asking model's - now keyed by the skeleton layout as well. On
    three fresh streets no clip was bound differently, so this one is a
    latent fault, NOT shown to be the reader's.
  Neither is reproduced on the desktop: the airlock and the temple cannot be
  walked out of and back into with `--hold` (backwards does not reach the
  trigger, turning does not find the door). **The next play's log decides**:
  `crowd library - N walker(s) whose slot was last drawn as another model`,
  `... rebound after the library change`, and `crowd tracks: ... the shared
  binding of another skeleton` - each N above 0 is a fault caught.
* **there is no Quest-only preload**: `quest-port.md` §4's mode was never
  built. What preloads is the engine's own `area.preload` (a door zone loads
  the next area into the second resident slot) and the threaded set read -
  the same on every build.
* **the street's walkers keep walking and are DRAWN while he is indoors**
  (the temple: 44 drawn with Qalisar's slot hidden). Whether the original
  draws a hidden slot's crowd is not read - `sub_48D7F0`'s caller is the
  place to look; recorded, not changed. **READ 2026-10-07: the original
  does NOT** - the circuit's instances live in the street decor's own scene
  (`dword_8F5E34`), and a hidden decor's scene is out of the render chain
  `sub_479C20` submits (`docs/STREET_LIFE.md`, "The crowd is drawn WITH ITS
  STREET"). And `Sliders_Tick` has no such gate: the crowd keeps WALKING
  unseen. FIXED the same day, like the original: drawn only while the
  circuit's slot is shown, the ridden slider excepted (`engine: crowd
  indoors`, red with the gate removed: 32 drawn); BUILT, NOT installed (the
  headset had dropped off USB) - `scripts/android-build.sh install`. The log says `crowd library - the circuit's slot N is HIDDEN` /
  `SHOWN` at each change.
* **two programs drive one actor** at frame 98359: actors 147 CMH_FN and 458
  CWH_FN each have a pose on a sewer path AND on an Anekbah path 20 m apart
  in the same frame - two resident scenes naming the same actors. One body,
  so not a duplicate, but a body that jumps. Recorded, not investigated.

**The order for the rest** (one at a time, each played before the next):
2. ~~the menu stick~~ - done above;
3. ~~the street after an interior~~ - above;
4. the shoot strafe - first what the original's shoot scheme binds;
5. the keyboard (6c).

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
