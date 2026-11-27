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
  objet mobile dans la scène de tests, pas de problèmes vu"* - **OPEN**: a
  door or a moving mesh, drawn through the copies' `pending` corners, is
  still to be watched on the console (what changed Anekbah's set each frame
  was not seen moving).
* **WHERE THE STREET STANDS** (the original, enhancements off, two frames
  in flight, 2026-11-27): deko3d **work 8.2 ms (122 fps unpaced), GPU 7.2,
  submit 2.0** - GLES 16.6 (the refresh's floor: its swap waits), GPU 8.5,
  submit 1.3. This morning deko3d was 31.9 / 22.7. Left on the CPU's side:
  `pedestrians, traffic` 2.7, the bodies posed on the CPU (D3: on the GPU).

  The rest of D2: the interface as OVERLAY
  planes over the GPU world (`glesPresentOverlay`'s C/M planes and fade -
  Vulkan reads back instead), the cloud under the menus (`under.frag`),
  frames in flight, the GPU timer (`dkCmdBufReportCounter` timestamps ->
  `gpuFrameMs`, so `--bench` compares GLES and deko3d).
* **D3 - the platform**: touch from libnx, docked 1920x1080 / handheld
  1280x720 (the swapchain remade on the operation mode), the posed bodies on
  the GPU (set 2), the depth tie baked.
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
