# The what-if MAC port - Omikron on a 1999 Macintosh

Written 2026-10-01 from a conversation with the reader, who framed it as a
challenge: *what if Eidos had decided to port the game to the Mac at the
time?* Nothing here is started. This file records the target, how a 1999
porting house would most likely have done it, how OMK can reproduce that, and
the steps in order.

**What kind of claims this file makes.** Three kinds, and they are labelled:

* **[repo]** - established in this tree, with its source named;
* **[period]** - how Mac ports of 1998-2000 were generally done. This is
  general knowledge of the era, NOT sourced here and NOT about Omikron, since
  Omikron never shipped on the Mac (Windows 1999, Dreamcast 2000). Treat it
  the way §1 of CLAUDE.md treats a name in `renames.json`: a hypothesis;
* **[estimate]** - a reading of a gap nobody has measured.

## 1. The target a 1999 port would have printed on the box

[period] By 1999 a 3D Mac game typically asked for:

| | the box | the machines it means |
|---|---|---|
| OS | Mac OS 8.6 or 9 | |
| CPU | PowerPC **G3 233-266 MHz** (604e + a 3D card sometimes allowed) | beige G3, iMac G3 Rev A onward, blue-and-white G3, PowerBook G3 Wallstreet / Lombard |
| RAM | **64 MB**, virtual memory on | a Mac app runs in a FIXED partition and the OS takes its share, so the PC's 32 MB became 64 |
| 3D | **ATI Rage Pro / Rage 128, or 3dfx Voodoo2/3** (Rage II not supported) | the built-in chips of the machines above, or a PCI card in an older Mac |

[repo] The PC original asked for a Pentium II with 32 MB
(`optimization.md` line 3); a G3 at 266-300 MHz is that class of machine.

[period] Who: most likely a porting house rather than Quantic Dream. Eidos's
Mac titles of the period (the Tomb Raider games) were published by Aspyr;
studios like Westlake Interactive did much of the era's technical porting. A
port would have shipped 6-18 months after the PC, so 2000.

## 2. How they would have ported each piece

[period] Keep the game code; replace the platform layer.

