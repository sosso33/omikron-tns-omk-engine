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
| — | **all of them at once**, each as high as it goes. A BASE: any specific key or flag beats it, whatever the order, and it touches nothing outside `[Enhancements]` | `all = max` / `--enhance-all` | **done 2026-09-09**; `enhance all` |
| 0 | anti-aliasing (MSAA 2/4/8) | `antialiasing = N` / `--aa N` | **done 2026-09-08**, `ed349ca`; `engine: anti-aliasing` |
| 1 | bilinear texture filtering. The colour key (flag 0x800, black) travels in the texture's ALPHA so a filtered sample is premultiplied: discard below 0.5, divide by alpha above - no dark fringe, and the nearest path is bit-identical to before | `texturefiltering = bilinear` / `--filter bilinear` | **done 2026-09-08**; `engine: texture filter`; judged by eye on Aapden's floor stain |
| 2 | mipmaps (trilinear) and anisotropic filtering, generated at upload; the alpha key averages correctly into the chain | `texturefiltering = trilinear`, `anisotropy = N` / `--filter trilinear --anisotropy N` | **done 2026-09-08**; `engine: mipmaps`; the stain judged by eye at 16x |
| 3 | interface scaling: linear or integer instead of nearest for the 640x480 layer | `uiscaling = linear|integer` / `--ui-scaling` | todo |
| 4 | unlimited draw distance: options row 3 is a CAP the port already runs the visible-set walk from; 0 lifts it. Authored risk: the sets end inside the fog | `clipdistance = 0` under `[Enhancements]` / `--clip 0` | todo |
| 5 | **fitted shadows**: the same blobs, laid on the surface actually under them instead of on a flat quad at the probed height | `shadowquality = fitted` / `--shadow-quality fitted` | **done 2026-09-09**; `engine: fitted shadows` |
| 6 | **mapped shadows**: a real shadow map, cast by the set's own authored lights, characters only | `shadowquality = mapped` / `--shadow-quality mapped` | **done 2026-09-09**; `engine: mapped shadows` |
| 7 | **per-pixel lighting**: the engine's OWN light law evaluated per fragment instead of per vertex, and received by every character rather than the crowd alone | `lighting = perpixel` / `--lighting perpixel` | **done 2026-09-09**; `engine: per-pixel lighting` |
| 8 | **the SETS receive the lights too.** Held back deliberately - it overrides authored art; see below | `lighting = sets` | not recommended |
| 9 | **supersampling**: render N times larger each way and average down. Reaches the CUTOUT edges MSAA never looks at, and the texture aliasing it cannot touch either | `supersampling = N` / `--ssaa N` | **done 2026-09-09**; `engine: supersampling` |

## Not an enhancement, and it came out of this list: THE SHIMMER

Written down here because it was found while looking for enhancements and is
the opposite of one. Mesh flag `0x8000000` oscillates the vertex colour of
**233 set meshes** — the far skyline of every city, 132 of them in Lahoreh —
and the port decoded it into `Corner::phase` and then read that phase nowhere.
A defect, not a missing feature: fixed on 2026-09-09, on by default, in both
backends. `docs/ASSETS.md` 4c.

**The lesson for this file**: before building an enhancement, check whether the
game already does the thing and the port simply dropped it. That is cheaper
than any enhancement and it is what the original looks like.

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

## Row 7 - DONE 2026-09-09

Built as designed. The vertex carries its normal to the GPU, the frame's
nearest eight lights go into set 1 alongside the shadow map, and `scene.frag`
runs the same reach test, the same linear falloff and the same `-(N.L)` per
fragment. Measured: at the point directly under a light the shader matches the
law computed from the formula EXACTLY, and along a scanline across a lit quad
per pixel varies by 51 where per vertex varies by 0.

**TWO BASES, and the split is the one place this departs from transcription.**
A body the engine itself lights starts from BLACK - `sub_494E80` writes
`instance[+416]` into every runtime vertex colour and every site that sets
+416 sets it to 0, and the crowd models ship pure white, so there is no baked
light in them to lose. A body the engine never lights does NOT: HO1_FN carries
real baked shading, and black-plus-lamps threw it away and left him a
silhouette wherever no lamp reached, which is what the first version drew. So
the crowd is lit from black and every other character has the lights ADDED to
its baked colour.

