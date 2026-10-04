# Every RAM-consuming item against the original

The reader's ask (2026-10-04): *"Since good results have been done with
comparing with the original, compare every ram-consuming items with the
original game implementation."* The two first comparisons (the music stream,
the crowd's per-slot copies, `todo/debug-tools.md`) each found the port
keeping far more than the original and gave 14 MB back with no visible
change. This does the same for everything else the street holds.

The target this serves is the classic Mac's **64 MB** - a goal, set by the
original's own 32 MB requirement (`classic-mac-port-1999.md` 3b) - but every
build gains.

## How

1. **An inventory by OWNER, not by moment.** The profiler's categories say
   which zone a block was allocated in ("input: motion", "setup: session"),
   and several large ones are moments. So each block also records the code
   that allocated it (the return address of `operator new`), the profiler
   keeps live bytes per site, the capture carries the table, and
   `tools/omkprof.py --sites` symbolizes it (`atos`): every byte with a
   function's name, nothing tagged by hand. A profiling-build instrument
   like the rest (`OMK_PROFILE=0` has none of it).
2. **Per item, the original's mechanism**, read from the binary - what it
   loads, in what form, when it frees it - and the port's, side by side,
   with the bytes each keeps. Recorded in the table below as each is read.
3. **The cuts**, item by item, where the port keeps more than the original
   and the difference is not one this port chose for a reason (each such
   reason written down - the music's 32 KB file window is one: one 4 MB
   read froze a Vita for 0.7-0.9 s). Each cut proved invisible (frames
   byte-identical) and held by a check.

## The inventory

**Step 1 DONE 2026-10-04** (`657914f`): the profiler's allocation sites
(`omkprof.py <capture> --sites`). The street, standing, the Vulkan world
renderer, 900 frames, on an M1 (`sysctl`), a 64-bit build - so every pointer
and `size_t` here is twice the classic Mac's - after the music ring and the
crowd's release (`todo/debug-tools.md`): **72.9 MB live**.

