# 3. The data

← [Contents](README.md) · prev: [Boot, and the frame](02-boot-and-frame.md) · next: [The script VM](04-the-script-vm.md)

---

## In short

Almost all of the game is in its data files rather than in the executable. The
directories that matter:

| | |
|---|---|
| `IAM/` | the **archives** and the plain state files: conversations, the world scripts, the areas and scenes, the object records, the interface text, the new-game state, the saved games |
| `MESHES/` | models, sets and their textures |
| `SCPTDATA/` | the scene scripts, their animation clips, paths and sound tables |
| `MORPH/` | facial animation with the voice recording inside it |
| `FONTS/`, `I2D/`, `IMAGES/`, `MAP2D/` | the interface and the maps |
| `FLIS/` | the three intro movies |
| `VOICEOFF/`, `SOUNDS/`, `TRACKS/` | speech, effects and music |

There are two shapes of file. Some are **archives**: one file holding hundreds
of numbered chunks, with a directory of offsets at the front. The rest are
ordinary files in a dozen small formats, each read by one loader in the engine.

The formats are *plain*: a record is a struct, an array is an array, a count
is usually right there, and many files are the loader's memory image of
themselves. They are not all raw — textures are LZ-packed and the audio is
ADPCM — but nothing is encrypted, and every packing scheme has been decoded
exactly. That is what made the whole project tractable.

They are also **little-endian**, because the game ran on a PC. A reader that
copies bytes straight into a number works on a PC and an ARM Mac and gives
silently different numbers on a PowerPC — which the port met when it went to
run on old Macs.

A file is also often more than its name says. The `MAP2D` files are the maps
the player sees — and also the navigation grid shoot mode's gunmen walk by. A
mesh flag that makes a skyline shimmer also marks the bed of a canal you can
swim in. Nothing in the data announces that; each was found by reading the
code that uses it.

## In detail

### The IAM container

The archives — `IAM\DIALOG`, `IAM\AREA`, `IAM\SCENE` among them — are flat
files read by `Archive_ReadChunk` (0x0040FF90) (`docs/FILE_FORMATS.md` §1,
*verified*):

```
offset 0   directory: an array of 8-byte entries
             uint32 offset      absolute, from the start of the file
             uint32 size        in bytes
             (0,0) means "no chunk with this index"
offset N   payload
```

The directory ends where the first payload byte begins, so **its length is
implied rather than stored** — `IAM\DIALOG` has a 4 096-byte directory, so 512
entries, 420 of which hold data (§1). The loader computes an entry's address as
`(index >> 8) * 2048 + 8 * (index & 255)`, which is `8 * index` written the long
way: it reads a 2 048-byte sector and indexes inside it.

There is a second access path: called with a positive fourth argument,
`Archive_ReadChunk` skips the directory and the caller supplies the offset and
the stride. That is how **`IAM\OBJECT`** is read — not a directory archive at
all, but 1 002 fixed slots of 2 048 bytes, each read as a 1 304-byte record
(`docs/FILE_FORMATS.md` §5e).

Because the offsets are **absolute**, a copy that gains even a few bytes shifts
every chunk after them while the directory still reads cleanly. The port met
exactly that on the PS Vita: an FTP client in ASCII mode turned each CR LF in
the extension-less IAM files into CR CR LF, the console's `IAM\AREA` came out
1 253 386 bytes against the shipped 1 253 376, and area 118 then read with no
set and no startup script, so the game never left its splash
(`todo/handoff-vita-port.md` §3). The archives must be copied in binary mode.
The port's loaders read a file whole or report the shortfall (`readWholeFile`,
`engine/src/platform/datafs.h`), and the session logs the file's size and hash
when an area chunk comes out with neither a set nor a startup script, which
separates a bad read from a bad copy.

### Every byte of an AREA or SCENE chunk

The world's archives are accounted for to the byte (`docs/FILE_FORMATS.md`,
"Every byte of an AREA / SCENE chunk, accounted for"; `verify.py: chunk
accounting`). The object and prop tables tile with **0 overlaps in 330
chunks** once a `+8` that had been read as a table header is read as what it
is, a field offset inside each record. Scripts are claimed by
**reachability**, not by a linear decode, because a script can jump forward
over its own `end`.

