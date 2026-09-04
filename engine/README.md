# `engine/` — the OMK replica

OMK's engine: builds as `build/omk`, with `build/omk-play` as its window.
The third tree. [`readable/`](../readable) is the decompilation and is **never
built**; [`tools/sim/`](../tools/sim) is the Python executable specification;
this is the replica that compiles.

Started **2026-08-30**, on the plan's own condition — "when phase 6 can test
it" ([`docs/RECONSTRUCTION.md`](../docs/RECONSTRUCTION.md), "Where the code
lives"). Phase 6's six stages are done and the tables compiled into the
executable are lifted to [`tables/`](../tables), which was the one hard
prerequisite: a replica reading the user's own `gamedata/` cannot find them anywhere
else.

## The rules, and they are not negotiable

1. **No original assembly, ever.** The binary and its listing are a reference
   and an oracle, never a component. No static recompilation.
2. **Never copy from `readable/`.** It addresses the original 32-bit memory
   image through ~15100 offset accessors, carries 306 `__asm` blocks, and has
   84 sites where the decompiler *admits it dropped a conditional*. It is a
   specification to read, not source to paste. Turning those offsets into real
   fields **is** the port.
3. **Every file format is written fresh and proved against the corpus.** Not
   against the C, and not against a reading — against every shipped file, the
   same standard the Python readers are held to. That is why the formats can
   be ported now without waiting for CLEAN bodies: the proof does not come
   from the decompiled body at all.

## Why C++, when the original is not

Measured, rather than assumed. Of 432 `__thiscall` functions in the binary,
**422 are the linked C++ runtime and 9 are game code**; there are **no**
vtables, no `__purecall`, no exceptions and effectively no `std::`. The game is
C written with a C++ toolchain, and its idiom is structs, free functions and
tables of function pointers.

So C++ here is a choice about the *replica*, made for three reasons:

* the port's real failure mode is a silent field-offset error, and strong
  typing plus `readable/types.h`'s 17 named structs is the tool for it;
* every format is "load the file whole, relocate offsets in place" — an
  ownership problem, where RAII beats reproducing the engine's manual
  `Mem_Free` discipline;
* differential testing wants a test binary and easy byte comparison.

**Where it stays deliberately plain**, because OOP would actively hurt:

* the VM's 153 opcodes are a table, matching `tables/vm_opcodes.json` — a
  switch or an array of function pointers, never 153 classes. Same for
  `tab_special_move`'s 66 rows, the camera presets and the key bindings: they
  are *data*, and keeping them data is what lets them be diffed against the
  extraction;
* the hot paths — the 0x4000-bucket render walk, the vertex transform, the
  sweep kernel — stay data-oriented. A virtual call per face would be wrong;
* floats stay `float`. The decompilation is full of `double` temporaries
  because x87 promotes everything; that is an artifact of the listing, not
  intent. No `-ffast-math` either: the plan already accepts low-bit drift
  ("diff decisions, not numbers") without adding more.

And the decomposition stays close to the engine's — one module per subsystem,
functions carrying the engine's own names — so that when behaviour diverges it
can still be compared to a `readable/` body function by function.

## What is here

Laid out by **subsystem, along the engine's own lines** — the same
decomposition `docs/RECONSTRUCTION.md` §2 counts the binary in, so a module
here has a counterpart there to be compared against.

```
src/platform/    the host
    datafs       DataFs - the ONE way the engine opens game data
    json         a hundred-line JSON reader, for the nested tables/
    boot         WinMain -> Game_Main -> Game_RunLoop, headless
    movie        the three FLIS MPEG-1 streams, decoded through the VENDORED
                 pl_mpeg and doubled 320x240 -> the 640x480 framebuffer, with
                 their 44100 Hz soundtrack. Not a port of anything: the
                 original called DirectShow
src/formats/     the containers: what comes off disk
    iam          IAM archives - AREA, DIALOG, SCENE
    mesh3do      the .3DO container: header, materials, five record kinds
    tex3dt       the .3DT textures: container walk + the LZ codec
    anim         the .ani body-animation libraries ("3.0V")
    ctl          the .CTL state machines ("CE70") - a byte walk
    scx          the .SCX scene scripts - a block plus a stream
    sfx          the .SFX scene sounds and AMBIENT EFFECTS ("5.0V")
    morph        the .3DM spoken lines - a derived frame count
    adpcm        the OTNS ADPCM codec
    fnt          the FONTS/*.FNT bitmap faces - 8-byte-unit glyph offsets
src/o3de/        the 3D engine
    particles    the effect emitters and their integrator - `.SFX` section C
    setpiece     the set pieces - `.SFX` section E run as `SetPiece_Show` and
                 `sub_451600` run them: waypoints, delay, loops, links
                 placed at a point, velocity, the +28 Y ACCELERATION, the
                 scale ramp and the camera-facing quads
    geom3do      geometry assembly: index resolution, offsets, batching
    collision    the walkable / narrow-phase / render triangle soups
    camedit      SCX chunk 10 - the camera editings a cutscene is cut with
    worldcam     the WORLD camera table `Camera_FindWorld` scans - AREA +64,
                 SCENE +32, GLOBAL +20 - and the two SUBJECT fields that make
                 1440 of its 5381 records offsets from an actor rather than
                 world coordinates
    render       the drawable filter, the bucket key, the blend modes, the cull
    raster       the software 3D rasterizer - textured, vertex-coloured,
                 z-buffered triangles into RGB565. NOT a port: the engine has
                 none, D3D drew every triangle
    texcache     the 58 global texture slots, keyed on the file NAME alone
src/actor/       the actors
    walk         the ground probe and the step limits
    pose         a character POSED - the .3DM's node quaternions composed
                 down the mesh hierarchy, conjugated, over absolute rests;
                 and the FACE MORPH, whose vertices replace the *visage*
                 mesh's rather than rotating it
    speaker      where a conversation's speaker stands - the line cameras'
                 rays, converged and dropped onto the walkable floor
    channel      the live .CTL channel: transitions, input matching, GoTo
    state        ACTOR_STATE 0..17 as a closed, enforced machine
    shoot        the four shoot-AI callbacks and Gandhar's compiled scripts
src/audio/       the sound path
    mixer        the 160-buffer bank, the 16 voices, the listener, and a
                 REFERENCE mixer where DirectSound stood
src/input/       the four control schemes
    bindings     Input_InstallScheme, Input_Poll, Game_Frame's edge filter
src/ui/          the interface
    widgets      the widget tree, Ui_DispatchInput, the name field, the
                 load panel
    options      screen 35 - thirteen pages over sixteen shared row widgets
    text         Text_LayOutBlock - the markup, the faces, the advances,
                 and Text_DrawRun's 32-entry coverage ramp
    screendraw   COMPOSE a screen: the background (full sheet or the 80-cell
                 tile map), then every visible row's text through the
                 alignment ladder. The first thing that puts the drawers
                 together into one frame
    i2d          the I2D display list: 16 layers, seven pools, the flag banks
    surface      the RGB565 surface, the BMP loader, Blt, and the mode-2
                 software rasterizer
    iamtext      IAM\<Screen> - a screen's own NUL-separated strings
src/script/      the VM
    script       slot enumeration (AREA/SCENE/GLOBAL) + the bytecode decoder
    gamestate    the 8192-byte game database and its six arrays
    interp       the interpreter: flow opcodes real, the rest stubbed + recorded
    world        zones, the action FIFO and the pump - which script, and when
    area         Area_TickLoad case 9, and the Session that ticks frames
    dialogue     conversations: conditions gate, actions run; and
                 DialogPlayer, which plays one and waits for the player -
                 voice from MORPH/<+46 stem>.3DM, text from ptr[8]
    objects      IAM\OBJECT - 1002 fixed slots, not an archive
    globaldata   IAM\GLOBAL's fixed header: the combination recipes
    program      Script_PlayScript - the SCENE OBJECT interpreter
    scenehost    which .SCX a chunk plays from: AREA +97, and the scene map
    scenerunner  Script_PlayAllScripts - scx.play* starts, the frame ticks
    goldendiff   the golden-trace differential: anchor, replay, subsequence
    savefile     the save container, and the 41-day calendar
    inventory    Game_HandleEvent 25..42 - the inventory screen's data channel
tools/           one dump program per differential, each its own binary
```

Includes are written **from the `src/` root** — `#include "formats/iam.h"`,
`#include "script/script.h"` — which is what `-Isrc` is for and what keeps
them stable when a file moves. The Makefile globs `src/*/*.cpp` and builds
every `tools/*.cpp` into its own binary, so adding either needs no edit to it.

And **outside** `src/`, because A8 rule 1 says `make` with nothing installed
must build everything and pass the suite:

```
backends/sdl/play.cpp    the LIVE frontend - the only file in the tree that
                         includes a library header. `make play` builds it when
                         pkg-config finds sdl3 or sdl2, and prints why it did
                         not otherwise. `make` is unaffected either way.
                         It carries TWO programs: the interface (the boot
                         chain, the movies, the widget tree) and `--scene`,
                         the SCENE VIEWER - a set drawn live through the same
                         `drawGeometry` the checks measure, with free-look,
                         the set's own cameras on [ and ], and the `lights`
                         cycle. An instrument rather than a ported slice, and
                         the reason it exists is that until 2026-09-01 the
                         3D path could only be judged as a number.
```

```
backends/vulkan/vkrender.cpp   the LIVE renderer - Vulkan, MoltenVK on macOS,
                         and the only file that includes vulkan.h. Implements
                         `Renderer` (src/o3de/renderer.h), which is PORTING
                         A2's boundary at the DECISION level: it receives the
                         bucket key, the blend, the cutout and the ORDER, and
                         turns them into API calls without making one. Renders
                         offscreen with `readback()` to RGB565, so the live
                         renderer and the software reference are
                         differenceable rather than merely parallel.
                         `make vulkan` builds it when pkg-config finds vulkan
                         and glslc is present; `make` and the suite are
                         unaffected without either (PORTING A1/A8).
```

**`build/omk-play ../fr ../tables` LAUNCHES THE GAME**: the three FLIS movies
(any key skips one, ALT all three), then whatever screen AREA 118's own startup
script asks for - the start menu - answered by a person, then the script
resumes into its first conversation. The frontend does not know about screen
29; `Session::answerUiFromPerson` parks the script at `ui.open` and only
`answerUi` releases it, which is `Game_HandleEvent` case 5.

The world RENDERS after the menu, and the script is what puts it there: the
resident AREA header's `+88` names the decor set, and `camera.set` /
`camera.set.wait` name the framing out of `Camera_FindWorld`'s three tables
(`src/o3de/worldcam.h`). `camera.set.wait` HOLDS the script for its move, which
is what the handler does - without it AREA 118's six intro shots and its
`area.goto` all fire in one frame.

A conversation PLAYS, and nothing but the player moves it: the node's voice
runs, ENTER is NEXT (which evaluates the conditions and reveals the menu), the
arrows select and ENTER picks. `src/script/dialogue.h` carries why a timer
there was wrong. Its **line and replies are DRAWN** with the game's own fonts,
in the box `Dialog_TickUI` measures - 32 px in from each side, against the
bottom, both scaling with the display - and through the node's own **camera
pair**, which snaps to the first and travels to the second over 160 frames.

And the BEAT before it PLAYS: `scx.play.actor.wait` (60) holds the script until
the object's program ends - 187 frames while the camera travels 130 - and the
character is on screen for it, because `character.show` is what puts him there
and it comes first. His pose is the scene object's: a clip of the scene's
animation array (param 1) on an authored `.3DP` path (param 8), which for GRID
is `1KaylArrives -> INTRO1.3DA` on `UBas.p1`, then `2KaylStand -> INTRO2.3DA`
looping on `UBas.p2-3`. A path names the PELVIS; a camera solve names the
ground and takes the feet.

**The speaker is DRAWN**: the actor record names his model, the line's `.3DM`
poses him a frame at a time, and the line cameras' rays converge on where he
stands, dropped onto the walkable floor. `src/actor/pose.h` and `speaker.h`.

His FACE is morphed too - the `.3DM`'s per-frame vertices replace the
`*visage*` mesh's outright, which is what moves the lips.

A line drives the pose for its own frame count and FADES into and out of the
scene idle over `min(30, frames/4)` frames (`actor/pose.h`, BLENDING TWO
POSES), with its ROOT rotation kept - the engine's identity-key write misses
(`g_MorphRootTrack` is only ever -2), and that is the whole-body bow the
original shows; a speaker no scene object drives keeps the old cancellation
as its staging fallback. `tools/blend_probe` pins the arithmetic,
`tools/dump_lineblend` prints a line's poses beside the idle's. The press
that leaves a line SILENCES its voice: `Dialog_TickUI` case 2/7/8 calls
`Morph_Stop`, which stops the voice buffer (`sub_46CAE0`), so the frontend's
`playSound` now returns a handle and `stopSound` drops that shot from the
mixer on NEXT.

