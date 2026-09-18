# 8. Rendering

← [Contents](README.md) · prev: [Conversations and cutscenes](07-conversations-and-cutscenes.md) · next: [Audio](09-audio.md)

---

## In short

The original draws through Direct3D in its 1999 fixed-function form: it hands
the driver a stream of already-transformed, already-lit vertices and a texture,
and the driver fills triangles. There are no shaders and no lighting equations
at run time for a set. A set is *pre-lit* — every vertex carries a colour baked
into the file — and the moving population is lit at run time only by a small
table of lights that ships inside the set's model.

It is also a very particular picture. The engine turns anti-aliasing **off**,
samples its textures **point** with no mipmaps, and turns the driver's
**dither** on. A character's shadow is not a shadow pass but one soft blob,
copied under a handful of bones. A city's far skyline shimmers, because a mesh
flag oscillates its vertex colour on the frame clock.

The interface is a separate world: a 16-layer display list of rectangles,
sprites and text, blitted with a colour key. A blit is a memory copy, which is
why the 2D half of this chapter can be reproduced pixel for pixel and the 3D
half cannot. The framebuffer is **16-bit RGB565**, and that decides what a
comparison against a captured frame is even allowed to mean.

The port draws through one boundary with several implementations behind it — a
software rasterizer that the checks measure, a Vulkan backend that makes it
playable, and a GLES2 one begun for a handheld. The boundary is at the level of
**decisions**, not API calls, which is what makes more than one backend
possible. Everything the original did not do — anti-aliasing, filtering, real
shadow maps, per-pixel light — exists only as an **enhancement**, off by
default.

## In detail

### What is ported is decisions

The interface between the engine and a backend carries four things:

```
begin(view)                        the camera, its frustum, its fog
submit(draw)                       {bucketKey, mesh, range, blend, cutout}
submit2d(I2dList)                  the display list, already ordered
end() -> Frame                     RGB565, 640x480
```

A backend receives decisions and turns them into API calls; it never makes one.
The decisions themselves are the port:

* **the drawable mask** — one test, `flags & 0x800043`, which replaced three
  separate viewer heuristics and disagrees with them in both directions;
* **the 14-bit bucket key** that orders every draw;
* **the texture slot as that key's low six bits** — the texture is the low six
  bits of the key and nothing else;
* **two blend modes** — `0x1000|0x2000` additive (211 meshes) and
  `0x1000|0x4000` multiply (6) — with `0x800` a separate cutout path;
* **the 58-slot texture cache**, matched on a 19-character name;
* **the visible-set walk** against the engine's own frustum.

### The render states the original sets

`sub_4638C0` is where the device is configured, and reading it settled several
questions at once: **anti-aliasing explicitly off**, textures sampled **point**
with **no mipmaps**, perspective-correct, Gouraud, specular on — and
`DITHERENABLE` **on**, on both device arms.

The first two make anti-aliasing and texture filtering **enhancements**: a
replica judged against the original must draw what it drew unless told
otherwise. The third made the dither a **fidelity fix**: the port had been
drawing hard 565 bands where the original drew dithered noise. What is ported
is the *decision*; the 4 × 4 matrix is the driver's, not the engine's, and is a
labelled reconstruction.

The same shape of defect turned up once more. Mesh flag `0x8000000` oscillates
the vertex colour of **233 set meshes** — the far skyline of every city, 132 of
them in Lahoreh — through a 32-byte signed table indexed by the frame clock and
the vertex's own phase. The port had decoded the phase when the geometry loader
was written and read it nowhere. Both are now **on by default**, and a check
asserts the shimmer table's three copies (the header, the shader's, and the
reference) against the bytes in the executable, because a table duplicated for
a shader is a table that drifts.

**Before building an enhancement, check whether the game already does the
thing and the port dropped it.** That is cheaper than any enhancement, and it
is what the original looks like.

### RGB565, and how a comparison has to be done

Measured from a captured frame: the red channel carries 32 distinct levels,
green 63, blue 25 of a possible 32, with gaps of 8 and 9 in the 5-bit channels
and 4 and 5 in the 6-bit one — the signature of a 5/6-bit value expanded by bit
replication.

So comparisons are made **in 565, not in 888**. A captured frame's 8-bit values
are the *host's* expansion, not the game's data. Expanding the reference up to
888 matches the menu's title region 94.9%, every difference a rounding artefact
of something that is not the game; quantising the capture back down to 565
matches **66 560 of 66 560**. Bring the capture into the framebuffer's space,
never the other way.

### The set is lit before it ships