What is left is **two runs, 65 bytes** — an unhooked startup script in AREA 45
and a countdown in AREA 0 — both decoding clean and reached by nothing. It was
three runs and 97 bytes until 2026-10-11: the third was the tail of a
shooting-gallery script that a wrong operand length (op 142, chapter 4) had
cut in half, and at the right length the script runs on into it (`d845dec`).
A decode that ends on `end` had hidden it — the instruction before that `end`
had the wrong length.

### Three files in that directory are not archives at all

This is the trap the repository's first ground rule was written for
(`CLAUDE.md` §1), and it cost real work twice:

* **`IAM\GLOBAL` parses plausibly as an archive** and is not one. `Global_Load`
  (0x0040DE60) opens it directly — a plain file with a fixed header. Reading it
  as an archive lost 2 of its 10 scripts, silently (`docs/FILE_FORMATS.md`
  §5d). The header's walk lands exactly on the file size (6 584 + 44 × 4 =
  6 760), and its `+32` is the weapon table: five ammunition types, then the
  guns that fire them, index-aligned — slot *n* paired with slot *n* + 5 on all
  five, a check the data could fail flatly (§5d; `verify.py: weapon table`).
* **`IAM\START` does not fit that header either**, and the conclusion "a
  different format" was also wrong: `Game_NewGame` hands it to `State_Apply`,
  the function that applies a saved game. It is **the new-game save** — the
  8 192-byte game state as it stands before the first frame (§5d;
  `docs/GAME_STATE.md`).
* **`IAM\GAMES`** is created at run time and is not shipped: a 3 496-byte
  settings header and 256 slots of 32 808 bytes (`docs/PRACTICE.md` §4, the
  save directory row). Chapter 5 has it.

`GLOBAL` and `START` were settled the same way: by finding the loader. A
layout that fits the shipped bytes looks exactly like a correct one.

### The format families

Each is one loader in the engine and one reader in `engine/src/formats/`. The
numbers are what the checks assert, and most of them sit at tier 1 or 2 of
`docs/PORTING.md` B1 — the data checked against itself, which establishes the
layout and nothing about how the engine uses it:

| format | what it is | established | source |
|---|---|---|---|
| `.3DO` | models, characters and sets — and the **lights** inside them | 635 models; 4 179 light records over 216 models; 99.9986% of 33 370 836 bytes claimed, 611 models byte for byte | `FILE_FORMATS` §5b; `verify.py: .3DO bytes`, `light record`, `engine: 3DO` |
| `.3DT` | textures: a palette, then LZ-packed pixels, in material order | 2 534 of 2 534 exact | `ASSETS` §3; `textures`, `engine: 3DT` |
| `.ani` | animation libraries | 243 362 of 243 362 unit quaternions | `ASSETS` §7; `.ani quaternions` |
| `.CTL` | the actor state machines — saved memory images whose pointers the loader recomputes | 7 of 7 walks exact; 398 clips; all 2 044 graph edges resolve | `ASSETS` §7; `.CTL walk`, `ctl transitions` |
| `.SCX` | scene scripts — objects, programs, camera editings, effect sprites | 220 of 220 | `FILE_FORMATS` §5c; `.SCX scenes`, `engine: SCX` |
| `.3DA` / `.3DP` | scene animation clips and authored paths | 1 490 clips; the header duration equals the last key's frame in all 6 756 paths | `docs/PRACTICE.md` §4; `ASSETS` §7 |
| `.3DM` | facial animation with its voice audio inside | 777 of 777 files, sample-identical | `FILE_FORMATS` §5; `.3DM files`, `engine: morph+ADPCM` |
| `.SFX` | a scene's sounds, shot sprites and ambient effects | 67 of 67, a six-section walk exact | `FILE_FORMATS` §5b6; `sfx files` |
| `.OPT` | the city's traffic circuits | 7 blocks, 6 of 6 files exact | `STREET_LIFE` §2; `opt tracks` |
| `.MPT` (`MAP2D/`) | a location's map, and the shoot AI's navigation grid | 16 files, 79 floors; the walk lands exactly | `FILE_FORMATS` §5b5; `map2d`, `map2d grid` |
| `.WRE` (`RADAR/`) | the radar and ambience wireframes | 15 files, 11 exact and 4 shipped one edge short; all 35 788 edges reference valid vertices | `FILE_FORMATS` §5b5; `wre wireframes` |
| `.FNT` | the interface fonts | 2 899 glyphs, none outside its file, none overlapping | `docs/PRACTICE.md` §4; `ui fonts`, `engine: fonts` |
| `IAM\AREA`, `SCENE`, `GLOBAL` | the world scripts and trigger zones | 5 785 of 5 785 script slots decode; 4 558 zones, none malformed | `SCRIPT_VM`; `world scripts`, `zone records` |