| PC | Mac, 1999 | the reason, and what OMK already knows |
|---|---|---|
| Direct3D immediate mode, `D3DTLVERTEX` (pre-transformed) | **RAVE** (QuickDraw 3D RAVE), possibly **Glide** for Voodoo | [repo] the engine hands D3D vertices it has ALREADY transformed (`optimization.md` step 7, the 2026-09-29 correction). RAVE and Glide take screen-space vertices too, so the transform, lighting, buckets and blends carry over unchanged and only `Raster_DrawTriangles` is rewritten. OpenGL (Apple's arrived 1999) needs pre-transformed vertices faked through an orthographic projection - likely added later, not first |
| DirectDraw: fullscreen, flip, colour key | **DrawSprocket**, 640x480, thousands of colours | Mac 16-bit is **555, not 565**. `.3DT` texels and I2D bitmaps converted; the colour key becomes 1-bit alpha (ARGB1555), native on Rage cards and in RAVE |
| DirectSound, primary buffer 22050/16/stereo | **Sound Manager** | [repo] there is NO mixer to port - DirectSound summed into the primary buffer (CLAUDE.md §2, the audio path). The Mac needs a small software mixer for the 16 voices, or one Sound Manager channel each |
| DirectInput / window messages | **InputSprocket**, Event Manager keys | the name field's `WM_CHAR` path (`docs/UI.md` §3f) becomes key-down events |
| Win32 files, registry, `[Preferences]` ini | MSL stdio under CodeWarrior, a preferences file | colon paths; the CD is read the same way |
| x86 asm / MMX hot loops (the ADPCM decoder, the blitters) | C, then PPC asm where it pays | |
| the FLIS movies | the decoder recompiled, or re-encoded | |
| MSVC | **CodeWarrior Pro 4/5**, C/C++98 | |

### 2a. Which 3D API, and when - the Mac's changeover

[period] From memory, not checked title by title. OpenGL on the Mac did NOT
start with OS X: it arrived on classic Mac OS in 1999, and OS 9 is where the
switch happened, about two years before OS X mattered for games.

| years | the API | examples |
|---|---|---|
| before 1999 | **RAVE** (Apple's low-level layer, across ATI and other cards), **QuickDraw 3D**, **Glide** (3dfx Voodoo) | Bungie's *Myth* (RAVE + Glide); *Unreal* (1999, Westlake: RAVE + Glide); Pangea's *Nanosaur* (1998) and *Bugdom* (1999) on QuickDraw 3D; the 3dfx build of *Quake* |
| 1999 | **OpenGL arrives** | Jobs and Carmack show *Quake III* on a Mac at Macworld, January 1999; Apple's OpenGL 1.0 for Mac OS ships during the year and Mac OS 9 bundles it; *Quake III Arena* (Mac, around the end of 1999) REQUIRES it |
| 2000-2002 | **OpenGL dominant, mostly still on OS 9** (OS X 10.0 is March 2001; games move there in numbers ~2002) | *Unreal Tournament* (RAVE + Glide at launch, OpenGL added); the Quake III-engine games (*Elite Force*, *Return to Castle Wolfenstein*); Bungie's *Oni*; *Alice*; *Giants*; Aspyr's *Tony Hawk* ports |

What it means here: the port of §1 would have shipped in 2000, right at the
changeover, so a porter had two credible choices:

* **RAVE, plus Glide for Voodoo owners**, as *Unreal* did. Omikron's
  pre-transformed vertices go straight into both;
* **OpenGL, on *Quake III*'s lead**, at the cost of feeding it
  pre-transformed vertices through an orthographic projection. The early OS 9
  OpenGL drivers for the Rage Pro were reportedly weak, which argues for the
  **Rage 128** as the realistic minimum on that path.

§3c's order follows from it: GL first because it is the practical one (OS 9
and OS X from one Carbon build), RAVE as the period-faithful option.

### 2b. Byte order: swapped at LOAD, mostly

The question the reader asked: would the files have been re-arranged
big-endian to avoid swapping at run time? [estimate] Mostly not:

* **The cost was nil.** A 1999 CD delivered ~2-4 MB/s; swapping a few MB on a
  G3 is milliseconds. Loading was bound by the drive, never by the swap.
* [period] **It was the norm.** Ports of the Quake and Unreal engines kept
  little-endian files and swapped in the loader (`LittleLong()` and kin). A
  porting house rarely had the studio's exporters to re-cook 1.7 GB with.
* [repo] **The loaders already have the hook.** They read a chunk into memory
  and RELOCATE its pointer fields in place - `Scene_Load`'s pass over `+4` and
  the rest, `InitCEFFile` recomputing the `.CTL` pointers. That pass visits
  every field; the swap goes in it.

Where offline conversion is UNLIKELY:

* **The VM bytecode.** Operands are multi-byte, unaligned, variable-length
  across 153 opcodes. Converting offline needs a disassembler exact on every
  operand length - [repo] this repo took weeks to get them right (ops 57, 58,
  78, 103, 120; CLAUDE.md §1), and one wrong length corrupts every script
  after it. Making the VM's operand fetch read little-endian is ten lines and
  cannot fail that way.
* **The 8192-byte DB.** Addressed by byte offset at mixed widths and saved
  verbatim (`docs/GAME_STATE.md`). Kept little-endian in memory behind
  accessors - and saves stay portable between PC and Mac.

Where offline conversion is PLAUSIBLE:

* **Textures.** Every texel needs rewriting 565 -> 555/1555 anyway, so write
  them once in the Mac form. [repo] The 58-slot cache re-reads a texture from
  disc when a slot is reused (`docs/ASSETS.md` §4b), so this saves the
  conversion on every reload too.
* **Sound.** ADPCM nibble streams barely care about byte order (only the
  headers swap); the 61 interface `.wav` might have become AIFF.
* **Movies** - a codec decision, not a byte-order one.

The one scenario where it all goes big-endian: **Quantic Dream does the port
itself**, with its own exporters - the original tools in the reader's
screenshots (`IAM`/`GALE`/`GEM`, names unconfirmed) are the kind that grow a
per-platform export - on a separate Mac disc set. Even then the bytecode
probably stays little-endian, for the reason above.

## 3. How OMK reproduces it

The renderer boundary (`engine/src/o3de/renderer.h`, PORTING A2) sits at the
DECISION level, which is exactly where a 1999 porter cut: below it, one new
backend. [repo] `backends/vita` already proves the engine runs behind a
non-SDL platform layer, and all data access goes through `DataFs`.

### 3a. Byte order - at load, as the period would have

[repo] Most readers assemble values byte by byte and are endian-neutral as
written (`engine/src/formats/mesh3do.cpp:10` says so). **19 files `memcpy`
raw fields** and need auditing - among them `mesh3do.cpp:237-247`,
`opt.cpp:12`, `ui/radar.cpp:146`, the float bit-casts in `anim.cpp`,
`sfx.cpp`, `scx.cpp`. Plus the 16-bit texels, the ADPCM, the save file (write
it LITTLE-endian, so a Mac save loads on a PC) and the DB.

An offline "Mac CD" converter is possible as a side project but is NOT the
path: its output must live outside `gamedata/` (§1, the `safeOutputPath`
rule), every reader would need a big-endian twin, and `verify.py` could no
longer check those readers against the shipped bytes.

### 3b. The budget - the original's pools ARE the budget

[repo] The port today: **136.8 MB live heap** on the street and **6.0 ms** of
main-thread work a frame on an M1 (`optimization.md` "Measured 2026-09-14");
a Vita core was judged several times too slow for that. [estimate] A 300 MHz
G3 is roughly a Vita core's class. The 2026-09-29 correction there is the key
fact: the crowd is the engine's own rule, so the port draws the SAME scene
more expensively - and the gap is the port's, to close by taking the
engine's mechanism wherever the port computes more.

The largest sites of that 136.8 MB, each with what replaces it:

| port today | the original's mechanism |
|---|---|
| render corners, 18.2 MB | indexed vertices, transformed into a per-frame transient buffer - the `D3DTLVERTEX` path |
| posed bodies, 16.4 MB in 921 allocations | ONE frame scratch pool; if bodies are rigid per-bone meshes, a matrix per mesh and no vertex copy |
| the music, 16.1 MB decoded | streamed in small chunks |
| texture pixels, 12.6 MB | the 58-slot cache RUN as a bounded cache, 16-bit |
| collision soups and grids, 11.6 MB | what the engine's probe keeps resident; the grids are the port's own and can coarsen |
| playing sounds, 9.4 MB | the 160-buffer bank, 16 voices, ADPCM decoded on demand |
| the depth tie, ~10 MB | gone on a hardware Z path (a 16-bit Z-buffer IS first-wins); the 18 sign pairs by draw order or `glPolygonOffset` |
| `play.cpp`'s harness state | a lean frontend without the instruments (`play-split.md`) |

Every enhancement (Vulkan, SSAA, mapped shadows, per-pixel lighting,
mipmaps) is out of the Mac build at compile time.

The CPU side:

* the transform on the CPU, as the engine did - neither Rage card does T&L,
  so GL would do it on the CPU anyway, and doing it in OMK lets the
  visible-set walk and the drawable mask cut first;
* crowd lights and posing stay (they are the game's work), into the scratch
  pool, no allocations (`optimization.md` step 18), floats never doubles;
* scale with the GAME'S OWN options - row 3 (clip distance: visible set,
  bucket splits, fog), row 6 (density), rows 5/7 (shadow detail) - defaulted
  low as a 1999 PC would have had them;
* [repo] `Game_Frame` sets the delta to 30/fps (`docs/BOOT.md` §4), so the
  game is correct at 15-20 fps; a G3 need not hold 30.

### 3c. The renderer

Two backends, in this order:

1. **OpenGL 1.1/1.2 fixed-function, under Carbon** - the practical one. It
   runs on OS 9 with a Rage Pro/128 AND natively on Tiger/Leopard. [repo] The
   shipped render states map onto fixed function one for one: Gouraud vertex
   colour, additive and multiply blends, alpha-test cutout, LINEAR black fog,
   dither on, point sampling (CLAUDE.md §4, `sub_4638C0`). The mirror is draw
   order and depth with no stencil (`docs/ASSETS.md` 4c), which suits a
   Rage 128.
2. **RAVE** - the period-faithful one, closest to the engine's own
   pre-transformed D3D path, and what the Rage Pro handles best. OS 9 only;
   RAVE does not exist on OS X.

### 3d. The platform layer

* **Toolchain**: Retro68 (a modern GCC for classic Mac, 68k and PPC), Carbon
  target - its Carbon support is described as experimental; prove it first.
  The engine is C++20 (`std::span` 378 sites, `std::filesystem` 3); the
  language should hold, `std::filesystem` and `std::thread` will not. No
  RTTI, ideally no exceptions - `bad_alloc` becomes a pool-exhausted path,
  which is what the original's silent-when-full pools do.
* **No threads**: loading becomes a TICKED state machine, as the original's
  `Area_TickLoad` is; the voice read-ahead (`platform/threads.*`) decodes a
  slice a frame.
* **Video**: 640x480, 555. **Audio**: Sound Manager double-buffer callback.
  **Files**: colon paths, 31-character HFS names - check the shipped tree
  against that limit before anything else.

### 3e. Testing without a 1999 Mac

* **Correctness on a big-endian CPU, first and cheapest**: build the engine
  for big-endian Linux (ppc64 or s390x) under `qemu-user` and run
  `verify.py --only` there. Every endianness bug becomes a red check, with no
  Mac involved.
* **Budget, machine-independent**: milliseconds on an M3 say nothing about a
  G3. Count instead - peak live heap, allocations a frame, vertices
  transformed, triangles submitted, texture bytes resident - under a "1999
  budget" mode that caps the heap (48 MB, say) and fails loudly past it.
* **Mac OS 9.2.2 under `qemu-system-ppc -M mac99`**, for correctness only;
  its speed is not a G3's. Not Classic under Tiger: Classic has no hardware
  3D, and it is a layer between the build and what it measures.
* **OS X PPC with a GPU**: <https://github.com/linuxkid473/ppcosxkvm>
  (read 2026-10-01) runs Tiger 10.4.11 / Leopard 10.5.8 under QEMU with an
  emulated **Radeon 9700 PRO**, 3D drawn through Metal on Apple Silicon, CPU
  "roughly a G4-era Mac". No OS 9, no Classic. A Carbon build of 3c's GL
  backend can be tested there with acceleration - though a 9700 is far more
  forgiving than a Rage 128.
* **Real hardware last**, for timing.

## 4. Steps

Each ends in a commit and a report. Steps 1-4 help every target (the Vita
too) and need no Mac at all, so they are worth doing even if the Mac build is
dropped.

1. **The budget mode and its counters** - the five counts of 3e on the
   street start, recorded here as the baseline.
2. **The memory cuts** of 3b, one row at a time, each "same output, less
   memory" in `optimization.md`'s discipline.
3. **The big-endian run** - `qemu-user` + `verify.py --only` over the format
   and runtime checks; fix the 19 `memcpy` files and whatever else goes red.
4. **The GL 1.1 fixed-function backend** behind `renderer.h`, built and
   compared against the software reference on the dev Mac (PORTING B2: shown
   to fail).
5. **Retro68 + Carbon bring-up**: a C++20 hello-world on OS 9 in QEMU, then
   the engine with ticked loading, Sound Manager audio, 555 video.
6. **Correctness in QEMU** - OS 9.2.2 (`mac99`), then Tiger on ppcosxkvm with
   the GPU.
7. **RAVE** (optional, the period-faithful backend).
8. **Real hardware**: a G3 with a Rage 128, then the floor.

## 5. Open questions

* Do the character bodies pose as rigid per-bone meshes (a matrix each) or
  need per-vertex work? It decides 3b's second row.
* Does any shipped path exceed 31 characters per component (HFS)?
* What codec are the FLIS movies, and can a G3 decode it at their size?
* Can Retro68's Carbon target hold this engine, or does OS 9 need the classic
  Toolbox build (and OS X a separate Mach-O one)?
