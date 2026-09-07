# 3. The data

← [Contents](README.md) · prev: [Boot, and the frame](02-boot-and-frame.md) · next: [The script VM](04-the-script-vm.md)

---

## In short

About 1.7 GB in 2 812 files, and almost all of the game is in there rather than
in the executable. The directories that matter:

| | |
|---|---|
| `IAM/` | 67 files — the **archives**: conversations, the world scripts, the areas, the interface text, the saved games |
| `MESHES/` | 1 271 — models, sets and their textures |
| `SCPTDATA/` | 287 — the scene scripts, their animation clips, paths and sound tables |
| `MORPH/` | 779 — facial animation with the voice recording inside it |
| `FONTS/`, `I2D/`, `IMAGES/`, `MAP2D/` | the interface: 21 fonts, 73 sprite sheets, 45 bitmaps, 32 map screens |
| `FLIS/` | the three intro movies |
| `VOICEOFF/`, `SOUNDS/`, `TRACKS/` | speech, effects and music |

There are two shapes of file. Most are **archives**: one file holding hundreds
of numbered chunks, with a directory at the front. The rest are ordinary files
in a dozen small formats, each read by one loader in the engine.

The important thing about all of it is that the formats are *plain*. There is
no compression anywhere in the shipped tree, no encryption, and no versioning
beyond one field. A record is a struct, an array is an array, and a count is
usually right there. That is what made the whole project tractable.

## In detail

### The IAM container

Files directly under `IAM/` with no extension — `DIALOG`, `AREA`, `SCENE`,
`OBJECT`, `GAMES` — are flat archives, read by `Archive_ReadChunk`
(0x0040FF90):

```
offset 0   directory: an array of 8-byte entries
             uint32 offset      absolute, from the start of the file
             uint32 size        in bytes
             (0,0) means "no chunk with this index"
offset N   payload
```

The directory ends where the first payload byte begins, so **its length is
implied rather than stored** — `IAM/DIALOG` has a 4 096-byte directory, so 512
entries, 420 of which hold data. The loader computes an entry's address as
`(index >> 8) * 2048 + 8 * (index & 255)`, which is `8 * index` written the long
way: it reads a 2 048-byte sector and indexes inside it.

There is a second path in: called with a positive fourth argument,
`Archive_ReadChunk` skips the directory entirely and the caller supplies the
offset and the stride.

### Two files in that directory are not archives at all

This is the trap the repository's first ground rule was written for, and it
cost real work twice:

* **`IAM\GLOBAL` parses plausibly as an archive** and is not one. `Global_Load`
  `fopen`s it — a plain file with a fixed header. Reading it as an archive lost
  2 of its 10 scripts and 86 trigger sites, silently.
* **`IAM\START` does not fit that header either**, and the conclusion "a
  different format" was also wrong: `Game_NewGame` hands it to `State_Apply`.
  It is **the new-game save** — the 8 192-byte game state as it stands before
  the first frame — and reading a section offset in it as a count once produced
  a confident "5 016 scripts".

Both were settled the same way: by finding the loader. A layout that fits the
shipped bytes looks exactly like a correct one.

### The format families

Each is one loader in the engine and one reader in `engine/src/formats/`. The
numbers are what the checks assert:

| format | what it is | established |
|---|---|---|
| `.3DO` | models, characters and sets — and the **lights** inside them | 635 models, 16 188 meshes, 666 cameras; 4 179 light records over 216 models; 99.9986% of every byte claimed |
| `.3DT` | textures | 2 534 of 2 534 byte-identical |
| `.ani` | animation libraries | 243 362 of 243 362 unit quaternions |
| `.CTL` | the actor state machines | 7 of 7 walks landing exactly on the file size; 398 clips; all 2 044 graph edges resolving |
| `.SCX` | scene scripts — objects, programs, camera editings | 220 of 220; 4 511 objects; 6 756 paths |
| `.3DA` / `.3DP` | scene animation clips and authored paths | 1 490 clips; every path's duration field confirmed |
| `.3DM` | facial animation with its voice audio inside | 777 of 777, sample-identical |
| `.SFX` | a scene's sounds and ambient effects | 59 of 59, a six-section walk exact |
| `.OPT` | the city's traffic circuits | 7 blocks, 6 of 6 exact |
| `.FNT` | the interface fonts | 2 899 glyphs, none outside its file, none overlapping |
| `IAM\AREA`, `SCENE`, `GLOBAL` | the world scripts and trigger zones | 5 785 of 5 785 script slots decoding; 4 558 zones, none malformed |

Two audio formats sit under that: **OTNS ADPCM**, transcribed from
`sub_483200` and sample-identical across all 777 morph files, and plain
`.wav` for the 61 interface and effect sounds.

### Case, and why it needed a class

The game shipped with inconsistent casing because Win95 did not care: the
executable asks for `FLIS\EIDOS.mpg` and the disc holds `EIDOS.MPG`; eight of
the scene sound files are spelled `.Sfx` where the rest are `.SFX`, and five
checks once missed them by globbing the wrong one. A count that is quietly
short looks exactly like a count that is right.

So **every data access in the port goes through one class**, `DataFs`, which
resolves case-insensitively — 2 367 of 2 367 shipped files resolve under four
manglings of their path. It is also where the write guard lives: `safeOutputPath`
refuses any path inside the shipped tree or carrying a shipped-data extension,
because a tool whose second positional argument was its output once truncated a
26 KB mesh from the 1999 disc, and the check that noticed it ran afterwards.

### What cannot be read out of the data

Some tables are compiled into the executable, and a replica cannot recover them
from any file: the VM's 153-entry opcode table, the interface widget tree, the
four control schemes, the camera-mode presets, the ADPCM coefficients, the
66 special-move rows. Those are lifted to `tables/*.json` — nine files, each
self-checking, each regenerable by `tools/exetables.py --check`.

This is the one place where the port depends on this repository as well as on
your copy of the game.

## Where it lives

| | |
|---|---|
| the container and the asset formats | `docs/FILE_FORMATS.md`, `docs/ASSETS.md` |
| the readers | `engine/src/formats/` — one file per format |
| all data access | `engine/src/platform/datafs.*` |
| the lifted tables | `tables/*.json`, regenerated by `tools/exetables.py` |
| the Python readers | `tools/omkdata.py` and one module per format |

## What is not settled

* **`.3DM` still has unread fields.** Three `float[3]` tracks whose meaning is
  narrowed rather than settled: read as root-motion deltas they walk every
  character off the map, so that reading is refuted, and what neutralises the
  integral in the engine is untraced. Two node slots (0 and 1) are uploaded
  with ids no drawn mesh binds — not rotations, and measurably not the voice
  envelope either.
* **The Dreamcast `.DDM` variant** is decoded only as far as its section tags.
  It is not this port's subject.
* **The 1999 spec sheet claims a BSP tree** in the models. Every byte of every
  `.3DO` is now accounted for — 460 unexplained bytes across 33 MB — so
  whatever the sheet meant, it is not a structure hiding in those files.
