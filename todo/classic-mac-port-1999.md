# The WHAT-IF classic Mac port - Omikron on a 1999 Macintosh

**Not a modern macOS port.** OMK already builds and runs on today's Macs
(`omk-play`, Metal through MoltenVK). This file is a period exercise: Mac OS
8.6/9 on a PowerPC G3, and Mac OS X 10.4/10.5 on PowerPC as its practical
neighbour.

Written 2026-10-01 from a conversation with the reader, who framed it as a
challenge: *what if Eidos had decided to port the game to the Mac at the
time?* Nothing here is started. This file records the target, how a 1999
porting house would most likely have done it, how OMK can reproduce that, and
the steps in order. The same day four read-only investigations answered most
of the first draft's open questions; their findings are folded in below.

**What kind of claims this file makes.** Four kinds, and they are labelled:

* **[repo]** - established in this tree, with its source named;
* **[checked]** - a fact about the period or a tool, checked against the
  sources named beside it on 2026-10-01. Several rest on search snippets
  rather than primary pages, and say so;
* **[period]** - how Mac ports of 1998-2000 were generally done, from general
  knowledge and NOT checked. Treat it the way §1 of CLAUDE.md treats a name
  in `renames.json`: a hypothesis;
* **[estimate]** - a reading of a gap nobody has measured.

## 1. The target a 1999 port would have printed on the box

[checked] Two real boxes of the period:

| game | CPU | OS | RAM | 3D |
|---|---|---|---|---|
| *Quake III Arena* (Mac, late Dec 1999 / early Jan 2000) | G3 | 8.6 - 9.2 | 64 MB | Voodoo / Rage Pro / Rage 128, 4-6 MB VRAM, OpenGL 1.1.2 |
| Bungie's *Oni* (Jan 2001, Carbon) | 300 MHz PowerPC | 8 or later, and OS X | 64 MB | Voodoo2 or Rage Pro or better |

Sources: <https://www.macintoshrepository.org/3593-quake-iii-arena>,
<https://classicmacdemos.com/quake-iii-arena>,
<https://www.macworld.com/article/165576/oni-2.html>.

So an Omikron box of 2000 would have read:

| | the box | the machines it means |
|---|---|---|
| OS | Mac OS 8.6 or 9 | |
| CPU | PowerPC **G3 233-300 MHz** | beige G3, iMac G3 Rev A onward, blue-and-white G3, PowerBook G3 Wallstreet / Lombard |
| RAM | **64 MB**, virtual memory on | a Mac app runs in a FIXED partition and the OS takes its share, so the PC's 32 MB became 64 |
| 3D | **ATI Rage Pro / Rage 128, or 3dfx Voodoo2/3** | the built-in chips of the machines above, or a PCI card in an older Mac |