Two other things worth keeping:

* **the push-constant alignment trap caught me a second time.** `lit` was put
  in front of `fogColour`, which is 16-byte aligned - so the colour moved from
  offset 80 to 96, the C++ struct and the GLSL block disagreed, `lit` was
  never 1 and the whole enhancement drew nothing. Same fault as row 6's, one
  field later, and the shaders now say out loud that a new scalar goes AFTER
  the vec3.
* **adding lamps to an already-bright body changes little.** The player's
  baked colour is near-saturated, so the visible gain is almost all on the
  crowd, whose shading IS the lights. Said here rather than implied by a
  screenshot.

**What it looks like** (`renders/lighting-perpixel.png`, made 2026-09-09; the
directory is gitignored, so regenerate it rather than expecting it in a
checkout):

    build/omk-play ../gamedata ../tables --save ../traces/save-appart.bin \
        --area 0 --stand 2052,0,-1151,180 --density 4 --frames 150 \
        --world-vulkan --lighting pervertex|perpixel --dump out.bin

Measured on that frame: **5712 of 480000 pixels** move, 1196 of them by two or
more luma steps - and **0 of them are on the player**. All of it is the crowd,
which is the honest shape of the enhancement: their shading IS the lights, so
sampling it finer changes them, while a body that already carries a
near-saturated baked colour barely moves when lamps are added to it.

**What it does NOT take account of, asked by a reader and measured**: the
EFFECTS. A fire, a vent, a neon sign emits sprites and never light - nothing
in `ASSETS` 3b's chain carries a light record and there is no dynamic light to
attach one to. The street lamps and the signs ARE accounted for, because the
`.3DO` light table was authored onto them; where a fire seems to light the
street that is a record the artist placed beside it, and it varies by set (the
Impasse's flame has one 17 units away, the temple's four have none nearer than
93). So nothing flickers: a fire's light, where it has one, is static.
`docs/ASSETS.md` 4c, `verify.py: effects and lights`.

Still open, and named in the row below as it was: the port applies reach and
falloff per BODY where the engine does them per MESH. The GPU path is now per
FRAGMENT, which is finer than either; the CPU path is unchanged.

## Row 7, as designed - per-pixel lighting

### What the engine's lighting IS, which bounds what "enhanced" can mean

Three facts, and they are narrower than they look:

* **a set has no dynamic light at all.** It is shaded by a colour baked into
  every vertex, and 38.9% of set vertices are not grey (ASSETS 4c);
* **the `.3DO` light table is real and authored** - 4179 records over 216
  models, each with a position, a colour, an intensity, an inner and an outer
  radius, and a direction the loader computes as `normalize(centre - pos)`;
* **the receivers are narrow.** All EIGHT call sites of `sub_4380B0` are in
  `18_d3d.c`, the street-life module, so the lights fall on the procedural
  crowd and the traffic and on nothing else. The player and every staged actor
  are lit by their baked vertex colour alone. The law is `-(N.L)` over a linear
  falloff between the two radii, through the engine's `(t * c) >> 8` ramp.

Two further paths are DEAD and not worth chasing: the per-object live light
gated on mesh flag `0x8` is carried by **0 of 15720** shipped meshes, and the
environment-map stage is never given a texture (ASSETS 4c).

### The enhancement, and why it is the most defensible one left

**Evaluate the same law per FRAGMENT, and let every character receive it.**

Neither half invents anything. Today the falloff and the dot product are
computed once per vertex and interpolated across the triangle; a character's
thigh is one or two quads across, so a lamp's pool bends over his leg in flat
facets and pops as a vertex crosses the falloff boundary. Per pixel is the
same arithmetic sampled finely enough to stop showing the tessellation.

Letting the player and the staged actors receive IS a deviation from the
original and must be labelled as one - but it is the same lights under the
same rules, and it settles the oddity that a passer-by is lit by a lamp the
player standing beside them is not.

