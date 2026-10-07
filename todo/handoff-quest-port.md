# Handoff — the META QUEST VR PROTOTYPE (begun 2026-10-06, updated 2026-10-07)

**Read this first to pick up the Quest.** [`quest-port.md`](quest-port.md) is
the plan and the record: §1-4 why this code and not Unity, the performance
proxy, the camera rule, the preload mode; **§5 the seven-step implementation
plan**, each finished step followed by its "DONE" block with what was read,
measured and checked. This file is the state, the recipes and what to do
next.

The reader's way of working on this task: **one step at a time** - finish
it, commit it, report it in a message that stands on its own, then STOP and
wait for "Go".

---

## 1. Where it stands

**Steps 1-4 done; steps 5-7 not started.** Step 4 RAN on the reader's Quest 2
on 2026-10-07: flat, in a 2D panel, to the start menu with the films' sound.
**The controllers cannot drive it** - measured: in a 2D panel only the laser
trigger arrives (as a touch and a left click), every button and stick is kept
by the shell. The reader chose to go to step 5 rather than map touch.

| step | state | commits |
|---|---|---|
| 1 the stereo seam, the fake headset | done | up to `2b22f9e` |
| 2 the cameras, mode by mode | done | `94c1987`, `54178a4` |
| 3 shoot mode aims with the controller | done | `d4a8bb4`, `93a7c66` |
| 4 an Android build booting FLAT on the Quest | done - runs on a Quest 2 to the start menu; no controller input in a 2D panel | `3d63f43`, `b679f0e` |
| 5 the OpenXR frontend | not started | |
| 6 the interface on a quad, the films, the keyboard | not started; the screen analysis is written (§5 step 6) | |
| 7 the first play pass | not started | |

What works under `--vr-sim` (desktop, software / GLES / Vulkan builds):

* **Stereo**: side by side, one eye per half, each through an OFF-AXIS
  frustum (`RCamera::lensX/lensY/tanHalfV`, honoured by the software
  raster, GLES and Vulkan - **Vulkan built, never run**); one cull pass for
  both eyes; `--vr-sim=mono` draws one eye full frame.
* **The camera rule**: the authored camera is the headset's ORIGIN, the head
  composed in its own frame (`src/vr/xrspace.cpp`); `--vr-camera=level`
  (default) drops its pitch and roll. The head RECENTRES at a camera-kind
  change and at a cut (eye jump > 1 m or gaze > 20° in a frame), never in
  first person or a fight.