Two audio formats sit under that: **OTNS ADPCM**, transcribed from
`sub_483200` and sample-identical to the Python reader across all 777 morph
files, 225 441 216 samples (`docs/PORTING.md` A5; `engine: morph+ADPCM`), and
plain `.wav` for the interface and effect sounds.

### One file, two jobs

**`MAP2D/*.mpt`** is loaded with an area that names one at `+106` (16 areas
do, all shipped except the cut `ARCHIV04`; `FILE_FORMATS` §5b5). Per floor it
carries a grid of byte cells — the picture of the in-game map and its reveal
state — and it is also what shoot mode's gunmen think with: `Shoot_Think`
converts a gunman's world position into a cell of his floor and tests cells
before choosing a destination, and a mover stamps 0x80 over the cell he
stands in, so the grid is also the engine's occupancy map. A cell of 0x10 is a
door, crossable only while the scene says that door is open. The data agrees
16 of 16 both ways: every area whose scripts carry a shoot opcode names a map,
and the two maps left over belong to the areas the Supermarket and Toits
gunfights are loaded over (§5b5; `verify.py: shoot arenas`). Another section of
the file holds the patrol routes — 53 shipped, 51 closed rings and 2 two-point
sentry beats (`verify.py: shoot patrol`). Chapter 6 has the gunmen.

**Mesh flags** do the same kind of double duty:

* `0x8000000` makes a mesh's vertex colour oscillate on the frame clock — the
  shimmer, over 233 set meshes and no character (`ASSETS` §4c; `shimmer
  table`) — and in Jaunpur it is also what marks the canal's bed and banks,
  the floor that sends an actor into the water (`ASSETS` §7, `ACTOR_STATE`).
* `0x20000000` marks the water's *surface* — the same bit the step refusal and
  the camera's see-through test name (`ASSETS` §7) — and it is also the one
  exemption from the engine's **back-face cull**: the renderer drops every
  face wound away from the camera, in software, unless its mesh carries this
  flag, and **93 of the 16 188** shipped meshes do, nearly all water
  (`ASSETS` §4, corrected 2026-10-01). Read as "the device culls nothing", the
  port drew the back of the scenery for a month.

And the vertex record carried a **normal** at `+12` that nobody had read until
the crowd's dynamic lighting needed it: twelve bytes skipped since the format
was first decoded, fixed as a normal because `sub_493E40` dots it with the
light direction (`FILE_FORMATS` §5b, "The vertex record").

### Case, and why it needed a class

The game shipped for Windows 95/98, whose filesystem ignores case, so the
authors typed whatever they liked: the executable asks for `FLIS\EIDOS.mpg`
and the disc holds `EIDOS.MPG` (`docs/BOOT.md` §2), and eight of the 67 scene
sound files spell the extension `.Sfx` or `.SfX`. Checks once measured 59 of
those 67 while reporting a total, because they globbed two spellings
(`verify.py: extension case`, `sfx files`). A count that is quietly short looks
exactly like a count that is right.

