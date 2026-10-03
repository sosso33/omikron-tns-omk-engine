<h1 class="part" id="part-iii">Part III — Pictures and sound</h1>

<div class="pagebreak"></div>

# 8. Drawing a frame in 1999

## Fixed function, and the CPU does the geometry

If you learned OpenGL after 2008, the first surprise is how little the GPU
did. The original draws through **Direct3D in its 1999 fixed-function form**.
It hands the driver vertices that are **already transformed and already lit**
(`D3DTLVERTEX`: a screen position, a depth, a reciprocal *w*, a colour and a
texture coordinate), plus a texture, and the driver fills triangles. There
are no shaders. Every vertex of every character is posed and projected **by
the CPU**, on a Pentium II.

> **From OpenGL:** imagine a vertex shader that does nothing but pass through
> `gl_Position` computed on the CPU, and a fragment shader that does
> `texture * colour`. The lighting, the skinning and the projection all
> happened before the driver saw the data.

## Which meshes are drawn

A set is a few thousand meshes. Each carries a word of flags, and the engine's
decision "is this mesh drawable?" is a single test:

```c
if (mesh.flags & 0x800043) skip;
```

That one mask replaced three heuristics the project's own viewers had used
before the engine's rule was read, and it disagreed with them in both
directions. It is a good example of why reading the code beats reasoning
about the data.

Then a **visible-set walk** culls meshes against the camera's frustum. It
culls whole meshes, not triangles, which is cheap and matches what the
original does.

## The draw order is a number

This is the mechanism that decides almost everything about the picture.

Every triangle that survives is put into one of **16 384 buckets**, chosen by
a **14-bit key**. The key's low six bits are the **texture slot**, and nothing
else chooses the texture. The bits above come from the mesh's flags: blended
or not, cutout or not, and so on. At the end of the frame, `Render_FlushBuckets`
walks the keys from 0 to 16 383 in **ascending** order and draws each non-empty
bucket.

![The 14-bit bucket key.](figures/fig07-keys.svg)
<p class="caption">Figure 7 — The draw order is the bucket key, ascending.</p>

Three consequences follow from that one design.

* **State changes are cheap.** All the triangles sharing a blend mode and a
  texture are drawn together. Nobody had to write a state cache: the order
  itself minimises the changes.
* **Transparency works.** Blended geometry sets a high key bit, lands at the
  top of the range, and is drawn after everything opaque.
* **Coincident faces are settled.** When two faces occupy exactly the same
  place, the one with the lower key is drawn first. The depth test is strict
  (a pixel is replaced only by something strictly nearer), so the first-drawn
  face keeps every pixel. The shop signs in Anekbah are built exactly like
  that: the two sides of a sign are the same four vertices wound the other
  way, each with its own advert, and the engine always shows the one drawn
  first. Chapter 14 comes back to this, because it turned out to be one of
  the hardest things for a modern GPU to reproduce.

> **From Unity:** the bucket key is a render queue plus a sort key. It is
> roughly what URP does when it sorts opaque objects by material to batch
> them, and draws transparent ones afterwards, except that here the sort is a
> fixed integer order with no camera-distance sort at all.

## Textures: 58 slots

Textures are paletted: each texel is an 8-bit index into a 256-colour palette.
The loader hands out **58** texture slots from one global pool, matching a
texture by its **file name** alone, and on a match reuses what is already in
memory. That is the cache chapter 5 described, whose side effect is that a
set can draw its neighbour's version of a texture.

## Colour is baked

A set is **pre-lit**. Every vertex carries a colour baked into the file, and
that colour is what reaches the screen: the driver is told the vertex has a
diffuse colour, so it is a colour, not a brightness. (The project's early
viewers read only the green byte, which drew every set in grey. 39% of set
vertices are not grey.)

The moving population, the crowd and the characters, is lit at run time,
per vertex, by a small table of **lights** that ships inside each set's model:
4 179 of them across 216 models, each with two radii, a colour and a position.
A vertex takes `-(N·L)` through a linear falloff and an integer colour ramp.

## The picture's particular look

The engine sets its render states deliberately, and they are part of the
game's look:

* anti-aliasing **off**;
* textures filtered **bilinear** on a 3D card, sampled **point** on the
  software devices, and **no mipmaps** on either;
* the driver's **dither on**;
* fog **linear**, and **black**;
* the sky a **flat painted ceiling** that follows the camera, not a dome;
* a far skyline that **shimmers**: a 32-byte table oscillates the vertex
  colour of 233 set meshes on the frame clock.

A replica has to do all of them, including the dither and the shimmer, which
are easy to leave out because they look like defects. They are the original.

## Shadows are blobs

A character's shadow is not a shadow pass. It is **one soft quad**, copied
under a handful of bones (the chest always, the head and legs from detail
level 1, the arms at level 2) and faded by distance to the floor below each
bone. The floor is found by probing straight down, the same kind of probe the
walker uses.

## Mirrors are a second pass

Six meshes in the whole game are mirrors. When one is visible, the engine
**reflects the camera** through the mirror's plane and draws the whole scene a
second time, with the screen flipped horizontally. It is the most expensive
thing the renderer ever does, and it is used sparingly.

## The interface is a different world

