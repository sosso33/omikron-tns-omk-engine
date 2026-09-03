# The porting standard

`CLAUDE.md` §1 is the working practice for **reading** the game: the data is for
finding, the code is for confirming. This is the working practice for
**porting** it, and it answers two different questions that had both been
accumulating unstated:

* **Part A — the target.** What does the replica compile against? Every slice
  so far emitted numbers, so the question never arose. For the output half it
  is the *first* question, and nothing else in this repo answers it.
* **Part B — the evidence.** What licenses the claim that a port is right, and
  what does a green tick in `verify.py` actually mean?

Both are **descriptive of what has worked**, not decreed. The standard changes
when an instrument changes — which is exactly what happened on 2026-09-01, when
`goldentrace.py capture` turned the output rows from "no oracle by
construction" into "diffable", and every ceiling in Part B moved with it.

---

# Part A — the target

## A1. What `engine/` builds toward

**Playability is a goal**, and so is verifiability, and they are not the same
target. The resolution is that every output subsystem has **two
implementations behind one boundary**:

| | the reference | the live one |
|---|---|---|
| render | a software rasterizer into an RGB565 framebuffer | Vulkan (MoltenVK on macOS) |
| audio | PCM buffers compared sample-exact | an audio device |
| input | a replayable event stream | a live device |

The reference implementation is the one `verify.py` checks. The live one is
what makes the replica playable. **Neither may be required to build the other**,
and in particular a bare checkout with no Vulkan SDK must still `make` and pass
the suite — that property is what keeps the evidence half honest on any machine.

## A2. The renderer boundary is at the DECISION level, not the API level

This is the load-bearing design decision in Part A, and getting it wrong is
expensive in a way that is hard to undo.

What `engine/` has ported is not triangles — it is **decisions**: the drawable
mask `flags & 0x800043`, the 14-bit bucket key, the texture slot as the key's
**low six bits**, the two blend modes (`0x1000|0x2000` additive,
`0x1000|0x4000` multiply, `0x800` cutout), the 58-slot texture cache keyed on
the 19-character name, and the visible-set walk against `sub_48D0D0`'s frustum.
The interface takes *those*:

```
    begin(view)                        the camera and its frustum
    submit(draw)                       {bucketKey, mesh, range, blend, cutout}
    submit2d(I2dList)                  the display list, already ordered
    end() -> Frame                     RGB565, 640x480
```

A backend receives decisions and turns them into API calls. It never makes one.

Put the boundary at the Vulkan level instead and the ported decisions leak into
API-specific code; the software backend then cannot be added without extracting
them again, and the frame oracle becomes unusable. The boundary is what makes
two backends possible at all.

## A3. RGB565, and why it is not a detail

**Measured from a captured frame** (`traces/frames/menu-22.png`): the red
channel carries **32** distinct levels, green **63**, blue 25 of a possible 32,
with gaps of 8 and 9 in the 5-bit channels and 4 and 5 in the 6-bit one — the
signature of a 5/6-bit value expanded by bit replication. The game's
framebuffer is **16-bit RGB565**.

So the reference framebuffer is RGB565, and **a replica that renders 8:8:8
fails a pixel diff everywhere for a trivial reason**. The Vulkan backend should
target a 565-equivalent format, or quantise on readback, if its output is ever
to be compared against the software path or against a capture. Cheap to specify
now; annoying to retrofit.

**And comparisons are made IN 565, not in 888** — established the first time a
frame was diffed, on 2026-09-01. A captured frame's 8-bit values are the
**host's** 565→888 expansion, not the game's data, and the host does not expand
the way you would guess: bit replication (`n<<2 | n>>4`) gives 56 in green where
CrossOver produced 57. Expand the reference to 888 and the menu's title region
matches 94.9%, every difference a rounding artefact of something that is not
the game. Quantise the capture back to 565 instead and it matches **66560 of
66560**. So the rule is: **bring the capture into the framebuffer's space, never
the framebuffer into the capture's.**

