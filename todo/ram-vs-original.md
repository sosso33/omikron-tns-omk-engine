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

(step 2 fills this)