Not there yet: no second speaker is staged; the `.CTL` idle the web viewer
falls back to between lines is not run, so a speaker with no scene clip holds
his last frame while the menu is up; `RCamera` has no
ROLL (conversation 272's first camera asks for -15 degrees); and the subtitle's
WRAPPING is a reconstruction rather than `Text_LayOutBlock`.
**A camera whose eye or target is an OFFSET from an actor cannot be pointed**: 1440 of the 5381 world cameras are, including the Impasse's, and
the player has no position until the actor runtime places him
(`actor.goto_address 654` is where AREA 118's script does it). No characters,
props or scene objects are drawn - only the decor set.

```bash
make play                                    # + Vulkan when it is available
build/omk-play ../fr ../tables                          # the game (Vulkan
                                                        # when there is a
                                                        # device for it)
build/omk-play ../fr ../tables --software               # the reference
build/omk-play ../fr ../tables --fps                    # the frame rate
build/omk-play ../fr ../tables --scene Aapkayl          # V swaps the backend
build/omk-play ../fr ../tables --scene Aapkayl \
    --eye 3526,1015,-905 --at 3412,1032,-882 --fov 83   # dialog 402's camera
build/omk-play ../fr ../tables --scene Aapkayl --frames 1 --dump shot.bin
make vulkan && build/run_vulkan ../fr ../gamedata/MESHES/DECORS/Aapkayl.3DO \
    3526,1015,-905 3412,1032,-882 83 out.bin 640x352    # the two, differenced
```

The render path is **there** as of 2026-09-01: `src/o3de/renderer.h` is
`PORTING` A2's boundary and it has both implementations behind it - the
software reference and Vulkan - plus the **mirror pass** (`drawWithMirror`),
which lives on the boundary rather than in a backend so both get it.

Two contracts that boundary turned out to need, both learned the hard way and
both now written down where a caller will read them:

* **`readback()` is idempotent and its result is writable.** The mirror pass
  composites straight into it and the frontend then reads it again. Software
  gets this free by returning its stored framebuffer; Vulkan re-read the GPU
  buffer per call and silently discarded the composite.
* **`setTextures()` is a LOAD-time call.** Per frame it is a span assignment in
  software and 22 descriptor-set allocations in Vulkan, which exhausts the pool
  in about six frames and then crashes.

And one build rule: `make play` / `make vulkan` depend on `$(SRCOBJS)`, because
they compile their own source fresh and link prebuilt objects - a struct that
grows between the two halves links anyway, with no error.

### Later: performance, and none of it is urgent

Measured 2026-09-01 over 300 frames of Aapkayl with `--nodelay`: software
14-16 ms a frame, Vulkan **1.4-5 ms** including readbacks, against a 16.7 ms
budget at 60 Hz. So the replica is already inside budget on this machine and
every item below is headroom rather than a fix. Written down so they are not
rediscovered, and deliberately NOT done: the replica is inside frame budget,
so none of it buys anything a player would feel. (This line read "not done
while the game does not boot to play" until 2026-09-03, which stopped being
true when adventure mode landed on 09-02 - `omk-play` now runs the movies,
the menu, the intro conversation, the Impasse editings and hands over the
player in one ~3600-frame run. The conclusion is unchanged; its reason was
stale, and the README quoted it.)

* **The software rasterizer is the reference and is allowed to be slow.** Do
  not optimise it at the cost of clarity: it is the thing `verify.py` checks
  and every claim in `docs/` about the 3D path is a claim about it.
* **Vulkan still re-uploads a whole vertex buffer per `Geometry`** and keys the
  cache on the pointer, so a relight (`L`) or any per-frame geometry edit
  re-uploads. A dirty flag, or splitting static positions from the per-vertex
  colour, removes it.
* **One command buffer, one fence, one frame in flight.** `end()` waits for the
  GPU before returning. Two or three frames in flight would overlap CPU and GPU
  and is the single largest win if one is ever needed.
* **The mirror pass costs a second full scene draw**, which is what a planar
  mirror costs; the reflection could be culled harder than `inFrontOf` does
  (it clips per triangle against one plane and nothing else).
* **`presentSurface` converts 565 on the CPU** for a software or CPU-composited
  frame. A compute shader or a 565 staging image would move it, but it only
  runs on frames that are not GPU-resident anyway.
* **The 2D layer is still a texture upload per frame** and is not on the
  renderer boundary at all (A2 lists `submit2d`). Worth doing when the UI and
  the 3D scene have to compose in one frame, not before. `audio/` still wants a device behind the
reference mixer. **The intro movies play** as of 2026-09-01 - three MPEG-1
program streams at 320x240, doubled into the framebuffer, skippable with LMENU
because that is the engine's own key (`docs/BOOT.md` 2).

Both dumps are **binary, not text**. The records carry floats, and a text dump
would compare two float *formatters* rather than the parsed values; here the
four bytes are read out of the file and written back unchanged, so a mismatch
is a parse difference and nothing else.

### Floats: the one thing that does not compare naively

`struct.unpack("<f")` widens to a Python double, so the reference readers sum
`vertex + meshOffset` in **double** and keep it. The replica sums in
**float32**, which is what the engine stores. So the geometry differential is
not equality but *"the C++ float32 equals the float32 rounding of the
reference"* — and under that it is exact on all 1696452 corners.

The drift that hides is measured, not assumed: **0.00195 units** at worst,
**5.4e-08** relative — one float32 epsilon, three parts in 100000 of a body.
That is RECONSTRUCTION's "diff decisions, not numbers" with a number on it,
and every later slice will meet the same thing.

## Building and proving it

```sh
cd engine && make                     # needs only a C++20 compiler
python3 ../tools/verify.py            # `engine: 3DT` runs the differential
```

**`verify.py: engine 3DT`** requires the decoded RGB to be **byte-identical to
`tools/tex3dt.py`** on all **2534** textures. Not "looks right", not "decodes
without crashing" — the same bytes, or it fails. The Python side is itself
corpus-proved (every texture lands on exactly width×height, which a wrong
choice in any codec parameter would desynchronise), so agreement with it is
evidence about the port.

**`verify.py: engine 3DO`** does two things, and the second matters more:

* every mesh node and camera in all **635** models matches
  `tools/mesh3do.py`'s own readers field for field — 16188 meshes, 666
  cameras;
* and four **invariants the data itself could fail**: the per-mesh
  vertex/triangle/quad counts (mesh record +64/+68/+72) must sum exactly to
  the header's totals (descriptor +196/+188/+192), and `doorOff - meshOff`
  must be exactly `140 * meshes`. Those are different places in the file, so
  a wrong offset on either side breaks them. 635/635 on all four.

**`verify.py: engine geometry`** compares all **220** decor sets against
`omkdata.decor_geometry` batch for batch and corner for corner — 1696452
corners — under the float32 relation above. Built with the **engine's** own
drawable rule (`flags & 0x800043`) instead of the viewer's, the corner count
drops by exactly **9, in one set**: three triangles, the same three meshes
`verify.py: render drawable mask` finds from the flags alone. Two independent
routes to the same three faces.

**`verify.py: engine IAM`** asserts a *shape*, not a total, and the reason is
worth repeating. Only **three** files under `gamedata/IAM` are archives — `AREA`
(259 chunks), `DIALOG` (420), `SCENE` (71) — and the reader matches
`dialog_triggers.archive()` on every chunk of them, by index, size and content
hash. Everything else there is a different format wearing the same directory:
`GLOBAL` is `fopen`ed as a plain file with a fixed header, `START` is the
new-game save, `OBJECT` is 1002 records of 2048 bytes, `GAMES` is the save
file, and the small screens are interface text.

`GLOBAL` and `START` still come back as "one chunk" — from the C++ **and** from
the Python. That is a false positive both implementations share, it is
asserted explicitly so nobody builds on it, and CLAUDE.md §1 already records
what reading `GLOBAL` as an archive cost the first time. A reader that started
finding chunks in `OBJECT` would fail this check, which is the point.

**`verify.py: engine scripts`** is the first slice that is not a file format,
and the first with a real falsification. It decodes all **5775** AREA and
SCENE script slots — **57185 instructions**, every one matching
`dialog_disasm`'s pc, opcode and operand bytes — and then decodes them *again*
with the executable's own uncorrected operand lengths, where **77 slots fail**.

That second run is the point. Without it the check would only say the C++
agrees with the Python. With it, "everything decodes" becomes a test the data
can fail, and the 17 corrected operand lengths stop being a convention this
repo adopted and become something the corpus requires.

The operand table is loaded from `tables/vm_opcodes.json` **as data**, not
baked into code — which is what lets this check exercise the extraction as
well as the decoder.

**`verify.py: engine execute`** is the first check here that compares what two
implementations *compute* rather than what they parse. It runs all **5958**
world scripts — the same corpus `tools/sim/vm.py` walks, including each
chunk's startup script at `+4` and GLOBAL's ten — against one game state
carried across the whole corpus, and requires:

* **5818** to reach `end` and **140** to stop at `dialog.start`, where
  `Script_Execute` returns outright (treating those as `end` would silently
  execute code the engine never reaches);
* every slot's offset and status to agree in order, and both sides to record
  the same **18584** stubbed subsystem calls;
* **the final 8192-byte game database to be byte-identical.**

That last one is the strongest thing available: every variable write, bit flip
and `scene.load` accumulates into that block, so an error in the arithmetic,
the operand indirection, the `case` peek, or the read-modify-write on a zone
bit lands as a differing byte.

**`verify.py: engine zones`** is the scheduler, and its test is checkable by
name rather than by "something executed". Stand in zone **3732** — record 0 of
SCENE 53 — facing into its arc, press action, and its activate script must
launch dialog **387**. All ten calls come out identical to `tools/sim`'s, in
order, with their operands, ending in `dialog.start 387`.

The lifecycle closes with it: the activate script retires its own zone, the
save bit goes **1 → 0**, and `Zones_RegisterAll` no longer registers it. That
only works because `Zone_SetStateBit` read-modify-writes — its decompilation
is a bare OR, which could never clear a bit.

And **two** zones arm at that spot, not one: 3737's enter script runs to `end`
while 3732's activate stops at `dialog.start`. Asserting both is the kind of
detail a looser test drops.



**`verify.py: engine area load`** is the first check here measured against the
**game** rather than against the Python. Loading AREA 118 — "Introduction
Kay'l", what a new game enters — runs its startup script at `+4`, and what the
port issues is compared with `traces/intro.log`, a capture of the real binary
announcing its own operands. Seeded with `Interface = 1` it emits

    OBJECTS 997, CAMERAS 2172, CAMERAS 2148, DIALOGS 272

— the capture's first four events of those domains, **in order**, before
stopping at `dialog.start` exactly as `Script_Execute` does.

The seeding is not tuning. `ui.open` is a player question: the handler parks
the context and the interface writes the answer into variable 19. Nothing here
models the interface, so the variable must carry what the player chose — and
the check asserts **both** arms, because unseeded the port takes the *other*
opening (`CAMERAS 2152, 2153, OBJECTS 753, …`), which the capture does not
contain. A port that ignored variable 19 would fail.

The same run sweeps every chunk's `+4`: **259 AREA and 71 SCENE chunks, 173
with a startup script, 157 without, 0 failing to decode, 1968 instructions** —
every number identical to what `verify.py: startup scripts` derives in Python.

**`verify.py: engine intro`** boots a new game and matches the original
binary's own recording of it, **42 decisions of 42, in order, from the very
first one** — the whole opening, the conversation, the `area.goto 222` /
`scene.load(222, 55)` transition, and the start of the Impasse's beats. The
capture runs to 58; the remaining 16 need the player to walk.

**The port narrates every announcing opcode, implemented ones included**, and
that is not a detail. `Dbg_LogTagged` fires from the handler whether or not the
opcode does anything a stub would model, so a variable write announces exactly
as loudly as a `camera.set`. Recording only the *stubbed* calls made the port
look silent where the game is not — and the diff reported it as a drift at
event 17 rather than as a missing feature.

What made it reach past the conversation: **`dialog.start` does not suspend
its context.** The handler leaves it running with the pc past the operand,
`Script_Execute` returns for the frame, and `g_DialogState` goes to 3 — after
which the pump refuses to run until case 63 puts it back to 1. The world stops
*around* the launch script and it resumes at the next instruction **with its
stack intact**, which is why a context owns its interpreter rather than making
a new one per frame.

Three filters are applied and all are the engine's own: `Dbg_LogTagged` drops
`a1 == -1`, the `CHARACTERS` domain and `VALUES` before the profile call, so a
diff must apply the same three or it reports the logger's own filter as a
disagreement. Nothing is dropped from the capture side.

**The announce map is data, and that is not an aesthetic choice.**
`tables/vm_announce.json` is derived from the assembly — 49 handlers announce,
104 are silent, and `field` says which operand (`scene.load` announces its
*second*). A hand-written version lasted about an hour before being wrong
three ways: `scx.play` and `music.play` announce nothing, and
`scx.play.actor.wait` announces the **actor**, not the object. CLAUDE.md §1's
rule, biting inside the port.

**`verify.py: engine walk`** ports the walker: one frame of `Actor_Move` +
`Walk_GroundResponse`, in the engine's order — try the move, probe under the
result, and let the ground decide. Walking a 400-step spiral across `ARESTO14`
from an authored ADDRESSES position it moves **287** times and reverts **113**,
identical to `tools/sim`'s stage-5 numbers, with the height varying by under a
unit and no fall.

**The reverts are as much the point as the moves** — a walker that always
succeeded would be one that had stopped testing. Two things had to be right to
get there, both of which the reference got wrong first: the ground ray starts a
step-height *above* the feet (from the feet exactly it finds nothing and every
step reads as a hole), and the start is an authored position probed *downward*
(probing up from -1e6 returns the ceiling, which put the first version on the
roof).

**Two soups, and the difference is stated rather than chosen.** The engine
probes COLLISION geometry — every mesh, CollisionOnly volumes included, only
faces flatter than 30° — which is 1107 triangles here. `tools/sim` probes the
RENDER soup: 2875 triangles, CollisionOnly dropped, no slope test. The port
builds both and the check runs both; they give the same 287/113, which is a
fact about *this room* and not a general equivalence.

Not covered: the narrow phase. Collision here is the ground probe alone, so the
walker keeps to the floor and passes through walls — the same limit stage 5
states, and the sweep kernel is 930 lines of x87 with no shipped fact to prove
a transcription against.

**`verify.py: engine dialogue`** runs the conversations — the last stubbed
subsystem in the decision path. Two tests, kept apart because they ask
different things:

* the **walk**: every conversation from node 0, taking the first available
  branch — **321 parse, 321 end, 0 cycles, 837 nodes visited**. Each gets a
  *fresh* state; carrying one across the corpus would let an earlier
  conversation's actions change which branches a later one offers, making the
  corpus order part of the answer;
* the **scripts**: every condition and action executed standalone — **612 of
  them (247 + 365), all reaching `end`**. A condition that cannot be evaluated
  is one the reply menu could not have been built from.

Plus the graph's own invariant: **1452 of 1452** branch targets point at a node
that exists.

The node's nine pointers split into conditions (`ptr[0..3]`, event 55, while
Dialog_TickUI builds the menu) and actions (`ptr[4..7]`, event 59, when a reply
is chosen). That split was proven by tracing, not guessed. Which reply a person
picks is player input and is not in the data, so the walk takes the first
available branch — the least interesting policy that still exercises every
condition on the path.

**`verify.py: engine anims`** reads the `.ani` libraries and checks the one
thing the data could fail: **every rotation key must be a unit quaternion, and
all 243362 are.** A wrong track offset lands on numbers that are not — which is
exactly how "the offsets are relative to the *descriptor*, not the file" was
confirmed. Read as file offsets they mostly still land on quaternions (the file
is full of them), but the root bone's tracks fall inside the bone table and
decode to garbage. 11 libraries, 265 clips, 52893 position keys.

Worth knowing for any port: **the engine never names a clip.**
`List_PickRandomByType` walks the list matching `type` at +0 and returns a
*random* one of the matches, so an actor asking for an idle gets whichever of
that type the file happens to offer.

**`verify.py: engine CTL`** ports the `.CTL` state machines, and the check is
the format's own: **the walk must land exactly on the file size, and all 7
shipped files do.** Nothing in a `.CTL` points at the next section — the reader
adds up nine variable-length blocks, each gated by a flag on the entry — so
misreading one gate (the `0x8002` unnamed junction, the `0x140` turn, the
`0x280` root shift, the `0x2000000` combat window, the fight-AI table's
two-pass layout) puts everything after it out of step. Landing on the size is
the whole proof.

Then the graph, independently: **398 clips, 1286 states, 931 children and 931
parents** — the same edges stored both ways, which is why the documented total
is 931 distinct rather than 1862 — plus **1113 gotos**, and **0** of any kind
unresolved. Parents and children resolve within the state's own group and a
goto across the whole file, so they are checked separately.

The trailing pointer fields the format appears to carry are **dead**:
InitCEFFile never follows them, it recomputes them. A port that trusted them
would read garbage — chasing them is what made this format look unsolvable.

**`verify.py: engine SCX`** reads the scene scripts — **220 scenes, 4511
objects, 13887 functions, 2228 linked state pairs, 132857 parameters**, all
identical to `tools/scene_scx.py`.

Two things a reader gets wrong unless told, and the port has both: **an unknown
chunk tag is skipped rather than an error** (literally the loader's `default:`
case, so the block carries padding between chunks — a reader that rejected one
would refuse files the engine accepts), and the function parameters live in
**one shared pool**, indexed rather than inlined, which is what makes "the pool
is consumed in order with no gaps" a check the data could fail.

**`verify.py: engine FX`** is the ambient effects — a set's neon, smoke, fire
and steam, which are not in the `.3DO` at all:

```
.3DO   a mesh flagged 0x40000000, and its position
  |    the first FOUR BYTES of its name, compared as a dword
.SFX   section D -> section C: sprite, velocity, lifetime, cone, scale, mode
  |
.SCX   chunk 4: the sprite, whose QUADS are its animation frames
```

Ported: the six-section `.SFX` walk — **67 files, all 67 landing exactly on
the file size**, which is the whole proof the strides are right since nothing
points at the next section — and the binding: **579 meshes carry the flag and
325 bind an effect**, across 16 of the 220 sets.

**The chain is complete end to end.** Section C's sprite id resolves through
the `.SCX` stream's chunk 4, and **321 of the 325 bound effects also find their
sprite** — matching `tools/ambientfx.py` exactly. The four that don't are
effects whose sprite isn't in their own scene: bound, but not drawable.

Two things a port must not get wrong, both already paid for once: the cadence
at section D `+12` is a period in **frames**, not seconds (ticking it in
seconds ran a set 30× too slow), and a period of **0 means a particle every
frame**, not "no emitter" — with a 1-frame lifetime that is a steady glow, and
an earlier reading that had Anekbah's neon flickering was withdrawn.

**And four things this port DID get wrong, all of them by reading the data
where `Sfx_TickAmbient` had already decided** - the rule CLAUDE.md 1 states,
caught here by rendering a frame beside `traces/frames/intro-75.png`:

| what | the port had | what the tick does |
|---|---|---|
| the sprite | an index into `aventure.scx`'s twenty | an **ID** resolved through the SCENE (`sub_4A5800(scene+8, id)` over 36-byte rows keyed `+32`). GRID registers ids 9..12; indexing swapped IMPACT1/2 and made `burn`'s smoke a muzzle flash |
| the colour | none - every particle white | section C `+48` at birth ramping to `+52` at death, `0x00RRGGBB`, the rate built only when the two dwords differ |
| the scale | started at 1.0, ramping `+-scale/life` always | starts at the effect's `+56` (written into the instance's `+24` **and** `+28`), ramping only under flag `0x4` or `0x2000` - 8 of GRID's 10 would ramp, 2 do |
| the alpha | 0.887, `Sprite_SpawnInstance`'s | **0.5**, written outright by the tick unless flag `0x2` ramps it, which no shipped row sets |