(Which quantisation is a free choice: rounding and truncation both invert every
level the host produces, so no test here separates them. Rounding is kept
because it is the correct inverse in general — not because a check prefers it.)

## A4. The software backend comes first — and it is not extra work

Both backends are specified. The software one is built first, for three
reasons, and only the third is about caution:

1. **It is the port.** The six x87 rasterizers have to be read and transcribed
   regardless; writing them into a framebuffer *is* that transcription. The
   Vulkan backend is a few thousand lines that are a port of nothing in the
   binary.
2. **It is the only path the oracle can check.** A software frame diffs against
   `traces/frames/` exactly. A Vulkan frame cannot, in any strong sense —
   different rasterisation rules, filtering and precision.
3. **It separates two kinds of bug.** With Vulkan first, a wrong-looking frame
   could be a wrong *ported decision* or a wrong API call, and telling them
   apart is expensive exactly when the code is newest. A verified software path
   makes that ambiguity impossible.

Vulkan with MoltenVK is the right playable target: OpenGL has been deprecated
on macOS since 10.14, is capped at 4.1 there, and its drivers are poor.
MoltenVK's gaps — no geometry shaders, some format restrictions — touch nothing
a 1999 fixed-function renderer needs. OpenGL may follow as a third backend; if
the boundary in A2 holds, it is additive.

## A5. Audio

The decode half is already at the top tier: OTNS ADPCM is transcribed from
`sub_483200` and is **sample-identical to `tools/adp.py` across all 777 `.3DM`
files, 225 441 216 samples**. That is the pattern the rest follows.

**The movie soundtrack is not this mixer's business**, and the distinction is
sharp enough to write down: `gamedata/FLIS/`'s three streams carry one **44100 Hz**
audio track each, and `Sound_Init` gives the game's own primary buffer
**22050**. The original played the movies through DirectShow, which had its own
output and never went through that mixer, so `Frontend::queueAudio` takes them
straight to a device. A replica that fed movie audio through the ported 22050
path would be wrong about both the rate and the route.

The reference implementation renders a mix into PCM buffers; no device is
involved and nothing is real-time. A live device sits behind the same boundary
and is not what `verify.py` checks. The `.SFX` chain, the effect emitters and
the cin-sfx cues are already ported and feed it.

**Amended 2026-09-01, and the amendment is the point.** This section originally
said the reference mixer would be compared "sample-exact", which assumed the
engine had a mixer to compare against. It does not: DirectSound mixes, and the
engine only ever decides *what* to play, *where*, and *how loud*. So the
boundary for audio falls exactly where A2 puts the renderer's - the decisions
are ported and checkable, and the thing on the far side is a REFERENCE
implementation whose own law is not claimed. The single sample-exact property
that survives is transparency: a mono voice, not 3D, at full volume and at the
mix rate must come out of the reference mixer unchanged. That is met; the
attenuation curve is declared unverifiable, in B6 and in `mixer.h`.

## A6. Input

The four control schemes are already lifted — **4 context groups x 14 actions x
3 devices** — and the runtime already consumes the 14-bit binding word,
edge-filtered by mask `0x203F`, in both the `.CTL` channel and the UI.

The reference implementation is a **replayable event stream**, which is what
`goldentrace run --keys` and `tools/sim/ui.py` already speak. A live device is a
source of the same stream. This is the subsystem where the two implementations
are closest to free, because the ported code never sees a device either way.

## A7. Timing

Settled, and already ported: `Game_Frame` sets the delta to `30.0 / fps`, so
**one unit is one frame at 30 Hz** and `flt_4C30D8 dd 1.0` is only its shipped
value. The engine's clocks are floats.

This is not a formality — it has already caused two bugs worth remembering: an
ambient-emitter period read as seconds ran a set **30x too slow**, and a viewer
that pre-sampled one entry per whole frame went black on a fractional index.
A live frontend must keep feeding the simulation *frames*, not seconds, however
fast it presents.