A set's vertices carry a **colour**, not a brightness — the engine copies the
whole dword at vertex `+28` into the vertex it hands the driver, and declares
`D3DFVF_DIFFUSE` in both its draw calls. Reading only the green byte renders
every set in monochrome; 38.9% of set vertices are not grey.

The engine *does* contain a luma conversion, and it is in the **second render
bank** — the same scene walk with every vertex colour converted to grey. VM
opcode 150 installs it at 14 shipped sites against 15 restores, and 74 of the 82
instructions between them are camera and fade opcodes. The game has
black-and-white cutscenes.

### The lights inside a model

The 1999 press sheet advertised "Multilights", and the table is real: **4 179
records of 304 bytes across 216 models**, each naming itself `LIGHT` and
carrying two radii in round metres, an RGB colour, a position and a footprint.
The count is at `desc+240`, not the `+232` a first reading used — the loader
overwrites `+232` from it.

A decor supplies them and the **street's moving population receives them**:
every call site of the lighting function is street life. Per vertex it is
`−(N·L)` over a linear falloff through a fixed ramp, which needed the vertex
**normal** at `.3DO` vertex `+12`. A lit body starts from black; a body the
engine never lights keeps its baked shading.

### The shadows

There is **no shadow pass**. The engine has one shipped quad,
`MESHES\MISC\shadows.3DO` — a soft white disc on black in the *multiply*
bucket — and `Actor_DrawShadow` writes copies of it into the frame's own pools
under a fixed set of **bones** each frame. The options menu's detail row says
how many (arms at 2, head and legs at 1, the chest always; an NPC gets one level
less), the reaches are 3.6, 1.8 and 1.4 round metres, the size is the bone's own
bounding sphere over 10, 12 or 14 clamped at 1.5, and the blob fades by its
vertex colour as the bone rises. The crowd's shadow is a separate node at the
midpoint of the two feet.

The trap is the lookup: **bones are found by `strstr`, keeping the last match**,
because every character bone carries a prefix. An equality test draws nothing
at all. And a crowd model carries **four LOD skeletons side by side**, so every
bone name matches four times and the last-match rule lands on the lowest-detail
one six metres away — the port drew blobs sliding across a street with nothing
above them until the lookup was scoped to the skeleton the animation names.

### What one option sizes

The graphical options are not decoration. The clip distance — quoted in metres
against an inch world unit — sizes **four things at once**: the visible-set
radius, both bucket splits, and the fog range. The fog is **linear**, starts at
a quarter of the clip distance and ends exactly at it, and its colour ships as
**black**, so it darkens toward the horizon rather than hazing.

The sky is a flat painted **ceiling**, not a dome, following the camera in x
and z — and there is only one, whichever resident area named it last.

### The mirrors

Six meshes in the whole game carry the mirror flag, one is live at a time, and
the engine reflects the camera through the mirror's plane and **draws the whole
scene again** with screen X flipped — a second full pass, gated on the display
driver, so hardware mode only.

Two parts of the port's version stay reconstruction and say so: how the engine
confines the reflection to the mirror's area and the plane's normal. Both were
confirmed by **flying a camera across viewpoints** rather than by a metric,
which is the only thing that can settle a plane, a normal's sign or a flip.

### The texture cache, and a flicker that was the port's

The cache hands out 58 slots from a **global** pool, matching on the texture's
19-character name alone; on a hit it points the material at whatever is already
there. Two decor sets are resident at once — hidden is not unloaded — so an
arriving set cache-hits against the one you walked out of. **182 texture names
ship with different pixels in different model files**, and rendered with only
the resident neighbour changing, one neighbour moves 6 920 pixels of an
Anekbah frame and another **121 588**. The location draws differently depending
on where you walked in from — in the original too.

The **flicker** a reader reported beside those panels on the first day of play
had no account for eleven days, and both candidate explanations were measured and
refuted. It turned out to be the port's. The coincident shop-sign pairs are the
two **sides** of a sign — the same four vertices in opposite winding — and the
engine's strict depth test on a quantised z-buffer shows whichever was drawn
first. The port's float comparison let last-bit noise between the two
triangulations pick the face pixel by pixel: dots of the other advert, re-rolled
by every movement of the camera. A tie band of 2⁻¹⁶ closed it.

### The 2D layer

A 16-layer display list with seven primitives and their own pools, ending in
`IDirectDrawSurface::Blt` with a colour key. The per-layer cache is a **head**
cache, so within one layer the rest draw in **reverse** submission order; and
the blits do not test the source Y. Two pool submitters once recorded as
uncalled turned out to be called from draw hooks that are table dwords — the
save thumbnail and the monitors' interference.

Because a blit is a memory copy with no filtering, this half is exactly
reproducible, and it is: the menu's deterministic region comes out **66 560 of
66 560** pixels identical to the engine's own framebuffer, the load panel's
selection box 1 518 of 1 518, and the ported text 6 132 of 6 132.