The colour is the one that could not have been found any other way: the
intro's portal is an **orange** impact sprite modulated by `0x2125AF`, so
every hypothesis that stayed on the texture side was consistent with drawing
it fire-orange. `verify.py: sfx set pieces` asserts `vd` and `burn` as
(id, sprite, start scale, flags, birth colour) so the four cannot come back.

**And eight more the next pass got wrong**, every one a rule already read and
transcribed a step off, caught by laying the intro's frame 90 beside
`traces/frames/intro-75.png` (2026-09-02). `tools/particle_probe.cpp` was
written before the fixes so `verify.py: engine particles` is on record
failing all of them:

| what | the port had | what the engine does |
|---|---|---|
| the alpha's consumer | folded into the colour, halving every particle | into the diffuse's HIGH byte; ONE/ONE and ZERO/INVSRCCOLOR never read it, and no shipped effect uses the SRCALPHA modes |
| the multiply | `dst * src`, in both backends | `dst * (1 - src)` - D3DBLEND 4 is INVSRCCOLOR; `omkcut.html` had it right |
| the draw order | set, then speaker, then particles grouped by sprite | ONE ascending 14-bit key; every additive batch before every multiply one |
| the lifetime | `age < life` | `age <= life`: L+1 per emitter, 210 for GRID's four rows |
| the frame index | from the age after the increment | from the age BEFORE it: a newborn draws frame 0 |
| the drift sign | `+drift` | `-drift` unless flag bits 0x600 read 0x200 (`fchs` at 0x46DD9D) |
| the Vulkan cache | a fresh Geometry a frame, revision 1 at the same address - frozen particles | one persistent Geometry whose revision accumulates |
| the set pieces | one persistent emitter at the row's shipped `+28` | a STATE MACHINE (`o3de/setpiece.h`): the row walks its section F block as waypoints, after a delay, for a counted number of loops, in a linked frame - which is what orbits `ttt` round the disc and makes the capture's dark ring |

**`verify.py: engine world data`** is the three world-DATA tables, ported
together because they interlock and because each catches a way the others can
look right alone.

The **message subscriptions** are the *same bytes* as the second script table:
the 8-byte records `Message_RunHandlers` walks are what slot enumeration mines
script offsets out of, read as `[int32 handler][int16 message id]`. **154
subscriptions, 138 with a handler, 0 pointing outside their chunk, ids 0..32** —
and the 16 without a handler are rows, not failures. That double duty is why
AREA 118's `+68` read as a script pointer for a while and was a coincidence:
its subscription table is *empty* and based at the start of the code after it.

`IAM\OBJECT` is **1002 fixed 2048-byte slots, not an archive** despite the
`IAM\` path, and the test the data could fail is `+0 == the slot number`:
**1002 / 1002**. An archive reading gets a directory that is really the first
record's fields, and this check collapses.

The **combination table** at `GLOBAL +12` is 11 symmetric records `[a][b]
[product][gate]`; all **33** ids name a real object. The gate is compared
against a global with four references in the whole binary, all inside
`Game_HandleEvent` case 37, which writes 1, 0 and −1 — **never 8**. Six of the
eleven want 8, so every spell recipe is dead content. `+64` is an **int16**
naming object 330; read as a dword it gives 29229386, which nothing downstream
rejects, and that is how the width was caught.

**`verify.py: engine fight AI`** reads the `.CTL` `+76`/`+80` table the walk
used only to step over. A profile is 156 bytes keyed by the difficulty level
plus one, and a "move" is a *sequence* of input words `Fight_TickAI` pushes
into the actor's own queue with `Perso_InjectInput` — so the AI has no
animation path of its own: it plays the same `.CTL` machine the player does, by
pressing buttons.

**7 files still walk exactly to EOF** with the table read rather than skipped,
only the **3** combat files carry profiles, **11** in all, and every profile's
move and word counts match `anim_ctl.py` row for row. The number that is a fact
about the content rather than the parse is the **bit union, 0xCFF**: the AI
presses ten of the fourteen bindings and never CTRL, SPACE, SHIFT or TAB — the
four non-combat ones. A wrong stride does not produce that.

**`verify.py: engine programs`** is `Script_PlayScript`, the scene-object
interpreter — the other half of the game's logic. The world scripts decide
*what* happens; an object's program decides *how it looks while it happens*.

The corpus half asserts the runtime fields really are authored repeat counts:
**13887 function records over 4511 objects**, the run counter identically 0 on
disk, the repeat limit 1 in 13247 and −1 in 43 with nothing outside 1..32 or
−1, and the loop count taking exactly two values, 1 (3551) and −1 (960).

That last number is why the port **models a bug rather than fixing it**. The
rewind past the last function goes through `Script_StartScript`, which zeroes
`loopsDone` along with the pc and the clock — so a finite loop count above 1
would loop for ever. It never bites, because 2 is not a value the data uses.
The engine's behaviour is the specification.

The run half is the plan's own test: `Re14.SCX`'s `Telis_eat` must alternate
its two clips for ever. Over 400 frames it begins **12** clips, alternating
`TELRES02.3DA` and `TELRES05.3DA` with no repeat, is still running at the end,
and has rewound 5 times — matching `tools/sim/scene.py` exactly.

What `busy` **means** per function is the one thing not in `Script_PlayScript`
— it is in each handler — so it is a stated model, written at the top of
`script/program.h` rather than buried: an animation is busy for its clip's
frame count, `Wait` for its float parameter, `PlaySyncSound` until param 1
reaches the clock, everything else never.

**`verify.py: engine save+clock`** closes the last of them. A slot is
`32 + 4 + 4 + 8192 + 24576 = 32808` bytes and the file is
`3496 + 256 × 32808 = 8402344` — three literals from the writers, and the save
the engine wrote under the golden-trace rig is exactly that size.

It settles something no literal could: the header is `OMK_SAVE`, then
`640 × 480`, and only **119** of its 3496 bytes are non-zero. So the header is
the *profile* block and a slot is self-describing — which means
`SaveDir_CountByName`'s 256 × 72 walk is over an in-memory directory, not over
this file. Two readings had been disagreeing about that with nothing to
adjudicate. And the DB inside reads as state: **area 237 with scene 57**,
`premiere impasse` set, `Impasse Finie` not, `Interface` = 1.

`IAM\START` is that same block as a new game ships it. The walk lands on
**5686** with 8 bytes of alignment padding, all six counts agree with a source
outside the header, and **no bit is set past a count** — array 4 declares 791
addresses in 792 bits and array 5 declares 4558 zones in 4560, so a wrong count
would leave state in the leftovers. The `State_Apply` ↔ `State_Save` round trip
differs in **exactly bytes +60..+67**, which is not a failure: those eight are
the two bio pointers `State_Apply` plants in the player record and `State_Save`
does not put back. Anything else differing would be one.

The clock is tested by the **content**, not by its constants: eight objects in
`IAM\OBJECT` are in-world newspapers named for their date, and all eight are
legal in the 41-day, 13-month calendar, with `41 Andar` landing exactly on the
month length. A new game starts 12 Nadim 7216 at 11:10:00 — and every division
in the formatter is integer and none of them even, so a float there drifts.

**`verify.py: engine scene loop`** is the two halves of a frame joined. Until
this, the object interpreter ran standalone — `Program` could be ticked, but
nothing in the engine ever *started* one, because `scx.play*` was recorded and
dropped like every other stubbed call. `SceneRunner` is the wiring, and both
callers drive it: the zone lifecycle (`World`) and the area session
(`Session`).

**Resolving the scene is the part that fails silently.** A `scx.play*` operand
is an object id *local to the resident scene*, and the ids are small and
reused — so the wrong scene resolves the wrong object without any error. An
AREA names its set at `+97`; a SCENE names nothing, and has to be traced back
to the area whose `scene.load` loads it, which is a corpus scan of every
opcode-71 site rather than a field. Zone 3732 exercises both halves: it is in
SCENE 53, the map says AREA 217, `+97` says `Re14`.

**And the operand is not always field 0.** 57/58 name a scene object and 46/90
the player's, but 59/60 name an **actor first and the object second**. Reading
field 0 for all six starts the wrong thing on two of them.

Standing in the zone runs the whole chain: two objects start — 22
`Uzal---->assis` through the *waiting* variant and 32 `Uzal_Stand` — none is
missed, `dialog.start` loads 387 with its 13 nodes, and after 120 frames
**one of the two programs is still running and one is not**. That last number
is the one no format check can produce: the entrance must finish, the loop
must not. Every value matches `tools/sim/run.py` stage 4b.

An area transition re-resolves the scene, because entering an area changes
which `.SCX` is resident — and the outgoing scene's programs go with it, which
is the engine's behaviour rather than a simplification: `Scene_LoadSCX`
rebuilds the object pool, so nothing survives the hop.

**It does not swap the decor, though** (2026-09-03). The destination comes up
in state 2 beside the origin and both are drawn until `area.arrive` hides the
non-active row; the ACTIVE row - the zone tables, the area's music - moves
only when the player's feet cross onto the new decor (event 9,
`Walk_ProbeGround`). `Session::slotShown` carries the state, `decorUnder`
(actor/walk.h) answers which shown soup is under the feet, and `omk-play`
draws every shown slot, refills the walker's soup from both and keeps the
controller across the change - so the Impasse's airlock, which used to go
black, is walked through. `verify.py: engine: airlock walk`.

> **A witness for the frame unit, from the content side.** `Grid.SCX` — the
> scene AREA 118 makes resident, the title sequence's own — holds an object
> named **`Wait5sec`** whose single function is `Wait` with the parameter
> **150.0**. The name says five seconds, the number says 150. Somebody
> authoring content wrote the engine's time unit down: 150 / 30 = 5, which is
> `docs/BOOT.md` §4's `30.0 / fps` arrived at from the opposite end by a
> person who had no idea anyone would ever check.

**`verify.py: engine golden traces`** — the whole oracle, not a tenth of it.

Until now the port was diffed against `intro.log` alone: 42 events, compared
as one whole announcement stream. That works for the opening because it *is*
one entry point. It cannot work for a capture of somebody playing, where the
events come from dozens of scripts running concurrently — so this does what
`goldentrace.py diff` does. Anchor each event on the one slot in the corpus
that could have announced it, replay that slot, and require the prediction to
appear as an ordered **subsequence**; demanding a contiguous block would report
the engine's own interleaving as disagreement.

**1469 events across six captures, 116 anchors, and the two implementations
agree on every one.** The sixth, `fight.log`, was taken to give the actor runtime an oracle and
proved that it cannot have one: combat's only two opcodes are `fight.begin`,
which announces nothing, and `player.become`, which announces to CHARACTERS —
a domain the logger filters out itself. Everything else in a fight is native
code. `tools/sim` and `engine/` are independent readings of the
same VM, and they now decide identically on every script the engine was
recorded running — including the 840-event walk from the apartment to the
restaurant, nine conversations, some thirty areas.

Porting it found two things wrong on the *reference* side, both now fixed
there:

* **`show` caps the work, not the printing.** At its default of 25,
  `trace agreement` was replaying the first 25 of `resto-387`'s 64 anchors and
  reporting "21 agreeing, 1 disagreeing" as though that were the capture. Over
  all of them it is **51 and 6**. Nothing regressed — the number was always
  this, it was just never looked at.
* **the tight index over-included.** Six opcodes have a field map but no
  section, so they announce nothing and were contributing anyway; and op 152's
  section is **`JINGOFF2.ADP`** — a filename, out of the unbounded handler
  block `CLAUDE.md` §1 warns about. 99 extra pairs in the index and **two
  false mismatches**. Both sides now read `tables/vm_announce.json`, so
  neither can drift from the other.

That second fix is worth its own sentence, because the evidence for it came
from the simulator rather than the argument. Dropping those opcodes took the
sim's announced total from 45 to 44 on the area load and 70 to 68 on the
tutorial walk — while the counts **confirmed against the engine** held exactly
at 42 and 51/56. What went was events the engine never logged.

**Indexing the conversation scripts is not optional.** The 612 branch scripts
are never replayed — a conversation runs in the dialogue UI, not the script
pump — but a pair a conversation could also announce is *not unique*, and an
index that cannot see them calls it unique and anchors a world slot that may
not be the emitter. They cost exactly two false anchors on `resto-387`, which
was the entire remaining gap between this and the reference.

**One of the six was closed by the port, and two more are now explained.**

Modelling `ui.open`'s park closed `AREA 157 rec 60 +4` — it opens screen 4,
the LIFT, at instruction 3 of 37 and branches on variable 496, so without the
park the replay ran the remaining 33 instructions and predicted *both* arms of
the floor switch. Six became five, the reference was taught the same thing, and
nothing else moved.

Of the five left, **`AREA 179 rec 47 +4` and `AREA 217 rec 13 +4` are the same
eleven-instruction save point twice**: `var.set.actor_stat(-1, 5, 60)` writes a
live actor stat into variable 60, and a `cmp.gt` opens screen 30 — the save
panel — when it is high enough. Opcode 86 is stubbed (it reads the player
record; a replay has no actor), so the replay walks the `media.play` arm the
engine never took. Forcing variable 60 high makes both park at `ui.open` and
predict exactly the two events each was already agreeing on. **A named missing
input, not a decision difference.**

The remaining three are branch-on-variable scripts where the replay takes a
different arm, and they follow the state rather than the script — which is what
makes them anchoring artifacts rather than the port deciding differently.

**`verify.py: engine render`** — the render *decisions*. Not a rasterizer, and
deliberately not: the plan drops the standard to "read and explained" at the
pixel because there is nothing to diff pixels against. Everything upstream of
the pixel is a decision the shipped data can fail, and all of it is one 14-bit
number.

**What draws** is a single test, `flags & 0x800043`, guarding every call to
`Render_SubmitMesh`. It replaces three separate viewer heuristics and
disagrees with them in *both* directions: bit 0 alone accounts for every
skipped PERSOS mesh — no shape test needed — and 3 decor meshes the viewers
draw the engine does not.

**The key's two halves have to be independent**, and that is the invariant
worth asserting: the state bits `Render_SubmitMesh` can set and the six bits
the texture slot occupies must not intersect (they don't), 58 slots must fit
in six bits, and together they span 0x3FFF. The material field the key reads
is a *runtime* field, which the data proves by shipping `-1` at +64/+68 in
**2534 of 2534** materials — one shipping a real value would mean it is
authored and the whole reading is wrong.

> `meshStateBits`'s first line is an **assignment, not an OR**. Flag 0x10000
> is `mov esi, 800h`, so it wipes every state bit set before it. Written out
> as a table of independent bits it would be wrong — and it looks exactly like
> such a table.

**Transparency is two modes**, and the counts pin which corpus is being asked
about: over the 220 sets it is **211 additive / 6 multiply**, the numbers
ASSETS §4 quotes; over all 635 models 398 / 7. **0** meshes ask a sub-mode
without 0x1000.

### The texture cache, run rather than described

`Tex3DT_BindMaterials` keys the 58-slot pool on the **texture file name
alone**, nineteen characters, and on a hit `fseek`s past the incoming file's
own pixels. The pool is global while `Area_LoadSet` keeps two decor sets
resident — hidden is not unloaded — so an incoming set's materials cache-hit
against the *outgoing* location's atlases.

The reference asserts the hazard: these names are shared, those pixels differ.
`TextureCache` executes it. Load AImpasse, then Anekbah, the way walking out
of the Impasse does:

```
AImpasse alone takes 8 slots; Anekbah then gets 7 cache HITS and 13 loads
  substituted: BATITR06 BATITR12 BATITR10 BATITR20 BATITR19 BATITR16 BATITR09
