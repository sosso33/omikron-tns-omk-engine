# The remaster mode - the plan of 2026-10-20, and what was built from it

**Status, 2026-11-02:** planned 2026-10-20 and largely BUILT since - steps 1-3,
the packs, the preload radius, the weather and the `OMK_REMASTER` switch each
have an "as built" section below; the plan that follows is kept as written.

The reader asked (2026-10-20) for a mode that improves the graphics as far as
they go - lighting, reflections, AI-improved textures and models, "a big
remaster or a small remake" - with small artistic changes allowed as long as
the global vision holds, and the core of the game unchanged. This file is the
feasibility estimate and the plan. Every size below is a guess the first hour
of the actual work is allowed to overturn.

**The verdict**: feasible, and a third of it exists. The hard part is not
technical; it is the one `todo/enhancements.md` row 8 already wrote down - the
baked vertex colour IS the artists' lighting, so the remaster must build on
that look, never replace it.

## What it starts from

`todo/enhancements.md` rows 0-13 are done: MSAA, supersampling, trilinear and
anisotropic filtering, per-pixel lighting, mapped shadows (characters only),
60 fps with smoothed bodies, the lifted clip, filtered interface and text.
`--enhance-all` turns them on. The remaster is a tier ON TOP of that, not a
new renderer.

## Two rules, decided before any code

1. **A section of its own: `[Remaster]` / `--remaster`, NOT part of
   `all = max`.** `[Enhancements]` is held to "the data backs it"; row 8 is
   held back precisely because it overrides authored art, and a remaster does.
   So it says so and lives apart. Still OFF by default, still byte-identical
   at rest, every feature with a check shown to fail (CLAUDE.md §4, PORTING
   B2) - the same contract as every enhancement row. **Every feature is its
   OWN option** (the reader, 2026-10-20): `--remaster` is only a preset over
   them, any key beats it, and the current render must stay available
   after every step - the default run is never changed by this plan.
2. **Generated assets never enter the repo.** An upscaled texture or a remodel
   is a derivative of the copyrighted data and the repo is PUBLIC. Ship the
   PIPELINE (GPL scripts); each player generates the pack from their own
   `gamedata/` into a directory of its own, through `omk::safeOutputPath` /
   the tools' equivalent - never into `gamedata/` (CLAUDE.md §1).

## Tier A - the renderer only, no new assets (~2-4 weeks, Vulkan first)

| # | feature | why it respects the art | open question |
|---|---|---|---|
| A1 | HDR target, tone mapping, a colour-grade LUT per city | a grade, not new light; tuned against the shipped frame | the LUTs are a choice of this port - say so |
| A2 | bloom on what already glows: additive-blend meshes, the neon | the GAME chose which meshes emit - that answers row 8's "bloom has no data behind it" | enumerate the emitters from the mesh flags / blend modes (ASSETS 4), not by name |
| A3 | reflections: generalise the planar mirror pass (`AP_mirror`, `AB_mirror`, ...) and the envmap flag `0x4000000`; screen-space reflections on floors a material tag marks wet or polished | the mirrors and envmaps are the game's; the tags come from B3 | which floors - by eye, per city |
| A4 | SSAO at LOW strength; height or volumetric fog in the game's own fog colour and range | the vertex colours already hold painted occlusion, so AO only adds contact detail | strength by eye, never doubling the painted dark |
| A5 | **characters lit by their surroundings**: light probes sampled from the set's vertex colours near a body | READS the artists' lighting instead of adding lights - the most defensible lighting step | the probe spacing; how it composes with row 7 |
| A6 | FSR (open) / MetalFX upscaling at 4K - **FSR 1 BUILT, Vulkan (2026-10-22)** | cheaper than row 9 (`todo/optimization.md` "The 4K benchmark") | the 4K benchmark on the M3 is the gate (30+ fps) |

## Tier B - the HD texture pack (~1 week engine, then curation). The best ratio.

* **B1, the pipeline.** `tools/tex3dt.py` already decodes all 2534 model
  textures (palettised, at most 256x256, ASSETS 3). Upscale 4x with an AI
  upscaler run locally - chaiNNer or Upscayl, Real-ESRGAN-family models
  (4x-UltraSharp, Nomos-type), both run on Apple GPUs. Set textures go the
  same way.
* **B2, the traps the pipeline must handle:**
  * TILING textures: tile 3x3 before upscaling and crop the centre, or every
    repeat grows a seam.
  * The colour KEY (mesh flag `0x800`, solid black is cut out): upscale the
    mask separately and threshold it, or the cutout edge blurs into a fringe
    (the alpha premultiply of row 1 handles the filtered edge after that).
  * Signs and lettering: AI garbles text - an exclusion list, or by hand.
  * 16-colour textures come out over-smoothed; tune per model.
* **B3, material maps.** Normal / roughness / height generated from the
  upscaled textures (DeepBump - a free Blender add-on; Materialize; Substance
  3D Sampler, paid). They feed row 7's per-pixel law and A3. A small sidecar
  per texture also carries the wet/polished tag.