<p align="center">
  <img src="images/menu-text-port.png" width="440" alt="The start menu's labels drawn by the port">
  <br><em>The four start-menu labels, drawn by the ported text renderer out of the<br>game's own fonts at the coordinates its widget tree gives them.</em>
</p>

### The 3D path, and what a captured frame can prove

A captured 3D frame is exact about **geometry and ordering** and not about a
pixel's low bits — filtering, dithering and the fog table belong to the driver,
and the capture was taken under Wine rather than on a Voodoo. Between two
captures of the same parked scene, 42% of pixels differ by ≤ 8.

So the criterion is **silhouette and coverage**, and per-pixel equality is
explicitly not claimed. Rendered through one conversation's camera, the set
scores 0.73 / 0.83 against the two parked captures on a chance floor of 0.27 /
0.30, and 92% / 99% of the holes a set-only render leaves fall where the capture
is black, against 33% frame-wide.

<p align="center">
  <img src="images/dlg402-port-render.png" width="440" alt="The port's render of the apartment through camera 4555">
  <br><em>The same set through the same camera as <code>traces/frames/dlg402-47.png</code>.<br>The capture also carries a character, the props and a subtitle; the render<br>draws the set alone, which is the asymmetry the metric is built around.</em>
</p>

**And that tier covers one camera in one set.** Within hours of it being
reached, a player flying the free-look viewer found that the rasterizer had no
near-plane clipping at all. Through that one camera the fix changes **0
pixels**; one step into the room it changes 12 710.

### The enhancements, and the backends

Everything the original did not do is **off by default** and says so:
multisample anti-aliasing, bilinear and trilinear filtering with anisotropy,
**fitted** shadows (the blob laid over the ground under each of its vertices),
**mapped** shadows (a real 1024 × 1024 shadow map from the set's own lights —
the first render pass the port has that the original never had), per-pixel
lighting by the engine's own law, supersampling, an unlimited clip distance and
interface scaling. One switch turns them all to their top. The renderer
boundary grew for them by two fields and one virtual whose default is a no-op,
so the software reference still draws exactly what the original drew.

The **Vulkan** backend presents directly and is explicitly unverifiable — its
correctness is inherited from the software backend it mirrors. A **GLES2**
backend was begun on 2026-09-18 for the PS Vita and has drawn the Anekbah
street start on a Mac at 0.99 coverage against the software reference; it is
that session's work in progress.

Speed was worked on with the Vita as the yardstick: on an M1, capped at 30 fps
on the Anekbah street, the main thread does about **6.0 ms** of work a frame
after a series of measured steps (grids for the ground probe and the sweeps, a
hashed depth-tie table, shared texture pixels). The decision recorded from it
is that a 30 fps Vita build doing this CPU work on one core is not in reach by
a factor of several.

## Where it lives

| | |
|---|---|
| the findings | `docs/ASSETS.md` §4 (the render path, the lights, the shadows, the fog), `docs/PORTING.md` A2–A4 |
| the boundary | `engine/src/o3de/renderer.h` |
| the backends | `engine/src/o3de/raster.*` (software), `engine/backends/vulkan/`, `engine/backends/gles/` |
| the 2D layer | `engine/src/ui/i2d.*`, `surface.*` |
| the enhancement list | `todo/enhancements.md`; the performance record, `todo/optimization.md` |
| the checks | `engine: silhouette`, `engine: raster`, `engine: near clip`, `drawable mask`, `render bucket key`, `texture name cache`, `engine: sign tie`, `mirror pass`, `engine: I2D blit`, `engine: dither`, `shimmer table`, `engine: character shadow`, `engine: mapped shadows` |

## What is not settled

* **No pixel's *value* has a reachable tier in 3D.** Filtering, dither, the fog
  arithmetic and the blend maths are the driver's, and no rig here can
  distinguish them. The dither matrix is a labelled reconstruction.
* **A latent backend disagreement is recorded, not fixed**: undithered, the
  Vulkan readback truncates to 565 where the software path rounds, so the
  boundary that should move 0 pixels has moved some.
* **The mirror's confinement and its plane normal** are reconstruction.
* **Four of the second render bank's six swapped pointers** are unread.
* **The lift's dark arrival.** At the security centre's levels the arrival
  camera sits inside the lift car's own mesh, and the frame is almost black;
  displacing that one mesh takes it from 96.7% dark to 16.9%. What the
  original does about the car is open — it is not the obstruction pass, which
  no absolute camera takes.
* **No capture in this tree shows a shipped shadow**, so the blob's size is
  data-constrained.
