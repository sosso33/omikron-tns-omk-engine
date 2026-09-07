# 8. Rendering

← [Contents](README.md) · prev: [Conversations and cutscenes](07-conversations-and-cutscenes.md) · next: [Audio](09-audio.md)

---

## In short

The original draws through Direct3D in its 1999 fixed-function form: it hands
the driver a stream of already-transformed, already-lit vertices and a texture,
and the driver fills triangles. There are no shaders, no lighting equations at
run time and no depth of field. A set is *pre-lit* — every vertex carries a
colour baked into the file — and a character is lit at run time only by a small
table of lights that also ships inside the model.

The interface is a separate world: a 16-layer display list of rectangles,
sprites and text, blitted with a colour key. A blit is a memory copy, which is
why the 2D half of this chapter can be reproduced pixel for pixel and the 3D
half cannot.

The framebuffer is **16-bit RGB565**, and that is not a detail: it decides what
a comparison against a captured frame is even allowed to mean.

The port draws through one boundary with two implementations behind it — a
software rasterizer that the checks measure, and a Vulkan backend that makes it
playable. The boundary is at the level of **decisions**, not API calls, which is
what makes two backends possible at all.

## In detail

### What is ported is decisions

The interface between the engine and a backend carries four things:

```
begin(view)                        the camera and its frustum
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

### RGB565, and how a comparison has to be done

Measured from a captured frame: the red channel carries 32 distinct levels,
green 63, blue 25 of a possible 32, with gaps of 8 and 9 in the 5-bit channels
and 4 and 5 in the 6-bit one — the signature of a 5/6-bit value expanded by bit
replication.

So comparisons are made **in 565, not in 888**. A captured frame's 8-bit values
are the *host's* expansion, not the game's data, and the host does not expand
the way you would guess. Expanding the reference up to 888 matches the menu's
title region 94.9%, every difference a rounding artefact of something that is
not the game; quantising the capture back down to 565 matches **66 560 of
66 560**. Bring the capture into the framebuffer's space, never the other way.

### The set is lit before it ships

A set's vertices carry a **colour**, not a brightness — the engine copies the
whole dword at vertex `+28` into the vertex it hands the driver, and declares
`D3DFVF_DIFFUSE` in both its draw calls. Reading only the green byte, which is
what one importer does, renders every set in monochrome; 38.9% of set vertices
are not grey.

The engine *does* contain a luma conversion, and it is in the **second render
bank** — the same 660-line scene walk with every vertex colour converted to
grey. VM opcode 150 installs it at 14 shipped sites against 15 restores, and 74
of the 82 instructions between them are camera and fade opcodes. The game has
black-and-white cutscenes.

### The lights inside a model

The 1999 press sheet advertised "Multilights", and the table is real: **4 179
records of 304 bytes across 216 models**, each naming itself `LIGHT` and
carrying two radii in round metres, an RGB colour, a position and a footprint.
The count is at `desc+240`, not the `+232` a first reading used — the loader
overwrites `+232` from it.

They are not for the set, which is pre-lit. A decor supplies them and the
**street's moving population receives them**: every call site of the lighting
function is street life. Per vertex it is `−(N·L)` over a linear falloff
through a fixed ramp — which needed the vertex **normal** at `.3DO` vertex
`+12`, twelve bytes this project had skipped since the format was first
decoded.

### What one option sizes

The graphical options are not decoration. The clip distance — quoted in metres
against an inch world unit — sizes **four things at once**: the visible-set
radius, both bucket splits, and the fog range. The fog is **linear**, starts at
a quarter of the clip distance and ends exactly at it, and its colour ships as
**black**, so it darkens toward the horizon rather than hazing — which is what
hides the hard edge at the clip distance in a domed city at night.

The sky is a flat painted **ceiling**, not a dome, following the camera in x
and z.

### The mirrors

Six meshes in the whole game carry the mirror flag, one is live at a time, and
the engine reflects the camera through the mirror's plane and **draws the whole
scene again** with screen X flipped — a second full pass, gated on the display
driver, so hardware mode only.

Two parts of the port's version stay reconstruction and say so: how the engine
confines the reflection to the mirror's area (its flip is global, and no clip or
stencil step was traced) and the plane's normal (the engine reads a runtime
value; the port takes the face's cross product). Both were confirmed by
**flying a camera across viewpoints** rather than by a metric — which is the
only thing that can settle a plane, a normal's sign or a flip, since each looks
plausible in any single still frame.

### The texture cache, and a prediction that came true

The cache hands out 58 slots from a **global** pool, matching on the texture's
19-character name alone; on a hit it points the material at whatever is already
there. And two decor sets are resident at once — hidden is not unloaded — so an
arriving set cache-hits against the one you walked out of.

**182 texture names ship with different pixels in different model files.** So
the same panel in the same street should look different depending on which
location you walked in from. That claim stood for three days as an argument
about atlases, and then the port could draw it: same set, same camera, only the
resident neighbour changing — one neighbour substitutes 7 atlases and moves
6 920 pixels, another substitutes 18 and moves **121 588**. A quarter of the
frame repaints depending on where you came from.

### The 2D layer

A 16-layer display list with seven primitives and their own pools, ending in
`IDirectDrawSurface::Blt` with a colour key. Two details decide what it draws:
the per-layer cache is a **head** cache, so within one layer the rest draw in
**reverse** submission order; and the blits do not test the source Y.

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
is black, against 33% frame-wide. Mid-sweep captures of the same set with the
camera elsewhere reach 0.14–0.38; a mirrored reading lands *below* its own
floor.

<p align="center">
  <img src="images/dlg402-port-render.png" width="440" alt="The port's render of the apartment through camera 4555">
  <br><em>The same set through the same camera as <code>traces/frames/dlg402-47.png</code>.<br>The capture also carries a character, the props and a subtitle; the render<br>draws the set alone, which is the asymmetry the metric is built around.</em>
</p>

**And that tier covers one camera in one set.** Within hours of it being
reached, a player flying the free-look viewer found that the rasterizer had no
near-plane clipping at all — a triangle with any vertex behind the cut was
dropped whole, so a floor with one corner behind you vanished while still in
shot. Through that one camera the fix changes **0 pixels**, because every
triangle it rescues is off the edge anyway. One step into the room it changes
12 710, and closes a 10 878-pixel hole in the floor.

## Where it lives

| | |
|---|---|
| the findings | `docs/ASSETS.md` §4 (the render path, the lights, the fog), `docs/PORTING.md` A2–A4 |
| the boundary | `engine/src/o3de/renderer.h` |
| the backends | `engine/src/o3de/raster.*` (software), `engine/backends/vulkan/` |
| the 2D layer | `engine/src/ui/i2d.*`, `surface.*` |
| the checks | `engine: silhouette`, `engine: raster`, `engine near clip`, `drawable mask`, `render bucket key`, `texture name cache`, `anekbah rendered`, `mirror pass`, `engine I2D blit` |

## What is not settled

* **No pixel's *value* has a reachable tier in 3D.** Filtering, dither, the fog
  arithmetic and the blend maths are the driver's, and no rig here can
  distinguish them.
* **The Vulkan backend is explicitly unverifiable.** Its correctness is
  inherited from the software backend it mirrors, and it says so in its own
  header.
* **The mirror's confinement and its plane normal** are reconstruction,
  labelled as such.
* **Four of the second render bank's six swapped pointers** are unread, and no
  capture distinguishes them.
* **One Anekbah panel flickers** and has no account at all: the two candidate
  explanations — coincident faces z-fighting, and a flickering neon emitter —
  are both measured and both refuted.