* **Adventure is first person** (`--vr-adventure=first`, default; `authored`
  keeps the game's camera): eye at the pelvis + the engine's own first-person
  height (~1.81 m), body hidden, snap turn 30° (numpad 1/3), movement
  relative to the LOOK (the head's yaw translated into the Aventure input
  bits: turn beyond 12°, walk within 70°).
* **The fight camera, calmed** (`--vr-fight=calm`, default): the game's own
  `Fight_TickCamera` output with its distance frozen and its turn limited to
  `--vr-fight-turn` (1°/frame).
* **Shoot mode aims with the right controller**: its ray goes through
  `shootGunmanAim` and sets the player's facing and `shootPitch` (±45), as
  the mouse did; the shot and the arm follow. Shoot mode's keyboard
  turn/look bits are stripped in VR.
* **The flat game is unchanged**: with no head pose every frame is
  byte-identical to before; every option is a path BESIDE the original, which
  stays the default and stays selectable.

## 2. Where the code is - and the two rules for it

* `engine/src/vr/xrspace.*` - the platform-free arithmetic (poses, the
  composition, the cull camera, quaternions). Tested by
  `engine/tools/vr_probe.cpp`.
* `engine/backends/vr/playvr.h` - `VrKind`, `VrState` (ALL the VR state, one
  `PlayState` member); `playvr_setup.cpp` the `--vr-*` flags,
  `playvr_camera.cpp` the fake headset / the modes / the recentre / the input
  translation / the aim, `playvr_draw.cpp` the two-eye draw.
* `engine/backends/sdl/playvr_off.cpp` - the stubs a non-VR build links.
* **The main code holds one-line calls only**: `play.cpp` (`vrSetup`),
  `playframe_world_scene.cpp` (`vrAfterWorldCamera`, `vrHidesPlayer`),
  `playframe_world_draw.cpp` (`vrWorldDraw`), `playframe_input_parts.cpp`
  (`vrAdventureInput`); `frontend.h` a `headPose()` virtual; `raster.h` three
  fields; the off-axis rows in `raster.cpp`, `glesrender.cpp`, `vkrender.cpp`.

**Rule 1 (the reader's): VR code in its own files**; an existing file gains
only a call at its seam. **Rule 2: everything VR is under `#if OMK_VR`**,
defined only by the main `engine/Makefile` (`VR ?= 1`). The Vita, classic
Mac, 3DS and PowerPC builds compile `src/vr/` empty and link the `_off`
stubs, with their build files untouched - a step-4 Android build must
define `OMK_VR=1` itself.

§5's file list in `quest-port.md` was written before the code and names
`backends/sdl/playvr_*` and `vrcamera/vrfight/vrmove/vraim`; what exists is
the layout above (the fight, move and aim halves live in
`playvr_camera.cpp`, small enough not to split yet).

## 3. Recipes

**Look at it** (numpad 4/6 yaw, 8/2 pitch, 7/9 roll, 5 recentre, 1/3 snap
turn; the mouse moves the fake controller in shoot mode):

```bash
cd engine && make play
build/omk-play ../gamedata ../tables --save ../traces/save-appart.bin \
    --area 0 --stand 1804,0,-6890,336 --vr-sim          # the street, first person
build/omk-play ../gamedata ../tables --area 230 --scene-chunk 56 \
    --shoot-health 1000 --vr-sim                        # shoot phase from frame ~394
build/omk-play ../gamedata ../tables --fight-supermarket --vr-sim   # a fight
build/omk-play ../gamedata ../tables --area 222 --scene-chunk 55 --vr-sim  # Impasse cuts
build/omk-play --vr-help                                # every --vr-* flag
```

**Test instruments**: `--vr-head=Y,P,R` (a fixed head - zeroed by the first
recentre, see traps), `--vr-head-after=F:Y,P,R` (turn it from frame F),
`--vr-aim=Y,P` (the fake controller), `--vr-headpos=X,Y,Z`, `--vr-ipd=MM`,
`--vr-fov=quest2`. `OMK_VRLOG=1` prints one `[vr] frame N kind K target ...
authored ... drawn ... look ... origin ... him ... facing F pitch P` line a
frame; the recentres and the aim log without it.

**Checks** (all `--slow`, so through `--only`; all shown to fail):

```bash
python3 tools/verify.py --only "engine: vr" "licence headers"
```

`vr camera rule` (the probe), `vr frame` (flat == mono identity, the halves
drawn and different), `vr modes` (first-person walk, still vs moving,
Impasse cuts, the calm fight), `vr aim` (the controller's facing and pitch,
the shots, the head not aiming). `licence headers` counts **590** authored
files - a new VR file moves it.

## 4. Waiting on the reader - the headset

**The build no longer waits** - `scripts/android-build.sh` finds the toolchain
(it differs PER MACHINE: the M1 had no NDK; the M3's Unity installs each
bundle NDK r27c, the SDK platforms, build-tools and a JDK) and builds the APK.
On a machine with none of those: install the command-line tools and
`sdkmanager 'ndk;27.2.12479018'` (~3 GB), or set `ANDROID_NDK_HOME`.

1. **Developer mode on the Quest 2**, the USB cable, `adb devices` listing it
   (`adb` is `/opt/local/bin/adb` on the M3, `/opt/homebrew/bin/adb` on the M1).
2. **The data**, 1.7 GB, sideloaded, never in the APK:

```bash
scripts/android-build.sh install            # build + adb install -r
adb push gamedata /sdcard/Android/data/org.omk.play/files/gamedata
adb push tables   /sdcard/Android/data/org.omk.play/files/tables
adb logcat -s OMK                           # the game's stdout/stderr
```

   (`gamedata` is wherever `python3 tools/omkpaths.py` says it resolved.)
   Extra flags go in `.../files/args.txt`, one a line.

## 5. What to do next, in order

1. **Step 4 is done.** To run it again: `scripts/android-build.sh install`,
   launch OMK from Library > Unknown sources, `adb logcat -s OMK`. `[in]`
   lines are the raw input log (`android_main.cpp`, the first 600 events).
2. **Step 5** - `backends/openxr/` (a frontend like `sdlfront.*`):
   `XR_KHR_android_create_instance`, `XR_KHR_opengl_es_enable`, the LOCAL
   space, a GLES swapchain per eye, the frontend PACING (skip the 30/60
   pacer, `playframe_present.cpp`), controllers into `pad::Pad`, hands and
   eyes into `vr::HeadPose`, pause when not FOCUSED, the traffic listener
   (`session.sliders().setListener(view.cam.eye)`) following the head.
   `headPose()` returning true is all the camera code needs - it already
   runs on whatever `HeadPose` arrives.
3. **Step 6** - the interface on an `XrCompositionLayerQuad`, the four
   screen kinds' VR forms (the table in §5 step 6), the system keyboard.
4. **Step 7** - the play pass; write what it decides back into §5.

**Open for the play pass** (recorded in the step blocks): shoot-mode movement
follows the gun, not the head; the body turns instantly with the controller;
only the right hand aims; the calm fight camera takes ~4 s to catch up after
the game's 120° re-placements (a fade-cut may be better); the original's
own first-person key (Aventure bit `0x80`, key L) is not read.

**Not in this prototype**: §4's preload, multiview (the GLES backend is GLES2
/ `#version 100`; `GL_OVR_multiview2` needs GLES 3 and `#version 300 es` -
it IS available on GLES, the reader corrected a "Vulkan only"), standing
play (the `Space` seam is there), menu pointing.

## 6. Traps that cost time

* **The C++ runtime must stay STATIC on Android** (`c++_static`): the
  profiler replaces `operator new`, and a shared libc++ loaded first keeps the
  system allocator for its own code - the first device run died in `free()`.
* **A 2D panel gets no controller input** but the laser trigger (a touch).
  Do not debug the pad path in the flat build; it receives nothing.
* **`adb` differs per machine**: `/opt/local/bin/adb` on the M3. The NDK is
  found per machine by `android-build.sh` (a Unity install's on the M3).

* **The recentre zeroes a fixed head.** The first kind change recentres, so
  `--vr-head` from frame 0 is undone and a "turned" run equals the flat one.
  Turn after it: `--hold k77*N` (numpad 6; the sim reads `st.keyboard` as
  well as the held keys) or `--vr-head-after=F:...`.
* **First person is the default**, so an identity comparison against the
  flat game needs `--vr-adventure=authored`.
* **The shoot phase starts at frame 394** in the supermarket chunk; a
  300-frame run never reaches it. Use 700, and make a `--hold` cover the row
  the check reads.
* **Shoot mode binds numpad 4/6/8/2** - the same keys as the fake head. The
  VR shoot input strips them from the game's word; a desktop run without
  `--vr-sim` still uses them.
* **Changing `VR=` wipes `build/obj`** (the `.vr` marker): `OMK_VR` changes
  `RCamera`'s and `PlayState`'s layout, so mixed objects would be silent
  corruption. Expect a clean build each way.
* **Mutations can mask each other** (the aim-from-head mutation hid the pitch
  flip); run the ones that touch the same value alone. After restoring one,
  `touch` the file (make 3.81's one-second clock, CLAUDE.md §1).
* **Other sessions commit to this tree at the same time** (the 3DS port
  among them): commit only the VR lines, check `git diff --cached` first.
* **zsh**: flags in a variable need `${=var}`.
