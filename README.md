# OMK — an open-source engine for *Omikron: The Nomad Soul*

A from-scratch, dependency-free C++20 reimplementation of the engine behind
Quantic Dream and Eidos's 1999 game — together with the format notes it is
built on: what each of the game's data formats is, and how each one was
established.

Open source under [GPL-3.0-or-later](LICENSE); the game itself is not
included and not ours to give — OMK reads the data files from your own copy.

**Where it is up to:** OMK plays the opening. From a cold start it steps the
three intro movies, shows the splash, draws the start menu and takes your
answer, runs the Kay'l intro conversation with its dialogue cameras and
voice-over, plays the seven camera editings of the Impasse arrival, and then
**hands you the player**: adventure mode, arrows to walk and turn, a follow
camera, the walkable floor under you, and area transitions that load the next
set. On an M1 that is one uninterrupted run of ~3600 frames through three
sets.

It is not a finished game, and no claim is made about anything past the
opening. The open items are filed in
[`todo/iam-script-engine.md`](todo/iam-script-engine.md) under `## Open` —
the largest being that `Actors_SpawnFromTables` is not ported, so the
world's own ambient characters never spawn; only the ones a script names
with `character.show` appear. [`engine/README.md`](engine/README.md) audits
what is ported row by row, and it has been wrong twice, so trust it over
this paragraph.

The name is not new — it is what the code has always called itself. The C++
lives in `namespace omk`, the replica builds as `build/omk` and its viewer
as `build/omk-play`, the readers are `omkdata.py`, `omkpaths.py`,
`omkweb.py` and `omkdialog.py`, and local configuration is `omk.conf`.
Only the prose had never said it.

Two halves, and the split is the point:

* **`docs/`** — the findings. Every container and asset format the game ships,
  its 153-opcode script VM, its 8192-byte game state, its interface, its
  cutscene system. Each claim carries the evidence that established it.
* **`engine/`** — the engine that consumes them. C++20, no dependencies,
  `make`. Its announcement stream matches a capture of the **original**
  engine, 42 events of 42, from a cold start — so "it boots" above is a
  measurement against the real thing, not a screenshot.