## A8. Dependencies: what "dependency-free" actually means

**It is a property of the verification path, not of the program.** Everything
`verify.py` touches — the format readers, the runtime, the reference backends
and all ~40 `dump_*` / `run_*` tools — builds on a machine with nothing
installed. That is what makes the evidence portable, and it is not negotiable.

**The playable frontend will need dependencies and should have them.** Avoiding
SDL by hand-rolling Cocoa + Win32 + X11, or avoiding an MPEG decoder by writing
one, would be strictly worse: more code, no verification benefit, and none of
it a port of anything in the original binary.

What is actually needed, and nothing beyond it:

| need | dependency | kind |
|---|---|---|
| window, input, audio device | SDL3, **or SDL2** | system, optional |
| render | Vulkan loader + MoltenVK | system, optional |
| the three intro movies | pl_mpeg | **vendored** — integrated 2026-09-01 |

**Amended 2026-09-01, twice, and both are recorded rather than left to be
found.** First, this named SDL3 and the machine the frontend was written on has
only SDL2. The surface `backends/sdl/play.cpp` uses is a dozen calls both
versions have, so it builds against either and the Makefile prefers 3
(`pkg-config --exists sdl3 || sdl2`). Second, **pl_mpeg is now integrated** and two things about it were wrong here.
It is **MIT**, not public domain (`SPDX-License-Identifier: MIT`, Dominic
Szablewski) - corrected in `engine/third_party/README.md`, which records the
provenance. And rule 2 needed a distinction it did not draw: it keeps
dependency headers in backend files, which is right for a **system**
dependency, whose absence must not break the build. A **vendored** one is
checked in and therefore always present, so `src/platform/movie.cpp` may use it
and `make` still needs nothing installed - which is rule 1's actual property.
Behind the boundary the decode would be untestable; where it is,
`verify.py: engine movies` checks it.

Rule 3 was checked rather than waved through, because a decoder is exactly the
shape of thing it forbids. The engine's own import table names
**`CoCreateInstance`** (ole32 - a DirectShow filter graph) and
**`mciSendCommandA`** (winmm - MCI), and the image carries no MPEG decoder: the
original handed the file to the OS. A vendored decoder is the equivalent of
what it did, not a substitute for code that could have been ported.

`gamedata/FLIS/` holds `EIDOS.MPG`, `QUANTIC.MPG` and `GAME.MPG`, and they are plain
**MPEG-1 Program Streams** (`0x000001BA` pack headers, video plus audio) — which
is exactly pl_mpeg's subject: one public-domain file, no build configuration.
ffmpeg is orders of magnitude larger and brings licensing questions for the one
codec this needs. And no audio *codec* is required at all: OTNS ADPCM is already
ported and sample-identical to `tools/adp.py` over 777 files. Only a device is
missing.

Two things shrink this further. **Shaders can be pre-compiled to SPIR-V and
checked in**, so the Vulkan backend needs the loader but not glslang or shaderc
in the build — a fixed-function-era renderer wants a textured, vertex-coloured
triangle and a blit. And the movies are a *playability* dependency only: the
boot path already steps past them without decoding, which is why `engine: boot`
passes today with nothing installed.

### The three rules

1. **`make` with nothing installed builds every `dump_*` / `run_*` and passes
   the suite.** A dependency that breaks this is rejected, not worked around.
2. **No ported source includes a dependency header.** They appear only in
   backend files, behind the A2 boundary.
3. **No dependency may perform work a reference implementation is supposed to
   be a port of.** Using SDL's blitter instead of the ported `Blt`, or a
   library's BMP loader instead of `surfaceFromBmp`, means the check tests the
   library rather than the port — and it would pass exactly as green while
   establishing nothing.

Rule 3 is the one that is not hygiene. The first two keep the build honest;
that one keeps the *evidence* honest, and it is the easiest to breach by
accident because the result looks identical.