| # | MB | owner (`--sites`) | what it is | what step 2 reads in the original |
|---|---|---|---|---|
| 1 | 14.10 | `omk::textures` | the set's and the bodies' texture pixels, decoded | `Tex3DT_BindMaterials`, the 58-slot cache: 16-bit surfaces, whether the file's pixels are kept after the upload |
| 2 | 11.99 | `omk::buildGeometry` | the set's geometry as expanded corners | `.3DO` load: vertices and faces as the file holds them, transformed per frame into the pool |
| 3 | 9.16 | `PlayState::inputMotion` | the scripted objects' meshes, soups and grids | how the engine moves a scripted set mesh (`Anim` on a node) and what it keeps for its collision |
| 4 | 7.39 | `omk::ScxRuntime` | the scene's `.SCX` (3 blocks) | `Scene_LoadSCX`: what of the file stays resident |
| 5 | 7.19 | `omk::collisionSoup` | collision triangles (6 blocks) | the ground probe and wall test - what geometry they read |
| 6 | 3.00 | `omk::readWholeFile` | files kept whole (IAM archives and more) | whether `IAM` chunks are read on demand or kept |
| 7 | 2.76 | `omk::wavToDevice` | sounds converted to the device's format | `Wav_LoadToBuffer`, the 160-buffer bank: 8/16-bit as shipped |
| 8 | 2.65 | `Geometry::operator=` | geometry copies (218) | - (the port's own: which copies, and whether any need be kept) |
| 9 | 1.63 | `PlayState::rebuildWorld` | the world's draw state | - |
| 10 | 1.25 | `DepthTie::*`, `bakeDepthTie` | the GPU depth tie's tables | none: a GPU-only decision (the original's Z-buffer is first-wins) |
| 11 | 1.23 | `particleGeometry` | particle quads | the sprite/particle pools |
| 12 | 1.18 | `setupSplash`, `Surface` | the splash's 640x480 surfaces, still held after it | the splash's bitmap - freed after the `Sleep`? |
| 13 | 1.06 | `lodRestFor` | the crowd's LOD cuts | `sub_453A70` (already read: once per model) |
| 14 | 0.82 | `clipTracks` | animation tracks (2656 blocks) | `.ani` load: keys as stored |
| 15 | 0.72 | `0x19b20ecbb` | 15694 small blocks in a system library | (unsymbolized: libc++'s strings, likely) |
| 16 | 0.54 | `loadObjects` | `IAM\OBJECT` | the object table's load |
| 17 | 0.50 | `setupOptions` | the options' state | - |
| 18 | 0.35 | `buildSoupGrid` | the collision grids | the port's own (no grid in the original) |

The rest - some 540 functions - is under 0.3 MB each, 4 MB together.

## Item by item

**Step 2 DONE 2026-10-04**: six parallel readings of the decompilation (the
textures, the set geometry, collision and moving meshes, the `.SCX` and the
archives, the sounds, the small items), and the decisive line of each
re-read here before it was written down (`SetMaterialsMemory`'s two
`Mem_Alloc`s, `Scene_Load3DO`'s `File_LoadWhole`, `Read3DO_Init`'s
`Mem_Calloc(count, 0xB8)`, `Scene_LoadSCX`'s block and `fclose`,
`Archive_ReadChunk`'s directory-then-chunk reads, `sub_46C740`'s
`CreateSoundBuffer` in the file's own format, and the port's `ScxRuntime`
copying whole files). MB are the street's, 64-bit; INFERRED is marked.

| item | port | original | how the original does it | deliberate in the port? |
|---|---|---|---|---|
| **textures** | 14.1 | 3.87 fixed | `SetMaterialsMemory` (0x004406B0): ONE arena of 58 pages of 256x256 **8-bit palette indices**, `Mem_Alloc((58+1) << 16)` aligned to 64 KB, + `768 * n` of palettes; never freed (the software path samples it, D3D re-uploads from it); the card gets PAL8 or 16-bit 1555 (`sub_461D50`); D3D's managed copy is the driver's (INFERRED) | the CPU copy yes (the software raster and the GPU re-uploads read it); the **RGB888** form no - 3x the original's byte a texel |
| **set geometry** | 11.3 (+ 2.65 copies, + 1.63 rebuildWorld) | 2.26 | `Scene_Load3DO` (0x0044EA10) keeps the file AS LOADED (`File_LoadWhole`), `Read3DO_Init` (0x0044DF10) points into it and adds a 184-byte node a mesh (`Mem_Calloc(count, 0xB8)`); vertices transformed each frame into SHARED pools (75000 x 48, 25000 x 124), faces indexed | the flat per-corner layout yes (the GPU vertex, the depth tie, the dirty lists and the motion patch index corners); the 31% `insert` slack, `cornerDeclared` (kept "to render the wrong reading beside") and `cornerVertex` (the face morph only) for decor, no |
| **moving set meshes** (`inputMotion`) | 9.16 | ~0 | a scene program writes a node's matrix; the probe and the sweep read it on their next call - nothing copied | no: `baseCorners` is a copy of the WHOLE set's corners (6.7 MB) to re-place ~30 moving meshes, plus whole-set base soups (1.6) |
| **collision** | 7.19 + player soups / grids ~2 | ~0 | `Walk_ProbeGround` (0x00467030) and `Sweep_ActorMove` (0x004AD360) walk the resident meshes by bounding sphere (`o3de_ForEachMeshInBox`) and test the mesh's own shipped faces IN MESH SPACE (`sub_498B10`, `Sweep_MeshTest`); no grid, no BSP, no separate collision mesh | world-space float soups and the grid yes (one grid for every probe; the slope pre-split); the 2.35 MB of doubling slack and FOUR soups of the same faces (walkable, steep, shot, sight) no |
| **`.SCX`** | 7.39 | ~6.4 | `Scene_LoadSCX` (0x00449750) keeps the 69 KB structural block and gives each streamed resource its own buffer (clips, paths, sounds into DirectSound, sprites), then `fclose`s; the global library has ONE slot (`Game_Start` swaps `aventure.scx` and `fight.scx`) | the whole files kept yes (the readers point into them); `fight.scx` resident beside `aventure.scx` (1.0 MB) yes - "one read instead of one per fight" - and not the original's |
| **IAM archives** | 3.0 | ~0.12 | `Archive_ReadChunk` (0x0040FF90): a 2 KB directory read and freed, then ONE chunk; two area slots keep their chunks; a conversation's chunk is freed when it ends | yes: kept whole (2.6 MB) because a Vita card's read took 0.7-0.9 s; and a BUG beside it - `SceneRunner::load` and `loadArea` still read AREA and SCENE whole on every load, past the cache |
| **sounds** | 2.76 (+ the raw ones inside the kept `.SCX`) | 5.07 | `sub_46C740` creates each DirectSound buffer in the FILE's own format (16-bit mono, native rates); a playing sound is a `DuplicateSoundBuffer` sharing the data | no: `wavToDevice` makes float stereo copies at 22050 Hz, ~4x a mono source |
| posed bodies' copies | 2.65 | 0 | one per-frame scratch pool (`sub_4947F0`) | the crowd's release (done) does not cover the staged bodies' `g = rest` |
| depth tie | ~1.25 (its own comment: up to 9.7 across a street's bodies) | 0 | a first-wins Z-buffer needs no state | a GPU decision; not needed by the software renderer |
| particles | 1.23 | fixed pools (INFERRED) | - | capacity kept at the peak frame |
| animation tracks | 0.82 | same keys | `.ani` keys are 16-byte float quaternions, as the port's | the keys yes; a heap block per frame no |
| `IAM\OBJECT` | 0.54 | ~0.001 | each record read, 56 bytes kept, the record freed | no: up to five copies of the parsed records |
| the splash | 1.18 | 0 | `sub_420A20` frees the bitmap BEFORE its `Sleep` | NOT an item: the 0.59 + 0.59 is the frame's own compose surface `fb` (INFERRED from the source) |

**What it adds up to**: the original holds roughly **12 MB** of these in the
street (textures 3.9, geometry 2.3, the SCX's resources ~6.4 with the sounds
inside them, the archives 0.1, collision and motion nothing of their own);
the port holds **~62 MB** of the same things. The cuts below take back
about **35 MB** of the 73 on a 64-bit host.

## Step 3 - the cuts, proposed in three tiers

**A. Safe, frames identical, small code** (~14 MB): size the soups exactly
(-2.35, bit-identical); `reserve` the set geometry and stop filling
`cornerDeclared` / `cornerVertex` for decor (-3.8); rest copies of the
MOVING meshes only, not the whole set (-7.9 - both the geometry and the
collision readings found it); one shared `IAM\OBJECT` table (-0.45); and
the AREA/SCENE whole-file reads that bypass the cache (a speed bug).

**Tier A DONE 2026-10-04: the street 72.7 -> 58.7 MB (-14.0)**, standing
900 frames through the Vulkan world renderer on an M1, the same run as the
inventory. Every frame byte-identical to the binary before it: standing on
the software reference and on Vulkan, and a 600-frame walk (1052 units, the
motion patch running) - the dumps and the whole logs agree. By category:

| cut | category | before | after |
|---|---|---|---|
| rest copies of the MOVING meshes only (`restXyzOfMesh`, `restSoupOfMesh`, `restSteepOfMesh`: a mesh's own corners and triangles, taken the first time it moves; the GPU path restores before it reads `mm.rest`) | input: motion | 9.17 | 1.40 |
| the set's corners reserved exactly, and `cornerVertex` / `cornerDeclared` dropped for the decor and the sky (`dropCharacterArrays`; the face morph is a body's) | geometry | 11.99 | 8.24 |
| the soups sized exactly (`shrink_to_fit` - the doubling slack) | collision | 7.19 | 4.94 |
| ONE `IAM\OBJECT` table (`sharedObjects`) for the Session's three lookups, the voice-overs and the viewer | objects | 0.54 | 0.27 |
| `SceneRunner::load` and `loadArea` read AREA / SCENE through `archiveBytes` instead of whole on every load | archives | 1.20 | 1.48 |

The last is the speed fix, and it COSTS 0.29 MB: SCENE is now kept like
AREA, as the 2026-09-30 decision kept all three archives - it had simply
never been reached through the cache. Its cost goes with tier C's ranged
reads.

`engine: street memory` holds all five from a 60-frame run (2 s): each
category under its bound at the last frame, the motion patch having run,
and SCENE among the kept archives. Each cut was shown to fail it alone -
and the first geometry bound (9.5) did not see `cornerVertex` /
`cornerDeclared` coming back (9.30), so it is 8.8. The changes are
`f9fad39`; `engine: classic build` caught two `static inline` empty tables
on the way (PORTING A10), now one `noObjects()` in `objects.cpp`.

**B. Medium** (~10 MB): sounds kept 16-bit mono at their own rate and
converted while mixing (-2.76); `fight.scx` swapped with `aventure.scx` as
`Game_Start` does (-1.0); one collision soup with a class byte a triangle
instead of four (-5.3); the staged bodies' copies released like the crowd's
(-2.65 max); no depth tie on the software renderer.

**Tier B DONE 2026-10-05: the street 58.7 -> 51.6 MB (-7.1)**, the same
run as tier A's (Vulkan, standing, 900 frames, M1, 64-bit); on the software
reference 56.9 -> 50.5. Frames and logs identical to the binary before tier
A, standing and walking, on both renderers; the one log line that moved is
the fight library's, now printed at a fight's start.

| cut | category | before | after | how |
|---|---|---|---|---|
| sounds kept as their files hold them | input: sounds | 2.76 | 0.30 | `omk::DeviceSound`: 16-bit at the file's rate, read at the device rate by the host mixer with `wavToDevice`'s own index rule - every sample the same float. `engine: device sounds` proves it over all 1728 shipped sounds (61 interface `.wav`, 1667 in the `.SCX`), and their looped mixes, bitwise. Only 2 of them are stereo |
| the staged bodies' posed copies released after two idle seconds | staged skin | 1.79 | 0.07 | as the crowd's slots (`releaseIdleStaged`); 68 releases over 25 bodies in a 600-frame street run, the re-pose after one exercised |
| the sight ray through the shot soup and a cutout byte a triangle | collision | 4.94 | 3.40 | `omk::cutoutMask`; the unmasked shot triangles ARE the sight soup in order, so the same face wins (`engine: sight mask`, 220 sets, 14080 rays) |
| `fight.scx` and `shoot2.scx` loaded on entering their mode, released on leaving | setup: bodies | 4.02 | 3.01 | as `Game_Start` swaps them; with them their converted samples (`sfxCache` is keyed by the file's bytes). `shoot2.scx` was 4 MB kept after the first shoot phase, outside this street run |
| no depth tie on the software renderer | - | - | - | ALREADY TRUE: the software run's sites hold none; the tie is the GPU renderers' |

**What was NOT done, and why**: the plan's "one collision soup with a class
byte instead of four" in full. Walkable and steep are disjoint subsets
(15137 + 31141 triangles in Anekbah, 1.6 MB), each with its own grid and
its own moving-mesh patch; folding them into one soup would renumber the
triangles every probe, grid and patch walks, and a tie between two faces is
decided by that order - not a cut that can be shown identical by
construction. The tier's -5.3 was measured before tier A took the soups'
slack. The interface sounds (`loadSlot`, the 32-slot cache) still convert
to float: none is resident in the street.

**The cost, named**: entering a fight now reads `fight.SCX` (1 MB) and a
shoot phase `shoot2.SCX` (4 MB) on the frame the mode starts, as the
original's `Game_Start` does - on a console card that is a pause of a
fraction of a second where the port had none.

Checked with `--only` over the 49 checks tier B could reach - the shoot and
fight phases, the staged bodies, the audio, the three builds (`--jobs 6`,
892 s, M1): two red. `licence headers` a census step (545 -> 547:
`hostmix_probe.cpp` of 2026-10-04, never re-pinned, and `sound_equiv.cpp`),
re-pinned; `engine: shoot hit`, red on purpose, its output identical to the
same check run at `641e557` - before tier B - in a worktree.

**C. Large** (~12 MB): textures kept as 8-bit indices + palette, expanded
at upload and looked up by the software raster (-9.4; every pixel reader
changes); ranged archive reads like `Archive_ReadChunk` (-2.4; to be timed
on a Vita card first); an indexed decor geometry (toward the original's 2.3).

**Tier C, two of three DONE 2026-10-05: the street 51.6 -> 40.8 MB
(-10.8)**, Vulkan, standing, 900 frames, M1 (software 50.5 -> 39.9).
Frames identical to the binary before tier A standing, walking and through
the new-game opening; the software renderer the same speed (22.1 s for 300
frames either way).

| cut | before | after | how it's held |
|---|---|---|---|
| textures as the file holds them: 8-bit INDICES and a 256-entry palette (`Texture::idx` / `pal`), expanded where read - the software sampler looks the colour up as the original's software path does, the GPU uploads take keyed RGBA from a 256-entry table (3.1x faster than the per-texel test, EXACT - the Vita bench's `texkey` stage) | 14.10 | 4.77 | `engine: indexed textures`: every shipped texture - 2534 under MESHES, 230 sprites in the `.SCX` - hashed as RGB, equal to the hashes the RGB decode gave before the change; a third of the bytes |
| a chunk read alone, as `Archive_ReadChunk` does: each archive's directory kept (`IamArchive::directory`, from the file's head against its size), a chunk one seek and one read (`readFileRange`: Vita, classic Mac, stdio); the scene -> area map built once and kept instead of AREA and SCENE | 1.48 | 0.03 | `engine: archive chunks`: all 750 chunks of AREA, SCENE and DIALOG byte-identical both ways. The street no longer opens SCENE at all |

**NOT done: the indexed decor geometry.** The flat per-corner layout is
what the GPU vertex buffers, the depth tie, the dirty lists and the motion
patch all index; an indexed one rewrites the vertex path of all three
renderers, and frames identical by construction is not on offer. The set's
geometry is 8.24 MB here against the original's 2.26.

**THE .SCX, DONE 2026-10-05** (asked for in place of the geometry): the
runtime keeps the clips' and sounds' bytes and drops the file, as
`Scene_LoadSCX` keeps its block and a buffer a resource. **Smaller than this
file's step-3 table implied**: the SOUNDS are most of every `.SCX` (2.4 of
Anekbah's 3.5 MB, 2.5 of `aventure`'s 3.0) and the original keeps them too,
in DirectSound buffers - what it drops is the container, the sprites'
embedded models and textures and the raw path records. Measured over all 220
files 94% of the bytes stay; in the street 6.39 -> 5.67 MB, **the street
40.8 -> 40.1**. `engine: scx kept` (1490 clips with their frame counts, 1667
sounds, identical through the runtime); 29 checks over the scene programs,
the sounds and the cross builds green.

**DEFERRED by the reader (2026-10-05), with what it would buy and cost.**
Measured that day: a vertex can be shared only where position, UV, colour,
normal and shimmer phase all agree, and Anekbah's 139245 corners (6.5 MB)
hold 63079 such vertices - indexed, 2.96 MB + 0.54 of indices = 3.5 MB, so
about **-3 MB**, not the -6 the gap to the original suggests (Qalisar 4.2 ->
2.1, Aapkayl 0.5 -> 0.2). The original's 2.26 comes from keeping the file's
own vertices and transforming them each frame into a shared pool - another
renderer design. **The risk** is that a corner's index is an ADDRESS shared
by the motion patch and its rest copies, the back-face cull flags, the
shimmer, the depth tie and the GLES dirty-corner uploads, across three
renderers; a wrong re-keying draws a plausible frame, wrong only when
something moves or only on one backend - invisible to a standing identity
run (CLAUDE.md 1, "invisible at rest"). If it is taken up: keep the corner
index as the shared address and add the vertex index BENEATH it, test every
consumer over the transition (a moving mesh, the GLES partial upload), and
accept part of the saving for it.

**Not yet measured where it matters**: a chunk read on a Vita memory card
(two small reads a load, where the kept archives made it none after the
first), and the ranged read and the palette sampler running on the classic
Mac (both build; neither has been run on OS 9 or Tiger).

Checked with `--only` over the 62 checks tier C could reach - every render,
texture, load and transition check and the three builds (`--jobs 6`, M1):
four red, all this work's. `engine: vita build`, `classic build` and
`release build` - `datafs.h` used `std::uint64_t` without `<cstdint>`, which
the Mac's headers had supplied and the cross toolchains do not; fixed, all
three green. `licence headers` - the census (547 -> 549, the two probes).