```

Seven of Anekbah's materials now point at the Impasse's pixels; their own were
never read. `BATITR12` — 12.1% different, and the atlas the wrong sign panels
sample — is one of them. `traces/impasse-walk.log` announces `AREAS 222` then
`AREAS 0`, which is exactly that walk.

Note which side this puts in the wrong: **the viewers are right and the game is
the odd one out**, so a replica that wants to look like the game has to
reproduce this, not correct it. Watching it happen in the shipped build is
shut off — the `cached texture :%s` call goes to `Dbg_Printf`, which is
`nullsub_1`, a one-byte `retn`. This is the way to watch it.

> **A fault found on the way, in a standing check.** `drawable mask` globbed
> `gamedata/MESHES/PERSOS/*.3DO` case-sensitively, and **12 of the 193 PERSOS models
> ship `.3do`** — so it had been counting 3517 meshes of 3730 and 511 skips of
> 547. The port went through `DataFs` and disagreed. Same class as the `.SFX`
> bug, this time inside a test rather than in a reader.

**`verify.py: engine cull`** — the visible-set walk, and it is worth being
exact about how much of it is ported.

`cullMesh` **is** `sub_48D3B0`: the hidden bit, then one distance compare,
then four dot products, in that order, over a flat node list with no portal
walk and no PVS. What builds the four planes is **not** ported. The engine
calls `sub_442FB0` four times with three points each, writing into
`flt_660AD4 … flt_660B18` struct-of-arrays — which is why the walk reads that
range as one block — and *which* three points each call uses is unread.
`makeFrustum` is a fixture standing in for it, labelled as one, and no
invariant asserted about the fixture is an invariant about the game. (The
first version of this check asserted that widening a fixture frustum can only
keep more meshes. It doesn't — four of Anekbah's are dropped by margins of 84
to 457 units — and that is a fact about the fixture, not the engine.)

**The bounding volume is identified from the data**, which is what makes this
more than a transcription: the radius at `+88` equals `max |v|` over the mesh's
own vertex block in **12039 of 16176** meshes, and **11745 of DECORS' 12191**.
A field that was not the radius would not do that. The centre is `pos` at
`+36` — so what the mesh reader calls the position *is* the sphere's centre.

The residue is now **characterised** rather than merely bounded — and one
earlier statement of it was wrong. 3776 meshes store a radius smaller than
their extent, and **361 store a larger one**, which the first count missed by
only tallying one direction.

Six hypotheses were tested against the 446 decor cases and all failed: the box
diagonal (7), its largest half-extent (6), the whole vertex block, only the
vertices the mesh's own faces *reference* (0), resolving through ancestor
meshes (0), and another mesh's block in the same file (15 of 85 sampled). What
*is* established is that the parse is sound: **every file's per-mesh vertex
counts sum exactly to its header total**, so the running base is right and this
is a fact about the authored data, not a reading error.

What it looks like is a **stale authored value** — computed once and not
updated as the mesh was edited. The distribution fits: the median miss is
**0.937** of the true extent with a range of 0.057..1.094, so most are slightly
off and a few wildly; 140 of the 446 share a radius with a sibling in the same
file, which is what copying a prop does; and it touches 94 of the 220 sets
rather than clustering in one.

The consequence is stateable rather than cosmetic: a sphere smaller than its
geometry culls the mesh **early**, so those meshes can pop at the screen edge;
a larger one merely wastes a test.

**What is asserted about the cull needs no oracle**, because there isn't one:
no capture records a visible set and no reference reader implements the walk.
So the tests are invariants the code can fail —

* `planeThroughPoints`, the engine's own builder, puts its three points on the
  plane it returns and a fourth off it;
* the far test is monotone: reaching further can only keep more. That one *is*
  the engine's, `(r + far)² > |c − eye|²`;
* **a mesh containing the eye is never culled.** Every plane passes through
  the eye, so `|n·(c − eye)| ≤ |c − eye| ≤ r`, and the distance test cannot
  fire either. Standing inside a wall must not cull it.

The far distance is a runtime global this port has **not** established, so it
is set out of play and the survivor counts measure the four side planes alone.
Anekbah's three authored cameras keep **95, 0 and 84** of its 860 meshes — and
the 0 is content, not a fault: that camera sits 20000 units away and 5000 above
the set, looking sideways. "Every camera sees something" was asserted first and
was simply wrong.

### The frustum, finished

The cull's plane construction is now `sub_48D0D0`'s, not a fixture. The engine
**does not build planes from angles**: it takes the four corners of the view
rectangle *at the far plane*,

```
(±far · w / (2·projX),  ±far · h / (2·projY),  far)
```

— viewport size the uint16 pair at camera `+0x19C`/`+0x19E`, projection scales
at `+0xE4`/`+0xE8` — transforms them to world space, and hands the eye plus two
adjacent corners to `sub_442FB0` four times, into `flt_660AD4 … flt_660B18`
struct-of-arrays. **The two axes are scaled independently; there is no
aspect-ratio term anywhere.** `far` is `dword_6A2B9C` from the camera's
`+0x154`, and the near plane `flt_6A2BBC` from `+0x144`, both settled by the
range test that rejects a vertex with `z ≤ near` or `z ≥ far`.

That last detail was paid for. The earlier fixture derived the vertical
half-angle from the horizontal one through an aspect ratio, and under it four
of Anekbah's meshes were kept at 45° and *dropped* at 90°, by margins of 84 to
457 units. The invariant "widening the field of view can only keep more" was
withdrawn as untestable. **It was true — the fixture was wrong**, and with the
engine's own construction it holds at 0 violations and is asserted.

**`verify.py: engine actor`** — the actor runtime's `.CTL` half, which is the
data the fight AI already ported actually drives: a profile injects input words
into the actor's queue, `Cef_FindTransition` matches them against the entries'
`+4` codes, and a landed hit resolves through the combat block.

**The combat block is ten floats, three of which are integers wearing a
float's bytes.** Reinterpreting rather than converting is the point, and the
corpus says so: damage runs **1..25** over 116 blocks and the two reaction
references resolve in **232 of 232**. Converting would give 25.0f, which is not
25, and ids that resolve to nothing.

A reaction names a state by its **low sixteen bits**, which works only because
that id space is collision-free — **0** collisions in any file, a property of
the content the format does not enforce. And the role codes are content too:
`Fight_Begin` caches six (3, 4, 5, 9, 18, 20), every combat file carries
exactly one entry of each and every non-combat file none — **7 of 7**. A wrong
offset would not produce that shape.

Transitions: **1286** entries, 640 with an input bitfield, **28** of them the
`0x80000000` no-input sentinel — which is *not* the queue's idle word
`0x40000000`, since `Cef_InputMatches` ends
`if (a1 == 0x80000000) return a2 <= 0x2000`. **168** carry a cancel window,
**0** malformed, and the priority at `+84` only ever takes 0, 1 or 2. **202
groups, 202 with exactly one default entry** — which is what makes "the default
entry" well defined for the runtime to fall back to.

Not ported from this row: the camera-mode presets, which are already in
`tables/`. This is the data half; the machine that reads it is the next
section.

**`verify.py: engine actor states`** - `ACTOR_STATE` 0..17 RUN, and the `.CTL`
channel underneath it: `Cef_TickChannel`, `Cef_FindTransition`,
`Cef_InputMatches` and `GoToMove`, driving 202 groups of the seven banks with
the fourteen key bits and with the files' own AI moves. **395352 ticks, 34056
states landed in, 0 unresolved and 0 chains that did not terminate.** **Since 2026-09-02 (T17) the channel's commit is `Cef_TickChannel`'s whole**: the lone-idle drop and the `0x20000000` cut, and `GoToMove`'s dynamic return edge read as well as written - the sweep runs 26093 edges and 55182 landings against 12063 and 34056 before, and the numbers this paragraph quotes are the new ones.

**The standard for this row is data-constrained, not engine-verified**, and
that is a property of the subject rather than of the effort. Every other
runtime here is checked against something the original produced. This one
cannot be: the logger sees only what a VM handler narrates through
`Dbg_LogTagged`, and combat is two opcodes - `fight.begin` announces nothing,
`player.become` announces to CHARACTERS, which the logger filters out itself.
`traces/fight.log` was taken to test exactly this and settled it the wrong way
round: the capture reached combat, 32 of its anchored scripts carry
`fight.begin`, and it says nothing about any of it. So the warranty is what
the shipped data can falsify, and the whole of it is:

* **every ACTOR_STATE transition is one the binary writes.** The table was
  enumerated twice and the second pass is why it is right: a scan of the
  assembly for stores to `[reg+194h]` finds most writers and **misses
  `Fight_Engage`**, which writes through the `dword_910834[328*index]` alias
  and is **the only writer of state 2 in the game**. A state that the dispatch
  names and nothing writes was the tell - the same shape as the `+4` startup
  script, where the enumeration rather than the search was short;
* **the table is enforced.** Three deliberately illegal transitions per file
  are refused - 21 of 21 - which is what stops it being a comment;
* **the park round-trips, and 16 and 17 are not alike.** 42 of 42, and
  collapsing the two into one moves that to 35/7;
* **every committed edge is re-derived from the file** by a second copy of
  `Cef_InputMatches` written from the documentation rather than from the
  runtime: **12063 edges, 0 not opened by their own `+4` code**;
* **232 of 232 reaction references** resolve uniquely in the low-16 space, and
  **0 of 1338 AI input words** fall outside the 0xCFF union;
* the per-state **dispatch** - which tick function each state reaches and
  whether a channel runs in it - is a differential between two transcriptions,
  **119 states checked, 0 disagreeing**, plus the **7** (one per file) with no
  case in `Actors_TickAll` at all, which is state 7 and is content.

**One of these checks was circular in its first version, and it is worth
saying which.** The dispatch row originally ticked each state and asserted the
channel advanced iff the table said it would - which is precisely what
`ActorRuntime::tick` reads that flag to decide, so flipping a row passed. It
now checks the per-state tick function against a second transcription, written
from `readable/src/21_d3d.c`, of which of those ten functions reaches
`Cef_TickChannel`; flipping a row now moves it from 0 to 7. Every other
assertion here was likewise checked by breaking it first, which is the only
reason a table of zeroes means anything.

**And one thing this cannot decide, said out loud because a silent pass is
worse than a gap.** `Cef_FindTransition` takes the first matching child when
ungated and the highest-priority allowed one under the gate. Of **9103** gated
decisions **120 are a genuine contest**, and in **0** of them is the first
match not also of maximal priority - so replacing the priority rule with a
plain first-match rule changes not one of the 12063 edges, and the corpus
cannot tell them apart. The rule stands on the function's own code. What is
asserted is the corpus property that makes the difference invisible, which
could have been false and is not.

**Three findings came out of pinning the writers rather than trusting the
offset search**, and they correct the state map this repo has been carrying:

* **state 14 is the WATER state.** Its writers are `RSTNAGE` (0x0046C150 -
  *nage*) and `MDDIVEND` (0x0046BEF0), and `RSTAVNT` (0x0046C120) and
  `MDSW2SD` (0x0046BF20) write 1 back. All four are `tab_special_move[]`
  handlers, so the `.CTL` machine changes `ACTOR_STATE` **through its own
  callbacks** - which is also why none of them has a `proc` label to find them
  by. The addresses close arithmetically against the table's next row, which
  is what makes them safe to name;
* **7 and 8 are the two halves of one slider ride**, not "riding" and
  "channel-only". `MDSLIDIN` (0x0046B7F0) writes 7 and opens screen 7;
  `sub_468FA0` puts the actor on **`.CTL` group 61** and writes 8 to both
  `[101]` and `[102]`; and `MDSLIDOU` (0x0046B890) **refuses to dismount from
  anything but 8** - "bad mode getting out of the slider !" is its own error
  string. Group 61 is not in the group table `docs/ASSETS.md` lists;
* **state 7 has no case in `Actors_TickAll`.** It falls through `default:` and
  gets no tick from that loop at all. An absent case is content, not a gap in
  the read.

**`verify.py: engine player walk`** - the player controller, adventure mode.
The chain `Actor_TickNpc` runs is joined from modules already ported: the
word, the channel, the clip's root delta rotated by the facing (`node+156` is
`actor+288`, the facing matrix - `Anim_RootDelta`'s "optional 3x3"), the
walker. Four replayable input streams on `AIMPASSE` from ADDRESSES[654]: UP
walks 69.5 units in 60 frames along the facing and the machine goes
`H_STAND -> H_SD-WK -> H_WALK -> H_WK-SD -> H_STAND`; LEFT turns 85 degrees
without walking; LEFT then UP walks the new way; nothing held stays within
0.1 units. The follow camera sits 118.11 behind at fov 75 - the mode-0 preset
row, with the target chasing at 1/3 and the eye and yaw at 1/8 per frame
(`sub_415D10`/`sub_415E60`, quoted in `player.h`). **Tier 5**, no oracle,
and one mutation the corpus cannot separate: 0 for "nothing held" passes,
because `H1AVNT`'s stop edges open on it as on the idle word. Six others
bite.

**`verify.py: engine actor states`** — since 2026-09-02 the sweep also drives
the two queue rules of `Cef_TickChannel`'s commit, which it cannot reach any
other way: it injects whole queues, and `injectInput` replaces the queue, so a
lone idle word never stands in front of a press there. Seeded the way
`SetPersoBankGroup` seeds it — one word, the idle one — a press is acted on
this tick in **53 of 53** banks and parked behind the idle word in **0**;
remove the rule and all 53 park, the sweep falls back to 12063 edges, and the
player stops walking. The truncation rule (entry flag `0x20000000`) fires 4088
times in the main sweep without moving one of its numbers, so it is asserted on
the 9 entries a designed case can reach: 3 cut, 0 not.

---

---

**`verify.py: engine text draw`** — `Text_DrawRun` ported, and its glyph
pixels checked against the original's own framebuffer.

The layout half was already here. This is the rasteriser, and the colour model
is the whole of it: a glyph byte is a **coverage level 0..31** into a 32-entry
ramp of the text colour, rebuilt when that colour changes, with zero
transparent. Read from the assembly rather than the docstring — the builder
runs 32 entries across 0x40 bytes, carries one accumulator per channel and
divides by 31 with the `0x08421085` magic-multiply, so the division
**truncates**.

**6132 of 6132 painted pixels are identical to the engine's frame**, on all
four start-menu labels, with the horizontal offset zero on every one. The
colours were read out of the frames rather than assumed: white for the focused
row, `0x7F7F7F` for the others, which through the ramp gives 565 `0x7BEF` and
displays as (123,125,123) — so the focus highlight is a brightness and the
glyphs are identical.

Both mistakes on the way were caught by the frame. The glyph record's `bottom`
is relative to the baseline and **adds** — subtracting it put every row eight
pixels low. And aligning by a thresholded ink box gave 45% agreement and
per-band offsets of −7/−7/−9/−9, because different letters have different
ascenders; searching the offset gives 100% at every one. Rounding the ramp
instead of truncating costs 95 pixels, and treating the coverage byte as an
intensity collapses it to 529.

**`verify.py: engine frame`** — the engine's own FRAMEBUFFER, and the text
renderer checked against it.

`goldentrace.py` gave this repo the original's decisions. **`goldentrace.py
capture` now gives it the original's pixels**: the game runs under CrossOver in
a Wine virtual desktop, `screencapture` grabs the window, and `frame.recover`
takes the 2× Retina capture back to the game's own **640×480**. Three frames of
the start menu are committed in `traces/frames`, and **nothing in the check
needs CrossOver** — the rig makes an oracle, the file *is* the oracle, exactly
as the `.log` captures already work.

**The recovery is exact, and it refuses rather than degrades.** A 2× capture is
the framebuffer only if the display scaled nearest-neighbour, so every 2×2
block must be uniform — measured, **0 of 307200 non-uniform**. If a future
display interpolates, `recover` raises and nothing is written. A frame that is
only *nearly* the framebuffer would make every pixel diff built on it quietly
wrong while still passing, which is this repo's recurring failure and worth
one explicit refusal.

**What a menu frame is deterministic about, measured rather than assumed**: the
title bitmap is pixel-identical across all three captures, and so are all four
labels' glyph pixels — mask *and* values — while the frame as a whole differs
by **52%**, because the tile-map background animates. So a check may assert the
text and the title, and must not assert the scene.

**The labels are found by SATURATION, not brightness.** The menu draws text
through a neutral grey ramp over a strongly coloured tile map, so near-grey
separates glyph from scene where bright does not — and it excludes the orange
title too, which is what makes the band count come out at exactly four.
Dropping the saturation rule gives five. Their positions are then *derived*
from the frame, so a check that drew them elsewhere would fail rather than
pass.

**The result.** Each label rendered by `tools/uitext.py` in **MENUINTR** —
font `I`, the face the compiled font table names — matches the engine's own
pixels at an **IoU of 0.998 or better, two of them exactly 1.000**. That is the
text renderer verified against the original's output rather than against a
second implementation of the same reading, which is the distinction CLAUDE.md
§5 says has already cost this repo twice. Rendering in the wrong face collapses
it to 0.15–0.22.

**And the focus highlight reads out as a number**: the focused row's ramp peaks
at **255** and the other three at **124**. The glyphs are otherwise identical,
so the highlight is a brightness, not a second font.

**What a frame is and is not evidence about**, because everything built on this
depends on it: it is CrossOver's rasterisation. **2D is exact** — I2D ends every
primitive in a `Blt`, which is a memory copy with an optional colour key, and
there is no filtering to differ. **3D is exact about geometry and ordering and
not about the low bits of a pixel** — texture filtering, dithering and the fog
table are the driver's, and Wine's are not a Voodoo's. Anything built on top
should say which of the two it leans on.

**`verify.py: engine I2D`** — the 2D compositor's display list, its pools,
its acceptance tests and its three flag banks.

The DirectDraw back end is not ported and cannot be from here: there is no
surface, and `Blt` is on the far side of the same line as the six x87
rasterizers. What is ported is what the layer **decides** — which primitives
are accepted and in what order they would be drawn — the same line the
renderer port draws, where the bucket key and the texture cache are run and
the triangles are not.

**One check touches shipped data**, and it is the only one the game could
contradict: the widget tree carries **139 flag constants**, 87 applied by an
item and 52 broadcast by a list, and `I2D_SetFlag`'s three-way `if` **silently
drops** a constant that names no bank. All 139 resolve, splitting 42 / 95 / 2
across the three words, and breaking the 0x40000000 test takes that to 44. The
other numbers are invariants of the transcription — worth running, since one
of them found the ordering below, but not evidence about the original.

**The 4862 is derived.** The docs quoted the display list's node cap as a bare
number; it is **exactly the sum of the seven pools** — 4096 + 200 + 100 + 220
+ 220 + 16 + 10 — so the list can never fill before the pools do. That
identity only closes if the **dead** pool is counted: `sub_428660` (cap 10)
and `sub_4286F0` have no callers, and the second carries a latent overflow —
it checks the bitmap counter (cap 220) and writes into the 10-entry pool. It
never fires, and the port keeps it rather than quietly correcting it.

**The ordering, which the docs had wrong, and which no screenshot could
settle.** `docs/UI.md` called `dword_4E97B8` a per-layer *tail* cache. It is a
per-layer **head** cache: the assembly writes it on both exits of the list
walk (`loc_42850C`, `loc_428524`) and not on the cache-hit path
(`loc_428534`), which only does `new->next = cached->next; cached->next =
new`. So **within one layer the first primitive submitted draws first and the
rest draw in reverse submission order.** Across layers the list stays sorted,
which is the property the layer exists for. A real tail cache scores **724 of
1407** against the prediction instead of 1407 — it is right about every layer
boundary and wrong about every overlap inside one.

**A second shape the check found rather than assumed.** The frame's very first
node takes `I2D_Enqueue`'s `if (!count)` arm, which sets the head and does not
set the cache, so the second node on that layer goes through the walk, is
inserted *before* it, and becomes the cache. The first version of the check
predicted only the ordinary shape and scored **1260 of 1407**; the 147 misses
were exactly the layers holding node 0. Both shapes are predicted now and it
is 1407 of 1407.

**And a missing test that is not a bug to fix.** Both blits reject a
degenerate rectangle with **three** comparisons, not four: `dst.x0 >= dst.x1`,
`dst.y0 >= dst.y1`, `src.x0 >= src.x1`. **There is no test on the source Y**,
identically in both, so a source rectangle inverted only vertically is
accepted and submitted. Adding the "missing" fourth takes the count from 2 to
0, which is why it is asserted rather than tidied away.

**`verify.py: engine shoot AI`** — the four callbacks `Shoot_ActorEnter` picks
between, and a documented negative that did not survive being looked at.

The dispatch is a `switch` on the character type (property 7, record `+176`):
**7 X-Tech → `nullsub_9`, 10 Gandhar → `sub_47F6F0`, 13 Astaroth →
`sub_4800C0`, anything else → `sub_424DE0`**. The docs recorded the subsystem
as having "no data at all". That is true of the dispatch and **false of the
AI**: Gandhar plays **three behaviour scripts compiled into the executable**,
through two twelve-entry handler tables, and the fourteen character types are
a name table the binary carries. All of it is now in `tables/shoot_ai.json`,
and the five tables **chain end to end** — 0x004CFA30 types, then the healthy
(14), wounded (10) and critical (13) scripts, then the enter and tick handler
tables, the last of which ends exactly on the string the first points at. A
wrong base address could not produce that.

The negative was about the `switch` and got generalised to the subsystem,
which is the shape CLAUDE.md §1 keeps warning about: a negative result is a
fact about the hypothesis, never about the field.

**What the shipped data says, and it is what decides how much each arm is
worth:** **1032 character records, 330 of them carrying type `0xFFFFFFFF`** —
so `default:` is load-bearing, not defensive — and **317 `shoot.actor.enter`
sites, 306 resolving in their own chunk**, splitting **302 generic / 3
Astaroth / 1 Gandhar / 0 X-Tech**. **No shipped character is type 7 at all**,
so `nullsub_9` is unreachable content, like the six spell recipes whose gate
is never 8.

**Gandhar is the one arm where running the port and reading the data are the
same act**, because he is table-driven: all three scripts walked twice round,
**144 steps, 0 disagreeing** with the table expanded by its own repeat counts,
and the health band re-read every frame with a change **restarting** the
routine rather than resuming it.

**The standard here is lower than `engine: actor states`, and deliberately
visible in the numbers.** Only the tables and the dispatch touch shipped data.
Astaroth and the generic shooter are code with nothing behind them, so they
are ported as their state **graphs** — Astaroth's 16..21 / 27 / 29 with his
own 195/273/156/78 unit distances and 150/60 frame timers, the generic's
1..15 / 28 with the five-way sub-switch inside state 6 — and **not** as the
1500 lines of per-state geometry that decide where to stand and where to aim.
The only thing asserted about them is that the runtime never leaves the state
set that was read: 0 of 2000 ticks. That catches a port that wanders and
nothing else, and it is §3's "read and explained" rather than "verified".

All three real checks were confirmed by breaking them: resuming instead of
restarting on a band change moves the reset count to 0, dropping the Astaroth
arm moves the record split 2/2 → 0/1030 and the site split 3/302 → 0/305, and
misspelling one type name moves 14 → 13.

**One thing that looked like AI and is not**, recorded so nobody chases it
again: `Shoot_ActorEnter` also installs a table at record `+84` — 0x004C3798,
and 0x004C37E8 for Astaroth. `sub_434C30` hands it straight to the poser
`sub_471950`, so it is an animation node mask. Located, not lifted; nothing
here poses a skeleton.

**`verify.py: engine UI`** — the widget tree, walked with the engine's own
input words. **31 screens, and this and `tools/sim/ui.py` agree on every one.**

Nine of the coverage rows below are interface, and I had listed them as having
no oracle because pixels cannot be diffed. That was too quick: the simulator is
deterministic and reads the same records, so the *logic* has an oracle even
though the drawing does not — the same way it did for the scripts.

**The tree had to be lifted first.** A panel, its lists and their items are
`.data` in `Runtime 2.exe`, not in `gamedata/`, so `tables/ui_widgets.json` now
carries them — 44 panels (31 screens plus 13 children), 125 lists, 572 items —
for exactly the reason the VM opcode table is carried, and `exe tables`
re-derives it so it cannot go stale.

**The tree links downward through the item — and a first version of this got
that wrong.** `panel+0` is the parent, so "top panels only; children need
native code" went into the docs as a limit of the format. It isn't one:
`Ui_ConfirmSelection` descends into an item's `+44` when it has no `+40`
callback, and following those transitively reaches **7 child panels** — the
start menu's confirm dialog and its name field among them. The name-field hook
I had recorded as unreachable is simply one level down. Also worth knowing: the
28 screens share only **13** distinct top panels, so a record per screen is not
a record per address, and de-duplicating by address leaves 15 screens unable to
look themselves up.

What still needs native code is a panel a *callback* installs rather than a
`+44`, and the answers those callbacks write.

### The SNEAK — the first screen the PLAYER opens

**`verify.py: sneak chain`, `engine: sneak`** (2026-09-04). Every screen the
port could open before this came from a script: `ui.open` parks its caller and
the Session says which screen is waiting. The sneak — Kay'l's handheld device,
screen 9 — comes from the player's hands instead, and the chain is entirely in
the shipped data:

```
TAB                    key_bindings group 0 action 13 "Ouvrir sneak", bit 0x2000
-> H1Avnt/F1Avnt.CTL   group 0 entry, +4 = 0x00002000, flags bit 2 = alias
-> GoTo group 6        H_SNKON, that group's flag-0x20 default
-> its child           flags 0x25000013 - bit 0x10 names a move
-> tab_special_move[0] "MDSNEAK0" -> sub_0046ADF0: event 25, then screen 9
```

Four things had to be built for it, and three of them are not about the sneak:

* **`tab_special_move` had no consumer at all.** `CefChannel` has been
  emitting `ChannelEvent::Kind::Move` with the entry's move name since it was
  written and nothing read it, so every one of the 66 rows was a no-op.
  `PlayerController::specialMoves()` now reports them and `actor/moves.h`
  resolves a name to its row; 65 of the 66 are still named-and-not-run, which
  the log says once each rather than silently.
* **The sneak family entered the widget tree.** `Ui_OpenSneakFamily` installs
  a different panel on each arm of a `+4` branch, which a linear scan cannot
  attribute — the same shape as `Ui_OpenShop`'s titles. Screens 0, 7 and 9
  and six child panels joined, and with them `panel+24` (the current list),
  `panel+72` and `list+2` (the selection) wherever an open callback writes
  them: 15 panels and 8 lists do, the ten shops among them. Both walkers now
  prefer the callback's value and fall back on `Ui_MoveBetweenLists`'s rule
  only where the engine really does.
* **Two "unmodelled hooks" were generic movers.** `sub_42A5C0` and
  `sub_42A7E0` take their two direction bits as PARAMETERS, and the default
  dispatch is just `sub_42A7E0(…, UP, DOWN)`. `sub_42A710` is the first bound
  to LEFT/RIGHT as a panel hook and `sub_42A930` the second as a list hook —
  the sneak's tab column and its verb bar. `tools/sim/ui.py` had transcribed
  the first as `_move_lists` and **never called it**, because until now no
  panel in the tree named the hook.
* **The inventory channel got its first consumer.** `script/inventory.h` was
  written, checked and run by nothing; the sneak's nine rows are filled
  through it — `IAM\OBJECT` and `IAM\GLOBAL` read out of `gamedata/`, event
  25 opening object list 0 the way `sub_0046ADF0` does, event 26 closing it.

Two bugs in the frame loop fell out of it, both the same mistake: a line
gated on `adventure`, which a screen over the world takes FALSE in the same
breath. `Ui_BeginScreen`'s 0x203F repeat mask stayed at the world's 0, so
every held key repeated every frame; and the `--hold` stream stopped feeding,
so a headless run could press TAB and then nothing.

**What a PLAY REPORT then found, and it was four faults in one drawer**
(2026-09-04). A player listed four complaints and every one was a field the
widget lift had never carried, so the composer was substituting a rule of its
own for a rule in the binary:

* **`Ui_DrawItem` never reads `+28`.** Text is `+24` or the `+32` callback;
  an item with both zero draws none, and **111 of 572 items are that**. The
  sneak's tab icons are among them - "the menus are essentially icons".
* **the icons are `Ui_DrawItemSprite`**, 233 items, none ever drawn.
* **the flash is `Ui_Oscillator(1)`**, a 500 ms square wave on a MILLISECOND
  clock; and the sprite and text ladders are NOT the same ladder, which this
  repo's own docs said they were.
* **`Ui_DrawPanelBack` scales the destination and not the source** - the
  reason the device did not fill an 800x600 display, and a fault no 640x480
  check can see.
* **`item+36` is the font** - identified in the docs years of sessions ago
  and never lifted, so every screen drew in one hard-coded face. MENUINTR has
  no Latin glyphs, so the sneak was rendering the right strings through the
  wrong alphabet.

It was not a sneak bug: the LIFT's seven floor buttons are sprite items too,
and the port had been printing seven labels over seven icons there as well.

**And the clock found a bug outside the interface entirely.** The sneak's
clock row is the first thing in this port to put the time on screen, and it
read day 0 against a save the loader had just printed a real date for: the
clock is an engine global and NOT in the DB image, so `omk-play` had been
loading every save at 00:00:00 since saves were loadable. Nothing noticed
because nothing drew a clock. `verify.py: save clock`, and the corroboration
is that `traces/save-appart.bin` restores to 12 Nadim 7216 14:14:17 while the
player's screenshot of the ORIGINAL shows 12 Nadim 7216 - 13:01:15: the
fixture and that screenshot are one play session.

**What is NOT done, and is visible in a screenshot rather than a number.**
`Text_LayOutBlock` (0x0043F3E0) wraps a row's text inside the item's own box;
this composer draws one unwrapped line. It stopped biting when the captions
stopped being drawn at all - they were never text - but it is still there.
`sub_0049C050`, the inventory list's SCROLLING hook, is unmodelled, so the
page shows the first nine carried items; `sim: ui coverage` counts SLIDER and
SNEAK as refusing for that reason, and it is an HONEST refusal rather than a
gap to close: the hook's behaviour depends on the object COUNT and the widget
walker has no game state to ask.

The three 50x50 items are live `.3DO` MODELS - `setek`, `anneau` and `imager`,
loaded by the screen's own open - which rotate to show selection. The UI layer
has no 3D path, so they are not drawn; their LABELS and the two counts are,
on the echo bar, which is where the engine puts them.

**`Ui_DrawItemCursor` IS PORTED** (2026-09-04), and it was a play report that
made it worth the read: "the hovering effect is absent so it is very difficult
to know where I am". `sub_479920` drives ONE global pool of SIXTEEN elements
that chase the focused item and orbit it, and the pool's layout closes exactly
on the 220 dwords an earlier pass had measured - 0x30 + 16 x 0x34 = 880 - so
the parse could have failed and did not. Five functions read: the rebuild, the
two eases, the orbit and the renderer, which submits one quad per element at
blend mode 4, the same inverse blend as the fill.

The number that decides the look is the ALPHA, and it is oscillator **3** -
period 500, 230..235, a triangle - not oscillator 2's 45..200, which the docs'
table made an easy wrong assumption. Under the inverse blend a high alpha is a
faint quad: sixteen at ~9% leave the bar near 80% of the source. At 45 it
saturates, which is the mutation the check is shown to fail on.

Tier 5 - every number read, none checked against a capture, because a still
frame cannot judge sixteen eased elements. What is asserted is the property
the player missed: the focused row paints (156, 85, 0) where the plain fill
paints (49, 28, 0). It is ATTACHED rather than owned by the composer, so
`engine: screen`'s hashes stay a pure function of the screen.

**`Ui_DrawItemFill` IS PORTED, and it took four attempts and a picture of the
original.** The blend is the inverse of source-over - `sub_480AC0`'s mode-4
arm sets SRCBLEND = INVSRCALPHA and DESTBLEND = SRCALPHA, so
`result = src * (1 - a) + dst * a` and a LARGE alpha makes the quad faint.
Every earlier attempt drew it the usual way round, four times too bright, and
was rightly backed out.

What settled it was a screen whose fill colour is not the placeholder. **209
of the tree's 222 fills carry (255, 0, 0)**, so most screens cannot test the
rule; the LIFT's description panel carries (80, 122, 118) over black artwork:

    the rule predicts                          (17.3, 26.3, 25.5)
    a player's screenshot of the game measures (15, 25, 25)
    engine/ composes                           (16, 24, 24)

`verify.py: fill colour`, and it is the first UI number in this port checked
against a picture of the ORIGINAL rather than against the port's own
reference. The per-row gate went in with it - `sub_42AAE0` sets `0x40000001`
on every row widget past the object count, so the sneak fills one bar and not
nine, which is the shape the original has.

**And the placeholder is ANSWERED** (2026-09-04). Two setters write
`+8/+9/+10` at run time - `sub_4296B0` on one item, `sub_4296D0` on every
item of a list - and **21 of their 23 list-wide call sites sit in a function
with no `proc` label**, because every one is a panel or list callback, a
dword in the widget tree. That is why nothing found them: a search through
named functions cannot.

What they are handed is a **tab icon's own colour**. The sneak's five pages
share one icon column (list 0x004DE210, all at x = 15) whose five records
carry the five page colours, and each page's `panel+4` builder pushes the
bytes of its own icon by address. The inventory page's builder, 0x0049B710,
ends by colouring the rows, the verbs and the echo bar from icon 0x004DE040,
(240, 135, 15).

Confirmed against five captures of the original: the inventory page draws
amber, the slider page green, the identity page blue, each matching the icon
beside it. Over 15 channel samples from 3 hues, `measured = 0.19 x source +
11` against a predicted slope of 55/255 = 0.216 (source-over would be 0.784).
`UiWalk::buildPage`, `verify.py: sneak page colour`. Only the inventory
page's builder is ported - the one page the port opens, and the one whose
function boundary is established rather than inferred from the listing order.

### The trap, which this port walked into

The open callback's `I2D_SetFlag` calls are recovered by scanning its bytes
linearly — and a callback has **branches**. The ten shop screens grey item 0
and enable item 1 on one arm, and the reverse on the other, so a linear scan
sees `off, off, on, on` for one item and `on, on, off, off` for its neighbour.
Applying those in address order picks whichever arm comes last, and made item 0
unselectable on all ten screens. That was exactly where the two implementations
disagreed — 10 of 28.

**20 items carry a flag written both ways**, on exactly those ten
screens. Such a write is *conditional* and a linear scan cannot resolve it, so
it must not be applied; refusing them leaves the static record standing, which
is what the reference does by reading `+48` raw. Cruder, and right for the same
reason. This is `CLAUDE.md` §1's "a regex must respect nesting" one level up: a
linear byte scan over a branching function.

`UI_GridMenuInput` is ported — the LIFT's floor panel, the one list hook in the
image with a single reference — which is what makes screen 4 answer at all:
four DOWNs land on slot 4, and confirm answers `slot - 1` with slot 0 giving 6.

One thing matched deliberately rather than improved: **`Ui_MoveBetweenLists` is
not wired to LEFT/RIGHT.** The reference transcribes it as `_move_lists` and
then never calls it — nothing in the modelled dispatch reaches it. Wiring it
here would manufacture a disagreement, so it is left as the reference leaves
it, and said so in the code.

### The start menu answers for itself

`ui.open` suspends the calling script and a screen answers it. Both the
simulator and this port used to **supply** that answer as a literal, which
tested the suspend/resume mechanism and nothing whatever about the screen.

Now it is walked. Confirm on "Nouvelle partie" descends through the item's
`+44` into the confirm dialog; DOWN moves off the name field onto the buttons —
the dialog's own panel hook moves lists with **up and down**, not left and
right; confirm on "Confirmer" runs the one item callback whose effect has been
read, and it writes **1**, the value the shipped save records for the intro's
`Interface`.

```
start menu with a name typed: answer 1 (approximate: no)
start menu with NO name:      answer -1 (approximate: no)
name field: "Kay'l" is 5 chars, 30 typed caps at 20, backspace leaves 4;
            RETURN on empty refused, on a name accepted
```

**The gate is the interesting half.** That callback's first instruction tests
the name field's cursor and jumps straight to the ret when it is zero, writing
neither the answer nor the screen's state word — so confirming with an empty
field leaves the screen open and the calling script suspended for ever. Walked
without typing a name, the port answers *nothing*, and nothing is the correct
outcome rather than a modelling failure.

The name field is the engine's own switch, read from the compiled jump table
rather than transcribed: 8 backspace, 9 (TAB) and 27 (ESC) ignored, 13 (RETURN)
its own case, everything else inserting. The cap is the buffer itself — twenty
bytes at 0x0069BDA0 — while the save slot that receives the name has room for
32, so the field is the tighter of the two.

**`boot_intro` now takes `--derive-ui` instead of `--var 19=1`**, and the dump
is byte-for-byte what the supplied run produced. The last literal in the intro
is gone.

### The options tree, and the load panel

That is the whole modelled UI surface: five panels, and the port walks all
five.

**Screen 35 is built unlike the rest.** Its "panel" is one of thirteen *page*
records and every page fills the same sixteen row widgets — so what a page
shows isn't in the row, it's in the `Opt_BindRow(row, item, page)` calls its
builder makes. Those are lifted (**191 bindings across the thirteen**) and
walked.

**The same branch hazard, met a second time knowing its shape.** A builder has
branches, so a row bound twice with different items was seen on two arms.
**12 rows are like that**, and the reference documents exactly one —
`Opt_PageRoot`'s row 4, bound to "Retour" only when the screen parameter is 1.
The other eleven have not been read, so the table carries all twelve and
touching one marks the walk approximate instead of taking whichever arm the
scan saw last.

**Page 0 is a trampoline, and missing it looks exactly like a bug.** Every
sub-page's "Retour" binds page **0**, not page 1, and page 0 has no rows — its
builder is `if (dirty) prompt; else Ui_GoToPanel(screen, page 1)`. That is
precisely how this port first disagreed, reporting Retour going to page 0 where
the reference said 1.

The choice cycling is what says the index is stepped *modulo* the list rather
than clamped: `Distance de clipping` driven right six times runs 50, 100, 150,
200, **25** — the wrap — then 50, and two lefts walk back over it.

**The load panel branches on the save directory and on nothing else**, so it is
driven twice, against the shipped `IAM\GAMES` and against a synthetic
one-profile directory:

```
save directory: 256 entries, 0 distinct profiles; geometry 8402344, memset 18432
  empty directory: focus 1, mode 3, 1 of 4 buttons live
  one profile:     focus 0, mode 0, 3 of 4 buttons live
```

Showing it *branch* is the point — a model that only ever ran the empty case
would be indistinguishable from one that ignored the directory. "Nouvelle
partie" is hidden either way on screen 29: it belongs to screen 30, the save
panel, which shares this very panel and is told apart by `word_4CEA9A`.

> **A C++ lifetime bug worth naming**, because it failed silently rather than
> loudly. `const auto& os = Json::parseFile(path)["rows"]["options"]` binds a
> reference into a temporary that dies at the semicolon — reference lifetime
> extension does not reach through `operator[]`. It read as an *empty table*,
> not a crash, so the walk produced blank labels and looked like a parse
> problem in the data. Both documents are now held in named locals.

### The inventory channel

`Game_HandleEvent` 25..42. The interface never touches an object list — it
raises an event with one argument block (`+0` the value in and out, `+4` the
result code, `+8` a caller-supplied buffer) and reads the answer back. So the
whole of the inventory's behaviour is decidable from the object records and the
game DB, both already ported; this is the wiring between them.

```
IAM\START's lists: carried 2 [6..171], second 2, memos 0
objects: 161 priced (max 5000, max sell 2500), 161 halving exactly,
         0 needing the 0xFFFF clamp; 36 carrying a quantity, 17 guns
recipes: 11, 5 gated on a value the engine writes and 6 on 8 which it never
         does; 6 unreachable at any gate
```

Ported: 33 the display name with `" - N"` when flag 0x20 says the record
carries a quantity (counted from the *player* record for kinds 2..6 and the
item's own `+12` for 7..11), 34 the price, 38 buy — refused when the list is
full or the price exceeds the player's money, 39 sell at **half** clamped to
0xFFFF, 41 the shop's refusal to sell a gun already held, and 37 combine.

**The 0xFFFF clamp never bites.** The dearest object is 5000, so all 161 priced
objects halve exactly and none reaches it — defensive, not load-bearing. That
distinction only shows up by running the ported function over the data rather
than reading the code.

**Six of the eleven recipes are unreachable at any gate the engine can
produce.** `combine` is offered 1, 0 and −1 — the only three values written to
the global it compares against — and six recipes want 8. The documented dead
content, re-derived by asking the ported function instead of inspecting the
table.

### Text layout

**A `.FNT` alone cannot lay text out**, which is why porting the format did not
finish this row. A glyph's advance is its own width plus the *face's* kerning,
and a character the file does not carry falls back to the face's default
advance — both of which live in the 13-record font table at 0x004C7090
(`Font_Find`), not in the glyph file. That table is now lifted into
`tables/ui.json`, keyed by an ASCII **letter**, which is what an item's `+36`
has been all along: 74 is `J` JOURNAL, 83 `S` SNEAK for a heading, 76 `L` SMALL
below 640×480.

```
Nouvelle partie        15 chars,  127 px, 17 tall, align -1, ink 255/255/255, face J
{fS}Options             7 chars,   68 px, 20 tall, align -1, ink 255/255/255, face S
{fCI255120045}rouge     5 chars,   38 px, 14 tall, align -1, ink 255/120/ 45, face C
{C}centre               6 chars,   53 px, 17 tall, align  8, ink 255/255/255, face J
```

Widths in pixels are the thing worth comparing: three different places
contribute to one advance, and a layout that got any of them wrong still
renders *something* — only the measured width says which. Both implementations
run over the same eight strings and every character count, width, height,
alignment, ink and face agrees.

The cases exercise the markup `FILE_FORMATS` §5b4 had wrong: `{f<letter>}`
changes face mid-string, `{I<9 digits>}` is **three 3-digit components** and not
a hex triple, several chain inside one brace, `{C}` sets alignment where a bare
string sets none — so the item's own flags stand — `{X<6 digits>}` is a move
layout skips because the caller owns the box, and `[` `]` delimit counted spans
that carry no styling and no width.

## It boots

```
$ ./build/omk ../fr --tables ../tables
Omikron - the replica, booted fullscreen
  movies   3 of 3 present in FLIS/
  boot     Game_Start("aventure.scx") - 20 effect sprites, 53 sounds
           (the GLOBAL library, not a menu)
  menu     Interface = 1 (derived by walking screen 29)
  area     118, from IAM/START +1414
  loop     1 startup contexts, 200 frames, 1 conversations, 1 areas entered,
           61 decisions announced
  screen 29 (Menu): Nouvelle partie | Charger une partie | Options | Quitter
```

**And its announcement stream matches `traces/intro.log` 42 of 42, in order,
from a cold boot.** That is the same number the hand-wired harness gets,
reached without the harness — which is the whole point of it existing.

Until this, every slice was proved on its own and assembled by a test
program: `boot_intro` builds a Session itself, hands it an opcode table, calls
`loadArea(118)` and ticks. That proves the pieces. It does not prove they fit
together, and "each part is correct" is exactly the claim that survives an
integration being wrong.

`tools/omk.cpp` parses the command line the way `WinMain` does — `NOFMV` and
`WINDOW` matched as whole words — steps the three FLIS movies, does
`Game_Start("aventure.scx")`, derives the start menu's answer, and runs
`Game_RunLoop`'s idle path calling `Game_Frame`. Headless: no window, no
DirectDraw, no audio. What it reproduces is the **sequence and the clock**,
which is the part that has an oracle.

### `ui.open` parks, and that changed three checks

A script that reaches `ui.open` waits on a **person**: the handler parks its
caller at status 6, names a result variable, and only `Game_HandleEvent` case 5
— the screen answering — writes that variable and releases it. Nothing in the
pump can do it.

Modelling that properly made `boot_intro` stop after **three events** — which
is exactly what `traces/menu-noinput.log` recorded the *engine* doing, and
`menu-keys.log` is the same three four times over as the screen reopens rather
than answers. The harness now attaches the widget tree, and answers by walking
the screen.

Three corpus sweeps had to opt out, and the switch is explicit rather than
silent: `run_scripts`, the free `loadArea`, and `replaySlot` all predict one
slot standalone, which is the job `tools/sim` does with a VM that **stubs**
opcode 70. Parking there would make the two disagree about something neither
models, so those three call `setUiOpenSuspends(false)` and compare like with
like. Everything that actually runs the game leaves it on.

> One thing recorded rather than banked: with the park *on*, `resto-387`'s
> disagreements go 6 → 5 and `telis-dialog`'s agreements 5 → 4. A mismatch
> dissolving under a more faithful VM hints that some of the six are the replay
> running past a point the engine stopped at — but a hint measured against a
> reference that does not model the park is not evidence, so it stays a note.

> **A claim of mine that did not survive being checked.** This section first
> said "the main menu is a scene like any other, started by the same call that
> starts a location, so there is no separate menu state to model". That was
> inferred from the `Game_Start("aventure.scx")` *call* without opening the
> *file* — `CLAUDE.md` §1 exactly backwards, since the rule is that the data
> proposes and the code confirms, not that a call licenses a guess about its
> argument. Opening it takes one command and shows 22 objects, 21 of them
> registering an effect sprite and the 22nd registering 53 sounds.
> **Where the menu is opened from is now an open question**, not an answered
> one.

Five things it shows that no slice could:

* the three movies **resolve** — and the executable spells them `.mpg` while
  the disc ships `.MPG`, so the boot path is the first thing in the game that
  cannot work without a case-insensitive filesystem;
* `aventure.scx` resolves and is read — and it is **not a menu and not a
  location**. It holds **20 effect sprites and 53 sound registrations** and no
  menu logic whatever: the smoke, glows, impacts and stars, plus the player's
  own footsteps, breathing, jumps and eat/drink. It is the *global library*,
  loaded once so every scene can reference it — which is why it is 3 MB with a
  39 KB block, and why `Game_Start` takes it before anything else;
* `Interface` is 1 — **and the script asks for it.** AREA 118's startup script
  reaches `ui.open(29, −1, → variable 19)` at pc 1078; the interpreter parks
  the context there, the way the handler parks its caller at status 6; the
  Session walks screen 29 for an answer, writes variable 19, and the script
  resumes into `dialog.start 272`. The first version walked screen 29 in the
  *boot* and seeded variable 19 before any script ran — the right number by
  doing the right thing in the wrong place, and it would have stayed right
  even if the script had asked for a different screen. The screen id and the
  variable now come from the instruction's own operands;
* the starting area is **118, read from `IAM\START`'s `+1414`** rather than
  named. The first version of this had the 118 as a literal in `boot.cpp`
  while the paragraph above it claimed nothing was hand-wired —
  `Game_NewGame` loads `IAM\START` over a zeroed DB and applies it, and
  where the player is lives in that block;
* the menu's four labels resolve out of `IAM\Menu`. Without the text archives
  every item the widget walk finds is a number, and a booting engine that
  prints its menu as integers is obviously half-done.

## Coverage — what is ported, and what is not

**Added 2026-09-03, outside the 41-row audit below: the street life**
(`docs/STREET_LIFE.md`, `todo/street-life.md`) — the `.OPT` traffic circuit
(`formats/opt.*`), the procedural pedestrians (`actor/pedestrians.*`: the
spawner at `39 * (5 - density) * h[3]`, the mover over lanes and routes, the
body on the walk clip's root motion, following, overtaking, reservation
groups, action points), the spatial index and the crowd push
(`actor/spatial.*`, `Session::crowdPush`, the bump and talk messages), the
head look (`aimHead`, `actor/pose.*`), all fed from the Session at an area's
load and ticked every frame, and drawn by `omk-play` with a street start
(`--save --area --address/--stand --density --no-crowd`). Fully ported,
with the density held at the engine's default 3 until the options menu
hands its value in. Checks: `engine: pedestrians`, `engine: city crowd`,
`engine: street frame`, `engine: crowd push`, `engine: head look`, `opt
tracks`.

**And 2026-09-04, the ROAD TRAFFIC** (`docs/STREET_LIFE.md` §2b,
`todo/road-traffic.md`) — the vehicle half of the same circuit:
`actor/vehicles.cpp` is `Slider_Init`'s vehicle branch (the two model tables
`sli_fn`/`moto` behind the AREA masks at `+172`/`+174`, the 40-slot ride
pool), `sub_4543F0`'s spawn over lanes `[2]..[5]` at `39 * h[4]` with NO
density factor, and `sub_456530` state 0 — `sub_456C70`'s drive (the
walkers' own mover step, their gait with the vehicle thresholds 195/390,
`+256`/`-768` a frame to a cap of 5000, the body 30.75 under its node, the
brake for a player in the road, message 17 above 1706.6666) and
`sub_456B40`'s 585-unit sound. It shares the walkers' 240-slot pool, which
is not tidiness: 70 of Anekbah's reservation groups are reachable from both
classes and a vehicle waits on a walker 2197 times in 1800 frames.
`engine: road traffic`, `engine/tools/veh_probe`. **DRAWN and WATCHED** the
same day: `omk-play` stages a vehicle as the rigid body it is — the chosen
sub-object composed once, re-centred on its own root, turned to its heading
and put at `sub_437F80`'s `y - 30.75` — and
`--stand 5620,0,-2400,270` shows a moto with its rider crossing the street
and `--stand 5980,0,-3200,270` a hover taxi, both riding above the road
(`engine: traffic frame`). Still unwatched: the pace, the spacing, a corner,
a brake. Drawing it also found a LATENT CROWD BUG in `play.cpp` — the
per-frame `charModels` eviction keeps only models a staged actor wears, and
the circuit's own bodies are not staged actors, so their cached
`CharModel*` pointed at a freed node; the crowd was masked only because a
city's authored extras happen to wear the same models. Fixed for both.
Not ported: the player's RIDE (`Slider_TickRide`, `ACTOR_STATE` 7/8,
`sub_456530` states 1..7), the engine's LOD selection for an actor's four
skeletons (the viewer draws the first), the bump's `camera.shake`.

Re-audited row by row against `CLAUDE.md` §4 on 2026-08-31, **41 content
rows**. This table has now been wrong twice — once with a count that had
quietly dropped the rows it judged unportable, once with a figure left stale by
a day's work — so it is written out in full rather than summarised, and every
row is named so the count can be checked rather than believed.

**31 fully ported.** IAM archives and `IAM\DIALOG`; the script VM; `.3DM`;
ADPCM; `.3DT`; `.3DO`; `.ani`; `.CTL`; `.SCX` block, stream and object
interpreter; `.3DA`; `.3DP`; the camera editings; `.SFX`; the effect sprites;
the effects chain; the world scripts; the trigger zones; the event/message
system; how a conversation is launched; conversation → model; the dialogue
runtime; the options menu; the widget tree; the UI input path; the inventory
data channel; the combination table; the fight AI; the cutscenes; and the game
state with its save file and clock.

**7 partly ported**, and the missing half of each is the same kind of thing —
the runtime that *uses* the data, or native code that is not in the
decompilation at all:

| row | ported | not |
|---|---|---|
| the interface text / save directory | the save file, its geometry, the 72-byte directory | the `IAM\<Screen>` text archives |
| the 37 screens | the definition table, the widget tree, the walk over it | the per-screen native callbacks |
| the per-screen open/close | the flag broadcasts, **and now the item bindings** — 35 string ids and 22 tags the opens write into `+28`/`+60`, none of which is in the item record | the answers the callbacks write. `Ui_OpenShop`'s per-screen titles are **done** — the `+8` jump table at `0x004AE7AC`, which the linear scan bound wrongly (all ten shops to string 19) rather than missing |
| the four control schemes | **the whole path** — `Input_InstallScheme`'s 4 x 14 x 3 copy into the live tables, `Input_Poll`'s slot-k-to-bit-`1 << k`, `Game_Frame`'s edge filter against `Ui_BeginScreen`'s 0x203F mask, and group-local rebinding with its 0/1/4 refusals. Tier **3, differential**: the start menu answered by SCANCODE reaches the answer `tools/sim` reaches from words | the joystick axes (codes 0 and 4) are carried but nothing steers with them yet |
| the shoot AI | the compiled tables, the type dispatch, Gandhar exactly (he is table-driven), X-Tech (it does nothing) | Astaroth's and the generic shooter's per-state geometry — ported as state graphs only |
| the 45 interface sounds | **the whole path the engine has**, at tier **2** for the loader / **3** for `Sound_LengthMs` and the mixer's transparency / **6** for the bookkeeping — `Wav_LoadToBuffer`'s acceptance run over all 61 shipped `.wav`, `Sound_LengthMs`, the 160-buffer bank, the 16-voice pool with its flag word, `Sound_FreeBuffer` killing what plays it, the listener and its guard, and the volume law. All 45 names resolve; the 32-slot cache means 13 cannot be resident | the twelve per-screen slots are not fired by the widget walk yet, and the attenuation and pan law is DirectSound's, with **no reachable tier at all** |
| the I2D 2D layer | the display list and its ordering, the seven pools, every acceptance test, the three flag banks, **the software back end** — an RGB565 surface, the BMP loader and `Blt` with its colour keys, reproducing the engine's own framebuffer **66560/66560** over the menu's deterministic region — **and the mode-2 software rasterizer**: the clipped Bresenham `sub_48C4C0` (which is also the whole of the wireframe triangle) and `sub_48C060`'s four blend modes | the Vulkan back end |

**0 lifted as a table but not consumed.** The 45 interface sounds were the
last row in this bucket and `src/audio/` now reads them: every one of the 45
names resolves to a shipped `.wav`, is accepted by the engine's own
`Wav_LoadToBuffer`, and is played into the voice pool. See the audio section
below for what that does *not* establish.

**The four control schemes moved up on 2026-09-01** — `src/input/bindings.*`
runs `Input_InstallScheme`, the live tables, `Game_Frame`'s edge filter and
group-local rebinding, and the start menu is now answered **by scancode**
rather than by handing the walk an input word. That found a documented error:
`docs/UI.md` gave the interface bits' defaults as **E** and **R**, which is
`0x004C65B8`'s static initialiser and not the default scheme — `Game_Init`
installs Aventure over it before the first frame, so slots 4 and 5 are ENTER
and SPACE and no player ever saw E or R. The port keeps the initialiser
precisely so that overwrite is observable. `verify.py: engine input`.

**0 not ported.** This section used to list two rows, the I2D layer and the
shoot AI, and both have moved up — for different reasons. The shoot AI was
described as having "no data table behind them", and **the second half of that
was wrong**: Gandhar plays three behaviour scripts compiled into the
executable. The I2D layer was described as having no oracle, which is true of
its *pixels* and not of its *decisions* — the display list is exactly the kind
of thing this tree models everywhere else. Both are below.

What remains unported is now stated by row rather than by subsystem, and the
honest summary is that it is all **device**: DirectDraw, DirectSound's mix, and
the 26 screen callbacks that are absent from the decompilation. The "six
rasterizers" that stood in this sentence until 2026-09-01 were **two**, and
both are ported — see the software-rasterizer row below. `Text_DrawRun` and the audio path have both come off this list
since it was written.

**3 are not portable subjects at all**: the simulator, the UI under the
simulator, and the golden traces. They are this project's instruments, and two
of them are what everything above is checked against.

### The script engine, re-audited against its handlers (2026-09-02)

The rows above say a subsystem is *ported*. That is not the same as saying
every rule inside it is the engine's, and a review of
`todo/iam-script-engine.md` — 39 issues raised by reading `Script_Execute`
(0x00406460), `Script_Pump` (0x00407DC0), `Script_ProcessActions` (0x00408220),
`Script_QueueAction` (0x004063D0) and the handler blocks against the port —
found 18 places where the rule being run was the **port's** and not the
engine's. They are listed here by module, in the same
what-the-port-had / what-the-engine-does form the FX and particle rows use,
because the interesting half is always the second column.

**None of them was reachable from the 5958-slot sweep**, which is why they
survived: each check below runs hand-built bytecode or a probe rather than
re-decoding the corpus, and each was SHOWN to fail on the unfixed behaviour
(PORTING B2).

| subsystem | what the port had | what the engine does | pinned by |
|---|---|---|---|
| the interpreter — `src/script/interp.*` | floor division (after `tools/sim`); the shared operand fetch on 5 opcodes; `var.set.random` a stub | `cdq; idiv` — truncation, so −7/2 is −3, and a zero divisor **faults**; the fetch on **91** opcodes, indexing the parameter block from its **second** word, so `0x4000` is the message *sender*; `Random_NoRepeat` (0x0041D6B0) with one process-global previous draw | `engine: vm probe` |
| the Session scheduler — `src/script/area.*` | `dialog.start` ended the frame for every context; a screen closed with no answer killed the script; A → B → A replayed A's startup script; `scene.load`/`unload` on the resident area deferred; `player.become` a stub | `Script_Execute` returns for **that one context** and the pump's loop carries on; `UI_OpenScreen` seeds the answer at −1 so the script **resumes** at −1; `Area_LoadIntoSlot` returns before `Area_Load` on a resident return, so **no startup script runs**; 71/72 swap the scene in the same frame; `player.become` `strcpy`s two bios and `rep movsd`s 0x43 dwords into the DB player record, so `+272` **is** the player's actor id | `engine: session rules`  **Batch 2 (T11):** the two resident slots, the staged load (slice-counted off the set file: ceil(bytes / 0x20000), Anekbah 17), `Area_Transition`'s state machine with the departure/arrival objects (door pairs, 441 of 448 sites in zone enter scripts), `area.preload`'s deferral, `area.arrive`, the 32-entry context table with first-free reuse and index order, the no-hold on an unknown camera, and `Session::restart()`; tier 6 for the scheduling, 4 where the traces reach (the intro's order), data-constrained for the slice count and the object resolution. `Actors_SpawnFromTables` is still not ported. `verify.py: engine: area transition`.  **Batch 2 wave B (T15):** the zones LIVE - `ZoneRegistry` in `frame()` at `Game_Tick`'s two points, the prompt slots' `+0` pointers, `Script_NewContext`/`QueueAction` on real contexts, the scan from the player's ACTOR_STATE dispatch (in a conversation, not under a scene program), event 8's camera; the world hooks writing the resident blocks (`Actor_FindById` by id, row 0 first; `Scene_LoadProps`' first-free slot); the controller's position fed each adventure frame; `Script_Pump` step 2's message 26 (the dry-run flag is dead - one reader inside the count it cannot be 0 under); `end`'s message-0 marker, boot context, activate count and deferred SCENE-block free. Tier 6 for the scheduling, 5 for the scan (no capture reaches it; `traces/impasse-walk.log`'s far side is the first oracle once the walker replays), 2 for the 24-handler flag list read off the image. `verify.py: engine live zones`.  **T18 (2026-09-03):** walking between areas - `ResidentSlot::shown` (two decors in state 2 across a transition; the shown set and the active row are two things), `playerAnimHeld` for ops 104/105 (`Actor_HoldAnimation`'s 0x81), `placementSeq` so a teleport re-seats the walker, `decorUnder` raising event 9 from the FEET; the viewer's half draws both slots, merges the walkable soups in place, keeps the player across the change and shows the `media.play` SUBTITLE (`Subtitle_Show`: 80 ms a character, floor 2 s, inset 16). Tier 4 by `traces/impasse-walk.log`'s airlock order and by rendering the handoff's repro (0 of 57 frames black); `hideOutgoing`'s raise stays labelled for walkerless runs. `verify.py: engine: airlock walk`.  **Batch 3 (T19, 2026-09-03):** `Actors_SpawnFromTables` runs at every area load and every `scene.load`, so both resident slots carry the characters their AREA and SCENE tables place — model, `.CTL` bank, position, facing, runtime slot and the `ObjectShown` bit that decides whether each is attached — and `shown()` is derived from them (7 spawned and 4 attached for the Impasse, `verify.py: engine spawn from tables`). No model or `.CTL` instance is loaded in the Session; the names travel to the viewer, which (T20, 2026-09-03) stages EVERY shown character as its own posed body — driven by its scene program, the conversation line, or its bank's idle — so the Impasse's arrival beats draw Kay'l and the Demon, confirmed by rendering (issue 41 fixed); `Actor_SetPlacement`'s spatial-index side effects and `freeActorSlots`' player exception are labelled. |
| the zone pump — `src/script/world.*` | one activate queued per held frame, up to the FIFO's four; every action run with a **fresh** interpreter | `Script_QueueAction` refuses a second activate while the first is unfinished (`ctx+32`, cleared only at `end`); a context owns its interpreter and is **resumed**, so a script that parks on `ui.open`/`camera.set.wait` is not restarted from the top. Measured on SCENE 11 zone 933: 5 queued / 0 refused and the conversation never launched, against 1 queued / 4 refused and it launches | `engine: zone pump`  **The Session now runs zones itself** (`src/script/zones.*` + `area.cpp`, T13/T15); `World` stays the one-chunk harness for `engine: zones` / `zone pump`. |
| the dialogue runtime — `src/script/dialogue.*` | every line waited for a press | the end rule is **per asset name**: `125338` → state 7 (any key but up/down cuts), `02E19A` → state 8 (**ends by itself**, no press), everything else → state 2 (confirm only). Both sentinels ship exactly once, in conversations 272 and 186 | `engine: dialogue line states` |
| the game state — `src/script/gamestate.*` | no object lists, prop state or timer for opcodes 29/30/37 to reach | `ObjectList_InsertFront` inserts at the **front** and refuses when full; the duplicate refusal is per *list*, not per opcode; `ObjectState_Get` sign-extends (state 3 at `i % 4 == 3` reads back −1); the timer's unit is a **millisecond**, and table ops 111/112 were **inverted** | `engine: game state` |
| the camera editing — `src/o3de/camedit.*`, `src/script/scenerunner.*` | `scx.play*` started an object and left the camera alone | every `scx.play*` handler requests **camera mode 13** when the object has a linked editing, and mode 13 copies the scene's active camera every frame; the port samples the editing at the **pre-advance** clock and `omk-play` flies the Impasse's seven shots through it | `engine: cam mode 13` |
| the viewer's screen close — `backends/sdl/play.cpp` | `break` out of the loop when the walk closed a screen | closing **is** an answer: post −1 and keep running, which is `UI_OpenScreen`'s preset through `Game_HandleEvent` case 5 | `engine: screen close` |
| the tables — `tables/vm_opcodes.json` | op 16 at 5 bytes, 43/44 at 0, op 93 `hud.show_var`, 111/112 named backwards | 6, 4 and 6 bytes; op 93 is `actor.stat.set` (both arms reach `Actor_SetProperty`); 111 stops and 112 starts. **20** corrected lengths, **129** named | `opcode table fresh` |

**What this does not claim.** No golden trace can adjudicate any of it:
`Script_QueueAction`, `Script_Pump` and `Script_Execute` announce nothing to
`Dbg_LogTagged`, so the standard here is **assembly-transcribed and
data-constrained**, not engine-verified — the same ceiling the actor runtime
hits, and for the same mechanical reason. What a capture *could* eventually
check is the one thing the resume changes: **when** the announcements happen.

The remaining open issues, with the reason each is still open, are in
[`todo/iam-script-engine.md`](../todo/iam-script-engine.md).

### What is left, and what "done" means for it

The standard this is held to is [`docs/PORTING.md`](../docs/PORTING.md) — the
target the replica compiles against (Part A) and the evidence a port needs
(Part B). Its §B6 gives each remaining subsystem a reachable tier and an
acceptance criterion; this is the same list, from the coverage side.

**Two sentences that used to stand here are now false, and the change is the
point.** This section said "there is no capture that records a frame, no
reference rasterizer", and dropped the standard for the whole remainder to
"read and explained". `goldentrace.py capture` records a frame, so most of
these rows reach **tier 4** instead. And the **software rasterizers**, which the
plan listed as deliberately excluded alongside DirectX and win32, are no longer
excluded: they are software by definition, and porting them into the reference
framebuffer is what makes a frame diffable at all. **There are two of them, not
six** — the count in this paragraph was never grounded, and enumerating the
driver-mode tests from the image found exactly three drawing functions that
branch, all three in I2D, behind two rasterizers that are both ported.

| what is left | reachable | in short |
|---|---|---|
| the 26 screen callbacks | 5 | disassembled at their own addresses; the 20/20 and 19/20 shape is the check |
| the I2D back end | 4 | **met for the deterministic region**: 66560/66560 pixels, in RGB565 |
| the I2D primitives | **4** for the quad's geometry, 6 for the rest | the quad reproduces the engine's own selection outline 1518/1518; line and triangle have no captured frame that draws one |
| the software rasterizers (2D) | **4**, and both already met | there are **two**, not six: `sub_48C4C0` (Bresenham) and `sub_48C060` (box fill), reproduced 1518/1518 and 138/138 |
| the 3D rasterizer | **4, for ONE camera**, geometry and ordering only | the engine has none — D3D drew every triangle — so this is a **reference implementation**, not a port. Its projection agrees with `camshot.py` on 106/106 sampled corners (worst 0.0018 px) and the mirrored reading moves 349998/352000 pixels (tier 3, `engine: raster`). **Tier 4 2026-09-01** (`engine: silhouette`): measured against the engine's own framebuffer through dialog 402's camera 4555 — directed edge alignment **0.73/0.83** on a chance floor of **0.27/0.30**, and the holes a set-only render leaves falling where the capture is black, **92%/99%** against 33% frame-wide. Mid-sweep captures of the same set reach 0.14–0.38; the mirrored reading sits below its own floor; and **removing the depth test** — geometry untouched, ordering destroyed — drops it to 0.66/0.75, so B5's *and ordering* is tested. **Not covered**: any second camera, the characters and props the render omits (the directed metric cannot see them missing), any pixel's value, and the drawable mask (0 pixels differ here). The one-camera limit is not theoretical: the missing **near-plane clip** - triangles with a vertex behind the cut dropped rather than cut - changes 0 pixels through 4555 and 12710 one step into the room, and was found by a player flying the scene viewer, not by any check (`engine: near clip`) |
| the two render BANKS | 2 | six two-entry arrays swapped by `sub_42FA00`; opcode 150 installs bank 1 at 14 shipped sites. What each bank **draws** is unread and has no oracle |
| audio mixing | **2** for the loader, 6 for the bookkeeping, **none** for the law | the criterion was wrong: **the engine does not mix**, DirectSound does. The decisions are ported and the loader runs over all 61 shipped `.wav`; the attenuation and pan law has no reachable oracle |
| the cutscene VOICES - `media.play` (op 92) | **PORTED** `src/audio/voiceover.{h,cpp}`, `tools/voice_probe.cpp` | **tier 4** for the resolution (the golden traces' own media ids: 102 announcements, 56 distinct, 56/56 resolving, and `impasse-walk` opening 142/141/404/410 in order - `media.play`'s `"OBJECTS"` literal at 0x004C0844 has one `push offset` in the image, so the traces separate it from the nine other OBJECTS announcers); **tier 3** for the decode (four FNV hashes over the PCM, re-derived by `tools/adp.py`); **tier 2** for the 10 + 520 + 31 = 561 partition and the filter's 1-in-1584 collision rate; **tier 6** for the `{C}` subtitle and the kind-16 `IMAGES\<stem>.BMP` document arm, which are resolved and reported and NOT drawn; **no tier** for loudness or placement, which are DirectSound's. `verify.py: engine voice over` |
| the PLAYER in adventure mode | **the chain, run from the keyboard**: `Input::frame`'s word (Aventure scheme, the world's repeat mask 0) -> `CefChannel` -> the clip's root delta (`Anim_RootDelta`, fractional ends, ROTATED by the facing matrix - node+156 = actor+288, which closes CLAUDE.md 6's "optional 3x3" for the actor path) -> `Walker` -> the floor; the window and whole-on-transition turn and root shift through `Cef_ApplyTurn`/`Cef_ApplyRootShift`; the idle word `sub_4A7A20` makes when nothing is held; the follow camera as `sub_415D10`/`sub_415E60` resolve and lag it (f42/f44/f46 = 3/8/8 for mode 0). Tier **5**: four replayable streams on `AIMPASSE`, `H_STAND -> H_SD-WK -> H_WALK -> H_WK-SD -> H_STAND`, 69.5 units in 60 frames along the facing, the eye 118.11 behind at fov 75, 1539/1595 tracks resolving | `Actor_Move`'s slide (a blocked step stops); the camera's flag 8 / 0x10 passes (`sub_417070`, `sub_416450`) and `sub_413C00`'s 0.7 x height rule, so the eye keeps the offset's own height and passes through walls; a blend is drawn as a cut; feet-or-pelvis for `+244..+252`; the world camera's own smoothing shorts (the preset's 3/8/8 stand in) |
| input | 3 | the binding tables reproducing `tools/sim`'s decisions |
| the Vulkan backend | none | explicitly unverifiable; correctness inherited from the software backend it mirrors |

**Still deliberately excluded**: DirectX, win32 and the C runtime. Those are
host APIs the backends replace rather than code to port.

## Opening files: `DataFs`, and why it is a class

The game shipped for **Windows 95/98, whose filesystem is case-insensitive**.
Every name the data references itself by was resolved by that OS without anyone
noticing the spelling — a `.3DO` naming its `.3dt`, an actor record naming its
`.CTL`, `Scene_FullPath` building a path from a record's string. The authors
typed whatever they liked, and the disc proves it: **eight of the 67 shipped
`.SFX` files spell the extension `.Sfx` or `.SfX`.**

On Linux or macOS none of that resolves by concatenation, so case-insensitive
lookup is a **compatibility requirement, not a convenience** — and it has to be
everywhere. Hence a class owning the data root rather than a helper someone can
forget to call: `data.read("MESHES/DECORS/Aapkayl.3DO")`,
`data.readSibling(rel, "3dt")`, `data.list("SCPTDATA", "sfx")`. Nothing else in
the engine touches `std::filesystem` or builds a path by concatenation, which
is also where the portability comes from — separators, listings and reads live
in one file.

Every component is matched, not just the filename: `meshes/DECORS/aapkayl.3do`
finds `MESHES/DECORS/Aapkayl.3DO`.

**`verify.py: engine DataFs`** tests it hostilely: all **2367** shipped files
are asked for again with the whole path ALL-UPPER, all-lower, and *case-flipped*
— and all 2367 resolve to the same real file under every spelling. A resolver
that only lowercased the extension, or only handled the last component, fails
it. Plus all 220 decor models resolving their `.3dt` sibling, which is exactly
where the two-spelling bug kept reappearing.

The same mistake has been made twice in Python — a `.3DM` sweep reporting 708
files where there are 777, and the eight `.SFX` files five checks could not
see. Both times the symptom was a total that was quietly short rather than an
error.

**`verify.py: engine morph+ADPCM`** covers the spoken lines and their audio.
The `.3DM` frame count is **derived, not read** — word 2 of the header is
nominal, and the real count is `(size - preamble) / record`. **582 of the 777
files land on a record boundary, 195 are short by exactly one audio block, 0
are anything else**, so a reader trusting word 2 would be wrong about a quarter
of the corpus.

The codec is then checked **sample for sample: all 777 files identical to
`tools/adp.py`, 225 441 216 samples.** OTNS ADPCM is IMA with two load-bearing
differences — the high nibble decodes first, and IMA's unconditional
`step >> 3` bias is absent (leaving it in drifts the predictor ~−9000 DC over a
line, burying the audio).

**A bug the corpus nearly hid.** Only **3** of the 777 files are stereo, and
the port alternated whole *bytes* between channels where `sub_483340` splits
them *inside* the byte — high nibble left, low nibble right. 774 files pass
either way; the three are what fail it. The check asserts the stereo count for
that reason.

**The limit of a differential, stated because it is easy to forget.** Agreeing
with the Python cannot catch an error the two implementations share — one was
written from the other's description. What it does catch is the port's real
failure mode: a field at the wrong offset, a signedness slip, an off-by-one in
a back-reference that overlaps its own source. The data-falsifiable invariants
above are what cover the rest, and every future slice should carry some.

## The audio path — and the criterion that was wrong

`docs/PORTING.md` B6 asked for "audio mixing … PCM sample-exact against
`tools/adp.py`; the mix compared offline". **The engine does not mix.**
`Sound_Init` (`0x0046C3A0`) creates a DirectSound *primary* buffer, sets its
format — PCM, 2 channels, **22050 Hz**, 16 bits, block align 4, 88200 bytes a
second — and calls `Play(0, 0, DSBPLAY_LOOPING)` once. Everything after that is
a *secondary* buffer that DirectSound's own mixer sums into it. There is no
loop in the image that adds two samples together, so the criterion as written
could not have been met by anything. It is corrected in B6 rather than left
looking unfinished.

What the engine's half *is*, is **decisions** — which buffer exists, which
voice plays it, where it is, how loud, at what rate. That is the same boundary
`PORTING` A2 draws for the renderer, and `src/audio/mixer.*` ports it.

**Three of the seventeen wrappers are in neither `Runtime.exe.c` nor
`readable/`.** `Sound_SetFrequency` (`0x0046CBF0`), `Sound_GetFrequency`
(`0x0046CC30`) and `Sound_LengthMs` (`0x0046CC70`). Their addresses came from
disassembling the gap between `Sound_SetVolume` and `Sound_FindVoice` and
measuring each block back to a 16-byte alignment.

**Why they are absent was first written here as the missing `push` prologue,
and that was wrong** — caught by measuring rather than by re-reading.
`Sound_SetVolume` (`0x0046CBB0`) opens with the identical `A1 <ppDS>` and no
`push`, and it *is* decompiled. Scanning every `E8` rel32 in the image gives
`Sound_SetVolume` **6 direct callers** and each of the three **0**, with **0
dword references** to any of them anywhere — so nothing takes their address
either, and IDA never made functions of them because **nothing reaches them**.

**They are dead code.** They are ported anyway (`Sound_LengthMs` carries an
exact arithmetic law worth having, and the pair completes the wrapper set), but
they join `pluie.wav`, options page 12 and the X-Tech shoot callback on the
shipped-and-unreachable list, and no claim here rests on them.

### What the shipped data is asked, and could fail

**`Wav_LoadToBuffer` (`0x0049F830`) over all 61 shipped
`gamedata/I2D/sounds/*.wav`.** It reads a 20-byte header and requires `RIFF` and
`WAVEfmt `; reads **sixteen** bytes of `WAVEFORMATEX` whatever the chunk's own
size says; requires format tag 1; then **peeks two bytes and seeks back over
them only if they are non-zero** — a trick for the 18-byte extended form, which
reads the `da` of the next chunk id as "not a `cbSize`" and rewinds. All 61
take the rewind; **0 take the other arm and 0 chunks are skipped**, so two of
the loader's branches are dead against the shipped corpus. Recorded, not
trimmed — the same treatment the two I2D primitives nothing calls got.

**`Sound_LengthMs`**, `bytes * 1000 / ((bits/8) * frequency * channels)`,
truncating — re-derived in Python from the same 61 files, so the two
implementations are *differenced* rather than one asked about itself. Note it
uses `(bits/8) * channels` and **not** `nBlockAlign`; the two are equal for
every shipped file, so substituting one moves nothing. That mutation is
recorded as non-discriminating (a `PORTING` B7 anti-pattern) rather than
counted as evidence.

**The interface's own table: 45 names, all 45 resolving** to a shipped `.wav`,
against a cache of **32 slots** (`unk_657B40`…`dword_657D40`, 16 bytes each).
`Ui_LoadSound` (`0x00482D00`) takes the first slot whose flag bit 0 is clear
and **returns silently** when there is none — no eviction, no error — so **13
of the 45 can never be resident together**. Sixteen further `.wav` ship that
the table does not name, one of them called `cptrebour02..wav`, with two dots.

### The one claim the reference mixer makes about a waveform

`render()` is **not a port**. It stands where DirectSound stood, and its
attenuation curve, pan law and resampler are DirectSound's documented
behaviour, not anything read out of the image. **No tier, and no check asserts
any of it** — beyond the single property that has to hold for the boundary to
be transparent, which is `PORTING` B6's criterion restated for a boundary that
turned out to be a device:

> a mono voice, not 3D, at full volume and at the mix rate must come out
> **sample-identical in both channels**.

`men001.wav` does, over **32953 frames**, and the FNV of the rendered left
channel is recomputed in `verify.py` from the file's own `data` chunk — so the
identity is asserted *across* the two implementations rather than inside one,
which is the distinction CLAUDE.md §5 says has already cost this repo twice.
**`pause.wav` is the control**: it is the only shipped file that is not
22050 Hz (it is **22080**), so it resamples, and 33572 of its 34323 frames must
differ. Mixing without resampling takes that to 0.

### Three readings worth keeping

* **The volume "percentage" is an attenuation.** `Music_SetVolume`
  (`0x0042BE60`) clamps its argument at 100 and computes `-10000 * pct / 100`,
  so **0 is full volume and 100 is silence**, and the global it feeds is
  initialised to 0.
* **`Sound_Play3D`'s placement block is `pos[3], vel[3], minDistance,
  maxDistance`** — corroborated three ways: the copy into the voice record,
  `Sound_SetVoice3D`'s copy of the same block into
  `SetPosition`/`SetVelocity`/`SetMinDistance`/`SetMaxDistance`, and the
  slider's own call site (`0x00456B40`) passing the literals **39.0** and
  **585.0**. That second literal occurs **exactly once** in the function and is
  loaded twice — as `maxDistance` and as the audibility test `d < 585.0` — so
  the slider is dropped at exactly its own max distance rather than at a
  separate cut-off. (It was first written here as 560.0, from misreading the
  decompiler's `1142046720`.)
* **The listener is told the world unit is an inch.** `Sound_SetListener`
  (`0x0046D080`) writes `flDistanceFactor = 0x3CD013A9` = **0.0254**, metres
  per inch, and `flRolloffFactor = 1.0`. Its argument is in the engine's own
  order — position, top, front, velocity — which is *not* `DS3DLISTENER`'s
  order, so reading it as a straight copy swaps the orientation for the
  velocity.

`verify.py: engine audio`. Every claim was shown to fail: dropping the
two-byte peek rejects **61 of 61**; taking the first `"sounds"` key in
`tables/ui.json` finds a *screen's* twelve-slot array instead of the name table
and gives **0 names** (it did, and the check passed until the count was
asserted); mixing at the wrong rate takes `pause.wav` 33572 → 0; dropping the
listener's `<= 0.0001` guard takes it 1 → 0; and `Sound_FreeBuffer` not killing
the voices that play the buffer leaves 4 alive where 0 should be.