So **every data access in the port goes through one class**, `DataFs`, which
resolves case-insensitively: every one of the 2 367 shipped files under `IAM`,
`SCPTDATA`, `ANIMS`, `MESHES` and `MORPH` is asked for again with its whole
path mangled — all upper, all lower, case-flipped — and all come back
(`verify.py: engine: DataFs`). It is also where the write guard lives:
`safeOutputPath` refuses any path inside the shipped tree or carrying a
shipped-data extension (`CLAUDE.md` §1). The Python readers do **not** resolve
case; they rely on the host filesystem, which is why they are expected to fail
on a case-sensitive volume (`README.md`, "Platforms").

### Byte order

Every shipped file is little-endian. A reader that `memcpy`s a field into an
`int` is right on x86 and ARM and wrong on a big-endian CPU without any parse
failing: run on PowerPC, such reads gave **0 actor records for 1 032**, 16 map
lines for 79 and a traffic circuit that would not load (`docs/PORTING.md`
A9). So every multi-byte read in ported code and tools goes through
`loadLE<T>`, or the checked `le::u32(buf, off)` family that returns 0 past the
end (`src/formats/le.h`, `bytes.h`). The PowerPC replay of every tool call a
check makes — 1 928 calls over 146 tools — came back 1 927 identical, the last
differing only in directory order; that replay is not part of `verify.py`
(A9).

### What cannot be read out of the data

Some tables are compiled into the executable, and a replica cannot recover them
from any file: the VM's 153-entry opcode table and which operand each handler
announces, the interface widget tree, the four control schemes, the
camera-mode presets, the ADPCM coefficients, the 66 special-move rows, the
shoot AI's behaviour scripts and its weapons, the fight AI's eight built-in
moves, the city maps. They are lifted to `tables/*.json` — twelve files, each
self-checking and regenerable (`tables/README.md`; `verify.py: exe tables`).

This is the one place where the port depends on this repository as well as on
your copy of the game.

## Where it lives

| | |
|---|---|
| the container and the asset formats | `docs/FILE_FORMATS.md` §1, §5–5e; `docs/ASSETS.md` §2–§7 |
| the readers | `engine/src/formats/` — one file per format, `map2d.*` among them; `le.h` / `bytes.h` for every raw read |
| all data access | `engine/src/platform/datafs.*` (`DataFs`, `readWholeFile`, `safeOutputPath`) |
| the lifted tables | `tables/*.json`, regenerated by `tools/exetables.py` |
| the Python readers | `tools/omkdata.py` and one module per format |
| the checks | `.3DO bytes`, `textures`, `.ani quaternions`, `.CTL walk`, `.SCX scenes`, `sfx files`, `map2d`, `wre wireframes`, `chunk accounting`, `extension case`, `engine: DataFs`, `exe tables` |

## What is not settled

* **`.3DM` still has unread fields** (`FILE_FORMATS` §7; `docs/PRACTICE.md`
  §6). A `float[3]` at the start of each frame record: read as root-motion
  deltas it walks nearly every character off the map, so that meaning is
  refuted and what it is remains open. Node slots 0 and 1 hold something other
  than quaternions — not the voice envelope; eye-direction or blink channels
  are the shapes that survive.
* **The `.3DO` vertex's `+24` float** has no traced consumer, and 233 of the
  635 models mix unit and non-unit normals — very likely vertices of meshes
  that are never drawn, which is not established (`FILE_FORMATS` §5b).
* **`.CTL` entry flags** are known by position, not by meaning, and its
  32-byte group records and 156-byte second table are walked for their sizes
  without their fields being understood (`FILE_FORMATS` §7). The clips they
  carry are fully readable.
* **The Dreamcast `.DDM` variant** is decoded only as far as its section tags.
  It is not this port's subject.
* **The 1999 spec sheet claims a BSP tree** in the models. Every `.3DO` is
  accounted for to 460 unexplained bytes across 33 MB, short ascending integers
  in 24 files (`FILE_FORMATS` §5b), so whatever the sheet meant, it is not a
  structure hiding in those files.
* **The byte-order rule is held by convention between replays.** The PowerPC
  replay needs an emulator and is not part of `verify.py`, so a new raw read is
  caught only when someone runs it again (`docs/PORTING.md` A9).
