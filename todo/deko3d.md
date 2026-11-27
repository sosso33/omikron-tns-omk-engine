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
  for a measurement, `-DOMK_DEKO3D_DEBUG=OFF`, and the GPU timer): the interface as OVERLAY
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