**Vendored and system dependencies fail differently**, so they are kept apart.
A vendored file is checked in and `make` works without it being installed — a
missing one is a broken checkout. A system one is found by the build and its
absence merely disables the frontend, never the suite.

---

# Part B — the evidence

## B1. The ladder

Six tiers. Each row says what it means, an example that already holds, and —
the part that matters — what it does **not** license.

| tier | means | example | does not license |
|---|---|---|---|
| **1 exact** | byte-identical to the shipped data | `.3DT` 2534/2534; ADPCM 777/777 sample-identical | anything about how the data is *used* |
| **2 corpus-constrained** | an invariant the shipped data could fail | `.CTL` 7/7 landing exactly on the file size; `.SCX` 220/220 | that the runtime reading it behaves correctly |
| **3 differential** | agrees with an independent implementation | `engine/` vs `tools/sim`; `uitext.py` vs `fnt.py` | anything both implementations got wrong — they were often written from one reading |
| **4 behavioural** | reproduces the original's own output | golden traces 42/42 in order; the captured framebuffer — the menu title 66560/66560, the selection outline 1518/1518, `Text_DrawRun` 6132/6132 | more than the capture actually contains (see B5) |
| **5 data-constrained** | only the data it reads is checkable; the logic is not | `ACTOR_STATE` 0..17; the `.CTL` channel | that the machine *behaves* like the original |
| **6 read and explained** | a transcription with internal invariants only | I2D's ordering; Astaroth and the generic shooter's state graphs | anything at all about the original's behaviour |

Tier 3 deserves its warning spelled out, because this repo has paid for it
twice: **two implementations of one reading agreeing is not evidence.** It
catches offsets, signedness and off-by-one; it cannot catch a wrong reading
applied consistently. Only tiers 1, 2 and 4 can.

## B2. Declare the tier in three places

Every slice states its tier in the **source header**, the **check docstring**
and the **coverage row**. Three, not one, because each is read by someone
different and a green tick in `verify.py` otherwise implies more than was
established.

This is not bureaucracy; it is the specific failure that produced
"the shoot AI has no data at all". That claim was true of the dispatch, was
written as though it were true of the subsystem, and survived in three
documents until something contradicted it.

## B3. Four requirements per slice

1. **Tier declared**, per B2.
2. **Oracle named** — or *"none, and here is why none is possible"*. The actor
   runtime's entry is a model: it says the trace rig cannot reach it, names the
   two opcodes, and cites the capture that proved it.
3. **At least one invariant the shipped data could fail** — or an explicit
   statement that there is none. I2D has exactly one (139 flag constants
   resolving); it says so rather than letting six transcription invariants
   imply more.
4. **A falsification record** — see B4.

## B4. Every check must be shown to fail

A check that has never been broken is a check that has never been tested. Break
it deliberately, record which mutation moves which number, and put that in the
docstring.

This is the rule that has earned the most in practice. In one session it caught:

* a **circular** dispatch check — it ticked each state and asserted the channel
  advanced iff the table said it would, which is what the code reads that flag
  to decide. Flipping a row passed. It is now a differential against a second
  transcription, and flipping moves it 0 → 7;
* a **driver** bug rather than a port bug — all three Gandhar behaviour scripts
  were driven at 200 hp, so all three ran the healthy script and 90 of 144 steps
  disagreed;
* a **second real shape** — the I2D within-layer prediction scored 1260 of 1407,
  and the 147 misses turned out to be the layers holding the frame's first node,
  which takes a different branch and sets no cache;
* a check that **could not fail on this corpus** — see the priority rule in B7.

**And a trap in the mechanics of it: a stale link makes a mutation look
harmless.** Deleting the object file is not enough. The rebuilt object and the
existing binary can land in the same second, `make` relinks nothing, and the
run reports the *original* numbers — so the mutation reads as "the check does
not catch this". That happened to `engine: input`'s initialiser mutation on
2026-09-01, and a forced relink then moved 18/19 to 28/57 exactly as it should
have. This is the worst direction for a falsification test to be wrong in,
because the tidy response to a mutation that changes nothing is to weaken or
delete the check — throwing away a *working* one. **Delete the binary as well
as the object**, and treat a mutation that moves no number as a suspect build
until a forced relink says otherwise.