Most of the machinery exists already. `Corner` carries the vertex normal
through the pose (read for the crowd's lighting on 2026-09-05), and row 6 has
just added set 1 - a per-frame descriptor set with a uniform buffer - which is
where a small light list would go.

Also to fix while there, and it is a FIDELITY item rather than an enhancement:
the port applies the reach and the falloff per BODY where the engine does them
per MESH, which is a declared deviation of `o3de/vertexlight.h`.

### The check

Sharper than a pixel count, and it writes itself: per pixel and per vertex must
**agree AT the vertices** and **differ BETWEEN them**. Sample the fragment
colour exactly at a vertex position and require it to match the per-vertex
value; sample the middle of a long triangle spanning a falloff boundary and
require it to differ. That measures "the same law, sampled finer" and fails on
any change to the law itself.

Then the motion test, as everywhere else here: a body walking out through a
lamp's outer radius must dim CONTINUOUSLY rather than in steps.

## Row 8 - lighting the sets, and why it is held back

The obvious next thought, and the lamps are sitting in the set's own file. The
problem is that the baked vertex colour **is** the artists' lighting and
already contains those pools, so adding the lights on top does not enhance the
picture, it doubles it - and there is no principled way to subtract what the
painter put in. It would be a look, chosen here, replacing the one the game
shipped. Kept as a row so nobody has to rediscover the reason; if it is ever
built it must say plainly that it overrides authored art.

Bloom on the neon and a coloured fog are the same shape: they would flatter
the game and have no data behind them.

## `all = max` / `--enhance-all`

One switch for the whole section, so nobody has to know what the list
currently is. The two the DEVICE caps - MSAA and anisotropy - are asked for at
their largest defined value and the backend reduces what it cannot meet and
says so, which is what "max available" means here; the rest have a fixed top.
The tops live in `settings.h` (`kMaxAntiAliasing` and friends) so the flag and
the ini key cannot drift apart from each other or from the parsers.

`verify.py: enhance all` is the guard that matters, because **nothing about a
forgotten enhancement is red**: it just stays off while the report says
everything is on. So the check compares the two directly - every key
`settings.cpp` reads under `[Enhancements]` except `all` itself must be driven
by `applyMaxEnhancements` - and is shown to fail by adding a key that is not.
It also asserts the precedence in the direction that matters: `all = max` with
`anisotropy = 4` gives 4, so a config still means what it says.

## Row 9 - supersampling, DONE 2026-09-09

`--ssaa 1|2|4`. The Vulkan attachments are made N times larger each way, the
render area and the viewport with them, and `readback()` averages each NxN
block down. **The resolve happens on the 8-bit side, before the one 565
quantisation** - averaging four 565 values would quantise four times and then
average the error, which is most of the point thrown away. The present blit
takes `VK_FILTER_LINEAR` only above 1, so at 1 nothing changes.

**Why it is not MSAA over again**, which is the question the row exists to
answer: MSAA samples geometry EDGES. This game's aliasing is mostly texture -
256x256 atlases sampled POINT - and CUTOUT, where the silhouette of a grille
or a railing is a colour key INSIDE a triangle, which is a place MSAA never
looks. Supersampling reaches both.

Measured on Anekbah's own camera 3: `--ssaa 1` is BIT-IDENTICAL to no flag at
all (0 pixels), 4x moves 26021, and the frame goes from **95 distinct colours
to 138** - the signature of a resolve rather than a shift, since averaging
makes values that were in neither sample.

**Not asserted, and deliberately**: that it looks less aliased. Edge energy
over a texture-heavy set falls about 1%, because most of the high-frequency
content there is the artists' texture and this averages that too. The eye
settles whether it looks better; the check settles that it resolves.

Three faults on the way, all mine and all the same shape - a change applied in
one place and not its partner:

* the **render area** stayed at the output size inside a larger attachment, so
  everything outside it was never cleared and the resolve averaged
  uninitialised memory into magenta stripes;
* the **readback's resolve never landed at all** - it was in an edit batch
  whose other anchor failed, so the whole batch was refused while the rest of
  the plumbing went in. The symptom was a black frame, because reading the
  first `w*h` pixels of a 2x-wide buffer walks the top quarter of the picture,
  which on that set is ceiling;
* `presentSurface` uploads a finished picture at the OUTPUT size into the
  top-left of the attachment, so the present blit had to be told which region
  is live rather than assuming the whole target.

Known limit of 1 and 2, to be judged by eye: the sets sample sub-rectangles
of shared atlases (the Anekbah signs), and a filter reaches half a texel past
a rectangle's edge into its neighbour. Nothing per draw says where the
rectangle ends, so it is not clamped.
