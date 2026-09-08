# Enhancements - what the original never had, all OFF by default

The reader's rule (2026-09-08): anything the 1999 game did not draw stays
optional, behind a launch flag on `omk-play` or a key in the config file's
`[Enhancements]` section - a category of its own, beside the engine's
`[Preferences]` and this port's `[Options]` stand-ins for real menu rows.
Only the Vulkan backend draws them; the software reference stands where D3D
stood and draws what `sub_4638C0` told D3D to (`docs/ASSETS.md` 4).
Each one gets a `verify.py` check that pins the default OFF in the source and
measures the enhancement's own property on the GPU, shown to fail.

| # | enhancement | key / flag | status |
|---|---|---|---|
| 0 | anti-aliasing (MSAA 2/4/8) | `antialiasing = N` / `--aa N` | **done 2026-09-08**, `ed349ca`; `engine: anti-aliasing` |
| 1 | bilinear texture filtering. The colour key (flag 0x800, black) travels in the texture's ALPHA so a filtered sample is premultiplied: discard below 0.5, divide by alpha above - no dark fringe, and the nearest path is bit-identical to before | `texturefiltering = bilinear` / `--filter bilinear` | **done 2026-09-08**; `engine: texture filter`; judged by eye on Aapden's floor stain |
| 2 | mipmaps (trilinear) and anisotropic filtering, generated at upload; the alpha key averages correctly into the chain | `texturefiltering = trilinear`, `anisotropy = N` / `--filter trilinear --anisotropy N` | **done 2026-09-08**; `engine: mipmaps`; the stain judged by eye at 16x |
| 3 | interface scaling: linear or integer instead of nearest for the 640x480 layer | `uiscaling = linear|integer` / `--ui-scaling` | todo |
| 4 | unlimited draw distance: options row 3 is a CAP the port already runs the visible-set walk from; 0 lifts it. Authored risk: the sets end inside the fog | `clipdistance = 0` under `[Enhancements]` / `--clip 0` | todo |
| 5 | **fitted shadows**: the same blobs, laid on the surface actually under them instead of on a flat quad at the probed height, and composited ONCE instead of ten stacking multiplies | `shadowquality = fitted` / `--shadow-quality fitted` | todo |
| 6 | **mapped shadows**: a real shadow map, cast by the set's own authored lights, characters only | `shadowquality = mapped` / `--shadow-quality mapped` | todo |

## Rows 5 and 6 - the shadows

**One key with levels, and it is SUBORDINATE to the game's own option.**
`displayshadows` (row 5 of the options menu, `[Preferences]`) says whether
shadows exist at all; `shadowquality` says how they are drawn. With the option
off the enhancement draws nothing. `classic` is the default and must render
byte-identical to what ships now - that is the first of the three checks.

### 5, fitted - two faults that need no new pass

Both are visible in what landed on 2026-09-08 (`docs/ASSETS.md` 4d).

* **The actor blob is a flat quad at the probed floor height.** The crowd's
  node is turned to the floor's normal and the actor's is not, because
  `Shadow_EmitBoneBlob` does not turn it either - it lays the model quad at
  `floor - 1` and every corner shares that y. On a kerb or a stair it clips
  through or floats. Fitted projects each blob onto the triangles actually
  under it, which `o3de/collision.h` already supplies.
* **Ten blobs stack, and each MULTIPLIES.** Under `dst x (1 - src)` the pool
  under a standing character goes nearly black by construction. Faithful, and
  also the reason it reads as a hard dark circle. Fitted accumulates coverage
  across a body's bones once and applies it once - the maximum rather than the
  product.

No boundary change, no new pass. The cheap half of the work and the half a
player notices.

### 6, mapped - and where the light comes from

**Not an invented sun.** The port already reads the sets' light table
(`formats/light3do.*`): 4179 records of position, colour, two radii and an
intensity, with a direction the loader computes as `normalize(centre - pos)`.
Every one of `sub_4380B0`'s call sites is street life, so the lights that
light the crowd are the lights that should cast it. Where no light reaches a
body, fall back to straight down, which degenerates into the blob.

**Characters only, and this is a constraint rather than a saving.** A set is
shaded by a colour baked into every vertex (`ASSETS` 4c) and that colour
already contains the artists' shadows. A map that darkens the set from the set
fights lighting that is already there and double-darkens every corner. Casting
from actors and the crowd alone is also exactly what the game shadows.

**The boundary change is small**: an optional light on `View`, a `castsShadow`
flag on `Draw`. The Vulkan backend renders a depth pass from the light over
the casting draws and samples it; the software reference ignores both fields,
so PORTING A2's "the boundary itself moves 0 pixels" still holds for it.
Multi-pass has precedent - `drawWithMirror` already does a second full pass
with a GPU stencil - and the mirror is the thing to watch, because its
reflected pass has to sample the same map or reflections lose their shadows.

### The three checks

* `classic` renders byte-identical to the build before the option existed.
* `fitted` and `classic` are IDENTICAL on flat ground and differ only where
  the ground is not flat - which discriminates the fix from a general
  darkening, and a check that only measured "more dark pixels" would not.
* `mapped` must **MOVE**. A body between a known set light and a wall,
  rendered at two positions, and the dark region translates the way the light
  predicts. A shadow verified in one still frame is not verified (CLAUDE.md
  1, "some errors are invisible at rest") - and the shadows already shipped
  one fault of exactly that family, a blob six metres from its owner that two
  green checks could not see.

Known limit of 1 and 2, to be judged by eye: the sets sample sub-rectangles
of shared atlases (the Anekbah signs), and a filter reaches half a texel past
a rectangle's edge into its neighbour. Nothing per draw says where the
rectangle ends, so it is not clamped.
