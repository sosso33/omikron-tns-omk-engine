# The `.3DO` light table

The 1999 spec sheet's "Multilights" line (`todo/engine-spec-1999.md`) pointed
at a table this repo has parsed the header of since the format was decoded and
never read the records of. This is the work.

## Steps

| # | step | state |
|---|---|---|
| 1 | the count and the stride, from the LOADER | **DONE** 2026-09-05 |
| 2 | decode the 304-byte record | **DONE** 2026-09-05 |
| 3 | what CONSUMES a light - the question that makes it worth doing | **DONE** 2026-09-05 |
| 4 | port, if step 3 warrants it | **DONE** 2026-09-05 |

## Step 4, done - the crowd is lit

Ported and drawn. `formats/light3do.*` reads the table, `o3de/vertexlight.*` is
`sub_493E40` transcribed, and `omk-play` applies every light of both resident
slots to every drawn walker before submitting. On Anekbah's street start:
**53 light hits across 13 drawn walkers**, moving **49877 bytes** of the frame.
`--no-crowd-light` is the before/after.

Three things had to be read rather than assumed, and each cost a round:

* **the vertex NORMAL is at `.3DO` vertex `+12`**, in the twelve bytes this
  port has skipped since the format was decoded. `applyPose` and the walker's
  yaw now turn it, because a normal that stays in the rest orientation lights a
  turning figure from the wrong side - invisible in any one still frame.
* **the base colour is BLACK, not the baked vertex colour.** `sub_494E80`
  writes `instance[+416]` into every runtime vertex and every site setting
  `+416` sets 0; the crowd models ship pure white (all 446 of PSH_FN's
  vertices are 255,255,255), so there is no baked light in them to keep. The
  first attempt added light to white and changed EXACTLY ZERO pixels - correct
  in every line and invisible.
* **the direction is not in the file.** `sub_493C30` computes
  `normalize(centre - pos)` at load and writes it over corner 0 at `+112`.
  Taking `+112` as given hands the lighting a world-space POINT: 5277 changed
  bytes against 49877.

**And the check could not see the third of those until it was rebuilt.** Its
first version used a light shining straight down, where the wrong component is
zero either way, so it passed the corner-as-direction bug twice. The light is
oblique now and an `xfacing` row separates them 102 against 243. Twice more,
a `find A -o B | xargs rm` deleted only the second pattern and a mutation
"passed" on a stale object - the rule is to name the object files explicitly.

Declared deviations: the reach and falloff are per BODY where the engine does
them per MESH, and the falloff is clamped to 1 where the engine has a guard
(`if (!(v11 | v12))`) this reading did not decode. `verify.py: engine vertex
light`.

## Step 3, done - it is the CROWD

The question that made the table worth decoding is answered, and the answer is
the one this repo recorded as a hypothesis and refused to assert: **a decor set
supplies the lights and the street's moving population receives them.**

`Read3DO_Init` registers a set's lights into the structure the binary calls
"Lights Collisions". `sub_4380B0` - LightInstance, by its own error strings -
registers a drawn instance in the same structure, and its eight call sites are
six functions of which **every one is street-life**: the walker and vehicle
spawn callbacks, the walker tick, two reached from `Slider_Init`, and the
player's ride mount. `sub_48D7F0` then queries the structure per mesh per
frame and calls `sub_493E40` for each overlapping light before submitting.

The lighting is per-vertex, `−(N · L)` gated on a squared-radius reach test,
with a LINEAR falloff between the inner and outer radii and the colour added
through 256-entry ramps and a saturating table. That traces `+32` as the
INTENSITY, closing one of step 2's three unexplained floats; `+36` and `+40`
remain open.

`verify.py: light consumers` asserts the offsets `sub_493E40` reads and that
no caller of LightInstance sits outside the street-life module. It reads the
decompilation and skips without it.

## Step 2, done

The record is decoded and it NAMES ITSELF: all 4179 open with the tag `LIGH`
and a name beginning `LIGHT` (573 distinct). Layout in
`engine/src/formats/light3do.h` and `docs/FILE_FORMATS.md`; the parts with a
traced consumer are the flags, the two radii (whose squares the loader caches),
the RGB colour, the position, and a footprint given as a centre and four
corners. `+112` is the first corner on disk and the light's DIRECTION at
runtime - `sub_493C30` writes `normalize(centre - position)` over it.

Two invariants a wrong layout breaks: the eight runtime-only floats are zero on
disk in 4179/4179, and the centre lies inside the corners' bounding box in
4178/4179.

**The one failure is instructive.** `MTrone.3DO`'s `LIGHT15` has NaN corners,
and because every comparison against a NaN is false, the C++ probe's
containment test PASSED it and reported 4179/4179. It was caught only because
a Python pass written separately said 4178. A check that cannot see its own
blind spot is worth less than two implementations that disagree.

Open from this step: three floats (`+32`, `+36`, `+40`) with no traced
consumer, and the light is above its footprint in only 2871 of 4179 - so "a
spot shining down" is the common case, not the rule.

## Step 1, done - and it corrected a number this repo had just published

`Read3DO_Init` (0x0044DF10) relocates the light table like every other, but
the line before it matters more:

```c
u32(*v1, 232) = u32(*v1, 240);       /* the count comes from +240 */
if (u32(*v1, 232))
    v1[8] = v1[11] + v3[10];         /* lights = base + header[+40] */
else
    v1[8] = 0;
```

**The engine copies `desc+240` over `desc+232` and then uses it.** The port
read the on-disk `+232`, and so did the `mesh lights` check written hours
earlier - which is how "6244 lights in 375 files" got published. The two fields
disagree in **256 of 635** files, `+240` is never larger, and the real figures
are **4179 lights in 216 files**.

The data settles it independently, which is the point of CLAUDE.md 1's
self-checking parse. The light table is the LAST thing in a `.3DO`, so the walk
has a file size to land on:

| count used | `lightOff + n * 304 == filesize` |
|---|---|
| **`desc+240`** | **216 of 216** |
| `desc+232` | 119 of 216 |

So the record is **304 bytes** and the count is **`desc+240`**, agreeing from
the loader and from the corpus. `desc+232` is an authored or allocated figure
that the engine ignores; nothing yet says what it means.

The lesson is the one CLAUDE.md 1 opens with, and it cost a published number:
the port's parser is DATA reading, and a field's meaning is not established
until a loader says so. `readHeader` had carried `+232` as `lights` since the
format work, plausibly and wrongly, and a check built on it merely made the
error harder to notice.