[checked] The Rage Pro was SUPPORTED but marginal: *Quake III* and *Oni* both
list it, while forum reports trace *Quake III* trouble on Rage Pro iMacs
(Rev C/D) on OS 9 to its OpenGL driver, where a Rage 128 worked
(<https://macosx.com/threads/opengl-problems-with-rev-d-imac.796/>,
<https://gona.mactar.hu/ATI_Mac/>). Anecdotal, but it makes the **Rage 128**
the realistic floor for a GL build.

[repo] The PC original asked for a Pentium II with 32 MB
(`optimization.md` line 3); a G3 at 266-300 MHz is that class of machine.

[checked] Omikron never had a Mac release (Windows 1999, Dreamcast 2000;
<https://en.wikipedia.org/wiki/The_Nomad_Soul>). Whether a Mac port was ever
ANNOUNCED and cancelled was not searched; that would take the 1999 magazines.

### 1a. Who would have done it

[checked] Eidos's Mac titles of the period were **published by Aspyr and
ported by Westlake Interactive**: *Tomb Raider II* (1998), *III* (1999) and
*Chronicles* (2001) (<https://en.wikipedia.org/wiki/List_of_games_ported_by_Aspyr>,
<https://www.macworld.com/article/160748/tombraider.html>). An Eidos Omikron
port would most plausibly have been the same pair, shipping 6-18 months
after the PC [period] - so in 2000.

## 2. How they would have ported each piece

[period] Keep the game code; replace the platform layer.

| PC | Mac, 2000 | the reason, and what OMK already knows |
|---|---|---|
| Direct3D immediate mode, `D3DTLVERTEX` (pre-transformed) | **OpenGL** most likely, by Westlake's own record (§2a); RAVE/Glide the alternative | [repo] the engine hands D3D vertices it has ALREADY transformed (`optimization.md` step 7, the 2026-09-29 correction; `todo/gpu-skinning.md`: `sub_48D3B0`/`sub_494650` build one 3x3 per mesh, `sub_4947F0` transforms every vertex once, and "nothing keeps a posed copy"). RAVE and Glide take screen-space vertices directly; OpenGL takes them through an orthographic projection. Either way the transform, lighting, buckets and blends carry over and only `Raster_DrawTriangles` is rewritten |
| DirectDraw: fullscreen, flip, colour key | **DrawSprocket**, 640x480, thousands of colours | Mac 16-bit is **555, not 565**. `.3DT` texels and I2D bitmaps converted; the colour key becomes 1-bit alpha (ARGB1555), native on Rage cards |
| DirectSound, primary buffer 22050/16/stereo | **Sound Manager** | [repo] there is NO mixer to port - DirectSound summed into the primary buffer (CLAUDE.md §2, the audio path). The Mac needs a small software mixer for the 16 voices, or one Sound Manager channel each |
| DirectInput / window messages | **InputSprocket**, Event Manager keys | the name field's `WM_CHAR` path (`docs/UI.md` §3f) becomes key-down events |
| Win32 files, registry, `[Preferences]` ini | MSL stdio under CodeWarrior, a preferences file | colon paths; the CD is read the same way |
| x86 asm / MMX hot loops (the ADPCM decoder, the blitters) | C, then PPC asm where it pays | |
| the FLIS movies, played through **DirectShow/MCI** | **QuickTime's MPEG extension** | [repo] the original has NO decoder of its own: its imports are `CoCreateInstance` and `mciSendCommandA` (`engine/src/platform/movie.h`). The Mac equivalent hands the same files to QuickTime, which played VCDs in software on G3s. See §3f for the files |
| MSVC | **CodeWarrior Pro 4/5**, C/C++98 | |

### 2a. Which 3D API, and when - the Mac's changeover

OpenGL on the Mac did NOT start with OS X: it arrived on classic Mac OS in
1999, and OS 9 is where the switch happened, about two years before OS X
mattered for games.

| years | the API | examples, with what was checked |
|---|---|---|
| before 1999 | **RAVE** (Apple's low-level layer, across ATI and other cards), **QuickDraw 3D**, **Glide** (3dfx Voodoo) | [checked] Bungie's *Myth II* (QD3D/RAVE + Glide; <https://www.pcgamingwiki.com/wiki/Myth_II:_Soulblighter>); Pangea's *Nanosaur* (1998) and *Bugdom* (1999) on QuickDraw 3D (<https://en.wikipedia.org/wiki/Bugdom>); *Tomb Raider* (Mac) with software, 3dfx and RAVE modes. [period, NOT confirmed] *Unreal* (1999) on RAVE + Glide; a Glide build of *Quake* |
| 1999 | **OpenGL arrives** | [checked] Jobs and Carmack showed *Quake III* on a G3 with a Rage 128 at Macworld San Francisco on **5 January 1999**, Apple licensing OpenGL over QuickDraw 3D (<https://www.theregister.com/on-prem/1999/01/05/apple-drops-quickdraw-3d-in-favour-of-opengl/576057>). *Quake III Arena* for Mac shipped late Dec 1999 / early Jan 2000 (sources disagree) and REQUIRED OpenGL, version **1.1.2**. The exact release dates of OpenGL for Mac OS 1.0/1.1 were NOT found, and "Mac OS 9 bundled OpenGL 1.0" is NOT confirmed - *Quake III* asking for 1.1.2 suggests otherwise. OS 9 ends at OpenGL **1.2.1** (<https://www.macintoshrepository.org/52177-opengl-1-2-1>) |
| 2000-2002 | **OpenGL dominant, mostly still on OS 9** (OS X 10.0 is March 2001; games move there in numbers ~2002) | [checked] *Unreal Tournament* (Westlake/MacSoft, Jan 2000: RAVE, Glide and software; OpenGL mentioned but not confirmed at launch; <https://static.classicmacdemos.com/demos/unreal-tournament/README.html>); *Oni* (Jan 2001, Carbon, OS 9 and X, OpenGL). [period] the Quake III-engine games (*Elite Force*, *Return to Castle Wolfenstein*), *Alice*, *Giants*, Aspyr's *Tony Hawk* ports |

**The Eidos line decides it.** [checked] Westlake's *Tomb Raider* ports moved
to OpenGL early: *III* (1999) already offers OpenGL, *The Last Revelation*
has OpenGL and no Glide, and *Chronicles* (2001) is OpenGL-only and Carbon
(<http://www.users.on.net/~macraider/tombraider/tr_osx_classic.html>,
<https://www.macintoshrepository.org/2805-tomb-raider-the-last-revelation>).
So an Eidos Omikron of 2000, ported by the same studio, would most likely
have been **OpenGL**, with RAVE/Glide the less likely alternative (the
*Unreal Tournament* route). §3c's order - GL first, RAVE optional - is the
period's own as well as the practical one.

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
* **Movies** - none needed: MPEG-1 is a byte stream, and QuickTime reads the
  same files.

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

[repo] Audited 2026-10-01, read only. Almost every reader assembles values
byte by byte, little-endian first, and is endian-neutral as written: iam,
morph, anim, ctl, scx, sfx, fnt, addresses, placements, camedit, pose,
player, spatial, props, **the VM's operand fetch** (`script/interp.cpp`),
dialogue, objects, inventory, scenehost, **the DB** (`script/gamestate.cpp`),
**the save and its thumbnail** (`script/savefile.cpp`), the mixer's WAV
header, ADPCM, the `.3DT` LZ stream. Their `f32` helpers `memcpy` an
already-assembled `uint32` into a float, which is neutral too. **No file
format changes, and the save is already written portably.**

What is NOT neutral - **12 sites in 9 files, plus one line**:

| site | what | how it fails on big-endian |
|---|---|---|
| `formats/opt.cpp:12` (`at<T>`, used 33, 53-86) | raw `memcpy` of u32/i32/i16/float from `.OPT` | the whole traffic circuit; a swapped lane origin may pass the range checks and put walkers in the wrong place SILENTLY |
| `formats/mesh3do.cpp:237`, `:244-247` | the body-sphere count and its four floats | wrong spheres - the crowd push |
| `formats/light3do.cpp:16,21` (`u32at`/`f32at`) | the 304-byte light records | wrong colours and radii, silently |
| `formats/map2d.cpp:14,20,26` | MAP2D `.MPT` | the map and the shoot AI's navigation grid |
| `ui/radar.cpp:146`, `:148` | bulk float3 and u16 edge copies from `.WRE` | the one LOUD failure: the edge check (`i >= nv`) rejects every file and the radar vanishes |
| `script/area.cpp:964` | the int16 weapon slots at `IAM\GLOBAL`+42 | wrong weapons |
| `script/area.cpp:1996` (`typeOfActor`) | the actor record's +176 | the wrong shoot-AI type, silently |
| `actor/pedestrians.cpp:82,112` | float3 root keys from `.ani` | walkers drift |
| `app/playhelpers.cpp:23` (`wavToDevice`) | casts the WAV data to `int16_t*` | every interface sound full-scale noise |
| `backends/sdl/play.cpp:870` | `AUDIO_F32`, which SDL2 defines little-endian | the mix as noise; `AUDIO_F32SYS` is the fix |

Only the radar fails visibly; everything else is wrong without an error,
which is why 3e's big-endian `verify.py` run comes before any Mac work. The
fix is one header (`formats/le.h`, say: `le16`/`le32`/`lei16`/`lei32`/`lef32`
over a span and an offset) replacing the nine local helpers, per-element loops
(or a swap pass gated on `std::endian::native`) for the two bulk copies and
the WAV cast, and `AUDIO_F32SYS`. A source scan banning `memcpy` from a data
span outside that header would stop a new site appearing - written over the
invariant, not over today's call sites (CLAUDE.md §1, the rotting scan).

Output-only and safe: the `--dump` frames are written as explicit lo/hi bytes,
the frame hashes (`raster.cpp:418`, `screendraw.cpp:1768`) split each pixel
into bytes, and the GLES row hash and the depth tie's bits differ by host but
are only compared within one run. No unions, packed structs, bitfields over
data or `bit_cast` anywhere. NEON paths are behind `__ARM_NEON`, so PowerPC
takes the generic path; an AltiVec path would be an optimisation for a G4,
not a fix. Not audited: `third_party/` (pl_mpeg, the Vulkan loader),
`backends/vulkan`, `backends/vita`.

#### 3a-i. Run on a big-endian CPU, and fixed - 2026-10-02

Done the way §3e proposed, but with Tiger itself rather than `qemu-user`:
`engine/ppc-darwin.mk` cross-compiles `src/` and all 196 tools for
`powerpc-apple-darwin8`, and the tools ran in Tiger under ppcosxkvm against
the same data as their Mac builds. **Before the fixes the audit held exactly**:
every tool reaching an audited raw read disagreed - `shoot_range` found **0**
actor records against 1032, `map2d_probe` printed 16 map lines against 79,
`veh_probe` failed (exit 1), `ped_probe` and `light_probe` differed - while
`dump_world_data` and `combine_probe`, which read byte by byte, matched to the
output file's MD5. And `opt.cpp`'s range checks do NOT reject a byte-swapped
circuit: `ped_probe` ran to exit 0 on it, with the wrong counts.

The fix is `src/formats/le.h`, `loadLE<T>(const std::byte*)`, at all twelve
sites of the table above plus `playhelpers.cpp`'s WAV - and **eleven more in
six TOOLS** the audit had not covered (`shoot_range`, `slider_call`,
`slider_door`, `ped_probe`, `shoot_trigger`, `light_probe`; `slider_call`,
`slider_door` and `shoot_trigger` fixed by reading, not yet run on PowerPC): the probes read
the data themselves, and a probe's raw read is as wrong on PowerPC as an
engine's. `shoot_range` was the instructive one - two bytes copied into an
`int` and masked with `0xFFFF`, which on big-endian keeps the half the bytes
did NOT land in. After the fixes all seven tools give the same output on
both CPUs, and on the Mac every output is byte-identical before and after
(the fixes change nothing on little-endian).

**One residue was not byte order**: `veh_probe` still differed by 0.1 unit
after 300 simulated frames. GCC fuses `a*b+c` into PowerPC `fmadd` by default
and rounds once where the host rounds twice; built with `-ffp-contract=off`
on both sides the outputs are identical, so `ppc-darwin.mk` sets it - and a
host build to compare against must too (`make -f ppc-darwin.mk PPC_CXX=c++
PPCFLAGS=-ffp-contract=off LDFLAGS= OUT=build/host-ref`).

#### 3a-ii. Every tool a check runs - 2026-10-02

Then all of them, with the arguments the checks themselves use: a worktree
whose build rule wraps each tool in a recorder ran the 148 `engine:` checks
that call a tool (four worktrees in parallel), which logged 1959 calls - 1928
distinct, over 146 tools - and each was replayed on PowerPC in Tiger and on
the Mac (`-ffp-contract=off` both), comparing printed output (timings
masked), exit code and every file written. **1927 of 1928 match**; the last,
`dump_cull`, differs only in line ORDER (it lists a directory, and HFS+ and
APFS return entries differently). On the Mac all 1928 are byte-identical
before and after every fix below.

What the run found, beyond the twelve sites:

* **No more byte order in the engine.** Every remaining difference was a
  TOOL's.
* **Three use-after-frees, invisible on the Mac.** `launch_scene` built an
  IAM archive over a temporary file buffer and `diff_traces` handed one to
  `appendDialogScripts`, so their spans dangled; `used_object_probe` kept a
  pointer into the zone registry across `useObject`. On the Mac the freed
  memory stays mapped and still holds the old bytes, so all three gave the
  right answers by luck; Mac OS X 10.4's allocator returns large blocks, so
  the first two SEGFAULTED and the third printed zone 21075 for 3887. A
  crash log (`~/Library/Logs/CrashReporter`) and the binary's own symbols
  were enough to place each.
* **Tool output in the CPU's byte order**: 18 tools wrote their result
  arrays with a raw `ofstream::write`, which verify.py reads as
  little-endian - now `writeLE` in `formats/le.h` (one write, unchanged, on
  a little-endian host). And `play_dialog` and `run_audio` hashed 16-bit
  PCM by its in-memory bytes - now low byte first, the same value on the Mac.
* **Overflow-free bounds** in the IAM directory (`iam.cpp`) and `zonesOf`:
  `off + size <= n` WRAPS with PowerPC's 32-bit `size_t`. Found while
  chasing the crashes, which turned out to be the dangling spans instead, so
  no observed failure stands behind these two; kept because the wrap is real.

**Not covered**: the 50 tools no check calls (no known-good arguments to
replay) - `light_probe`, `map2d_probe` and `shoot_range` among them were
compared by hand in 3a-i. And `backends/sdl/sdlfront.cpp`'s `AUDIO_F32`
(should be `AUDIO_F32SYS`): the viewer, not in the PowerPC build.

How it was run, for next time: the harness, the recorder and the worktrees
live outside the repo (the tools volume's `work/`); a Tiger run of all 1928
calls takes about 1.5 h, mostly `probe_grid`, and must be started DETACHED
inside the VM (`nohup`), as must QEMU itself - a background command on the
host is stopped after two hours. Copy files into Tiger with `scp`, not a
macOS `tar`: a sparse file (`traces/games-resto.bin`) goes as a pax sparse
entry, which Tiger's GNU tar 1.14 unpacks as a DIRECTORY.

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
| posed bodies, 16.4 MB in 921 allocations | ONE frame scratch pool that every body is transformed into and submitted from, as `sub_4947F0` does - no posed copy kept per body (see 3b-i) |
| the music, 16.1 MB decoded | streamed in small chunks |
| texture pixels, 12.6 MB | the 58-slot cache RUN as a bounded cache, 16-bit |
| collision soups and grids, 11.6 MB | what the engine's probe keeps resident; the grids are the port's own and can coarsen |
| playing sounds, 9.4 MB | the 160-buffer bank, 16 voices, ADPCM decoded on demand |
| the depth tie, ~10 MB | gone on a hardware Z path (a 16-bit Z-buffer IS first-wins); the 18 sign pairs by draw order or `glPolygonOffset` |
| `play.cpp`'s harness state | a lean frontend without the instruments (`play-split.md`) |

Every enhancement (Vulkan, SSAA, mapped shadows, per-pixel lighting,
mipmaps) is out of the build at compile time.

#### 3b-i. How a body poses - read 2026-10-01

[repo] **Mostly rigid, with no weights**: every corner is posed by exactly
one mesh, `pos[m] + R(q[m])·(rest − mesh.pos[m])` (`engine/src/actor/pose.h`
~300-331, `todo/gpu-skinning.md`). **But not purely per-mesh**:

* a NEGATIVE face index points at an ANCESTOR mesh's vertex, so a seam
  triangle mixes two matrices - **9405 corners across 40 of the 181 models**
  (`docs/ASSETS.md`, `docs/FILE_FORMATS.md`, `o3de/geom3do.h` ~108-131).
  Posing them by the declaring mesh "tears the model open at the shoulders";
* the face (`*visage*`, ~130 vertices) takes its positions from the `.3DM`
  stream while a line plays (`pose.h` ~266-315);
* the street's vertex lights are per vertex: `ramp[c][-(N·L)]` with a
  `(t*c)>>8` and a saturating clamp (`o3de/vertexlight.h`), needing the
  posed normal - for walkers, vehicles and the ride only (`sub_4380B0`);
  staged bodies carry none.

So "one `glLoadMatrix` per mesh" covers most of a body but not the seams or
the face, and the GLES backend's GPU skinning (a per-corner mesh slot and a
`uPose[96]` uniform) needs vertex programs a Rage card does not have. And
without hardware T&L the GL driver transforms on the CPU anyway, so a matrix
per mesh saves copies and bus traffic, not arithmetic. **The faithful model
is the original's own**: transform every vertex once a frame on the CPU into
one pre-transformed buffer, light the street bodies there, submit, keep
nothing. The port's step 10 already took a body pose from ~30 to ~10 µs
(`optimization.md` §10); 3b's row is about where the result LIVES.

The CPU side, generally:

* the transform on the CPU, as the engine did - neither Rage card does T&L,
  and doing it in OMK lets the visible-set walk and the drawable mask cut
  first;
* crowd lights and posing stay (they are the game's work), into the scratch
  pool, no allocations (`optimization.md` step 18), floats never doubles;
* scale with the GAME'S OWN options - row 3 (clip distance: visible set,
  bucket splits, fog), row 6 (density), rows 5/7 (shadow detail) - defaulted
  low as a 1999 PC would have had them;
* [repo] `Game_Frame` sets the delta to 30/fps (`docs/BOOT.md` §4), so the
  game is correct at 15-20 fps; a G3 need not hold 30.

### 3c. The renderer

Two backends, in this order:

1. **OpenGL 1.1/1.2 fixed-function, under Carbon (AGL)** - the practical one
   AND the period's likely one (§2a). It runs on OS 9 (OpenGL 1.2.1, with an
   ATI renderer) and natively on Tiger/Leopard. [repo] The shipped render
   states map onto fixed function one for one: Gouraud vertex colour,
   additive and multiply blends, alpha-test cutout, LINEAR black fog, dither
   on, point sampling (CLAUDE.md §4, `sub_4638C0`). The mirror is draw order
   and depth with no stencil (`docs/ASSETS.md` 4c), which suits a Rage 128.
   Vertices go in pre-transformed (3b-i) through an orthographic projection.
2. **RAVE** - optional, the *Unreal Tournament* route: closest to the
   engine's own pre-transformed D3D path and kind to a Rage Pro. OS 9 only;
   RAVE does not exist on OS X.

### 3d. The platform layer

* **Toolchain, classic Mac OS**: [checked] **Retro68** ships **GCC 16.1**
  and builds PPC CFM/PEF applications; `-carbon` links `CarbonLib` instead of
  `InterfaceLib` (<https://github.com/autc04/Retro68>). Headers are the free
  Multiversal Interfaces by default, Apple's Universal Interfaces 3.x as the
  more complete alternative. **NOT confirmed** and decided only by a test:
  the C++20 library level (`std::span` should hold, being header-only),
  `<thread>` and `<filesystem>` (very likely absent), exceptions and RTTI on
  PPC, whether ONE Carbon build really runs on OS 9 and OS X both, and which
  of OpenGL/AGL, DrawSprocket, Sound Manager and RAVE the headers carry. The
  engine is C++20 (`std::span` 378 sites, `std::filesystem` 3); plan for no
  RTTI and ideally no exceptions - `bad_alloc` becomes a pool-exhausted path,
  which is what the original's silent-when-full pools do.
* **Toolchain, Mac OS X PPC**: [checked, from forum snippets] the unofficial
  MacPorts for PowerPC builds **GCC 14** on Leopard (gcc10-bootstrap, then
  gcc14), Tiger more thinly
  (<https://forums.macrumors.com/threads/macports-development-for-powerpc-10-4-10-5-10-6-unofficial-invitation-to-cooperate.2363509/page-4>).
  Its own libstdc++ must ship with the app, since the system one is GCC
  4.0's. No ready cross-compiler from a modern host was found; clang on PPC
  Darwin is work in progress. A community **SDL2 2.0.6** runs on Leopard PPC
  (alex-free/leopard-sdl2; controllers reported not working;
  <https://forums.macrumors.com/threads/sdl2-for-legacy-mac-os-x-sdl-v2-0-3-for-mac-os-x-panther-10-3-9-sdl-v2-0-6-for-leopard-10-5-powerpc-intel.2262878/>),
  so on OS X the existing SDL frontend may carry over.
* **No threads** on OS 9: loading becomes a TICKED state machine, as the
  original's `Area_TickLoad` is; the voice read-ahead
  (`platform/threads.*`) decodes a slice a frame. OS X keeps pthreads.
* **Video**: 640x480, 555. **Audio**: Sound Manager double-buffer callback.
* **Files**: colon paths. [repo] **HFS's 31-character limit is not a
  problem**: of 2812 files under `gamedata/` the only component over 31 is
  `Désinstaller The Nomad Soul..lnk` (34), the Windows uninstaller shortcut,
  which a Mac disc would not carry - and the only non-ASCII name.

#### 3d-i. One Carbon binary, run on both - 2026-10-02

Settled by a test program built with Retro68 (`-carbon`, Apple's Universal
Interfaces) - ONE binary, run unchanged on **Mac OS 9.2.1** (QEMU `mac99`
with the Screamer fork, CarbonLib) and on **Mac OS X 10.4.11** (ppcosxkvm,
through `LaunchCFMApp`). Both wrote the same results:

* C++20 holds - `std::span`, ranges, `<bit>`/`std::endian`, `std::map`,
  `std::unique_ptr` - and so do **exceptions** (a `std::runtime_error`
  caught) and **RTTI** (`dynamic_cast`); big-endian, `sizeof(size_t)` 4;
* **`std::thread` does not exist** (no thread model in Retro68's
  libstdc++) and **`std::filesystem` compiles but does not link** (it needs
  POSIX `symlink` and kin). On OS 9 the voice read-ahead becomes ticked work
  (3d's "No threads") and the three `std::filesystem` uses go through
  `DataFs`; Tiger's own cross-compiler (3a-i) has both;
* Retro68's `printf` prints `%zu` as the letters `zu` - OMK's `%zu`
  formats need `%lu` and a cast on that target.

How it was run: OS 9 has no shell, so the program went into the OS 9 disk's
`System Folder/Startup Items` (mounted on the Mac as HFS+), ran at boot,
wrote its file beside itself and called `FlushVol` - OS 9 caches writes and
the emulator is stopped, not shut down - and the file was read back off the
disk image. On Tiger: `ditto -c -k --sequesterRsrc` keeps the resource fork
in transit, and `LaunchCFMApp` runs it from a shell.

#### 3d-ii. The game on Tiger - 2026-10-02

`omk-play` - the viewer, software renderer, no instruments - runs in Mac OS
X 10.4.11 on PowerPC (ppcosxkvm): the three FLIS films with sound (EIDOS
dropped 5 of 386 frames to keep up), the splash, the music, the start menu,
and on into the intro conversation with its subtitles.

How it is built: SDL 2.0.3 - the last SDL2 that runs on 10.4 is a community
port, alex-free's `panther-sdl2` (Thomas Bernard's Tiger patches) - built
NATIVELY in Tiger with Xcode 2.5, because its Cocoa half is Objective-C and
the cross-compiler has only C and C++; its static `libSDL2.a` then links
with the cross-compiled game: `make -f ppc-darwin.mk SDL2_PREFIX=... play`.
Every viewer file compiled against 2.0.3's headers unchanged. The binary is
`ppc_7400` (G4): SDL was built with `-maltivec`, so a G3 would refuse it.

Launch it with `open` on an application bundle, not from a shell: a process
started over SSH after a reboot is not in the desktop session, SDL gets no
window (`bootstrap_register() failed`) and the game exits silently after
"display 640x480". A two-file `OMKPlay.app` whose executable is a script
running `omk-play` with its arguments does it.

**The colours are the EMULATOR's**: with ppcosxkvm's Radeon 9700 the picture
had a strong blue cast and black drew as pure BLUE - alpha in the blue
channel, i.e. 32-bit pixels read as bytes in memory order. SDL 2.0.3 draws
every window through an OpenGL texture (Cocoa has no native framebuffer
there), uploaded as `GL_BGRA` + `GL_UNSIGNED_INT_8_8_8_8_REV`, which is
right on either byte order by the GL spec; SDL's own software renderer
showed the same cast; and with ppcosxkvm's `--vga` safe mode (Apple's
software OpenGL, no Radeon) the colours are correct - confirmed by the
reader. Safe mode is slower than the fix below, though: 5 fps.

**Found and fixed in ppcosxkvm, 2026-10-02.** Instrumenting the emulator's
texture setup (`r300_draw.c`, `set_textures`) showed that over a whole
session - desktop and game - Apple's driver programmed exactly ONE texture
with `TXO_ENDIAN` 2: SDL's 640x480 ARGB frame; every other uses 0. On the
card, 0 is data the CPU wrote through the swapping aperture and 2 is data
copied in raw for the card to swap on read - the same word either way - and
the emulator, which models neither, holds the guest's big-endian bytes in
VRAM in both cases. It reversed them only for 0 (`decode = (off & 3) ==
0`), so a mode-2 texture drew byte-reversed. With 2 decoded like 0 the
colours are right with the Radeon - confirmed by the reader - and the start
menu draws at **30 fps** (1 before, 5 in safe mode). The patch lives with
the tools (`ppcosxkvm-r300-txo-endian.patch`), not in this repo; it is
ppcosxkvm's to take.

### 3e. Testing without a 1999 Mac

* **Correctness on a big-endian CPU, first and cheapest**: build the engine
  for big-endian Linux (ppc64 or s390x) under `qemu-user` and run
  `verify.py --only` there. Every site of 3a becomes a red check - except
  that most of them fail SILENTLY in play, so the run is only as good as the
  checks over the traffic circuit, the lights, the map, the radar, the
  weapons, the shoot type and the sounds.
* **Budget, machine-independent**: milliseconds on an M3 say nothing about a
  G3. Count instead - peak live heap, allocations a frame, vertices
  transformed, triangles submitted, texture bytes resident - under a "1999
  budget" mode that caps the heap (48 MB, say) and fails loudly past it.
* **Mac OS 9.2.2 under `qemu-system-ppc -M mac99`**, for correctness only;
  its speed is not a G3's. [checked] Sound needs the **Screamer** fork
  (mcayland/qemu, branch `screamer`;
  <https://www.emaculation.com/forum/viewtopic.php?t=9820>); no 3D
  acceleration was found, so GL there is Apple's software renderer or
  nothing; 16-bit colour modes were not confirmed. Not Classic under Tiger:
  [checked, not authoritative] Classic reportedly gets no hardware 3D
  (<https://forums.macrumors.com/threads/opengl-or-old-classic-driver.1654306/>),
  and it is a layer between the build and what it measures.
* **OS X PPC with a GPU**: <https://github.com/linuxkid473/ppcosxkvm>
  (read 2026-10-01) runs Tiger 10.4.11 / Leopard 10.5.8 under QEMU with an
  emulated **Radeon 9700 PRO**, 3D drawn through Metal on Apple Silicon, CPU
  "roughly a G4-era Mac". No OS 9, no Classic. A Carbon build of 3c's GL
  backend can be tested there with acceleration - though a 9700 is far more
  forgiving than a Rage 128.
* **Real hardware last**, for timing.

### 3f. The movies - read 2026-10-01

[repo] Three files, `FLIS/EIDOS.MPG` (5.7 MB), `QUANTIC.MPG` (10.3 MB),
`GAME.MPG` (46.4 MB) - 62.3 MB (`docs/BOOT.md`). Read from their headers:
**MPEG-1 program streams**, video **320x240 at 29.97 fps, 3 Mbit/s** (above
the constrained-parameters limit, ~2.6x a VCD's video rate), 15-frame GOPs
about two thirds B-frames; audio **MP2, 384 kbit/s, 44.1 kHz stereo**. About
388 / 704 / 3212 frames - **~144 s** in all (picture start codes counted, so
approximate). The start menu replays `GAME.MPG` after 1800 idle units
(`docs/UI.md`); `media.play` (op 92) is voice audio, not video; every
cutscene is rendered in-engine. There is no other video.

[estimate] Fewer pixels a frame than a VCD (76800 against 84480), more
bitstream: **comfortable on a 300 MHz G3, marginal at 233**. QuickTime is
the period answer (§2). If OMK decodes them itself (`pl_mpeg`, as now), the
waste is the port's own chain, not the decoder: YCbCr -> RGB24 -> 565 -> a
nearest 2x scale on the CPU (`platform/movie.cpp` ~55-121). Convert YUV
straight to 555 and let the card scale; drop B-frames (nothing predicts from
them) only if still short.

## 4. Steps

Each ends in a commit and a report. Steps 1-4 help every target (the Vita
too) and need no Mac at all, so they are worth doing even if the Mac build is
dropped.

1. **The budget mode and its counters** - the five counts of 3e on the
   street start, recorded here as the baseline.
2. **The memory cuts** of 3b, one row at a time, each "same output, less
   memory" in `optimization.md`'s discipline.
3. **Byte order** - DONE, 2026-10-02 (3a-i, 3a-ii): `le.h`, the PowerPC
   build, every tool a check runs compared in Tiger, 1927 of 1928 identical.
   Left: `sdlfront.cpp`'s `AUDIO_F32SYS`, and the 50 tools no check calls.
4. **The GL 1.1 fixed-function backend** behind `renderer.h`, pre-transformed
   vertices, built and compared against the software reference on the dev
   Mac (PORTING B2: shown to fail).
5. **Retro68 + Carbon bring-up**: the hello-world is DONE (3d-i, 2026-10-02):
   one Carbon binary runs on OS 9.2.1 and Tiger, C++20 + exceptions + RTTI
   hold, no `std::thread`, no `std::filesystem`. The GAME runs on Tiger
   through SDL 2.0.3 (3d-ii). Left for OS 9: the engine itself, with ticked
   loading, Sound Manager audio, 555 video, the movies through QuickTime -
   and before that, the reader's direction of 2026-10-02: every SDL call
   outside the frontend files replaced by calls through a gateway class
   (the `Frontend` interface, extended), so a Carbon frontend can stand in
   for SDL on both systems. Seven `backends/sdl/play*.cpp` files call SDL
   directly today; they are the play split's, so coordinate first.
6. **Correctness in QEMU** - OS 9.2.2 (`mac99`, Screamer), then Tiger on
   ppcosxkvm with the GPU.
7. **RAVE** (optional, the *Unreal Tournament* route).
8. **Real hardware**: a G3 with a Rage 128, then the floor.

## 5. Open questions

Closed 2026-10-01: the HFS name limit (3d - not a problem), the movie codec
(3f - MPEG-1, within a G3), how bodies pose (3b-i - rigid but for the seams
and the face; the original's CPU transform is the model). Closed 2026-10-02:
`opt.cpp`'s range checks PASS a byte-swapped circuit (3a-i); what Retro68
holds, and that ONE Carbon binary runs on OS 9 and OS X both (3d-i).

Still open:

* Period facts not confirmed: *Unreal* (1999) on RAVE + Glide, a Glide
  *Quake*, OpenGL for Mac OS 1.0/1.1's release dates, whether *Unreal
  Tournament* had OpenGL at launch, and whether a Mac Omikron was ever
  announced (§1, §2a).
