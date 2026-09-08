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
| 5 | **fitted shadows**: the same blobs, laid on the surface actually under them instead of on a flat quad at the probed height | `shadowquality = fitted` / `--shadow-quality fitted` | **done 2026-09-09**; `engine: fitted shadows` |
| 6 | **mapped shadows**: a real shadow map, cast by the set's own authored lights, characters only | `shadowquality = mapped` / `--shadow-quality mapped` | **done 2026-09-09**; `engine: mapped shadows` |

## Rows 5 and 6 - the shadows

**One key with levels, and it is SUBORDINATE to the game's own option.**
`displayshadows` (row 5 of the options menu, `[Preferences]`) says whether
shadows exist at all; `shadowquality` says how they are drawn. With the option
off the enhancement draws nothing. `classic` is the default and must render
byte-identical to what ships now - that is the first of the three checks.

### 5, fitted - DONE 2026-09-09

The blob follows the ground. `Shadow_EmitBoneBlob` lays the model quad at
`floor - 1` with every corner sharing that y, so at a step the shadow is cut
off dead at the lip; fitted subdivides it **4x4** and lays each of the 25
vertices on the surface probed under IT. On the bank's entrance stairs the
player's own blob spreads **10.7 units** vertically - one 30 cm riser - where
classic spreads 0.00, and on the flat street both are 0.00.

The cost is paid by gathering, not by probing: `soupInBox` takes the
triangles under a body ONCE and the 25-vertex grids probe against those. That
is fewer city-wide scans per body than the unfitted path already did.

Two things declared with it:

* **This one is NOT backend-gated, which departs from the rule at the top of
  this file.** The change is to the geometry the port generates, not to how a
  backend rasterises it, and making the two backends build different geometry
  would destroy the property the renderer boundary rests on - that they draw
  the same picture from the same decisions, which `mirror pass` and
  `engine silhouette` measure at 0.998 and 0.995. It is off by default, so the
  software reference still draws what the original drew, which is what the
  rule protects.
* **A per-vertex drop is CLAMPED** to the blob's own half-width, so slopes up
  to 45 degrees are followed exactly and a blob overhanging a ledge does not
  stretch down the drop. This port's number, not the engine's.

**The stacking is NOT fixed, and it turned out not to be geometry's to fix.**
Ten blobs overlap under a standing character and each multiplies under
`dst x (1 - src)`, so the pool goes nearly black. Taking the MAXIMUM instead
of the product needs somewhere to accumulate coverage - a buffer or a stencil
- which is a backend feature and not a shape. Culling the blobs that lie
inside a larger one does not stand in for it either: the contained ones are
the shins, which are the DARKEST because they are nearest the ground, so
dropping them lightens exactly where a shadow should be strongest. Row 6 gets
it for nothing, because a shadow map is a visibility test rather than an
accumulation. A stencil pass on the Vulkan backend, blobs drawn darkest
first so "first wins" is "max wins", would also do it and would be properly
backend-gated - written down here rather than done.

Remainder: the CROWD's blob is still `Slider_PlaceShadow`'s single node
turned to the floor normal, under both qualities. It is already
ground-aligned, which is the fault fitted fixes, so it gains least; on a
stair it would still float.

### 6, mapped - DONE 2026-09-09

Built as designed: an optional light on `View`, a `castsShadow` flag on
`Draw`, `Renderer::shadowPass` defaulting to a NO-OP so the software reference
ignores the whole thing, a depth-only Vulkan pipeline into a 1024x1024 map, and
a 3x3 PCF lookup in `scene.frag`. The slab is ORTHOGRAPHIC and fitted to the
casters, so a fragment outside it is lit by definition and a characters-only
shadow cannot darken the far end of a street.

**The light is the set's own, and the data made that easy**: 153 of Anekbah's
155 `.3DO` lights point within 30 degrees of straight down - they are the
street lamps, at y -350 to -426 with a reach of 700-900 units. The pick is
`strongestLightAt`, `applyLights`'s own reach and falloff rules, asked at the
PLAYER. Standing under `LIGHT6` at (1954, -350, -3184) the shadow lands under
his feet, which is what a lamp overhead does.

Four things that had to be got right, three of them found by measuring:

* **`setTextures` RESETS the descriptor pool**, which frees every set
  allocated from it - the shadow's set 1 included. Allocated once at start-up
  it was destroyed by the game's first texture load, the fragment shader read
  a strength of 0, and nothing was ever drawn. The synthetic probe passed an
  EMPTY texture list, took the early-out and never reset, so it worked while
  the game did not: the bug lived in the difference between the two callers.
* **A `vec3` in a push-constant block is 16-BYTE ALIGNED.** `fogColour` had
  been packed at offset 76 in C++ against 80 in GLSL since the fog landed, and
  nobody saw it because the shipped fog colour is BLACK - the four misplaced
  bytes were zeros either way. Adding an `int` after it made it visible: the
  new field read padding and every character shadowed itself. A latent fault
  the enhancement flushed out.
* **A caster must not RECEIVE.** The engine's shadow only ever darkens the
  ground, and a map that also shadows the caster puts its own silhouette
  across a low-polygon character.
* **The slab is centred on the PLAYER, not on the cast's midpoint.** Eleven
  scattered bodies put that midpoint out in the road beyond every lamp's
  reach, so the light picked there was the one distant fill that reaches
  everywhere - 20 degrees below horizontal, whose shadow lands off the frame.

Declared: the slab radius (220 units) and the 1200-unit range that decides
which bodies are bounded are this port's numbers; a body outside the slab
casts nothing. `--world-vulkan` was added as a HARNESS - the world drawn
through an offscreen Vulkan renderer while the frame is presented on the CPU -
because `--vulkan` needs a real window and nothing GPU-only could otherwise be
measured headlessly.

### 6, as designed - where the light comes from

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
* ~~`fitted` and `classic` are IDENTICAL on flat ground~~ - **this was the
  wrong instrument and the first version of the check used it.** The classic
  blob is a four-triangle FAN with a centre vertex carrying the average of the
  four UV pairs and the fitted one is a grid with bilinear UVs, so the two
  sample the same disc slightly differently and differ by ~1500 pixels on
  perfectly flat ground. What is asserted instead is the geometric property
  the enhancement exists for: the VERTICAL SPREAD inside one blob, 0 for every
  classic blob by construction, and measured on the PLAYER's own blobs
  because the global maximum is dominated by a fixed staged body elsewhere in
  the city and would not move if his were wrong.
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