Plus `tools/` (Python readers, four web viewers and the test suite) and
`tables/` (the tables compiled *into* the executable, lifted to JSON — the one
thing a replica cannot read out of the game's data files).

## You need your own copy of the game

**No game data is in this repository, and none ever will be.** Nothing here is
useful on its own; everything reads a copy of the game you supply.

```
omk/
  gamedata/    <- YOU PROVIDE THIS: the game's data directory, from your disc
                  or your GOG install. Must contain `Runtime 2.exe`,
                  `IAM/`, `MESHES/`, `SCPTDATA/`, `MORPH/`, `SOUND/`.
```

Put it anywhere you like instead — a flag, an environment variable or a config
file, in that order of precedence:

```sh
python3 tools/verify.py --data /Volumes/OMIKRON1     # a flag
OMK_DATA=~/games/omikron/data python3 tools/verify.py # the environment
cp omk.conf.example omk.conf && $EDITOR omk.conf      # once, for good
python3 tools/omkpaths.py                             # what resolved where, and why
```

The directory used to be called `fr/`, from a first test against the French
release. It was never a French thing — the executable is the same for every
localisation — so it is `gamedata/` now.

`gamedata/` is **input, and is never written to**. Every tool that takes an
output path routes it through `omk::safeOutputPath`, which refuses any path
inside the shipped tree or carrying a shipped-data extension — because a tool
whose second positional argument was its output once truncated a 26 KB mesh
from the 1999 disc, and the check that noticed ran afterwards.

### The disassembly is optional, and is not here

Some analysis tools read an IDA listing of `Runtime 2.exe` (`Runtime.exe.asm`,
`Runtime.exe.c`) and the `clean/` tree derived from it. **A disassembly is a
derivative work of the binary it came from, so this repository does not
distribute one.** Producing your own is up to you; point at it the same way:

```sh
OMK_ASM=~/ida/Runtime.exe.asm OMK_CLEAN=~/ida/clean python3 tools/verify.py
```

Without it, **14 of the suite's 159 checks report `skipped`** — naming the
variable to set — and the other 145 run normally. `tools/dialog_disasm.py`
falls back to the committed `tables/vm_opcodes.json`, which carries the same
VM table; `verify.py: vm table sources` asserts the two agree, 153/153 operand
counts and 49/49 `.TAG` domains, so the fallback cannot drift unnoticed.

## Quick start

```sh
# 1. put your game data at ./gamedata  (or point OMK_DATA at it)

# 2. check your toolchain              (optional; it only reports and advises)
./scripts/install-deps.sh

# 3. build the replica                 (C++20 compiler; ~11 s from clean)
cd engine && make && cd ..

# 4. boot it
engine/build/omk gamedata --tables tables

# 5. check that everything the docs claim is still true
python3 tools/verify.py --list                 # all 241, and which doc quotes each
python3 tools/verify.py --only "engine: cull"  # one check, seconds
python3 tools/verify.py                        # all the fast ones
python3 tools/verify.py --slow                 # plus the asset sweeps — minutes
```

`verify.py` exits with the number of failures, so it drops into a hook or a
`&&` chain. Checks whose inputs you have not supplied report `skipped` rather
than failing.

### Dependencies

**The Python side needs nothing** — every tool is standard library only, and
`verify.py` deliberately has no PIL dependency (`tools/frame.py` carries a
PIL-free PNG codec to keep it that way). There is no `requirements.txt`
because there is nothing to put in it.

**The C++ side needs only a C++20 compiler.** SDL and the Vulkan loader are
optional and enable the viewers; `make` and `verify.py` work without either,
and `docs/PORTING.md` A1 requires that they keep working without either.
`scripts/install-deps.sh` reports what is present, `--install` adds the
optional pieces via Homebrew, apt, dnf or pacman.

### Platforms — portable by construction, tested only on macOS

OMK is written to be platform-independent: C++20 with no dependencies, a plain
Makefile driven by `pkg-config`, Python that is standard library only, and an
install script with Homebrew, apt, dnf and pacman branches. `engine/src`
contains **no operating-system `#ifdef` at all** — not one `_WIN32`,
`__APPLE__` or `__linux__` in the whole core. The only conditionals in the
tree are in the SDL backend, and they choose between SDL2 and SDL3 or gate
the optional Vulkan path; none of them asks which OS it is on.

**But it has only ever been built and run on macOS on Apple Silicon.** Nothing
else has been tried, so treat any other platform as unexplored rather than
supported. Four things are known in advance to need attention:

* **Case-sensitive filesystems.** The game shipped with inconsistent casing
  because Win95 did not care, so the C++ `DataFs` resolves every lookup
  case-insensitively and is expected to be fine anywhere. The **Python
  readers do not** — they let the host filesystem do it, which works on macOS
  and will not on a case-sensitive Linux volume without normalising the tree
  first. Expect the engine to run and some of `tools/` to fail.
* **The golden-trace rig is macOS-only by construction.**
  `tools/goldentrace.py` drives the original Windows executable under
  CrossOver and captures frames with `screencapture`, including a 2x Retina
  recovery. Its checks skip without it; the rig itself would need rewriting
  elsewhere.
* **Vulkan reaches the GPU through MoltenVK here.** OMK only ever talks to the
  loader, so a native driver should need no change — but that is a
  reasonable expectation, not a tested one.
* **`scripts/install-deps.sh`'s Linux branches are written, not exercised.**

Reports from other platforms are welcome, and so are the failures: a bug
found by someone building on Linux is worth more than another macOS run.

## Playing it, and looking at it

```sh
cd engine && make play                     # needs SDL2 or SDL3
build/omk-play ../gamedata ../tables       # THE GAME: movies, menu, intro,
                                           # then adventure mode
```

Arrows walk and turn, `RSHIFT` runs, `ENTER` advances a conversation and
chooses a reply, any key skips a movie and `ALT` skips all three.

The same binary is also a free-look viewer for one set, which is how a
rendering question gets answered by eye instead of by metric:

```sh
build/omk-play ../gamedata ../tables --scene Aapkayl     # a set, live
build/omk-play ../gamedata ../tables --scene Aapkayl --vulkan   # on the GPU
```

`W/S` fly, `A/D` strafe, `Q/E` up and down, arrows look, `[`/`]` step the
set's own cameras, `L` cycles the baked vertex light, `V` swaps the software
rasterizer for the Vulkan backend, `P` prints the current camera in a form you
can paste into a check.

And four web viewers that read the data directly rather than through the
replica:

```sh
python3 tools/omkweb.py --port 8752     # http://127.0.0.1:8752
```

`/dialog` plays a conversation — text, branching replies, ADPCM audio, the
speaker posed from the morph data, staged in its set under its own dialogue
cameras. `/cutscene` flies an in-engine cutscene with its ambient particle
emitters. `/ui` drives the game's own menus with its own input words.
`/world` serves the world scripts as annotated listings.

## What is established

Solved and documented with evidence: the `IAM` archives and conversations, the
script VM (153 opcodes, 129 named, 99.97% of executed instructions), textures
(2534/2534 exact), meshes and sets, the animation libraries (243362/243362
unit quaternions), the `.CTL` state machines (7/7 exact, all 2044 graph edges
resolving), the scene scripts (220/220), morph animation and its ADPCM audio
(777/777 sample-identical), the trigger zones (4558, 0 bad), the world scripts
(5785/5785 slots decoding), the game state, the fonts and text layout, the 37
interface screens and their widget tree, the effect sprites, the fight and
shoot AI, and the cutscene camera editings (24112/24112 frames sampling).

`docs/RECONSTRUCTION.md` is the roadmap and the running log — what is left, in
what order. `engine/README.md` audits the port row by row against that list,
and it has been wrong twice, so read it rather than any summary of it.

Open questions are listed in `CLAUDE.md` §6 **with what has already been ruled
out**, so nobody repeats a search.

## How this work is done

`CLAUDE.md` is the working practice, and it is worth reading before the docs.
Its ground rules exist because ignoring each one produced a wrong result that
survived until something else contradicted it. The short version:

* **The data is for finding, the code is for confirming.** Explore the bytes
  freely — but a layout is not established until a loader in the binary says
  so. A wrong layout that happens to fit the shipped bytes looks exactly like
  a right one, and fails silently.
* **Make the parse self-checking.** Prefer a test the data can fail: a walk
  that must land exactly on the file size, a pool consumed in order with no
  gaps, every cross-reference resolving. "It decodes without crashing" is not
  a check — random bytes decode.
* **A value verified standing still is not verified moving.** An angle stored
  near 4096 is a small negative one: identical in every still frame, and a
  full wrong turn once two of them are interpolated. Write the invariant over
  the transition.
* **Look at it.** A suite that only compares this repo to itself cannot see a
  wrong reading applied consistently. Several findings here were corrected by
  somebody watching the viewer, after every number in the repo had agreed with
  every other.
* **Record the dead ends.** A negative result is a finding, and it is only as
  strong as the enumeration behind it.

Every number quoted in `docs/` is asserted by a check in `tools/verify.py`, so
the prose and the code cannot drift apart.

## Built with AI, and why that shapes everything above

OMK was written with heavy use of AI — Claude, via Claude Code. The
sessions in [`transcript/`](transcript/README.md) are the archived record,
kept deliberately: they contain the wrong turns as well as the findings, and
several of the ground rules above are only convincing with the mistake that
produced them still attached.

That is stated here rather than buried, because it changes what a reader
should ask of the work.

**It is also the reason the verification apparatus is as large as it is.** An
LLM produces fluent, confident, plausible answers, and in a format-archaeology
project the failure mode is specific and nasty: *a wrong layout that happens to
fit the shipped bytes looks exactly like a right one.* It decodes without
crashing. It produces round numbers. It reads well. So the rules are not
stylistic — nothing is established until a loader in the binary says so, every
finding that produces a number gets a check, and a check must be **shown** to
fail before it is trusted. `verify.py` exists because the author of most of
this text cannot be taken at its word.

That machinery has earned its cost repeatedly. A `SCENE` chunk count that
parsed beautifully as an int32 is an int16 in `Scene_Load` — the plausible
reading silently rejected 22 of 71 chunks. A confident "5016 scripts" was a
section offset read as a count. An asset sweep globbed `*.3DM` case-sensitively
and reported 708 files where 777 ship.

**And it is worth being equally clear about what the machinery did *not*
catch.** A suite that only compares this repository to itself cannot see a
wrong reading applied *consistently*. The dialogue staging was wrong through
two successive "fixes" while every check passed and every number agreed with
every other; one frame of the running game showed the error in a second. That
is why "look at it" is a rule, why the viewers exist, and why several of the
corrections recorded in `docs/` came from a human flying a camera rather than
from any test here.

So: the findings carry their evidence, and the evidence is the point. Read
that rather than the confidence of the prose around it — and if something here
is wrong, it will most likely be wrong in a way that reads perfectly.

## Licence

Two, split along the code/prose line:

* **code** — `tools/`, `engine/`, `scripts/`, `tables/` —
  **GPL-3.0-or-later** ([`LICENSE`](LICENSE)). Copyleft is the norm for game
  reimplementations, and it keeps a closed commercial fork off the table.
* **prose** — `docs/`, `CLAUDE.md`, `README.md`, `todo/`, `transcript/` —
  **CC-BY-4.0** ([`docs/LICENSE`](docs/LICENSE)). The findings are the part
  most worth quoting elsewhere, and copyleft text would block exactly that.

[`LICENSING.md`](LICENSING.md) has the reasoning, the third-party inventory
(`engine/third_party/pl_mpeg.h` is MIT and is **not** relicensed by any of the
above), and what is deliberately kept out of the published tree.

**These licences cover only what OMK owns.** *Omikron: The Nomad Soul*
is © Quantic Dream / Eidos Interactive. OMK redistributes none of it,
is not affiliated with or endorsed by either, and is published as
documentation of file formats for interoperability. Neither the game data nor
any disassembly of its binary is here, and no licence on this repository would
change their status if they were.