The 2D interface does not go through the 3D path at all. It is a **16-layer
display list** of rectangles, sprites and text, blitted with a colour key.
A blit is a memory copy. That is why the port can reproduce the interface
**pixel for pixel**, while it cannot do that for the 3D half (Part IV explains
what it does instead).

## A captured frame, and the port's

![The apartment through dialogue 402's camera, captured from the original.](../traces/frames/dlg402-47.png)
<p class="caption">The original engine's framebuffer: Kay'l's apartment through dialogue 402's camera, letterboxed, with Telis and a subtitle (<code>traces/frames/dlg402-47.png</code>).</p>

![The same set through the same camera, drawn by the port's software rasterizer.](../manual/images/dlg402-port-render.png)
<p class="caption">The same set through the same camera, drawn by the port's software rasterizer, without the characters and the text (<code>manual/images/dlg402-port-render.png</code>). This pair is how the 3D path is judged: by edges and coverage against a chance floor, not pixel by pixel (chapter 11).</p>

## RGB565

The framebuffer is **16-bit**, five bits of red, six of green, five of blue.
That shapes what a comparison against a captured frame can mean. A capture
comes back as 8-bit values, but those are the host's expansion of the
original's 16-bit pixels, not the game's data. So every comparison in this
project is made **in 565**.

<div class="pagebreak"></div>

# 9. Sound without a mixer

## The mixer that isn't there

The most useful thing to know about the game's audio is that **there is no
mixer in it**. The engine opens one DirectSound primary buffer, sets it to
22 050 Hz stereo, and starts it looping. After that, every sound is a
*secondary* buffer that DirectSound itself sums. Nothing in the executable
ever adds two samples together.

What the engine does is **decide**: which of its 160 buffers to use, which of
its 16 voices plays it, where it is in the world, how loud it is, when it
stops. The listener is told that the world's unit is an **inch**. The volume
law is an **attenuation**, 0 for full and 100 for silent.

That changes what a port can claim. The decisions are portable and checkable.
The sound that comes out is the operating system's, and nothing in this
project records audio, so the exact attenuation curve and panning have **no
reachable evidence at all**. They are implemented as honest approximations and
written down as unverifiable, not presented as established.

## One decoder, exact

Speech, music and the facial animation's audio are stored in a variant of
**IMA ADPCM**: four bits a sample, each nibble adjusting a predictor by a step
that grows or shrinks. It differs from the textbook version in two ways, and
both matter: the high nibble is decoded first, and the textbook's small
rounding term is absent. Leave the term in and the signal drifts by thousands
of units over a line.

The port's decoder is **sample-identical** to an independent one across all
777 files that carry a voice, 225 million samples. In 2026-09 it also became a
**table**: a channel's state is just its predictor and its index, so each
nibble's effect can be precomputed, 89 × 16 entries. It was proven identical
over all 93 million states a channel can be in.

## Music as a stream

A music track is a stream of stereo ADPCM at 22 050 Hz. The port first decoded
a whole track when it started playing: simple, and 16 MB of memory per track.
That was fine on a Mac and was refused by the PS Vita's heap the moment a city
loaded. So the port now decodes **as it plays**, from the open file, keeping
about a second queued in the device and topping it up a quarter of a second at
a time.

<div class="pagebreak"></div>

# 10. The interface

## Screens are data, mostly

The game's menus are data too. There are **37 screens**: the start menu, the
options, the save and load panels, the shops, the terminals, the lift, a few
puzzles, and the sneak. Each is a row in a table compiled into the executable,
naming its background bitmap, its text file, and the callbacks that open and
close it.

What a screen contains is a **tree**: a screen owns a panel, a panel owns up
to ten lists, a list owns items, and an item is 72 bytes saying where it sits,
what it draws and which flags it has.

> **From Unity:** a panel is a Canvas, a list is a layout group, an item is a
> UI element, and the "button hook" is an `onClick` pointing at native code.
> The difference is that the whole tree lives in the executable's data
> section, so it had to be lifted out to JSON: no data file carries it.

## The code a table points at

What makes a screen *work* is not in the tree but in small native functions it
points at: a list's input hook, an item's draw hook, the callback a button
press runs. Many of them are **called from nowhere but a table**. The
disassembler that produced this project's listing only recognises code where
it sees a call, so 26 of the 30 per-screen open and close callbacks were not
recognised as functions at all. Each screen the port brought up meant finding
them in the raw image first. Part IV comes back to that trap.

## Text

Text is drawn with the game's own **13 fonts** (2 899 glyphs, each pixel a
coverage value 0..31 into a colour ramp), laid out by the engine's own block
layout, with a small markup language for colour, font and images. The start
menu in the game's invented alphabet is drawn by the port's text code, from
the same fonts, and matches the original's framebuffer pixel for pixel.

![The start menu's labels, drawn by the port.](../manual/images/menu-text-port.png)
<p class="caption">The start menu's four labels drawn by the port from the game's own fonts: 6 132 pixels, each the same as in the original's capture shown in chapter 2 (<code>manual/images/menu-text-port.png</code>).</p>

## Input is shared

The interface reads the same **14-bit input word** as the bodies do (chapter
6). A menu and a character are listening to the same word. Confirm, back and
close are three of its bits.

