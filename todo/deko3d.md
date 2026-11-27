# deko3d - the Switch's native GPU backend

The reader's call of 2026-11-27 (*"passe sur deko3d"*), after the GLES path
through Mesa reached the Switch's floor: the remaster's rain at GPU 14.3 ms,
its frames still missing the refresh with what is left PAST the GPU timer
(the swap, Mesa's own work - `switch-port.md`, "THE REMASTER'S 3D RAIN, ITS
VEILS"). The plan of 2026-11-26 already said how: **deko3d starts from the
VULKAN backend** (it is Vulkan-shaped, and that one has the remaster and the
tiled lights), and every reordering of passes the Vulkan backend deferred
(the dust once a pixel, `switch-port.md` "VULKAN'S DUST") goes in here.

## What deko3d is, against Vulkan

| Vulkan (`backends/vulkan/vkrender.cpp`) | deko3d (`backends/deko3d/`) |
|---|---|
| instance, physical device, queue family | `dk::Device` + one `dk::Queue` (graphics) |
| `VkDeviceMemory`, memory types | `dk::MemBlock`: CpuUncached/GpuCached (buffers, uniforms), Image, Code (shaders) - a GPU address each |
| pipelines (one per blend x variant) | NONE: state objects (rasterizer, colour, blend, depth-stencil) bound in the command list, per draw if need be |
| descriptor sets | per-stage bindings: a UBO / SSBO by GPU address, a texture by `dkMakeTextureHandle(image, sampler)` into descriptor arrays |
| push constants (128 bytes) | `pushConstants`: a UNIFORM BUFFER written inline in the command list |
| render passes, subpasses, barriers | `bindRenderTargets`, `barrier(...)` |
| `VkSwapchainKHR` on an SDL surface | `dk::Swapchain` on libnx's `nwindowGetDefault()` |
| SPIR-V through glslc | DKSH through `uam` (Mesa's GLSL front end) |

The device is made with **DepthZeroToOne | OriginUpperLeft | YAxisPointsDown**,
Vulkan's conventions, so `scene.vert`'s matrix and `kCullSign` carry over
unchanged.

## The shaders: Vulkan's, translated (D0, DONE 2026-11-27)

`backends/deko3d/dkshaders.py` rewrites each `backends/vulkan/shaders/*`
file for `uam`, every rewrite ASSERTED: the push block -> uniform buffer 0
(std140, which lays out every block here exactly as std430 - the script
refuses an array or a struct in one); `set = S, binding = B` -> `binding =
S*6+B` (deko3d binds per stage and kind, 16 buffers); `gl_VertexIndex` ->
`gl_VertexID`; `#include` inlined (FSR's headers); `-D` written after
`#version`. All 14 modules (the scene's four fragment variants, the AO's two)
compile. `-DOMK_DEKO3D=ON` builds them into the NRO's romfs
(`romfs:/shaders/*.dksh`).

**Open: `uam` lowers a non-constant integer `/` and `%` to FLOAT with a
warning ("loss of precision")** - in `present.frag` (`divTrunc`, the
supersample's average), `under.frag` (the cloud's nearest scale, which the
CPU's `blt` reproduces EXACTLY and `OMK_VERIFY_GPU_CLOUD` compares bit for
bit) and the tiled lights' cell index. Judged on the console by those
verifies; if they differ, an exact integer quotient (a float estimate
corrected by one multiply each way) goes into the shared shaders.

## SDL without a window

devkitPro's SDL2 (`libSDL2.a`, read from its object code 2026-11-27 -
the source pages answered 404): `SWITCH_VideoInit` touches no `NWindow`
(it adds the display, starts psm and the touch / keyboard / mouse / swkbd
readers), but `SWITCH_CreateWindow` refuses without EGL ("EGL not
initialized") and ALWAYS puts an EGL surface on `nwindowGetDefault()` - the
window deko3d's swapchain must own. So under deko3d SDL's video is started
for its input and its `appletMainLoop` (in `SWITCH_PumpEvents`) and NO SDL
window is made. Pads come through the game controller as before. Lost:
`SWITCH_PollTouch` (and mouse, keyboard) send nothing without a FOCUS window
(`cbz` on `SDL_GetFocusWindow`) - so the touch screen is read by the deko
frontend from libnx (`hidGetTouchScreenStates`) and pushed as SDL events
(D3); the dock's resize (`SWITCH_PumpEvents` -> `SDL_SetWindowSize`) is the
backend's own to follow (D3).

## Steps

* **D0 - the shaders.** DONE (above).
* **D1 - first light, the original's picture.** `dkrender.cpp`: the device,
  a 1280x720 swapchain, the memory (a few large blocks, sub-allocated), the
  textures (the pool as Vulkan's `poolTextures`), the geometry's vertex
  buffers, the scene pass (`scene.vert` / `scene.frag`, the three blends, the
  cutout, the cull, the fog; set 1 and the pose buffer bound to stand-ins),
  `readback`, and the present (`present.frag`, the dither and the 565
  expansion) of the world and of a CPU-composed `Surface`.
  `playgpu_deko.cpp` the frontend, `make switch-deko` its NRO beside the
  GLES one. Judged on the console: Anekbah's street and a menu.
  **2026-11-27: BUILT, not yet run on the console** - `make switch-deko`
  links the NRO (11.3 MB, the 14 modules in its romfs) and `make switch`
  (GLES) still builds beside it; both files first type-checked clean on the
  Mac (`clang++ -fsyntax-only -Wall -Wextra` against the image's own
  `deko3d.h` and SDL2's headers). (A first build stopped on the external
  drive being full - the colima VM's writes failed, "Input/output error".)
  The layouts both backends fill moved, unchanged, into
  `backends/vulkan/shaderblocks.h` (the Vulkan viewer rebuilt on it).
  **ON THE CONSOLE, 2026-11-27 (the reader: "le jeu tourne normalement"):**
  the films, the menus and the game drawn by deko3d - the picture the right
  way up, the rooms not inside out (`kCullSign` 1 as reasoned), the textures
  in place, the pads answering with no SDL window. The remaster not drawn,
  as D1 intends (`takes` names the texture filter alone).
  * **The first run went BLACK and silent at the first present**: the
    frame's fence, signalled on the QUEUE after `dkQueuePresentImage`
    (`dkQueueSignalFence`), never signalled - the release library waited on
    it for ever; with a 2 s bounded wait (the debug build's trace) every
    frame took 2 s and the menu crawled. Signalled INSIDE the frame's command
    list instead (`dkCmdBufSignalFence`, as deko3d's own samples,
    `CCmdMemRing::end`), every fence came back 0. The trace
    (`OMK_DEKO_TRACE=N`, the first N presents step by step) and the bounded
    wait stay.
  * **Not yet a measurement**: Anekbah's street at 31-34 ms `sim+draw` (a
    60-frame mean) with the reader's `all = max` - against GLES's 34.9 for
    the original there with the enhancements (`switch-port.md`, the second
    sweep). Not comparable yet: deko3d's DEBUG library validates every call,
    the bodies are posed on the CPU, one frame at a time, no MSAA, no
    trilinear. The films drop two frames of three to keep up with the sound
    (EIDOS 71 in 2.9 s): a surface is expanded 565 -> RGBA on one core into
    uncached memory and copied, a frame waiting for the one before.
  * `OMK_DEKO_CLEAR=1`, an instrument: the swapchain image cleared MAGENTA
    under the present pass (seen magenta: the images reach the screen and the
    pass draws nothing).
* **D2 - the Switch's frame as GLES draws it** (first: the RELEASE library
  for a measurement, `-DOMK_DEKO3D_DEBUG=OFF`, and the GPU timer).

  **MEASURED 2026-11-27** (the console, Anekbah's street, `save-appart`,
  `--bench` with `OMK_BENCH_STEPS=original|original, enhancements off` -
  steps chosen by NAME now, `|`-separated): **GLES** work 16.6 ms (the
  refresh's floor), GPU **8.5**, either step. **deko3d** work 31.9, GPU
  **22.7**. The GPU timer (`gpuFrameMs`): four `DkCounter_Timestamp`
  reports, the scene's list and the present's, the swapchain wait between
  them left out; each report 16 bytes, the timestamp its SECOND word (read
  on the console, not assumed). `OMK_DEKO_PROBE` (via `OMK_BENCH_VARY`), the
  GPU time with one part taken away: a trivial fragment shader 13.1; no
  pixel (1x1 scissor) 12.5; no cutout-shader switch 23.1; one push a draw
  22.9; a ring of uniform blocks instead of `pushConstants` 22.7; **NO DRAW
  AT ALL 12.3**; no pixel nor state change 12.5. ~88 draws, ~25,000 vertices
  a frame (`dekoTakeCounts`) - GLES draws 86. So a FIXED 12 ms a frame, and
  `OMK_DEKO_LADDER` (a timestamp at each rung) put it in the PRESENT PASS's
  draw: 12.05 ms (the clears 0.31, the barrier 0.3, the state 0.01).
  **The cause: thirteen integer divisions in `present.frag`.** Maxwell has
  no integer divide and `uam` made each a long sequence a pixel (the menus'
  RAW path, fewer of them, presented in 3 ms). Every divisor there is a
  constant or a power of two: each is now a shift or a multiply, exact over
  its domain (`a / 255 = (a*257 + 257) >> 16` for 0..65535, checked
  exhaustively; the Bayer table its bits) - in the SHARED shader, Vulkan's
  result bit for bit the same. The present 12.05 -> **2.73 ms**; deko3d's
  GPU 22.7 -> **13.3**, the work 31.9 -> 22.4. What is left: the scene's
  fragment shader (~9.5 ms: Vulkan's, every remaster feature behind uniform
  branches, 49 registers - `scene.vert` 22), and the CPU waiting for the
  GPU (one frame in flight: "submit" 16 ms). Both are next.
* **THE RELAUNCH CRASH** (2026-11-27, the reader: *"crash au lancement"*):
  the launch after a `--bench` run that RETURNED crashed in
  `dkDeviceCreate` -> `nvInitialize` -> `tmemCreate`'s memset (a data abort
  at 0x752f3f7000, in the heap - Atmosphère's report symbolised with the
  build's ELF). hbloader loads the next program into the SAME process, and a
  device never destroyed keeps nvservices' transfer memory, whose pages
  inside the next program's heap stay locked. The frontend never deletes
  its renderer (GLES needs nothing: `SDL_Quit` closes EGL), so
  `playgpu_deko.cpp` closes the device in `gpuFinishReport` (the run's end,
  said in the log: "the device closed at the run's end (57.7 ms)"), a static
  owner beside it for another exit. The relaunch then came up and ran.
* **`tools/switch_link.py` sent EVERY NRO to `omk/omk.nro`**: its program
  arguments were a `REMAINDER` positional after `nro`, which swallowed
  `--path` and `--log` given after it (they reached the game). Fixed: the
  program's arguments are what follows `--`, split off before parsing. The
  GLES `omk.nro` was put back on the card.

* **THE ORIGINAL'S MODULE** (2026-11-27): `scene.frag` `-DOMK_ORIGINAL`
  (and with OMK_LATE_DEPTH, the cutout's) leaves out every block a draw
  with no per-pixel light, no water or wet bit and no bloom, in a frame with
  no mist, dust or veil (and no mapped shadow - not drawn here yet), would
  run as a no-op of the full shader: the game's own shading is what is left
  (the cull, the cutout, the shimmer, the grey bank, the fog). 2 KB and 9
  registers against 26 KB and 49. Vulkan never defines it: its four
  `scene.frag` modules are byte-identical (glslc before and after). The
  street: 88 of 88 draws take it; the scene's GPU 9.8 -> 3.5 ms. The A/B on
  the console (`OMK_DEKO_FULL=0,1` through `OMK_BENCH_VARY`; read by
  `envOn` - `envFlag` is "set", and `=0` is set: the first A/B drew the
  full shader on both sides): GPU **6.6 ms** against 13.5 - GLES 8.5.
* **TWO FRAMES IN FLIGHT** (`OMK_DEKO_SLOTS`, default 2): a slot holds a
  frame's command memory, fence, timestamps, surface staging and retired
  buffers; a frame waits for its slot's LAST frame alone. A geometry's
  changed contents go into a NEW buffer, the old one freed when its slot
  comes round (a frame in flight may still read it - the bodies posed on the
  CPU change every frame). With the full shader the street's work went 22.4
  -> 16.1 ms (the GPU the same, 13.5); with the original's module 16.1 ->
  **15.5 ms** (65 fps unpaced), GPU 6.6. The reader, on the picture of those runs:
  *"je n'ai pas tout regardé, mais je n'ai pas vu de problèmes"*.
* **THE CPU'S SUBMIT, MEASURED** (`deko3d CPU` in the 60-frame report: the
  slot's fence waited, the submit, its vertex uploads): 8.9 ms a frame, of
  it 23 uploads of 7.5 MB in 8.6 ms - NOT the bodies (the suspect): the
  whole of Anekbah's set re-sent each frame for its moving meshes, where
  GLES sends ~72 KB (`dirtyCorners`). With frames in flight a buffer may
  not be written in place, and Vulkan's copy-on-write (a second buffer, the
  first COPIED into it) would read 7 MB of CPU-uncached memory back here.
  So a static geometry keeps one buffer, and one whose contents change gets
  ONE COPY A SLOT, each keeping the union of the `dirtyCorners` it missed
  (`pending`) - or `whole` after a revision without them, or past a quarter
  of the corners: a frame writes its own slot's copy alone, and only what
  changed. Submit 8.9 -> **1.6 ms** (1.3 MB a frame left: the bodies posed
  on the CPU, whole). The reader on that run: *"il n'y avait que les piétons comme
  objet mobile dans la scène de tests, pas de problèmes vu"*; and then,
  checked on purpose: *"door are corrects"* - the copies' `pending` corners
  draw a door moving as it should.
* **WHERE THE STREET STANDS** (the original, enhancements off, two frames
  in flight, 2026-11-27): deko3d **work 8.2 ms (122 fps unpaced), GPU 7.2,
  submit 2.0** - GLES 16.6 (the refresh's floor: its swap waits), GPU 8.5,
  submit 1.3. This morning deko3d was 31.9 / 22.7. Left on the CPU's side:
  `pedestrians, traffic` 2.7, the bodies posed on the CPU (D3: on the GPU).

* **THE BODIES POSED ON THE GPU** (2026-11-27, D3's item done early - the
  reader's *"do the two optimizations"*): Vulkan's pose block word for word
  (a header, 32 lights, three vec4 an affine, the body light's tint) in the
  SLOT's pose buffer (2 MB, the CPU writes it each frame), bound at storage
  buffer 12 (`scene.vert` set 2) per posed draw; the draw's corners are its
  REST corners, static, uploaded once. A body past the buffer is posed on
  the CPU (`cpuPose`, Vulkan's). The street (the original, enhancements off):
  work 8.5 ms (118 fps), GPU 7.0; `pedestrians, traffic` 2.7 -> **0.7 ms**;
  the uploads 1.3 MB -> 0.1 MB a frame, the submit 1.6 -> 0.4 ms - the CPU
  now waits ~3.8 ms for the GPU, the GPU the frame's wall again.
* **THE INTERFACE AS AN OVERLAY** (2026-11-27, the rest of D2): GLES's and
  GL1's planes - `resolveOverlayPlanes` leaves C (the CPU's 565 frame, its
  world pixels the KEY) and M - laid over the world the present pass just
  drew, `C + world * M`, the colour fade over all (`backends/deko3d/shaders/
  overlay.frag`, deko3d's own module, its CMake row `deko/overlay.frag`). C
  is read as its 565 WORDS (R16_UINT) and widened by replication, not by a
  UNORM read. The planes go up from the slot's staging each frame (2.7 MB,
  behind a full barrier: the last frame's overlay read the same images). A
  frame with a HUD, a subtitle, a conversation or an open screen then needs
  no readback. The reader's session in Anekbah: whole runs of frames through
  it (up to 60 of 60), none refused, the device closed at the end (41146
  frames). The reader on both, posed bodies and overlay: *"Tout était
  bon"*.

  The rest of D2: the interface as OVERLAY
  planes over the GPU world (`glesPresentOverlay`'s C/M planes and fade -
  Vulkan reads back instead), the cloud under the menus (`under.frag`),
  frames in flight, the GPU timer (`dkCmdBufReportCounter` timestamps ->
  `gpuFrameMs`, so `--bench` compares GLES and deko3d).
* **D3 - the platform**: touch from libnx, docked 1920x1080 / handheld
  1280x720 (the swapchain remade on the operation mode), the posed bodies on
  the GPU (set 2), the depth tie baked.
* **D4a - THE REMASTER'S FIRST PART** (2026-11-27, the reader's *"Fais D4"*):
  the pack (`takesPack`, the codecs BC and ASTC: Maxwell samples both; a
  block file's levels copied as they are, every one under trilinear - the
  sampler's mip filter, `kSampTrilinear`), the per-pixel and the emitters'
  lights (`drawsPixelLights`, `drawsEmitterLights`, 64 in the frame's light
  block - the tiled points wait for D4d), the mist, the wet floors and the
  3D rain (`setWater`: the shader's, from the draws' bits and the frame's
  block), the body light (the pose block carries its tint), and
  `retexture` - the mode switch drops every texture and the pool comes again,
  from the pack or as shipped. The street: 31 of 35 textures from the pack's
  block files, uploaded in 7.8 ms; original work 8.5 ms (GPU 6.9),
  **remaster 15.1 ms (66 fps), GPU 14.7** - at the native 720p (no upscale
  yet), and with no bloom, grade, occlusion or reflection. The reader, after
  switching modes back and forth: *"pas de problèmes pour l'instant"*.
* **D4b - THE UPSCALE** (2026-11-27): the scene drawn `rs()` smaller in
  `colour_`'s top-left, brought to the output's size by EASU into `easu_`,
  sharpened by RCAS in the present pass, then dithered; the upscaled readback
  through both, raw; and `remasterConfigure` called before `init` (it never
  was: no upscale, no bloom could reach deko3d).
  * **THE CYAN PICTURE** (the reader's photo of the remaster session): AMD's
    EASU reads through `textureGather`, and on deko3d / Maxwell the gather
    returns the wrong components - `uam`'s TGSI asks components 0, 1, 2
    correctly, the picture comes out cyan anyway. Two frames captured on the
    console (`--frames 300 --dump`, the readback through EASU, fetched with
    the launcher's `--b64`): gathered, the reader's cyan; the same four
    texels FETCHED (`easu.frag` -DOMK_NO_GATHER), the street as it is.
  * **ITS COST** (Anekbah, the ladder with a rung after EASU): AMD's header
    through `uam`, fetched or gathered, and GLES's float transcription
    (`backends/deko3d/shaders/easu_lite.frag`, and RCAS under
    `present.frag` OMK_RCAS_LITE - the default here, `OMK_DEKO_FSR_AMD=1`
    for AMD's) cost the same: the upscaled present **9.9 ms, EASU 6.36 of
    it**, RCAS's present 3.58, against the native present's 2.73. Not the
    reads either (the scene target without `Usage2DEngine`: no change). On
    this GPU at the handheld clock EASU over 1280x720 costs about twice the
    original's whole native scene, so the frame is SLOWER upscaled: original
    13.1 ms (GPU 12.8) against 8.5 native, remaster 16.6 against 15.1. It
    pays only where the scene at the output's size costs more than ~10 ms
    over the drawn one's - the remaster's full-screen passes (D4c-e) may
    bring it there. GLES's whole frame with the same upscale is 8.5 ms: its
    compiler (Mesa's GLSL -> NIR) makes better code of the same arithmetic
    than `uam`'s TGSI path - not measured, inferred.
  * **NATIVE UNDER THE PRESET** (the reader's choice, 2026-11-27): deko3d
    does not take the preset's 1.5 (`kPresetUpscale`, GLES's on the Switch),
    a value set by hand still is (`--upscale`, `[Remaster] upscale`, OMK's
    menu) - `playgpu_deko.cpp`, said in the log. The street: original 8.1
    ms (124 fps), remaster 15.0. To be weighed again once D4c-e's
    full-screen passes raise the scene's cost.
* **D4c - the bloom and the grade (2026-11-27, on the console).** Vulkan's
  `recordPost` (`post.frag` itself, untranslated beyond `dkshaders.py`):
  with a glow or a grade the scene draws into `postSrc_` (RGBA8, the alpha
  the texel's light under the bloom), the glow's extraction and two blurs
  at a quarter of the size (320x180, RGBA16F), the halos in the rain at that
  size, then the final pass - the scene, the glow screened over it, the
  grade's LUT - into `colour_`, which the present pass reads as before. The
  pack's LIGHT MAPS (`_e`, BC4 blocks or R8 bytes) go up with their texture
  and are bound per draw at binding 1; the glow only when a draw bound one
  (Vulkan's `lightMapNow_`). No ambient occlusion: GLES's Switch draws none.
  * The street, `--bench` (bloom 1.6, the ANEKBAH grade): original 8.5 ms,
    unchanged (no post-pass without a glow or a grade); remaster 18.5 ms
    (54 fps), GPU 18.2, against 15.0 before - the glow and the grade 3.5
    ms. The ladder's scene rung (the post-pass recorded inside it): 14.45
    ms. A captured frame (`--frames --dump`, fetched with `--b64`): the
    lamps' and the fire's glow, the grade's colour - the picture right.
  * Not yet: the remaster's log lines still say "drawn by Vulkan and GLES
    only" (`texpack.cpp`, `playremaster_setup.cpp`) - true of the desktop
    builds, to be widened when D5 makes deko3d the Switch's default.
* **D4d - the anisotropy and the mapped shadow (2026-11-27, on the
  console).** RE-SCOPED from "the tiled lights and the relief": GLES's
  Switch draws neither, and the card's pack carries no normal maps (the
  relief's) - so D4d took what GLES draws there and deko3d did not
  (`kTakesAnisotropy`, `kTakesShadowMap`); the tiled lights and the relief
  stay beyond GLES, after D4e.
  * THE ANISOTROPY: the trilinear sampler's `maxAnisotropy` (GLES's and
    Vulkan's rule: only behind trilinear), set before `init`, moved by
    `retexture` with no frame in flight. The preset's 16x on the street:
    GPU 19.5 ms against D4c's 18.2 - about 1.3 ms.
  * THE MAPPED SHADOW: Vulkan's `shadowPass` (`shadow.vert`, a 1024x1024
    ZF32 map, 3x3 PCF in `scene.frag`); the frame's list opened by the pass,
    a barrier before it (the frame before still sampling the map) and after;
    the original's module off on a frame with a live map (it has no
    `litness`). Two departures from Vulkan, both measured:
    * THE CASTERS posed here, POSITIONS ALONE, into one buffer drawn once.
      Vulkan's `cpuPose` (first used) poses AND LIGHTS every corner of the
      draw's whole geometry and uploads it whole, once a caster: the street
      went to 62 ms (original) and 195 ms (remaster) of CPU a frame.
    * NO Y NEGATION: drawn with the scene's negated second row, the street
      gained a long false streak of shadow (2441 px darker than the fitted
      frame), Vulkan's same frame on the M1 none (mapped against fitted, 2
      px); drawn with Vulkan's matrix as it is, 385 px - the anisotropy's
      and the walkers' noise. So `OriginUpperLeft` maps Y into this target
      as Vulkan does. OPEN: why the scene's projection then needs its
      negation (`pushView`) and lands upright - the present chain is not yet
      read for the step that compensates. `OMK_DEKO_SHADOW_FLIP=1`, the A/B.
  * The street, `--shadow-quality mapped`: original 19.2 ms (GPU 19.0),
    remaster 29.2 (GPU 27.2), remaster with the shadows fitted 19.8 -
    GLES's sweep paid ~11 ms for the map (53.5 against 42.7). The most of
    it is the FRONTEND's, every backend's: with a map no body is culled by
    the view's sides (`sideCullBodies`, `cornersUnread` - an off-screen
    caster's shadow can fall in view), 155-213 bodies posed a frame against
    14-23, for 9 in the slab. A cull to the slab (bodies near its centre
    kept) would serve every backend - not done here. The Switch's preset
    keeps the shadows fitted.
* **D4e - the water and the floors' reflections (2026-11-27, on the
  console).** Vulkan's `drawWaterScene`: the draws above the plane through
  the reflected camera into `refl_` (the render size; RGBA8), the frame's
  depth serving both passes; then the frame, the water's draws (mode 1)
  and the reflecting floors' runs (mode 3, `Draw::reflect`) adding it at
  `gl_FragCoord` - the reflected camera goes through `pushView` as the
  frame's does, so no axis needs turning. The floors' reflection at half
  size in the image's top-left quarter. `begin` may now run twice in one
  frame (`inFrame_`): the second keeps the list, the pose buffer and the
  counts - a reset there would have let the frame's bodies overwrite the
  reflection's poses before the GPU read them.
  * Anekbah's street has neither (no "reflection's target" line): the
    places are JAUNPUR'S CANAL (area 1, `10524,-40,10284,180`, the water)
    and QALISAR'S NEON STREET (area 101, `15300,0,-10050,270` - the card's
    `floors.txt` lists Qalisar's three floors alone).
  * The canal: original 9.6 ms; remaster 34.9 (29 fps), water off 24.2 -
    the water's whole-size reflection 10.7 ms. Qalisar: original 8.4;
    remaster 25.0 (40 fps), floors off 19.6 - the floors' half-size one
    5.4 ms. Two frames captured on the console: the canal's railing and
    wall mirrored in the water, Qalisar's walkers and green sign in the
    floor, each the right way up.
  * THE SECOND REFLECTING LEVEL (`drawTwoReflections`, `[Remaster]
    reflections = 2`, the card's preset): Vulkan's - a second reflected pass
    into the image's top-right quarter (its clear scissored to it, the
    first's kept), a level-2 floor's run bit 22.
  * ITS OCCLUSION TEST (`secondLevelSeen`, Vulkan's and GLES's probe): in
    a floors' frame the second level's runs drawn again after the scene,
    depth LEQUAL, neither depth nor colour written, the samples counted
    (`DkCounter_SamplesPassed` reset, then reported into the slot's stamps
    report 14) and read when the slot comes round - two frames late with
    two in flight, Vulkan's one. On the console: the LAMP STREET draws the
    second pass once (frame 2, the level's fade still 0) and never again -
    Vulkan never; the RAISED WALK draws it from frame 2 to the end, one
    frame off at 3 while the camera settles - Vulkan from frame 3.
    * Qalisar's raised walk (`15000,-140,-10500,270`): remaster 28.0 ms
      (36 fps) with both levels, floors off 19.4 - the two 8.6 ms.
    * The proof is `engine: floor levels`' own: on the LAMP STREET
      (`10341,-260,-9507,90`) `OMK_SWAP_LEVELS=1 --reflections 2` (the
      street reflected through the SECOND pass and quarter) against
      `--reflections 1`: 0 pixels differ on the console, 0 bytes on
      Vulkan (M1) - a frame whose reflection moves 42768 pixels on Vulkan
      (`--reflections 0` against 1). On the raised walk, `--reflections 2`
      against 1 moves 1 pixel on the console and 1 on Vulkan with the
      card's `floors.txt` (the check's own list moves >1000) - no proof
      there either way.
    * deko3d against Vulkan at the lamp street, one level: 14419 pixels
      past 48 levels - not chased (the runs' anisotropy and packs differ:
      the card's against the M1's `~/omk-pack`).
* **D4 - the remaster**: the per-pixel and tiled lights, the shadow map, the
  pack (ASTC natively - Maxwell samples it), water and the two reflections,
  bloom and grade, the 3D rain and its veils, the dust once a pixel (the pass
  Vulkan deferred). Each measured against GLES on the console with `--bench`.
* **D5 - the default**: when deko3d is ahead of GLES on every bench row and
  equal in the picture, `make switch` builds it and GLES stays a flag.

## Traps

* The image's `uam` is at `/opt/devkitpro/tools/bin/uam`; the colima VM
  mounts only the repo and `/Volumes/omk-devtools/switch` (a scratch folder
  elsewhere reads as "Could not open input file").
* `uam` has no include path and no `-D` - the translator does both.