## B5. What the frame oracle licenses, and what it does not

`goldentrace.py capture` grabs the engine's own framebuffer. `tools/frame.py`
recovers it and **refuses rather than degrades**: a 2x capture is the
framebuffer only if every 2x2 block is uniform, and if a display ever
interpolates, nothing is written. A frame that is only *nearly* the framebuffer
would make every diff built on it quietly wrong while still passing.

It is CrossOver's rasterisation, so:

* **2D is exact.** I2D ends every primitive in an `IDirectDrawSurface::Blt` — a
  memory copy with an optional colour key. There is no filtering to differ.
  Text, the tile map, the menus, the HUD and every interface blit are the
  framebuffer the original produced.
* **3D is exact about geometry and ordering, and not about a pixel's low bits.**
  Filtering, dithering and the fog table are the driver's, and Wine's are not a
  Voodoo's.

And a frame is deterministic only in parts. Measured across three captures of
the same screen: the title bitmap and all four labels' glyph pixels are
identical — mask *and* values — while the frame as a whole differs by **52%**,
because the tile-map background animates. **A check may assert the text and the
title; it must not assert the scene.**

Every slice built on a capture states which of these it leans on.

## B6. The remaining work: reachable tier, and what "done" means

| item | reachable | acceptance criterion |
|---|---|---|
| the 26 screen callbacks | **5** | **the item bindings are done** (2026-09-01): 35 string ids and 22 tags recovered by decoding the opens' `+28`/`+60` stores, with all 29 that sit on a screen with a text file naming a non-empty string in it. **and the shop titles with them** (same day): `Ui_OpenShop`'s `+8` switch decoded through its jump table at `0x004AE7AC`, which the linear scan does not merely miss but gets **wrong** — it keeps the last arm, binding all ten shops to string 19. Eight of the ten titles name their own screen, and shifting the table one place takes that 8 to 0. **And the answers are now done too** (2026-09-01): the reply is one global, `dword_930750`, with **17 writers** enumerated from the image into `tables/ui_widgets.json` as `answerSites` - only 3 of them inside a function IDA labelled, because a widget callback is reached as a dword in the widget tree and never by a direct call. The terminal family turned out to be a second `Ui_OpenShop`: seven screens on panel `0x004E4108`, one activate callback at `0x004AF410`, a seven-case jump table at `0x004AF578` switching on the screen's own parameter - and the parameters are 0..6 with no gap or repeat, which is what makes the case index the parameter. Case 4 FALLS THROUGH into case 6, which reading the arms independently would miss. `verify.py: ui answers`, tier 2. Each site is attributed to the screens its tree serves - a child panel by the open callback that names it, which is what recovers screen 29's tree - and cross-checked against the corpus: of the **242** `ui.open` sites over 25 screens (241 in slots, **1 in a `+4` startup script**, which is why a slot-only walk sees no start menu), 15 keep the answer and 10 discard it, and **no site is attributed only to screens that discard** (0 of 16). What is NOT available, and is recorded rather than left as a gap: 14 screens share result variable **19**, so no per-screen comparison test exists. What is left on this row is only the per-screen bookkeeping between `Ui_BeginScreen` and the flag edits |
| the I2D back end (`Blt`) | **4** | a captured menu frame reproduced **pixel-exact** from the shipped assets plus the ported display list — **met 2026-09-01 for the deterministic region: 66560/66560** (`verify.py: engine I2D blit`) |
| the I2D primitives (line, triangle, quad) | **4** for the quad's geometry, **6** for the rest | **Met 2026-09-01 for the quad.** `I2D_DrawRectOutline` draws the load panel's selection box as four mode-1 quads; the ported `fillQuad` reproduces it at **1518 of 1518 covered pixels bright in the engine's own framebuffer**, with the one-pixel ring outside it background 19 of 19; the connector to the thumbnail — **another quad, not a line** — reproduces at **138 of 138**. That result *pins the half-open `[left, right)` span*: closing it covers 1552 px, 20 of which the engine never lit. The line and triangle **cannot be raised from the interface at all**, which is now established rather than suspected: `Ui_DrawItem`'s vocabulary is FILL (a quad), OUTLINE (four quads), CURSOR, ARROWS and MARKER (triangles) and SPRITE (a blit) — **no line** — and exactly **one item in the whole lifted widget tree** carries the arrows or marker bits, on a child panel (`0x004CF2E8` under `0x004CF218`) that no screen reaches. So no reachable screen draws a triangle either. Both need a 3D or HUD frame — and the blend cannot be checked from a single capture - mode 1 is 50% against an animated background |
| the software rasterizers (2D) | **4**, and both are MET | **The row was mis-specified and is corrected here (2026-09-01).** It asked for "a captured 3D frame agreeing on silhouette and coverage" for "the six rasterizers" - a phrase carried across three documents that never named them. Enumerated from the image: exactly **three** drawing functions test the software driver mode (`sub_45EF50() == 2`), and they are the I2D **line**, **triangle** and **quad** submitters. Behind them sit **two** actual rasterizers, `sub_48C4C0` (clipped Bresenham; the triangle is three of them, no fill) and `sub_48C060` (bounding-box fill, four blend modes) - and **both are already ported and at tier 4**: the selection outline 1518/1518 and the connector 138/138. There is no third. |
| the 3D rasterizer | **4**, geometry and ordering only | **A NEW row, because closing the one above closed too much (corrected 2026-09-01).** The engine has no software 3D rasterizer - nothing in the 3D path branches on the driver mode, and Direct3D drew every triangle - so there is nothing here to TRANSCRIBE, and A4's first reason for doing the software backend first ("it is the port") does not apply. A4's other two still do, and so does a criterion the closure above wrongly discarded: **B5 says a captured 3D frame is exact about geometry and ordering and not about a pixel's low bits**, so such a frame can check the visible-set walk, the bucket order, the transforms and the culling - all ported, all currently checked only against themselves. That is the acceptance criterion: **silhouette and coverage against a captured 3D frame; per-pixel equality is NOT the criterion and must not be claimed**, because those pixels are Wine's rather than a Voodoo's. The rasterizer itself is a reference implementation standing where D3D stood, like the mixer's `render()` and `movie.cpp` - not a port, and it must say so. **Built 2026-09-01** (`src/o3de/raster.*`): textured, vertex-coloured, z-buffered triangles into the RGB565 framebuffer, consuming the ported batch order and blend modes, with D3DCULL_NONE. **Tier 3 so far**: the projection is differenced against `tools/camshot.py` - whose wireframe was laid over a real screenshot of dialog 402 - and the two agree on **106 of 106** sampled Aapkayl corners through camera 4555, worst disagreement **0.0018 px**. The mirrored reading differs in **349998 of 352000** pixels, so the convention is pinned. **The 3D frames now exist** (2026-09-01): six captures of dialog 402 in `traces/frames/dlg402-*.png`, all 640x352 of picture inside the 1.818:1 letterbox, of which **t=44 and t=47 are the ones where the camera has finished its 160-frame travel and is parked at 4555** - they correlate +0.46/+0.43 with a render from that camera where the mid-sweep frames sit at 0.03. **Tier 4 reached 2026-09-01, and for ONE camera.** The silhouette metric is `frame.edge_map`/`edge_match`/`hole_darkness` and `verify.py: engine silhouette`. Per-pixel comparison is ruled out by the capture itself - between two parked frames 42% of pixels differ by <=8, which is B5's low-bit noise, while 17% differ by >=32, which is the character animating - so the instrument is **directed** (render -> capture, because the render draws the set and the capture also carries Telis, the props and a subtitle), **density-normalised** (the strongest 5% of gradients each side, so the score is not measuring a threshold) and **quoted against its own chance floor** (the same statistic at an 80-pixel shift). Rendered through 4555 into the letterbox the captures themselves define, the set scores **0.73 / 0.83** against the two parked frames on a floor of **0.27 / 0.30**; and where the render has no geometry at all - 9520 pixels, the holes a set-only render leaves - the captures are BLACK in **92% / 99%** of them against **33%** frame-wide. Four MID-SWEEP captures, same set and same everything with the camera elsewhere, reach only **0.14-0.38**; the mirrored reading lands at **0.18**, BELOW its own floor, and its holes collapse to **0.24**; `angle[1]` as the vertical fov gives **0.42 / 0.45**. **Shown to fail against the engine, not only against the check**: mutating `tanv = tanh` takes the score to 0.43/0.47, and REMOVING THE DEPTH TEST - every vertex still landing where it landed, only the ordering destroyed - takes it to 0.66/0.75 and the holes to 0.87/0.92, so B5's *and ordering* is tested rather than assumed. **What tier 4 does NOT cover here**: one camera in one set is not a claim about the renderer; the render has no characters or props, and the directed metric is built for that and therefore cannot see it (the reverse direction, 0.63/0.71, is reported as a diagnostic and asserted as nothing); no pixel's value is checked, so filtering, dither, fog and the blend arithmetic keep NO reachable tier; and the drawable mask is not exercised - swapping it for the viewer's rule changes 0 pixels here, because both rules select the identical 10257 corners of this set. **And the one-camera limit bit within hours** (2026-09-01): the rasterizer had NO near-plane clipping at all - a triangle with any vertex behind the cut was dropped whole, so a floor or a wall with one corner behind you vanished while still in shot - and through 4555 the fix changes **0 pixels and 0 holes**, because every triangle it rescues there is off the edge anyway. Nothing in `verify.py` saw it; a player flying the new scene viewer saw it in one session. That is not a reason to distrust tier 4, it is the reason B2 makes a slice state what its tier does not cover - and `engine: near clip` now carries the differential, 12710 pixels and a 10878-pixel hole one step into the room. |
| the two render BANKS | 2, and no more is reachable | Six two-entry arrays at `0x004C4910` swapped by `sub_42FA00(bank)`: five function pointers the renderer calls indirectly plus an activate hook. `Game_Init` installs bank 0; **VM opcode 150 installs bank 1**, at 14 shipped sites against 15 restores across eight named chunks - which refutes ASSETS 4c's "nothing installs it". Structure, installers and corpus are asserted by `verify.py: render back ends`. **Bank 1 is now identified**: `dword_90E09C` is the scene renderer, and its two implementations are the same 0x4000-bucket walk of near-identical length (659 lines against 660) differing only in that bank 1 converts every vertex colour to luma grey - 17 occurrences against 0. All 14 installs are closed, and **74 of the 82 instructions inside them are camera/fade opcodes**, so the bank brackets a CUTSCENE: the game has black-and-white cutscenes, and opcodes 150/151 are named `render.grey.on`/`.off` (the VM goes to **129 named**). Still unread: the other four swapped pointers, which have no oracle here - no capture distinguishes them |
| audio mixing | **2** for the loader, **6** for the bookkeeping, **none** for the law | **The criterion above was wrong and is corrected here (2026-09-01): the engine does not mix.** `Sound_Init` (0x0046C3A0) creates a DirectSound *primary* buffer, sets its format - PCM, 2 channels, 22050 Hz, 16 bits, block align 4, 88200 B/s - and calls `Play(0,0,DSBPLAY_LOOPING)` once; every sound after it is a *secondary* buffer that DirectSound's own mixer sums in. No loop in the image adds two samples together, so "the mix compared offline" could not have been met by anything. What the engine's half is, is DECISIONS, and A2's boundary applies unchanged. **Done for that half**: `Wav_LoadToBuffer` (0x0049F830) accepting all 61 shipped `.wav`, `Sound_LengthMs` differenced against a Python re-derivation, the 160-buffer bank, the 16-voice pool and its flag word, `Sound_FreeBuffer` killing what plays it, the listener with its `<= 0.0001` guard and its 0.0254 distance factor, and the volume law - which is an ATTENUATION, 0 full and 100 silent. Tier **2** for the loader and for the immediates and vtable offsets asserted against the IMAGE, **3** for `Sound_LengthMs` and the mixer's transparency (both sides written from one reading, so B1's tier-3 warning applies in full), **6** for the bookkeeping. Three of the wrappers - `Sound_SetFrequency`, `Sound_GetFrequency` and `Sound_LengthMs` - turn out to be **dead code**: 0 direct callers and 0 address references anywhere in the image, which is why they are in no decompilation, and not the missing `push` prologue this was first written as. The 45 interface names all resolve, against a **32-slot** cache, so 13 can never be resident. **Not done, and unreachable**: the attenuation and pan law is DirectSound's, is described nowhere in the image, and no rig here records sound - `render()` says so in its own header. The one waveform claim, which IS met: a mono voice, not 3D, at full volume and at the mix rate comes out sample-identical in both channels (`men001.wav`, 32953 frames), with `pause.wav` at 22080 Hz as the control that must differ. `verify.py: engine audio` |
| input | **3** | **Met 2026-09-01.** `src/input/bindings.*` runs `Input_InstallScheme`'s 4 x 14 x 3 copy, `Input_Poll`'s slot-k-to-bit-`1 << k`, `Game_Frame`'s edge filter and group-local rebinding. The start menu is answered **by scancode** — ENTER, DOWN, ENTER through the live tables and the filter — reaching answer 1, which is what `tools/sim` reaches handed the words directly; held rather than released, the third frame adds no edge and there is no answer, so the filter is shown to matter. 93 bound cells, which is ASSETS' 41 + 48 + 4 counted through the installer. It also corrected `docs/UI.md`: the interface bits' "defaults" of E and R are `0x004C65B8`'s static initialiser, overwritten by `Game_Init` before the first frame. `verify.py: engine input` |
| the Vulkan backend | **none** | **explicitly unverifiable.** Its correctness is inherited from the software backend it mirrors, and it must say so |

That last row is the point of the table. A subsystem with no reachable tier is
allowed — it just has to admit it, rather than borrowing credibility from the
checks around it.

## B7. Anti-patterns

Things that look like evidence and are not. Each has happened here.

* **Two implementations of one reading agreeing.** See B1, tier 3.
* **Asking the code that made a decision whether it made it correctly.** The
  first dispatch check read the same flag it asserted.
* **A corpus test that cannot separate the rule from a simpler one.** Of 9103
  gated `.CTL` transitions, 120 are a real priority contest, and in **0** of
  them is the first match not also of maximal priority — so a plain first-match
  rule changes not one of 12063 edges. The rule stands on
  `Cef_FindTransition`'s code; the corpus is silent. Say so.
* **A count that is quietly short.** A `.3DM` sweep reported 708 files where
  there are 777; five checks missed the eight `.SFX` files spelled `.Sfx`. A
  total that is too small looks exactly like a total that is right.
* **Generalising a negative from where it was measured.** "The shoot AI has no
  data at all" was true of the dispatch and false of the AI. A negative result
  is a fact about the hypothesis, never about the field.
* **A number in `docs/` that nothing asserts.** It is a claim with no test
  behind it, and it drifts.

---

## Enforcement

`verify.py: porting standard` keeps this document and the coverage table from
drifting apart — the specific failure `engine/README.md` records having had
twice. It asserts that the README's coverage counts sum to the row total, that
every tier this ladder defines is used by at least one check, and that every
item in B6 is one the coverage table still calls unfinished.

A standard nothing checks is prose, and this repo has a rule about that.