* **B4, the engine side.** A replacement loader keyed by a HASH of the
  decoded original indices (survives renames, two models sharing a texture),
  falling back to the shipped texture. (Corrected when built: the UVs reach
  the GPU in the original's TEXELS and the shader divides by the bound
  texture's size, so a 4x texture needs the factor passed - see below.)
  Memory: 2534 x 1024² is fine on desktop, not on the Vita or the 3DS - the
  pack is desktop/Quest only.
* **B5, optional and more creative**: ComfyUI + a Stable Diffusion tile
  ControlNet re-draws detail rather than sharpening it. Better-looking, and it
  DRIFTS in style: sign-off per texture on the main ones only.

## Tier C - a lighting re-bake: NOT recommended

A full offline re-bake (Blender Cycles, the sets' authored lights calibrated
to match the painted colours) is months of work and replaces the art
direction instead of honouring it - row 8's objection at full size. A1, A4
and A5 get most of the effect without that. Kept here so nobody has to
rediscover why.

## Tier D - models: the weakest part for AI

| # | route | cost | verdict |
|---|---|---|---|
| D1 | automatic smoothing: PN-triangles / Phong tessellation on characters, plus B3's normal maps | ~1-2 weeks | cheap rounded silhouettes; it softens edges the artists meant hard, so a per-model opt-out list |
| D2 | hand-remodel the main cast (~10-20 characters) in Blender, KEEPING each `.3DO` skeleton and weights so every `.ani`, cutscene and `.3DM` line still drives them | ~1-3 months of ARTIST time | the only route to genuinely better characters; needs a `.3DO`->glTF export (`tools/mesh3do.py` already reads the format) and a glTF loader in the engine (cgltf, vendored under `engine/third_party/`) |
| D3 | AI 3D generators: Hunyuan3D-2, TRELLIS (local), Meshy, Tripo (cloud) | - | static, unrigged, messy topology and UVs, style drift: a starting point for PROPS an artist cleans up; never characters (wrong skeleton), never the four city sets (that is remake scope) |

Uploading game assets to a cloud generator is the reader's call; the local
tools avoid the question.

## Tier E - the rest

* The 640x480 interface bitmaps AI-upscaled (B1's pipeline); row 3's integer
  mode is still open beside it.
* The MPEG-1 films (`todo/classic-mac-port-1999.md`) upscaled: Real-ESRGAN in
  video mode (free) or Topaz Video AI (paid). **BUILT 2026-10-22** - "Tier E -
  the film pack" below.
* Widescreen: the authored camera editings were framed for 4:3 - read what
  `docs/CUTSCENES.md` says about their framing BEFORE widening anything.
  Unchecked.

## The order

1. **B4 + B1 on ONE model** - the fastest visible result, and it settles the
   hash and the colour-key questions before 2534 textures depend on them.
   **DONE 2026-10-20** - see "Step 1, as built" below.
2. **A1 + A2** - tone map, grade, bloom on the game's own emitters.
   **A2 as written does not hold** - see "Step 2's bloom, tried" below: the
   additive meshes are water and glass, the lights are opaque textures.
3. **B3 then A3 + A5** - material maps, reflections, characters in the
   painted light.
4. **D1, then D2 on the player character** as the pilot for the glTF route.

Steps 1-3 are about one to two months of engine work and make a convincing
remaster; step 4 is where the small remake begins, bounded by artist time,
not code.

## Step 2's bloom, tried (2026-10-20) - and why its premise was wrong

A2 assumed the game marks its light sources by drawing them ADDITIVELY
(`0x1000|0x2000`). Built on Vulkan to find out (an emitter mask in the world
target's alpha - the luminance each additive draw adds - then a quarter-size
down / blur / add), and measured:

* **The additive meshes are mostly not lights.** Ranked by set: Lahoreh 41
  (nearly all `Eau` - water), Aapkayl 22 (`AP_mirror`, `Ap01vitre` glass,
  `eclair` lighting), Sprison 16 (`Barreau` bars), jaunpur 14 (water), the two
  sex shops 14 each (`vitre`), mayerem / Jangir 10 (water, lakes, rivers).
* **The street's signs and lamps are OPAQUE textures**, not additive. On
  Anekbah's street at strength 4 the bloom moved ~5300 pixels by about one
  565 step, all on the far skyline windows; in the apartment camera, 0.
* So a bloom that follows the data is nearly a no-op, and a bloom driven by
  brightness alone (a bright-pass) would make the sky and the pale walls glow
  - a look chosen here, with nothing in the data behind it.

**Not committed.** The working code (off by default, the default frame
unchanged) is kept outside the repo: `~/Documents/omk/omk-pack/wip/
bloom-wip.patch` + `bloom.frag`. Its post-pass (down / blur / add after the
scene pass, before the readback) is reusable for tone mapping and a grade.

**The principled way to a bloom is an EMISSIVE MAP per texture** - which
texels are light (a sign's lettering, a lamp's bulb, a window) - carried by
the texture pack beside B3's material maps, authored or generated and then
curated. That is the reader's call: it decides what in the city is light.

## Step 2, as decided (2026-10-20) - EMISSIVE bloom + a GRADE per city

**The reader's decision**, after the offline previews: an EMISSIVE map per
texture drives the bloom (not the additive draws - see "Step 2's bloom,
tried" above), then one colour grade per city, the two `[Remaster]` options
of their own, off by default. Qalisar takes the LIGHTER neon grade: "the area
is intended to be dark, and the contrast with the neons is probably a part
of the visual identity of the area" (the reader).

**Every tested parameter, kept** (the reader: "save all the tested
parameters somewhere just in case"). The scripts that made the previews, the
patch of the first bloom and the comparison page are at
`~/Documents/omk/omk-pack/compare/` (`scripts/emissive.py`, `scripts/look.py`,
`index.html`); the pixels never enter the repo.

*The emissive rule* (per texture, from the shipped texels; `l` the luma
`(299R+587G+114B)/255000`, `med` its median over the texture, `vmed` the
median of `max(R,G,B)`): a texel's emissive factor `f` is the larger of

    bright  clamp((l - max(med + 0.30, 0.55)) / 0.15)
    neon    clamp((sat - 0.55) / 0.2) * clamp((max - max(vmed + 0.25, 0.5)) / 0.15)
            with sat = (max - min) / max over the texel's R, G, B

and 0 on a keyed (pure black) texel. Of 1259 textures 1112 have some. It
finds the signs' lettering, the signboards, lit windows, lamp globes and -
with the neon term, added for Qalisar - the coloured tubes; it ALSO marks
quay edges and bright facade patches in the daylight cities (Lahoreh,
Jaunpur), which is why the maps are generated and then CURATED.

*The bloom* (composited in the preview on the 565 frame): the emissive frame
(the scene drawn with each texture's colour times `f`) box-averaged to a
quarter, blurred by a Gaussian of sigma 4 quarter-texels (21 taps) twice
each way, enlarged bilinear, times 1.3, SCREEN-blended:
`1 - (1 - base) * (1 - glow)`.

*The grades* (on 0..1 channels; `l` the luma; `S(v, a)` the S-curve
`clamp(v + a * (v - 0.45) * (1 - |2v - 1|) * 1.6)`):

| grade | city | the operations, in order |
|---|---|---|
| filmic | Jaunpur | `S(v * 1.06, 0.35)` per channel; saturation 1.12 about the new luma |
| noir | Anekbah (and Kay'l's apartment) | saturation 0.82; r*0.94, g*0.99, b*1.08; `S(max(0, v - 0.02) * 1.04, 0.3)`; in the preview ALSO a vignette `1 - 0.35 * min(1, (x^2 + y^2) * 2.2)` (x, y from -0.5 to 0.5) - spatial, so NOT in a LUT |
| sunlit | Lahoreh | `hi = max(0, 1.4 l - 0.3)`: r += 0.045 hi, g += 0.022 hi, b -= 0.03 hi; `0.03 + 0.97 v`; `S(v * 1.04, 0.22)`; saturation 1.06 |
| neon2 (chosen) | Qalisar | saturation 1.15; `sh = max(0, 1 - 2.5 l)`: r += 0.012 sh, b += 0.03 sh, g -= 0.01 sh; `0.012 + 0.988 v^0.95`; `S(v * 1.03, 0.22)` |
| neon (first, too dark) | - | saturation 1.18; the same shadow tint; `S(max(0, v - 0.015) * 1.05, 0.38)` |
| tealorange | - | `sh = max(0, 1 - 2.2 l)`, `hi = max(0, 1.6 l - 0.4)`: r += -0.035 sh + 0.06 hi, g += 0.01 sh + 0.025 hi, b += 0.05 sh - 0.04 hi; `S(v, 0.2)` |

*What the previews showed*: with the SKY in frame (the game session, not the
set alone) Anekbah's noir reads as dusk and is right; Lahoreh's sunlit and
Jaunpur's filmic kept each city's palette; the first Qalisar grade crushed
the street under its neons, neon2 keeps it dark and readable. Views: Anekbah
`--area 0 --stand 1804,0,-6890,336`; Jaunpur `--area 1 --stand
10524,-40,10284,90`; Lahoreh `--area 64` (its street start); Qalisar `--area
101 --stand 15516,0,-10182,270` (under the neons) and `,180` (the street),
the densest cluster of its 22 `Enseigne` meshes.

**Open, decided when built**: the area -> city map for interiors (they
inherit the city first); the day/night cycle may want a day and a night grade
(Qalisar at least); the grade is off while the greyscale bank is on.

## Step 2, as built (2026-10-20) - the bloom and the city grades

**What it is.** Two `[Remaster]` options of their own, off unless named,
never turned on by `all = max`, drawn by Vulkan and GLES (the software
reference, GL1 and the 3DS refuse them):

* `bloom = S` / `--bloom S` - a glow from the texture pack's LIGHT MAPS
  (`<key>_e.png`, greyscale: how much of each texel is light). 1.3 is the
  previews' strength.
* `grade = DIR` / `--grade DIR` - a colour grade per city: DIR's
  `<SET>.cube` files and `map.txt` (`AAPKAYL = ANEKBAH`), chosen each frame
  for the set under the player's feet (`PlayState::worldSet`), OFF while the
  greyscale bank is on; a hard switch, the game's own fade covers a change of
  set. The scene viewer (`--scene`) takes neither yet.

```bash
P=~/Documents/omk/omk-pack
python3 tools/omktexpack.py emissive $P/full/pack     # the light maps, ~2.5 min - then CURATE
python3 tools/omktexpack.py grades   $P/grades        # the four city .cube files + map.txt
engine/build/omk-play ../fr ../tables --save ../traces/save-appart.bin --area 101 \
    --texture-pack $P/full/pack --bloom 1.3 --grade $P/grades
```

**The preset** (the reader's "could you add a remaster arg?", 2026-10-20):
`--remaster DIR` / `[Remaster] dir = DIR` turns on all of it from one
folder - the texture pack in `DIR/pack` (or `DIR/full/pack`, which
`omktexpack.py export DIR/full` makes), the bloom at 1.3, the grades in
`DIR/grades`, the water at 0.6 (0.35 until the Fresnel). A BASE, as `all = max` is for `[Enhancements]`: `--bloom 0`,
`--texture-pack`, `--grade` or their keys beat it. With the reader's folder:

```bash
engine/build/omk-play ../fr ../tables --remaster ~/Documents/omk/omk-pack
```

`engine: remaster preset` (shown to fail with the preset's bloom dropped).

**How.** With the bloom on, the scene shader writes each texel's light into
the world target's ALPHA (Vulkan: push field `bloomMode` at 116, the
block 116 -> 120 bytes, and binding 1 of every texture's set - its light map
or a shared black one; GLES: a BLOOM VARIANT of the scene programs made from
their source at `init`, the light maps on unit 3, so the default programs and
the Vita's shader cache are untouched). After the scene, `post.frag` /
`kPostFrag`: the frame copied aside, `rgb * alpha` down to a quarter, two
Gaussian passes (21 taps, sigma 4), then one pass back into the target - the
glow SCREEN-blended, then the LUT (an N*N x N strip, blue the slice, red
fastest, trilinear). `o3de/colourgrade.*` reads `.cube` (3D, 2..64, domain
0..1) and maps sets to cities.

**Measured.** With neither on, every frame tried is byte-identical to the
build before: `run_vulkan` on Aapkayl and Qalisar under nearest, trilinear
+ 16x and 4x MSAA, and the 45-frame street dump on Vulkan and GLES, with and
without the full pack (its light maps present). The engine's Qalisar grade
against `omktexpack.grade_rgb` on the same frame: within the 565 step (mean
1.4-3.9 of 255, worst 9.4). Vulkan against GLES with both on, Qalisar under
the neons: 180 of 960000 pixels apart by more than ~16 levels. Checks:
`engine: bloom and grade` (new, shown to fail three ways), and
`engine: texture pack`, the GPU and enhancement neighbours, the Vita and 3DS
builds green.

**Open:** curating the 1112 light maps (the daylight cities' quays and pale
facades - "what counts as light" on the comparison page); a day and a night
grade (Qalisar at least, the clock runs); the scene viewer; the noir
preview's vignette is not in the LUT (spatial) - a separate option if wanted;
the Vita's shader cache does not hold the bloom variants and the post
program, so a console without the compiler refuses them.

## Step 3, A3 - water reflections: the design (2026-10-20, the reader's "ok, go")

**Why water first.** The sets are lit by BAKED vertex colour; only the
characters take per-pixel light (enhancement row 7). Normal or roughness maps
on a set would change nothing unless something relit it - which is row 8's
objection. The data does mark water: the additive meshes are mostly water and
glass (see "Step 2's bloom, tried").

**The preview that failed** (kept: `~/Documents/omk/omk-pack/wip/
water-mirror-preview.patch`): marking Jaunpur's 8 water meshes (by name, at
the commonest height) as the set's MIRROR (flag `0x100000`) and running the
existing mirror pass. The sky and half the town VANISHED and no reflection
showed on the water. The original's mirror is confined by DRAW ORDER - the
reflected pass first, the real pass over it, nothing cleared - which works
only because a mirror is an OPENING with nothing real behind it. Water has the
canal bed and the walls behind and around it, so it needs a pass of its own.

**The pass**:
1. the scene through the camera reflected in the water plane, CLIPPED to what
   is above it, sky included, into an offscreen target of its own;
2. the main pass as now; the water meshes keep their additive blend and add
   the reflection sampled at the same screen position, times a strength (a
   Fresnel term once judged);
3. a renderer entry beside `drawMirrorScene` (Vulkan: the scene pass able to
   target another framebuffer, one binding more; GLES: an FBO and a variant);
4. the game picks the water plane per set as it picks the mirror, and draws
   through the new path when the option is on and the camera is above it;
5. what counts as water: horizontal ADDITIVE surfaces - the data, not names.

Costs a second scene pass while water is in view. The vertical glass (`vitre`)
would be a further step, one plane per window.

### A3, as built (2026-10-20)

`[Remaster] water = S` / `--water S` (0..1), and `--remaster DIR` turns it
on at **0.35** (**0.6** since the Fresnel); off unless named, never `all = max`, Vulkan and GLES.

* **The plane** (`o3de/renderer.h` `waterPlane`) is the set's own data: the
  height carrying the most area of horizontal additive faces, a plane past
  1e6 square units. 7 sets have one: Lahoreh (y 12, 24.8M units, 3580
  triangles), mayerem (1072), jaunpur (4, 9.2M, 1250), PAstar (1384 -
  **Astaroth's arena: is that water, or a magic floor that should not
  reflect? for a play test**), LMoexter, ACSvent, L_Khonsu. Anekbah, the
  apartment and Qalisar have none.
* **The pass** (`drawWithWater`): the camera above the plane; the draws split
  into the water's triangles and the rest; the eye and target mirrored, the
  image flipped vertically (a half turn of roll and a screen-X flip); the
  scene clipped to what is above the plane (posed draws - the sky, the
  bodies - whole). Vulkan: the scene's render pass into a second framebuffer
  of the same attachments, binding 2 of the texture sets, push `water` /
  `waterStrength` at 120 / 124 (the block is FULL at 128); GLES: an FBO and
  a WATER VARIANT of the scene programs (unit 4), made at init only when asked.
* **Looked at** (Jaunpur's canal facing 180, Lahoreh's street start): the
  walls, railings and the arch mirror the right way up and meet the
  waterline; at 1.0 the water washes out (the reflection is ADDED to the
  water's own additive colour), at 0.35 it reads as a sheen. Vulkan and GLES
  3363 / 3326 of 960000 pixels apart.
* **Measured**: with the water off every frame tried is byte-identical to the
  build before (Vulkan and GLES, the street and the canal with the full pack;
  `run_vulkan` with 4x MSAA and trilinear). `engine: water reflection`, shown
  to fail three ways; the reflection's ORIENTATION is judged by eye, not
  checked (see the check's docstring).
* **Found on the way and fixed first** (`30f2328e`): the PNG reader's fixed
  Huffman codes were built lazily behind a plain bool, and with a pool
  decoding on the thread pool two workers rebuilt them at once - the viewer
  HUNG in Jaunpur. A function-local static now, and the inflate bounded;
  `engine: texture pack` decodes on the pool in 20 fresh processes.

* **The reader's play test, 2026-10-20 - a swimmer's legs reflected above
  his head.** The CPU clip keeps only what is above the plane, but a POSED
  draw (a body, the sky) is handed to the reflection whole - its corners are
  at rest, the pose is the GPU's - so a swimmer's submerged half was mirrored
  up through the water. Fixed in the backends: the reflection pass DISCARDS
  per fragment below the plane (Vulkan: `water` mode 2, the plane's height in
  `waterStrength`, `vWorld` is the posed position; GLES: `uClip` / `uClipY`
  and a `vWy` varying from VERTEX variants made the same way as the fragment
  one). Measured on the canal at strength 1: the shader clip alone (the whole
  scene handed over, `OMK_WATER_NO_CPU_CLIP=1`) is 77 pixels from the CPU
  clip; with no clip at all 2861. Part of `engine: water reflection` now,
  shown to fail on each backend. **CONFIRMED IN PLAY** (the reader, 2026-10-20: "Player reflect is fixed").
* **The water near the camera mirrored too strongly** (the reader's screenshot, 2026-10-20: the quay's own reflection at a steep angle). The geometry is right - the reflection meets the quay at its waterline - but one strength everywhere is not water: it reflects ~2% looking straight down and nearly all at grazing. Done: a Fresnel term (Schlick, F0 0.02) on both backends - `strength` is now the GRAZING strength, cos theta the eye ray's height over the level plane (Vulkan: the eye in the lights UBO at 400, since the push block is full; GLES: `uEye`, the ray made in the vertex stage). On the canal at strength 1 the reflection's added light per moved pixel falls from 31.2 to 20.7 (upper half, far) and from 43.0 to 19.8 (lower half, near), so lower/upper goes 1.38 -> 0.96; `engine: water reflection` part 4 asserts it under 1.15. The reflection was fainter overall and 0.35 read too weak (the reader, in play); the preset is now **0.6** - "it seems good now" (2026-10-20) - CONFIRMED IN PLAY.
* **CORRECTED 2026-10-20: the swimming canal IS the reflecting plane.** An
  earlier bullet here said the canal the player swims in lies lower than y 4;
  `todo/swimming.md` puts its surface at ~0..4 (the quay ~-120, the bed ~120),
  which is the plane, and the reader's swimming screenshot shows it
  reflecting. The camera that "dropped below y 4" in the headless runs was
  the swimmer's UNDERWATER camera. `texpack_tool water` with
  `OMK_WATER_LEVELS=1` lists every level past 1e4 square units: Jaunpur
  holds two more, both ABOVE the canal (Y down) - 405905 at y -368 (59
  triangles) and 43750 at -120 (14, the quay's height) - and Lahoreh's 16 is
  its 12 (within the 4-unit tolerance); the other five sets have one level.
  What Jaunpur's two are (terrace pools, or horizontal GLASS - additive too)
  is unread: reflecting them waits for a look, and per-level planes are not
  built.

* **The cost at 4K, measured 2026-10-20 on the M3** (load average 4-6;
  `--res 3840x2160 --frames 300 --fps`, the canal at 10524,-40,10284,180,
  `--remaster` with the pack, bloom and grades on; steady seconds, two runs
  each, alike): **Vulkan** (`--vulkan`, the swapchain presenting) 120 fps
  (vsync) with the water off, **110** with it on (12 ms worst); **GLES** 84-87
  off, **78-80** on. The reflection pass is a second 3840x2160 scene pass while
  water is in view - well inside the 30 fps yardstick on both. (`--world-vulkan`
  is the offscreen harness with a CPU present, ~40-60 fps either way - not the
  game's path, not quoted.)

**Open**: ripples (a normal map
from B3 perturbing the lookup); PAstar; the glass (`vitre`), one plane each;
the per-level planes above (once Jaunpur's two upper levels are looked at).

## Step 3, A3 continued - the reflecting floors and the lit reflections: as built (2026-10-21)

The reader's "do 1 and 2" (the wet and polished floors; the reflections lit by
the tiled point lights).

* **The floors, planar - the water's pass**, not screen-space: it shows what
  the camera cannot (SSR reuses the frame and fails at its edges), it was
  judged good on the water, and Qalisar's street is one level. `[Remaster]
  floors = S` / `--floors S` (0..1), the `--remaster` preset's **1**, off unless
  named; Vulkan and GLES (its water variant, now built when the floors alone
  are on).
* **Which floors**: the grades folder's `floors.txt` (`SET TEXTURE =
  STRENGTH`, `o3de/floorreflect.h`), curated by eye as `lights.txt` is;
  `omktexpack.py grades` writes the draft (CITY_FLOORS) when absent, and
  `texpack_tool floors <set.3DO>` lists a set's LEVEL opaque area by texture
  and height. Qalisar's draft: its street, BATITR09 at 0 and 4 (4060 m2) and
  CATITR03 at 0 (862), at 0.5; its raised walks, CATITR02 (774), at 0.35.
* **The plane**: a set's listed level triangles grouped by height, levels
  within 8 units one plane at the HIGHEST (the rest lie below and the
  reflection clips them - at a mean height the top floor was in its own
  reflection); each frame the level below the eye with the most listed floor
  within 40 m (`chooseFloor`); the water first when its plane reflects - one
  reflection a frame (the authored mirror set aside, said in the log).
* **In the frame**: `drawWithWater` keeps a listed floor's runs on the plane
  IN THE SCENE, in order (a decal and a depth tie ride on it), each carrying
  `Draw::reflect`; the backend adds the reflection there as on the water, at
  `floors x reflect`, with the Fresnel - shader mode 3, whose lookup the
  floor's RELIEF shifts (the pack's normal map, ~1% of the frame's height;
  the tiled module). The water's mode 1 is unchanged.
* **The lit reflections** (Vulkan): the water's reflection pass bins the tiled
  point lights for ITS camera into the tiles buffer's second half and binds
  its own set 1 (`ds1Refl_`); the shader no longer skips the tiles there.
  `OMK_REFL_UNLIT=1` leaves them empty, as before - byte-identical to
  `7929d606` on Jaunpur's canal. In Qalisar with the floors, lighting the
  reflection moves 57853 pixels at 640x480.
* **Found on the way, fixed**: `binLights` mirrored X a SECOND time under
  `flipX` (the basis already negates `s`) - a reflected camera's lights went
  to the other side's tiles: the tight binning 15838 pixels from the loose
  one, 0 after. The frame's camera never sets `flipX`, so nothing drawn
  before moved.
* **Off**: `--floors 0` byte-identical to `7929d606`, 14 of 15 (software,
  Vulkan, GLES; the burner, Qalisar, the canal) - the 15th the canal under
  `--enhance-all`, its water's reflection lit (1060 pixels). `engine: floors`
  new, shown to fail two ways; `env switches` had been red since the bloom
  (raw `getenv` in `run_vulkan`, `gles_probe`, `texpack_tool`) - mended.
* **The cost, and cut** (the reader's play test at 4K, M3: Qalisar 57 fps
  without the floors reflecting, 32 with): a second full scene pass. Measured
  apart - ~4.5 ms the pass (3.4 of it CPU, the 640x480 runs say), ~5.5 the
  reflection's per-fragment tiled lights at 4x MSAA. Now: the floors'
  reflection drawn at HALF size into the image's top-left quarter (one image
  and one binding for water and floors; Vulkan's viewport and render area,
  GLES's viewport and `uReflInv`), at ONE sample (`reflPass1_`, its own
  half-size depth, the scene pipelines again at 1 sample - all or none), and
  the reflection's CPU clip remembered for the SET's geometry (`clipAbove`;
  a particle's geometry is one object rewritten every frame, and a memo of it
  drew a stale clip - 1 pixel - so the others are clipped as before). Each
  step byte-identical where it should be (the memo to the clip it replaces;
  the water's frames to `7929d606` unlit). 4K Qalisar with the floors: 32 ->
  35-36 fps (floors off 42-48 in the same runs, the machine loaded); the
  half-size and one-sample pictures are hard to tell from the full one.
* **Open**: the list (Qalisar's draft only), the strengths, the relief's
  shift - for a play test; the authored mirror's pass still plain (no tiles);
  floors and water in one frame; GLES's reflection keeps its nearest slots;
  the second pass's CPU recording (~3 ms) left as it is.
* **CLOSED 2026-10-22 - NOT THE ENGINE: macOS held the bare binary's GPU at
  its lowest clock.** With `OMK_GPU_TIMES=1` (4K Qalisar, `--upscale 1.5`) the
  reader's drop came ~1140 frames in, on the pavement (12850, -10000): EVERY
  pass slowed at once, the content-independent ones too - post 1.6 -> 4 ms,
  present 4 -> 12 ms, scene 2.7 -> 15, reflection 0.7 -> 5 (GPU 5.8 -> 25 ms a
  frame) - and stayed slow wherever the camera looked. `powermetrics` while
  slow: **GPU 338 MHz, 90% busy, the SOFTWARE-requested state P1** (the lowest
  of 13; the top is 1338 MHz, 4x - the slowdown). Thermal pressure Nominal;
  plugging in changed nothing; a FRESH process at the same spot, the same
  CPU load beside it, ran fast for 1200 frames - the cap was the process's.
  Run as an APP (`make app`: `build/OMK.app`, Info.plist the adventure-games
  category with `LSSupportsGameMode` / `GCSupportsGameMode`), full screen,
  Game Mode engaged (the reader saw its icon) and the same walk held 4.4-6.8
  ms of GPU for 12840 frames, median 60 fps. **Play at 4K through
  `build/OMK.app`.** The record of the hunt, kept:
* **(was) OPEN BUG - the reader's 4K slowdown with the floors (2026-10-21, M3),
  reproduced by the reader several times, NOT YET by a headless run.** Playing
  Qalisar at 4K with the preset: 35-40 fps at the start, then ~16 fps
  (47-60 ms a frame) after walking back and forth between the start
  (15300, -10050) and the end of the pavement toward Anekbah's airlock (~13424,
  -9923), and staying low - in one log, short spikes at frames ~4980 and ~7920
  that recovered, then 47-59 ms held from ~9700 to the close (12350), the
  camera at 13541 -68 -9894 looking west. Established: a live `sample` of the
  slow game - the main thread waits on the GPU's fence (76%) inside
  `drawWithWater` -> `drawWaterScene`, so the GPU frame grew; the floors' plane
  stayed y 0, no area streamed in, the clip memo stays small (161 -> 245).
  On a QUIET machine (the reader's game closed - measurements taken beside it
  were void, a mistake made twice): the far end 58 fps off / 48 on / 52
  unlit or with an empty reflection; standing there 12000 frames, 21 ms
  steady; a scripted walk west and back, 17-30 ms. The lit reflection alone
  adds short SPIKES (to ~34 ms) as the street's motorbikes pass close - none
  with it unlit or the floors off; the reflection's binning is normal through
  a walk (listed a little below the main pass's, no full cluster).
  **The GPU timestamps are built** (2026-10-21, `OMK_GPU_TIMES=1`, Vulkan):
  one at each pass boundary of the frame's command buffer, outside every
  render pass, read after the frame's fence - "vulkan: GPU ms a frame (mean of
  60) - shadow, reflection, scene, post" and `@evt gpu.time`. At the far end,
  quiet, 4K: shadow 0.19, reflection 1.40, scene 6.62, post 3.67 - 12 ms, so
  that frame is not GPU-bound when it is fast. To reproduce, play with
  `OMK_GPU_TIMES=1` and read which pass grows when it slows; then, if it is
  the reflection's lights, a cap on the lights a reflected fragment takes.
  Machine notes: SDL3 appeared on this machine the same day and the Makefile
  prefers it (`make SDL_PKG=sdl2`, or `MAKEFLAGS=SDL_PKG=sdl2` for verify -
  no longer needed since 2026-10-26: SDL3 builds).
* **The game's own rain** (ASSETS 4e, read for the reader): Anekbah, the
  Impasse, the rooftops and Khonsu, one shower every ~11-18 minutes of wall
  clock (8-14 dry, 20 s ramp, 2.5-3.5 min, 10 s fade), only at *Niveau de
  detail* 2 - Qalisar has none. An idea: a rain city's floors wet with the
  cycle.

## Step 3, A5 - the bodies lit by their surroundings: as built (2026-10-20)

The reader's "continue with the character lighting". `[Remaster] bodylight = S`
/ `--body-light S`, and `--remaster`'s **1** (the reader's pick, "keep the
middle ground at 1", from the comparisons below); off unless named, never
`all = max`, Vulkan and GLES (`o3de/lightprobe.h`).

* **The rule.** The original starts every lit body - the crowd, the
  characters, the player, through `sub_494E80` - from ONE grey, the scene's
  `+416` (the set's ambient), and adds the set's lights that reach it
  (`o3de/vertexlight.h`); the set is shaded by colours BAKED into its
  vertices. `LightProbes` reads those: the set's OPAQUE surface (water, glass
  and multiply shadows are not light) summed into 200-unit cells at load BY
  AREA - each triangle cut into pieces no longer than a cell, its corners'
  colour interpolated - and around a body the cells' mean weighted
  (1 - d^2/R^2)^2 on each centroid, R 600 - continuous as he walks. The body
  starts, where the probe is brighter than `mean` (its mean colour at body
  height (90) over the set's floors, by area), from the grey PLUS
  `S (probe - mean)` per channel; where darker, from the grey SHADED
  `1 - S (1 - probe / mean)`; clamped 0..1 - so the average place keeps the
  original's grey and only the departures carry over: a dark stretch darker,
  a lit one brighter, its hue with it. The lights still add on top. Nothing is added to the set.
* **How it got there - three readings, compared by the reader:**
  * a RATIO of the grey, `1 + S (probe / mean - 1)`: "difficult to see a
    difference" - a set's grey can be nearly black (Anekbah's is **10 of
    255**, 0.039) and the lamps outweigh any multiple of it; a contrast curve
    on the ratio (`(L / mean)^(3 S)`) was no better for the same reason;
  * the PROBE ITSELF as the start (`grey + S (probe - grey)`): the most
    visible, and it brightens every body everywhere - the sets are painted
    far brighter than the grey the game gives its characters (0.44 against
    0.04 in Anekbah); not taken;
  * the DEPARTURE ADDED (`grey + S (probe - mean)`), "the middle ground":
    taken at 1. Anekbah's lit stretch at 1: Kay'l's red +33%, green +27%,
    blue +13% (the city's mean is warm, so blue moves least).
  * Read by its CORNERS first, Lahoreh's large floor triangles left spots with
    no corner in reach - the surface by area replaced it.
  * **The reader's play test, 2026-10-20 - "Lighting is not correct near the
    fire"** (Anekbah's gas flame `AGAZ`, (1741, -54, -7517)): Kay'l BLACK by
    it. The flame is a sprite and its glow was never painted into the
    alcove - the probe reads 0.25 there against the set's 0.44 - and ADDED,
    that departure is six times Anekbah's grey: the start clamped to 0. A
    darker place now SHADES in proportion (0.52 of the grey there); the
    brighter side stays added. (The original lights no body by that fire
    either - its grey and the set's lamps only; light from the effect
    sprites themselves would be a feature of its own.)
* **Measured** (`texpack_tool probe`, which prints the tint of a 0.2 grey):
  floor means Anekbah 0.4431, Jaunpur 0.6958, Lahoreh 0.7931, Qalisar
  0.4647, Kay'l's flat 0.6051. Anekbah's street tints (0.99, 0.92, 0.76) at
  (1804, -90, -6890), red 2.21 600 units on, 0.52 by the fire; Lahoreh's street start ~1 (an
  average place). Along 1.2 km walks the tint of a 0.2 grey moves at most
  0.04 per 10 units (its start colour 0.008).
* **The plumbing.** `Draw::lightTint` (1, 1, 1 unless the renderer says
  `drawsBodyTint`); Vulkan: one vec4 after the affines in the pose block
  (nothing moves), `scene.vert` multiplies; GLES: a posing-program VARIANT
  (`uLightTint`) made at init only when asked - the Vita's shader cache
  untouched; the CPU paths (the frontend's posed corners, `lightStart`,
  `poseGeometry`) multiply too. The software reference, GL1 and the 3DS
  never get a tint.
* **Found on the way: the GLES pose self-test drew its bodies BLACK** with
  the variant - a uniform starts at 0 - and every body went the CPU way (70
  pixels moved instead of 5931). The tint is defaulted to 1 at init.
* **Off is the original:** frames byte-identical to the build before
  (`0069efa7`, a worktree) on the software reference, Vulkan and GLES, plain
  and with `--remaster`, Anekbah's street and Jaunpur's canal - 10 of 10,
  and under `--enhance-all` on both backends. At 1, Vulkan moves 3011
  pixels of Anekbah's street, GLES 3042.
* **Under `--enhance-all` (the reader plays with it, 2026-10-20: "from now,
  run it with --enhance-all")** PER-PIXEL lighting shades a body in the
  fragment stage from the scene's grey (`Draw::lit` 1) and never reads its
  corner colour, so the tint did nothing there. A tinted body is drawn in
  mode 2 instead - the shaders' "lit ADDED to the corner colour", unused
  until now - with that colour the grey x tint (the CPU's corners; a
  GPU-posed body starts from the grey and `scene.vert` tints it). The
  traffic keeps mode 1. Anekbah's street at 1: 3250 pixels move on Vulkan,
  3208 on GLES; with it off byte-identical to `0069efa7` on both.
* **Near a lamp the tint is small** (measured, a forced 0.25 barely moved
  Kay'l by the Anekbah lamp without per-pixel lighting): the set's lights
  add on top and outweigh the grey. Tinting the LIGHTS too was rendered and
  not taken - the painted colours already contain the lamps, so it counts
  them twice.
* `engine: body light` new, shown to fail four ways (the Vulkan tint
  dropped, the GLES variant empty, the weight made a step, mode 2 left out
  under per-pixel lighting).

**CONFIRMED IN PLAY** (the reader's "commit", 2026-10-20, with `--enhance-all`). **Open**: the props and
the vehicles (posed by the traffic, untinted); a probe per BONE rather than
per body (a tall body under a lamp); the day/night slots (`sub_41E7A0`
rewrites the scene's grey - the probe reads the baked colours, which do not
change).

## Step 3, A5b - the emitters' light: as built (2026-10-20)

The reader's "continue with the emitter lights", after the body light's play
test by Anekbah's fire ("Lighting is not correct near the fire"). `[Remaster]
fxlight = S` / `--fx-light S` (0..4), `--remaster`'s **1**; off unless named,
never `all = max`, Vulkan and GLES (`o3de/emitterlight.h`).

* **Why.** A set's flames, neon halos and lamp glows are the `.sfx`'s
  ADDITIVE PARTICLES: they light nothing. The original lights a body from its
  scene's grey and the set's `.3DO` lights; none sits at a flame, and the
  flame's glow is not painted into the walls either (the burner's alcove
  reads 0.25 against Anekbah's 0.44). So the fire's corner is dark in the
  original too.
* **Which emitters glow** - read from the effect, not its name: additive
  (mode 4/5) and either a STANDING glow (a life of 2 frames or less, one
  sprite re-made on the spot every frame - a lamp's halo) or a stream whose
  SPRITE is saturated (a flame: the explosion sprite's bright texels 0.55;
  the smoke sprite's 0.22). Judged by the product with the colour ramp
  first, the burner (`agaz`, ramp (17,17,17) -> (88,136,154)) read 0.31
  against the steam's 0.22 - too close; the sprite separates them. Anekbah
  117 glows and 12 flames of 153 (24 steam and smoke dark), Qalisar 89 and 8
  of 104, Jaunpur 15 and 0 of 47, Lahoreh 12 and 0 of 22.
* **The light.** A `Light3do` of the set's own kind appended to the slot's
  table when its emitters are bound (`WorldSlot::setLights` keeps the set's
  own count), so per-vertex (`o3de/vertexlight`) and per-pixel lighting take
  it unchanged. A POINT light: a NEGATIVE inner radius (no shipped record has
  one) shines from the emitter to the lit point instead of along the record's
  fixed direction, and wraps 0.3 round the far side (the walls about a flame
  bounce it), and is NOTHING within 30 units of its emitter, whole past 90 -
  a lamp does not light its own housing. Colour: a glow the sprite's bright
  texels x the ramp; a flame its SPRITE's hue (seen orange - the ramp only
  dims it). Reach 4.5 m (glow) and 9 m (flame), full inside a fifth;
  intensity `S x brightness x 0.8` (glow) or `x 1.2 x min(4, sqrt(count x
  life))` (a flame's particles overlap). By eye: the first strengths (0.6 /
  1.5) lit the whole frame; 0.5 / 0.6 at 6 m the reader found "too weak, the
  fire is big"; now 0.8 / 1.2 at 9 m.
* **The set lit too, per pixel.** On a body alone the light did not show: the
  camera sees Kay'l's back, and the walls stayed dark. Under per-pixel
  lighting the SET's opaque draws take `Draw::lit` **3** - the baked colour
  plus the POINT lights alone (the set's own lights are in its bake). The
  frame's point lights are the 4 nearest the CAMERA within 20 m, faded in
  over the last 5 m so none pops; the set's own lights nearest the player
  fill the other 4 of the 8.
* **Backends.** Vulkan: `scene.frag`'s `litColour` takes the point case and
  a points-only pass (mode 3). GLES: a VARIANT of the enhanced fragment made
  at init only when asked (the program the Vita may cache untouched).
* **Measured** by the burner (1780, 0, -7380) at 800x600 with the preset and
  `--enhance-all`: 329514 pixels move, 4 (Vulkan) / 8 (GLES) darker, the two
  405 apart. Off: byte-identical to `26e042ff` on the software reference,
  Vulkan and GLES, plain, `--remaster` and `--remaster --enhance-all`, by the
  burner and in Qalisar - 14 of 14.
* **The reader's play test, 2026-10-20**, three findings:
  1. the fire too weak for its size - its reach 6 -> 9 m, its intensity
     doubled;
  2. **the sliders BLACK** under `--enhance-all` - not this feature (with
     it off the frames are byte-identical to before) but per-pixel
     lighting's: it drew the TRAFFIC in mode 1, from the scene's grey (10 of
     255 in Anekbah), while the engine never lights a vehicle - per vertex it
     draws in its own colours. The traffic is now mode 2 under per-pixel
     lighting: its baked colour plus the lights (an ENHANCEMENT's fix; the
     original per vertex is untouched);
  3. **a neon's arrow washed out**: its glow emitter sits on the sign's disk
     and lit the disk to white - the self-fade above. And "if the neon now
     emits light, make the grade darker for more contrast": `omktexpack.py
     grades --depth G` raises every channel to G after the city's grade
     (black and white kept, the mids darker); the pack's grades are now
     depth **1.2** (the depth-1 ones kept as `grades-depth1/`).
* **The neons: "very small, it is not visible that they emit light (they
  seem phosphorescent at most)"** (the reader, 2026-10-20 - and "letters not
  emitting light in the original is more a GPU limitation than an artistic
  vision"). The glows lit from were the white HALO sprites; the pink letters
  emitted nothing. Now the SET'S GLOWING SURFACES are lights too
  (`surfaceLights`, built at set load into `WorldSlot::fxSurface`): a texel
  is light past 0.6 of the pack's LIGHT MAP (the bloom's; weighted above it -
  the maps mark every bright painted texel too) or by the NEON rule, bright
  (value past 0.65) and saturated (past 0.55) - Qalisar's pink letters carry
  no light map. Each triangle is sampled on a LATTICE in texture space (2..16
  a side): a sign's letters are thin texels inside a keyed-out quad, and its
  corners sampled the key (the first version found no light at the sign).
  Summed by area into 3 m cells; a cell with 300 square units of light is a
  point light at its centroid, its colour the lit texels' pushed 1.6 from
  grey (a tube's core is near white), 7 m reach, `S x min(1, sqrt(area) /
  120)`. Anekbah ~390, Qalisar 304, Jaunpur ~320. Decoding the pack for
  them added ~110 ms to a set's load (M3) - the colour PNGs, 1024x1024 each,
  decoded beside the GPU's own decode; now `replacements(ts, false)` reads
  the light maps alone and the colours are the shipped palette's: Qalisar's
  load 131 -> 53 ms (21 with the emitters' light off), 217 -> 216 lights
  (2026-10-21). And the frame's light slots
  are 16 now (`View::kMaxGpuLights`; the set's own still 8,
  `kOwnGpuLights`): up to 12 point lights, nearest the camera, where the
  renderer has the room (`maxPixelLights` - Vulkan always, GLES in its
  variant).
* **The reader's play test in Qalisar, 2026-10-20**: "light is not very
  visible (maybe it is already too bright around or the light is not strong
  enough) - the inspiration for this area is clearly Blade Runner and its
  night scenes, the neons one of the main sources of light"; and "some
  lights appear and disappear when I move". Three answers:
  * THE NIGHT: `night.txt` beside the city grades (`omktexpack.py grades`
    writes `QALISAR = 0.5`; `CityGrades::nightOf`, a set's own then its
    city's): with the emitters' lights on, the share of the SET's painted
    light and of a lit body's start that stays (`View::setDim` - Vulkan the
    eye's w in the lights UBO, GLES `uSetDim` in the variant, defaulted to 1
    at init); the point lights add on top whole. The sky is not dimmed.
  * POOLS: a point light's falloff is SQUARED (shaders and `reachOf`), so
    the light gathers near its source and the street between stays dark -
    linear over 7 m, the shop panels' warm light washed the whole ground.
    Surfaces' colour pushed 2.5 from grey (1.6 left the pink sign's light
    pale `ffd6ca`), intensity 1.2, cells 5 m (fewer, stronger).
  * NO POP: the nearest 12 by distance swapped as he walked; a light now
    fades by its distance against the FIRST ONE LEFT OUT (`fxCut`, the 13th's
    distance, which moves continuously): one leaving the 12 has faded out.
* **"Better"; then: the light's colour from the neon texture's colours, and
  the lights too few - "the scene darker than it should be, the neons look
  turned off"** (the reader, 2026-10-20). The colour is now the lit texels'
  at full value weighted by their SATURATION (a white core or a grey texel
  adds light, not hue; pushed only 1.3 from grey). The slots: 64
  (`View::kMaxGpuLights`), the set's 8 as ever (`kOwnGpuLights` - the
  per-pixel enhancement's, not the original's: the engine lights per vertex
  from every light that reaches) and up to 56 point lights within 30 m of
  the camera - only with the emitters' light on, on a renderer that says so
  (`maxPixelLights`; GLES in its variant). With so many adding, the surfaces
  and the glows at 0.5 (1.2 and 0.8 washed the street), surfaces reaching 10
  m. The cost at 4K (M3, Qalisar, `--enhance-all`): Vulkan 92 -> 50 fps,
  GLES 97 -> 50 - above the 30 yardstick; a per-tile light list would win
  most of it back.
* **"Still looks off" - the CURATED LIST** (the reader, 2026-10-20: "go with
  the curated list"). The rules cannot tell a neon tube from a bright painted
  panel - both bright, saturated texels; tuned by numbers the street swung
  between washed and dark, the warm panels outshining the pink sign. So
  `lights.txt`, beside the grades (`SurfaceCuration`): lines `SET TEXTURE
  [U0,V0,U1,V1] = GAIN [RRGGBB]`, `*` any set. A texture is an ATLAS
  (Qalisar's `CATITR05`: a yellow emblem panel, lit windows and the pink
  letters), so an entry names a RECTANGLE in the original's texels; a texel
  takes the first rectangle it lies in, else the texture's line, else - the
  set being curated - no light. Which texels glow stays the rules'; the gain
  replaces the neon boost, the colour the texels'. A set the list does not
  name keeps the automatic rules. `texpack_tool lights <pack> <set.3DO>`
  lists the candidates (lit area, neon share, colour, pack key);
  `omktexpack.py grades` writes the first draft (`CITY_LIGHTS`, Qalisar's 20
  lines, from its atlases) only where there is none. At 4.0 the pink letters
  flooded the street; 2.0 kept it a magenta night. "Much better."
* **The neons' bloom** ("add just some bloom to the neon"): the bloom glows
  where the light maps say, and the pink letters have none. A hook on the
  pack (`TexturePack::setLightHook` -> `curateBloom`, on the pool) writes the
  curated rectangles' texels into the light map by BRIGHTNESS (0 at a value
  of 0.3, whole at 0.7 - the neon rule's 0.65 missed the letters' 0.77-0.86)
  times `min(1, gain / 2)`. At half it did not show; spreading the glow 2
  texels did nothing - the letters are a CUTOUT, their neighbours never
  drawn - so "a bit more bloom" is the preset's bloom 1.3 -> **1.6** (2.0
  washed the disk's arrow out again).
* `engine: emitter light` new, shown to fail four ways (a flame by the
  product's saturation; the GLES variant empty; mode 3 never set; the list
  never covering a set). Part 4, the list and the night, in Qalisar and
  Anekbah. On the way: the check compared the backends at TWO FILTERS -
  `_viewer_args` adds `--filter nearest` to a run naming none, the Vulkan
  harness took it and GLES under `--enhance-all` did not (3346 pixels apart);
  the filter is named now. Whether GLES should honour `--filter` under
  `--enhance-all` is OPEN.

**CONFIRMED IN PLAY** (the reader's "it's good now", 2026-10-20, with
`--enhance-all`: the fire, the sliders, Qalisar's night and neons). **Open**:
the LIGHT POPPING - the frame's point lights are the 56 nearest the camera,
so a far neon lights nothing round it and seems turned off ("set it by the
game's distance culling"): per-TILE light lists, every light within the clip
distance, is the next step (done, A5c); the light maps' decode at a set's
load (done: 131 -> 53 ms); the other cities' lists (drafted); without per-pixel lighting the lights reach
only the bodies; flicker (a flame's light is steady).

## Step 3, A5c - the tiled point lights: as built (2026-10-20)

The reader's "go" on the per-tile light lists, after "the neons look turned
off" and the light popping (the frame's point lights were the 56 nearest the
camera). `o3de/lighttiles`; Vulkan only for now - GLES keeps the nearest-slots
way.

* **What.** On a renderer that `tilesLights`, EVERY emitters' point light
  within the game's CLIP DISTANCE (the reader: "set it depending of the game
  distance culling setting"; 200 m at most, the unlimited clip's) goes to
  `View::points`, faded over the distance's last fifth - no slots, no cut;
  `View::lights` keeps the set's own 8. Vulkan writes them to a storage
  buffer (set 1, binding 3) and BINS them each frame: the screen in tiles
  (32 render pixels, growing so that no more than 64 lie across), each tile
  in 16 depth slices (exponential, 50 to 20000 units), a light joining a
  CLUSTER only where its sphere reaches the cluster's own box of view space
  (the tile's rays between the slice's depths); the lists (binding 4,
  16 MB) at most 64 a cluster, the nearest IT. `scene.frag` reads its own
  cluster's list (`vDepth` its slice) - every light within reach, at the cost
  of the ones that can reach it.
* **The steps the reader's play tests took it through:**
  1. 2D tiles alone: 880 of 880 tiles at their cap - a near light covers most
     of the screen and crowded the far ones out again. Depth slices: still
     7920 of 14080 clusters full.
  2. "The lights disappear when I put a resolution higher than 1512x982":
     the lists grow with the resolution and overflowed the 4 MB buffer,
     and the frame dropped every point light silently. Tiles now grow with
     the resolution (64 across at most - 60 px at 4K), the buffer is 16 MB,
     and an overflow is SAID.
  3. "Some kind of flickering big shadows": full clusters dropped lights as
     the camera moved, a block of street at a time. The cluster BOX test and
     the ranking by distance to the cluster: 0 clusters full in Qalisar, the
     lists 8x shorter (50883 entries against 421104 at 1280x960), and the
     frame byte-identical to `OMK_TILES_LOOSE=1` (no test, no cap: 725870
     entries) - a light the box test leaves out reaches none of the cluster's
     fragments. "It looks good now."
  4. Found on the way: the tiled loop PRESENT in `scene.frag`, never run,
     changed a plain frame through MoltenVK - a cutout sign's keyed texels
     drew dark (4317 pixels in Qalisar; bisected to the loop alone). So the
     shader is compiled TWICE (`scene_tiled.frag.inc`, -DOMK_TILED) and the
     tiled pipelines bound only on a frame whose tiles are on; the default
     module is the commit before's.
* **Off is the original:** byte-identical to `18da7d4d` on the software
  reference, Vulkan and GLES, plain, `--remaster` and `--enhance-all` (fx
  off), by Anekbah's burner and in Qalisar - 14 of 14.
* **Cost** (M3, 4K, Qalisar, `--enhance-all` and the preset): 48 fps (the
  nearest-56 way 50, 32-px tiles before the box test 56 but with half the
  clusters full).
* `engine: emitter light` part 5 (more than 200 lights tiled, 0 clusters
  full, tight == loose byte for byte), shown to fail (the box's vertical
  bounds' sign flipped - tight and loose part); its backends' agreement sets
  Vulkan beside GLES with `OMK_NO_TILES=1` (GLES does not tile yet).
  `licence headers` 644 -> 646.

* **The four cities' lists** (the reader's "continue", 2026-10-21): Qalisar
  was the only city curated, the others on the automatic rules, which lit
  white walls. First drafts from each city's atlases (`texpack_tool lights`
  and its textures beside their light maps), in `omktexpack.py`'s
  `CITY_LIGHTS`: ANEKBAH 20 lines (the orange letters and the neon star
  strong, the signs and the lamps bright, the lit windows low, the paintings
  out); JAUNPUR 7 and LAHOREH 5 - sunlit cities, their signs alone at 1, the
  white sandstone and pale walls the light maps mark left out. Kay'l's flat
  (AAPKAYL) is named by none and keeps the automatic rules. Before/after by
  eye: Anekbah's street loses the warm-green wash and keeps pools by its
  lights; under Jaunpur's bridge the walls' false light goes. `engine:
  emitter light` part 4: Anekbah curated, Kay'l's flat not.

**CONFIRMED IN PLAY** ("it looks good now", 2026-10-20, `--enhance-all`).
**Open**: the three new lists by the reader's eye; GLES tiles (float textures for the lists, desktop and Quest); the
CPU binning's per-frame allocations; a light's own shadow (none - a lamp
lights through walls within its reach).

## Step 3, B3 - the relief (normal maps): first cut (2026-10-21)

The reader's "go with the normal maps". No generated maps yet: the first cut
reads the texture's own BRIGHTNESS as height in the shader, so it needs no
asset, no pack format and no tangents, and shows whether relief is worth the
pack's maps (DeepBump and the like, the plan above) at all.

* `[Remaster] relief = S` / `--relief S`, 0..4, **0 by default**; the
  `--remaster` preset's **1** (**2** since the generated maps, below) - the reader's "ok" (2026-10-21) on a sheet of
  0, 1, 2 and 4 by Anekbah's burner and in Qalisar (4 too harsh; Qalisar's
  wide pink light shows little at any strength; the lit floor brightens a
  little as it grows - a bias to correct if a higher strength is wanted). `View::relief` ->
  the Lights UBO's `fx.x` (Vulkan).
* `scene.frag` `relief()`: four taps a texel apart on the BOUND texture (the
  pack's 4x where there is one), the slope x8 a texel, tilting the normal in
  the COTANGENT frame of the surface's screen derivatives (world position,
  UV) - the sets carry no tangents.
* Only the POINT lights see it (the painted light, the bake, has no
  direction), so it needs the emitters' light and per-pixel lighting
  (`--enhance-all`); only on the SET (mode 3) - Kay'l's shoulder pad and arms
  took grain from their painted texture; only in Vulkan's TILED module
  (`#ifdef OMK_TILED`, the MoltenVK lesson of A5c). GLES has none.
* x4 was barely seen: the median moved pixel 8 of 255 at 1, a crop at full
  size showed the wall's grain only at 2-4. x8 makes 1 what 2 was.
* Off: byte-identical to `e0b586b1` on the software reference and Vulkan,
  plain, `--remaster` and `--enhance-all`, by Anekbah's burner and in Qalisar
  (12 of 12). `verify.py: engine: relief` - the source, relief 1 moving the
  frame by the burner (81875 pixels at 640x480), and nothing without the
  point lights or the tiles; shown to fail three ways.
* Open: the strength and the sign (bright reads as raised - true of stone
  and mortar, not of a lit window); whether generated maps are worth it
  after this; GLES.

## Step 3, B3 - the generated normal maps: as built (2026-10-21)

The relief's second step: a NORMAL MAP per pack texture, `<key>_n.png`
beside its colour, read by the same `relief()` where there is one (the
brightness elsewhere). **The preset's relief 1 -> 2** - the reader's
"relief 2 is better" once the maps came.

* **The generator** (`engine/src/o3de/normalmap.{h,cpp}`, `texpack_tool
  normals`): the reader's pick from a sheet of seven textures - DeepBump's
  map (HugoTini, GPL-3, colour to normals; it reads VOLUMES but invents
  blotches and streaks) blurred until its inventions are gone, as the large
  forms, plus the engine's own band-passed brightness slope over four scales
  as the grain. Without DeepBump the map is the engine's alone, which anyone
  can rebuild. OpenGL's convention (R +u, G the image's up), the height in
  alpha. `tools/omktexpack.py materials PACK --deepbump DIR --python PY`;
  DeepBump's maps cached in `PACK/../materials-ai/` (1259, the long part),
  the whole pack regenerated from that cache in 79 s (M3, 8 jobs).
* **The engine**: `TexturePack` lists `_n` files beside `_e`; decodes them
  only when the session asked (`setNormals`, relief above 0) - another
  1024x1024 PNG per texture. Vulkan: binding 3 of a texture's set, a flat
  1x1 of ALPHA 0 where there is none (the shader's "none"); the descriptor
  pool 390 -> 530. GLES has no relief.
* **The floor's line** (the reader's "do it first"): a crisp groove along
  some tile edges of Qalisar's pink street. Found by bisecting the pack's
  maps (11 renders): `BATITR09`, an ATLAS - beige paving, a long green
  slab and an unused bolted plate in one 256x256. DeepBump draws a wide
  ridge where two pictures meet; a floor face maps the beige paving up to
  its border, and at a grazing angle the sampler's coarse mip reaches into
  the ridge. NOT the cause, measured on the way: the upscaler's border (the
  pack's edge texels step 2-14x an interior step, but the ORIGINAL's own
  wrap seam, sharpened) and DeepBump's unwrapped edges (handing it a
  wrapped margin of an eighth changed nothing - removed again).
* **The fix - the faces' REGIONS** (`texpack_tool islands <data> <dir>`,
  `<key>.cov`, made once into `PACK/../materials-cov/`, 33 s): every face's
  UV triangle over every model, rasterised by texel centres; triangles
  sharing a centre a texel or more inside each are ONE region (union-find),
  so the beige and green, which meet on a line, are two. 1253 textures,
  664258 faces, 48484 regions, 3.5% of texels a border. The generator
  fades the slope flat over 16 texels (at 1024 across, four of the
  original's) from a border or an unsampled texel: 8 left the line, 16 and
  32 took it. 0 of the 664258 faces spans more than its texture - the sets
  do not tile a texture across one face.
  Tried and dropped: a UV edge only one triangle has (170786 - the sets cut
  their faces freely, most such edges inside a picture); a 3D edge across
  which the texels jump (567299 - the paving restarts at nearly every face:
  Qalisar's floor, 5003 shared edges, 895 jump, 2123 one-sided).
* Off: byte-identical to `b39143ac` on the software reference and Vulkan,
  plain, `--remaster --relief 0` and `--enhance-all --relief 0`, by
  Anekbah's burner and in Qalisar (8 of 8). `verify.py: engine: relief`
  part 4 - the binding in the tiled module, normal maps uploaded at relief 1
  (39 by the burner) and none at 0; shown to fail (`setNormals` never
  called: 0). Relief 1 still moves 85210 pixels there.
* **The freeze entering a bar in Qalisar**, met in the play test (`b39143ac`):
  the bar's set loads on a `BackgroundJob` whose pack decode is a
  `parallelFor`, beside the frame's own on the main thread; the pool held
  one call's state and the loader waited for ever. Older than the maps -
  since the decode went onto the pool (`30f2328e`) - which made the window
  long enough to meet. A call lock per pool; `verify.py: thread pool`.
* Open: a faint trace of the line remains at relief 2 at a grazing angle;
  the one stretch of BATITR09's beige/green border no region parts (a face
  maps across both, by the looks); the sign (bright as raised) in the
  engine's grain; GLES.

## The review of steps 1-3 (2026-10-21)

The reader's "recheck everything you did for the remaster". Four read-only
reviews (the Vulkan backend, GLES and the plumbing, the CPU side, the tools
and checks), each finding re-read in the code before a fix; one rejected.

**Fixed:**
* `TexturePack`: `refused_` and `greyOf_` raced (a set's load on a
  BackgroundJob against the renderer's call and `aliasGrey`) - a mutex; a
  refused COLOUR no longer drops the key's light map; light maps decoded
  with the colours only when a bloom reads them (`setLights`), no normal
  maps for GLES (no relief there).
* The pool: a call finding it held by another thread runs INLINE rather than
  wait (the frame stalled for a set's decode); the Vita's caller count, now
  raced by two callers, replaced by the owner's thread id and a polled
  semaphore.
* The bloom: what no surface covers cleared to alpha 0, not 1 - the fog past
  the clip distance glowed as light (Vulkan and GLES; 0-226 pixels moved in
  three views, none by more than 8).
* GLES: the variants built over BOTH program sets, so the plain fallback
  links when the enhanced one does not (it inherited the enhanced fragment
  and GLES did not start); the light-map binding cache reset when textures
  are dropped; light maps freed at shutdown; the water's two uniforms
  through the cache; the post-pass's uniforms looked up once.
* Vulkan: a failed grade upload rebinds the post sets (they held a destroyed
  view). `Renderer::drawsWater()` asked before the shadow pass.
* `curateBloom` / `surfaceLights`: the curation's lists found once a texture,
  not by name per texel (a string each). `.cube`: `LUT_3D_INPUT_RANGE 0 1`
  read (Resolve's); the night factors cleared on a re-open.
* `omktexpack.py materials`: maps and DeepBump's written under another name
  and renamed, `islands` finished by a `.done` - an interrupted run left
  files a later one trusted.
* The checks: six remaster checks took "no frame" / "no vulkan device" as
  their own answer - a crashed viewer passed (`_or_skip`: only a backend not
  tried at all may stand in); water and body light compared GLES at bilinear
  against Vulkan at nearest and shared one saves folder; the pack through
  `omkpaths.pack_path()` (`$OMK_PACK`, `pack =`); timeouts. New `engine:
  normal maps` (the generator and `islands`, shown to fail both ways).
  `engine: bloom and grade` had been red since the AO (`|| aoOk_`).

**Rejected:** "GLES claims the emitters' light it cannot draw" - mode 3 needs
per-pixel lighting, which a failed enhanced link already refuses
(`playsetup_bodies.cpp`); and the emitters' light reaches the BODIES on GLES
per-vertex through the set's light table, which a refusal would have lost.

**The second pass** (the reader's "then do the second pass"; everything on
byte-identical to `4eb586ae`, 18 of 18 - the burner, Qalisar, Jaunpur's
canal; software, Vulkan, GLES):
* The light binning (`lighttiles.cpp`): one flat list of (cluster, distance,
  light) and a STABLE counting sort, its scratch kept from frame to frame -
  one vector a cluster (~37k at 4K) was allocated every frame; the words are
  the same bit for bit. The reflection's `begin` bins nothing (the frame's
  pass overwrote it and the reflection's shader skips the tiles).
* Under the AO the water reflection has its own render pass, its depth not
  stored (`reflPass_`; store ops leave the pipelines compatible).
* `setTextures`: light maps, normal maps and the 1x1 stand-ins uploaded
  without a descriptor set (each allocated, written and freed at once), and
  a failed allocation said and drawn white instead of written into.
* `makePost` remembers a failure for its size (it rebuilt and said so every
  frame); `LightUbo` sized by `View::kMaxGpuLights` and its offsets pinned by
  a `static_assert` (the comments said 400 / 416; they are 3088 / 3104).
* GLES: a variant whose anchor is not in the shader SAYS so (it built and
  did nothing).
* Shared: `lutStrip` (`colourgrade.h`, the grade's strip, written twice);
  `[Remaster]`'s keys through two helpers (`settings.cpp`); the light
  selection's and `drawWithWater`'s vectors kept between frames.
* `omktexpack.py`: `anchor()`'s docstring said one pass restores the box
  averages - it halves their error (4.96 -> 2.56; three passes 1.05);
  `build --anchor-passes N` for a rebuild, 1 the default (the judged pack);
  the emissive rule's medians over the drawn texels; argument errors said.
  `texpack_tool`: `keys` and `islands` share the mesh walk, `pngpar` refuses
  a count below 1, the usage names every mode.

Not done, and why: the water reflection's image in its own pass's sets - the
shader never reads it there (`pc.water == 1` is false), and the spec's
layout and feedback rules bind only descriptors a draw ACCESSES; one helper
for the session's flag / key / preset picks - three checks read those lines;
a memo of `source_textures` - the build walks keys, not models.

**The anchor-3 rebuild (2026-10-21), NOT ADOPTED, the reader to judge.**
`build --anchor-passes 3` into `omk-pack/full-anchor3/` (1255 textures, the
`_e`/`_n` maps linked from `full/pack`), against the judged pack (1 pass):
every texture differs, mean |diff| median 1.04/255, p90 2.07, max 7.78. The
sheet `full-anchor3/compare-sheet.png` (original | 1 pass | 3 passes; the
six most changed and two typical) shows what the number says: three passes
pull the noisy textures back toward the source's pixel grid - blockier on
the moss/water/stone grains, no visible change on drawn ones. To try it in
play: `--texture-pack ~/Documents/omk/omk-pack/full-anchor3/pack`.

Still open, the reader's to choose: the reflections (water and the authored
mirror) without the tiled point lights - a binning for the reflected camera
(DONE for the water and the floors, A3 continued); water and an authored
mirror in one frame; the light maps uploaded as one channel (R8 / luminance),
a quarter of their memory - **DONE 2026-10-21**: Vulkan `R8_UNORM` where the
device blits and filters it (`r8Ok_`, else RGBA as before; `@evt texpack.pool
lightChannels=1` on the M3), GLES `GL_LUMINANCE` (as `maskTex_` already was);
the shader reads `.r` - byte-identical to `1f3ff2fa` with the bloom on, 8 of
8 (Vulkan and GLES, plain preset and `--enhance-all`, the burner and Qalisar);
a 1024x1024 map 4 MB -> 1 MB.

## Step 3, A4 - the ambient occlusion: as built (2026-10-21)

The reader's "continue" after B3; confirmed in play at 2 ("commit, push").

* `[Remaster] ao = S` / `--ao S`, 0..4, **0 by default**; the `--remaster`
  preset's **2** since 2026-10-21 (the reader's "add ao 2 to the preset" -
  first left out, the reader having played it at 2 and named no strength).
  Vulkan only; GLES has none.
* The frame's DEPTH, kept only when asked: `makeTarget` makes it SAMPLED and
  STORED under `aoOk_` (the format's sampling checked), with a depth-aspect
  view. In the post pass (`recordPost`, run when bloom, grade or AO is on):
  `ao.frag` mode 3 reduces it to a HALF-size view z (R32F; sample 0 of the
  multisampled depth - `-DOMK_MS`, a second module), mode 0 the occlusion
  (8 samples on a disk of 60 game units, at most 4% of the picture's width,
  the Alchemy estimator, faded past 80 m and by the game's own FOG), modes
  1 and 2 two blurs weighted by depth; `post.frag`'s final step MULTIPLIES
  the frame by it, not on a light map's texels (the bloom's alpha).
* Contact only: the sets' vertex colours hold the painted occlusion. At 4 a
  dark halo spreads round the feet; a character standing ~30 cm from a wall
  darkens from 1 up (physically right - "characters cast but never receive"
  is offered, not built).
* Cost (M3 loaded, 4K Qalisar, preset + `--enhance-all`): 44.7 fps off, 40.3
  at 2 (2.4 ms). The first cut read the 4K multisampled depth 17 times a
  pixel: 31 fps; keeping the depth alone costs nothing (45).
* Off: byte-identical to `7ac3b75f` (12 of 12). `verify.py: engine: ao`,
  shown to fail two ways.
* **The SET's alone, 2026-10-22** - the reader, after play in Qalisar's bar
  and a shop at `--remaster --enhance-all`: "the shadow and the bloom for
  characters, they seem too strong". Two causes, both fixed:
  * the AO on the BODIES: a low-poly figure occludes itself (chins, armpits,
    legs black) and haloes the wall behind it - "characters cast but never
    receive" was too little. Now a body NEITHER takes NOR casts it:
    `scene.frag` writes its alpha 0 (`caster`, every body path), the set's
    1/255 plus its glow in the 254 steps above (a mark at the TOP would
    collide with full glow: additive draws add alpha); `ao.frag`'s depth pass
    takes a body's pixel for the sky, `post.frag` reads the glow through
    `glowOf`. By Anekbah's burner `--ao 2` moved 12310 pixels, 7605 in the
    player's box; now 2085 and 404. The ground under a figure keeps the
    shadow map's shadow;
  * the GLOW on the bodies: `omktexpack.py emissive`'s rule (much brighter
    than the texture's median) took pale skin and white cloth for light -
    229 of 303 character textures had a light map, faces and legs glowing
    under the bloom. No character emits in the game: the rule skips
    `MESHES/PERSOS` now, and the pack's 279 character `_e.png` were MOVED to
    `full/lights-persos-removed/` (kept, not deleted).

**Its cost at 4K, 2026-11-13** (`todo/perf-audit.md` 11, `OMK_GPU_TIMES` now
splitting the post-pass): at 2560x1440 drawn, the half-size depth 0.29 ms, the
estimator 2.08, the two blurs 0.45 and 0.21. The estimator's 16 cos/sin a pixel
are two now (a0's rotation of the 8 constant golden-angle directions, from the
same float products): the frame byte-identical, 2.1 -> 1.8 ms. The rest is its
8 scattered depth reads a pixel (the disk turned per pixel).

## B2 - the curation: the mechanism and a draft, NOT applied (2026-10-21)

The reader's choice: "build the tool + draft, don't apply" - which textures
leave the pack is theirs, by eye.

* **The mechanism**: `PACK/exclude.txt`, a 16-hex key a line (`#` comments):
  a listed texture draws AS SHIPPED - its colour and its normal map left out,
  its light map kept (read off the original's texels); the engine says how
  many ("left as shipped by exclude.txt", `@evt texpack.open excluded`).
  `texpack_tool open <pack>` answers the counts; `verify.py: texture pack
  curation` (a made pack of two, shown to fail two ways - the file unread,
  the normal map kept).
* **The draft**: `omktexpack.py curate ROOT` writes `ROOT/exclude-draft.txt`
  (48 keys, outside the pack: nothing applied) and contact sheets
  `ROOT/curation/sheet-NN.png`, the original beside the pack's. The score: the
  ORIGINAL's "ink" (luma 40 from its 5x5 mean) cut into glyph-sized marks,
  counted only in LINES (four or more on a baseline, alike in height, close) -
  the first cut counted every small mark and drafted noisy weapons and
  mottled walls - plus a bonus for names that say lettering (TEXT, MAPS, PLAN,
  ECRAN, LOGO, AFFICH...). Its image half finds real labels (PASSP1, PHARMA1,
  BATTERIE, TETRA02, OMNIAM) and some false ones (FOIE, MEGAZ1).
* **For the reader**: read the sheets in `~/Documents/omk/omk-pack/full/
  curation/`, move the agreed lines of `exclude-draft.txt` into
  `full/pack/exclude.txt`.

## Step 3, A4 continued - the mist: as built (2026-10-21)

The plan's "height or volumetric fog in the game's own fog colour and range";
the reader's "0.5 looks fine" on a sheet of 0, 0.5 and 1 by Anekbah's burner,
on Qalisar's street and at Jaunpur's canal.

* `[Remaster] mist = S` / `--mist S` (0..1), the `--remaster` preset's
  **0.5**, off unless named; Vulkan only (both scene modules - `fx` declared
  in the plain one too, which changed no frame: 15 of 15 off byte-identical
  to `5f4cb214`). Named apart from the game's fog.
* **The colour is the game's own fog colour**, per city and per the clock
  (`sub_41E7A0`) - at the save's clock, phase 2: Anekbah 12 25 35, Qalisar 19
  10 38, Jaunpur 12 35 48. Jaunpur's dark teal looked wrong beside its golden
  sky texture, and is the city's own night fog (the reader: "the fog colour
  is not the same on each city"); it follows the clock with nothing curated.
* **The shape**: density 1 at the set's GROUND (`groundLevel` - the height
  with the most level opaque area: 0 for Qalisar, Anekbah and Lahoreh, -120
  for Jaunpur's quay) falling as exp(-h / 400) above it (Y down), the ray's
  mean density in closed form; 15 m to 63% at strength 1 (40 m read as
  nothing over a street).
* **Which batches**: the game's fogged ones - and with its fog OFF (an
  unlimited clip, which `--enhance-all` sets) the same ones (not the near
  bucket, not the transparent state - key 0x2080) marked -1 / -1 by the
  backend: the first cut took only the fogged ones and drew nothing under
  `--enhance-all`. Not in the water's reflection pass.
* `verify.py: engine: mist` - the source; in Qalisar (its fog off, the
  post-pass off) `--mist 0.5` moves 260094 pixels toward the fog colour (48
  away: a street lamp's ADDITIVE halo, which the mist leaves as the game's
  fog does, over a background it moved) and its shape - the near ground 5.7,
  the horizon band 21.5 a pixel; shown to fail two ways (an inverted mix
  passed the first form). `engine: body light` and `engine: emitter light`
  compare Vulkan with GLES at `--mist 0`.
* Open: ~~GLES~~ (done 2026-10-27, "The mist on GLES too" below); the mist in the reflections; a lighter haze by day if the
  day's fog colours want it (unseen: the save's clock is evening).

## The rain's wet floors: as built (2026-10-27)

The reader's "how could the weather be improved in remaster mode (ex: wet
floor when raining)", then "go with 1 + 2" - 1 the floors wet by the game's
own shower, 2 a map of what is open to the sky so a floor under a roof stays
dry. The game's rain is docs/ASSETS.md 4e: Anekbah, the Impasse and its
airlock, the rooftops, Khonsu; one shower every ~11-18 minutes of wall clock,
only at *Niveau de détail* 2.

* `[Remaster] wet = S` / `--wet S` (0..1), the `--remaster` preset's **1**,
  off unless named; OMK's Remaster page "Sols mouillés" / "Wet floors".
  Vulkan and GLES (the floors' pass). `o3de/wetfloor.{h,cpp}`, compiled empty
  without OMK_REMASTER; the frame's half in `playremaster_draw.cpp`.
* **Which floors** (`wetFloors`, at the set's load, ~100 ms for Anekbah):
  every OPAQUE triangle of the set FACING UP within ~20 degrees of level
  (`kWetFlat` 0.94 - a third of Anekbah's street is gently sloped, and a
  level-only rule left it dry along a triangle edge; the floors wind with the
  normal's y negative, 4576 to 97 on the street, so a ceiling is not one).
* **Open to the sky, the original's own roof**: four rays straight up
  (`kWetSkyRay` 4000, ~100 m) from the triangle's centre and three points
  halfway to its corners, through the set's SHOT soup - `sub_481200`'s
  world, the bolts' - open when three or more meet nothing. Per TRIANGLE, on
  the CPU, so no shader carries a sky map: Anekbah 9913 open, 2217 covered.
  The edges between wet and dry are triangle edges (a street triangle is a
  few square metres) - a per-pixel map is the refinement if they show.
* **The edge, per corner** (the same day, the reader on the comparison
  picture - "a straight diagonal where whole floor triangles switch"): the
  open sky is cast at each CORNER too, once per place (neighbouring
  triangles' corners at one point agree), and a wet triangle is one whose
  corners or centre see the sky; `encodeOpenSky` writes `-2 - open` into a
  non-shimmering corner's phase (below -1: no shimmer, and exact under the
  depth tie's -8192), and both shaders blend it across the triangle - the
  film, the ripples and the reflection fade through the triangles a roof's
  edge crosses. 2562 of Anekbah's open triangles blend; blended against not
  (`OMK_WET_NO_BLEND=1`, an instrument) 19514 pixels. A roof over the MIDDLE
  of a large triangle that misses its corners is lost (164 such in Anekbah);
  a per-pixel map would keep it.
* **A roof is wider than a cable**: a top-down map of the test round the
  street start showed the soup's overhead WIRES as thin diagonal lines - the
  old per-triangle majority had dried whole street triangles under them.
  Covered now when three of five rays meet something, the point and four 30
  units round it (`kWetRoofSpan`): wires gone, awnings and walkways kept;
  Anekbah 11480 open, 650 covered. The 3D rain's roof test is still one ray
  (a drop under a wire is not drawn - unseen at a wire's width).
* **How wet** (`Wetness`): as wet as the rain is strong - the weather's drops
  over 200 (`weatherRain`, set by `weatherFrame`) - and drying linearly over
  2 minutes after (`kWetDryMs`); only where the area's weather is the RAIN,
  so a city left behind does not carry its puddles indoors. No rain (row 7
  not 2, a dry area, the dry wait) - no change at all, byte for byte.
* **What it does to them**: every wet triangle darkens by `wet x wetness x
  0.35` (`Draw::wet`; Vulkan carries it in the push block's `water` int above
  its four mode bits - the block is full at 128 bytes; GLES as `uWet` in the
  water variant); those on the frame's plane - the LEVEL ones, by
  `chooseFloor` over the wet planes or the listed floors, the larger near the
  eye - reflect at `wet x wetness x 0.5` through the floors' pass, with the
  water's Fresnel. The listed floors keep their own strength beside it.
* Measured (M3, Anekbah's street at full rain, 640x480): 109969 pixels moved
  on Vulkan, 109950 on GLES. `verify.py: engine: wet floors`.
* **The RIPPLES** (2026-10-27, the same day's "push and continue"): while it
  rains, rings spread on every wet floor - the floor in cells of 48 units,
  at most one drop each at a hashed place, kept where the hash is below the
  rain's strength (a light shower has few); the ring grows 40 units over 64
  of the SHIMMER's clock (32 frames - the engine's own per-frame clock, which
  wraps at 256, a whole number of rings - so a fixed frame count draws the
  same rings) and fades as it goes. Its crest adds 0.10 of light, and on the
  frame's plane its slope bends the reflection's lookup (2% of the frame's
  height). The rain's strength rides in the push block's `water` int's next
  eight bits (`View::rain`; GLES `uRain`, `uRippleClock`, the world's x and z
  as `vWxz`); `scene.frag`'s `ripples` and GLES's `kRipplesGlsl` are twins.
  Frames 700 -> 716 at full rain move 73954 pixels wet, 33097 dry.
  **Softened the same day** (the reader, on the comparison picture: rings
  "too regular, like stamped bull's-eyes" - the slope's length lit both
  halves, two rings a drop): ONE crest `(1 - x^2)^2` across 6 units, each
  drop's ring to its own size (24..48 units), rising over its first tenth;
  the crest's light 0.13, the slope only bending the reflection. 700 -> 716
  then moves 20230 more wet than dry.
* **Next, not built**: the mist and the grade answering the shower; the snow
  settling on Mahaleel's open ground. The cave's snow static
  (ASSETS 4e) is the original's and stays in both modes.

## The rain in 3D: as built (2026-10-27)

The reader's "continue" after the wet floors - item 3 of the weather list.

* `[Remaster] rain3d = S` / `--rain3d S` (0..1, the streaks' brightness), the
  `--remaster` preset's **1**, off unless named; OMK's Remaster page "Pluie
  en 3D" / "3D rain". `backends/remaster/playremaster_rain.cpp`; Vulkan and
  GLES (`kTakesWater`'s backends), and only while the remaster shows (F10).
* **The SAME drops**: `weatherFrame` hands the 200 it stepped to
  `remasterRainTake` - the cycle, the count, the box a third of its size
  ahead of the eye, the wind, all the original's, untouched - and draws no 2D
  streak; the next frame's draw list takes them (one frame late, a few
  units). The snow keeps the original's sprites.
* **In the scene**: each drop a strip from the drop to its tail (12 frames of
  wind, as the line), turned to face the eye, half a pixel each side at
  480 lines (scaled with the frame), ADDITIVE and untextured (a pool slot past
  every texture binds the backends' white; the mesh path's 0x2100 state),
  from the fog colour at the drop to 0xC0C0C0 at the tail, times S. The depth
  test hides it behind a wall - and below the street, where half the
  original's box lies: Anekbah's street shows fewer streaks than the 2D
  rain, the same 200 with the impossible ones gone.
* **Stopped by a roof**: a ray straight up (4000) from each drop through the
  set's shot soup - `sub_481200`'s world, as the wet floors' - and a drop
  that meets one is not drawn: 31 of 200 in Anekbah's street at frame 690,
  135 drawn (the rest off screen or behind the eye).
* Measured (M3, 640x480): the rain's pixels 2110 on Vulkan, 2109 on GLES,
  overlap 1.000. `verify.py: engine: rain 3d`.
* **Denser and fainter** (the same day, the reader on the comparison
  picture - "far fewer streaks"; asked whether transparency would help: yes,
  rain is seen through): each drop brings three more at fixed offsets in the
  same box (`kRainCopies`), wrapped as the box wraps, so they move with it
  and the cycle's count and ramp hold; every strip 0.55 as bright
  (`kRainGlow` - additive, so fainter is more see-through), faded in over the
  first 80 units from the eye and out over the box's far 400. Anekbah at
  frame 690: 573 of 800 drawn, 112 under a roof; 2762 pixels.
* **Soft streaks** (the same day, the reader's "do 1 and 2" after asking
  whether to keep the hard lines): (1) each strip carries a generated
  STREAK texture (`rainStreakTexture`, 8x32 grey, a texel's palette index
  its brightness - soft across, a soft tip at the leading end, brightest a
  fifth back, fading out behind), pooled after the sprites
  (`remasterPoolRain`) and three times the line's width, so the edges are
  the texture's; (2) 32 units long in the WORLD (`kRainLength` - a shutter
  open half a frame on a drop falling 64 a frame), so perspective draws a
  near drop's streak long and a far one's short. The leading end is the
  original line's bright one (its tail, ahead along the wind). Anekbah at
  frame 690: 7324 pixels on Vulkan, 7321 on GLES, overlap 1.000.
* **The near streaks faded** (the reader on the third comparison: a drop a
  step from the eye drew one long bright streak down the frame): faded in
  smoothly from 40 to 260 units (it was linear over 0..80); Anekbah 538
  drawn, 7139 pixels.
* **Lit by the set's lights** (the reader's "option 3", the same day): each
  drop takes the set's own lights that reach it - and the emitters', the
  flames and glows, when the emitters' light put them in the slot's table -
  on `lightReach`'s rule (the squared reach, the linear falloff, `intensity
  * 256`), as a surface facing each light, through the engine's ramp
  `(t * c) >> 8`, over a dimmer base (0.35 of the line's grey); the lights
  at 0.5, and past white the colour scaled down whole, not clipped per
  channel, so a drop by the burner stays warm. Anekbah's street has two
  FILLS reaching everywhere (a warm 255,171,79 at k 128 and a blue 94,149,217
  at ~230) - the variation is the burner's warm pool and the dark corners'
  dimmer rain; all 538 drops lit. CPU per drop, so both backends alike
  (7231 pixels each).
* **The shower's sky** (the reader's "push and continue", the same day - the
  list's "the mist and the grade answering the shower"), with the 3D rain:
  while it falls, the grade darkens (0.20 at full rain), greys (0.30 of the
  saturation) and cools (0.03 blue over red, by luma) with it -
  `overcastLut` over the city's own LUT, or a plain 17^3 one made for the
  while where the city has none (the post pass then runs only while it
  rains) - rebuilt in 16 steps as the shower ramps and fades
  (`gradeOvercast`; `remaster.grade` carries the step); and the MIST thickens
  by 0.3 at full rain in the city's own fog colour - Vulkan only, as the mist
  is (0.5 hid the far square). Both backends grade alike.
* **The preset turns the game's weather on** (the reader's "ok, continue",
  the same day): *Niveau de detail* - option row 7, Faible 0 / Intermediaire
  1 / Important 2, 2 the top - gates the city weather (`cmp byte ptr
  [0x90E727], 2`) and boots at 1, so none of the above was seen unless a
  save or `--detail` said 2. The preset raises it to 2 at start unless
  `--detail` or the ini's [Options] levelofdetail names it
  (`remaster.detail`); option row 7 lowers it in play, live. It is the
  original's own option at its top, so it is also the classic shadows'
  finest tier, and at 2 the weather's cycle draws on the engine's one
  `rand()` from the first frame, as the game's does - F10's original mode
  keeps it (and the original's 2D rain). `engine: remaster preset` part 5.
* **The snow in the scene too** (the same day): Mahaleel's 200 flakes -
  the bit-4 arm's box of 400 and drift, untouched - as quads facing the eye,
  the original's 8 units across, with the original's own `Images\snow.bmp`
  (an 8-bit BMP keyed on black: additive, so the key holds), three copies a
  flake (the offsets scaled to the box), faded in over 20..80 units, lit as
  the rain, stopped by roofs - so the ice cave's re-scattered static (ASSETS
  4e) is gone in the remaster, and the flakes stay inside the picture (the
  original's fall over the letterbox). Address 404 at frame 90: 650 of 800
  drawn, against the original's 68 sprites. The option says rain and snow
  (`Pluie et neige en 3D`).
* **The snow lying** (the reader's "go", the same day), under `[Remaster]
  wet` - now the weather ON THE GROUND ("Sols mouilles ou enneiges"): where
  the game's snow falls (Mahaleel - its cycle never stops, so the cover is
  there whenever the weather runs, `weatherSnowing`), the set's floors under
  the open sky turn white under their own baked light - `c / texel`, the
  light alone, times 0.9 - by 0.85 x the corners' open sky x how DARK the
  texel is (so a snow texture keeps its own drawing - whitening it flat lost
  the ground's shading - and stone and wood go white), BEFORE the fog.
  `coverRuns` splits the frame's own set draws into covered runs
  (`Draw::snow`, `Draw::wet` its cover): no reflection pass. Vulkan bit 20
  of the push block's `water`; GLES `uSnow`. Address 404 at frame 90: 772
  triangles covered, 8994 pixels on Vulkan, 8995 on GLES. A red debug view
  showed what is covered: open ground and the low blocks' tops; the big
  blocks in front of the monastery are under it by the roof test, the ramps
  steeper than 20 degrees.
* **The mist on GLES too** (the reader's "continue", the same day - the mist
  was Vulkan's alone since A4, so neither it nor the shower's haze reached
  GLES): the water variant applies `scene.frag`'s formula after the game's
  fog (`uMist` - strength, ground, height; the eye as `vWy + vToEye.y`, the
  vertex variant's `uEye` now set every frame), the fogless batches marked
  -1 / -1 for it as Vulkan's; the variant is built when the mist or the 3D
  weather is asked too. Anekbah's street at `--mist 0.5`: 218130 pixels on
  Vulkan, 218144 on GLES, overlap 1.000. `engine: mist` part 5.
* **In play, the reader's three reports** (2026-10-27, Anekbah with the
  preset): (1) TOO DARK - measured at the street start, the mean luma 45.8
  in the original, 27.4 with the preset dry (the noir grade and the mist),
  16.1 with the shower: the overcast's 0.20, the shower's haze (0.3 toward
  Anekbah's near-black fog colour 12 25 35) and the wet film's 0.35. Now
  0.08, 0.1 and 0.22: 21.9 with the shower (-20% where it was -41%). (2)
  THE RAIN STOPPED AT ONCE and (3) ITS MUSIC PLAYED ON: the original's roof
  ray - the camera under a roof sends a running shower to its 200 ms fade
  and posts no message 24, so track 119 stays (docs/ASSETS.md 4e). With the
  3D weather the cycle no longer takes the roof (`remasterWeatherRoofless`):
  the drops stop at it themselves, and the shower runs its course - under the
  roof at 3629, -9030 it ramps to full, message 24 at 2 min 36 s, the 10 s
  fade, the music back to track 2. The original's rule in the original mode
  and without the 3D weather. `engine: rain 3d` part 7.
* **In play again, with `--vulkan`** (the same day; 7% of frames over 33 ms
  where `--world-vulkan`'s CPU present had 63%): (4) IT RAINED ON, ITS
  SOUND GONE - after a visit indoors (the restaurant, area 217, its track
  10) the street plays its own track on the way back in (event 9 -
  Anekbah's 2) while the shower still runs, and message 23, which played the
  rain's 119, came only when it began. Coming back into a rain area with a
  shower running (state 1 or 2), the 3D weather posts message 23 again -
  its handler plays 119 after the area's 2 (`remaster.rain3d.reenter`). (5)
  THE RAIN MOVED IN THE PAUSE MENU: the original keeps the world drawn behind
  screen 31 (+112 bit 0x40000, docs/UI.md) and steps its drops per drawn
  frame, its clock stopped - so its 2D rain moves there too, and the
  original mode keeps that; with the 3D weather the drops are held while
  paused. `engine: rain 3d` parts 8 and 9.
* **A framing seen twice, not reproduced** (2026-10-27): some comparison
  renders of Anekbah's street start (`--stand 1804,0,-6890,336`, 700 frames,
  800x600) came out with the camera further back than others with the same
  arguments. Rerun 11 times - in sequence, three at once under load, and the
  comparison's own loop - every frame byte-identical (md5 135cdc25...), the
  follow camera 0 in each; no config beside the saves, the save unchanged
  since August. Likely a binary rebuilt by a check (or a mutation's build)
  between renders; the logs of those runs were not kept. Watch for it.
* **The rain under neon** (the reader's "rain effect on neon", 2026-10-27):
  each drop's light in two halves - the set's own lights, then the
  EMITTERS' (the neon, the glowing surfaces, the flames - in the slot's table
  with the emitters' light on), these weighed 2.0 (the set's 0.5) and
  replacing up to 70% of the grey base as they grow, the drop up to 1.8x
  brighter in a neon's light: under Qalisar's pink "?" sign the rain glows
  pink. 156 drops at frame 690 there with the emitters' light, 0 without.
  `engine: rain 3d` part 12.
* **More realistic rain, 1 - the VEILS** (the reader's "how could the rain be
  more realistic", then "do 1, 2 and 3", 2026-10-28): far rain reads as
  falling sheets, not streaks - `veilAmount` in both scene shaders samples
  three points IN THE AIR along the ray (500, 1200, 1900 units, those nearer
  than the surface) in a value noise of long bands (0.0007 along the fall,
  0.008 across) slanted by the wind and falling 1500 units a second on the
  weather's clock (held while paused), a light grey lit by the set's
  ambient, as thick as the shower and with distance (`1 - exp(-d / 2500)`,
  0.85 at full). Qalisar's lamp-lit street: 107261 pixels the veils' own on
  Vulkan, the same on GLES; `OMK_NO_VEIL=1` (an instrument) leaves them out.
  `engine: rain 3d` part 13.
* **More realistic rain, 2 - the PUDDLES** (2026-10-28): the wet film is
  no longer even - `puddleMask` in both scene shaders lays a two-octave
  value noise over the floor's world x/z (0.004 and 0.011 per unit,
  `smoothstep(0.52, 0.68)`), and where it is high the floor is a puddle:
  the film darkens it up to 1.8x the even film (0.5x between), its mirror
  is 2.2x as strong (0.5x between), the ripples 1.6x, and Fresnel never
  falls below 0.18 so a puddle mirrors even seen from above. Only under
  the open sky, only where the floor is wet. The puddle strength rides in
  the veil vector's w (`View::puddles`; `OMK_NO_PUDDLES=1`, an instrument,
  gives the even film back). For the puddles to mirror the street the
  player stands on, the wet plane is now `chooseFloorNear` (the listed
  level below the eye NEAREST the camera's target height, with at least
  `kWetPlaneMin` = 1.5e5 square units near) rather than the one with the
  most area near: in Qalisar the largest level was not the street.
  Anekbah's street at full rain: 61427 pixels move on Vulkan, 61405 on
  GLES, overlap 0.989. `engine: wet floors` part 9.
* **More realistic rain, 3 - the SPLASHES** (2026-10-28): where the rain
  meets open ground ahead of the eye, a drop's crown -
  `playremaster_rain.cpp`. 700 a second at the full shower (fewer as its
  count ramps), each landing at random over the half-disc 60..1100 units
  ahead within 0.9 rad of the view's heading, on the floor a ray down
  through the shot soup meets (a face no steeper than `n.y` 0.7), and only
  where the roof ray up from there meets nothing. Each lives 240 ms on the
  weather's clock (none spawned or aged while paused, none from
  `OMK_WEATHER_SKIP_MS`'s jump): an upright sprite turned to the eye, its
  own size (0.6..1.2 of 4.5 x 6 units) and way round, rising as it fades,
  additive, lit as the drops (the set's lights, the neon). The picture is
  generated (`splashTexture`: a rim, two flaring walls, a spray, four beads,
  blurred twice - the first, sharp one at 7 x 9 read as an icon of a crown).
  Their own fixed sequence, not `rand()`, so the backends draw the same
  ones. Anekbah at full rain: 115 live at frame 690; 1287 pixels on Vulkan,
  1288 on GLES, overlap 0.999; `OMK_NO_SPLASHES=1` (an instrument) leaves
  them out. `engine: rain 3d` part 14.
* **The drops less solid** (the reader, from play, 2026-10-28: "they look
  too solid, almost like a shooting star"): the streak was a comet - a
  bright head a fifth of the way back, fading behind it - and grew with the
  frame's height (10 px wide at 1600 lines). Now the streak is EVEN along
  its length, a drop's motion blur, soft over a quarter at each end
  (`kStreakEnds`); its width grows as the ROOT of the height (about 5.5 px
  at 1600, soft edges included); each drop has its own brightness,
  `kRainOwnMin` 0.35..1, kept as it falls; and the strips are 0.4 bright
  where they were 0.55 (`kRainGlow`).
* **Wet walls** (the reader's "wet walls and characters", step 1,
  2026-10-28): `wetFloors` also takes a set's opaque triangles within ~20
  degrees of upright (`kWetWallSteep` 0.34 on the normal's y) and calls a
  wall's corner wet when a point `kWetWallOut` (8 units) in front of it
  sees the sky - ONE roof ray, not the floors' five (five took Anekbah's
  load from 196 to 500 ms on an M1; one, 337 ms; Qalisar 121 -> ~200).
  `WetFloors::rained` (1 a floor, 2 a wall) is the wet pass's list - the
  snow keeps `open`, the floors alone. A wall's corner carries `-6 - open`
  in its phase (`kOpenSkyWall`; a floor's `-2 - open`), so both shaders
  tell the two apart below -5.5 (GLES: the new `vWall` varying). A wet wall
  darkens `kWetWallFilm` (0.6) of a floor's film and takes no puddle, no
  ripples and no reflection, but its TRICKLES (`trickles`, scene.frag and
  `kTrickleGlsl`): in some 5-unit columns, now and then a bead runs down
  on the ripples' clock (16 or 32 cycles in its 256, so it wraps whole),
  meandering a little, a glint (`kTrickleLight`) with its wet trail above
  it darker (`kTrickleDark`), gone past 900 units. The columns run along
  a fixed level direction (0.83, 0.56), so a wall at one rare heading
  would show one wide column - GLES has no normal to do better.
  Anekbah: 22152 wall triangles the rain reaches; at full rain 61374
  pixels move on Vulkan, 61410 on GLES, overlap 0.999 (measured with the
  five rays); `OMK_NO_WET_WALLS=1` (an instrument) wets the floors alone.
  `engine: wet floors` part 10.
* **Wet bodies** (step 2, 2026-10-28): `remasterWetBodies` - in a rain area
  while the floors are wet, every body the renderer poses (`Draw::meshPose`)
  casts one roof ray from `kWetBodyHead` (150 units; 64 since 2026-10-29,
  "10, the hall" below) above its root through
  the shot soup, and if nothing is over it its draws carry `Draw::wetBody`
  with `wet` the floors' wetness. Vulkan's bit 21 of `Push.water`, GLES's
  `uWetBody`: the BODY rule, before the fog. First a gloss with no normal,
  `c * (1 - 0.2 w) + 1.2 w c^3` - which the reader could not see in the
  game ("wet effect on characters is not visible", 2026-10-28). Now: darker
  (`kWetBodyDark` 0.3) and deeper in colour (`kWetBodySat`), and a SHEEN -
  the film mirroring the lit street, lit by the body's own light: a
  Fresnel rim (`kWetBodyRim`, power 4) and a tight highlight where the
  reflected ray rises to the sky (`kWetBodySky`, power 12), `kWetBodySheen`
  0.9 at full wet. GLES had no normal for it in its plain programs: the
  water vertex variant hands down `vBodyN` (a posing stage's turned `nrm`,
  a set stage's `aNormal`, which the plain program now binds), and a wet
  body posed on the CPU reads its posed normals from their own buffer
  (`remasterWantsNormals`). AND THE BODIES POSED ON THE CPU were never
  found - they carry no pose: GLES poses Kay'l on the CPU under the mapped
  shadows (the preset's), so in the reader's run the player was never wet
  on GLES. `remasterWetBodies` now knows them by their posed copy (the
  player's, the staged, the crowd's, the traffic's) and places each by its
  lowest corner. Anekbah at 640x480: 33969 pixels on Vulkan, 33970 on GLES,
  overlap 1.000; 1343 brighter by more than 48 - under the FLOOR rule
  (`wetBody` lost) 0, which part 11 tells apart; Kay'l on the CPU
  (`OMK_CPU_PLAYER=1`) on GLES 1343 too, 1 with his normals or his marking
  lost. `OMK_NO_WET_BODIES=1` (an instrument) leaves them dry. Qalisar
  with its crowd: 79 of 84. `engine: wet floors` part 11.
* **The sheen in BEADS** (2026-10-28): the even sheen read as plastic - the
  reader in Qalisar's restored rain at 4K, `--remaster --enhance-all`:
  "it looks like character are in a plastic film". The sheen now sits in
  beads of water, a value noise on the body's TEXTURE (`kWetBodyBeadScale`
  48 on `vUV`, thresholded `kWetBodyBeadLo..Hi` 0.55..0.8) so they move with
  the body (the world's would slide over it); the rim between them a
  quarter (`kWetBodyRimFilm`, power 5), the sky's highlight on the beads
  alone (power 16), `kWetBodySheen` 0.55, and no floor of light in the
  body's shadows - lit by `shade` alone. The darkening and the colour kept.
  Anekbah 640x480: 34803 pixels on Vulkan, 34804 on GLES, overlap 0.994;
  133 brighter by more than 48 (part 11 now asks more than 60; the floor
  rule's 0 still told apart), Kay'l on the CPU 143. The reader: "better",
  then "a bit stronger": `kWetBodySheen` 0.75, `kWetBodySky` 1.0, more beads
  (`kWetBodyBeadLo` 0.5) - 292 brighter, Kay'l on the CPU 300, overlap
  0.993. The reader: "better" again.
* **The weather list, round 2** (2026-10-28): asked "what else could be done
  to improve the weather rendering in the remaster mode?", eight offered -
  (1) the lights' streaks on wet floors, (2) halos round the lights in the
  shower, (3) bodies drying slowly under cover, (4) drips from roof edges,
  (5) splashes from feet and on shoulders, (6) the rain sound by cover,
  (7) gusts, (8) lightning (invented, last) - and the reader's "ok, go" on
  the recommended 1 + 2, then 3.
* **The weather list - where it stands** (updated 2026-10-28, the reader's
  "update the weather plan with the new items"):

  | # | item | state |
  |---|---|---|
  | 1 | the lights' streaks on wet floors | DONE `d50f917b` - from the lights themselves (the reflection-based try taken out); faded in against the first light left out |
  | 2 | halos round the lights in the shower | DONE `2cf19683`, gain 1.5; taken at the GLOW's size since 2026-11-13 (`post.frag` mode 3: 25 taps a quarter-size texel, not a full-size pixel's - the final pass 1.4-2.0 -> 0.3 ms at 4K; one 565 step on 0.56% of the pixels, GLES keeping it as its square root, 1.35%) |
  | 3 | bodies drying slowly under cover | DONE `2cf19683` |
  | 4 | drips from roof edges | DONE `ca3f49de` - past the eaves, the drawn set's sky too |
  | 5 | splashes from feet and on shoulders | DONE (2026-10-29) - a crown at each step of a body in the open, drops bursting on heads and shoulders; see "5, the splashes off the bodies" below |
  | 6 | the rain sound by cover | DROPPED - the shower's sound is its music track (118-122, the city's music mixed with the rain, ASSETS 4e): muffling it muffles the music. Only a separate drip sound, if the game ships one (not looked for) |
  | 7 | gusts | DONE (2026-10-29) - the drawn rain's lean and the veils' sheets sway with a wind that comes and goes; the original's drops, step and roof test untouched; see "7, the gusts" below |
  | 8 | lightning | OPEN, last - invented (the game has none) and needs a thunder sound |
  | 9 | a SECOND reflecting level (new, from play) | DONE (2026-10-29) - planar, a second pass where a second level is in shot, chosen in OMK's Remaster page (`reflections`: none, one plane, two planes; the preset's two); see "9, the second level" below; **CONFIRMED IN PLAY** with its occlusion test (the reader's "commit", 2026-10-29) |
  | 10 | rain under a hall's roof (new, from play) | DONE (2026-10-29) - NOT the 3D rain: the wet WALLS' trickles, on a wall whose top meets its ceiling, and Kay'l "in the rain" under a low ceiling; see "10, the hall" below |
  | 11 | snow on slopes | OPEN (from the snow lying) |
  | 12 | the drips' load cost (new) | DONE (2026-10-29) - the wet pass's rays through a grid: Anekbah 668 -> 106 ms, Qalisar 464 -> 85 (M1), the same answers bit for bit; see "12, the grid" below |

  Fixed on the way, not on the list: the wet floors reflecting off the
  level (`2cf19683`, a street's kerbs and slopes within 16 of the plane);
  THE DARKNESS (`e86a9cbd`, Vulkan's draws unlit where none of the set's own
  lights reached the player, the emitters' tiled lights unread); F9 to name
  a place seen in play.
* **3, the bodies dry slowly** (`stepBodyWet`, `bodyWet` by the body's
  pose): a body soaks over `kWetBodySoakMs` (4 s) in the open and dries over
  `kWetBodyDryMs` (45 s) under cover, never wetter than the floors - where
  the roof ray of the frame alone had it dry the moment it stepped under an
  awning. Anekbah with every body under cover from frame 690
  (`OMK_WET_BODY_COVER_AT`, an instrument): 4 of 4 still wet at 720, dry
  at 2070 (46 s on). The `remaster.wetbodies` event counts them
  (`drying`). `engine: wet floors` part 12.
* **2, the HALOS in the shower** (`View::halo`, with the 3D rain, as strong
  as the shower): `post.frag`'s final pass adds round each light a wide soft
  glow of its bloom - the glow image on two rings (8 taps at half
  `kHaloRadius`, 16 at it: 12 glow texels, a quarter of the frame's), times
  `kHaloGain` - so a lamp keeps its core and gains the halo. 2 at first, then
  1.5 (the reader: "reduce the halo strength a little bit"; "halos are good
  now"). First tried as
  the bloom's blur reaching further (1.5 x) and 0.6 brighter: 10 pixels
  brighter and 1785 DARKER - a wider blur spreads the same light thinner, the
  lamps' cores dimmed. Qalisar's lamp street at full rain with the pack, at
  gain 2: 20597 pixels brighter on Vulkan, 19722 on GLES, none darker; at
  1.5, 9964 and 9504, overlap 0.842.
  `OMK_NO_HALO=1` (an instrument) leaves the bloom as it is dry.
  `engine: rain 3d` part 15 (skipped without the pack).
  **RED since, not from the code** (2026-10-29): part 15's overlap is
  0.782 against its 0.8 - and the same, to the pixel, built at `2cf19683`
  itself, `d50f917b`, `ca3f49de` and `1e50247`: Vulkan 9959 brighter as
  recorded (9964), GLES 9304 where 9504 was. What moved is outside the
  tree: the record was taken on the M3 (the day's other sessions, the
  reader), this on the M1 - GLES's count is the machine's, Vulkan's is not.
  To settle: the part on the M3; green there, the threshold is one
  machine's. **SETTLED 2026-11-10 (M1), the measure was wrong, not a
  backend:** of the pixels in one set and not the other (1549 Vulkan's,
  900 GLES's), every one is up 16..31 on the other backend - the "up by
  more than 24" contour drawn across a soft halo, which the GPU's
  filtering moves by a ring of pixels. 99.9% of each set is up more than
  12 on the other, the light added agrees within 3% (GLES 0.971 of
  Vulkan's), none darker. Part 15 now asks exactly that (over 98% both
  ways, within 10%); GLES's `kHaloGain` halved fails it three ways.
* **1, the streaks - TRIED AND TAKEN OUT** (2026-10-28): the reflection's
  bright part (over 0.35) smeared along the screen's vertical, 8 taps each
  way over 8% of the frame, on a wet REFLECTING floor (mode 3). It moved 0
  pixels in Qalisar (lamp street and the reader's street start) and 190 in
  Anekbah: one plane reflects a frame (`chooseFloorNear`), and painting the
  branch red showed only a far patch of Qalisar's lamp street on it - the
  green street the player stands on is wet but not that plane - see the
  next, from the lights themselves.
* **The wet floors reflect off the level** (2026-10-28, the reader: "wet
  floor reflections ... missing in most places (working at some
  locations)"): the one plane a frame took a wet triangle only with its
  three corners within 4 of it, a listed floor's rule - and a street is not
  level. Near the camera on Qalisar's lamp street 3402 open wet triangles
  were within 4 and 119724 more within 16, NONE between 16 and 32 (the next
  levels past 64); the reader's start and Anekbah's street alike. The plane
  itself was right (the eye 280 above the street at -4, the look-at -272).
  A wet FLOOR's triangle (`rained` 1, not a wall) now reflects within
  `kWetPlaneBand` 16 (`o3de/floorreflect.h`, `drawWithWater`'s
  `onWetPlane`); the listed floors keep 4. Moved against the 4: 47560
  pixels on the lamp street, 12191 at the start; Anekbah 79426 on Vulkan,
  80238 on GLES, overlap 0.958. `OMK_WET_PLANE_EXACT=1` (an instrument) the
  4 back. `engine: wet floors` part 13. The reader: "better". **Open**: a
  second level in view (a walkway above the street, a square below) still
  takes only the film - a second pass, or screen-space reflections. (Done
  as the weather list's 9, "9, the second level" below.)
* **1 again, THE LIGHTS' STREAKS from the lights themselves** (2026-10-28,
  the reader's "go with 1"): each emitter's light drawn on every wet floor as
  a long highlight toward the eye - `streakOf` (`scene.frag`, GLES's twin in
  `kStreakGlsl`): an anisotropic Gaussian of the half vector in the floor's
  frame, LONG along the view (`kStreakLong` 0.30) and TIGHT across it
  (`kStreakWide` 0.035), the floor's normal up; `kStreakGain` 1.2 at full
  wet (the film over `kStreakFilmFull`, the shaders' `kWetDarken`), more in
  a puddle (`kStreakPuddle`), on open floors alone - no wall, body or snow.
  The lights: `View::streakLights`, the `kMaxStreakLights` (16) emitters'
  nearest the camera within `kStreakFar` (4000) - or the emitters' own
  reach, `kFxView`, 3000 untiled, the nearer - picked in
  `remasterPointLights` (so under per-pixel light, which `--enhance-all`
  gives), each faded over the reach's last fifth AND against the first one
  left out (`kStreakCutFade` 0.35, as the emitters' slots): ranked by
  distance alone a light entering the 16 came in whole, "from nothing to a
  huge reflection in one step" (the reader). Vulkan: the Lights block's tail
  (`streakN` at 3184, the lights at 3200); GLES: `uStreak[32]`,
  `uStreakN`, sent once a frame on a program that draws a wet floor.
  Qalisar's lamp street: 7278 pixels brighter by more than 24 on Vulkan,
  7355 on GLES, overlap 0.915. `OMK_NO_STREAKS=1` (an instrument) none.
  `engine: wet floors` part 14 (skipped without the pack). The reader:
  "reflections themselves are good", then with the fade "better".
  **Not checked**: the fade over the transition (a headless run stands
  still); by construction it is continuous in the camera's distances.
* **4, THE DRIPS off the roofs' edges** (2026-10-28, the reader's "ok for
  the drips"; the rain SOUND by cover dropped - the shower's sound is its
  music track, 118-122, its city's music mixed with the rain, ASSETS 4e):
  `DripPoint`s found with the wet floors (`wetFloors`): every floor edge an
  OPEN triangle shares with a covered one, or whose corners disagree in an
  open one - a roof's edge runs along a triangle's side as often as across
  it - with one of its corners under the open sky, a point every
  `kDripSpacing` (32); the roof found up from a little toward the covered
  side, then the point walked OUT `kDripStep` at a time (to `kDripSearch`)
  until the sky is clear in the shot soup AND the drawn set - so the drop
  falls past the eaves. Drawn in the rain's draw (`remasterRainDraws`):
  each point near the eye (`kDripFar` 900) drops one at a time under
  gravity (`kDripG` 386, an inch a unit), a streak as long as its speed
  blurs it (`kDripStreakSec`, 16..80), `kDripGlow` 2 and `kDripWide` 1.3
  against a rain drop, lit as one, more of them the heavier the shower, and
  lands in a splash. Tuned by the reader's eye: at 1500 far ones read as
  floating dots, wide and short as squares; at the floor's edge they fell
  from under the roof's overhang, "drips through the ceiling" (542 of 1760
  with drawn geometry in their fall) - and edges wholly under a hall's
  ceiling dripped. Qalisar 2346 points (11 with anything over them, the
  `world.wet` self-check), Anekbah 3583; ~30 in the air at Qalisar's start.
  The load: Qalisar's wet pass 155 -> ~300 ms, Anekbah's 223 -> ~590 ms
  (M3) - the walk out and the drawn soup. `OMK_NO_DRIPS=1` (an instrument)
  none. `engine: rain 3d` part 16. Not the drips: the reader's marks in a
  hall (15303, 0, -10414) - read then as the 3D rain, which was WRONG: they
  are the wet walls' trickles (2026-10-29, "10, the hall" below).
* **THE DARKNESS, found and fixed** (the reader, 2026-10-28, from play,
  "global lighting/reflections changes completely in a few steps": the
  arcade dark and Kay'l a black silhouette, a step from a lit spot). Older
  than the weather work (it stayed with streaks, halos, drips and the wet
  band off). F9 named the place: `--area 101 --stand 15132,0,-10597,204
  --remaster pack --enhance-all`, dark every run. Vulkan pushed a draw's
  lighting mode only while the Lights block held a light
  (`push_.lit = litCount_ > 0 ? d.lit : 0`) - and with the emitters'
  lights TILED the block holds the set's own lights alone, those within
  reach of the player: out of their reach EVERY draw went unlit (mode 0,
  bodies too), the 313 tiled point lights unread. A shader colouring each
  pixel by its mode showed the whole frame mode 0 there and mode 3 a step
  away. Now lit while the block holds a light OR the tiles are bound:
  the arcade 7.7 -> 16.9 (the nearest-slots way, `OMK_NO_TILES=1`, 16.7 -
  which slotted the emitters into the block and hid it); a lit spot
  unchanged. `engine: emitter light` part 7. On the way, ruled out: the
  binning (`OMK_TILES_LOOSE=1` the same), the preload, a set switch, free
  look, the mist, AO, relief, the reflections, the 3D rain, blended draws.
* **10, THE HALL - the wet walls, not the rain** (2026-10-29): rendered at
  the reader's place (`--area 101 --stand 15303,0,-10414,76`), the marks
  are rows of falling dots in front of the far wall. Taken apart: removing
  every 3D drop moved 25 pixels; the drops behind the wall (x > 15595, the
  set's soup EMPTY there from -1000 to 2000) are hidden by depth; the drips
  off, the same; `OMK_NO_WET_WALLS=1` - gone. The far wall `Sol07` (x
  15590) tops out at -136.7, exactly the ceiling `PlafSSol02`'s height
  (`shot_ray`, `soup_dump`), and a wall corner's roof ray started 2 ABOVE
  the corner - already past the ceiling it meets: every wall that meets its
  ceiling ran with water. Now from `kWetWallBelow` (8) below the corner,
  never below the triangle's foot (`skyFront`): Qalisar's wet walls 10444
  -> 9465, Anekbah's 22152 -> 20682. The same fault in the bodies: the roof
  ray from `kWetBodyHead` 150 above the feet - twice Kay'l's 71 - so under
  the hall's 136.7 he stood "in the rain"; now 64. And a body posed on the
  GPU has its ROOT near its head, not at its feet (Kay'l's at -63.6 over
  the street at 0): its feet are now the floor `kBodyFloorReach` (200) under
  the root. In the hall: the walls' trickles 6724 -> 1099 pixels (the rest a
  ramp past the ceiling's edge and the side wall's outside), the player
  under cover (`remaster.wetbodies`' new `playerOpen`). `engine: wet
  floors` part 15. Also: Qalisar's shower is the RESTORED one (the preset
  turns it on), and without the 3D rain the original's camera roof stops
  it there. **CONFIRMED IN PLAY** (the reader's "good", 2026-10-29, at
  their place in the hall).
* **12, THE GRID** (2026-10-29): every ray of the set's wet pass is straight
  up, so `wetFloors` builds a `SoupGrid` (`kWetGridCell` 64, widened past
  512 cells an axis) over the shot soup and one over the drawn set, and
  each ray visits one cell's triangles - where the meshes' boxes left every
  mesh whose box spans the point. The same triangles through the same test
  in ascending order, so the same answers: `world.wet`'s new `sum` (FNV of
  the corners' open sky, the triangles wetted, the drips) identical with
  `OMK_WET_RUN_BOXES=1` (an instrument, the boxes). Anekbah 668 -> 106 ms,
  Qalisar 464 -> 85 (M1) - the drips' walk out and the floors' and walls'
  rays alike. The grids live only for the pass. `engine: wet floors` part
  16, shown to fail with every 100th triangle left out of the grid.
* **5, THE SPLASHES OFF THE BODIES** (2026-10-29, `remasterWetBodies`): in
  the 3D rain's shower, a body in the open (its roof ray) lays a crown at
  its feet every `kFootStride` (26) walked, either side of its way
  (`kFootSide`), `kFootSplashSize` 2.5 and `kFootSplashGlow` 3 against a
  drop's (2 was too faint on a bright wet floor, and 6 ahead of the root
  the leg hid it from the camera behind); and drops burst on its head and
  shoulders, `kHeadSplashRate` (10) a second at the full shower, a small
  crown (0.45) within 7 of its middle. Their own random sequence
  (`bodySplashSeed`), so the floor's splashes land as without them; on the
  weather's clock (none while paused); a move past `kBodyStrideJump` in a
  frame is a placement. Anekbah's street, frames 600 -> 690: 19 at the feet
  walking, 1 standing (another walker); ~125 on the heads either way.
  `OMK_NO_BODY_SPLASHES=1` (an instrument) none. `engine: rain 3d` part
  17. **CONFIRMED IN PLAY** (the reader's "good", 2026-10-29, Qalisar
  with the full pack and `--enhance-all`, M1).
* **7, THE GUSTS** (2026-10-29, the reader's "go with 7"): the original's
  wind is one fixed step a frame - `(0.5, 0.866, 0) * 64`, the rain 30
  degrees off the vertical - and stays so: its drops, their step, their
  roof test. What sways is what the remaster DRAWS: `remasterGust`
  (`playremaster_rain.cpp`) gives the lean a gust adds to the slant's
  tangent - three slow waves on the weather's clock (13, 6.1 and 2.3 s,
  weights 0.6/0.3/0.1; 4.3 and 1.7 s read as a nervous flutter), coming and
  going in 23-s spells (`kGustCalm` 0.25 between them), a lull taking back
  a third (`kGustLull`), up to `kGustShear` 0.35, times the shower - and a
  gust front travels down the wind (`kGustSpeed` 900 units a second along
  x). Each streak leans by the gust where its drop is; the veils' sheets
  are sheared by the gust where the eye is, about the eye's height
  (`View::veilGust`, in the veils' colour's unused w: Vulkan's Lights
  block at 3180, GLES's `uVeilColour` now a vec4) - both shaders'
  `veilAmount`. Anekbah's street: the lean -0.081..0.285 over frames 600 ->
  870; at 870, 53781 pixels on Vulkan (42466 the veils'), 54038 on GLES,
  overlap 0.920. `OMK_NO_GUSTS=1` (an instrument) none. Not the snow, the
  drips or the splashes. `engine: rain 3d` part 18. **CONFIRMED IN PLAY**
  (the reader's "good", 2026-10-29, the same run).
* **9, THE SECOND LEVEL** (2026-10-29, the reader's "go with 9, two
  planes and add it as a selection on the reflection setting"):
  `[Remaster] reflections = N` / `--reflections N`, OMK's Remaster page
  "Niveaux qui reflètent" - 0 none, 1 one plane (unset: as before), 2 two;
  the `--remaster` preset's 2. With 2, beside the frame's plane
  (`chooseFloor` / `chooseFloorNear`, unchanged) the level below the eye
  with the most floor AHEAD of the camera (`chooseFloorSecond`: within
  `kSecondCone`, 60 degrees, of the look on the ground; at least
  `kWetPlaneMin`; more than twice `kWetPlaneBand` from the first), of the
  wet levels and the listed ones; `drawWithWater` gives its floors' runs
  `Draw::reflectPlane` 1 - those IN SHOT alone (the frustum a tenth wider) -
  and the backend draws a second reflection, half size, into the
  reflection image's top-RIGHT quarter (`Renderer::drawTwoReflections`):
  Vulkan with a keeping render pass (`reflPass1Keep_`, `reflPassKeep_`
  without MSAA: the colour begun in COLOR_ATTACHMENT, so the clear is the
  quarter's), GLES with a scissored clear; each floor samples its own
  quarter (`scene.frag` bit 22; GLES `uReflOff`, `uReflBox`, a clamp only
  in a two-level frame - one-plane frames the same bytes). The clip memo
  keeps two planes (`clipAbove`). Found on the way: the tiled lights are
  indexed by `gl_FragCoord`, so the right quarter read past the tiles - its
  reflection unlit; the pass's x origin now rides in the tile header's
  word 3 above the tile's size, its lists in the upper quarter of the
  buffer (`ds1Refl2_`). And the first choice, the most floor within 100 m,
  gave the lamp street's second pass to a sunken court (y 176) not a pixel
  of which was drawn - hence ahead-only and in shot. With 0, no level
  mirrors and the wet film is laid without the pass (`coverRuns`, its
  `snow` false; the `remaster.wetfilm` event). On Qalisar's raised walk
  (`--area 101 --stand 15000,-140,-10500,270`, the rain) the street below
  mirrors the banner and the signs: 2513 pixels brighter, none darker. The
  cost there at 4K (M3, headless, the frame read back): the median frame
  37.8 -> 41.0 ms, the reflections' GPU time 1.4 -> 2.9 ms.
  `OMK_SWAP_LEVELS=1` (an instrument) swaps the two levels: the frame's
  own floor from the second quarter is one plane's picture, 0 pixels on
  Vulkan, 4 on GLES. `engine: floor levels` (new, 5 parts), `engine:
  floors` pinned to one plane.
  **The occlusion test** (the reader's "ok, do it", the same day): the
  lamp street's second level (y -140, the raised walks) is in shot but
  behind walls - its pass, 6.2 ms a frame at 4K (44.9 -> 51.1), drew
  nothing. Now the backend draws that level's floors AGAIN after the
  frame's scene, each run in an occlusion query, through a probe that
  writes neither depth nor colour at depth LESS_OR_EQUAL (Vulkan's
  `pipeProbe_`, GLES's masks; queries since GL 1.5 on the Mac, GLES 3 on
  Android, none on GLES2/vitaGL - there the level counts as seen); the
  next frame runs the second pass only if a sample passed
  (`Renderer::secondLevelSeen`), the reflection fading in over
  `kLevel2FadeSec` (0.25 s) - the query is a frame late. The first try
  queried the runs where the frame draws them: a wall drawn after the
  floor did not count, and the hidden level read "seen". A new level
  starts at 0, queried before it is drawn; at fade 0 its runs are marked
  (`reflectPlane` 1) but reflect nothing. The lamp street: no second
  pass, its frame `OMK_NO_LEVEL_QUERY=1`'s (an instrument: every level
  seen) pixel for pixel; 4K 45.6 ms against one plane's 44.9. The walk
  keeps its pass, the probe moving no pixel. `engine: floor levels` part
  6, and part 4 on GLES. **CONFIRMED IN PLAY** (the reader's "commit" after
  the run on Qalisar's raised walk, 2026-10-29, the full pack and
  `--enhance-all`, M3). **Open**: which second level elsewhere, by eye.
* **Open**: snow on slopes.

## Tier E - the interface pack: as built (2026-10-22)

The reader's "do the upscaled interface". `--ui-pack DIR` / `[Remaster]
uipack = DIR`, and the `--remaster` preset's `DIR/ui/pack` when there is one;
off unless named. Every backend - the 2D is composed on the CPU.

* `engine/src/ui/uipack.*`: the texture pack's way for the 2D - `<key>.png`,
  the key `uiKey` (FNV-1a over the bitmap as LOADED, size and 565 pixels).
  `surfaceFromBmp` notes every bitmap the pack has (its pixel buffer, which
  survives the move out); `blt`, the one place a bitmap is sampled at the
  output's size, draws an ENLARGING blit from the HD version, its rectangle
  scaled. The HD bitmap is 565 and keyed as the original (alpha < half ->
  0, any other 0 nudged to 1), so the key draws what it drew, its edge from
  an upscaled mask. At most 16 held decoded.
* `tools/omktexpack.py ui ROOT --bin realesrgan-ncnn-vulkan`: the engine's
  keys (`texpack_tool uikeys`: 56 bitmaps, 52 distinct - `I2D/bitmaps`,
  `IMAGES`, `IMAGES/French`, `FONTS`), each at its 8-bit colour, padded by
  its EDGE (not wrapped - these do not tile), the black key dilated and kept
  as a mask; Real-ESRGAN x4plus; cropped, ANCHORED to the original's colours,
  the mask thresholded into alpha -> ROOT/ui/pack (52 files, 302 MB; ~25 min
  on the M3, mostly the pure-Python anchor).
* `verify.py: engine: ui pack` - a pack MADE in the check (the menus' sheet
  in one colour, its key exact) drawn by `texpack_tool uiblit` through the
  interface's own `blt`: every drawn pixel the pack's, the drawn set the
  original's exactly, nothing without it; the real pack drawn from where
  there is one. Shown to fail twice.
* Cost: none measured - the 4K pause menu 26-27 fps either way (loaded M3).

**The FONTS** - text is drawn by its own glyph path, not `blt`: the font
pack below. The pack is not in the curation's eye yet: a picture whose upscale suffers can
be dropped from `ui/pack` by hand (its key: `texpack_tool uikeys`).

## Tier E - the font pack: as built (2026-10-22)

The reader's "do 1) and 4)" (HD fonts, and the GLES upscale). `--font-pack
DIR` / `[Remaster] fontpack = DIR`, and the `--remaster` preset's
`DIR/fonts/pack`; off unless named. Every backend - text is the CPU's. It
acts only where `textscaling fit` ENLARGES the glyphs (the original drew
them at their own size: at 4K a 17-pixel face is 4.5x).

* `engine/src/ui/fontpack.*`: one `<key>.png` a face, GREY, the key FNV-1a
  over the `.FNT` blob; every present glyph at `k` (8) times its size, laid
  out by `hdLayout` from the face alone (2048-wide shelves, code order), so
  the file carries no table and `k` is the power of two whose layout has the
  image's size. `TextLayout::drawRun` asks for it inside its `scaled()`
  branch and `drawGlyphHd` brings each glyph DOWN by area averages; blended
  as `drawGlyphScaled` blends (`blendCoverage`, shared).
* `engine/tools/font_hd.cpp` makes it, `tools/omktexpack.py fonts ROOT`
  wraps it (-> ROOT/fonts/pack, 13 files, 3.3 MB, 14 s). Two sources, both
  the disc's:
  * **`FONTS/OMIKRON.TTF`** - "Omikron One Medium", Loic Normand, 1998,
    Fontographer: the game's ALIEN SCRIPT, which MENUINTR and SNEAK are
    rasterisations of (a sheet of every face beside the TTF, 2026-10-22;
    PARCHEMI is another script). Each glyph drawn from its outline by
    stb_truetype (vendored, the tool only), at an em fitted per face (40.25
    and 21.75 px) and moved onto the shipped glyph's centroid; kept when it
    averages back down within 0.06 of the shipped coverage - 182 of 223
    MENUINTR glyphs, 156 of SNEAK's. The others fall back to:
  * **the glyph's own coverage**: the 0..31 levels are ANTIALIASING, so they
    place the contour to under a texel. A Catmull-Rom enlargement, its
    half-coverage contour drawn sharp - the runtime's `fit` does the same
    with a bilinear one, so the Latin faces gain a ROUNDER contour, not new
    detail: there is no vector source for them. Back-projection
    (Irani-Peleg) was tried and dropped: it fits the coverage's quantisation
    as shape, lumps contours and breaks thin strokes (COMPUTER, GENERIC1).
* `verify.py: engine: font pack` (`texpack_tool fontline` draws a line
  through the engine's `TextLayout`), shown to fail twice.

**Judged so far** (crops at 9/2, 2026-10-22): the alien script clearly
cleaner; the Latin faces slightly rounder than the runtime's enlargement.
**Open**: the reader's eye in play; an AI upscale of the Latin faces' sheets
(Real-ESRGAN) was not tried - letters are where it invents most.

## Tier E - the film pack: as built (2026-10-22)

The reader's "do 3)". `--film-pack DIR` / `[Remaster] filmpack = DIR`, and
the `--remaster` preset's `DIR/films/pack`; off unless named. Every
backend: the boot chain's three FLIS films (320x240 MPEG-1, 29.97 fps, MP2
sound) played from upscaled copies BY THE SAME DECODER (pl_mpeg).

* `engine/src/platform/filmpack.*`: `<key>.mpg`, the key FNV-1a 64 over the
  original's size and first MiB (another language's film is another key).
  The player (`playsetup_play.cpp`) opens the pack's film before the
  original, and `Movie::setSmooth` draws it BILINEAR to the display - texel
  centres on texel centres; the shipped films stay nearest. The copied
  sound is the shipped bytes, so the clock the picture follows is unchanged.
* `tools/omktexpack.py films ROOT --bin realesrgan-ncnn-vulkan`: every frame
  out (ffmpeg, `-vsync passthrough`), Real-ESRGAN's VIDEO model
  `realesr-animevideov3-x4` (x4plus was sharper but darkened the films'
  tones, and is 13x slower: ~2 s a frame), re-encoded MPEG-1 1280x960 at
  `-q:v 2` with the original MP2 stream COPIED, muxed as a program stream.
  A film's frames are deleted once its .mpg is written - the three at once
  (~10 GB of PNG) filled the disk on the first run.
* `verify.py: engine: film pack` - the engine's key against the tool's, a
  pack MADE in the check (EIDOS 2x) played in the original's place and
  registered on it, the real pack's three found and IN STEP with the
  originals (frame N against frame N, on frames that move); shown to fail
  twice. The counts: EIDOS 387 against 386, GAME 3212 against 3211, QUANTIC
  703 both - pl_mpeg never emits a stream's LAST picture and ffmpeg does, so
  the copy carries one more at the end; frame 100 / 3000 match their own
  numbers, so nothing drifts. Made in ~15 min on the M3 (GAME 140 MB, the
  pack 155 MB). Its registration samples every THIRD pixel:
  every fourth is one phase of the original's 4x4 blocks and pulled the
  best shift to (1, 2) on a frame that registers at (0, 0) whole.

**Played** (OMK.app, 4K, M3, 2026-10-22): all three from the pack; EIDOS 14
of 387 frames and GAME 46 of 3212 DROPPED to keep up with the sound - the
CPU's bilinear to 2880x2160 plus the 1280x960 decode is near a frame's
budget. **Open**: that cost (the GPU could scale it, as GLES already takes
the film at its own size); the GLES path presents the film through the
present pass's NEAREST scale (the smoothing is the CPU's, on the
display-sized surface the other backends use).

## The pack in GPU block formats: as built (2026-10-22)

The reader's "is there another format more adapted [than PNG]" and "BC7 in
KTX2 first, generate the android astc version too, then the preload". What
an arrival cost before it (the walk into Anekbah's restaurant, M3, `--remaster
--enhance-all`, 1080p, `vulkan: texture pool` - a line added for this): 63 ms
decoding the pack's PNGs and 28 ms uploading on the frame the set arrives,
then 41 + 10 and 38 + 7 as its characters come; 275 + 119 ms at the start.
The set itself (`set load:`) 4 ms, already on a thread. Two thirds PNG
inflate, one third upload of 4 bytes a texel.

* `engine/src/formats/ktx2.*`: KTX 2.0, written and read by OMK itself -
  one 2D image, its whole mip chain, no supercompression, BC4 / BC7 / ASTC
  4x4..8x8 (UNORM); the reader refuses anything else and checks every
  level's length against its block count.
* `engine/tools/tex_encode.cpp` (vendored `third_party/bc7enc`, MIT/PD;
  ARM's `astcenc` 5.7 binary for ASTC, run): a pack PNG -> a KTX2 with the
  chain made by 2x2 averages (the Vulkan blit ladder's), BC7 perceptual for
  colours, BC7 linear for normal maps, BC4 for light maps, or ASTC; prints
  the level-0 round trip. `omktexpack.py compress PACK --codec bc|astc|all`
  runs it over a pack, 8 at once, skipping what is newer than its PNG.
* The pack (`o3de/texpack.*`) indexes `<key>[_e|_n].{bc7,bc4,astc}.ktx2`;
  a backend says what it samples (`setCodecs`) and those files are READ
  instead of the PNGs. Vulkan: BC where the device samples and filters BC7
  and BC4 (MoltenVK on Apple GPUs does), the blocks copied level by level.
  GLES: ASTC where `GL_KHR_texture_compression_astc_ldr` is named (Android,
  the Quest) - not on the Mac's GL (no ASTC, and 4.1 has no BC7), which keeps
  the PNGs. Kept on PNG: the greyscale bank's alias (its pixels recomputed)
  and the bloom curation's LIGHT MAPS (it paints into them; their colours
  may be blocks - the curation then reads the shipped texels).
  `OMK_NO_BLOCK_FORMATS=1` keeps the PNGs, for comparison.
* The full pack (1259 colours, 833 light maps, 1259 normal maps): BC 3351
  files, **3.11 GB**, mean round trip 48.2 dB (colours), 53.7 (light maps),
  43.5 (normal maps), ~10 min on the M3. ASTC 6x6 (Android, the Quest):
  3351 files, **1.41 GB**, 44.3 / 54.9 / 41.1 dB, ~15 min. `full-anchor3`
  the same, its light and normal maps LINKED to `full`'s KTX2s as its PNGs
  are linked to `full`'s (`compress` follows a linked PNG to its twin).
  **The ASTC path is not run here**: no Mac context names the ASTC
  extension, so `uploadAstc` is built and compiled for Android but its first
  run will be the Quest's or a phone's - an `OMK_NO_BLOCK_FORMATS=1` run
  there is the comparison.

**Measured after** (the same walk): the start 27 + 57 ms (from 275 + 119);
the restaurant's arrival **8 + 12 ms** (from 63 + 28), then 3 + 3 and 2 + 2
(from 41 + 10, 38 + 7). `verify.py: engine: block formats` (the reader, a
made pack through Vulkan the same bytes as its PNGs, the real arrival at a
third of the PNGs' cost or less - 9.0 against 91.7 ms), shown to fail three
ways.

## The preload radius: as built (2026-10-22)

The reader's "an option for a pre-load radius ... every set in a 25 metres
radius pre-loaded in another thread, unloaded when 30-35 metres away ...
optional as it would need more ram/vram", after the block formats; and two
points before it was built: "many places have a sas ... they could make the
current idea more complex", "sas are not only when changing area: a
corridor between the street and a bar". `--preload M` / `[Remaster]
preload = M`, **the `--remaster` preset's 15** (25 until the measurement of 2026-10-26, below), 0 off, released past M + 10.
First planned for the Quest (`todo/quest-port.md` §4), built here.

* **Nothing of the game's logic moves.** The engine keeps two areas
  resident and loads into them at its own moments (`Area_TickLoad`, the
  transitions); the preload only makes the DATA those loads ask for ready.
* **The doors** (`Session::doorTargets`, `zoneDestinations`): each live
  zone's ENTER and ACTIVATE scripts walked STATICALLY by the opcode table's
  lengths to their `end`, collecting `area.goto` (47) / `area.preload` (45)
  with an immediate destination - and, the airlocks being why, **TWO HOPS**:
  each destination's own AREA chunk read (`zonesOf`) and its destinations
  listed at the FIRST door's place. `engine/tools/door_census.cpp` prints
  the graph: from the street, a door opens on a small 2-door area (an
  airlock or corridor, `ABAR06SS` 14 KB, `QBARSAS` 6 KB, `QPEEPSAS`) with
  the room behind (`ABAR06` 130 KB, `QBAR`, `QPEEPSHO`); from the room, its
  sas and then the STREET its load evicted (QBAR -> QBARSAS -> QALISAR). A
  city's tunnels (`TUNELAQ1/2`) open on the next CITY at two hops: near
  Anekbah's tunnel door, Qalisar (1459 KB) is prepared.
* **The work** (`backends/sdl/playpreload.cpp`): every 10 frames, each
  destination's nearest door; within M a set is prepared on a thread
  (`prepareSet`, the job a load starts), at most 6 held, nearest first; once
  ready its textures go to the renderer (`Renderer::setPreloadTextures`),
  which uploads 3 a frame (`preloadStep`) and PINS them - the pool's
  eviction spares them (Vulkan `poolTextures`, split out of `setTextures`).
  `askSet` takes a prepared set before starting one.
* **Measured** (the walk into Anekbah's restaurant, M3, `--remaster
  --enhance-all`, the block formats on): ARESTO prepared when its door was
  4.7 m away, its 7 textures up 3 a frame during the walk (~7-12 ms of work
  a frame), taken ready (0.0 ms waited); **the arrival's set textures: none
  on the frame** (7 new, 2.4 + 10.9 ms, without). Its characters still load
  on arrival (3 + 2 textures, ~11 and ~6 ms) - not preloaded.
* `verify.py: engine: preload` (the source, the door graph both ways through
  the airlocks, the walk, the same frame with and without), shown to fail;
  the pin's eviction caught by the source alone (see its docstring).

**Open**: the arriving area's CHARACTERS (their models and textures); an
airlock walked end to end in a check; the memory held, measured
(`omkprof.py --sites`). The save's first frames put the player at Kayl's
door before `--stand` moves him, which prepares and drops two sets once -
harmless, said.

## The remaster switch, OMK_REMASTER: as built (2026-10-23)

The reader's "how much independent are the two modes ... could you evaluate
if the remaster code has already a cost for low-end devices on original
mode?", then "do the OMK_REMASTER switch first. Would it help to keep
remaster code in another files as much as possible?" (yes: option B, the
code moved into files of its own; the VR switch's pattern).

**The cost measured first** (2026-10-22, M3, Anekbah's street with its
crowd, 300 frames at 640x480, original mode): against the engine before the
remaster (`3022b073`, a worktree build), the software renderer 10.2 -> 9.7
ms a frame (noise), GLES 2.74-2.92 -> 2.80-2.95 s (noise), peak memory +1
MB; the binaries +17% (2.83 -> 3.32 MB, the Vulkan build) and +12% (GLES).
No runtime cost - every feature an early return when off - but the code was
compiled into every port: the Vita, the 3DS, the classic Mac and PowerPC
builds glob `src/*/*.cpp` and `backends/sdl/*.cpp`. ~3,450 lines in the
remaster's own files and ~2,200 inside shared ones (an inventory, 2026-10-22).

**As built**:

* `engine/Makefile`: `REMASTER ?= 1` defines OMK_REMASTER (Android's
  CMake too); the Vita, 3DS, classic Mac and PowerPC build files never do.
  The object tree's stamp holds VR and REMASTER together. Without it the
  remaster's own tools are not built and Vulkan is left out (desktop-only,
  and the remaster's through and through). `make noremaster` builds the
  software and GLES viewers without it into `build/noremaster/`.
* The modules (`src/o3de/{texpack,normalmap,emitterlight,lightprobe,
  lighttiles,floorreflect,smoothbody,colourgrade}`, `src/ui/{uipack,
  fontpack}`, `src/platform/{filmpack,pngread}`, `src/formats/ktx2`)
  compile EMPTY without it, `src/vr/`'s way; their headers stay (types
  only cost nothing).
* The frontend's half moved to `engine/backends/remaster/` (compiled by
  this Makefile and Android's alone): `playremaster_setup.cpp` (the packs,
  strengths and preset - the 215-line block of `playsetup_session.cpp`),
  `playremaster_world.cpp` (a set's water, floors, probes and glows; the
  emitters' light table; the grade; the grey alias; the films; a GPU
  renderer's requests), `playremaster_bodies.cpp` (the smoothing, the body
  tint), `playremaster_draw.cpp` (the mist, the emitters' point lights with
  the night and the relief, the water and floor reflections),
  `playpreload.cpp`, and `playremaster_mode.cpp` (the original/remaster
  switch, F10 - `todo/graphics-menu.md` B). The shared files keep one-line calls
  (`remasterSetup()`, `remasterPrepareSet(L)`, `remasterDrawReflected(...)`
  ...), `backends/sdl/playremaster_off.cpp` stubs them (`playvr_off.cpp`'s
  pattern) - the per-pixel lighting stub reproduces exactly what the code
  did with the emitters' light off.
* GLES: its remaster code in two `.inl` included by `glesrender.cpp` under
  OMK_REMASTER only - `glesrender_remaster_shaders.inl` (the post pass, the
  scene programs' variants, FSR) at namespace scope, `glesrender_remaster.inl`
  (`runPost`, the water pass, `fsrPass`, the ASTC upload, `setGrade`, and
  the hooks `remasterSources` / `remasterProgramUniforms` /
  `remasterInitUpscale` / `remasterInitCodecs` / `packActive` / `packDecode`)
  at the file's end; its stubs beside the include. The remaster's overrides
  and fields in the class are under `#if`.
* `src/`: the interface pack's two calls (`surface.cpp`) and the font
  pack's (`text.cpp`) under `#if`; `TextLayout::drawGlyphHd` moved into
  `fontpack.cpp`; the water helpers of `renderer.cpp` (`waterPlane`,
  `groundLevel`, `drawWithWater`) under `#if` - and no longer computed at
  every set load on a port.

**Measured after** (`verify.py: engine: remaster switch`): the software
viewer 3.31 -> 2.78 MB without it, GLES 3.20 -> 2.91 MB; none of the
remaster's symbols in either (and every one in the development build);
Anekbah's street THE SAME BYTES with and without, software and GLES; a
remaster option on a build without it said. Shown to fail: `uiPack().attach`
back without its `#if` - `make noremaster` does not link; the set's own
lights wrongly moved behind the switch - both frames differ. The thirty
checks the move could reach green (every remaster check, the GLES ones, the
area load, the city return, the Vita, 3DS and hardened builds); the source
scans now read a shared file together with the remaster file its facts
went to (`verify.py` `_engine_src`).

**Not checked here**: the classic Mac build (no Retro68 on this machine)
and the Android build (no NDK) - their build files were changed to match.
**Still inline in shared code**: GLES's `begin` / `submit` set the water,
tint and emitter uniforms (a few dozen lines on plain data, no remaster
call); `View` / `Draw` / `WorldSlot` keep the remaster's fields; the
settings and the command line parse the remaster's options everywhere (so
a build without it can SAY it ignores them). Vulkan (desktop) is built only
with the remaster; its upscale, post pass and water reflection moved to
`vkrender_remaster.inl` on 2026-10-24 (the audit's cleanliness).
Also found and fixed on the way: the Vita's hand-written source list and a
3DS type deduction, both broken by the 2026-10-22 commits (`471971ac`).

## The audit of the whole remaster (2026-10-23) - findings, NOTHING FIXED yet

The reader's "do an audit of the remaster mode (performances,
optimizations, bugs, cleanliness, etc...)". Four read-only reviews at
`9168b0e9` (Vulkan; GLES; the CPU modules; the frontend, the switch, the
options and the checks), the headline findings re-read in the code, then
reviewed by the session that wrote the preload, the block formats and the
switch (its nuances folded in below). **No timing taken**: the machine was
heavily loaded (the reader's warning) - the costs below are the records'
or estimates. Line numbers are `9168b0e9`'s; `texpack.cpp` has moved since
(`0120c502`), so it is named by function. ✔ = re-read and confirmed.

**What holds up.** HEAD builds every variant (`make SDL_PKG=sdl2 play
play-gles vulkan noremaster` in a worktree); without the switch the
software viewer is 2.75 MB, GLES 2.88 MB (3.31 / 3.20 with it); the only
remaster names left in those are the stubs and `LightProbes::Cell`'s map
type (a `WorldSlot` field). The Vita/3DS/Mac/PowerPC build files never
define it; the stubs match the real code with everything off; GLES's ten
hooks match their stubs and its destructor frees every remaster resource;
Vulkan's `Push` / `PostPush` / `AoPush` / `LightUbo` match their shaders;
the tile binning handles `flipX` and the roll.

**Bugs, the order to fix them in:**
1. ✔ **GLES: the floors' reflection is drawn FULL size and sampled as
   HALF.** `begin` sets the half viewport (`glesrender.cpp:2688`), then
   `setView` (:2594) sets the full one; `submit` (:3053) samples with
   `0.5/rw_`. A floors-only frame shows the reflection's lower-left quarter
   magnified 2x, and GLES saves none of the quarter. Fix: halve inside
   `setView` when `passFbo_ && reflHalf_`; compare with Vulkan's floors.
2. ✔ **The reflection's clip memo outlives a slot's set**
   (`renderer.cpp:254-286`, `ClipMemo`). Keyed by `const Geometry*`, then
   `(start, count)`; a slot reloaded in place (`w = std::move(L.out)`,
   `playstate.cpp:2232`) keeps its address and `GpuResidency`'s assignment
   does nothing, so no listener fires. A next set with its plane at the same
   y and a draw of the same `(start, count)` draws the OLD set's runs (which
   may pass its corner count). Fix: the memo keyed by `geo->revision` too.
3. ✔ **The preload's cap never yields to a nearer door**
   (`playpreload.cpp:114`, `break` at `kHeld`). Two hops a door: three doors
   within 25 m fill the six, and the door walked to next waits until one is
   35 m away. Fix: a nearer want evicts the farthest held; `engine: preload`
   to test the cap and the release radius, shown to fail on the code now.
4. ✔ **Vulkan: texture images unchecked** (`upload` ~3085-3096, `uploadZ`
   ~3187-3201): `vkCreateImage` / `vkAllocateMemory` / `vkBindImageMemory`
   - out of device memory (the 4x pack with its maps) acts on null handles.
   Fix: ONE checked `makeImage2D` helper (seven hand-rolled sequences:
   `makeTarget`, `makePostAt` - which also leaks `qv`/`qf` on a half failure
   -, `makeReflection`, `reflDepth1_`, `makeUpscale`, `uploadLut`, the two
   uploads), the texture then drawn white. Before the split (below).
5. ✔ **A build without the switch names only some ignored options**
   (`playremaster_off.cpp:17-19`): `--bloom --water --floors --mist --ao
   --relief --fx-light --body-light --grade --ui-pack --font-pack
   --film-pack --smooth-skip` and every `[Remaster]` key but `dir` /
   `texturepack` pass silently; `engine: remaster switch` part 4 tries
   `--remaster` alone. Fix: `Settings::anyRemaster()`; the check one plain
   flag and one ini key.
6. **One-liners with them**: `.cube` reads `nan`/`inf` (`sscanf %f`), and
   `lutStrip` casts NaN to a byte - refuse non-finite in `parseCube`; a glow
   whose ramp is black (`hi = 0`) computes 0/0 (`emitterlight.cpp:62-87`)
   and still takes a light slot - `hi <= 0` is dark.
7. ✔ (2026-10-26, "The last batch" below) **Lower** (the pack is our own content): a PNG's IHDR alone reserves up
   to ~1 GiB before inflating (`pngread.cpp:246`, `out.reserve(expect)`;
   cap at deflate's 1032:1); a KTX2's codec slot not checked against its
   content (`readBlocks`: require `f.astc` to match the slot).
8. ✔ (2026-10-26, "Item 8" below) **Vulkan's present-pass barriers** (`runPresentPass` 2236-2245, 2275):
   `TOP_OF_PIPE`, access 0, both ways - no dependency on the post's writes
   nor the reads before the transition back. Harmless on MoltenVK (layouts
   are no-ops), wrong by the spec.
9. ✔ (2026-10-26, "Item 9" below) PLAUSIBLE: GLES FSR at `mediump` (no `GL_FRAGMENT_PRECISION_HIGH`,
   Mali-400 class): `1.0e-20` flushes to 0 and coordinates past 2048 lose
   their texel - refuse the upscale without highp; ASTC with a partial mip
   chain incomplete (black) under trilinear; an NPOT light map at
   `GL_REPEAT` (:2121-2139) incomplete on GLES2 (the glow silently gone);
   detached preload jobs capture `this`, nothing waits at teardown;
   `fontpack`'s `byFont` keyed by a `Font*` that may come back; NaN corner
   normals unpinned in `smoothbody` (:97-108); `--preload nan` passes the
   range test, `--preload abc` is 0, the ini's `upscale` takes 0..3 where
   the flag takes 1..3; preloads keyed by stem but matched by area (two
   areas on one `.3DO`, if any - `door_census` would say); the taken set's
   pins kept until the next change (`preloadPin()` after the take).

**Performance:**
* **The preload's uploads on Vulkan**: `preloadStep` calls
  `pack.replacements` on the render thread, and every colour, light map and
  normal map is its own staging buffer, allocation and `oneShotEnd`
  (`vkQueueWaitIdle`, :851) - the shape of the ~7-12 ms a frame measured
  (preload section). Order: FIRST `TexturePack::replacements`' lock scope
  (`textureKey` hashes every texel under `m_` - with six preload jobs, the
  frame's calls wait), THEN the decode into `prepareSet`'s thread, one
  command buffer and fence a batch, a staging ring, image memory
  sub-allocated (three allocations a texture, toward
  `maxMemoryAllocationCount`).
* **GLES has no pins** (FIXED 2026-10-24, "GLES's pins" below) (`setPreloadTextures` is Vulkan's): each door
  decodes and uploads its set on the arrival frame, and the airlock's round
  trip evicts and reloads the city's pool - several times the M3's 13 ms on
  Android or the Quest. A `pinned` flag spared by the eviction, an
  `uploadOne(i)` driven 3 a frame, all in `glesrender_remaster.inl`.
* **The preset's 25 m on GLES and software** (DONE 2026-10-26, "The preload's cancel" below) holds up to six full
  `SetLoad`s with nothing pinned; up to six threads started in one frame
  (one per want, 1 MB stacks), no cancellation - a released set's job runs
  to its end. One or two in flight, a cancel flag between `prepareSet`'s
  stages; the preset's preload only where pins exist, or a lower cap.
* **4K memory and bandwidth, Vulkan (estimates)** (partly DONE 2026-10-24, "The 4K trims" below): `reflMs_` /
  `reflDepth_` made full size at full samples for a floors-only scene that
  draws into `reflFbuf1_` alone (~300 MB at 4x; the water's alone needs
  them - and the water could go half size too); `stencilStoreOp` STORE
  though nothing loads it; under the AO the whole multisampled depth stored
  where `ao.frag` reads sample 0 (`VK_KHR_depth_stencil_resolve`,
  SAMPLE_ZERO); no `TRANSIENT` / lazily allocated attachments (memoryless
  on Apple); the post copies `colour_` -> `postSrc_` whole (~66 MB a 4K
  frame) even for the AO or grade alone - a ping-pong pair, or the
  composite folded into `present.frag`; the glow images made and barriered
  with the bloom at 0; post passes over the letterbox bands; the glow at
  RGBA16F (B10G11R11 half the bytes); the 21-tap blur as 11 bilinear taps.
* **GLES** (DONE 2026-10-26, "The GLES items" below): a water frame calls `begin()` twice (`drawWaterScene` :146,
  :156) - `frameNo_` twice, every body's depth ties resolved twice, the
  target cleared and the lights sent twice; `fsrPass` looks up 9 uniforms a
  run and runs again in `readback`; `runPost` runs with the bloom on when
  no draw bound a light map, and `glCopyTexSubImage2D`s the whole frame
  (ping-pong on tilers); `reflInv` / `eye` / `clipY` sent every draw.
* **CPU** (`uipack`'s part MEASURED 2026-10-26, no change: "The last batch" below): `uipack`'s LRU of 16 thrashes on a screen of 17+ packed bitmaps
  in a fixed order (decode under `impl_->m`) - count the largest screen's
  bitmaps before changing it; a KTX2 copied whole (`std::byte` ->
  `uint8_t`); `clipAbove` allocates per non-set draw a frame and
  `drawWithWater` rescans the set's level triangles a frame (memo by
  revision, with bug 2); HD glyphs area-averaged per draw; `smoothbody`'s
  `ord` sorted per grid point; `spriteColour` per emitter.
* **The plane scan with the remaster off** (`playremaster_world.cpp:26-27`,
  `waterPlane` / `groundLevel` on every set): the DEVELOPMENT build only,
  on the loading thread - the ports never compile it. A gate on the water /
  mist is optional. In original mode at run time: no cost (two float and an
  int compare a draw on GLES).

**Cleanliness:**
* **Vulkan unsplit** (DONE in part 2026-10-24, "The Vulkan split" below): ~1,100 lines to a `vkrender_remaster.inl` on GLES's
  pattern (upscale 2310-2458, post 2460-2945, reflection 4048-4250, the
  tile block of `begin` 3778-3840, the pack's light / normal branches of
  `poolTextures` and the BC / R8 probe), the members in one struct, stubs.
  Its own MECHANICAL commit after the fixes, byte-identical by `engine:
  remaster switch` and the Vulkan frame checks.
* GLES's `#if` in the class fragmented (DONE 2026-10-26, "The cleanliness batch" below) (:1423-1474): `bloom_`,
  `waterWant_` and its six, `fxWant_`, `lightBlack_`, the variant strings,
  the `refl*` ids unguarded - on the Vita. `static constexpr` zeros under
  `#else` compile every branch out. The variant sources kept as members
  after `link`; two consecutive `if (waterWant_ > 0)` (:273, :279) (DONE
  2026-10-26); the five-line `glTexParameteri` block six times; light maps,
  `postSrc_`, the glows, the LUT and `reflTex_` never counted by
  `gpuTexture()` (the Quest's budget) (DONE 2026-10-26).
* New `-Wshadow` (DONE 2026-10-26): a `view` parameter shadowing `PlayState`'s field in
  `playremaster_draw.cpp` (22, 31, 66, 95, 118, 131) and
  `playremaster_off.cpp` (33, 35); `vkrender.cpp:997` (`qi`).
* **Units** (DONE 2026-10-26): comments saying the unit is ~1 cm - it is the INCH:
  `lightprobe.h:65` (kCell ~5 m, not 2), `emitterlight.h:60-61` (the
  glow's ~11 m, not 4.5), `floorreflect.h:31` (~100 m, not 40);
  `playpreload.cpp:30` its own `kInchesPerMetre`. **The comments only** -
  the values were tuned by eye; a retune is the reader's, from a picture.
* (DONE 2026-10-26 but `normalMap`, which `texpack_tool` uses) `lighttiles.h:32` says "nearest the camera first" - nearest the cluster,
  unordered after `nth_element`; `Rect::z` written, never read.
  `TexturePack::replacement()` has no caller (still dead after
  `0120c502`); `normalMap` used by `texpack_tool` alone. `upper()` three
  times; the FNV / `hex16` in four modules; the level-triangle walk three
  times (`floorreflect.cpp:76` without the `t+2 < size` guard). Vulkan
  writes the requests of `remasterConfigure` itself twice
  (`playgpu_vulkan.cpp:40-45, 77-82`); its destructor leaves `shUbo_`,
  `litUbo_`, the shadow image / pass / framebuffer and `dsl1_` (older than
  the remaster); `makeShadowSet`'s comment on the pool reset is stale.
* (DONE 2026-10-26) `renderer.h:635-640` declares `waterPlane` / `groundLevel` outside the
  `#if` their definitions are in (a shared caller fails only at a
  `noremaster` link).

**The checks**: `engine: preload` never reaches the cap, the release or the
area mismatch (how bug 3 passed), its take needs `ready` in time (a
PLAUSIBLE flake under `--jobs 6`), and part 1 matches one exact Vulkan line;
`engine: remaster switch` reads the development GLES binary's symbols only
and `make noremaster` builds with `VR=1` - the ports have none (build it
`VR=0`); `nm -j` is macOS's. `engine: block formats` reads the FULL pack,
which moved to the external drive with the runtime pack's (`0120c502`) -
red here until it has a setting of its own.

**The order** (agreed with the reviewing session): bugs 1-5 and the two
one-liners; then the pack's lock scope, Vulkan's batched upload with the
decode in `prepareSet`, GLES's pins; then the split and the 4K trims,
measured with `OMK_GPU_TIMES=1` on a QUIET machine (4K Qalisar, before and
after).

**The checks on the M1 (2026-10-24, MacBookPro17,1, `b7fb18e6`)**, the pack
on the external disk (`omk.conf`: `pack = /Volumes/Crucial_X8/omk/omk-pack`,
`full/pack` with PNG, BC7, BC4 and ASTC). The 21 remaster checks with
`--only`, `--jobs 4`: **20 green, `emitter light` red** - `block formats`
part 4 (red at the audit, the pack moved), `preload` and `remaster switch`
among the green. No timing: the load average was 6 on 8 cores.
* **SDL3 does not build** (FIXED 2026-10-26, "SDL3 builds" below) (`sdlfront.cpp:539-540` and three more:
  `SDL_AudioSpec` has no `callback` / `userdata` in SDL3), and the Makefile
  takes SDL3 wherever `pkg-config` has it - this M1 has 3.4.18 beside
  2.28.5. `make SDL_PKG=sdl2` (and `MAKEFLAGS=SDL_PKG=sdl2` for `verify.py`'s
  own makes) until it does. **The trap**: the failed SDL3 make left objects
  built with `-DOMK_SDL3` in `build/obj/play-gles` that the SDL2 make then
  linked (make does not track flags) - a GLES viewer importing
  `SDL_GetWindowSizeInPixels` whose `OMK_VERIFY_GPU_PRESENT` never ran, so
  `gles upscale` failed part 4 and `body light` once. `rm -rf
  build/obj/play-gles build/obj/play-vk` after a make with the wrong SDL.
* **`emitter light`'s `tight == loose` is not the tiles** (the cause found 2026-10-25, below): tight and loose
  differ by 1-2 pixels, one 565 step, at (510,55) and (501,249) of Qalisar
  640x480 - and so do two runs of the SAME tight build (each pixel takes one
  of two values per run). A 1-unit margin on the tight test's radius leaves
  the frame byte-identical; the 313 `View::points` are the same in order and
  value every run; the pack pools the same counts. The flips need the
  `--remaster` preset's RELIEF (`--relief 0`: none in 5) and TRILINEAR
  (`--filter bilinear`: none in 5); off with the pack alone or the lights
  alone. Every KTX2 in `full/pack` carries its full chain (6702 files), so
  the suspect is the levels as UPLOADED - a level never written, read at a
  distance through the relief's taps - with audit item 9's partial-chain
  worry, on Vulkan. The check is right to be red until that is found; not
  M1-only either, the M3's runs may simply have landed on one value.
* **The 4K frame on the M1** (the M3's table above, same street and flags:
  `--vulkan --res 3840x2160 --remaster --enhance-all`, hidden window,
  `OMK_GPU_TIMES=1`, 600 frames, frames 240-599 averaged; load ~2.3, the
  BARE binary - the M3's GPU-clock cap showed only past ~1140 frames):

  | `--upscale` | ms a frame | fps | world begin..end | present | GPU: shadow / reflection / scene / post |
  |---|---|---|---|---|---|
  | off | 39.2 | **25.5** | 33.3 | 4.1 | 0.23 / 2.32 / 12.38 / 6.03 = 21.0 |
  | 1.5 | 28.9 | **34.6** | 20.1 | 6.9 | 0.27 / 1.45 / 5.65 / 2.70 = 10.1 |
  | 2 | 24.0 | **41.6** | 15.2 | 7.0 | 0.23 / 1.05 / 3.41 / 1.62 = 6.3 |

  (the M3: 42.6 / 57 / 68 fps.) Repeated 20 minutes later: 25.7 / 34.6 /
  41.6, every GPU pass within 0.15 ms - the figures hold. At x2 the GPU is 6.3 ms of a 24 ms frame:
  the CPU's submission (~9 ms of `world begin..end` past the GPU's) and the
  4K present (7 ms) are the frame - the Vulkan trims of the list above buy
  little there; at native 4K the GPU's 21 ms is most of it.
* **The preload's uploads, the same runs**: 7 frames each uploading 3
  textures ahead (all from block files). The upload ~4.7 ms a frame on every
  run; the READ AND DECODE 10.7 ms a frame (worst frame 21.7 ms) on the
  first run - the pack cold on the external disk - and 1.3 ms once in the
  file cache. The read on the render thread is the cost a cold pack pays:
  the audit's "decode into `prepareSet`'s thread" first.

**Bugs 1-5 FIXED (2026-10-24, M1)**, each with a check shown to fail on the
old code:
1. **GLES floors** - `setView` halves the picture's own viewport when
   `passFbo_ && reflHalf_` (Vulkan's `setViewport` rule), `begin` no longer
   sets one it overwrote. `engine: floors` part 5: the pixels the floors move
   on GLES and on Vulkan overlap 0.670 (IoU; 0.826 against the untiled
   Vulkan), 0.003 with the bug (282 pixels moved, against 3874).
2. **The clip memo** - per `Geometry::revision`: a new revision empties a
   geometry's entry, the motion patch's (`dirtyFrom` = the memo's revision)
   drops only the draws over a moved corner (sorted copy - `dirtyCorners` is
   not). New `engine/tools/clip_memo.cpp` + `engine: clip memo`: one geometry
   holding sets in turn - the old memo reflects 6 corners for 3 after the
   reload and 6 for 0 after the patch.
3. **The preload's cap** - a want nearer than the farthest held releases it
   (`@evt preload.drop ... for= metres= forMetres=`; every drop now carries
   its metres). `OMK_PRELOAD_HELD=n` lowers the cap for a test. `engine:
   preload` part 5, Qalisar's street at cap 1: QSEXSHP1 (17.81 m) released
   for QBAR (17.39 m); with the old `break` nothing released. Seen on the
   way, not touched: every Qalisar start prepares ANEKBAH "6.5 m away" and
   releases it at 60.9 m one pass later - the first distance pass runs from
   a position before `--stand`'s (a wasted `prepareSet`, not a wrong frame).
4. **Vulkan's images** - ONE `makeImage2D` (image, memory, bind, view, or
   none and a line) behind all ten sequences (the audit's seven, plus the
   shadow map, the present image and the floors' depth); `vkrender.cpp` has
   one `vkCreateImage`. A refused texture has no image and no set and draws
   white; the 1x1 stand-ins are their own category, and a draw with no set
   at all is skipped. The shadow map's refusal now REFUSES THE RENDERER:
   set 1 is made with it and carries the lights and tiles, so the old "no
   shadow map, fitted blobs instead" path faulted in MoltenVK's first draw
   (SIGBUS) - never a working path. (The `qv`/`qf` leak is the PRESENT
   pass's shader modules, not `makePostAt`'s - fixed there.)
   `OMK_VK_IMAGE_FAIL=<category>` refuses a category as out of memory; new
   `engine: vulkan images`. Frames unrefused byte-identical before and after.
5. **The ignored options** - `Settings::anyRemaster()` and
   `PlayOptions::anyRemaster()`; `engine: remaster switch` part 4 also tries
   `--bloom 1` and `[Remaster] water = 1` (`--config`) - both silent before.
Also: `env switches` was red since the block formats (`OMK_NO_BLOCK_FORMATS`
read, never listed) - its row added. The remaster checks after: all green
but `emitter light` (the run-to-run flip above).

**Then (2026-10-24, M1), item 6 and the first performance items:**
* `.cube`: a non-finite value refused (`std::isfinite` in `parseCube`);
  `engine: bloom and grade` part 2 refuses a `nan` and an `inf` table, and
  reads both without the test.
* A glow whose ramp is black is DARK (`hi > 0`, as a flame): no 0/0 hue, no
  light slot. NOT CHECKED, and none can be on this data: Anekbah, Qalisar
  and Jaunpur keep their counts (117/12/24, 89/8/7, 15/0/32) - none of
  their glows is black.
* **The pack's lock**: `replacements` hashes its keys BEFORE taking `m_`
  (and `aliasGrey` both of its), the lock for the maps alone.
* **The decode on the load's thread**: `TexturePack::prepare` - called by
  `remasterPrepareSet` for every set load when the renderer `takesPack()`
  (Vulkan, GLES) - decodes what the set's pool will ask, SERIALLY on that
  thread (`replacements(..., pooled = false)`: through the shared pool, six
  jobs took its workers and the street's first pool decoded in 59.5 ms, not
  10.7), and PARKS each result; `replacements` hands a parked one over once
  (the colours asked, the normal maps as parked). 256 MB at most, the oldest
  let go; a released preload `forget`s its own. `@evt texpack.pool` /
  `texpack.preload` carry `prepared`; `engine: preload` part 3 wants all 7
  of ARESTO's prepared (0 without the call). GLES gains the same: an
  arriving set's decode is done before it arrives (its pins are still open).
* **Two preload jobs in flight** (`kInFlight`), the nearest first - six
  started in one frame competed with the frame for the cores and the disk.
* Measured, 4K Qalisar (load ~3-4, two runs): the street's first pool's
  read and decode 10.7 -> 5.5 / 5.5 ms, the second 4.6 -> 10.5 / 4.8; a
  preload frame's 1.0-2.0 -> 0.3-1.1 ms; fps unchanged (24.6 / 25.1). The
  UPLOADS are unchanged, ~5 ms per 3 textures - the batched upload is next.
* **The batched upload (Vulkan)**: a pool's copies recorded into ONE
  command buffer (`uploadCb`), submitted and waited on ONCE at the pool's end
  (`flushUploads`), staged in a persistent 32 MB host-visible RING (16-byte
  slices; full, the batch is flushed first; a texture past it its own buffer,
  freed at the flush) - not a staging buffer, an allocation, a submit and a
  `vkQueueWaitIdle` a texture. Frames byte-identical to `263e28b` on five
  paths (the preset trilinear, plain, the preset nearest, the pack's blocks,
  its PNGs through the blit ladder). 4K Qalisar, old against new back to
  back, twice: the first pool's uploads 60.7 / 43.2 -> 36.7 / 25.9 ms, the
  second's 22.9 / 21.1 -> 9.0 / 9.6, the preload's 1.69 / 1.58 -> 0.99 /
  0.75 ms a texture. NOT DONE: the image memory sub-allocated (still three
  allocations a texture with its maps).
* **The emitter light's flip, narrowed** (FOUND AND FIXED 2026-10-25, "The
  emitter light's flip, found" below): it needs the pack's BLOCK files -
  with `OMK_NO_BLOCK_FORMATS=1` (its PNGs, the blit ladder) none in 5 - and
  not the levels under 4x4 (uploaded without them it still flips). The BC4
  light maps' levels sit at 8-aligned offsets (392 of 1688), legal for an
  8-byte block; every BC7 level is 16-aligned. Open.
* **GLES's pins** (2026-10-24): `setPreloadTextures` / `preloadStep` in
  `glesrender_remaster.inl`, as Vulkan's - a `pinned` flag on the pool's
  entry that `setTextures`' eviction spares, the listed textures not yet on
  the GPU queued and uploaded 3 a frame through `uploadOne` (one texture's
  upload, split out of `setTextures` unchanged - frames byte-identical to
  `263e28b` in original mode and with the pack, trilinear and nearest). The
  pack hands a pool asking NO normal maps (GLES's) the PARKED result too,
  its normal maps dropped: before, GLES re-decoded every texture the set's
  load had prepared whenever the relief was on (the preset's), since the
  parking asked the normals and GLES did not. The restaurant walk on GLES:
  ARESTO's 7 textures up ahead, 7 of 7 decoded ahead (0.1-0.3 ms a step,
  uploads 1.2-1.5 ms a texture), the arrival's pool **0 new, 0.0 ms**
  against 7 new and 8.8 ms without (59 ms before the decode moved to the
  load); the frame the same bytes. `engine: preload` part 6, shown to fail
  three ways (part 1 for the eviction). DONE 2026-10-26 ("The last batch"): on GLES the relief is not
  drawn, yet `setNormals` still has the load decode the normal maps it
  parks - work and up to its share of the 256 MB for nothing there.
* **`engine: block formats` red since `263e28b`, the check's measure**: its
  part 4 summed the read and decode of the restaurant's arrival pools, which
  the set's load now decodes ahead (~0.6 ms with blocks or PNGs) - what was
  left was the 5 characters' textures, and their noise (28.5 against 65.8
  ms, a third wanted). It now sums the pools the frame decodes itself
  (nothing prepared, the whole walk - 10 textures): 24.5 / 23.8 ms from
  blocks against 101.3 / 102.8 from the PNGs. Not run after `263e28b` -
  found running it for GLES's pins.
* **The Vulkan split** (2026-10-24): the upscale (`makeUpscale`,
  `killUpscale`), the post pass (`makePost` .. `recordPost`, the LUT) and
  the water reflection (`makeReflection` .. `drawWaterScene`) - 750 lines -
  moved UNCHANGED to `backends/vulkan/vkrender_remaster.inl`, included at
  the end of `vkrender.cpp`'s anonymous namespace (GLES's pattern). No stubs:
  the backend is never compiled without OMK_REMASTER (the Makefile drops
  it), and an `#error` says so. The mirror pass stays (the original's two
  passes); its comment, which sat above the reflection's section, moved down
  to it. Frames byte-identical to `c2a78d8` on five paths (plain trilinear,
  the preset on Qalisar and the canal, the upscale x1.5, the water and bloom
  alone); the ten checks that read Vulkan's source now read both files
  (`_engine_src`), all green; `licence headers` 677 -> 678. LEFT INLINE, as
  they need code changes rather than a move: the tile block of `begin`, the
  pack's light / normal branches of `poolTextures`, the BC / R8 probe, the
  members in one struct.
* **The 4K trims (Vulkan), 2026-10-24, M1** - 4K Qalisar, `--vulkan --res
  3840x2160 --remaster --enhance-all` (4x MSAA, the AO, the bloom, the
  grade), hidden window, `OMK_GPU_TIMES=1`, 600 frames, frames 300-599
  (`gpu.time` windows 5-10), the Parallels VM stopped, load ~1.3-2.9. The
  baseline `c7a1c98`: 22.3-22.5 ms of GPU a frame (shadow 0.35, reflection
  2.4, scene 13.0, post 6.5-6.7), repeatable within 0.25 ms. THE POST
  BROKEN DOWN by switching its parts off: the AO ~4 ms (its view-z
  reduction 1.1, the occlusion 1.2, the two blurs together ~1.0 - timed by
  skipping each, a scratch build), the bloom ~0.7, the copy and the final
  pass ~1.8.
  * **The copy gone**: when the post runs, `begin` draws the scene into
    `postSrc_` through a second framebuffer of `pass_` (`fbufPost_`: the
    resolve, or the attachment itself without MSAA), and the final pass
    writes every pixel of `colour_` - no `vkCmdCopyImage` (66 MB a 4K frame).
    `postNow_` is decided by `begin`, where the target is chosen.
  * **The stencil not stored**: cleared at each pass's start, never loaded.
  * **TRANSIENT attachments in lazily allocated memory** (`makeImage2D`):
    the MSAA colour, the depth when no AO reads it, the reflection's MSAA
    colour and depth, the floors' half-size depth - MEMORYLESS on Apple's
    GPUs (MoltenVK has the type): 130 MB at 4K for the scene alone, 433 MB
    with the water's target (`@evt vulkan.memoryless`). The figures are the
    allocations' sizes; MoltenVK backing them with no memory is its
    documented mapping, not measured here (the profiler build would).
  * Measured, all three, against `c7a1c98` back to back, two pairs: 21.57 ->
    20.72, 22.37 -> 21.92 ms (the post 6.4-6.7 -> 5.5-6.2; the copy alone,
    four pairs, 22.40 -> 22.22 on average - the post faster by 0.6-0.9 but
    the scene's stamp 0.3-0.5 later; the stencil alone within noise). A
    small win in time; the memory is the larger one.
  * **Frames byte-identical** to `c7a1c98` on nine paths (the preset on
    Qalisar and the canal, `--enhance-all`, the bloom alone, the upscale with
    4x MSAA, plain, trilinear, the water with 4x MSAA, the set viewer facing
    Kay'l's mirror).
  * **The validation layer** (Homebrew's 1.4.357; it loads only with
    `DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib` - the manifest names the
    library bare): the new framebuffer first wanted `postSrc_` with
    TRANSFER_SRC usage (fixed). It also found FIVE older faults, all in
    `c7a1c98` too, now fixed: a one-level texture moved to TRANSFER_SRC
    without that usage (it goes DST -> shader now); the shadow map sampled
    UNDEFINED by a run whose shadow pass never draws (made SHADER_READ at
    creation); the water reflection's colour CLEARED without TRANSFER_DST
    usage (its clear and transition refused, the image then read UNDEFINED);
    the post's down and blur V passes binding the glow image they write, and
    the glow passes binding the occlusion before its first run (their unused
    bindings now `postSrc_`). LEFT: `VUID-vkCmdDraw-imageLayout-00344` on the
    reflection pass - the texture sets bind the reflection image (binding 2,
    `refl`) while the pass draws into it; the shader reads it only on the
    water's draws, so it is the layer's static view (a second set per
    texture would silence it). Plain, the upscale and the bloom: silent.
  * NOT DONE, measured small or moot: the glow at B10G11R11 and the blur as
    11 bilinear taps (the whole bloom is ~0.7 ms; both change pixels); the
    glow images made with the bloom at 0 (8 MB at 4K); the post over the
    letterbox bands (the target is the letterboxed size already); the
    reflection's full-size MSAA targets for a floors-only scene (memoryless
    now on Apple, still real elsewhere); the AO's depth through
    `VK_KHR_depth_stencil_resolve` (SAMPLE_ZERO) - it would make the depth
    memoryless under the AO too (~166 MB at 4K) and let the view-z pass read
    one sample, but needs `vkCreateRenderPass2` and a resolve attachment in
    every framebuffer of `pass_`; the AO's own 4 ms, the largest cost left.
  * `engine: profiler gpu` was RED since this session's earlier commits, not
    from these: the 1x1 fallbacks' own category (`6319abd`) and the staging
    ring (`c2a78d8`) - re-pinned (39 + 3, the ring alone at rest), shown to
    fail. `engine: bloom and grade` and `engine: ao` read the new lines.
* **The emitter light's flip, found (2026-10-25, M1)**. Reproducing it
  needed the CHECK'S OWN ARGUMENTS - `--save traces/save-appart.bin`: without
  the save, 48 runs over three builds, under CPU and GPU load, gave one frame;
  with it, 10 runs gave 5. The cause: IMPLICIT LOD PAST A NON-UNIFORM BRANCH
  in `scene.frag`. `relief()`'s four brightness taps followed the normal
  map's early return (`if (nm.a > 0.0) return`), so in a 2x2 quad where a
  neighbour had returned, `texture()`'s derivatives - and the mip level - were
  UNDEFINED; so were `relief()`'s own `dFdx`s, its normal-map read, the
  floors' normal-map read and the glow's light-map read past the cutout's and
  the water's `discard`s. Fixed: `main` takes `gradUV` / `gradW` before the
  water's discard (the cull's takes a whole primitive), and every read after
  goes through `textureGrad` (`relief()` takes the gradients as arguments).
  The colour read before the cutout stays `texture()` - the original's.
  The check's configuration: 5 frames in 10 before; 10, 8, 10 and 6 runs the
  same after (34 of 34), and the check green 3 of 3. `engine: relief` part 1
  holds the rule (no `texture()` or `dFd` in `relief()`, the gradients before
  the water's discard, the normal map and the light map through
  `textureGrad`), shown to fail twice; `relief`, `floors`, `water
  reflection`, `bloom and grade`, `body light` green. The earlier narrowing
  (blocks, relief, trilinear) was what made the undefined LOD SHOW: a level
  choice matters only where the levels differ.
* **FOUND AND FIXED 2026-10-26 (below) - without MSAA, the same Qalisar frame differed on EVERY run**
  (`--aa 1`, which `--enhance-all` overrides, so no check sees it): ~2000
  pixels of the wall behind the pink sign (340-410, 0-95 at 640x480), in
  blocks, from frame 2 on - and older too (`263e28b`, `c7a1c98`). Ruled out,
  each by hashing or zeroing on the backend's side, the frame still varying:
  the light and tile data (the last frame's UBO, points and tile words
  byte-identical over runs), the vertex data (every upload hashed; the
  vertex-buffer cache off), the textures (all 53 uploads hashed), the draw
  list (785 draws, same order and fields), the pose buffer (zeroed; its
  cache off), the light buffers (zeroed), texture eviction (off), the
  reflections (floors and water off), the post pass (bloom, AO and grade
  off), the relief, the normal map, the shadow map; no MoltenVK or Metal
  error, Metal's shader validation silent, the Khronos layer only its known
  static `refl` report. What it NEEDS: the emitter lights' TILED pipelines -
  `OMK_NO_TILES=1` and `--fx-light 0` are stable - and not their light loop
  (skipped, it still varies). A debug output of the fragment's world
  position shows DIFFERENT SURFACES winning those pixels from run to run
  (positions thousands of units apart), not one surface lit differently.
  Next: which draws use `pipeTiled_` there and whether its vertex stage
  places them as the plain pipeline does (a Metal compile of the vertex
  function per pipeline, its depth then not the plain one's), and a Metal
  capture on a machine with Xcode.
* **The no-MSAA variation, found (2026-10-26)**: a CUTOUT's `discard` that
  let depth through. Bisected on the backend's side: the tiled PIPELINES with
  everything else the frame's (plain pipelines with the tiles on: 1 frame in
  5; tiled: 5 in 5); then the module - its tile block removed, still 5 in 5;
  its storage buffers too, still; the two modules made byte-identical SPIR-V:
  1 in 5; adding back the relief's code alone (its strength 0, returning at
  once): 5 in 5; the tile block alone: 5 in 5; the floors' normal-map read
  alone: 1. So no one feature: the module's SIZE. With the cutout's
  `discard` disabled: 1 in 5; with `gl_FragDepth = gl_FragCoord.z` written:
  1 in 5. A debug output of the winning fragment's world position had shown
  surfaces thousands of units apart winning those pixels: a cutout's keyed
  texels sometimes WROTE DEPTH, hiding what lies behind - and where they
  wrote colour too, the mip levels' blend of the atlas's neighbours showed
  ("other images"). On Apple's GPUs, through MoltenVK, once the module grew
  past some size; the 2026-10-20 note in `scene.frag` (the storage buffers,
  "present but never run", darkening a cutout sign's keyed texels) was the
  same family. FIX: the OPAQUE CUTOUT draws alone bind a variant of their
  pipeline whose module writes `gl_FragDepth` (`scene.frag` OMK_LATE_DEPTH,
  `scene_cut.frag.inc` / `scene_tiled_cut.frag.inc`, `pipeCut_` /
  `pipeTiledCut_`): the depth decided after the shader, a discarded texel
  writing none. Every draw that way costs the hidden-surface removal - 5.4 ms
  of 13 at 4K, measured - the cutouts alone 0.4-0.65 ms. After: no MSAA 5 of
  5 and 8 of 8, the check's MSAA configuration 6 of 6; the frame within 1-7
  pixels (one 565 step, scattered - the written depth's precision on the
  non-cutout draws) of the late-depth-everywhere one; ORIGINAL MODE
  byte-identical (Anekbah plain and trilinear, Qalisar). `engine: emitter
  light` part 6 (three no-MSAA frames the same, and the source), shown to
  fail (2117 and 1763 pixels apart). The validation layer: only its known
  `refl` report. GLES draws its cutouts with `discard` too and does not tile:
  measured 2026-10-26, Qalisar with the full preset at 1920x1080, 3 of 3
  runs identical with 4x MSAA and 2 of 2 without - not affected.
* **The GLES items (2026-10-26)** - the audit's GLES line, and Vulkan where
  the same waste was there:
  * THE WATER'S SECOND `begin`: GLES's `begin` is now the frame's
    bookkeeping (the upload counters, the dead buffers, `frameNo_`) and then
    `beginPass` - the target, its clear, the view, the lights; the water's
    frame pass calls `beginPass` alone. The clear stays (a different target)
    and the lights are a handful of uniforms; what mattered was `frameNo_`:
    bumped twice, every posed body's ties were walked again in the frame's
    pass - 15-23 posed draws a frame on Jaunpur's canal, each now walked once
    (the reflection's pass draws every posed draw, in order, so the frame's
    pass finds them all settled). Frames byte-identical. Vulkan's `begin` is
    called twice too and rewrites the pose blocks at the same offsets with the
    same data - harmless, left.
  * `fsrPass`: its 11 uniforms looked up once (`fsrLoc_`), and ONCE a frame
    (`fsrVh_`, forgotten at `end`) - a frame read back and then presented ran
    both passes twice.
  * THE POST WITH NOTHING GLOWING: GLES runs the glow passes only when a draw
    of the frame's pass bound a light map (`lightMapNow_`), and the post only
    for them or the grade; Vulkan runs the post only for a pool holding a
    light map (`poolLit_`, its scene pass's target is chosen in `begin`,
    before any draw) or the grade or the AO, and the glow passes, again, only
    after a lit draw. A frame with no light map has a light of 0 in every
    texel: the copy, the three passes and the screen blend moved no pixel.
    4K, the street, bloom 1 and no pack, on the M1: Vulkan's post 1.99 ms ->
    0 (`OMK_GPU_TIMES`); GLES's readback, which waits on the GPU, 17.1-17.9
    -> 13.5-14.1 ms. Byte-identical on both backends (no pack, the full pack,
    the full preset at Qalisar, the upscale, the canal with everything) but
    one case, GLES with a grade and the bloom and no light map: 152 pixels by
    one 565 step, the old frame's `1 - (1 - c) * (1 - 0)` rounding before
    the LUT - the new frame is the grade-alone frame exactly.
    `engine: bloom and grade` part 3 (the bloom with no light map at all:
    the frame unchanged and no post built), shown to fail on each backend.
  * THE WATER'S UNIFORMS (`reflInv`, `eye`, `clipY`) through the draw-state
    cache, as `water` and `clip` already were.
  * THE MEMORY: the post's copy and glows, the reflection's colour and depth
    (a renderbuffer), the LUT, the light maps and `lightBlack_` counted; the
    1x1 white and `lightBlack_` as "texture stand-ins", Vulkan's category.
    The canal with the water and a grade at 640x480: 7 render targets,
    5,068,800 bytes, where 2 were counted. `engine: profiler gpu`'s GLES
    part, shown to fail.
* **Item 8 (2026-10-26), Vulkan's present-pass barriers**: `runPresentPass`'s
  two transitions of `colour_` now name their scopes - in, after the scene's
  or the post's colour writes and `presentSurface`'s copy, before the
  fragment shader's reads; out, after those reads, before the readback's
  copy or `presentDirect`'s blit reads it - and `presPass_` states its two
  external dependencies (after the last blit's or copy's use of `presImg_`,
  before the next one's read). Only the host's waits (the frame's fence,
  `oneShotEnd`'s queue idle) kept the old ones right. Byte-identical (x2
  and `--ssaa 2`, the M1), `engine: gpu present` / `fight gpu present` /
  `vulkan images` green. The Khronos layer (1.4.357, synchronization
  validation ON - its CURRENT-VALIDATION-ENABLED line says so) silent before
  and after - and silent too on a hazard PLANTED in the EASU pass (its
  external dependency removed), which moved 7 pixels of a frame: on this
  machine the sync validation is no guard. So `engine: upscale` part 1 pins
  the source, shown to fail (the first transition back to the top of the
  pipe).
* **The preload's cancel (2026-10-26)** - the audit's "preset's 25 m"
  item. The two in flight were already there (`kInFlight`, 2026-10-24);
  what was left:
  * A set RELEASED while its job prepares is CANCELLED: `SetLoad::cancelled`
    (a `std::function`, so no `<atomic>` in the ports' headers - the flag
    itself lives in `playpreload.cpp`) is asked between `prepareSet`'s five
    stages, the last before the pack's decode; the job is kept in
    `preloadDropped` and COUNTS IN FLIGHT until it returns (before, a
    released job ran to its end uncounted, beside two new ones).
    `remasterFinish` (the session's end) cancels what still prepares and
    waits for every job - they capture `this` (the preload's half of audit
    item 9). `@evt preload.cancelled` says where each stopped.
  * Where the renderer keeps no textures (`Renderer::keepsPreload`, false
    on the software reference) 2 are held, not 6. MEASURED: no memory saved
    on Qalisar's walk (peak 92-93 MB both ways, 5 held against 2) - the held
    interiors are small beside the resident city; it saves a want or two of
    thread work, no more.
  * THE PACK'S PARKED DECODES COUNTED (`parkedRefs_`): two sets sharing a
    texture park it once (`prepare` skips a parked key), and one set's
    `forget` dropped it from the other - AAPKAYL and ARESTO share
    `APPART10`. Forgetting a cancelled job that returned past its last stage
    turned `engine: preload` part 3 red in 4 of 6 runs (6 of 7 decoded
    ahead); the old code mostly escaped by order. Now a key is forgotten as
    many times as it was prepared.
  * `engine: preload` part 7 (part 5's walk, each job 3 s late via
    `OMK_PRELOAD_DELAY_MS`: ANEKBAH cancelled at stage 0 - QSEXSHP1 once the
    stale first pass was fixed, the next want
    counting it running, never more than one; held 2 on software, 6 on the
    GPU walks) and a part-1 pin for the count - shown to fail four ways (no
    cancel: stage -1; the dropped not counted: 0 running; the cap ignoring
    the renderer: 6 on software; the count's guard removed: part 1).
  * FIXED THE SAME DAY (the reader's next "2"): the FIRST distance pass of a
    `--stand` start measured from the area's first ADDRESS - the session
    places him there, and `--stand` moves him only at the hand-over, when
    his controller is built - Qalisar's wants ANEKBAH and TUNELAQ1 at 6.5 m,
    released at 61 and 125 m ten frames later; Anekbah's restaurant walk
    AAPKAYL and AHALL27 at 2.5 m, released at 48.7 m. The distances now wait
    for the hand-over (`handoverFrame`). A save's own start was not wrong (no
    stand; its address start wants the same two sets, at 1.0 m from where
    the body stands rather than 2.5 from the address). `engine: preload`
    part 3: ARESTO the only set wanted, Vulkan and GLES - shown to fail
    without the wait (AAPKAYL and AHALL27 wanted). Part 7's cancelled set is
    now QSEXSHP1 (released for QBAR's nearer door while it sleeps).
* **SDL3 builds (2026-10-26)** - the audit's note. Thirteen errors at four
  sites, and one fault that compiled:
  * the AUDIO DEVICE: SDL3's spec is the format alone and the callback the
    stream's - `feed3` mixes the asked bytes (whole frames, rounded up) into
    `abuf_` and puts them; `SDL_AUDIO_F32` (native order there), the audio
    subsystem started, `SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES` 1024 (SDL2's
    `samples`). Both now print the device's real format (`audio: device ...`):
    SDL2's device reopens at the world's 22050 Hz, SDL3's stays at 44100 and
    its stream resamples - the same signal, every other SDL3 frame within
    1e-6 of SDL2's where the gaps fell alike (the disk driver's output);
  * `SDL_StartTextInput` / `SDL_StopTextInput` take the window in SDL3;
  * the Vulkan window, its extensions and its surface: `vkSdlWindow`,
    `vkSdlExtensions`, `vkSdlSurface` in `playgpu_vulkan.h`, for the viewer's
    window and the set viewer's;
  * COMPILED BUT WRONG: GLES's `SDL_InitSubSystem(SDL_INIT_VIDEO) == 0` -
    SDL3 answers true on success, so GLES fell to the software reference;
  * the options' resolutions: SDL3's fullscreen list need not hold the
    desktop's mode (its dummy driver lists none) - added, as SDL2's list
    carried it (`engine: options menu` red under SDL3 without it).
  THE TRAP closed in the Makefile: SDL3's frontend objects compile into
  their own `play-*-sdl3` directories, and the instruments' stamp names the
  SDL package, so switching relinks. `OMK_AUDIO_DEVICE=1` opens the world's
  device in a `--frames` run. `engine: sdl3` (new): both flavours built into
  `build/sdl/`, the software and GLES frames the same bytes, Vulkan's window
  0 pixels from the CPU's on both, the device open and sounding on both
  (SDL's `disk` driver) - shown to fail twice (the `== 0` back: GLES on the
  software reference; `feed3` not handed over: SDL3 silent). SDL3 is now
  this M1's default build; 21 frontend and GPU checks rerun green on it
  (the next full sweep is its first). NOT JUDGED: SDL3's resampled sound by
  ear - the reader's.
* **The cleanliness batch (2026-10-26)** - the audit's cleanliness section,
  mechanical: every scene of a fixed set the SAME BYTES as before, ten
  scenes (software plain and with the pack, Vulkan's harness at Qalisar, the
  canal and `--enhance-all`, GLES at Qalisar, the canal and plain, and both
  `noremaster` viewers), the set itself repeating byte for byte first.
  * `-Wshadow`: the remaster hooks' `view` / `draws` parameters `v` / `list`,
    the query pool's `qi` `qpi` - the builds warning-free.
  * THE UNIT, the comments only (the values were tuned by eye): the probe's
    cell ~5 m and reach ~15 m, its body height ~2.3 m (meant as the hips,
    read as centimetres - unchanged), the glow 450 units ~11 m and the flame
    ~23 m, the floors' choice ~100 m; the preload uses `omk::kInchesPerMetre`
    rather than its own 39.37.
  * `lighttiles`: a full cluster keeps the lights nearest IT, unordered after
    the cut (the header said nearest the camera); `Rect::z` gone.
    `TexturePack::replacement()` (no caller) gone.
  * ONE COPY EACH: the packs' FNV-1a and the 16-digit file name
    (`platform/packkey.h`, four modules - `engine: texture pack`'s 2534 keys
    unchanged), `upperAscii` (`platform/datafs.h`, three), the LEVEL-triangle
    walk (`forLevelTriangles` in `renderer.h`, three - now bounded by the
    corners too; each caller keeps its own area arithmetic, float or double,
    so the planes are the same bits).
  * `renderer.h`: `waterPlane`, `groundLevel`, `drawWithWater` declared
    behind OMK_REMASTER as defined; `WaterPlane` stays shared (a world slot
    holds one).
  * Vulkan: both of its windows' renderers take `remasterConfigure` (and the
    AO, Vulkan's alone) instead of its six requests written out twice; the
    destructor destroys the mapped shadow's image, view, sampler, pass,
    framebuffer, pipeline and layout, set 1's layout and the two UBOs -
    `engine: vulkan teardown` (new, the Khronos layer over `shadow_probe`;
    the viewer never deletes its renderer): shown to fail, 2 reports of
    objects outliving the device without it. `makeShadowSet`'s comment: the
    pool is no longer reset, the set is made once.
  * GLES: the remaster's members in ONE `#if OMK_REMASTER` block, and
    without it `static constexpr` zeros, so the shared code's branches on
    them compile out on the ports; `submit`'s water uniforms and the bloom's
    light map moved to the `.inl` (`remasterWaterUniforms`,
    `remasterLightMap`), the destructor's remaster frees to
    `remasterRelease`, stubs beside the others; the draw-state cache's
    counting a member (`stateSet`); `texParams(filter, wrap)` for the four
    calls twelve textures made by hand. NOT DONE: the variant sources kept
    after `link` (a few KB, and now in remaster builds alone).
* **Item 9 (2026-10-26)** - GLES's limits on the low end (Android, the
  Quest's lesser cousins), none reproducible on the M1, so each is read,
  guarded, and the guard pinned:
  * FSR WITHOUT HIGHP: where the fragment stage has no highp float
    (`glGetShaderPrecisionFormat`, GLES contexts; a desktop GL's 1.20 is
    fp32) the upscale is REFUSED - the prelude's mediump fallback is fp16 on
    a Mali-400, which flushes EASU's 1e-20 and loses a texel past 2048.
    `OMK_GLES_NO_HIGHP=1` stands for such a device: `engine: gles upscale`
    part 5, the refusal said and the frame the native one byte for byte -
    shown to fail with the test taken out. The NDK's clang compiles the
    GLES branch (`--target=aarch64-linux-android29`, syntax only).
  * AN ASTC CHAIN NOT WHOLE: `uploadAstc` uploads a chain only when it runs
    to 1x1 and says so; else level 0 alone and no mip in the filter - GLES2
    has no `GL_TEXTURE_MAX_LEVEL`, and an incomplete texture samples black.
    MEASURED: all 6698 ASTC files of the pack (2514 colours, 1666 light maps,
    2518 normal maps) hold their whole chains - the guard is for a pack to
    come.
  * A LIGHT MAP NOT A POWER OF TWO: clamped, as the colour is - GLES2 repeats
    only powers of two, and an incomplete light map's glow is silently
    black. None in the pack is. Both pinned in `engine: block formats` part 1,
    shown to fail each way.
  * The preload's jobs waited for at teardown: `remasterFinish`, the same
    day. A SET LOAD's own job (`askSet`) still captures `this` and nothing
    waits for it at the end - outside the remaster, left.
  All ten scenes of the batch's set the same bytes.
* **The preload radius, MEASURED (2026-10-26, the reader's "what do you think
  about the radius length?" then "run the measurement")** - M1, PACED runs (the
  hidden Vulkan window at 30 fps, `--remaster` with the pack, `--no-crowd`),
  two doors approached from ~30 m, walking (`k200`, ~2.1 m/s) and RUNNING
  (`k54+k200`, ~5.6 m/s - the order matters: `k200+k54` only walks), at 0, 5,
  10, 15 and 25 m; frame numbers from temporary prints, not committed:
  | door | pace | 0 | 5 | 10 | 15 | 25 |
  |---|---|---|---|---|---|---|
  | ARESTO (7 textures) | walk | arrival 12 new, 34.6 ms | 1.2 s spare | 3.9 | 6.3 | 11.2 |
  | ARESTO | run | 12 new, 33.6 ms | 0.2 s spare | 1.2 | 1.9 | 3.9 |
  | TUNELAQ1 (airlock) | walk | 8 new, 8.1 ms | never wanted | 2.0 | 4.3 | 9.5 |
  | TUNELAQ1 | run | 8 new, 7.7 ms | never wanted | 0.4 | 1.1 | 3.1 |
  ("spare": from the last texture uploaded to the arrival.) The work is
  prepared within one 10-frame pass and uploaded by frame 13-18 after the
  want - ~0.4-0.6 s. A door's ZONE fires metres before its point: 5 m never
  wanted the tunnel. With the preload the arrival still brings 4-5 new
  textures - the CHARACTERS', which nothing preloads (ARESTO 29-30 ms against
  34 without: the set's own 7 saved ~4 ms here; the block files decode fast
  on the M1). Reading: 25 m is ~4x what the M1 needs running; 15 m keeps
  1.1-1.9 s spare at a run, ~3x the work. A device several times slower (the
  Quest) is what the margin is for, and where its memory is the limit.
  THE PRESET IS 15 since (the reader's "yes"); `--preload 25` or the ini's key
  gives the old reach back.
* **Keeping the pedestrians' models, CHECKED (2026-10-26, the reader's "a lot
  of characters are generic ... keeping the pedestrian in memory could settle
  this")** - it already happens, and what is left is not theirs:
  * A city's crowd is FOUR models (Anekbah PSH/PSH1/FSH/FSH1, Jaunpur's souk
    KSH/KSH1/KWH/KWH1, Lahoreh PVH/PVH1/VFH/VFH1, Qalisar PSH/PSH1/AN1/KIL2,
    the well CMH) - 15 in all; the street's placed extras wear them too.
  * Walking in, they STAY on the GPU: the restaurant stages six PSH diners and
    its arrival uploads none of them - its 5 new textures are V5H_FNM (3) and
    SAM_FN (2), exactly; through an AIRLOCK too (the street stays resident
    in it: 14 character textures, 7 models, before and after `area.goto 235`).
    The same with and without the crowd drawn.
  * Over the game (each of the 260 areas started cold with `--area N`, what
    it stages at load - a script may stage more later): 86 of 253 non-city
    areas stage characters; their distinct models are 113 crowd (187
    textures) and 166 SPECIFIC (347); 34 of the 41 using crowd models within
    two hops of a city use that city's. The specific: 91 models, 71 of them
    in ONE area; the recurring few - ANO_FN 26 areas (1 texture), V5H_FNM 10
    (3), ANOB_FN 8 (1), V4H_FNM 7 (3), MCG_FN 6 (2), NUR_FNM 6 (3).
  * THE COST on the M1: an arriving character texture ~0.3 ms to read and
    decode and ~0.6 to upload with the OS's file cache warm; the 10-17 ms
    measured first were the external disk's COLD reads (the first run after a
    pause, crowd or not). The restaurant's 5: ~4 ms warm, ~28 cold.
  * So: NOT BUILT. Keeping the recurring specific models (~13 textures)
    would be the only gain left, a few ms; the off-frame decode of a model's
    textures when it loads (as a set's are prepared) covers the cold reads
    and every model alike, if a device (the Quest) ever shows the hitch.
* **The last batch (2026-10-26, the reader's "go with 1 to 4")** - the
  audit's remaining small items:
  1. GLES's NORMAL MAPS: decoded and parked by every set's load for the
     relief GLES never draws. `Renderer::drawsRelief` (Vulkan's true) and
     `remasterConfigure` turn them off for a renderer without it
     (`@evt texpack.normals`). M1, Qalisar's start, the preset on GLES: the
     city load's pack work 1030-1170 -> ~555 ms, the peak 512 -> 436 MB, the
     frame the same bytes. `engine: relief` part 5, shown to fail.
  2. `engine: remaster switch`: `make noremaster` builds `VR=0`, as every
     port without the remaster does; its symbol scan takes plain `nm`'s last
     field (`-j` is macOS's). Item 7: the PNG inflate reserves at most 1032:1
     over the bytes there are; a KTX2 is read only as the codec its name
     says - both pinned in `engine: block formats` part 1, shown to fail.
  3. THE UI PACK'S 16, measured: all 37 interface screens draw from ONE
     upscaled sheet (`gfxint.BMP`); the 52 packed bitmaps are 11 I2D sheets,
     full-screen pictures one at a time, three gauges and three weathers -
     no screen comes near 16. No change.
  4. A SET'S OWN LOAD JOB (`askSet`) is waited for at the session's end, and
     one replaced while it ran is kept until then (`setLoadsLeft`) - each
     captures `this`. `@evt session.loads`; `engine: tunnel door walk`'s new
     part (the city's load 3 s late, the run ended at frame 10: one running,
     waited for, none after), shown to fail.
  Left of the audit: Vulkan's image memory sub-allocated (three allocations
  a texture with the pack) - for a mobile Vulkan, not needed on a desktop.

## D1 - the rounded bodies: first cut (2026-10-22)

`--smooth S` / `[Remaster] smooth = S` (0..1, 0.75 Phong's own), off by
default; **the `--remaster` preset's 0.75 since 2026-10-22** (the reader: "do
it by default ... it is difficult to fully validate now without seeing every
character in the game, so maybe we will have to change it later" - the way
out below stays). Every backend:
the character's REST geometry is subdivided ONCE at load
(`engine/src/o3de/smoothbody.*`, hooked in `charModelFor` and the player's
load) - each triangle 3x3, every new point pulled toward its corners'
tangent planes by S (Phong tessellation, Boubekeur and Alexa 2008).

* Kept as they are: the triangles spanning two meshes (the skinned JOINTS -
  a new corner can follow one bone only; 30659 of 89107 over the 191 shipped
  characters), the FACE (its `.3DM` morph replaces vertices by
  `cornerVertex`; 13871), and every position on a crease (> ~45 degrees),
  an open boundary or a kept triangle - pinned, so joints and creases stay
  where they were. 44354 split.
* No crack: a point on an edge depends on its two ends alone, its weights
  integers over 3 and its corners summed in one order (by position) - the
  first cut summed in each triangle's own order and left the two sides of
  an edge an ulp apart (318889 open edges against 84012).
* No FOLD (the reader's play, 2026-10-22: "when the npc move their arm, the
  arm become empty", a shop seller): the full pull on a thin limb of few
  sides folded 453 sub-triangles over at 0.75 (145 at 0.5, 68 at 0.35), and
  a fold is culled from behind - a hole that opens as the limb turns. One
  pull PER EDGE now, shared by its two triangles, and a guard: a triangle
  that would fold has its interior unpulled, then its edges' pulls 0, until
  none folds - 0 left, 211 of 44354 interiors flattened. Damping every edge
  by its ends' angle also cured it and flattened nearly every body back
  (judged on a crop), so it was not kept.
* The CROWD stays flat: its bodies are posed on the CPU every frame, and four
  times the corners took 4K Qalisar from 58 to 34 fps. `CharModel::flat`
  keeps the unrounded rest for `lodRestFor`; staged characters and the
  player are rounded. Cost then (M3, 4K, `--upscale 1.5`, preset +
  `--enhance-all`): the street 57.3 -> 55.2 fps, the bar 93 -> ~85.
* `verify.py: engine: smooth bodies` (`tools/smooth_probe.cpp` over all 191
  models: originals kept, watertight, the face whole, nothing flung, no fold;
  the bar's frame moves), shown to fail three ways. Off: byte-identical to `451c0aa5` (3 of
  3: Vulkan, software, the harness with `--enhance-all`).

**The way out, kept** (the reader, 2026-10-22: "good for now, but maybe I
will find an issue later ... keep it easily changeable"): `--smooth 0` (or
the key unset) turns it all off, and `--smooth-skip A,B` / `[Remaster]
smoothskip = A,B` leaves the named models as they are - the name the log
prints ("smooth: V2H_FNM - ..."), any case - the rest still rounded.

**Open**: the joints - rounding them needs a corner to blend two bones,
which no renderer here does (one `inMesh` per corner); a per-model opt-out
list (the plan's), if a model's hard edges suffer; the crowd near the camera.

## A6 - the upscale: as built (2026-10-22)

`--upscale F` / `[Remaster] upscale = F` (1..3, 1 off), **off by default and
not in the `--remaster` preset** - a frame rate's choice, not a look. Vulkan
and, since 2026-10-22, GLES (below). AMD **FSR 1**, vendored verbatim (MIT,
`engine/third_party/ffx_a.h`, `ffx_fsr1.h`); MetalFX is out of reach through
MoltenVK.

* The frame is drawn `F` times smaller each way - every render size through
  one rule, `rs()` (the target, the viewport, the AO's view, both light
  binnings) - and `runPresentPass` brings it up: **EASU** (`easu.frag`) into
  an output-size image, then **RCAS** (sharpness 0.2 stops, AMD's sample)
  inside `present.frag`, BEFORE the original's 565 dither, so the dither stays
  the output pixel's. It takes `--ssaa`'s place (said, and ignored).
* The CPU paths: `readback` runs the same two passes raw (no dither) and
  quantises on the CPU as before; `presentSurface` (the interface over the
  world) uploads into the present pass's output-size image, `colour_` being
  smaller than the picture.
* `src/o3de/fsrcon.h` transcribes `FsrEasuCon` / `FsrRcasCon` (the header's
  CPU side drags in hundreds of warnings); `engine: upscale` compares the
  words with AMD's own.

Proved (`verify.py: engine: upscale`, shown to fail two ways): the 100
constant words AMD's; the street drawn at 640x480 for 1280x960, close to the
native frame and registered; the window's present pass 0 pixels from the
readback's over 90 GPU-presented frames (`OMK_VERIFY_GPU_PRESENT`). Off:
byte-identical to `451c0aa5` (`--vulkan`, `--world-vulkan`, `--ssaa 2`,
`--remaster --enhance-all`, 4 of 4); the Khronos validation layer silent with
it on (the world, and the pause menu over it).

**The 4K frame, 2026-10-22, M3** (Qalisar's street `--area 101 --stand
15516,0,-10182,180`, `--vulkan --res 3840x2160 --remaster --enhance-all`,
hidden window, one other session's Python on a core):

| `--upscale` | drawn at | fps | worst | GPU: scene / post / reflection |
|---|---|---|---|---|
| off | 3840x2160 | **42.6** | 28 ms | 7.4 / 3.4 / 1.1 ms |
| 1.5 | 2560x1440 | **57** | 20 ms | - |
| 2 | 1920x1080 | **68** | 19 ms | 2.0 / 0.8 / 0.5 ms |

At x2 the GPU's passes are 3.4 ms of a 14.7 ms frame: what is left is the
CPU's submission (`world begin..end` 8.7 ms with the wait) and the 4K present
(2.7 -> 4.7 ms, EASU and RCAS at the output size). The upscale removes the
GPU from the reader's 4K slowdown (open bug above) whatever its cause there.

**GLES (2026-10-22, the reader's "do ... 4)")**: the same rule (`rs()`,
`rw_`/`rh_`, the viewport, the posing self-test's target) and FSR 1
TRANSCRIBED into GLSL 1.00/1.20 (`kEasuFrag`, `kRcasFrag` in
`glesrender.cpp`: no unsigned integers, no `textureGather`; AMD's bit-trick
reciprocals as exact ones, RCAS's two divisions kept finite on pure black
and white). Two passes into output-size targets (`fsrPass`), then the
present pass's own dither, untouched; `readback` runs them over the last
view's picture rows (`picH_`). `--ssaa` is ignored beside it, as on Vulkan;
`presentEye` (VR) refuses it. `kFsrRcasStops` (`fsrcon.h`) is both
backends' sharpness. `verify.py: engine: gles upscale`: the street at x2
registered on native (mean 3.2 a channel), the fight's letterboxed approach
too, and the hidden window 0 pixels from the readback over 90 frames; shown
to fail twice. Against Vulkan's x2 on the Qalisar street: a mean 2.5 grey
levels.

**Open**: the sharpness as an option, if the reader finds RCAS's 0.2 too
strong - judged in play, not yet.

**Measured at 4K on the M1, 2026-11-13** (`todo/perf-audit.md` 11): EASU 3.07
ms, RCAS and the present pass 1.59. Half precision (`FSR_EASU_H` with
`shaderFloat16`): ~2.7 ms for 4.5% of the pixels moved, up to 36/255 on edges -
not taken (`easu.frag` says so). EASU and RCAS fused in one compute pass: the
intermediate's round trip is ~0.15 ms (RCAS with its reads replaced by a
constant: 1.59 -> 1.44) against ~27% more EASU for the tiles' border and a
second copy of `present.frag`'s dither and placement - not built.

## Step 1, as built (2026-10-20) - the texture pack

**What it is.** `[Remaster] texturepack = DIR` or `--texture-pack DIR` on
`omk-play` / `omk-play-gles` (the scene viewer takes the flag too). OFF unless
named; `all = max` never touches it. Vulkan and GLES draw it; the software
reference, GL1 and the 3DS never consult it. A pack is a directory of
`<key>.png` (RGBA, the original's size times a power of two each way), so
any one texture can be opened and fixed in an image editor.

**The pieces.**

| file | what |
|---|---|
| `engine/src/o3de/texpack.{h,cpp}` | the key (FNV-1a 64 over size, indices, 768 palette bytes), the pack index, `openTexturePack` |
| `engine/src/platform/pngread.{h,cpp}` | PNG + inflate, written here (no dependency, PORTING A8) |
| `backends/vulkan` | `Tex::su/sv/packed`; push fields `texScaled`/`texScaleU`/`texScaleV` at 104..112 (the block 104 -> 116 bytes); `scene.frag` divides by the original's size only when `texScaled` |
| `backends/gles` | a replacement uploaded at its size, `texSize` kept the original's |
| `engine/tools/texpack_tool.cpp` | `keys` (the engine's keys) and `png` (the engine's reader, for the Python tool) |
| `tools/omktexpack.py` | `export` / `upscale` / `build` - see its docstring |
| `verify.py: engine: texture pack` | the check, `--slow`, shown to fail five ways |

**How to make and use a pack** (the M3; ~1 h 10 for everything):

```bash
P=~/Documents/omk/omk-pack                       # never inside the repo - refused
python3 tools/omktexpack.py export  $P/full      # 1255 unique textures, ~2 min
python3 tools/omktexpack.py upscale $P/full --bin $P/realesrgan-dl/x/realesrgan-ncnn-vulkan
python3 tools/omktexpack.py build   $P/full      # anchored (the default), ~25 min
engine/build/omk-play-gles ../fr ../tables --save ../traces/save-appart.bin --area 0 \
    --stand 1804,0,-6890,336 --texture-pack $P/full/pack
```

`--match DECORS/Aapkayl.3DO` limits any step to some models. Real-ESRGAN is
the official `realesrgan-ncnn-vulkan-20220424-macos` release (universal,
arm64; sha256 `e0ad0558...cc333a`), ~2 s an image on the M3 GPU.

**What the pilot showed** (Kay'l's apartment `Aapkayl` + the street set
`Anekbah`, 41 textures, all 256x256 -> 1024x1024; judged by eye on
`run_vulkan` frames at 1200x800, trilinear + 16x):

* **Raw Real-ESRGAN DENOISES the art.** `realesrgan-x4plus` sharpens edges
  (the pillars, the sofa's folds, the sign lettering, the cutout triangles)
  but wipes the painted GRAIN of floors and ceilings flat - a plastic look,
  away from the original. `realesrgan-x4plus-anime` is flatter still and
  posterises. Mixing a bilinear enlargement back in (`--grain F`) only
  muddies it.
* **ANCHORING fixes it, and is the default.** Back-projection: the upscaled
  image is corrected so its 4x4 box average is the SHIPPED texel again
  (`omktexpack.anchor`). The grain and the colours are the artists' at their
  own scale; the upscaler contributes only what lies between texels - the
  edges. In the comparison it kept x4plus's sharper sofa and panel edges and
  the original's floor and ceiling. `--no-anchor` keeps the raw picture.
* **The game's own lettering survives** (the hospital sign, the vertical
  banners): crisper, not garbled - at this distance. Close up is not yet
  judged.
* Sizes: 1.75 MB a texture on disk (the pilot 72 MB; a full pack ~2.2 GB);
  in VRAM 4 MB a texture + mips, ~200 MB for the street's pool of ~40.

**Measured, the guarantees.** With no pack, every frame tried is
byte-identical to the build before the feature: Vulkan `run_vulkan` on
Aapkayl and ABar06 under nearest / bilinear / trilinear + 16x / 4x MSAA, the
Vulkan scene viewer on Anekbah camera 3, and the 45-frame street dump on
GLES. Under the nearest sampler a pack of every texture at 4x nearest draws
byte-identical to none - on the street, all 1255 textures, 31 then 39 of the
pool replaced.

**The full pack** (2026-10-20, M3): all 1255 textures exported, upscaled
(x4plus, ~2 s an image, 1916 images with the masks) and built anchored in
about 1 h 10; 2.1 GB at `~/Documents/omk/omk-pack/full/pack`. On the street
from the apartment save every texture of both pools is replaced (31 and 39),
none refused, and at 800x600 the picture is the original's - same colours,
same mood - a little crisper on the ground and the clothes; the gain is for
higher resolutions, where the 256x256 originals are magnified.

**On Vulkan in the game session** (`--world-vulkan`, the offscreen path a
headless run can take - a dummy-driver `omk-play` otherwise falls back to the
software reference): the same street, both pools replaced (31 and 39), the 4x
nearest pack byte-identical to none; the full pack's frame against GLES's
differs in 246 of 480000 pixels by more than ~16 levels, and with NO pack the
two backends already differ in 245 (the fire's additive glow among them) - the
backends' own difference, not the pack's. The windowed Vulkan present was not
run headless.

**Open, in the order I would take them:**

1. **A person's look in play** - the street and an interior at 1080p+ with
   the full pack, and the close-up of lettering. The pilot was judged on
   static frames only.
2. ~~**Decode cost at a pool change**~~ - **DONE 2026-10-20**: a pool's
   replacements are decoded together on the shared thread pool
   (`TexturePack::replacements`); the street's uploads with the full pack
   (GLES, M3) went 633.7 -> **145.7 ms** and 129.6 -> **34.0 ms** (2.4 / 0.7
   without a pack), the frame byte-identical. What is left is the inflate
   itself, ~20 ms a 1024x1024 texture on one core; keeping decoded copies
   would trade 4 MB a texture of memory for it.
3. ~~**The greyscale bank**~~ - **DONE 2026-10-20**: the viewer greys the
   pool's PALETTES (`playframe_world_scene.cpp`), which made every key
   another, so a black-and-white cutscene drew the shipped textures. Now the
   viewer tells the pack where each grey texture came from (`aliasGrey`) and
   the pack greys the colour replacement with the engine's luma, keyed where
   the grey is 0 as the grey palette keys it. Area 237 with `--op-at 30:150`:
   the grey pool replaced 34 of 36 on Vulkan and GLES (1 without the alias),
   the nearest pack byte-identical to none, the full pack 0 colourful pixels;
   `engine: texture pack` part 6.
4. ~~**Textures not under MESHES**~~ - **DONE 2026-10-20**: `export` also
   walks the 26 effect sprites stored whole inside the .SCX files
   (`sprite_fx.library()`, `SCX:<name>` in the index, `--match SCX:`) - 6
   unique atlases, 2 of them also under MESHES. Area 237: 36 of 36 replaced
   (34 before), the nearest pack still byte-identical to none. Added to the
   full pack (1259 files). Judged on the atlases only: shapes and colours
   kept; the sprites draw ADDITIVELY, so the near-black rims a cutout view
   shows add nothing - to confirm in play on a fire or an explosion.
5. Curation: an exclusion list for anything the upscaler hurts, and B3's
   material maps from the same images.

The full-sweep red seen on the way, NOT this change's: `engine: release
build`'s second field (the development build's `OMKSNAP1` marker) is 0 in the
build from before this feature too.
