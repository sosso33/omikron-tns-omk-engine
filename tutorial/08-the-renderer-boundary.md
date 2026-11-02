# Lesson 8 — The renderer boundary

**Goal:** know where the engine stops deciding and a renderer starts drawing,
render one frame through two renderers and measure the difference honestly,
and change a rule all four GPU backends share.

## Read

* `docs/PORTING.md`, Part A up to and including A2 - what the replica
  compiles against, and why the renderer boundary sits at the level of
  DECISIONS. And Part B's opening: the six tiers of evidence.
* `engine/src/o3de/renderer.h`: `struct View`, `struct Draw` (the comments on
  its fields), and `class Renderer` - `init`, `setTextures`, `begin`,
  `submit`, `end`, `readback`.
* `engine/src/o3de/drawcore.h` - what every GPU backend computes the same
  way, in one page.

## The boundary

The original engine decided everything a frame shows - which meshes, in what
order, in which bucket, with which texture slot, blend mode, fog, light - and
then handed triangles to Direct3D, which only rasterized them. The port
keeps that cut:

* everything up to the cut is the ENGINE's and lives in `src/`: a frame is a
  `View` (the camera, the fog, the dither, the grey bank...) and a list of
  `Draw`s (a geometry, a range of its corners, a blend mode, a bucket key, a
  pose, the lights that reach a body);
* below the cut is a `Renderer`: the software reference (`src/o3de/raster.*`,
  which every check compares against), Vulkan, GLES (the Vita), fixed-function
  GL 1.x (the PowerPC Mac) and citro3d (the 3DS).

The rule that keeps the cut honest: **a backend chooses HOW, never WHAT.**
And where the four GPU backends must agree on arithmetic that is the
engine's - posing a body on the CPU, the per-vertex light law, which of two
coincident faces wins, what a blend mode does - they share it, in
`drawcore.*`, and `verify.py: engine: draw core` holds that one copy against
the reference path.

The remaster is not a fifth backend. Its enhancements - the weather, the
graphics menu, the wet floors - are code of their own (`backends/remaster/`,
and the GLES backend's `backends/gles/glesrender_remaster.inl`), compiled only when
`OMK_REMASTER` is on and stubbed out of the low-end ports
(`backends/sdl/playremaster_off.cpp`); what they add is drawn through the
same boundary.

## Run: one frame, three ways

```
cd engine
B="../gamedata ../tables --res 640x480 --save ../traces/save-appart.bin --area 0 --stand 1804,0,-6890,336 --frames 60 --nofmv --no-crowd"
SDL_VIDEODRIVER=dummy build/omk-play ${=B} --software --dump /tmp/sw.bin > /dev/null
SDL_VIDEODRIVER=dummy build/omk-play ${=B} --software --no-dither --dump /tmp/swnd.bin > /dev/null
SDL_VIDEODRIVER=dummy build/omk-play ${=B} --world-vulkan --dump /tmp/vk.bin > /dev/null
python3 ../tutorial/dump2png.py /tmp/sw.bin
```

`/tmp/sw.png` is Anekbah's main street: Kay'l from behind, a burning
brazier, the street lamps, the signs. `--world-vulkan` draws the world
through the Vulkan backend offscreen and still dumps the frame - the GPU
path, headless. (It needs a Vulkan loader: on a Mac, MoltenVK.)

Now compare them. Save this as `/tmp/cmp.py`:

```python
import sys
a = open(sys.argv[1], 'rb').read(); b = open(sys.argv[2], 'rb').read()
n = len(a) // 2; diff = big = 0
for i in range(n):
    p = a[2*i] | a[2*i+1] << 8; q = b[2*i] | b[2*i+1] << 8
    if p != q:
        diff += 1
        d = max(abs((p >> 11) * 8 - (q >> 11) * 8), abs(((p >> 5) & 63) * 4 - ((q >> 5) & 63) * 4),
                abs((p & 31) * 8 - (q & 31) * 8))
        big += d > 24
print("%d of %d pixels differ, %d by more than 24 levels" % (diff, n, big))
```

```
python3 /tmp/cmp.py /tmp/sw.bin /tmp/swnd.bin
python3 /tmp/cmp.py /tmp/sw.bin /tmp/vk.bin
```

```
129146 of 307200 pixels differ, 240 by more than 24 levels
148845 of 307200 pixels differ, 463 by more than 24 levels
```

The first line is the DITHER alone: the original's devices set
`DITHERENABLE`, and a 16-bit frame dithered moves 42% of its pixels by one
step. The second is software against Vulkan: half the pixels differ, and
almost none by much. Part of it is the texture filter - a 3D card in 1999
filtered bilinear, and the GPU backends do by default, while the software
reference point-samples as the original's software device did. Add
`--filter nearest` to the Vulkan run and the large differences fall from 463
to 168, and the pixels that differ at all from 148845 to 81292 (on the
machine this was written on, an Apple M3).

**So two renderers are never compared pixel for pixel.** Floating-point
rasterizers disagree in the last bit of a colour and on which pixel an edge
covers. The checks compare what can be asserted: coverage (is the same area
drawn), silhouettes against their own chance floor, a bound on the pixels
that differ by a lot (`engine: gl1 backend`: at most 0.5% by more than 24).
2D pixels - the interface, text - ARE exact, and are asserted value for
value.

## Change

The light law every GPU backend uses to colour a lit body on the CPU is
`omk::lightCorner` in `src/o3de/drawcore.h`. Its ramp index is the dot
product of the normal and the light, TRUNCATED to an integer, as the
engine converts it:

```cpp
int ti = static_cast<int>(-(n[0] * L[k].lx + n[1] * L[k].ly + n[2] * L[k].lz));
```

Round it instead: add `+ 0.5f` inside the cast, after the parenthesis.

## Prove

```
cd ..
python3 tools/verify.py --only "engine: draw core"
```

```
engine: draw core    FAIL ((155, ('HO1_FN', 1626, 48, True, 16011, True, 0), ...
```

`engine/tools/drawcore_probe.cpp` poses three real characters 48 times each
under Anekbah's 155 lights, and lights them twice: with the core, and with
`applyLights`, the reference path's own law. 16011 corner-poses now differ
in colour - by a step or two of 255, invisible to the eye, and exactly what
a reader of a frame would never find. The check's second half scans the
four backends for a copy of the law that has crept back in.

Remove the `+ 0.5f`, `touch src/o3de/drawcore.h`, and run it green.

## Traps

* **A device render state is not the engine's behaviour.** The original sets
  `CULLMODE NONE` on the device, and for a month this repository said "the
  engine never culls" - while one level above the device it dropped every
  back face in software. Before concluding from a render state, read what
  reaches the device.
* **A GL run blocks at frame 0 while the display sleeps, and a killed run
  still writes its dump - of frame one.** Read `N frames presented` in the
  log before comparing a GPU dump, and hold the display awake
  (`caffeinate -d -u -i`) for any GL run.
* **A fix found in one backend goes to every backend it affects.** Four GPU
  backends with four limits (the 3DS has 6 MB of video memory, the iBook 8):
  when a fault or an optimisation is found in one, the other three are part
  of the same change.
